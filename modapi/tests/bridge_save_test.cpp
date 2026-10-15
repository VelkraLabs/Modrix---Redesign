// Ponta a ponta no host: runtime + BridgeGameAdapter + ponte IL2CPP contra a
// libil2cpp.so falsa (il2cpp_bridge/test/mock_il2cpp.cpp). Confere o
// adaptador de verdade (contêineres, baús pela posição, itens temporários),
// que o runtime_test não exercita (ele usa o FakeGame).
#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <tuple>

#include "bridge.h"
#include "bridge_adapter.h"
#include "tlmod/runtime/item_persistence.h"
#include "tlmod/runtime/log.h"
#include "tlmod/runtime/mod_loader.h"

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
        if (api->register_item(ctx, &d) == TLMOD_INVALID_ID) return 1;
    }
    return 0;
}

std::u16string U16(const std::string& ascii) { return std::u16string(ascii.begin(), ascii.end()); }

TLModItemFields Get(Il2CppObject* player, TLModItemContainer c, int32_t group, int32_t index) {
    TLModItemFields f{};
    TLMod_ReadItem(TLMod_ItemSlot(player, c, group, index), &f);
    return f;
}
void Put(Il2CppObject* player, TLModItemContainer c, int32_t group, int32_t index, int32_t type, int32_t stack) {
    TLModItemFields f{type, type, stack, 0, false};
    TLMod_WriteItem(TLMod_ItemSlot(player, c, group, index), &f);
}

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
    CHECK(Get(player, TLMOD_ITEMS_INVENTORY, 0, 4).type == ruby && Get(player, TLMOD_ITEMS_HELD, 0, 0).type == ruby);
    CHECK(Get(player, TLMOD_ITEMS_LOADOUT_DYE, 1, 9).type == ruby && Get(player, TLMOD_ITEMS_BANK, 0, 0).stack == 3);
    std::string err;
    auto side = ItemPersistence::ReadSidecar(plr + ItemPersistence::kSidecarSuffix, &err);
    CHECK(side && side->size() == 5);

    // Novo load: o jogo leu o .plr (sem os itens de mod); o runtime reinsere.
    for (auto [c, g, i] : {std::tuple{TLMOD_ITEMS_INVENTORY, 0, 4}, {TLMOD_ITEMS_DYE, 0, 2}, {TLMOD_ITEMS_BANK, 0, 0},
                           {TLMOD_ITEMS_LOADOUT_DYE, 1, 9}, {TLMOD_ITEMS_HELD, 0, 0}})
        Put(player, c, g, i, 0, 0);
    CHECK(load_player(U16(plr).c_str(), false) != nullptr);
    TLModItemFields inv = Get(player, TLMOD_ITEMS_INVENTORY, 0, 4);
    CHECK(inv.type == ruby && inv.netID == ruby && inv.stack == 12);
    CHECK(Get(player, TLMOD_ITEMS_DYE, 0, 2).type == topaz && Get(player, TLMOD_ITEMS_BANK, 0, 0).stack == 3);
    CHECK(Get(player, TLMOD_ITEMS_LOADOUT_DYE, 1, 9).type == ruby);
    // O mouse volta pelos temporários lidos do arquivo (Player._temporaryItemSlots).
    CHECK(Get(player, TLMOD_ITEMS_TEMPORARY, 0, 0).type == ruby && Get(player, TLMOD_ITEMS_TEMPORARY, 0, 0).stack == 2);
    CHECK(Get(player, TLMOD_ITEMS_HELD, 0, 0).type == 0);

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
    CHECK(!threw && maxSaved() == 9 && Get(nullptr, TLMOD_ITEMS_CHEST, 3, 6).type == topaz);
    side = ItemPersistence::ReadSidecar(wld + ItemPersistence::kSidecarSuffix, &err);
    CHECK(side && side->size() == 1 && (*side)[0].slot.chestX == 77 && (*side)[0].slot.chestY == 88);

    // O load renumera os baús: o mesmo baú agora em Main.chest[0].
    remove_chest(3);
    add_chest(0, 77, 88);
    Put(nullptr, TLMOD_ITEMS_CHEST, 0, 7, 9, 2);
    load_world(false);
    CHECK(Get(nullptr, TLMOD_ITEMS_CHEST, 0, 6).type == topaz && Get(nullptr, TLMOD_ITEMS_CHEST, 0, 6).stack == 40);
    CHECK(loader.Persistence().Orphans(wld).empty());

    fs::remove_all(dir);
    std::printf("\n%s (%d falha%s)\n", g_failures ? "FALHOU" : "TODOS OS TESTES PASSARAM", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
