// Testes do runtime de mods: semver, manifest, registro de conteúdo,
// resolução de dependências e carga real do mod de exemplo.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

#include "fake_game.h"
#include "modrix/runtime/content_registry.h"
#include "modrix/runtime/log.h"
#include "modrix/runtime/manifest.h"
#include "modrix/runtime/mod_loader.h"

using namespace modrix::rt;
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

static void QuietSink(ModrixLogLevel, const std::string&, const std::string&) {}

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
  "modrix_api": 1, "id": "acme.swords", "name": "Espadas", "version": "1.4.0",
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
        CHECK(m->Declares(MODRIX_CONTENT_ITEM, "gold_katana"));
        CHECK(m->Declares(MODRIX_CONTENT_PROJECTILE, "slash"));
        CHECK(!m->Declares(MODRIX_CONTENT_ITEM, "slash"));
    }
    auto bad = [](const std::string& json, const std::string& expectInError) {
        std::string e;
        bool rejected = !ParseManifest(json, &e).has_value();
        bool mentions = e.find(expectInError) != std::string::npos;
        if (!rejected || !mentions) std::printf("      erro recebido: %s\n", e.c_str());
        return rejected && mentions;
    };
    const std::string head = R"("modrix_api":1,"name":"x","version":"1.0.0",)";
    const std::string entry = R"("entry":{"linux-x86_64":"lib/a.so"})";
    CHECK(bad("{" + head + R"("id":"Bad Id",)" + entry + "}", "id"));
    CHECK(bad("{" + head + R"("id":"a.b"})", "entry"));
    CHECK(bad("{" + head + R"("id":"a.b","entry":{"linux-x86_64":"../../etc/x.so"}})", "entry.linux-x86_64"));
    CHECK(bad("{" + head + R"("id":"a.b","content":{"spells":["x"]},)" + entry + "}", "content.spells"));
    CHECK(bad("{" + head + R"("id":"a.b","content":{"items":["x","x"]},)" + entry + "}", "repetido"));
    CHECK(bad("{" + head + R"("id":"a.b","dependencies":{"a.b":"*"},)" + entry + "}", "si mesmo"));
    CHECK(bad("{" + head + R"("id":"a.b","dependencies":{"c":">>1"},)" + entry + "}", "faixa"));
    CHECK(bad(R"({"modrix_api":1,"id":"a","id":"b"})", "duplicada"));
    CHECK(bad("{" + head + R"("id":"a.b",)" + entry, "JSON"));
    CHECK(IsValidModId("modrix.example_item") && !IsValidModId(".x") && !IsValidModId("a..b"));
}

// ---------------------------------------------------------------------------
static void TestRegistry() {
    std::printf("\n== registro de conteúdo\n");
    FakeGame game;
    ContentRegistry reg(&game);
    std::string err;

    // IDs de mod começam acima de todos os originais do dump.
    CHECK(reg.VanillaCount(MODRIX_CONTENT_ITEM) == 5456);
    CHECK(reg.Range(MODRIX_CONTENT_ITEM).firstModId > 5456);
    auto a = reg.Register(MODRIX_CONTENT_ITEM, "acme.swords", "iron_katana", ItemDefinition{}, &err);
    auto b = reg.Register(MODRIX_CONTENT_ITEM, "acme.swords", "gold_katana", ItemDefinition{}, &err);
    CHECK(a == 8192 && b == 8193);
    auto buff = reg.Register(MODRIX_CONTENT_BUFF, "acme.swords", "bleeding", {}, &err);
    CHECK(buff == 4096);
    CHECK(reg.Find(MODRIX_CONTENT_ITEM, "acme.swords:gold_katana") == 8193);
    CHECK(reg.Get(MODRIX_CONTENT_ITEM, 8192)->key == "acme.swords:iron_katana");
    CHECK(reg.Get(MODRIX_CONTENT_ITEM, 5455) == nullptr);  // original: não é do registro
    CHECK(!reg.IsModId(MODRIX_CONTENT_ITEM, 100));

    CHECK(!reg.Register(MODRIX_CONTENT_ITEM, "acme.swords", "iron_katana", {}, &err));
    CHECK(err.find("já registrado") != std::string::npos);
    // Mesmo nome em outro mod não colide: a chave inclui o id do mod.
    CHECK(reg.Register(MODRIX_CONTENT_ITEM, "other.mod", "iron_katana", {}, &err) == 8194);
    CHECK(!reg.Register(MODRIX_CONTENT_ITEM, "acme.swords", "Iron Katana", {}, &err));

    auto cp = reg.Mark();
    reg.Register(MODRIX_CONTENT_ITEM, "acme.swords", "temp", {}, &err);
    reg.Rollback(cp);
    CHECK(!reg.Find(MODRIX_CONTENT_ITEM, "acme.swords:temp"));
    CHECK(reg.Register(MODRIX_CONTENT_ITEM, "acme.swords", "after_rollback", {}, &err) == 8195);

    // Faixa esgotada.
    auto ranges = ContentRegistry::DefaultRanges();
    ranges[MODRIX_CONTENT_NPC] = {"npc", 688, 1000, 1001};
    ContentRegistry small(&game, ranges);
    CHECK(small.Register(MODRIX_CONTENT_NPC, "m", "a", {}, &err) == 1000);
    CHECK(small.Register(MODRIX_CONTENT_NPC, "m", "b", {}, &err) == 1001);
    CHECK(!small.Register(MODRIX_CONTENT_NPC, "m", "c", {}, &err));
    CHECK(err.find("esgotada") != std::string::npos);

    // Jogo atualizado com mais itens que a folga: tipo desativado, nunca colide.
    FakeGame bigGame;
    bigGame.vanilla[MODRIX_CONTENT_ITEM] = 9000;
    ContentRegistry guarded(&bigGame);
    CHECK(!guarded.KindEnabled(MODRIX_CONTENT_ITEM));
    CHECK(!guarded.Register(MODRIX_CONTENT_ITEM, "m", "x", {}, &err));
    CHECK(guarded.KindEnabled(MODRIX_CONTENT_TILE));
}

// ---------------------------------------------------------------------------
static ModRecord Rec(const std::string& id, const std::string& ver,
                     std::vector<std::tuple<std::string, std::string, bool>> deps = {}) {
    ModRecord r;
    r.manifest.apiVersion = MODRIX_API_VERSION;
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
    CHECK(Get(mods, "i")->state == ModState::Rejected && Get(mods, "i")->error.find("modrix_api") != std::string::npos);
    for (const auto& m : mods)
        if (m.state == ModState::Rejected) std::printf("      %s: %s\n", m.manifest.id.c_str(), m.error.c_str());
}

// ---------------------------------------------------------------------------
// Mods embutidos usados no teste de carga.
static int g_postCalls = 0;
static int32_t g_lastLifeAfter = -1;

static bool HalveDamage(void*, ModrixPlayerHurt* ev, bool after) {
    if (after) {
        ++g_postCalls;
        g_lastLifeAfter = ev->life_after;
        return true;
    }
    if (ev->damage > 1000) return false;  // cancela golpes absurdos
    ev->damage /= 2;
    return true;
}

static int HalverInit(const ModrixHostApi* api, ModrixContext* ctx) {
    return api->subscribe_player_hurt(ctx, &HalveDamage, nullptr) ? 0 : 1;
}

// Registra um item declarado, tenta um não declarado e falha: o runtime deve
// desfazer o registro do primeiro.
static int BadInit(const ModrixHostApi* api, ModrixContext* ctx) {
    ModrixItemDef def{};
    def.struct_size = sizeof(def);
    def.name = "declared_item";
    def.display_name = "Declarado";
    def.width = def.height = 10;
    def.max_stack = 1;
    if (api->register_item(ctx, &def) == MODRIX_INVALID_ID) return 2;
    def.name = "undeclared_item";
    if (api->register_item(ctx, &def) != MODRIX_INVALID_ID) return 3;
    return 1;
}

static ModManifest Builtin(const std::string& id, std::vector<std::string> items = {}) {
    ModManifest m;
    m.apiVersion = MODRIX_API_VERSION;
    m.id = id;
    m.name = id;
    m.version = *SemVer::Parse("1.0.0");
    m.content[MODRIX_CONTENT_ITEM] = std::move(items);
    return m;
}

static void TestLoader(const fs::path& modsDir) {
    std::printf("\n== carga (mod de exemplo + embutidos)\n");

    // Pasta temporária com o mod de exemplo compilado + um mod quebrado.
    fs::path tmp = fs::temp_directory_path() / "modrix_runtime_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "broken");
    fs::copy(modsDir / "example_item", tmp / "example_item", fs::copy_options::recursive);
    std::ofstream(tmp / "broken" / "manifest.json") << "{ \"modrix_api\": 1, ";

    FakeGame game;
    ModLoader loader(&game);
    loader.DiscoverDirectory(tmp);
    loader.AddBuiltin(Builtin("test.hurt_halver"), &HalverInit);
    loader.AddBuiltin(Builtin("test.bad", {"declared_item"}), &BadInit);
    size_t n = loader.LoadAll();

    for (const auto& m : loader.Mods())
        std::printf("      %-22s %-10s %s\n", m.manifest.id.c_str(), ToString(m.state), m.error.c_str());

    CHECK(n == 2);
    const ModRecord* ex = loader.Find("modrix.example_item");
    CHECK(ex && ex->state == ModState::Loaded);
    CHECK(loader.Find("broken")->state == ModState::Rejected);
    CHECK(loader.Find("test.bad")->state == ModState::Failed);
    CHECK((loader.LoadOrder() == std::vector<std::string>{"modrix.example_item", "test.bad", "test.hurt_halver"}));

    auto rubyId = loader.Registry().Find(MODRIX_CONTENT_ITEM, "modrix.example_item:fictional_ruby");
    CHECK(rubyId == 8192);
    if (rubyId) {
        const ContentEntry* e = loader.Registry().Get(MODRIX_CONTENT_ITEM, *rubyId);
        CHECK(e && e->Item() && e->Item()->displayName == "Rubi Fictício" && e->Item()->maxStack == 999);
    }
    // O item do mod que falhou foi desfeito.
    CHECK(!loader.Registry().Find(MODRIX_CONTENT_ITEM, "test.bad:declared_item"));
    CHECK(loader.Registry().Entries(MODRIX_CONTENT_ITEM).size() == 1);

    // find_content pela API C, como um mod faria.
    const ModrixHostApi& api = loader.HostApi();
    CHECK(api.api_version == MODRIX_API_VERSION && api.struct_size == sizeof(ModrixHostApi));

    // Evento: o "jogo" aplica 100 de dano; o mod corta para 50.
    CHECK(game.Hurt(0, 100) == 50);
    CHECK(game.life[0] == 350 && g_postCalls == 1 && g_lastLifeAfter == 350);
    CHECK(game.Hurt(0, 5000) == 0 && game.life[0] == 350);  // cancelado

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// NPCs: carrega o example_boss real + um mod embutido que testa validações.
static const ModrixHostApi* g_npcApi = nullptr;
static ModrixContext* g_npcCtx = nullptr;

static int NpcValidationInit(const ModrixHostApi* api, ModrixContext* ctx) {
    g_npcApi = api;
    g_npcCtx = ctx;
    ModrixNpcDef d{};
    d.struct_size = sizeof(d);
    d.display_name = "Lacaio";
    d.life_max = 0;  // mantém o da casca
    d.damage = d.defense = -1;
    d.name = "undeclared";
    d.shell_type = 4;
    if (api->register_npc(ctx, &d) != MODRIX_INVALID_ID) return 2;  // fora de content.npcs
    d.name = "minion";
    d.shell_type = 4096;
    if (api->register_npc(ctx, &d) != MODRIX_INVALID_ID) return 3;  // casca precisa ser original
    d.shell_type = 0;
    if (api->register_npc(ctx, &d) != MODRIX_INVALID_ID) return 4;
    d.shell_type = 1;
    return api->register_npc(ctx, &d) == MODRIX_INVALID_ID ? 5 : 0;
}

static void TestNpcs(const fs::path& modsDir) {
    std::printf("\n== NPCs (example_boss + ganchos NewNPC/SetDefaults/AI)\n");
    fs::path tmp = fs::temp_directory_path() / "modrix_npc_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    fs::copy(modsDir / "example_boss", tmp / "example_boss", fs::copy_options::recursive);

    FakeGame game;
    ModLoader loader(&game);
    loader.DiscoverDirectory(tmp);
    ModManifest m = Builtin("test.npcs");
    m.content[MODRIX_CONTENT_NPC] = {"minion"};
    loader.AddBuiltin(m, &NpcValidationInit);
    CHECK(loader.LoadAll() == 2);
    for (const auto& r : loader.Mods())
        if (r.state != ModState::Loaded) std::printf("      %s: %s\n", r.manifest.id.c_str(), r.error.c_str());

    auto bossId = loader.Registry().Find(MODRIX_CONTENT_NPC, "modrix.example_boss:ember_titan");
    auto minionId = loader.Registry().Find(MODRIX_CONTENT_NPC, "test.npcs:minion");
    CHECK(bossId == 4096 && minionId == 4097);
    CHECK(loader.Registry().Get(MODRIX_CONTENT_NPC, 4096)->Npc()->shellType == 4);

    // NPC original: nenhum gancho interfere.
    CHECK(game.NewNpc(2, 0, 0) == 0 && loader.NpcInSlot(0) == MODRIX_INVALID_ID);

    // Boss pelo caminho do jogo (como um item de invocação chamando NewNPC).
    CHECK(game.NewNpc(4096, 500, 600) == 1);
    const ModrixNpcState& boss = game.npcs[1].s;
    CHECK(boss.type == 4 && loader.NpcInSlot(1) == 4096);  // jogo vê a casca; runtime sabe que é o boss
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
    CHECK(loader.NpcInSlot(1) == MODRIX_INVALID_ID);

    // Spawn pela API do mod: passa pelo mesmo remap.
    int32_t idx = g_npcApi->spawn_npc(g_npcCtx, 4097, 10, 20);
    CHECK(idx == 2 && loader.NpcInSlot(2) == 4097 && game.npcs[2].s.type == 1);
    CHECK(game.npcs[2].s.life_max == 101);  // life_max 0 = o da casca
    CHECK(g_npcApi->spawn_npc(g_npcCtx, 9999, 0, 0) == -1);
    CHECK(g_npcApi->spawn_npc(g_npcCtx, 3, 0, 0) == 3 && loader.NpcInSlot(3) == MODRIX_INVALID_ID);

    // Sem slot livre: o spawn falha e nada é marcado.
    while (game.NewNpc(2, 0, 0) >= 0) {
    }
    CHECK(g_npcApi->spawn_npc(g_npcCtx, 4096, 0, 0) == -1);

    // Em nenhum momento um ID de mod chegou ao SetDefaults original.
    CHECK(!game.vanillaSetDefaultsOverflow);
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s <build>/dist/mods\n", argv[0]);
        return 2;
    }
    if (!std::getenv("MODRIX_VERBOSE")) SetLogSink(&QuietSink);
    TestSemVer();
    TestManifest();
    TestRegistry();
    TestResolver();
    TestLoader(argv[1]);
    TestNpcs(argv[1]);
    std::printf("\n%s (%d falha%s)\n", g_failures ? "FALHOU" : "TODOS OS TESTES PASSARAM", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
