// Testes do runtime de mods: semver, manifest, registro de conteúdo,
// resolução de dependências e carga real do mod de exemplo.
#include <algorithm>
#include <cstddef>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "fake_game.h"
#include "json.h"
#include "sha256.h"
#include "tlmod/runtime/content_registry.h"
#include "tlmod/runtime/item_persistence.h"
#include "tlmod/runtime/item_shell.h"
#include "tlmod/runtime/library_cache.h"
#include "tlmod/runtime/log.h"
#include "tlmod/runtime/manifest.h"
#include "tlmod/runtime/mod_loader.h"
#include "tlmod/runtime/png.h"

#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

using namespace tlmod::rt;
namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (cond) {                                                                   \
            std::printf("ok    %s\n", #cond);                                         \
        } else {                                                                      \
            std::printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++g_failures;                                                             \
        }                                                                             \
    } while (0)

static void QuietSink(TLModLogLevel, const std::string&, const std::string&) {}

// ---------------------------------------------------------------------------
static void TestSemVer() {
    std::printf("\n== semver\n");
    CHECK(SemVer::Parse("1.2.3").has_value());
    CHECK(!SemVer::Parse("1.2").has_value());
    CHECK(!SemVer::Parse("01.2.3").has_value());
    CHECK(SemVer::Parse("1.0.0-beta")->Compare(*SemVer::Parse("1.0.0")) < 0);
    auto caret = VersionReq::Parse("^1.2.0");
    CHECK(caret && caret->Matches(*SemVer::Parse("1.9.9")) && !caret->Matches(*SemVer::Parse("2.0.0")));
    auto caret0 = VersionReq::Parse("^0.3.1");
    CHECK(caret0 && caret0->Matches(*SemVer::Parse("0.3.5")) && !caret0->Matches(*SemVer::Parse("0.4.0")));
    auto tilde = VersionReq::Parse("~1.2.3");
    CHECK(tilde && tilde->Matches(*SemVer::Parse("1.2.9")) && !tilde->Matches(*SemVer::Parse("1.3.0")));
    auto range = VersionReq::Parse(">=1.0 <1.5");
    CHECK(range && range->Matches(*SemVer::Parse("1.4.0")) && !range->Matches(*SemVer::Parse("1.5.0")));
    CHECK(VersionReq::Parse("*")->Matches(*SemVer::Parse("9.9.9")));
    CHECK(!VersionReq::Parse(">=abc").has_value());
}

// ---------------------------------------------------------------------------
static const char* kGoodManifest = R"({
  "tlmod_api": 1, "id": "acme.swords", "name": "Espadas", "version": "1.4.0",
  "dependencies": {"acme.core": "^2.0.0"},
  "optional_dependencies": {"acme.music": "*"},
  "content": {"items": ["iron_katana", "gold_katana"], "projectiles": ["slash"]},
  "entry": {"linux-x86_64": "lib/linux-x86_64/libswords.so"}
})";

static void TestManifest() {
    std::printf("\n== manifest\n");
    std::string err;
    auto m = ParseManifest(kGoodManifest, &err);
    CHECK(m.has_value());
    if (m) {
        CHECK(m->id == "acme.swords" && m->version.ToString() == "1.4.0");
        CHECK(m->dependencies.size() == 2 && !m->dependencies[0].optional && m->dependencies[1].optional);
        CHECK(m->Declares(TLMOD_CONTENT_ITEM, "gold_katana"));
        CHECK(m->Declares(TLMOD_CONTENT_PROJECTILE, "slash"));
        CHECK(!m->Declares(TLMOD_CONTENT_ITEM, "slash"));
    }
    auto bad = [](const std::string& json, const std::string& expectInError) {
        std::string e;
        bool rejected = !ParseManifest(json, &e).has_value();
        bool mentions = e.find(expectInError) != std::string::npos;
        if (!rejected || !mentions) std::printf("      erro recebido: %s\n", e.c_str());
        return rejected && mentions;
    };
    const std::string head = R"("tlmod_api":1,"name":"x","version":"1.0.0",)";
    const std::string entry = R"("entry":{"linux-x86_64":"lib/a.so"})";
    CHECK(bad("{" + head + R"("id":"Bad Id",)" + entry + "}", "id"));
    CHECK(bad("{" + head + R"("id":"a.b"})", "entry"));
    CHECK(bad("{" + head + R"("id":"a.b","entry":{"linux-x86_64":"../../etc/x.so"}})", "entry.linux-x86_64"));
    CHECK(bad("{" + head + R"("id":"a.b","content":{"spells":["x"]},)" + entry + "}", "content.spells"));
    CHECK(bad("{" + head + R"("id":"a.b","content":{"items":["x","x"]},)" + entry + "}", "repetido"));
    CHECK(bad("{" + head + R"("id":"a.b","dependencies":{"a.b":"*"},)" + entry + "}", "si mesmo"));
    CHECK(bad("{" + head + R"("id":"a.b","dependencies":{"c":">>1"},)" + entry + "}", "faixa"));
    CHECK(bad(R"({"tlmod_api":1,"id":"a","id":"b"})", "duplicada"));
    CHECK(bad("{" + head + R"("id":"a.b",)" + entry, "JSON"));
    CHECK(IsValidModId("tlmod.example_item") && !IsValidModId(".x") && !IsValidModId("a..b"));
}

// ---------------------------------------------------------------------------
static void TestRegistry() {
    std::printf("\n== registro de conteúdo\n");
    FakeGame game;
    ContentRegistry reg(&game);
    std::string err;

    // IDs de mod começam acima de todos os originais do dump.
    CHECK(reg.VanillaCount(TLMOD_CONTENT_ITEM) == 5456);
    CHECK(reg.Range(TLMOD_CONTENT_ITEM).firstModId > 5456);
    auto a = reg.Register(TLMOD_CONTENT_ITEM, "acme.swords", "iron_katana", ItemDefinition{}, &err);
    auto b = reg.Register(TLMOD_CONTENT_ITEM, "acme.swords", "gold_katana", ItemDefinition{}, &err);
    CHECK(a == 8192 && b == 8193);
    auto buff = reg.Register(TLMOD_CONTENT_BUFF, "acme.swords", "bleeding", {}, &err);
    CHECK(buff == 4096);
    CHECK(reg.Find(TLMOD_CONTENT_ITEM, "acme.swords:gold_katana") == 8193);
    CHECK(reg.Get(TLMOD_CONTENT_ITEM, 8192)->key == "acme.swords:iron_katana");
    CHECK(reg.Get(TLMOD_CONTENT_ITEM, 5455) == nullptr);  // original: não é do registro
    CHECK(!reg.IsModId(TLMOD_CONTENT_ITEM, 100));

    CHECK(!reg.Register(TLMOD_CONTENT_ITEM, "acme.swords", "iron_katana", {}, &err));
    CHECK(err.find("já registrado") != std::string::npos);
    // Mesmo nome em outro mod não colide: a chave inclui o id do mod.
    CHECK(reg.Register(TLMOD_CONTENT_ITEM, "other.mod", "iron_katana", {}, &err) == 8194);
    CHECK(!reg.Register(TLMOD_CONTENT_ITEM, "acme.swords", "Iron Katana", {}, &err));

    auto cp = reg.Mark();
    reg.Register(TLMOD_CONTENT_ITEM, "acme.swords", "temp", {}, &err);
    reg.Rollback(cp);
    CHECK(!reg.Find(TLMOD_CONTENT_ITEM, "acme.swords:temp"));
    CHECK(reg.Register(TLMOD_CONTENT_ITEM, "acme.swords", "after_rollback", {}, &err) == 8195);

    // Faixa esgotada.
    auto ranges = ContentRegistry::DefaultRanges();
    ranges[TLMOD_CONTENT_NPC] = {"npc", 688, 1000, 1001};
    ContentRegistry small(&game, ranges);
    CHECK(small.Register(TLMOD_CONTENT_NPC, "m", "a", {}, &err) == 1000);
    CHECK(small.Register(TLMOD_CONTENT_NPC, "m", "b", {}, &err) == 1001);
    CHECK(!small.Register(TLMOD_CONTENT_NPC, "m", "c", {}, &err));
    CHECK(err.find("esgotada") != std::string::npos);

    // Jogo atualizado com mais itens que a folga: tipo desativado, nunca colide.
    FakeGame bigGame;
    bigGame.vanilla[TLMOD_CONTENT_ITEM] = 9000;
    ContentRegistry guarded(&bigGame);
    CHECK(!guarded.KindEnabled(TLMOD_CONTENT_ITEM));
    CHECK(!guarded.Register(TLMOD_CONTENT_ITEM, "m", "x", {}, &err));
    CHECK(guarded.KindEnabled(TLMOD_CONTENT_TILE));
}

// ---------------------------------------------------------------------------
static ModRecord Rec(const std::string& id, const std::string& ver,
                     std::vector<std::tuple<std::string, std::string, bool>> deps = {}) {
    ModRecord r;
    r.manifest.apiVersion = TLMOD_API_VERSION;
    r.manifest.id = id;
    r.manifest.name = id;
    r.manifest.version = *SemVer::Parse(ver);
    for (auto& [d, req, opt] : deps) r.manifest.dependencies.push_back({d, *VersionReq::Parse(req), opt});
    return r;
}

static const ModRecord* Get(const std::vector<ModRecord>& v, const std::string& id) {
    for (const auto& m : v)
        if (m.manifest.id == id) return &m;
    return nullptr;
}

static void TestResolver() {
    std::printf("\n== dependências\n");
    std::vector<ModRecord> mods;
    mods.push_back(Rec("b", "1.0.0", {{"a", "^1.0.0", false}}));
    mods.push_back(Rec("a", "1.3.0"));
    mods.push_back(Rec("c", "1.0.0", {{"missing", "*", false}}));
    mods.push_back(Rec("d", "1.0.0", {{"a", "^2.0.0", false}}));
    mods.push_back(Rec("e", "1.0.0", {{"f", "*", false}}));
    mods.push_back(Rec("f", "1.0.0", {{"e", "*", false}}));
    mods.push_back(Rec("g", "1.0.0", {{"missing", "*", true}}));
    mods.push_back(Rec("h", "1.0.0", {{"c", "*", false}}));
    ModRecord old = Rec("i", "1.0.0");
    old.manifest.apiVersion = 99;
    mods.push_back(old);

    auto order = ModLoader::ResolveOrder(mods);
    CHECK((order == std::vector<std::string>{"a", "b", "g"}));
    CHECK(Get(mods, "c")->state == ModState::Rejected && Get(mods, "c")->error.find("missing") != std::string::npos);
    CHECK(Get(mods, "d")->state == ModState::Rejected && Get(mods, "d")->error.find("1.3.0") != std::string::npos);
    CHECK(Get(mods, "e")->error == "ciclo de dependências" && Get(mods, "f")->error == "ciclo de dependências");
    CHECK(Get(mods, "h")->state == ModState::Rejected);  // c foi rejeitado -> h também
    CHECK(Get(mods, "i")->state == ModState::Rejected && Get(mods, "i")->error.find("tlmod_api") != std::string::npos);
    for (const auto& m : mods)
        if (m.state == ModState::Rejected) std::printf("      %s: %s\n", m.manifest.id.c_str(), m.error.c_str());
}

// ---------------------------------------------------------------------------
// Mods embutidos usados no teste de carga.
static int g_postCalls = 0;
static int32_t g_lastLifeAfter = -1;

static bool HalveDamage(void*, TLModPlayerHurt* ev, bool after) {
    if (after) {
        ++g_postCalls;
        g_lastLifeAfter = ev->life_after;
        return true;
    }
    if (ev->damage > 1000) return false;  // cancela golpes absurdos
    ev->damage /= 2;
    return true;
}

static int HalverInit(const TLModHostApi* api, TLModContext* ctx) {
    return api->subscribe_player_hurt(ctx, &HalveDamage, nullptr) ? 0 : 1;
}

// Registra um item declarado, tenta um não declarado e falha: o runtime deve
// desfazer o registro do primeiro.
static int BadInit(const TLModHostApi* api, TLModContext* ctx) {
    TLModItemDef def{};
    def.struct_size = sizeof(def);
    def.name = "declared_item";
    def.display_name = "Declarado";
    def.width = def.height = 10;
    def.max_stack = 1;
    if (api->register_item(ctx, &def) == TLMOD_INVALID_ID) return 2;
    def.name = "undeclared_item";
    if (api->register_item(ctx, &def) != TLMOD_INVALID_ID) return 3;
    return 1;
}

static ModManifest Builtin(const std::string& id, std::vector<std::string> items = {}) {
    ModManifest m;
    m.apiVersion = TLMOD_API_VERSION;
    m.id = id;
    m.name = id;
    m.version = *SemVer::Parse("1.0.0");
    m.content[TLMOD_CONTENT_ITEM] = std::move(items);
    return m;
}

static void TestLoader(const fs::path& modsDir) {
    std::printf("\n== carga (mod de exemplo + embutidos)\n");

    // Pasta temporária com o mod de exemplo compilado + um mod quebrado.
    fs::path tmp = fs::temp_directory_path() / "tlmod_runtime_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "broken");
    fs::copy(modsDir / "example_item", tmp / "example_item", fs::copy_options::recursive);
    std::ofstream(tmp / "broken" / "manifest.json") << "{ \"tlmod_api\": 1, ";

    FakeGame game;
    ModLoader loader(&game);
    loader.DiscoverDirectory(tmp);
    loader.AddBuiltin(Builtin("test.hurt_halver"), &HalverInit);
    loader.AddBuiltin(Builtin("test.bad", {"declared_item"}), &BadInit);
    size_t n = loader.LoadAll();

    for (const auto& m : loader.Mods())
        std::printf("      %-22s %-10s %s\n", m.manifest.id.c_str(), ToString(m.state), m.error.c_str());

    CHECK(n == 2);
    const ModRecord* ex = loader.Find("tlmod.example_item");
    CHECK(ex && ex->state == ModState::Loaded);
    CHECK(loader.Find("broken")->state == ModState::Rejected);
    CHECK(loader.Find("test.bad")->state == ModState::Failed);
    // Sem dependências entre eles: a ordem é a dos ids (vale para qualquer prefixo).
    std::vector<std::string> expectedOrder{ex ? ex->manifest.id : "", "test.bad", "test.hurt_halver"};
    std::sort(expectedOrder.begin(), expectedOrder.end());
    CHECK(loader.LoadOrder() == expectedOrder);

    auto rubyId = loader.Registry().Find(TLMOD_CONTENT_ITEM, "tlmod.example_item:fictional_ruby");
    CHECK(rubyId == 8192);
    if (rubyId) {
        const ContentEntry* e = loader.Registry().Get(TLMOD_CONTENT_ITEM, *rubyId);
        CHECK(e && e->Item() && e->Item()->displayName == "Rubi Fictício" && e->Item()->maxStack == 999);
    }
    // O item do mod que falhou foi desfeito.
    CHECK(!loader.Registry().Find(TLMOD_CONTENT_ITEM, "test.bad:declared_item"));
    CHECK(loader.Registry().Entries(TLMOD_CONTENT_ITEM).size() == 1);

    // find_content pela API C, como um mod faria.
    const TLModHostApi& api = loader.HostApi();
    CHECK(api.api_version == TLMOD_API_VERSION && api.struct_size == sizeof(TLModHostApi));

    // Evento: o "jogo" aplica 100 de dano; o mod corta para 50.
    CHECK(game.Hurt(0, 100) == 50);
    CHECK(game.life[0] == 350 && g_postCalls == 1 && g_lastLifeAfter == 350);
    CHECK(game.Hurt(0, 5000) == 0 && game.life[0] == 350);  // cancelado

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// NPCs: carrega o example_boss real + um mod embutido que testa validações.
static const TLModHostApi* g_npcApi = nullptr;
static TLModContext* g_npcCtx = nullptr;

static int NpcValidationInit(const TLModHostApi* api, TLModContext* ctx) {
    g_npcApi = api;
    g_npcCtx = ctx;
    TLModNpcDef d{};
    d.struct_size = sizeof(d);
    d.display_name = "Lacaio";
    d.life_max = 0;  // mantém o da casca
    d.damage = d.defense = -1;
    d.name = "undeclared";
    d.shell_type = 4;
    if (api->register_npc(ctx, &d) != TLMOD_INVALID_ID) return 2;  // fora de content.npcs
    d.name = "minion";
    d.shell_type = 4096;
    if (api->register_npc(ctx, &d) != TLMOD_INVALID_ID) return 3;  // casca precisa ser original
    d.shell_type = 0;
    if (api->register_npc(ctx, &d) != TLMOD_INVALID_ID) return 4;
    d.shell_type = 1;
    return api->register_npc(ctx, &d) == TLMOD_INVALID_ID ? 5 : 0;
}

static void TestNpcs(const fs::path& modsDir) {
    std::printf("\n== NPCs (example_boss + ganchos NewNPC/SetDefaults/AI)\n");
    fs::path tmp = fs::temp_directory_path() / "tlmod_npc_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    fs::copy(modsDir / "example_boss", tmp / "example_boss", fs::copy_options::recursive);

    FakeGame game;
    ModLoader loader(&game);
    loader.DiscoverDirectory(tmp);
    ModManifest m = Builtin("test.npcs");
    m.content[TLMOD_CONTENT_NPC] = {"minion"};
    loader.AddBuiltin(m, &NpcValidationInit);
    CHECK(loader.LoadAll() == 2);
    for (const auto& r : loader.Mods())
        if (r.state != ModState::Loaded) std::printf("      %s: %s\n", r.manifest.id.c_str(), r.error.c_str());

    // Os IDs seguem a ordem de carga, que desempata pelo id do mod: quem vem
    // antes em ordem alfabética fica com 4096 (independe do prefixo do projeto).
    const std::string bossMod = "tlmod.example_boss";
    const bool bossFirst = bossMod < std::string("test.npcs");
    const TLModId bossId =
        loader.Registry().Find(TLMOD_CONTENT_NPC, bossMod + ":ember_titan").value_or(TLMOD_INVALID_ID);
    const TLModId minionId = loader.Registry().Find(TLMOD_CONTENT_NPC, "test.npcs:minion").value_or(TLMOD_INVALID_ID);
    CHECK(bossId == (bossFirst ? 4096 : 4097) && minionId == (bossFirst ? 4097 : 4096));
    CHECK(loader.Registry().Get(TLMOD_CONTENT_NPC, bossId)->Npc()->shellType == 4);

    // NPC original: nenhum gancho interfere.
    CHECK(game.NewNpc(2, 0, 0) == 0 && loader.NpcInSlot(0) == TLMOD_INVALID_ID);

    // Boss pelo caminho do jogo (como um item de invocação chamando NewNPC).
    CHECK(game.NewNpc(bossId, 500, 600) == 1);
    const TLModNpcState& boss = game.npcs[1].s;
    CHECK(boss.type == 4 && loader.NpcInSlot(1) == bossId);  // jogo vê a casca; runtime sabe que é o boss
    CHECK(boss.life == 12000 && boss.life_max == 12000 && boss.damage == 40 && boss.defense == 14);
    CHECK(boss.x == 500.0f && boss.y == 600.0f);

    // Introdução: 120 ticks em que o mod assume a AI (a da casca não roda).
    game.npcs[1].s.vx = 3.0f;
    game.UpdateNpcs();
    CHECK(game.npcs[0].s.ai[0] == 1.0f);                      // original rodou
    CHECK(boss.ai[0] == 0.0f && boss.vx == 0.0f);             // mod assumiu e parou o boss
    for (int t = 1; t < 120; ++t) game.UpdateNpcs();
    CHECK(boss.ai[0] == 0.0f);
    // Tick 121: fase 1 devolve o controle à AI da casca. Prova também que
    // local[] (fase/timer) persistiu entre ticks no runtime.
    game.UpdateNpcs();
    CHECK(boss.ai[0] == 1.0f);

    // Transform/reuso do slot: SetDefaults desmarca, o mod para de rodar.
    game.SetDefaults(1, 5);
    CHECK(loader.NpcInSlot(1) == TLMOD_INVALID_ID);

    // Spawn pela API do mod: passa pelo mesmo remap.
    int32_t idx = g_npcApi->spawn_npc(g_npcCtx, minionId, 10, 20);
    CHECK(idx == 2 && loader.NpcInSlot(2) == minionId && game.npcs[2].s.type == 1);
    CHECK(game.npcs[2].s.life_max == 101);  // life_max 0 = o da casca
    CHECK(g_npcApi->spawn_npc(g_npcCtx, 9999, 0, 0) == -1);
    CHECK(g_npcApi->spawn_npc(g_npcCtx, 3, 0, 0) == 3 && loader.NpcInSlot(3) == TLMOD_INVALID_ID);

    // Sem slot livre: o spawn falha e nada é marcado.
    while (game.NewNpc(2, 0, 0) >= 0) {
    }
    CHECK(g_npcApi->spawn_npc(g_npcCtx, bossId, 0, 0) == -1);

    // Em nenhum momento um ID de mod chegou ao SetDefaults original.
    CHECK(!game.vanillaSetDefaultsOverflow);

    // O teste de aparelho do example_boss: com o arquivo spawn_on_start, o
    // boss nasce perto do jogador depois de 120 frames seguidos num mundo com
    // ele vivo, uma vez só.
    {
        std::ofstream(tmp / "example_boss" / "spawn_on_start").put('\n');
        FakeGame g2;
        ModLoader l2(&g2);
        l2.DiscoverDirectory(tmp);
        CHECK(l2.LoadAll() == 1);
        const TLModId boss2 = l2.Registry().Find(TLMOD_CONTENT_NPC, bossMod + ":ember_titan").value_or(-1);
        auto bossSlots = [&g2] {
            int n = 0;
            for (const auto& npc : g2.npcs) n += npc.active ? 1 : 0;
            return n;
        };
        g2.inWorld = false;
        for (int f = 0; f < 200; ++f) g2.Frame();  // menu
        CHECK(bossSlots() == 0);
        g2.inWorld = true;
        for (int f = 0; f < 100; ++f) g2.Frame();
        g2.playerCenter.reset();  // morreu: a contagem recomeça
        g2.Frame();
        g2.playerCenter = std::make_pair(800.0f, 400.0f);
        for (int f = 0; f < 119; ++f) g2.Frame();
        CHECK(bossSlots() == 0);
        g2.Frame();
        CHECK(bossSlots() == 1 && l2.NpcInSlot(0) == boss2 && g2.npcs[0].s.type == 4);
        CHECK(g2.npcs[0].s.x == 1280.0f && g2.npcs[0].s.y == 160.0f);
        CHECK(g2.npcs[0].s.life_max == 12000);
        for (int f = 0; f < 300; ++f) g2.Frame();
        CHECK(bossSlots() == 1 && !g2.vanillaSetDefaultsOverflow);  // uma vez só
    }
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Boss Checklist: carrega a .so real e confere o JSON exposto à interface.
namespace bc {

using tlmod::rt::json::Value;

const Value* Entry(const Value& doc, const std::string& key) {
    for (const Value& e : *doc.Get("entries")->AsArray())
        if (*e.Get("key")->AsString() == key) return &e;
    return nullptr;
}
std::string Status(const Value& doc, const std::string& key) {
    const Value* e = Entry(doc, key);
    return e ? *e->Get("status")->AsString() : "?";
}
int Count(const Value& doc, const char* group, const char* field) {
    return static_cast<int>(*doc.Get("summary")->Get(group)->Get(field)->AsNumber());
}
std::string NextKey(const Value& doc) {
    const Value* n = doc.Get("next");
    return n->IsNull() ? "null" : *n->Get("key")->AsString();
}
std::string Tri(const Value* v) { return v->IsNull() ? "null" : (*v->AsBool() ? "true" : "false"); }

// Builtins para testar a proteção do runtime em volta dos provedores.
bool BadJson(void*, TLModBuffer* out);
bool Refuse(void*, TLModBuffer*) { return false; }
const TLModHostApi* g_api = nullptr;
bool BadJson(void*, TLModBuffer* out) {
    const char text[] = "{\"bosses\": [1, 2";  // JSON truncado
    g_api->buffer_append(out, text, sizeof(text) - 1);
    return true;
}
int ProvidersInit(const TLModHostApi* api, TLModContext* ctx) {
    g_api = api;
    if (api->register_provider(ctx, "undeclared", &Refuse, nullptr)) return 2;  // fora de "provides"
    if (!api->register_provider(ctx, "broken", &BadJson, nullptr)) return 3;
    if (!api->register_provider(ctx, "refuses", &Refuse, nullptr)) return 4;
    return 0;
}

}  // namespace bc

static void TestBossChecklist(const fs::path& modsDir) {
    std::printf("\n== Boss Checklist (mod real, jogo simulado)\n");
    fs::path tmp = fs::temp_directory_path() / "tlmod_checklist_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    fs::copy(modsDir / "boss_checklist", tmp / "boss_checklist", fs::copy_options::recursive);

    FakeGame game;
    ModLoader loader(&game);
    loader.DiscoverDirectory(tmp);
    ModManifest m = Builtin("test.providers");
    m.provides = {"broken", "refuses"};
    loader.AddBuiltin(m, &bc::ProvidersInit);
    CHECK(loader.LoadAll() == 2);
    auto providers = loader.Providers();
    // Em ordem de id (vale para qualquer prefixo do projeto).
    std::vector<std::string> expectedProviders{"tlmod.boss_checklist/checklist", "test.providers/broken",
                                               "test.providers/refuses"};
    std::sort(expectedProviders.begin(), expectedProviders.end());
    CHECK(providers == expectedProviders);

    auto query = [&](const char* label) {
        std::string err;
        auto text = loader.Query("tlmod.boss_checklist", "checklist", &err);
        auto doc = text ? tlmod::rt::json::Parse(*text, &err) : std::nullopt;
        if (!doc) std::printf("      erro: %s\n", err.c_str());
        if (text && std::getenv("TLMOD_PRINT_JSON")) std::printf("JSON[%s] %s\n", label, text->c_str());
        return doc;
    };

    // 1) O jogo ainda não rodou um frame: nada é conhecido.
    auto d1 = query("sem_frame");
    CHECK(d1 && bc::Tri(d1->Get("in_world")) == "null" && bc::Tri(d1->Get("hardmode")) == "null");
    CHECK(d1 && bc::Count(*d1, "bosses", "unknown") == 17 && bc::NextKey(*d1) == "null");
    CHECK(d1 && d1->Get("entries")->AsArray()->size() == 30);

    // 2) Início de jogo: 4 bosses pré-hardmode e o Exército Goblin derrotados.
    game.inWorld = true;
    for (auto& p : game.progress) p = false;
    for (TLModProgress f : {TLMOD_DOWNED_KING_SLIME, TLMOD_DOWNED_EYE_OF_CTHULHU, TLMOD_DOWNED_EVIL_BOSS,
                             TLMOD_DOWNED_QUEEN_BEE, TLMOD_DOWNED_GOBLIN_ARMY})
        game.progress[f] = true;
    auto d2 = query("inicio");
    CHECK(d2 && bc::Tri(d2->Get("in_world")) == "true" && bc::Tri(d2->Get("hardmode")) == "false");
    CHECK(d2 && bc::Count(*d2, "bosses", "defeated") == 4 && bc::Count(*d2, "bosses", "pending") == 13);
    CHECK(d2 && bc::Count(*d2, "events", "defeated") == 1 && bc::Count(*d2, "events", "total") == 4);
    CHECK(d2 && bc::Count(*d2, "minibosses", "pending") == 9);
    CHECK(d2 && bc::Status(*d2, "evil_boss") == "defeated" && bc::Status(*d2, "skeletron") == "pending");
    CHECK(d2 && bc::Status(*d2, "wall_of_flesh") == "pending");
    CHECK(d2 && bc::NextKey(*d2) == "skeletron");

    // 3) Hardmode: Wall of Flesh (via hardMode) e as três mecânicas caídas;
    //    Deerclops "desconhecido" (como uma flag que não resolveu nesta versão).
    for (TLModProgress f : {TLMOD_PROGRESS_HARDMODE, TLMOD_DOWNED_SKELETRON, TLMOD_DOWNED_TWINS,
                             TLMOD_DOWNED_DESTROYER, TLMOD_DOWNED_SKELETRON_PRIME, TLMOD_DOWNED_PIRATES})
        game.progress[f] = true;
    game.progress[TLMOD_DOWNED_DEERCLOPS] = std::nullopt;
    auto d3 = query("hardmode");
    CHECK(d3 && bc::Tri(d3->Get("hardmode")) == "true" && bc::Status(*d3, "wall_of_flesh") == "defeated");
    CHECK(d3 && bc::Count(*d3, "bosses", "defeated") == 9 && bc::Count(*d3, "bosses", "unknown") == 1 &&
          bc::Count(*d3, "bosses", "pending") == 7);
    CHECK(d3 && bc::Status(*d3, "deerclops") == "unknown");
    CHECK(d3 && bc::NextKey(*d3) == "queen_slime");  // desconhecido não vira "próximo"

    // 4) De volta ao menu: as flags ainda guardam o último mundo, mas a lista
    //    não deve mostrá-las.
    game.inWorld = false;
    auto d4 = query("menu");
    CHECK(d4 && bc::Tri(d4->Get("in_world")) == "false" && bc::Count(*d4, "bosses", "unknown") == 17);
    CHECK(d4 && bc::Status(*d4, "king_slime") == "unknown" && bc::NextKey(*d4) == "null");

    // Proteção do runtime: provedor com JSON inválido ou que recusa não chega à UI.
    std::string err;
    CHECK(!loader.Query("test.providers", "broken", &err) && err.find("JSON") != std::string::npos);
    CHECK(!loader.Query("test.providers", "refuses", &err) && err.find("erro") != std::string::npos);
    CHECK(!loader.Query("tlmod.boss_checklist", "nope", &err) && err.find("não existe") != std::string::npos);

    // Consultas de outras threads (como a UI faria) enquanto o estado muda.
    game.inWorld = true;
    std::vector<std::thread> uis;
    std::atomic<int> okQueries{0};
    for (int t = 0; t < 4; ++t)
        uis.emplace_back([&] {
            for (int i = 0; i < 50; ++i)
                if (loader.Query("tlmod.boss_checklist", "checklist")) ++okQueries;
        });
    for (auto& t : uis) t.join();
    CHECK(okQueries == 200);
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Itens de mod no save: o arquivo do jogo só vê IDs originais; o resto vai
// para o arquivo paralelo pela chave "mod_id:nome".
namespace saves {

using Item = FakeGame::Item;

TLModId Reg(const TLModHostApi* api, TLModContext* ctx, const char* name, int32_t maxStack) {
    TLModItemDef d{};
    d.struct_size = sizeof(d);
    d.name = name;
    d.display_name = name;
    d.width = d.height = 16;
    d.max_stack = maxStack;
    return api->register_item(ctx, &d);
}
int GemsInit(const TLModHostApi* api, TLModContext* ctx) {
    return Reg(api, ctx, "ruby", 999) != TLMOD_INVALID_ID && Reg(api, ctx, "topaz", 99) != TLMOD_INVALID_ID ? 0 : 1;
}
int ToolsInit(const TLModHostApi* api, TLModContext* ctx) { return Reg(api, ctx, "drill", 1) != TLMOD_INVALID_ID ? 0 : 1; }
int FirstInit(const TLModHostApi* api, TLModContext* ctx) { return Reg(api, ctx, "first", 1) != TLMOD_INVALID_ID ? 0 : 1; }

const std::string kRuby = "test.gems:ruby", kTopaz = "test.gems:topaz", kDrill = "test.tools:drill";
const std::string kFictionalRuby = "tlmod.example_item:fictional_ruby";  // o mod de exemplo real (.so)

// Uma "abertura do jogo": jogo novo, runtime novo, conjunto de mods dado.
struct Session {
    FakeGame game;
    ModLoader loader{&game};
    Session(const fs::path& realMods, std::initializer_list<const char*> builtins, bool load = true) {
        if (!realMods.empty()) loader.DiscoverDirectory(realMods);
        for (const char* id : builtins) {
            std::string s = id;
            if (s == "test.gems") loader.AddBuiltin(Builtin(s, {"ruby", "topaz"}), &GemsInit);
            if (s == "test.tools") loader.AddBuiltin(Builtin(s, {"drill"}), &ToolsInit);
            if (s == "test.aaa") loader.AddBuiltin(Builtin(s, {"first"}), &FirstInit);
        }
        if (load) loader.LoadAll();
    }
    // Ao fechar a "abertura do jogo": a interface desenha tudo o que ficou, e
    // nenhuma tabela do tamanho original pode ter sido lida com um ID de mod.
    ~Session() {
        try {
            game.DrawAll();
        } catch (const std::out_of_range&) {
        }
        CHECK(game.tableOverflows == 0);
    }
    TLModId Id(const std::string& key) const {
        return loader.Registry().Find(TLMOD_CONTENT_ITEM, key).value_or(TLMOD_INVALID_ID);
    }
    // Item de mod: type é a casca, netID a identidade.
    std::string Key(const Item& it) const {
        const ContentEntry* e = loader.Registry().Get(TLMOD_CONTENT_ITEM, it.netID);
        return e ? e->key : (it.IsAir() ? "ar" : std::to_string(it.netID));
    }
    Item Make(const std::string& key, int32_t stack, uint8_t prefix = 0, bool fav = false) {
        return game.NewItem(Id(key), stack, prefix, fav);
    }
    const FakeGame::Chest* ChestAt(int32_t x, int32_t y) const {
        for (const auto& c : game.chests)
            if (c && c->x == x && c->y == y) return &*c;
        return nullptr;
    }
    // Quantas unidades da chave existem no personagem + mundo (nada sumiu, nada duplicou).
    int Count(const std::string& key) const {
        int n = 0;
        auto add = [&](const Item& it) { n += Key(it) == key ? it.stack : 0; };
        const FakeGame::Player& p = game.player;
        for (const Item& it : p.inventory) add(it);
        for (const Item& it : p.armor) add(it);
        for (const Item& it : p.dye) add(it);
        for (const Item& it : p.miscEquips) add(it);
        for (const Item& it : p.miscDyes) add(it);
        for (const auto& b : p.banks) for (const Item& it : b) add(it);
        for (const auto& l : p.loadoutArmor) for (const Item& it : l) add(it);
        for (const auto& l : p.loadoutDye) for (const Item& it : l) add(it);
        for (const Item& it : p.temporary) add(it);
        for (const Item& it : game.held) add(it);
        for (const auto& c : game.chests)
            if (c) for (const Item& it : c->item) add(it);
        return n;
    }
};

std::vector<SavedModItem> Sidecar(const std::string& path) {
    std::string err;
    auto v = ItemPersistence::ReadSidecar(path + ItemPersistence::kSidecarSuffix, &err);
    if (!v) std::printf("      erro: %s\n", err.c_str());
    return v.value_or(std::vector<SavedModItem>{});
}
size_t CountOrphans(const std::vector<SavedModItem>& v, const std::string& why) {
    return static_cast<size_t>(std::count_if(v.begin(), v.end(), [&](const SavedModItem& e) { return e.orphan == why; }));
}
std::string ReadAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
bool Same(const Item& a, const Item& b) {
    return a.type == b.type && a.stack == b.stack && a.prefix == b.prefix && a.favorited == b.favorited;
}

}  // namespace saves

static void TestSaves(const fs::path& modsDir) {
    using namespace saves;
    std::printf("\n== itens de mod no save (arquivo paralelo)\n");
    fs::path dir = fs::temp_directory_path() / "tlmod_saves_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "mods");
    fs::copy(modsDir / "example_item", dir / "mods" / "example_item", fs::copy_options::recursive);
    const fs::path mods = dir / "mods";
    const std::string plr = (dir / "Jogador.plr").string(), wld = (dir / "Mundo.wld").string();
    const std::string plrSide = plr + ItemPersistence::kSidecarSuffix, wldSide = wld + ItemPersistence::kSidecarSuffix;

    // 0) Sem o TLMod: o ID de mod vai para o arquivo e o item some no load
    //    (SetDefaults troca type >= 5456 por ar). Num baú, SaveChests estoura.
    {
        FakeGame g;
        g.player.armor[0] = Item::Raw(8192, 1);
        g.player.inventory[1] = Item::Raw(8193, 5);
        CHECK(g.SavePlayer(plr) && g.lastSavedMaxId == 8193);
        CHECK(g.LoadPlayer(plr) && g.player.armor[0].IsAir() && g.player.inventory[1].IsAir());  // sumiram
        g.chests[0] = FakeGame::Chest{10, 20, {}};
        g.chests[0]->item[0] = Item::Raw(8192, 1);
        CHECK(!g.SaveWorld(wld) && g.tableOverflows > 0);  // o mundo não salva
        fs::remove(plr);
    }

    // 1) Primeira abertura: salvar com itens de mod em todo tipo de lugar.
    TLModId rubyId1 = 0, topazId1 = 0;
    {
        Session s(mods, {"test.gems", "test.tools"});
        rubyId1 = s.Id(kRuby);
        topazId1 = s.Id(kTopaz);
        CHECK(rubyId1 >= 8192 && topazId1 >= 8192 && s.Id(kDrill) >= 8192 && s.Id(kFictionalRuby) >= 8192);
        FakeGame::Player& p = s.game.player;
        p.inventory[3] = s.Make(kRuby, 5, 7, true);
        p.inventory[10] = Item::Raw(2, 99);  // original (terra)
        p.armor[0] = s.Make(kTopaz, 1);
        p.loadoutArmor[1][2] = s.Make(kDrill, 1);
        p.banks[3][5] = s.Make(kTopaz, 3, 0, true);  // Cofre do Vazio
        s.game.held[0] = s.Make(kFictionalRuby, 1);  // no mouse
        s.game.chests[0] = FakeGame::Chest{100, 200, {}};
        s.game.chests[0]->item[0] = s.Make(kDrill, 1);
        s.game.chests[0]->item[1] = Item::Raw(8, 10);  // tochas
        s.game.chests[5] = FakeGame::Chest{300, 50, {}};
        s.game.chests[5]->item[39] = s.Make(kRuby, 20);
        const FakeGame::Player before = p;

        CHECK(s.game.SavePlayer(plr) && s.game.lastSavedMaxId == 2);  // só IDs originais no arquivo
        CHECK(s.game.SaveWorld(wld) && s.game.lastSavedMaxId == 8);
        // Depois do save, tudo de volta no lugar.
        CHECK(Same(p.inventory[3], before.inventory[3]) && Same(p.armor[0], before.armor[0]));
        CHECK(Same(p.loadoutArmor[1][2], before.loadoutArmor[1][2]) && Same(p.banks[3][5], before.banks[3][5]));
        CHECK(s.Key(s.game.held[0]) == kFictionalRuby && s.Key(s.game.chests[5]->item[39]) == kRuby);
        CHECK(fs::exists(plrSide) && fs::exists(wldSide));
        auto side = Sidecar(plr);
        CHECK(side.size() == 5 && CountOrphans(side, "") == 5);
        CHECK(Sidecar(wld).size() == 2);
        CHECK(ReadAll(plrSide).find("\"key\":\"test.gems:ruby\",\"container\":\"inventory\",\"slot\":3,\"stack\":5,"
                                    "\"prefix\":7,\"favorited\":true") != std::string::npos);
        CHECK(ReadAll(wldSide).find("\"container\":\"chest\",\"x\":300,\"y\":50,\"slot\":39") != std::string::npos);

        // O jogo sozinho lê os dois arquivos: válidos e sem nada de mod.
        FakeGame vanilla;
        CHECK(vanilla.LoadPlayer(plr) && vanilla.player.armor[0].IsAir() && vanilla.player.inventory[3].IsAir());
        CHECK(vanilla.player.inventory[10].type == 2 && vanilla.player.inventory[10].stack == 99);
        CHECK(vanilla.LoadWorld(wld) && vanilla.chests[1]->item[39].IsAir() && vanilla.chests[0]->item[1].type == 8);
    }

    // 2) Outra ordem de mods: test.aaa entra antes e todos os IDs mudam.
    {
        Session s(mods, {"test.aaa", "test.gems", "test.tools"});
        CHECK(s.Id(kRuby) != rubyId1 && s.Id(kTopaz) != topazId1);
        CHECK(s.game.LoadPlayer(plr) && s.game.LoadWorld(wld));
        const FakeGame::Player& p = s.game.player;
        CHECK(p.inventory[3].netID == s.Id(kRuby) && p.inventory[3].stack == 5 && p.inventory[3].prefix == 7 &&
              p.inventory[3].favorited);
        CHECK(s.Key(p.armor[0]) == kTopaz && s.Key(p.loadoutArmor[1][2]) == kDrill);
        CHECK(s.Key(p.banks[3][5]) == kTopaz && p.banks[3][5].stack == 3 && p.banks[3][5].favorited);
        CHECK(s.Key(p.temporary[0]) == kFictionalRuby);  // o load põe o "mouse" nos temporários
        CHECK(p.inventory[10].type == 2);
        // Os baús voltam com índices novos; a posição identifica cada um.
        CHECK(s.game.chests[1] && s.game.chests[1]->x == 300);
        CHECK(s.ChestAt(300, 50) && s.Key(s.ChestAt(300, 50)->item[39]) == kRuby && s.ChestAt(300, 50)->item[39].stack == 20);
        CHECK(s.ChestAt(100, 200) && s.Key(s.ChestAt(100, 200)->item[0]) == kDrill &&
              s.ChestAt(100, 200)->item[1].type == 8);
        CHECK(s.loader.Persistence().Orphans(plr).empty() && s.loader.Persistence().Orphans(wld).empty());
        // Entrar no mundo: o jogo devolve os temporários ao mouse; salvar de novo.
        s.game.held[0] = s.game.player.temporary[0];
        s.game.player.temporary[0] = Item{};
        CHECK(s.game.SavePlayer(plr) && s.game.lastSavedMaxId == 2 && s.game.SaveWorld(wld) && s.game.lastSavedMaxId == 8);
        CHECK(Sidecar(plr).size() == 5 && Sidecar(wld).size() == 2);
    }

    // 3) Mods removidos (test.gems e o example_item): o save continua válido
    //    e os itens ficam órfãos no arquivo paralelo.
    {
        Session s({}, {"test.aaa", "test.tools"});
        std::string err;
        CHECK(s.game.LoadPlayer(plr, &err) && s.game.LoadWorld(wld, &err));
        const FakeGame::Player& p = s.game.player;
        CHECK(p.inventory[3].IsAir() && p.armor[0].IsAir() && p.banks[3][5].IsAir() && p.temporary[0].IsAir());
        CHECK(s.Key(p.loadoutArmor[1][2]) == kDrill && s.Key(s.ChestAt(100, 200)->item[0]) == kDrill);  // o mod que ficou
        auto orphans = s.loader.Persistence().Orphans(plr);
        CHECK(orphans.size() == 4 && CountOrphans(orphans, "missing_mod") == 4);
        CHECK(s.loader.Persistence().Orphans(wld).size() == 1);
        // Jogando sem o mod: terra no slot onde o rubi estava, e salva.
        s.game.player.inventory[3] = Item::Raw(2, 1);
        CHECK(s.game.SavePlayer(plr) && s.game.lastSavedMaxId == 2 && s.game.SaveWorld(wld));
        auto side = Sidecar(plr);
        CHECK(side.size() == 5 && CountOrphans(side, "missing_mod") == 4 && CountOrphans(side, "") == 1);
        CHECK(Sidecar(wld).size() == 2 && CountOrphans(Sidecar(wld), "missing_mod") == 1);
        CHECK(ReadAll(plrSide).find("\"orphan\":\"missing_mod\"") != std::string::npos);
    }

    // 4) Mods reinstalados: os itens voltam. O rubi achou o slot ocupado e foi
    //    para o primeiro slot livre do inventário.
    {
        Session s(mods, {"test.aaa", "test.gems", "test.tools"});
        CHECK(s.game.LoadPlayer(plr) && s.game.LoadWorld(wld));
        const FakeGame::Player& p = s.game.player;
        CHECK(p.inventory[3].type == 2);  // a terra continua lá
        CHECK(s.Key(p.inventory[0]) == kRuby && p.inventory[0].stack == 5 && p.inventory[0].prefix == 7);
        CHECK(s.Key(p.armor[0]) == kTopaz && s.Key(p.banks[3][5]) == kTopaz && s.Key(p.temporary[0]) == kFictionalRuby);
        CHECK(s.Key(s.ChestAt(300, 50)->item[39]) == kRuby);
        CHECK(s.Count(kRuby) == 25 && s.Count(kTopaz) == 4 && s.Count(kDrill) == 2 && s.Count(kFictionalRuby) == 1);
        CHECK(s.loader.Persistence().Orphans(plr).empty() && s.loader.Persistence().Orphans(wld).empty());
        s.game.held[0] = s.game.player.temporary[0];
        s.game.player.temporary[0] = Item{};
        CHECK(s.game.SavePlayer(plr) && s.game.SaveWorld(wld));
        CHECK(CountOrphans(Sidecar(plr), "") == 5 && CountOrphans(Sidecar(wld), "") == 2);

        // O save roda fora da thread do jogo: o jogador pôs terra no slot do
        // rubi enquanto o arquivo era gravado. O rubi vai para outro slot.
        s.game.duringSave = [&] { s.game.player.inventory[0] = Item::Raw(2, 7); };
        CHECK(s.game.SavePlayer(plr));
        s.game.duringSave = nullptr;
        CHECK(p.inventory[0].type == 2 && p.inventory[0].stack == 7);  // nada sobrescrito
        CHECK(s.Count(kRuby) == 25);                                     // nada perdido
        CHECK(s.Key(p.inventory[1]) == kRuby);

        // Save que falha no meio: os itens voltam e o arquivo paralelo não muda.
        std::string sideBefore = ReadAll(plrSide);
        s.game.player.inventory[20] = s.Make(kTopaz, 9);
        s.game.failNextSave = true;
        CHECK(!s.game.SavePlayer(plr));
        CHECK(s.Key(p.inventory[20]) == kTopaz && s.Count(kTopaz) == 13 && ReadAll(plrSide) == sideBefore);

        // ID fora da faixa original e sem dono: nunca vai para o arquivo, mas
        // continua na memória.
        Item unknown = Item::Raw(kDefaultItemShell, 1);
        unknown.netID = 9000;
        s.game.player.inventory[21] = unknown;
        CHECK(s.game.SavePlayer(plr) && s.game.lastSavedMaxId == 2 && p.inventory[21].netID == 9000);
        CHECK(ReadAll(plrSide).find("9000") == std::string::npos);
    }

    // 5) Saves concorrentes (autosave no ThreadPool + salvar e sair): o mesmo
    //    personagem salvo por duas threads ao mesmo tempo, e o mundo em paralelo.
    //    Um save só devolve os itens quando o outro do mesmo tipo não está gravando.
    {
        Session s(mods, {"test.gems", "test.tools"});
        CHECK(s.game.LoadPlayer(plr) && s.game.LoadWorld(wld));
        s.game.held[0] = s.game.player.temporary[0];  // entrar no mundo
        s.game.player.temporary[0] = Item{};
        s.game.worstSavedId = 0;
        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 2; ++t)
            threads.emplace_back([&] {
                for (int i = 0; i < 25; ++i) ok += s.game.SavePlayer(plr) ? 1 : 0;
            });
        threads.emplace_back([&] {
            for (int i = 0; i < 25; ++i) ok += s.game.SaveWorld(wld) ? 1 : 0;
        });
        for (auto& t : threads) t.join();
        CHECK(ok == 75 && s.game.worstSavedId < FakeGame::kItemCount);
        CHECK(s.Count(kRuby) == 25 && s.Count(kTopaz) == 13 && s.Count(kDrill) == 2 && s.Count(kFictionalRuby) == 1);
        CHECK(Sidecar(plr).size() == 6 && Sidecar(wld).size() == 2);
    }

    // 6) Arquivo salvo sem ter sido carregado pelo TLMod (o jogo leu o mundo
    //    antes de os mods subirem): o que estava no arquivo paralelo continua.
    {
        Session s(mods, {"test.gems", "test.tools"}, /*load=*/false);
        CHECK(s.game.LoadWorld(wld));          // ganchos ainda não existem
        s.loader.LoadAll();
        CHECK(s.game.SaveWorld(wld));
        auto side = Sidecar(wld);
        CHECK(side.size() == 2 && CountOrphans(side, "not_loaded") == 2);
        Session again(mods, {"test.gems", "test.tools"});
        CHECK(again.game.LoadWorld(wld) && again.Key(again.ChestAt(300, 50)->item[39]) == kRuby);
    }

    // 7) Arquivo paralelo corrompido: o jogo carrega, o arquivo ruim é
    //    guardado à parte (nunca sobrescrito sem cópia).
    {
        std::ofstream(plrSide, std::ios::trunc) << "{ isto não é json";
        Session s(mods, {"test.gems"});
        CHECK(s.game.LoadPlayer(plr));
        bool quarantined = false;
        for (const auto& e : fs::directory_iterator(dir))
            quarantined |= e.path().filename().string().find(".tlmod.json.invalid-") != std::string::npos;
        CHECK(quarantined && !fs::exists(plrSide));
    }

    // 8) Save na nuvem: o arquivo paralelo vai para a pasta do TLMod.
    {
        Session s(mods, {"test.gems"});
        s.loader.Persistence().SetCloudDirectory(dir / "cloud");
        SaveFile cloud{SaveFile::Kind::Player, "/cloud/players/Jogador.plr", true};
        CHECK(s.loader.Persistence().SidecarPath(cloud) == dir / "cloud" / "player" / "Jogador.plr.tlmod.json");
        SaveFile local{SaveFile::Kind::World, wld, false};
        CHECK(s.loader.Persistence().SidecarPath(local) == fs::path(wldSide));
    }
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Itens de mod de verdade: casca original + identidade em netID. Nenhum ID de
// mod pode chegar a uma tabela do tamanho original (FakeGame conta).
namespace items {

const TLModHostApi* g_api = nullptr;
TLModContext* g_ctx = nullptr;
TLModId g_blade = TLMOD_INVALID_ID;
bool g_badShellRejected = false, g_giveValidation = false;
TLModId g_oldAbiItem = TLMOD_INVALID_ID;

int BladesInit(const TLModHostApi* api, TLModContext* ctx) {
    g_api = api;
    g_ctx = ctx;
    TLModItemDef d{};
    d.struct_size = sizeof(d);
    d.name = "bronze_blade";
    d.display_name = "L\xc3\xa2mina de Bronze";
    d.tooltip = "Corta.\nCorta de novo.";
    d.width = d.height = 32;
    d.max_stack = 1;
    d.value = 1200;
    d.rarity = 1;
    d.damage = 12;
    d.use_time = 25;
    d.shell_type = 9000;  // não é original: recusado
    g_badShellRejected = api->register_item(ctx, &d) == TLMOD_INVALID_ID;
    d.shell_type = 24;  // Espada de Madeira
    g_blade = api->register_item(ctx, &d);
    // Mod compilado antes de shell_type existir: struct menor, casca padrão.
    TLModItemDef old{};
    old.struct_size = static_cast<uint32_t>(offsetof(TLModItemDef, shell_type));
    old.name = "old_gem";
    old.display_name = "Gema Antiga";
    old.width = old.height = 10;
    old.max_stack = 99;
    old.shell_type = 1234;  // fora de struct_size: ignorado
    g_oldAbiItem = api->register_item(ctx, &old);
    // give_item no init: fica na fila até haver um mundo.
    g_giveValidation = !api->give_item(ctx, 99999, 1) && !api->give_item(ctx, g_blade, 0);
    return g_blade != TLMOD_INVALID_ID && api->give_item(ctx, g_blade, 1) ? 0 : 1;
}

}  // namespace items

static void TestItems(const fs::path& modsDir) {
    using namespace items;
    using Item = FakeGame::Item;
    std::printf("\n== itens de mod (casca + netID)\n");

    // 0) Sem o TLMod, o que o jogo faz com um ID de mod: SetDefaults vira ar; um
    //    type >= 5456 ou um netID >= 5456 no tooltip estouram as tabelas.
    {
        FakeGame g;
        Item it = g.NewItem(8192);
        CHECK(it.IsAir() && g.tableOverflows == 0);
        g.player.inventory[0] = Item::Raw(8192, 1);
        bool threw = false;
        try {
            g.DrawAll();
        } catch (const std::out_of_range&) {
            threw = true;
        }
        Item shelled = g.NewItem(38);
        shelled.netID = 8192;
        try {
            g.RebuildTooltip(shelled);
        } catch (const std::out_of_range&) {
        }
        CHECK(threw && g.tableOverflows == 2 && g.lastOverflowIndex == 8192);
    }

    fs::path tmp = fs::temp_directory_path() / "tlmod_items_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    fs::copy(modsDir / "example_item", tmp / "example_item", fs::copy_options::recursive);
    FakeGame game;
    ModLoader loader(&game);
    loader.DiscoverDirectory(tmp);
    ModManifest m = Builtin("test.blades", {"bronze_blade", "old_gem"});
    loader.AddBuiltin(m, &BladesInit);
    CHECK(loader.LoadAll() == 2);
    const ContentRegistry& reg = loader.Registry();
    const TLModId ruby = reg.Find(TLMOD_CONTENT_ITEM, "tlmod.example_item:fictional_ruby").value_or(-1);
    const ItemDefinition* rubyDef = reg.Get(TLMOD_CONTENT_ITEM, ruby)->Item();
    CHECK(g_badShellRejected && g_giveValidation && g_blade >= 8192 && ruby >= 8192);
    CHECK(rubyDef->shellType == kDefaultItemShell && reg.Get(TLMOD_CONTENT_ITEM, g_blade)->Item()->shellType == 24);
    CHECK(reg.Get(TLMOD_CONTENT_ITEM, g_oldAbiItem)->Item()->shellType == kDefaultItemShell);

    // O jogo cria o Rubi Fictício (loja, receita, Item.NewItem...): monta a casca 38.
    Item r = game.NewItem(ruby, 5, 0, true);
    CHECK(r.type == kDefaultItemShell && r.netID == ruby && r.stack == 5 && r.favorited);
    CHECK(game.Name(r) == rubyDef->displayName && r.tooltip == rubyDef->tooltip);
    CHECK(r.maxStack == 999 && r.value == 5000 && r.rare == 2 && r.damage == 0 && r.useStyle == 0);
    // A arma: dano e tempo de uso, golpe corpo a corpo por cima da Espada de Madeira.
    Item b = game.NewItem(g_blade);
    CHECK(b.type == 24 && b.netID == g_blade && b.damage == 12 && b.useTime == 25 && b.useAnimation == 25);
    CHECK(b.useStyle == 1 && b.melee && b.maxStack == 1 && b.value == 1200 && b.rare == 1);
    CHECK(game.Name(b) == "L\xc3\xa2mina de Bronze" && b.tooltip == "Corta.\nCorta de novo.");

    // Refresh (netDefaults(netID)) e cópias mantêm a identidade; o tooltip
    // reconstruído pelas telas (RebuildTooltip) vem da definição.
    Item copy = r;
    game.Refresh(copy);
    CHECK(copy.netID == ruby && copy.type == kDefaultItemShell && copy.stack == 5 && game.Name(copy) == game.Name(r));
    copy.tooltip.clear();
    game.RebuildTooltip(copy);
    CHECK(copy.tooltip == rubyDef->tooltip);
    // Não se mistura com a casca: uma Lente de verdade não empilha com o rubi.
    Item lens = game.NewItem(kDefaultItemShell, 3);
    CHECK(!FakeGame::IsTheSameAs(r, lens) && FakeGame::IsTheSameAs(r, copy));
    CHECK(game.Name(lens) == "item 38" && lens.netID == kDefaultItemShell);

    // give_item: pedido no init, entregue só com um mundo carregado.
    game.player.inventory[0] = lens;
    game.Frame();  // menu: nada
    CHECK(game.player.inventory[1].IsAir());
    game.inWorld = true;
    game.Frame();
    CHECK(game.player.inventory[1].netID == g_blade && game.player.inventory[1].type == 24);
    // Pela API, de outra thread, e empilhando: 5 + 3 rubis viram uma pilha de 8.
    std::thread([&] { g_api->give_item(g_ctx, ruby, 5); }).join();
    game.Frame();
    CHECK(g_api->give_item(g_ctx, ruby, 3));
    game.Frame();
    CHECK(game.player.inventory[2].netID == ruby && game.player.inventory[2].stack == 8);
    CHECK(game.player.inventory[0].netID == kDefaultItemShell && game.player.inventory[0].stack == 3);  // a Lente intacta
    // Pilha máxima da definição: 1000 rubis = 999 + 1.
    CHECK(g_api->give_item(g_ctx, ruby, 1000 - 8));
    game.Frame();
    CHECK(game.player.inventory[2].stack == 999 && game.player.inventory[3].netID == ruby &&
          game.player.inventory[3].stack == 1);
    // Um item original também.
    CHECK(g_api->give_item(g_ctx, 8, 10));
    game.Frame();
    CHECK(game.player.inventory[4].type == 8 && game.player.inventory[4].netID == 8 && game.player.inventory[4].stack == 10);

    // Interface desenhando tudo, várias vezes: nenhuma tabela lida fora do tamanho.
    for (int i = 0; i < 3; ++i) game.DrawAll();
    CHECK(game.tableOverflows == 0);

    // Persistência por chave: o .plr só tem originais; com outra ordem de mods os
    // IDs mudam e os itens voltam montados na casca certa.
    const std::string plr = (tmp / "Jogador.plr").string();
    game.player.armor[3] = game.NewItem(g_blade, 1, 0, false);
    const TLModId blade1 = g_blade;  // BladesInit roda de novo na segunda sessão
    CHECK(game.SavePlayer(plr) && game.lastSavedMaxId == kDefaultItemShell);
    CHECK(game.player.inventory[2].netID == ruby && game.player.inventory[2].type == kDefaultItemShell &&
          game.player.inventory[2].stack == 999 && game.Name(game.player.inventory[2]) == rubyDef->displayName);
    {
        FakeGame g2;
        ModLoader l2(&g2);
        ModManifest early = Builtin("aaa.first", {"first"});
        l2.AddBuiltin(early, &saves::FirstInit);
        l2.DiscoverDirectory(tmp);
        l2.AddBuiltin(m, &BladesInit);
        CHECK(l2.LoadAll() == 3);
        const TLModId ruby2 = l2.Registry().Find(TLMOD_CONTENT_ITEM, "tlmod.example_item:fictional_ruby").value_or(-1);
        const TLModId blade2 = l2.Registry().Find(TLMOD_CONTENT_ITEM, "test.blades:bronze_blade").value_or(-1);
        CHECK(ruby2 != ruby && blade2 != blade1);
        CHECK(g2.LoadPlayer(plr));
        const Item& rr = g2.player.inventory[2];
        CHECK(rr.netID == ruby2 && rr.type == kDefaultItemShell && rr.stack == 999 && g2.Name(rr) == rubyDef->displayName);
        const Item& bb = g2.player.armor[3];
        CHECK(bb.netID == blade2 && bb.type == 24 && bb.damage == 12 && g2.Name(bb) == "L\xc3\xa2mina de Bronze");
        CHECK(g2.player.inventory[0].netID == kDefaultItemShell && g2.player.inventory[0].stack == 3);
        g2.DrawAll();
        CHECK(g2.tableOverflows == 0);
    }
    CHECK(game.tableOverflows == 0);

    // O teste de aparelho do mod de exemplo: com o arquivo give_on_start, 3
    // Rubis Fictícios chegam no primeiro frame dentro de um mundo, montados.
    {
        std::ofstream(tmp / "example_item" / "give_on_start").put('\n');
        FakeGame g3;
        ModLoader l3(&g3);
        l3.DiscoverDirectory(tmp);
        CHECK(l3.LoadAll() == 1);
        g3.inWorld = false;
        g3.Frame();
        CHECK(g3.player.inventory[0].IsAir());
        g3.inWorld = true;
        g3.Frame();
        g3.Frame();
        const FakeGame::Item& gift = g3.player.inventory[0];
        CHECK(gift.netID == l3.Registry().Find(TLMOD_CONTENT_ITEM, "tlmod.example_item:fictional_ruby").value_or(-1) &&
              gift.type == kDefaultItemShell && gift.stack == 3 && g3.Name(gift) == rubyDef->displayName);
        CHECK(g3.player.inventory[1].IsAir() && g3.tableOverflows == 0);  // uma vez só
    }
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Texturas: decodificador PNG, fila na thread principal, textura por slot.
namespace tex {

// Codificador PNG de teste (independente do decodificador): linhas já no
// formato do tipo de cor, cada uma com o filtro `filters[y % n]` aplicado.
struct PngSpec {
    uint32_t width = 0, height = 0;
    uint8_t bitDepth = 8, colorType = 6, interlace = 0;
    std::vector<std::vector<uint8_t>> rows;  // sem o byte de filtro
    std::vector<uint8_t> plte, trns;
    std::vector<uint8_t> filters = {0};
};

void PutBe32(std::vector<uint8_t>& out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
}
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& body) {
    PutBe32(out, static_cast<uint32_t>(body.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    PutBe32(out, static_cast<uint32_t>(crc32(0L, out.data() + start, static_cast<uInt>(out.size() - start))));
}
int PaethRef(int a, int b, int c) {
    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}
std::vector<uint8_t> Encode(const PngSpec& sp) {
    const int channels[] = {1, 0, 3, 1, 2, 0, 4};
    size_t bpp = std::max<size_t>(1, static_cast<size_t>(channels[sp.colorType] * sp.bitDepth / 8));
    std::vector<uint8_t> raw;
    std::vector<uint8_t> prev(sp.rows.empty() ? 0 : sp.rows[0].size(), 0);
    for (size_t y = 0; y < sp.rows.size(); ++y) {
        const std::vector<uint8_t>& r = sp.rows[y];
        uint8_t f = sp.filters[y % sp.filters.size()];
        raw.push_back(f);
        for (size_t i = 0; i < r.size(); ++i) {
            int a = i >= bpp ? r[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
            int pred = f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : f == 4 ? PaethRef(a, b, c) : 0;
            raw.push_back(static_cast<uint8_t>(r[i] - pred));
        }
        prev = r;
    }
    uLongf zlen = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> z(zlen);
    compress(z.data(), &zlen, raw.data(), static_cast<uLong>(raw.size()));
    z.resize(zlen);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    PutBe32(ihdr, sp.width);
    PutBe32(ihdr, sp.height);
    ihdr.insert(ihdr.end(), {sp.bitDepth, sp.colorType, 0, 0, sp.interlace});
    Chunk(out, "IHDR", ihdr);
    Chunk(out, "tEXt", {'C', 'o', 'm', 'm', 'e', 'n', 't', 0, 'o', 'i'});  // auxiliar: ignorado
    if (!sp.plte.empty()) Chunk(out, "PLTE", sp.plte);
    if (!sp.trns.empty()) Chunk(out, "tRNS", sp.trns);
    // IDAT dividido em dois chunks (o decodificador concatena).
    size_t half = z.size() / 2;
    Chunk(out, "IDAT", std::vector<uint8_t>(z.begin(), z.begin() + static_cast<long>(half)));
    Chunk(out, "IDAT", std::vector<uint8_t>(z.begin() + static_cast<long>(half), z.end()));
    Chunk(out, "IEND", {});
    return out;
}

uint32_t g_seed = 12345;
uint8_t Rand() {
    g_seed = g_seed * 1103515245u + 12345u;
    return static_cast<uint8_t>(g_seed >> 16);
}

// Imagem RGBA esperada -> PNG no formato pedido (só cores representáveis nele).
struct Case {
    const char* name;
    uint8_t colorType, bitDepth;
};

std::vector<uint8_t> WriteAll(const fs::path& file, const std::vector<uint8_t>& data) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                                static_cast<std::streamsize>(data.size()));
    return data;
}

std::vector<uint8_t> SolidPng(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
    PngSpec sp;
    sp.width = w;
    sp.height = h;
    for (uint32_t y = 0; y < h; ++y) {
        std::vector<uint8_t> row;
        for (uint32_t x = 0; x < w; ++x) row.insert(row.end(), {r, g, b, 255});
        sp.rows.push_back(row);
    }
    return Encode(sp);
}

const TLModHostApi* g_api = nullptr;
TLModContext* g_ctx = nullptr;
TLModId g_plain = TLMOD_INVALID_ID, g_blue = TLMOD_INVALID_ID, g_missing = TLMOD_INVALID_ID;
TLModId g_wisp = TLMOD_INVALID_ID, g_odd = TLMOD_INVALID_ID, g_rejected = TLMOD_INVALID_ID, g_oldNpc = TLMOD_INVALID_ID;
bool g_badArgsRejected = false;
bool WispAi(void*, TLModNpcState* npc) {
    npc->frame = 5;  // fixo; com 2 quadros desenha o 5 % 2 = 1
    return true;
}
TLModNpcDef NpcDef(const char* name, const char* texture, int32_t frames) {
    TLModNpcDef d{};
    d.struct_size = sizeof(d);
    d.name = name;
    d.display_name = name;
    d.shell_type = 1;
    d.texture = texture;
    d.frame_count = frames;
    d.frame_ticks = 0;  // <= 0 = 8
    return d;
}
int Init(const TLModHostApi* api, TLModContext* ctx) {
    g_api = api;
    g_ctx = ctx;
    auto item = [&](const char* name, const char* texture) {
        TLModItemDef d{};
        d.struct_size = sizeof(d);
        d.name = name;
        d.display_name = name;
        d.texture = texture;
        d.width = d.height = 16;
        d.max_stack = 1;
        return api->register_item(ctx, &d);
    };
    g_plain = item("plain", nullptr);
    g_blue = item("blue", "tex/blue.png");
    g_missing = item("missing", "tex/nao_existe.png");
    TLModNpcDef wisp = NpcDef("wisp", "tex/wisp.png", 2);
    wisp.on_ai = &WispAi;
    g_wisp = api->register_npc(ctx, &wisp);
    TLModNpcDef odd = NpcDef("odd", "tex/odd.png", 4);  // altura 10: não divide em 4 quadros
    g_odd = api->register_npc(ctx, &odd);
    TLModNpcDef rejected = NpcDef("rejected", "tex/rejected.png", 1);  // o jogo recusa (largura 7)
    g_rejected = api->register_npc(ctx, &rejected);
    // Mod antigo: struct_size sem os campos de textura; o lixo neles é ignorado.
    TLModNpcDef old = NpcDef("old", "tex/blue.png", 1);
    old.struct_size = static_cast<uint32_t>(offsetof(TLModNpcDef, texture));
    g_oldNpc = api->register_npc(ctx, &old);
    // Rejeitados: caminho fora da pasta do mod, frame_count inválido.
    TLModNpcDef escape = NpcDef("escape", "../fora.png", 1);
    TLModNpcDef zero = NpcDef("zero", "tex/wisp.png", 0);
    TLModItemDef abs{};
    abs.struct_size = sizeof(abs);
    abs.name = "abs";
    abs.display_name = "abs";
    abs.texture = "/etc/passwd";
    abs.width = abs.height = abs.max_stack = 1;
    g_badArgsRejected = api->register_npc(ctx, &escape) == TLMOD_INVALID_ID &&
                        api->register_npc(ctx, &zero) == TLMOD_INVALID_ID &&
                        api->register_item(ctx, &abs) == TLMOD_INVALID_ID;
    return 0;
}

}  // namespace tex

static void TestPng(const fs::path& modsDir) {
    using namespace tex;
    std::printf("\n== PNG (decodificador do runtime)\n");

    // Todos os tipos de cor e profundidades, com os 5 filtros alternando por
    // linha. Imagem 13x7 (largura ímpar: bits sobrando no fim da linha).
    const uint32_t w = 13, h = 7;
    int okCases = 0, cases = 0;
    struct Fmt {
        const char* name;
        uint8_t colorType, bitDepth;
    };
    const Fmt fmts[] = {{"RGBA8", 6, 8},  {"RGB8", 2, 8},   {"cinza8", 0, 8}, {"cinza+alfa8", 4, 8},
                        {"RGBA16", 6, 16}, {"RGB16", 2, 16}, {"cinza1", 0, 1}, {"cinza2", 0, 2},
                        {"cinza4", 0, 4},  {"paleta1", 3, 1}, {"paleta2", 3, 2}, {"paleta4", 3, 4},
                        {"paleta8", 3, 8}};
    for (const Fmt& f : fmts) {
        ++cases;
        PngSpec sp;
        sp.width = w;
        sp.height = h;
        sp.colorType = f.colorType;
        sp.bitDepth = f.bitDepth;
        sp.filters = {0, 1, 2, 3, 4};
        std::vector<uint8_t> expect;
        const int levels = (1 << f.bitDepth) - 1;
        if (f.colorType == 3) {  // paleta com alfa (tRNS) nas primeiras entradas
            for (int i = 0; i <= std::min(levels, 255); ++i) {
                sp.plte.insert(sp.plte.end(), {Rand(), Rand(), Rand()});
                if (i < 3) sp.trns.push_back(static_cast<uint8_t>(i * 60));
            }
        }
        for (uint32_t y = 0; y < h; ++y) {
            std::vector<uint8_t> row(((w * static_cast<uint32_t>(f.bitDepth) *
                                       (f.colorType == 6 ? 4 : f.colorType == 2 ? 3 : f.colorType == 4 ? 2 : 1)) + 7) / 8);
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t px[4] = {0, 0, 0, 255};
                if (f.bitDepth < 8) {  // 1 amostra por pixel, empacotada
                    int v = Rand() % (levels + 1);
                    size_t bit = x * f.bitDepth;
                    row[bit / 8] = static_cast<uint8_t>(row[bit / 8] | (v << (8 - f.bitDepth - bit % 8)));
                    if (f.colorType == 3) {
                        std::copy(&sp.plte[static_cast<size_t>(v) * 3], &sp.plte[static_cast<size_t>(v) * 3] + 3, px);
                        px[3] = static_cast<size_t>(v) < sp.trns.size() ? sp.trns[static_cast<size_t>(v)] : 255;
                    } else {
                        px[0] = px[1] = px[2] = static_cast<uint8_t>(v * 255 / levels);
                    }
                } else {
                    int n = f.colorType == 6 ? 4 : f.colorType == 2 ? 3 : f.colorType == 4 ? 2 : 1;
                    uint8_t s[4];
                    for (int c = 0; c < n; ++c) {
                        s[c] = Rand();
                        if (f.colorType == 3) s[c] = static_cast<uint8_t>(s[c] % (sp.plte.size() / 3));
                        if (f.bitDepth == 16) {
                            row[(x * static_cast<uint32_t>(n) + static_cast<uint32_t>(c)) * 2] = s[c];
                            row[(x * static_cast<uint32_t>(n) + static_cast<uint32_t>(c)) * 2 + 1] = Rand();
                        } else {
                            row[x * static_cast<uint32_t>(n) + static_cast<uint32_t>(c)] = s[c];
                        }
                    }
                    switch (f.colorType) {
                        case 6: std::copy(s, s + 4, px); break;
                        case 2: std::copy(s, s + 3, px); break;
                        case 0: px[0] = px[1] = px[2] = s[0]; break;
                        case 4: px[0] = px[1] = px[2] = s[0], px[3] = s[1]; break;
                        case 3:
                            std::copy(&sp.plte[s[0] * 3u], &sp.plte[s[0] * 3u] + 3, px);
                            px[3] = s[0] < sp.trns.size() ? sp.trns[s[0]] : 255;
                            break;
                    }
                }
                expect.insert(expect.end(), px, px + 4);
            }
            sp.rows.push_back(row);
        }
        std::string err;
        auto img = DecodePng(Encode(sp), &err);
        bool ok = img && img->width == static_cast<int32_t>(w) && img->height == static_cast<int32_t>(h) &&
                  img->rgba == expect;
        if (!ok) std::printf("      %s: %s\n", f.name, img ? "pixels diferentes" : err.c_str());
        okCases += ok;
    }
    CHECK(okCases == cases);

    // tRNS em RGB: a cor-chave vira transparente, as outras não.
    {
        PngSpec sp;
        sp.width = 2;
        sp.height = 1;
        sp.colorType = 2;
        sp.rows = {{255, 0, 255, 10, 20, 30}};
        sp.trns = {0, 255, 0, 0, 0, 255};
        auto img = DecodePng(Encode(sp), nullptr);
        CHECK(img && img->rgba == std::vector<uint8_t>({255, 0, 255, 0, 10, 20, 30, 255}));
    }

    // Erros: nada lança, a mensagem explica.
    std::vector<uint8_t> good = SolidPng(4, 4, 200, 10, 10);
    auto fails = [](std::vector<uint8_t> data, const char* expect) {
        std::string err;
        bool failed = !DecodePng(data, &err);
        if (!failed || err.find(expect) == std::string::npos)
            std::printf("      esperado \"%s\", veio \"%s\"\n", expect, err.c_str());
        return failed && err.find(expect) != std::string::npos;
    };
    CHECK(DecodePng(good, nullptr).has_value());
    CHECK(fails({'G', 'I', 'F', '8', '9', 'a', 0, 0, 0, 0}, "assinatura"));
    CHECK(fails(std::vector<uint8_t>(good.begin(), good.end() - 20), "truncado"));
    {
        std::vector<uint8_t> bad = good;
        const char idat[] = "IDAT";
        auto at = std::search(bad.begin(), bad.end(), idat, idat + 4);
        *(at + 4) ^= 0x40;  // primeiro byte de dados do IDAT
        CHECK(fails(bad, "CRC"));
    }
    {
        PngSpec sp;
        sp.width = sp.height = 2;
        sp.interlace = 1;
        sp.rows = {std::vector<uint8_t>(8, 1), std::vector<uint8_t>(8, 1)};
        CHECK(fails(Encode(sp), "entrela"));
        sp.interlace = 0;
        sp.filters = {7};
        CHECK(fails(Encode(sp), "filtro"));
        sp.filters = {0};
        sp.colorType = 3;  // paleta sem PLTE
        sp.rows = {std::vector<uint8_t>(2, 0), std::vector<uint8_t>(2, 0)};
        CHECK(fails(Encode(sp), "PLTE"));
        sp.colorType = 2;
        sp.bitDepth = 4;  // RGB não aceita 4 bits
        CHECK(fails(Encode(sp), "profundidade"));
    }
    {
        PngSpec sp;  // 5000x1: maior que kMaxImageSide
        sp.width = 5000;
        sp.height = 1;
        sp.rows = {std::vector<uint8_t>(5000 * 4, 0)};
        CHECK(fails(Encode(sp), "fora de 1..4096"));
        sp.width = 4;  // dados a menos que o IHDR promete
        sp.height = 4;
        sp.rows = {std::vector<uint8_t>(16, 0)};
        CHECK(fails(Encode(sp), "incompletos"));
    }
    CHECK(!LoadPng(fs::temp_directory_path() / "tlmod_nao_existe.png", nullptr));

    // Os PNGs dos mods de exemplo.
    std::string err;
    auto ruby = LoadPng(modsDir / "example_item" / "textures" / "fictional_ruby.png", &err);
    CHECK(ruby && ruby->width == 32 && ruby->height == 32);
    if (ruby) {
        auto alpha = [&](int x, int y) { return ruby->rgba[(static_cast<size_t>(y) * 32 + static_cast<size_t>(x)) * 4 + 3]; };
        CHECK(alpha(0, 0) == 0 && alpha(31, 31) == 0 && alpha(16, 16) == 255);  // fundo transparente, gema opaca
        CHECK(ruby->rgba[(16u * 32 + 16) * 4] > ruby->rgba[(16u * 32 + 16) * 4 + 2]);  // vermelho
    }
    auto boss = LoadPng(modsDir / "example_boss" / "textures" / "ember_titan.png", &err);
    CHECK(boss && boss->width == 64 && boss->height == 256 && boss->height % 4 == 0);
    if (boss) {  // quadros diferentes entre si (as chamas giram)
        size_t frameBytes = 64u * 64u * 4u;
        CHECK(!std::equal(boss->rgba.begin(), boss->rgba.begin() + static_cast<long>(frameBytes),
                          boss->rgba.begin() + static_cast<long>(frameBytes)));
    }
}

static void TestTextures(const fs::path& modsDir) {
    using namespace tex;
    using Item = FakeGame::Item;
    std::printf("\n== texturas de mod (fila na thread principal, textura por slot)\n");
    fs::path tmp = fs::temp_directory_path() / "tlmod_texture_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    fs::copy(modsDir / "example_item", tmp / "example_item", fs::copy_options::recursive);
    fs::copy(modsDir / "example_boss", tmp / "example_boss", fs::copy_options::recursive);
    fs::path own = tmp / "test.tex";
    WriteAll(own / "tex" / "blue.png", SolidPng(16, 16, 0, 0, 255));
    WriteAll(own / "tex" / "wisp.png", SolidPng(10, 20, 0, 255, 0));  // 2 quadros de 10x10
    WriteAll(own / "tex" / "odd.png", SolidPng(10, 10, 9, 9, 9));
    WriteAll(own / "tex" / "rejected.png", SolidPng(7, 7, 9, 9, 9));

    FakeGame game;
    game.texturesReady = false;  // TextureAssets ainda carregando
    game.rejectTextureWidth = 7;
    TLModId ruby = TLMOD_INVALID_ID, boss = TLMOD_INVALID_ID;
    {
        ModLoader loader(&game);
        loader.DiscoverDirectory(tmp);
        ModManifest m = Builtin("test.tex", {"plain", "blue", "missing", "abs"});
        m.content[TLMOD_CONTENT_NPC] = {"wisp", "odd", "rejected", "old", "escape", "zero"};
        m.dir = own;
        loader.AddBuiltin(m, &Init);
        CHECK(loader.LoadAll() == 3);
        CHECK(g_badArgsRejected);
        ruby = loader.Registry().Find(TLMOD_CONTENT_ITEM, "tlmod.example_item:fictional_ruby").value_or(-1);
        boss = loader.Registry().Find(TLMOD_CONTENT_NPC, "tlmod.example_boss:ember_titan").value_or(-1);
        const NpcDefinition* bossDef = loader.Registry().Get(TLMOD_CONTENT_NPC, boss)->Npc();
        CHECK(bossDef->frameCount == 4 && bossDef->frameTicks == 10);
        CHECK(loader.Registry().Get(TLMOD_CONTENT_NPC, g_oldNpc)->Npc()->texture.empty());
        CHECK(loader.Registry().Get(TLMOD_CONTENT_NPC, g_wisp)->Npc()->frameTicks == 8);

        // PNGs lidos no LoadAll: rubi, boss, blue, wisp, rejected (missing e odd
        // falharam com erro no log; old não tem textura). Nada foi ao "Unity" ainda.
        CHECK(loader.PendingTextures() == 5 && game.textures.empty());

        // Antes da textura existir, o desenho usa a da casca.
        Item r = game.NewItem(ruby);
        CHECK(game.Texture(r) == game.itemTexture[kDefaultItemShell]);

        // De outra thread nada é criado: a fila só anda dentro do frame.
        std::thread([&] {
            Item other = game.NewItem(g_blue);
            (void)game.Texture(other);
        }).join();
        CHECK(game.textures.empty() && game.offThreadTextures == 0);

        // Frames com o jogo ainda carregando: -1, tenta de novo depois.
        game.Frame();
        game.Frame();
        CHECK(game.textures.empty() && loader.PendingTextures() == 5);

        // Pronto: até 4 por frame, todas na thread do frame. Ordem de carga
        // (test.tex, example_boss, example_item), itens antes de NPCs: blue,
        // rubi, wisp, rejected (o jogo recusa: 0 -> sai da fila, fica a da
        // casca) no 1º frame; o boss no 2º.
        game.texturesReady = true;
        game.Frame();
        CHECK(game.textures.size() == 3 && loader.PendingTextures() == 1);
        game.Frame();
        CHECK(game.textures.size() == 4 && loader.PendingTextures() == 0 && loader.TexturesCreated() == 4);
        CHECK(game.textures[3].frame == game.textures[0].frame + 1);  // um frame depois
        CHECK(game.offThreadTextures == 0);
        game.Frame();
        CHECK(game.textures.size() == 4);  // nada recriado

        // A textura certa para cada item: pelo netID (ID de mod), não pela casca.
        auto texOf = [&](int32_t id) -> const FakeGame::Texture2D* {
            int32_t t = id - FakeGame::kModTexture;
            return t > 0 && static_cast<size_t>(t) <= game.textures.size() ? &game.textures[static_cast<size_t>(t) - 1]
                                                                            : nullptr;
        };
        const FakeGame::Texture2D* rubyTex = texOf(game.Texture(r));
        CHECK(rubyTex && rubyTex->width == 32 && rubyTex->height == 32 &&
              rubyTex->name.find("fictional_ruby") != std::string::npos);
        const FakeGame::Texture2D* blueTex = texOf(game.Texture(game.NewItem(g_blue)));
        CHECK(blueTex && blueTex->width == 16 && blueTex->rgba[2] == 255 && blueTex->rgba[0] == 0);
        CHECK(game.Texture(game.NewItem(g_plain)) == game.itemTexture[kDefaultItemShell]);    // sem PNG
        CHECK(game.Texture(game.NewItem(g_missing)) == game.itemTexture[kDefaultItemShell]);  // PNG faltando
        CHECK(game.Texture(game.NewItem(kDefaultItemShell)) == game.itemTexture[kDefaultItemShell]);  // a Lente
        // Inventário misto desenhado: cada slot com a sua.
        game.player.inventory[0] = r;
        game.player.inventory[1] = game.NewItem(kDefaultItemShell);
        game.player.inventory[2] = game.NewItem(g_blue);
        CHECK(texOf(game.Texture(game.player.inventory[0])) == rubyTex &&
              game.Texture(game.player.inventory[1]) == game.itemTexture[kDefaultItemShell] &&
              texOf(game.Texture(game.player.inventory[2])) == blueTex);
        game.DrawAll();
        CHECK(game.tableOverflows == 0);

        // NPCs: textura e quadro por slot.
        CHECK(game.NewNpc(2, 0, 0) == 0);  // original
        CHECK(game.NewNpc(boss, 0, 0) == 1);
        CHECK(game.NewNpc(g_wisp, 0, 0) == 2);
        CHECK(game.NewNpc(g_rejected, 0, 0) == 3);
        CHECK(game.NewNpc(g_oldNpc, 0, 0) == 4);
        FakeGame::NpcSprite s0 = game.DrawNpc(0), s1 = game.DrawNpc(1), s2 = game.DrawNpc(2);
        CHECK(s0.texture == 2 && s0.frameHeight == FakeGame::kShellNpcFrameHeight);
        const FakeGame::Texture2D* bossTex = texOf(s1.texture);
        CHECK(bossTex && bossTex->width == 64 && bossTex->height == 256 && s1.frameHeight == 64 && s1.frameY == 0);
        CHECK(texOf(s2.texture) && texOf(s2.texture)->height == 20 && s2.frameHeight == 10);
        CHECK(game.DrawNpc(3).texture == 1 && game.DrawNpc(4).texture == 1);  // recusada / mod antigo: casca

        // Boss na introdução: o mod fixa o quadro 0 por 120 ticks.
        for (int t = 0; t < 60; ++t) game.UpdateNpcs();
        CHECK(game.DrawNpc(1).frameY == 0);
        // Wisp fixa frame = 5 em on_ai: 5 % 2 = quadro 1.
        CHECK(game.DrawNpc(2).frameY == 10);
        // Depois da intro (tick 120) a animação é automática: quadro = ticks / 10 % 4.
        for (int t = 60; t < 135; ++t) game.UpdateNpcs();
        CHECK(game.DrawNpc(1).frameY == (135 / 10 % 4) * 64);
        game.UpdateNpcs(), game.UpdateNpcs(), game.UpdateNpcs(), game.UpdateNpcs(), game.UpdateNpcs();
        CHECK(game.DrawNpc(1).frameY == (140 / 10 % 4) * 64);
        // Um segundo boss tem o próprio relógio: começa do quadro 0.
        CHECK(game.NewNpc(boss, 0, 0) == 5);
        CHECK(game.DrawNpc(5).texture == s1.texture && game.DrawNpc(5).frameY == 0 && game.DrawNpc(1).frameY != 0);
        // Slot reaproveitado por um NPC original: volta à textura da casca.
        game.SetDefaults(1, 7);
        CHECK(game.DrawNpc(1).texture == 7);
        CHECK(!game.vanillaSetDefaultsOverflow);
    }
    // Sem o runtime, o desenho não chama ninguém.
    CHECK(!game.drawHooks.itemTexture && !game.drawHooks.npcTexture);
    CHECK(game.Texture(game.player.inventory[0]) == game.itemTexture[kDefaultItemShell]);
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Casca de item: o que ela passa para o item de mod e os avisos do register_item.
namespace shells {

std::vector<std::string> g_logs;  // avisos capturados
std::mutex g_logMutex;
void CaptureSink(TLModLogLevel level, const std::string& source, const std::string& msg) {
    if (level < TLMOD_LOG_WARN) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logs.push_back(source + ": " + msg);
}
size_t Count(const std::string& needle) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return static_cast<size_t>(std::count_if(g_logs.begin(), g_logs.end(),
                                             [&](const std::string& l) { return l.find(needle) != std::string::npos; }));
}

ItemDefinition Def(int32_t damage, int32_t maxStack, TLModId shell) {
    ItemDefinition d;
    d.displayName = "x";
    d.damage = damage;
    d.maxStack = maxStack;
    d.shellType = shell;
    return d;
}
bool Mentions(const std::vector<ShellIssue>& issues, const std::string& needle) {
    for (const ShellIssue& i : issues)
        if (i.text.find(needle) != std::string::npos) return true;
    return false;
}

TLModId Reg(const TLModHostApi* api, TLModContext* ctx, const char* name, TLModId shell, int32_t damage,
            int32_t maxStack) {
    TLModItemDef d{};
    d.struct_size = sizeof(d);
    d.name = name;
    d.display_name = name;
    d.width = d.height = 16;
    d.max_stack = maxStack;
    d.damage = damage;
    d.use_time = damage > 0 ? 20 : 0;
    d.shell_type = shell;
    return api->register_item(ctx, &d);
}
int GoodInit(const TLModHostApi* api, TLModContext* ctx) {
    return Reg(api, ctx, "gem", 38, 0, 999) != TLMOD_INVALID_ID &&      // material na Lente
                   Reg(api, ctx, "sword", 24, 12, 1) != TLMOD_INVALID_ID &&  // espada na Espada de Madeira
                   Reg(api, ctx, "heart_gem", 58, 0, 99) != TLMOD_INVALID_ID &&  // material num Coração
                   Reg(api, ctx, "magic_sword", 739, 20, 1) != TLMOD_INVALID_ID  // espada num cajado
               ? 0
               : 1;
}
int FailingInit(const TLModHostApi* api, TLModContext* ctx) {
    Reg(api, ctx, "torch_gem", 8, 0, 99);  // seria avisado, mas o init falha: o registro é desfeito
    return 7;
}

}  // namespace shells

static void TestShells(const fs::path& modsDir) {
    using namespace shells;
    std::printf("\n== casca de item: propriedades herdadas e avisos do register_item\n");
    FakeGame game;
    auto traits = [&](TLModId t) { return *game.ItemShellTraits(t); };

    // Casca recomendada + definição típica da categoria: nenhum aviso.
    struct Typical {
        ItemCategory cat;
        int32_t damage, maxStack;
    };
    const Typical typical[] = {{ItemCategory::Material, 0, 999}, {ItemCategory::MeleeWeapon, 12, 1},
                               {ItemCategory::Bow, 9, 1},        {ItemCategory::Gun, 11, 1},
                               {ItemCategory::Ammo, 6, 999},     {ItemCategory::Accessory, 0, 1}};
    int clean = 0;
    for (const Typical& t : typical) {
        const ShellRecommendation& rec = RecommendedShell(t.cat);
        ItemDefinition d = Def(t.damage, t.maxStack, rec.shell);
        auto issues = CheckShell(d, traits(rec.shell));
        bool ok = issues.empty() && InferCategory(d, traits(rec.shell)) == t.cat;
        if (!ok) std::printf("      %s na casca %d: %zu avisos\n", ToString(t.cat), rec.shell, issues.size());
        clean += ok;
    }
    CHECK(clean == 6);
    CHECK(RecommendedShell(ItemCategory::Material).shell == kDefaultItemShell);

    // Propriedades da casca que não combinam com a definição.
    CHECK(Mentions(CheckShell(Def(0, 99, 58), traits(58)), "IsAPickup"));            // Coração
    CHECK(Mentions(CheckShell(Def(0, 99, 71), traits(71)), "moeda"));                // Moeda
    CHECK(Mentions(CheckShell(Def(0, 99, 2334), traits(2334)), "botão direito"));    // Caixa
    CHECK(Mentions(CheckShell(Def(0, 99, 8), traits(8)), "coloca o bloco 4"));       // Tocha
    auto arrowSword = CheckShell(Def(12, 1, 40), traits(40));                         // espada numa flecha
    CHECK(Mentions(arrowSword, "some no primeiro uso") && Mentions(arrowSword, "max_stack é 1"));
    CHECK(Mentions(CheckShell(Def(20, 1, 739), traits(739)), "mana"));               // espada num cajado
    auto swordGem = CheckShell(Def(0, 999, 24), traits(24));                          // material numa espada
    CHECK(Mentions(swordGem, "a casca é arma"));
    CHECK(Mentions(CheckShell(Def(0, 999, 39), traits(39)), "dispara o projétil 1"));  // material num arco
    CHECK(Mentions(CheckShell(Def(0, 20, 216), traits(216)), "não empilham"));        // acessório empilhável
    CHECK(Mentions(CheckShell(Def(5, 1, 3507), traits(3507)), "Shimmer vira o item 3508"));
    CHECK(Mentions(CheckShell(Def(8, 1, 40), traits(40)), "munição") &&
          !Mentions(CheckShell(Def(8, 999, 40), traits(40)), "prefixos"));            // munição não reforja: ok
    CHECK(InferCategory(Def(9, 1, 39), traits(39)) == ItemCategory::Bow &&
          InferCategory(Def(0, 1, 216), traits(216)) == ItemCategory::Accessory &&
          InferCategory(Def(6, 999, 97), traits(97)) == ItemCategory::Ammo);

    // register_item avisa no log, com a casca recomendada; uma vez por item.
    SetLogSink(&CaptureSink);
    {
        FakeGame g;
        ModLoader loader(&g);
        ModManifest m = Builtin("test.shells", {"gem", "sword", "heart_gem", "magic_sword"});
        loader.AddBuiltin(m, &GoodInit);
        CHECK(loader.LoadAll() == 1 && loader.PendingShellChecks() == 0);
        CHECK(Count("\"test.shells:gem\"") == 0 && Count("\"test.shells:sword\"") == 0);
        CHECK(Count("\"test.shells:heart_gem\" na casca 58: é coletada ao encostar") == 1);
        CHECK(Count("\"test.shells:heart_gem\" na casca 58: para material, a casca recomendada é 38 (Lente)") == 1);
        CHECK(Count("\"test.shells:magic_sword\" na casca 739: gasta 5 de mana") == 1);
        CHECK(Count("\"test.shells:magic_sword\" na casca 739: para arma corpo a corpo, a casca recomendada é 24") == 1);
        g.Frame();
        CHECK(Count("heart_gem") == 2);  // nada repetido no frame
    }
    // O jogo ainda não responde (no Terraria: thread do loader): a avaliação fica
    // para o frame handler; a de um mod cujo init falhou some junto com ele.
    g_logs.clear();
    {
        FakeGame g;
        g.traitsReady = false;
        ModLoader loader(&g);
        loader.AddBuiltin(Builtin("aaa.failing", {"torch_gem"}), &FailingInit);
        loader.AddBuiltin(Builtin("test.shells", {"gem", "sword", "heart_gem", "magic_sword"}), &GoodInit);
        CHECK(loader.LoadAll() == 1 && loader.PendingShellChecks() == 5 && Count("na casca") == 0);
        g.Frame();  // o item do mod que falhou não existe mais: sai da fila sem aviso
        CHECK(loader.PendingShellChecks() == 4 && Count("na casca") == 0);
        g.traitsReady = true;
        g.Frame();
        CHECK(loader.PendingShellChecks() == 0 && Count("heart_gem\" na casca 58") == 2 && Count("torch_gem") == 0);
        CHECK(Count("magic_sword\" na casca 739") == 2 && Count("\"test.shells:gem\"") == 0);
    }
    SetLogSink(std::getenv("TLMOD_VERBOSE") ? nullptr : &QuietSink);

    // Nome de NPC: o do mod no slot dele, o da casca nos outros (e sem o runtime).
    fs::path tmp = fs::temp_directory_path() / "tlmod_npcname_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    fs::copy(modsDir / "example_boss", tmp / "example_boss", fs::copy_options::recursive);
    FakeGame g;
    {
        ModLoader loader(&g);
        loader.DiscoverDirectory(tmp);
        CHECK(loader.LoadAll() == 1);
        TLModId boss = loader.Registry().Find(TLMOD_CONTENT_NPC, "tlmod.example_boss:ember_titan").value_or(-1);
        CHECK(g.NewNpc(4, 0, 0) == 0 && g.NewNpc(boss, 0, 0) == 1);
        CHECK(g.NpcName(1) == "Tit\xc3\xa3 de Brasa" && g.NpcName(0) == "npc 4");
        g.SetDefaults(1, 5);  // slot reaproveitado
        CHECK(g.NpcName(1) == "npc 5");
    }
    CHECK(!g.drawHooks.npcName);
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Cópia privada das bibliotecas dos mods (o dlopen no Android não pode ler do
// armazenamento externo) e log em arquivo.
static std::string ReadAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
static void WriteAll(const fs::path& p, const std::string& data) {
    std::ofstream(p, std::ios::binary | std::ios::trunc).write(data.data(), static_cast<std::streamsize>(data.size()));
}
static unsigned Mode(const fs::path& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 ? (st.st_mode & 07777u) : 0u;
}
static size_t Leftovers(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) n += e.path().filename().string().find(".tmp-") != std::string::npos;
    return n;
}

static void TestSha256() {
    std::printf("\n== SHA-256\n");
    CHECK(Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // 2 blocos
    CHECK(Sha256Hex(two.data(), two.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 million;
    const std::string a(1000, 'a');
    for (int i = 0; i < 1000; ++i) million.Update(a.data(), a.size());
    CHECK(million.HexDigest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void TestLibraryCache(const fs::path& modsDir) {
    std::printf("\n== cópia privada das bibliotecas (library_cache)\n");
    fs::path tmp = fs::temp_directory_path() / "tlmod_libcache_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "mod");
    const fs::path src = tmp / "mod" / "libx.so";
    const fs::path root = tmp / "code_cache" / "tlmod";
    const fs::path dest = root / "acme.x" / "libx.so";
    std::string a(200 * 1024, '\0');
    for (size_t i = 0; i < a.size(); ++i) a[i] = static_cast<char>(i * 31 + 7);
    WriteAll(src, a);
    LibraryCache cache(root);
    std::string err;

    // Cópia nova: conteúdo igual, somente leitura (0444), sem temporários.
    auto r1 = cache.Prepare("acme.x", src, &err);
    CHECK(r1 && r1->copied && r1->path == dest);
    CHECK(r1 && r1->sha256 == Sha256Hex(a.data(), a.size()));
    CHECK(ReadAll(dest) == a && Mode(dest) == 0444 && Leftovers(dest.parent_path()) == 0);

    // Sem mudança: reaproveita (mesmo inode, nada regravado).
    struct stat before{}, after{};
    ::stat(dest.c_str(), &before);
    auto r2 = cache.Prepare("acme.x", src, &err);
    ::stat(dest.c_str(), &after);
    CHECK(r2 && !r2->copied && r2->path == dest && before.st_ino == after.st_ino);
    // Reaproveitada mas gravável (alguém mexeu na permissão): volta a 0444.
    fs::permissions(dest, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
    auto r3 = cache.Prepare("acme.x", src, &err);
    CHECK(r3 && !r3->copied && Mode(dest) == 0444);

    // O mod mudou: cópia nova por cima (rename atômico), de novo 0444.
    std::string b = a;
    b[1000] ^= 0x55;
    WriteAll(src, b);
    auto r4 = cache.Prepare("acme.x", src, &err);
    CHECK(r4 && r4->copied && ReadAll(dest) == b && Mode(dest) == 0444);
    CHECK(r4 && r4->sha256 != r1->sha256 && Leftovers(dest.parent_path()) == 0);

    // Cópia adulterada no cache (mesmo tamanho, outro conteúdo): hash diverge, recopia.
    fs::permissions(dest, fs::perms::owner_all, fs::perm_options::replace);
    WriteAll(dest, a);
    auto r5 = cache.Prepare("acme.x", src, &err);
    CHECK(r5 && r5->copied && ReadAll(dest) == b && Mode(dest) == 0444);

    // A cópia sai corrompida (disco, interrupção): a conferência recusa, nada
    // vai para o destino (a cópia anterior fica intacta) e o temporário some.
    LibraryCache corrupt(root);
    corrupt.afterWriteForTest = [](const fs::path& t) {
        std::fstream f(t, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(4096);
        f.put('\x7f');
    };
    WriteAll(src, a);  // muda a origem para forçar a cópia
    err.clear();
    CHECK(!corrupt.Prepare("acme.x", src, &err));
    CHECK(err.find("não confere") != std::string::npos && err.find(Sha256Hex(a.data(), a.size())) != std::string::npos);
    CHECK(ReadAll(dest) == b && Mode(dest) == 0444 && Leftovers(dest.parent_path()) == 0);
    std::printf("      erro: %s\n", err.c_str());

    // Temporário de uma cópia interrompida (processo morto): é limpo na próxima.
    WriteAll(dest.parent_path() / "libx.so.tmp-999-0", "lixo");
    auto r6 = cache.Prepare("acme.x", src, &err);
    CHECK(r6 && r6->copied && ReadAll(dest) == a && Leftovers(dest.parent_path()) == 0);

    // Origem que não existe / pasta do cache impossível de criar.
    err.clear();
    CHECK(!cache.Prepare("acme.x", tmp / "mod" / "nao_existe.so", &err) && err.find("nao_existe.so") != std::string::npos);
    WriteAll(tmp / "arquivo", "x");
    LibraryCache bad(tmp / "arquivo" / "tlmod");
    err.clear();
    CHECK(!bad.Prepare("acme.x", src, &err) && err.find("não foi possível criar") != std::string::npos);
    // Pasta do mod somente leitura: o temporário não pode ser criado (root ignora permissões).
    if (::geteuid() != 0) {
        const fs::path ro = tmp / "ro";
        fs::create_directories(ro / "acme.x");
        fs::permissions(ro / "acme.x", fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        err.clear();
        CHECK(!LibraryCache(ro).Prepare("acme.x", src, &err) && err.find("não foi possível criar") != std::string::npos);
        fs::permissions(ro / "acme.x", fs::perms::owner_all, fs::perm_options::replace);
    }

    // ModLoader: com a pasta de cache, o dlopen é feito da cópia.
    {
        const fs::path mods = tmp / "mods";
        fs::create_directories(mods);
        fs::copy(modsDir / "example_item", mods / "example_item", fs::copy_options::recursive);
        fs::create_directories(mods / "broken" / "lib" / "linux-x86_64");
        WriteAll(mods / "broken" / "manifest.json", R"({"tlmod_api": 1, "id": "test.broken", "name": "Quebrado",
            "version": "1.0.0", "entry": {"linux-x86_64": "lib/linux-x86_64/libbroken.so"}})");
        WriteAll(mods / "broken" / "lib" / "linux-x86_64" / "libbroken.so", "isto não é um ELF");
        FakeGame game;
        ModLoader loader(&game);
        loader.SetLibraryCacheDir(tmp / "loader_cache");
        loader.DiscoverDirectory(mods);
        CHECK(loader.LoadAll() == 1);
        const ModRecord* item = loader.Find("tlmod.example_item");
        const fs::path copy = tmp / "loader_cache" / "tlmod.example_item" / "libexample_item.so";
        CHECK(item && item->state == ModState::Loaded && item->libraryPath == copy && Mode(copy) == 0444);
        CHECK(ReadAll(copy) == ReadAll(mods / "example_item" / "lib" / "linux-x86_64" / "libexample_item.so"));
        // dlopen que falha: a mensagem inteira do dlerror() fica no estado do mod.
        const ModRecord* broken = loader.Find("test.broken");
        const fs::path brokenCopy = tmp / "loader_cache" / "test.broken" / "libbroken.so";
        CHECK(broken && broken->state == ModState::Failed && broken->libraryPath.empty());
        CHECK(broken && broken->error.rfind("não foi possível carregar lib/linux-x86_64/libbroken.so (cópia em " +
                                                brokenCopy.string() + "): ", 0) == 0);
        CHECK(broken && broken->error.find(brokenCopy.string() + ": ") != std::string::npos);  // texto do dlerror
        if (broken) std::printf("      erro (%zu caracteres): %s\n", broken->error.size(), broken->error.c_str());
    }
    fs::remove_all(tmp);
}

static int g_sinkCalls = 0;
static void CountingSink(TLModLogLevel, const std::string&, const std::string&) { ++g_sinkCalls; }

static void TestLogFile() {
    std::printf("\n== log em arquivo\n");
    fs::path tmp = fs::temp_directory_path() / "tlmod_log_test";
    fs::remove_all(tmp);
    ResetLogForTest();
    SetLogSink(&CountingSink);
    g_sinkCalls = 0;

    // Antes de haver arquivo: fica na memória.
    Log(TLMOD_LOG_INFO, "runtime", "antes do arquivo");
    RecordLog(TLMOD_LOG_WARN, "ponte", "linha da ponte", 1700000000123);
    CHECK(g_sinkCalls == 1);  // RecordLog não vai para o sink (a ponte já mandou ao logcat)
    const fs::path file = tmp / "tlmod" / "tlmod.log";
    std::string err;
    CHECK(SetLogFile(file, &err) && LogFilePath() == file);
    Log(TLMOD_LOG_ERROR, "acme.x", "depois do arquivo");
    std::string text = ReadAll(file);
    CHECK(text.find("===== TLMod: sessão iniciada") != std::string::npos);
    CHECK(text.find(" I [runtime] antes do arquivo\n") != std::string::npos);
    CHECK(text.find(" W [ponte] linha da ponte\n") != std::string::npos && text.find(".123 W [ponte]") != std::string::npos);
    CHECK(text.find(" E [acme.x] depois do arquivo\n") != std::string::npos);
    CHECK(text.find("antes do arquivo") < text.find("depois do arquivo"));
    auto recent = RecentLog(2);
    CHECK(recent.size() == 2 && recent[0].message == "linha da ponte" && recent[1].level == TLMOD_LOG_ERROR);

    // Nova sessão no mesmo arquivo: acrescenta, não apaga.
    CHECK(SetLogFile(file, &err));
    Log(TLMOD_LOG_INFO, "runtime", "segunda sessão");
    text = ReadAll(file);
    CHECK(text.find("depois do arquivo") != std::string::npos && text.find("segunda sessão") != std::string::npos);

    // Rotação: passou de 1 MiB, vira tlmod.log.1 e um arquivo novo começa.
    const std::string big(1000, 'x');
    for (int i = 0; i < 1100; ++i) Log(TLMOD_LOG_DEBUG, "spam", big);
    fs::path old = file;
    old += ".1";
    CHECK(fs::exists(old) && fs::file_size(old) <= kLogFileMaxBytes && fs::file_size(file) < kLogFileMaxBytes);
    CHECK(ReadAll(old).find("segunda sessão") != std::string::npos);
    CHECK(RecentLog().size() == kLogMemoryLines);

    // Pasta impossível: falha sem derrubar o log em memória.
    WriteAll(tmp / "arquivo", "x");
    CHECK(!SetLogFile(tmp / "arquivo" / "tlmod.log", &err) && !err.empty() && LogFilePath().empty());
    Log(TLMOD_LOG_INFO, "runtime", "ainda em memória");
    CHECK(RecentLog(1).at(0).message == "ainda em memória");

    ResetLogForTest();
    SetLogSink(std::getenv("TLMOD_VERBOSE") ? nullptr : &QuietSink);
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// API das ferramentas: frame, comandos, catálogo de nomes, hora, cura, matar.
namespace tools {
const TLModHostApi* g_api = nullptr;
TLModContext* g_ctx = nullptr;
int g_frames = 0;
std::string g_lastCommand;
bool g_initChecks = false;

void OnFrame(void*) { ++g_frames; }
bool OnCommand(void*, const char* cmd, TLModBuffer* out) {
    g_lastCommand = cmd;
    if (g_lastCommand == "recusa") return false;
    if (g_lastCommand == "quebrado") {
        g_api->buffer_append(out, "{x", 2);
        return true;
    }
    if (g_lastCommand == "vazio") return true;
    std::string json = "{\"toast\":\"ok: " + g_lastCommand + "\"}";
    g_api->buffer_append(out, json.data(), json.size());
    return true;
}
int Init(const TLModHostApi* api, TLModContext* ctx) {
    g_api = api;
    g_ctx = ctx;
    g_initChecks = !api->register_command(ctx, "nao_declarado", &OnCommand, nullptr) &&
                   !api->register_command(ctx, "panel", nullptr, nullptr) &&
                   !api->subscribe_frame(ctx, nullptr, nullptr);
    return api->subscribe_frame(ctx, &OnFrame, nullptr) && api->register_command(ctx, "panel", &OnCommand, nullptr) &&
                   !api->register_command(ctx, "panel", &OnCommand, nullptr)  // duplicado
               ? 0
               : 1;
}
TLModId g_gem = TLMOD_INVALID_ID;
int GemInit(const TLModHostApi* api, TLModContext* ctx) {
    TLModItemDef d{};
    d.struct_size = sizeof(d);
    d.name = "gem";
    d.display_name = "Gema Teste";
    d.width = d.height = 16;
    d.max_stack = 99;
    g_gem = api->register_item(ctx, &d);
    return g_gem == TLMOD_INVALID_ID;
}
std::string Name(TLModContentKind kind, TLModId id, bool internal, int32_t* rc = nullptr) {
    char buf[64] = {};
    int32_t n = g_api->content_name(g_ctx, kind, id, internal, buf, sizeof(buf));
    if (rc) *rc = n;
    return n >= 0 ? buf : "";
}
}  // namespace tools

static void TestTools() {
    std::printf("\n== ferramentas: frame, comandos, nomes, hora, cura, matar\n");
    using namespace tools;
    FakeGame game;
    game.vanilla[TLMOD_CONTENT_ITEM] = FakeGame::kItemCount;
    game.vanilla[TLMOD_CONTENT_NPC] = FakeGame::kVanillaNpcs;
    game.namesReady = false;
    ModLoader loader(&game);
    ModManifest m = Builtin("test.tools");
    m.provides = {"panel"};
    loader.AddBuiltin(m, &Init);
    loader.AddBuiltin(Builtin("test.gem", {"gem"}), &GemInit);
    CHECK(loader.LoadAll() == 2 && g_initChecks);
    for (const auto& r : loader.Mods())
        if (r.state != ModState::Loaded) std::printf("      %s: %s\n", r.manifest.id.c_str(), r.error.c_str());
    CHECK(loader.CommandChannels() == std::vector<std::string>{"test.tools/panel"});
    // Fora do init não registra mais nada.
    CHECK(!g_api->subscribe_frame(g_ctx, &OnFrame, nullptr));

    // Comandos (da thread da UI): resposta validada, recusa e JSON quebrado viram erro.
    std::string err;
    auto r = loader.Command("test.tools", "panel", "give 38 5", &err);
    CHECK(r && *r == "{\"toast\":\"ok: give 38 5\"}" && g_lastCommand == "give 38 5");
    CHECK(loader.Command("test.tools", "panel", "vazio") == std::optional<std::string>("{}"));
    CHECK(!loader.Command("test.tools", "panel", "recusa", &err) && err.find("recusado") != std::string::npos);
    CHECK(!loader.Command("test.tools", "panel", "quebrado", &err));
    CHECK(!loader.Command("test.tools", "outro", "x", &err) && err.find("não existe") != std::string::npos);

    // Catálogo: conteúdo de mod responde já; original espera o jogo (-2).
    int32_t rc = 0;
    CHECK(Name(TLMOD_CONTENT_ITEM, g_gem, false) == "Gema Teste" && Name(TLMOD_CONTENT_ITEM, g_gem, true) == "test.gem:gem");
    Name(TLMOD_CONTENT_ITEM, 38, false, &rc);
    CHECK(rc == -2 && g_api->content_ids(g_ctx, TLMOD_CONTENT_ITEM, nullptr, 0) == -2);
    game.Frame();  // o jogo ainda não responde nomes: nada avança
    CHECK(game.nameQueries == 0 && !loader.CatalogReady() && g_frames == 1);
    game.namesReady = true;
    game.Frame();
    CHECK(game.nameQueries == 2 * 256 && !loader.CatalogReady());  // 256 IDs por frame
    int frames = 1;
    while (!loader.CatalogReady() && frames < 100) game.Frame(), ++frames;
    CHECK(loader.CatalogReady() && frames == (5455 + 687 + 255) / 256);
    CHECK(Name(TLMOD_CONTENT_ITEM, 38, false) == "Lente" && Name(TLMOD_CONTENT_ITEM, 4956, true) == "Zenith");
    CHECK(Name(TLMOD_CONTENT_NPC, 4, false) == "Olho de Cthulhu" && Name(TLMOD_CONTENT_NPC, 50, true) == "KingSlime");
    Name(TLMOD_CONTENT_ITEM, 5000, false, &rc);
    CHECK(rc == -1);  // sem nome
    Name(TLMOD_CONTENT_ITEM, 0, false, &rc);
    CHECK(rc == -1);
    Name(TLMOD_CONTENT_PROJECTILE, 1, false, &rc);
    CHECK(rc == -1);
    char small[4];
    CHECK(g_api->content_name(g_ctx, TLMOD_CONTENT_ITEM, 38, false, small, sizeof(small)) == 5 &&
          std::string(small) == "Len");
    int32_t n = g_api->content_ids(g_ctx, TLMOD_CONTENT_ITEM, nullptr, 0);
    CHECK(n == 5455 - 1 + 1);  // 1..5455 menos o 5000, mais a gema
    std::vector<TLModId> ids(static_cast<size_t>(n));
    CHECK(g_api->content_ids(g_ctx, TLMOD_CONTENT_ITEM, ids.data(), n) == n && ids.front() == 1 &&
          ids.back() == g_gem && std::find(ids.begin(), ids.end(), 5000) == ids.end());
    CHECK(g_api->content_ids(g_ctx, TLMOD_CONTENT_NPC, nullptr, 0) == 687);
    // Mais frames não perguntam de novo.
    int queries = game.nameQueries;
    game.Frame();
    CHECK(game.nameQueries == queries);

    // Hora, cura, matar: só com mundo.
    bool day = false;
    double t = 0;
    game.inWorld = false;
    CHECK(!g_api->get_time(g_ctx, &day, &t) && !g_api->set_time(g_ctx, false, 100, true));
    game.inWorld = true;
    CHECK(g_api->get_time(g_ctx, &day, &t) && day && t == 1000.0);
    CHECK(g_api->set_time(g_ctx, false, 16200, true) && game.timeJumps == 1);
    CHECK(g_api->set_time(g_ctx, true, 27000, false) && game.timeJumps == 1 && game.worldTime->second == 27000.0);
    CHECK(!g_api->set_time(g_ctx, false, 40000, true) && !g_api->set_time(g_ctx, true, -1, true));  // fora da faixa
    game.life[0] = 7;
    CHECK(g_api->heal_player(g_ctx) && game.life[0] == 500 && game.mana == 200);
    CHECK(game.NewNpc(2, 900, 400) == 0 && game.NewNpc(2, 5000, 400) == 1 && game.NewNpc(3, 820, 410) == 2);
    game.npcs[2].friendly = true;
    CHECK(g_api->kill_hostile_npcs(g_ctx, 1000) == 1 && !game.npcs[0].active && game.npcs[1].active &&
          game.npcs[2].active);
    game.playerCenter.reset();
    CHECK(g_api->kill_hostile_npcs(g_ctx, 10000) == -1);
    CHECK(g_frames > 10);  // o frame do mod rodou todo frame
}

// ---------------------------------------------------------------------------
// Mod tlmod.devtools (a .so real) contra o jogo simulado.
static void TestDevTools(const fs::path& modsDir) {
    std::printf("\n== mod devtools (aba Ferramentas)\n");
    fs::path tmp = fs::temp_directory_path() / "tlmod_devtools_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    for (const char* m : {"devtools", "example_item"})
        fs::copy(modsDir / m, tmp / m, fs::copy_options::recursive);
    FakeGame game;
    game.vanilla[TLMOD_CONTENT_ITEM] = FakeGame::kItemCount;
    game.vanilla[TLMOD_CONTENT_NPC] = FakeGame::kVanillaNpcs;
    ModLoader loader(&game);
    loader.DiscoverDirectory(tmp);
    CHECK(loader.LoadAll() == 2);
    CHECK(loader.CommandChannels() == std::vector<std::string>{"tlmod.devtools/panel"});
    auto cmd = [&](const std::string& c) { return loader.Command("tlmod.devtools", "panel", c); };
    auto panel = [&]() -> std::string { return loader.Query("tlmod.devtools", "panel").value_or("?"); };
    auto toast = [&](const std::optional<std::string>& r) {
        auto doc = r ? json::Parse(*r, nullptr) : std::nullopt;
        const json::Value* t = doc ? doc->Get("toast") : nullptr;
        return t && t->AsString() ? *t->AsString() : std::string();
    };

    // Antes do catálogo: a busca avisa que os nomes estão carregando.
    CHECK(cmd("search item lente") && panel().find("ainda est\xc3\xa3o carregando") != std::string::npos);
    game.inWorld = false;
    for (int i = 0; i < 40 && !loader.CatalogReady(); ++i) game.Frame();
    CHECK(loader.CatalogReady());
    std::string doc = panel();
    auto parsed = json::Parse(doc, nullptr);
    CHECK(parsed && *parsed->Get("title")->AsString() == "Ferramentas" && parsed->Get("sections")->AsArray()->size() == 4);
    CHECK(doc.find("Entre num mundo") != std::string::npos);
    // Sem busca, a seção de itens lista os itens de mod, e a de NPCs os bosses.
    CHECK(doc.find("Rubi Fict\xc3\xad" "cio") != std::string::npos && doc.find("\"spawn 125 126\"") != std::string::npos);
    CHECK(doc.find("\"Olho de Cthulhu\"") != std::string::npos && doc.find("\"Rei Slime\"") != std::string::npos);

    // Busca: nome, sem acento, interno, ID; o painel recebe os resultados.
    CHECK(cmd("search item lente") && panel().find("\"give 38 10\"") != std::string::npos);
    CHECK(cmd("search item zenite") && panel().find("\"give 4956 1\"") != std::string::npos);  // Zênite
    CHECK(cmd("search item FallenStar") && panel().find("\"give 75 100\"") != std::string::npos);
    CHECK(cmd("search item 38") && panel().find("\"give 38 1\"") != std::string::npos);
    CHECK(cmd("search item rubi") && panel().find("Rubi Fict\xc3\xad" "cio") != std::string::npos);
    CHECK(cmd("search item xyzzy") && panel().find("Nada encontrado") != std::string::npos);
    CHECK(cmd("search item item") && panel().find("Mostrando os 30") != std::string::npos);
    CHECK(cmd("search npc olho") && panel().find("\"spawn 4\"") != std::string::npos);
    CHECK(!cmd("search bloco x") && !cmd("abracadabra") && !cmd("give 38 0"));

    // Dar item: chega no próximo frame dentro de um mundo.
    game.inWorld = true;
    auto r = cmd("give 38 5");
    CHECK(r && toast(r) == "5x Lente no invent\xc3\xa1rio");
    game.Frame();
    CHECK(game.player.inventory[0].type == 38 && game.player.inventory[0].stack == 5);

    // Hora: o pulo vai pelo SkipToTime; congelada, segura mesmo que o jogo ande.
    CHECK(cmd("time noon"));
    game.Frame();
    CHECK(game.worldTime == std::make_pair(true, 27000.0) && game.timeJumps == 1);
    CHECK(panel().find("Agora: 12:00 (dia)") != std::string::npos);
    CHECK(toast(cmd("freeze")) == "Hora congelada");
    game.Frame();
    game.worldTime->second = 27500;  // o jogo andou meio minuto
    game.Frame();
    CHECK(game.worldTime->second == 27000.0 && panel().find("congelada") != std::string::npos);
    CHECK(cmd("time midnight"));
    game.Frame();
    game.worldTime->second = 16300;
    game.Frame();
    CHECK(game.worldTime == std::make_pair(false, 16200.0));  // congelada na hora nova
    CHECK(panel().find("Agora: 0:00 (noite)") != std::string::npos);
    CHECK(toast(cmd("freeze")) == "Hora voltou a andar");
    game.worldTime->second = 16400;
    game.Frame();
    CHECK(game.worldTime->second == 16400.0);

    // Jogador: invencível cancela o golpe; curar; matar.
    CHECK(toast(cmd("god")) == "Invenc\xc3\xadvel: ligado" && panel().find("\"command\":\"god\",\"active\":true") != std::string::npos);
    CHECK(game.Hurt(0, 100) == 0 && game.life[0] == 400);
    CHECK(cmd("god") && game.Hurt(0, 100) > 0);
    CHECK(cmd("heal"));
    game.Frame();
    CHECK(game.life[0] == 500 && game.mana == 200);
    CHECK(game.NewNpc(2, 900, 400) >= 0);
    CHECK(cmd("kill"));
    game.Frame();
    CHECK(std::none_of(game.npcs.begin(), game.npcs.end(), [](const FakeGame::Npc& n) { return n.active; }));

    // Invocar: perto do jogador (centro 800,400), os Gêmeos lado a lado.
    // (o runtime espera 2 s seguidos no mundo antes do primeiro spawn perto do jogador)
    CHECK(toast(cmd("spawn 125 126")).find("a caminho") != std::string::npos);
    for (int i = 0; i < 120; ++i) game.Frame();
    CHECK(game.npcs[0].active && game.npcs[0].s.type == 125 && game.npcs[0].s.x == 1280.0f);
    CHECK(game.npcs[1].active && game.npcs[1].s.type == 126 && game.npcs[1].s.x == 1440.0f && game.npcs[1].s.y == 160.0f);

    fs::remove_all(tmp);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s <build>/dist/mods\n", argv[0]);
        return 2;
    }
    if (!std::getenv("TLMOD_VERBOSE")) SetLogSink(&QuietSink);
    TestSemVer();
    TestManifest();
    TestRegistry();
    TestResolver();
    TestLoader(argv[1]);
    TestNpcs(argv[1]);
    TestBossChecklist(argv[1]);
    TestSaves(argv[1]);
    TestItems(argv[1]);
    TestPng(argv[1]);
    TestTextures(argv[1]);
    TestShells(argv[1]);
    TestSha256();
    TestLibraryCache(argv[1]);
    TestLogFile();
    TestTools();
    TestDevTools(argv[1]);
    std::printf("\n%s (%d falha%s)\n", g_failures ? "FALHOU" : "TODOS OS TESTES PASSARAM", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
