// Ponta a ponta no host: runtime + BridgeGameAdapter + ponte IL2CPP contra a
// libil2cpp.so falsa (il2cpp_bridge/test/mock_il2cpp.cpp). Confere o
// adaptador de verdade (contêineres, baús pela posição, itens temporários),
// que o runtime_test não exercita (ele usa o FakeGame).
#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <tuple>

#include "bridge.h"
#include "bridge_adapter.h"
#include "tlmod/runtime/item_persistence.h"
#include "tlmod/runtime/log.h"
#include "tlmod/runtime/mod_loader.h"
#include "tlmod/runtime/png.h"

using namespace tlmod::rt;
namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (cond) {                                                      \
            std::printf("ok    %s\n", #cond);                            \
        } else {                                                         \
            std::printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

void QuietSink(TLModLogLevel, const std::string&, const std::string&) {}

bool (*mock_patch_function)(void*, void*);
bool TestInstaller(void* target, void* replacement, void** original) {
    *original = target;
    return mock_patch_function(target, replacement);
}

int GemsInit(const TLModHostApi* api, TLModContext* ctx) {
    for (const char* name : {"ruby", "topaz"}) {
        TLModItemDef d{};
        d.struct_size = sizeof(d);
        d.name = name;
        d.display_name = name;
        d.width = d.height = 16;
        d.max_stack = 999;
        d.value = 500;
        d.rarity = 3;
        if (std::string(name) == "ruby") d.texture = "fictional_ruby.png";  // o PNG do mod de exemplo
        if (api->register_item(ctx, &d) == TLMOD_INVALID_ID) return 1;
    }
    TLModNpcDef n{};
    n.struct_size = sizeof(n);
    n.name = "golem";
    n.display_name = "golem";
    n.shell_type = 4;
    n.texture = "ember_titan.png";  // a folha do boss de exemplo: 4 quadros de 64x64
    n.frame_count = 4;
    n.frame_ticks = 10;
    return api->register_npc(ctx, &n) == TLMOD_INVALID_ID ? 1 : 0;
}

std::u16string U16(const std::string& ascii) { return std::u16string(ascii.begin(), ascii.end()); }

TLModItemFields Get(Il2CppObject* player, TLModItemContainer c, int32_t group, int32_t index) {
    TLModItemFields f{};
    TLMod_ReadItem(TLMod_ItemSlot(player, c, group, index), &f);
    return f;
}
// Como o jogo cria um item: SetDefaults(type) (com o hook) e a pilha. 0 = esvazia.
void Put(Il2CppObject* player, TLModItemContainer c, int32_t group, int32_t index, int32_t type, int32_t stack) {
    Il2CppObject* item = TLMod_ItemSlot(player, c, group, index);
    TLModItemFields f{};
    if (type != 0) {
        TLMod_SetItemDefaults(item, type);
        TLMod_ReadItem(item, &f);
        f.stack = stack;
    }
    TLMod_WriteItem(item, &f);
}
constexpr int32_t kShell = 38;  // casca padrão (ItemID.Lens)
// Item de mod montado: casca no type, identidade no netID.
bool IsMod(const TLModItemFields& f, int32_t id) { return f.type == kShell && f.netID == id; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s /caminho/libil2cpp.so (mock)\n", argv[0]);
        return 2;
    }
    if (!std::getenv("TLMOD_VERBOSE")) SetLogSink(&QuietSink);
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    CHECK(lib != nullptr);
    if (!lib) return 1;
    auto sym = [&](const char* n) { return dlsym(lib, n); };
    auto mock_init = reinterpret_cast<void (*)()>(sym("mock_il2cpp_init"));
    mock_patch_function = reinterpret_cast<bool (*)(void*, void*)>(sym("mock_patch_function"));
    auto save_player = reinterpret_cast<void (*)()>(sym("mock_game_save_player"));
    auto load_player = reinterpret_cast<void* (*)(const char16_t*, bool)>(sym("mock_game_load_player"));
    auto save_world = reinterpret_cast<void (*)()>(sym("mock_game_save_world"));
    auto load_world = reinterpret_cast<int (*)(bool)>(sym("mock_game_load_world"));
    auto set_world_path = reinterpret_cast<void (*)(const char16_t*)>(sym("mock_set_world_path"));
    auto saved_ids = reinterpret_cast<size_t (*)(const int32_t**)>(sym("mock_saved_ids"));
    auto add_chest = reinterpret_cast<void (*)(int32_t, int32_t, int32_t)>(sym("mock_add_chest"));
    auto remove_chest = reinterpret_cast<void (*)(int32_t)>(sym("mock_remove_chest"));
    auto frame = reinterpret_cast<int (*)()>(sym("mock_game_frame"));
    auto load_assets = reinterpret_cast<void (*)()>(sym("mock_load_texture_assets"));
    auto draw_inv = reinterpret_cast<void* (*)(int32_t)>(sym("mock_game_draw_inventory"));
    auto draw_hotbar = reinterpret_cast<void* (*)(int32_t)>(sym("mock_game_draw_hotbar"));
    auto draw_npc = reinterpret_cast<void* (*)(int32_t, int32_t*, int32_t*)>(sym("mock_game_draw_npc"));
    auto new_npc = reinterpret_cast<int32_t (*)(int32_t, int32_t, int32_t)>(sym("mock_game_new_npc"));
    auto update_npcs = reinterpret_cast<void (*)()>(sym("mock_game_update_npcs"));
    auto item_tex = reinterpret_cast<void* (*)(int32_t)>(sym("mock_item_asset_texture"));
    auto npc_tex = reinterpret_cast<void* (*)(int32_t)>(sym("mock_npc_asset_texture"));
    auto tex_info = reinterpret_cast<bool (*)(void*, int32_t*, int32_t*, int32_t*, int32_t*, bool*)>(sym("mock_texture_info"));
    auto tex_pixel = reinterpret_cast<uint32_t (*)(void*, int32_t, int32_t)>(sym("mock_texture_pixel"));
    CHECK(mock_init && mock_patch_function && save_player && load_player && save_world && load_world &&
          set_world_path && saved_ids && add_chest && remove_chest);
    auto maxSaved = [&] {
        const int32_t* ids = nullptr;
        size_t n = saved_ids(&ids);
        return n ? *std::max_element(ids, ids + n) : 0;
    };

    mock_init();
    TLMod_SetLibraryName(argv[1]);
    TLMod_SetHookInstaller(&TestInstaller);
    CHECK(TLMod_Init(2000));

    fs::path dir = fs::temp_directory_path() / "tlmod_bridge_save_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string plr = (dir / "Jogador.plr").string(), wld = (dir / "Mundo.wld").string();

    BridgeGameAdapter adapter;
    ModLoader loader(&adapter);
    ModManifest m;
    m.apiVersion = TLMOD_API_VERSION;
    m.id = "test.gems";
    m.name = "gems";
    m.version = *SemVer::Parse("1.0.0");
    m.content[TLMOD_CONTENT_ITEM] = {"ruby", "topaz"};
    m.content[TLMOD_CONTENT_NPC] = {"golem"};
    m.dir = dir;
    fs::copy_file(TLMOD_EXAMPLE_RUBY_PNG, dir / "fictional_ruby.png");
    fs::copy_file(TLMOD_EXAMPLE_BOSS_PNG, dir / "ember_titan.png");
    loader.AddBuiltin(m, &GemsInit);
    CHECK(loader.LoadAll() == 1);
    const TLModId ruby = loader.Registry().Find(TLMOD_CONTENT_ITEM, "test.gems:ruby").value_or(-1);
    const TLModId topaz = loader.Registry().Find(TLMOD_CONTENT_ITEM, "test.gems:topaz").value_or(-1);

    // ---- Personagem ---------------------------------------------------------------
    // O jogo lista o personagem no menu (LoadPlayer): ainda sem arquivo paralelo.
    Il2CppObject* player = TLMod_GetPlayerByIndex(0);
    CHECK(load_player(U16(plr).c_str(), false) != nullptr && player);
    Put(player, TLMOD_ITEMS_INVENTORY, 0, 4, ruby, 12);
    Put(player, TLMOD_ITEMS_DYE, 0, 2, topaz, 1);
    Put(player, TLMOD_ITEMS_BANK, 0, 0, topaz, 3);
    Put(player, TLMOD_ITEMS_LOADOUT_DYE, 1, 9, ruby, 1);
    Put(player, TLMOD_ITEMS_HELD, 0, 0, ruby, 2);  // no mouse
    Put(player, TLMOD_ITEMS_INVENTORY, 0, 5, 3, 1);  // original

    save_player();
    CHECK(maxSaved() == 3);  // só o original foi para o .plr
    CHECK(IsMod(Get(player, TLMOD_ITEMS_INVENTORY, 0, 4), ruby) && IsMod(Get(player, TLMOD_ITEMS_HELD, 0, 0), ruby));
    CHECK(IsMod(Get(player, TLMOD_ITEMS_LOADOUT_DYE, 1, 9), ruby) && Get(player, TLMOD_ITEMS_BANK, 0, 0).stack == 3);
    CHECK(Get(player, TLMOD_ITEMS_INVENTORY, 0, 4).stack == 12);
    std::string err;
    auto side = ItemPersistence::ReadSidecar(plr + ItemPersistence::kSidecarSuffix, &err);
    CHECK(side && side->size() == 5);

    // Novo load: o jogo leu o .plr (sem os itens de mod); o runtime reinsere.
    for (auto [c, g, i] : {std::tuple{TLMOD_ITEMS_INVENTORY, 0, 4}, {TLMOD_ITEMS_DYE, 0, 2}, {TLMOD_ITEMS_BANK, 0, 0},
                           {TLMOD_ITEMS_LOADOUT_DYE, 1, 9}, {TLMOD_ITEMS_HELD, 0, 0}})
        Put(player, c, g, i, 0, 0);
    CHECK(load_player(U16(plr).c_str(), false) != nullptr);
    TLModItemFields inv = Get(player, TLMOD_ITEMS_INVENTORY, 0, 4);
    CHECK(IsMod(inv, ruby) && inv.stack == 12);
    CHECK(IsMod(Get(player, TLMOD_ITEMS_DYE, 0, 2), topaz) && Get(player, TLMOD_ITEMS_BANK, 0, 0).stack == 3);
    CHECK(IsMod(Get(player, TLMOD_ITEMS_LOADOUT_DYE, 1, 9), ruby));
    // O mouse volta pelos temporários lidos do arquivo (Player._temporaryItemSlots).
    CHECK(IsMod(Get(player, TLMOD_ITEMS_TEMPORARY, 0, 0), ruby) && Get(player, TLMOD_ITEMS_TEMPORARY, 0, 0).stack == 2);
    // Reinserido pelo SetDefaults do jogo: atributos e nome da definição.
    TLModItemStats st{};
    TLMod_ReadItemStats(TLMod_ItemSlot(player, TLMOD_ITEMS_INVENTORY, 0, 4), &st);
    CHECK(st.maxStack == 999 && st.value == 500 && st.rare == 3);
    CHECK(Get(player, TLMOD_ITEMS_HELD, 0, 0).type == 0);

    // O que não coube no inventário cai no chão (Main.item): a casca pelo NewItem do
    // jogo e o item de mod montado por cima, em pilhas de até maxStack.
    {
        auto world_count = reinterpret_cast<int32_t (*)()>(sym("mock_world_item_count"));
        auto world_item = reinterpret_cast<void* (*)(int32_t)>(sym("mock_world_item"));
        CHECK(world_count && world_item);
        const int32_t first = world_count();
        CHECK(adapter.DropItemNearPlayer(ruby, 1500) == 1500 && world_count() == first + 2);
        TLModItemFields a{}, b{};
        TLMod_ReadItem(static_cast<Il2CppObject*>(world_item(first)), &a);
        TLMod_ReadItem(static_cast<Il2CppObject*>(world_item(first + 1)), &b);
        CHECK(IsMod(a, ruby) && a.stack == 999 && IsMod(b, ruby) && b.stack == 501);
        CHECK(adapter.DropItemNearPlayer(8, 3) == 3 && world_count() == first + 3);
        TLMod_ReadItem(static_cast<Il2CppObject*>(world_item(first + 2)), &a);
        CHECK(a.type == 8 && a.netID == 8 && a.stack == 3);
    }

    // ---- Mundo: baús pela posição -------------------------------------------------
    add_chest(3, 77, 88);
    Put(nullptr, TLMOD_ITEMS_CHEST, 3, 6, topaz, 40);
    Put(nullptr, TLMOD_ITEMS_CHEST, 3, 7, 9, 2);
    set_world_path(U16(wld).c_str());
    bool threw = false;
    try {
        save_world();  // com o ID de mod no baú, SaveChests do "jogo" estouraria
    } catch (...) {
        threw = true;
    }
    CHECK(!threw && maxSaved() == 9 && IsMod(Get(nullptr, TLMOD_ITEMS_CHEST, 3, 6), topaz));
    side = ItemPersistence::ReadSidecar(wld + ItemPersistence::kSidecarSuffix, &err);
    CHECK(side && side->size() == 1 && (*side)[0].slot.chestX == 77 && (*side)[0].slot.chestY == 88);

    // O load renumera os baús: o mesmo baú agora em Main.chest[0].
    remove_chest(3);
    add_chest(0, 77, 88);
    Put(nullptr, TLMOD_ITEMS_CHEST, 0, 7, 9, 2);
    load_world(false);
    CHECK(IsMod(Get(nullptr, TLMOD_ITEMS_CHEST, 0, 6), topaz) && Get(nullptr, TLMOD_ITEMS_CHEST, 0, 6).stack == 40);
    CHECK(loader.Persistence().Orphans(wld).empty());

    // ---- Texturas: fila no Main.DoUpdate, troca no desenho ------------------------------
    CHECK(frame && load_assets && draw_inv && draw_hotbar && draw_npc && new_npc && update_npcs && item_tex &&
          npc_tex && tex_info && tex_pixel);
    CHECK(loader.PendingTextures() == 2);
    // A casca só é avaliada na thread do jogo: o LoadAll deixou os dois itens na fila.
    CHECK(loader.PendingShellChecks() == 2);
    frame();  // TextureAssets ainda vazio: a ponte devolve -1, a fila espera
    CHECK(loader.PendingShellChecks() == 0);  // Lente: nada a avisar
    CHECK(loader.PendingTextures() == 2 && loader.TexturesCreated() == 0);
    load_assets();
    frame();
    CHECK(loader.PendingTextures() == 0 && loader.TexturesCreated() == 2);
    void* lensTex = item_tex(kShell);
    void* rubyTex = draw_inv(4);  // o rubi no slot 4 do inventário
    int32_t w = 0, h = 0, filter = -1, wrap = -1;
    bool applied = false;
    CHECK(rubyTex && rubyTex != lensTex && tex_info(rubyTex, &w, &h, &filter, &wrap, &applied));
    CHECK(w == 32 && h == 32 && applied && item_tex(kShell) == lensTex);
    // Os pixels que chegaram ao Unity são os do PNG (topo continua no topo).
    auto png = LoadPng(TLMOD_EXAMPLE_RUBY_PNG, nullptr);
    bool same = png.has_value();
    for (int32_t y = 0; same && y < 32; ++y)
        for (int32_t x = 0; same && x < 32; ++x) {
            uint32_t want;
            std::memcpy(&want, &png->rgba[(static_cast<size_t>(y) * 32 + static_cast<size_t>(x)) * 4], 4);
            same = tex_pixel(rubyTex, x, y) == want;
        }
    CHECK(same);
    Put(player, TLMOD_ITEMS_INVENTORY, 0, 6, kShell, 1);  // uma Lente de verdade
    Put(player, TLMOD_ITEMS_INVENTORY, 0, 7, topaz, 1);   // de mod, sem PNG
    CHECK(draw_hotbar(4) == rubyTex && draw_inv(6) == lensTex && draw_inv(7) == lensTex);

    const TLModId golem = loader.Registry().Find(TLMOD_CONTENT_NPC, "test.gems:golem").value_or(-1);
    const int32_t eye = new_npc(0, 0, 4), boss = new_npc(50, 50, golem);
    CHECK(eye >= 0 && boss >= 0 && loader.NpcInSlot(boss) == golem);
    int32_t frames = 0, rect[4] = {};
    void* sheet = draw_npc(boss, &frames, rect);
    CHECK(sheet && sheet != npc_tex(4) && tex_info(sheet, &w, &h, &filter, &wrap, &applied) && w == 64 && h == 256);
    CHECK(frames == 4 && rect[0] == 0 && rect[1] == 0 && rect[2] == 64 && rect[3] == 64);
    for (int t = 0; t < 25; ++t) update_npcs();  // 25 ticks / 10 por quadro = quadro 2
    CHECK(draw_npc(boss, &frames, rect) == sheet && rect[1] == 128);
    CHECK(draw_npc(eye, &frames, rect) == npc_tex(4) && frames != 4);
    // Nome: NPC.TypeName e GetTypeNetName (anúncios) do NPC de mod; o original fica.
    auto npc_name = reinterpret_cast<size_t (*)(int32_t, char*, size_t)>(sym("mock_npc_type_name"));
    auto npc_net_name = reinterpret_cast<size_t (*)(int32_t, char*, size_t)>(sym("mock_npc_type_net_name"));
    char buf[64];
    CHECK(npc_name && npc_net_name && npc_name(boss, buf, sizeof(buf)) && std::string(buf) == "golem");
    CHECK(npc_net_name(boss, buf, sizeof(buf)) && std::string(buf) == "literal:golem");
    CHECK(npc_name(eye, buf, sizeof(buf)) && std::string(buf) == "npc:4");

    fs::remove_all(dir);
    std::printf("\n%s (%d falha%s)\n", g_failures ? "FALHOU" : "TODOS OS TESTES PASSARAM", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
