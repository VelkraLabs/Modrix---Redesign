// Teste de host: carrega a libil2cpp.so falsa, roda TLMod_Init como no jogo
// (esperando o "il2cpp_init"), lê statLife, chama Heal via runtime_invoke e
// dispara Player.Hurt pelo caminho do hook.
#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "bridge.h"

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                   \
        }                                                               \
        std::printf("ok  %s\n", #cond);                                 \
    } while (0)

namespace {
bool (*mock_patch_function)(void*, void*);
double (*mock_game_hurt_player)(void*, int32_t);

TLModHurtEvent g_lastPost{};
int g_preCalls = 0;

// Instalador de "hook" do teste: no Android isso seria DobbyHook/ShadowHook.
bool TestInstaller(void* target, void* replacement, void** original) {
    *original = target;  // a função original continua intacta no mock
    return mock_patch_function(target, replacement);
}

// Callback de mod de exemplo: corta o dano pela metade e bloqueia golpes > 1000.
bool HalfDamage(TLModHurtEvent* ev, bool after) {
    if (after) {
        g_lastPost = *ev;
        return true;
    }
    ++g_preCalls;
    if (ev->damage > 1000) return false;
    ev->damage /= 2;
    return true;
}
// --- NPCs: o mesmo esquema que a camada de mods usa --------------------------
// Tabela lateral: slot de Main.npc -> ID de mod (-1 = NPC original).
int32_t g_modSlot[200];
int g_setDefaultsCalls = 0;
int32_t g_lastSetDefaultsIndex = -2;

int32_t RemapSpawn(int32_t type) { return type >= 4096 ? 4 : type; }  // casca: Eye of Cthulhu
void OnSpawned(Il2CppObject*, int32_t index, int32_t requested, int32_t spawned) {
    if (index >= 0 && index < 200 && requested != spawned) g_modSlot[index] = requested;
}
void OnSetDefaults(Il2CppObject*, int32_t index, int32_t) {
    ++g_setDefaultsCalls;
    g_lastSetDefaultsIndex = index;
    if (index >= 0) g_modSlot[index] = -1;  // o slot mudou de dono
}
bool OnAi(Il2CppObject* npc, int32_t index) {
    if (index < 0 || g_modSlot[index] < 0) return false;  // NPC original: AI original
    TLModNpcFields f{};
    TLMod_ReadNpc(npc, &f);
    f.ai[1] += 10.0f;  // "comportamento do mod"
    f.vx = -2.0f;
    TLMod_WriteNpc(npc, &f);
    return true;  // substitui a AI original
}

// --- Frame / estado do mundo -----------------------------------------------------
int g_frameCalls = 0;
int32_t g_lastGameMenu = -2;
void OnFrame() {
    ++g_frameCalls;
    g_lastGameMenu = TLMod_IsGameMenu();  // aqui estamos na thread do jogo
}

// --- Save/load: o mesmo esquema que o runtime usa (tira antes, devolve depois) ---
constexpr int32_t kItemCount = 5456;  // ItemID.Count

struct Stripped {
    TLModItemContainer container;
    int32_t group, index;
    TLModItemFields fields;
};
std::vector<Stripped> g_stripped;
std::vector<std::string> g_saveLog;  // "fase:tipo:caminho:ok"
bool g_sawAirDuringSave = false;

// Todos os slots que o save do personagem grava (+ baús, para o mundo).
template <typename Fn>
void ForEachSlot(Il2CppObject* player, Fn fn) {
    static const TLModItemContainer kPlayer[] = {
        TLMOD_ITEMS_INVENTORY, TLMOD_ITEMS_ARMOR, TLMOD_ITEMS_DYE,   TLMOD_ITEMS_MISC_EQUIPS, TLMOD_ITEMS_MISC_DYES,
        TLMOD_ITEMS_BANK,      TLMOD_ITEMS_BANK2, TLMOD_ITEMS_BANK3, TLMOD_ITEMS_BANK4,       TLMOD_ITEMS_HELD};
    if (player) {
        for (TLModItemContainer c : kPlayer)
            for (int32_t i = 0; i < TLMod_ItemSlotCount(player, c, 0); ++i) fn(c, 0, i);
        for (int32_t l = 0; l < 3; ++l)
            for (TLModItemContainer c : {TLMOD_ITEMS_LOADOUT_ARMOR, TLMOD_ITEMS_LOADOUT_DYE})
                for (int32_t i = 0; i < TLMod_ItemSlotCount(player, c, l); ++i) fn(c, l, i);
        return;
    }
    for (int32_t c = 0; c < TLMod_ChestCount(); ++c)
        for (int32_t i = 0; i < TLMod_ItemSlotCount(nullptr, TLMOD_ITEMS_CHEST, c); ++i) fn(TLMOD_ITEMS_CHEST, c, i);
}

void OnSave(const TLModSaveEvent* ev, TLModSavePhase phase) {
    static const char* kPhase[] = {"antes", "depois", "carregou"};
    g_saveLog.push_back(std::string(kPhase[phase]) + ":" + (ev->kind == TLMOD_SAVE_PLAYER ? "player" : "world") +
                        ":" + ev->path + ":" + (ev->ok ? "ok" : "erro"));
    if (phase == TLMOD_BEFORE_SAVE) {
        g_stripped.clear();
        ForEachSlot(ev->player, [&](TLModItemContainer c, int32_t g, int32_t i) {
            Il2CppObject* item = TLMod_ItemSlot(ev->player, c, g, i);
            TLModItemFields f{};
            if (!TLMod_ReadItem(item, &f) || f.type < kItemCount) return;
            g_stripped.push_back({c, g, i, f});
            TLModItemFields air{};
            TLMod_WriteItem(item, &air);
        });
    } else if (phase == TLMOD_AFTER_SAVE) {
        for (const Stripped& st : g_stripped) {
            Il2CppObject* item = TLMod_ItemSlot(ev->player, st.container, st.group, st.index);
            TLModItemFields now{};
            if (TLMod_ReadItem(item, &now) && now.type == 0 && now.stack == 0) g_sawAirDuringSave = true;
            TLMod_WriteItem(item, &st.fields);
        }
        g_stripped.clear();
    }
}

// Põe um "item de mod" (ID fora de ItemID.Count) num slot.
bool PutItem(Il2CppObject* player, TLModItemContainer c, int32_t group, int32_t index, int32_t type, int32_t stack) {
    TLModItemFields f{type, type, stack, 3, true};
    return TLMod_WriteItem(TLMod_ItemSlot(player, c, group, index), &f);
}
int32_t TypeAt(Il2CppObject* player, TLModItemContainer c, int32_t group, int32_t index) {
    TLModItemFields f{};
    return TLMod_ReadItem(TLMod_ItemSlot(player, c, group, index), &f) ? f.type : -1;
}

// --- Itens de mod: casca + identidade em netID ------------------------------------
constexpr int32_t kModItem = 8192, kShell = 38;
int g_onDefaults = 0, g_onRebuild = 0;
int32_t ItemRemap(int32_t type) { return type >= kItemCount ? kShell : type; }
void ItemApply(Il2CppObject* item, int32_t requested, int32_t shell) {
    ++g_onDefaults;
    if (requested != kModItem || shell != kShell) return;
    TLModItemStats st{};
    TLMod_ReadItemStats(item, &st);
    st.damage = 7;
    st.useTime = st.useAnimation = 20;
    st.useStyle = 1;
    st.melee = true;
    st.maxStack = 999;
    st.value = 5000;
    st.rare = 2;
    TLMod_WriteItemStats(item, &st);
    TLMod_SetItemName(item, "Rubi Fict\xc3\xad" "cio");
    TLMod_SetItemTooltip(item, "Linha 1\nLinha 2");
}
void ItemTooltip(Il2CppObject* item, int32_t) {
    ++g_onRebuild;
    TLMod_SetItemTooltip(item, "Linha 1\nLinha 2");
}

int TestItems(void* lib) {
    auto sym = [&](const char* name) { return dlsym(lib, name); };
    auto new_item = reinterpret_cast<void* (*)()>(sym("mock_new_item"));
    auto set_defaults = reinterpret_cast<void (*)(void*, int32_t)>(sym("mock_game_item_set_defaults"));
    auto rebuild = reinterpret_cast<void (*)(void*)>(sym("mock_game_rebuild_tooltip"));
    auto clone = reinterpret_cast<void* (*)(void*)>(sym("mock_game_clone_item"));
    auto name_of = reinterpret_cast<size_t (*)(void*, char*, size_t)>(sym("mock_item_name"));
    auto tooltip_of = reinterpret_cast<size_t (*)(void*, char*, size_t)>(sym("mock_item_tooltip"));
    auto barriers = reinterpret_cast<int (*)()>(sym("mock_barriers"));
    auto handles = reinterpret_cast<size_t (*)()>(sym("mock_gchandles"));
    CHECK(new_item && set_defaults && rebuild && clone && name_of && tooltip_of && barriers && handles);
    auto name = [&](void* it) { char b[128]; name_of(it, b, sizeof b); return std::string(b); };
    auto tooltip = [&](void* it) { char b[128]; tooltip_of(it, b, sizeof b); return std::string(b); };
    auto fields = [](void* it) { TLModItemFields f{}; TLMod_ReadItem(static_cast<Il2CppObject*>(it), &f); return f; };
    auto stats = [](void* it) { TLModItemStats s{}; TLMod_ReadItemStats(static_cast<Il2CppObject*>(it), &s); return s; };

    TLModItemCallbacks cbs{&ItemRemap, &ItemApply, &ItemTooltip};
    TLMod_SetItemCallbacks(&cbs);

    // O jogo pede o item 8192: as tabelas (que abortariam) só veem a casca 38.
    void* ruby = new_item();
    set_defaults(ruby, kModItem);
    CHECK(fields(ruby).type == kShell && fields(ruby).netID == kModItem && fields(ruby).stack == 1);
    TLModItemStats st = stats(ruby);
    CHECK(st.damage == 7 && st.useTime == 20 && st.maxStack == 999 && st.value == 5000 && st.rare == 2 && st.melee);
    CHECK(name(ruby) == "Rubi Fict\xc3\xad" "cio");  // o UTF-8 virou System.String e voltou
    CHECK(tooltip(ruby) == "Linha 1\nLinha 2");   // ItemTooltip -> LocalizedText, ainda por validar
    int barriersAfterFirst = barriers();
    CHECK(barriersAfterFirst >= 3);  // _nameOverride, ToolTip e ItemTooltip._text com barreira

    // Tela de criação reconstrói o tooltip: Lang.GetTooltip(8192) estouraria.
    rebuild(ruby);
    CHECK(g_onRebuild == 1 && tooltip(ruby) == "Linha 1\nLinha 2");

    // Clone (MemberwiseClone) leva a identidade; SetDefaults pela API passa pelo hook.
    void* copy = clone(ruby);
    CHECK(fields(copy).netID == kModItem && name(copy) == name(ruby));
    size_t handlesBefore = handles();
    void* second = new_item();
    CHECK(TLMod_SetItemDefaults(static_cast<Il2CppObject*>(second), kModItem));
    CHECK(fields(second).type == kShell && fields(second).netID == kModItem && stats(second).damage == 7);
    CHECK(handles() == handlesBefore);  // nome e tooltip vêm do cache
    CHECK(TLMod_ApplyItemPrefix(static_cast<Il2CppObject*>(second), 5) && fields(second).prefix == 5);

    // Item original: o hook só repassa.
    void* torch = new_item();
    set_defaults(torch, 8);
    CHECK(fields(torch).type == 8 && fields(torch).netID == 8 && name(torch) == "vanilla:8" &&
          tooltip(torch) == "vanilla");
    // Um item de mod que vira original (ex.: TurnToAir/SetDefaults(0)) perde a identidade.
    set_defaults(ruby, 0);
    CHECK(fields(ruby).netID == 0 && name(ruby) == "vanilla:0");

    // Sem callbacks (runtime desligado), um netID de mod ainda não estoura: tooltip vazio.
    TLMod_SetItemCallbacks(nullptr);
    rebuild(copy);
    CHECK(tooltip(copy) == "none");
    return 0;
}

// --- Texturas de mod: criação pelo Unity e troca por desenho -----------------------
int32_t g_rubyTex = 0, g_sheetTex = 0, g_modNpcIndex = -1;
int32_t DrawItemTexture(Il2CppObject*, int32_t netId) { return netId == kModItem ? g_rubyTex : 0; }
bool DrawNpcTexture(Il2CppObject*, int32_t index, TLModNpcDraw* out) {
    if (index != g_modNpcIndex) return false;
    *out = TLModNpcDraw{g_sheetTex, 4, 2};
    return true;
}

int TestDraw(void* lib) {
    auto sym = [&](const char* name) { return dlsym(lib, name); };
    auto load_assets = reinterpret_cast<void (*)()>(sym("mock_load_texture_assets"));
    auto draw_inv = reinterpret_cast<void* (*)(int32_t)>(sym("mock_game_draw_inventory"));
    auto draw_hotbar = reinterpret_cast<void* (*)(int32_t)>(sym("mock_game_draw_hotbar"));
    auto draw_world = reinterpret_cast<void* (*)(void*)>(sym("mock_game_draw_world_item"));
    auto draw_icon = reinterpret_cast<void* (*)(void*)>(sym("mock_game_draw_item_icon"));
    auto draw_held = reinterpret_cast<void* (*)(void*)>(sym("mock_game_draw_held"));
    auto draw_npc = reinterpret_cast<void* (*)(int32_t, int32_t*, int32_t*)>(sym("mock_game_draw_npc"));
    auto item_tex = reinterpret_cast<void* (*)(int32_t)>(sym("mock_item_asset_texture"));
    auto npc_tex = reinterpret_cast<void* (*)(int32_t)>(sym("mock_npc_asset_texture"));
    auto frame_count = reinterpret_cast<int32_t (*)(int32_t)>(sym("mock_npc_frame_count"));
    auto tex_info = reinterpret_cast<bool (*)(void*, int32_t*, int32_t*, int32_t*, int32_t*, bool*)>(sym("mock_texture_info"));
    auto tex_pixel = reinterpret_cast<uint32_t (*)(void*, int32_t, int32_t)>(sym("mock_texture_pixel"));
    auto new_npc = reinterpret_cast<int32_t (*)(int32_t, int32_t, int32_t)>(sym("mock_game_new_npc"));
    CHECK(load_assets && draw_inv && draw_hotbar && draw_world && draw_icon && draw_held && draw_npc && item_tex &&
          npc_tex && frame_count && tex_info && tex_pixel && new_npc);

    // 2x2 RGBA, linha 0 = topo: vermelho, verde / azul, branco.
    const uint32_t kRed = 0xFF0000FF, kGreen = 0xFF00FF00, kBlue = 0xFFFF0000, kWhite = 0xFFFFFFFF;
    const uint32_t px[] = {kRed, kGreen, kBlue, kWhite};
    const uint8_t* rgba = reinterpret_cast<const uint8_t*>(px);
    CHECK(TLMod_CreateTexture(rgba, 2, 2, "cedo") == -1);  // TextureAssets ainda vazio: tentar depois
    load_assets();
    g_rubyTex = TLMod_CreateTexture(rgba, 2, 2, "test:ruby");
    CHECK(g_rubyTex == 1);
    CHECK(TLMod_CreateTexture(rgba, 0, 2, "zero") == 0 && TLMod_CreateTexture(nullptr, 2, 2, "nulo") == 0);
    // Folha de NPC 8x32: 4 quadros de 8x8.
    std::vector<uint32_t> sheet(8 * 32, kBlue);
    g_sheetTex = TLMod_CreateTexture(reinterpret_cast<const uint8_t*>(sheet.data()), 8, 32, "test:boss");
    int32_t w = 0, h = 0;
    CHECK(g_sheetTex == 2 && TLMod_TextureSize(g_sheetTex, &w, &h) && w == 8 && h == 32 && !TLMod_TextureSize(3, &w, &h));

    TLModItemCallbacks off{};
    TLMod_SetItemCallbacks(&off);  // itens montados à mão abaixo
    Il2CppObject* player = TLMod_GetLocalPlayer();
    auto make = [&](int32_t slot, int32_t type, int32_t netId) {
        Il2CppObject* it = TLMod_ItemSlot(player, TLMOD_ITEMS_INVENTORY, 0, slot);
        TLModItemFields f{type, netId, 1, 0, false};
        TLMod_WriteItem(it, &f);
        return it;
    };
    Il2CppObject* ruby = make(0, kShell, kModItem);  // item de mod: casca 38
    Il2CppObject* lens = make(1, kShell, kShell);    // uma Lente de verdade
    Il2CppObject* noTex = make(2, kShell, kModItem + 1);  // de mod, sem textura
    void* lensTex = item_tex(kShell);

    TLModDrawCallbacks cbs{&DrawItemTexture, &DrawNpcTexture};
    TLMod_SetDrawCallbacks(&cbs);
    void* modTex = draw_inv(0);
    CHECK(modTex != lensTex && modTex != nullptr);
    bool applied = false;
    int32_t filter = -1, wrap = -1;
    CHECK(tex_info(modTex, &w, &h, &filter, &wrap, &applied) && w == 2 && h == 2 && applied);
    CHECK(filter == 0 && wrap == 1);  // Point (pixel art) e Clamp
    // O Unity guarda de baixo para cima: o topo do PNG continua no topo.
    CHECK(tex_pixel(modTex, 0, 0) == kRed && tex_pixel(modTex, 1, 0) == kGreen && tex_pixel(modTex, 0, 1) == kBlue);
    CHECK(item_tex(kShell) == lensTex);           // a casca voltou depois do desenho
    CHECK(draw_inv(1) == lensTex);                // a Lente de verdade continua Lente
    CHECK(draw_inv(2) == lensTex);                // de mod sem textura: a da casca
    CHECK(draw_hotbar(0) == modTex && draw_world(ruby) == modTex && draw_icon(ruby) == modTex);
    CHECK(draw_held(ruby) == modTex && draw_held(lens) == lensTex && draw_world(noTex) == lensTex);
    CHECK(item_tex(kShell) == lensTex);

    // NPC: folha de 4 quadros; o 3º (frame 2). Main.npcFrameCount[4] e o
    // NPC.frame voltam ao que eram depois do desenho.
    int32_t vanillaIndex = new_npc(0, 0, 4), modIndex = new_npc(10, 10, 4);
    g_modNpcIndex = modIndex;
    void* eyeTex = npc_tex(4);
    int32_t frames = 0, frame[4] = {};
    void* bossTex = draw_npc(modIndex, &frames, frame);
    CHECK(bossTex != eyeTex && frames == 4 && frame[0] == 0 && frame[1] == 16 && frame[2] == 8 && frame[3] == 8);
    CHECK(npc_tex(4) == eyeTex && frame_count(4) == 6);
    CHECK(draw_npc(vanillaIndex, &frames, frame) == eyeTex && frames == 6);

    TLMod_SetDrawCallbacks(nullptr);
    CHECK(draw_inv(0) == lensTex && draw_npc(modIndex, &frames, frame) == eyeTex);
    for (int32_t slot = 0; slot < 3; ++slot) make(slot, 0, 0);  // o teste de save usa o mesmo inventário
    return 0;
}

int TestSaves(void* lib) {
    auto sym = [&](const char* name) { return dlsym(lib, name); };
    auto save_player = reinterpret_cast<void (*)()>(sym("mock_game_save_player"));
    auto load_player = reinterpret_cast<void* (*)(const char16_t*, bool)>(sym("mock_game_load_player"));
    auto save_world = reinterpret_cast<void (*)()>(sym("mock_game_save_world"));
    auto load_world = reinterpret_cast<int (*)(bool)>(sym("mock_game_load_world"));
    auto set_world_path = reinterpret_cast<void (*)(const char16_t*)>(sym("mock_set_world_path"));
    auto set_load_status = reinterpret_cast<void (*)(int32_t)>(sym("mock_set_load_status"));
    auto set_throw = reinterpret_cast<void (*)(bool)>(sym("mock_set_throw_on_save"));
    auto saved_ids = reinterpret_cast<size_t (*)(const int32_t**)>(sym("mock_saved_ids"));
    auto add_chest = reinterpret_cast<void (*)(int32_t, int32_t, int32_t)>(sym("mock_add_chest"));
    auto held_item = reinterpret_cast<void* (*)(int32_t)>(sym("mock_held_item"));
    CHECK(save_player && load_player && save_world && load_world && set_world_path && set_load_status && set_throw &&
          saved_ids && add_chest && held_item);
    auto maxSaved = [&] {
        const int32_t* ids = nullptr;
        size_t n = saved_ids(&ids);
        int32_t m = 0;
        for (size_t i = 0; i < n; ++i) m = std::max(m, ids[i]);
        return m;
    };

    // ---- Slots ------------------------------------------------------------------
    Il2CppObject* player = TLMod_GetLocalPlayer();
    CHECK(TLMod_ItemSlotCount(player, TLMOD_ITEMS_INVENTORY, 0) == 58);  // o save grava 0..57
    CHECK(TLMod_ItemSlotCount(player, TLMOD_ITEMS_ARMOR, 0) == 20 && TLMod_ItemSlotCount(player, TLMOD_ITEMS_DYE, 0) == 10);
    CHECK(TLMod_ItemSlotCount(player, TLMOD_ITEMS_BANK4, 0) == 40 && TLMod_ItemSlotCount(player, TLMOD_ITEMS_HELD, 0) == 4);
    CHECK(TLMod_ItemSlotCount(player, TLMOD_ITEMS_LOADOUT_DYE, 2) == 10 &&
          TLMod_ItemSlotCount(player, TLMOD_ITEMS_LOADOUT_DYE, 3) == -1);
    CHECK(TLMod_ItemSlotCount(player, TLMOD_ITEMS_TEMPORARY, 0) == 4);
    CHECK(TLMod_ItemSlotCount(nullptr, TLMOD_ITEMS_INVENTORY, 0) == -1);
    CHECK(TLMod_ItemSlot(player, TLMOD_ITEMS_INVENTORY, 0, 58) == nullptr);
    CHECK(TLMod_ChestCount() == 8000 && TLMod_ItemSlotCount(nullptr, TLMOD_ITEMS_CHEST, 5) == -1);
    add_chest(5, 120, 340);
    int32_t cx = 0, cy = 0;
    CHECK(TLMod_ChestPosition(5, &cx, &cy) && cx == 120 && cy == 340 && !TLMod_ChestPosition(6, &cx, &cy));
    CHECK(TLMod_ItemSlotCount(nullptr, TLMOD_ITEMS_CHEST, 5) == 40);
    // HELD passa pelos getters do jogo (runtime_invoke), inclusive o sacrifício da Jornada.
    CHECK(TLMod_ItemSlot(player, TLMOD_ITEMS_HELD, 0, 0) == held_item(0));
    CHECK(TLMod_ItemSlot(player, TLMOD_ITEMS_HELD, 0, 1) == held_item(1));
    CHECK(TLMod_ItemSlot(player, TLMOD_ITEMS_HELD, 0, 3) == held_item(3));

    // ---- Personagem: itens de mod em vários contêineres ----------------------------
    CHECK(PutItem(player, TLMOD_ITEMS_INVENTORY, 0, 7, 8192, 5));
    CHECK(PutItem(player, TLMOD_ITEMS_ARMOR, 0, 0, 8193, 1));
    CHECK(PutItem(player, TLMOD_ITEMS_BANK3, 0, 39, 8194, 2));
    CHECK(PutItem(player, TLMOD_ITEMS_LOADOUT_ARMOR, 2, 4, 8195, 1));
    CHECK(PutItem(player, TLMOD_ITEMS_HELD, 0, 0, 8196, 1));  // no mouse
    CHECK(PutItem(player, TLMOD_ITEMS_INVENTORY, 0, 8, 25, 9));  // original: fica no arquivo

    // Sem callback, o ID de mod vai para o arquivo (é o problema).
    save_player();
    CHECK(maxSaved() == 8196);

    TLMod_SetSaveCallback(&OnSave);
    save_player();
    CHECK(maxSaved() == 25);  // nenhum ID de mod no arquivo; o original está lá
    CHECK(g_saveLog.size() == 2 && g_saveLog[0] == "antes:player:/saves/Players/Jogador.plr:ok" &&
          g_saveLog[1] == "depois:player:/saves/Players/Jogador.plr:ok");
    CHECK(g_sawAirDuringSave);  // durante o save os slots estavam vazios
    CHECK(TypeAt(player, TLMOD_ITEMS_INVENTORY, 0, 7) == 8192 && TypeAt(player, TLMOD_ITEMS_ARMOR, 0, 0) == 8193);
    CHECK(TypeAt(player, TLMOD_ITEMS_BANK3, 0, 39) == 8194 && TypeAt(player, TLMOD_ITEMS_LOADOUT_ARMOR, 2, 4) == 8195);
    CHECK(TypeAt(player, TLMOD_ITEMS_HELD, 0, 0) == 8196);
    TLModItemFields back{};
    CHECK(TLMod_ReadItem(TLMod_ItemSlot(player, TLMOD_ITEMS_INVENTORY, 0, 7), &back) && back.stack == 5 &&
          back.netID == 8192 && back.prefix == 3 && back.favorited);

    // Exceção no save: o "depois" roda (ok=false), os itens voltam e a
    // exceção chega a quem chamou o save.
    g_saveLog.clear();
    set_throw(true);
    bool threw = false;
    try {
        save_player();
    } catch (...) {
        threw = true;
    }
    set_throw(false);
    CHECK(threw && g_saveLog.size() == 2 && g_saveLog[1] == "depois:player:/saves/Players/Jogador.plr:erro");
    CHECK(TypeAt(player, TLMOD_ITEMS_INVENTORY, 0, 7) == 8192 && TypeAt(player, TLMOD_ITEMS_HELD, 0, 0) == 8196);

    // Load: caminho UTF-16 com acento e emoji (par substituto) chega em UTF-8.
    g_saveLog.clear();
    CHECK(load_player(u"/saves/Players/Café \U0001F600.plr", false) != nullptr);
    CHECK(g_saveLog.size() == 1 && g_saveLog[0] == "carregou:player:/saves/Players/Caf\xc3\xa9 \xf0\x9f\x98\x80.plr:ok");
    set_load_status(1);  // arquivo que o jogo não conseguiu ler
    load_player(u"/x.plr", true);
    CHECK(g_saveLog.back() == "carregou:player:/x.plr:erro");
    set_load_status(0);

    // ---- Mundo: baús ----------------------------------------------------------------
    CHECK(PutItem(nullptr, TLMOD_ITEMS_CHEST, 5, 0, 8200, 30));
    CHECK(PutItem(nullptr, TLMOD_ITEMS_CHEST, 5, 1, 12, 3));
    TLMod_SetSaveCallback(nullptr);
    threw = false;
    try {
        save_world();  // sem o callback, SaveChests estoura no ID de mod (como no jogo)
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
    TLMod_SetSaveCallback(&OnSave);
    g_saveLog.clear();
    set_world_path(u"/saves/Worlds/Ilha.wld");
    save_world();
    CHECK(maxSaved() == 12 && TypeAt(nullptr, TLMOD_ITEMS_CHEST, 5, 0) == 8200);
    CHECK(g_saveLog.size() == 2 && g_saveLog[1] == "depois:world:/saves/Worlds/Ilha.wld:ok");
    CHECK(load_world(false) == 1 && g_saveLog.back() == "carregou:world:/saves/Worlds/Ilha.wld:ok");
    CHECK(load_world(true) == 2 && g_saveLog.back() == "carregou:world:/saves/Worlds/Ilha.wld:erro");
    TLMod_SetSaveCallback(nullptr);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s /caminho/libil2cpp.so\n", argv[0]);
        return 2;
    }
    // "O jogo" carrega a libil2cpp.so; o runtime ainda não foi inicializado.
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    CHECK(lib != nullptr);
    auto mock_init = reinterpret_cast<void (*)()>(dlsym(lib, "mock_il2cpp_init"));
    auto mock_local = reinterpret_cast<void* (*)()>(dlsym(lib, "mock_local_player"));
    mock_patch_function = reinterpret_cast<bool (*)(void*, void*)>(dlsym(lib, "mock_patch_function"));
    mock_game_hurt_player = reinterpret_cast<double (*)(void*, int32_t)>(dlsym(lib, "mock_game_hurt_player"));
    CHECK(mock_init && mock_local && mock_patch_function && mock_game_hurt_player);

    TLMod_SetLibraryName(argv[1]);
    TLMod_SetHookInstaller(&TestInstaller);
    TLMod_SetHurtCallback(&HalfDamage);

    // TLMod_Init tem de esperar o il2cpp_init, que acontece 300 ms depois.
    bool initOk = false;
    std::thread th([&] { initOk = TLMod_Init(5000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    mock_init();
    th.join();
    CHECK(initOk);
    CHECK(TLMod_OriginalPlayerHurt() != nullptr);

    Il2CppObject* player = TLMod_GetLocalPlayer();
    CHECK(player == mock_local());
    CHECK(TLMod_GetStatLife(player) == 400);
    CHECK(TLMod_GetPlayerIndex(player) == 0);
    CHECK(TLMod_GetPlayerByIndex(0) == player);
    CHECK(TLMod_GetPlayerByIndex(1) == nullptr);
    CHECK(TLMod_GetPlayerByIndex(255) == nullptr);
    CHECK(TLMod_WaitReady(0));

    CHECK(TLMod_Heal(player, 50));
    CHECK(TLMod_GetStatLife(player) == 450);

    // Dano 100 -> callback corta para 50 -> defesa 10 tira 5 -> 45 de dano.
    double dealt = mock_game_hurt_player(player, 100);
    CHECK(g_preCalls == 1);
    CHECK(dealt == 45.0);
    CHECK(g_lastPost.lifeBefore == 450);
    CHECK(g_lastPost.lifeAfter == 405);
    CHECK(TLMod_GetStatLife(player) == 405);

    // Golpe cancelado pelo mod: vida inalterada, retorno 0.
    CHECK(mock_game_hurt_player(player, 5000) == 0.0);
    CHECK(TLMod_GetStatLife(player) == 405);

    // ---- NPCs --------------------------------------------------------------
    auto mock_game_new_npc = reinterpret_cast<int32_t (*)(int32_t, int32_t, int32_t)>(dlsym(lib, "mock_game_new_npc"));
    auto mock_game_update_npcs = reinterpret_cast<void (*)()>(dlsym(lib, "mock_game_update_npcs"));
    auto mock_game_transform = reinterpret_cast<void (*)(int32_t, int32_t)>(dlsym(lib, "mock_game_transform"));
    auto mock_npc = reinterpret_cast<void* (*)(int32_t)>(dlsym(lib, "mock_npc"));
    CHECK(mock_game_new_npc && mock_game_update_npcs && mock_game_transform && mock_npc);
    for (int32_t& v : g_modSlot) v = -1;
    TLModNpcCallbacks cbs{&RemapSpawn, &OnSpawned, &OnSetDefaults, &OnAi};
    TLMod_SetNpcCallbacks(&cbs);

    // Spawn original: passa direto.
    CHECK(mock_game_new_npc(100, 200, 2) == 0);
    TLModNpcFields f{};
    CHECK(TLMod_ReadNpc(TLMod_GetNpcByIndex(0), &f) && f.type == 2 && f.x == 100.0f && g_modSlot[0] == -1);

    // Spawn de mod: 4096 vira casca 4 antes do SetDefaults original (que
    // abortaria com 4096); o slot fica marcado como NPC de mod.
    CHECK(mock_game_new_npc(300, 400, 4096) == 1);
    CHECK(TLMod_ReadNpc(TLMod_GetNpcByIndex(1), &f) && f.type == 4 && f.lifeMax == 104);
    CHECK(g_modSlot[1] == 4096);
    // Dentro de NewNPC o whoAmI ainda era 0: o índice veio da busca em Main.npc.
    CHECK(g_lastSetDefaultsIndex == 1);
    CHECK(TLMod_GetNpcByIndex(1) == mock_npc(1) && TLMod_FindNpcIndex(static_cast<Il2CppObject*>(mock_npc(1))) == 1);

    // Um tick: NPC original roda a AI original; o de mod roda a do mod.
    mock_game_update_npcs();
    TLMod_ReadNpc(TLMod_GetNpcByIndex(0), &f);
    CHECK(f.ai[0] == 1.0f && f.vx == 1.0f);
    TLMod_ReadNpc(TLMod_GetNpcByIndex(1), &f);
    CHECK(f.ai[0] == 0.0f && f.ai[1] == 10.0f && f.vx == -2.0f);

    // Transform (fase de boss, por exemplo) vira tipo original: slot desmarcado.
    mock_game_transform(1, 5);
    CHECK(g_modSlot[1] == -1);
    mock_game_update_npcs();
    TLMod_ReadNpc(TLMod_GetNpcByIndex(1), &f);
    CHECK(f.type == 5 && f.ai[0] == 1.0f);

    // Spawn pedido pela camada de mods (runtime_invoke -> NewNPC com hook).
    CHECK(TLMod_SpawnNpc(10, 20, 4097) == 2);
    CHECK(g_modSlot[2] == 4097 && TLMod_ReadNpc(TLMod_GetNpcByIndex(2), &f) && f.type == 4);

    // ---- Frame e estado do mundo ------------------------------------------------
    auto mock_become_game_thread = reinterpret_cast<void (*)()>(dlsym(lib, "mock_become_game_thread"));
    auto mock_set_game_menu = reinterpret_cast<void (*)(bool)>(dlsym(lib, "mock_set_game_menu"));
    auto mock_set_flag = reinterpret_cast<bool (*)(const char*, bool)>(dlsym(lib, "mock_set_flag"));
    auto mock_game_frame = reinterpret_cast<int (*)()>(dlsym(lib, "mock_game_frame"));
    CHECK(mock_become_game_thread && mock_set_game_menu && mock_set_flag && mock_game_frame);
    mock_become_game_thread();  // esta thread passa a ser a "thread do jogo"
    TLMod_SetFrameCallback(&OnFrame);

    CHECK(mock_game_frame() == 1 && g_frameCalls == 1 && g_lastGameMenu == 1);  // no menu
    mock_set_game_menu(false);
    CHECK(mock_game_frame() == 2 && g_lastGameMenu == 0);                     // num mundo
    // Fora da thread do jogo a Instance ThreadStatic é nula: -1, nunca crash.
    int32_t otherThread = -2;
    std::thread([&] { otherThread = TLMod_IsGameMenu(); }).join();
    CHECK(otherThread == -1);

    TLModStaticField* boss1 = TLMod_FindStaticField("Terraria", "NPC", "downedBoss1");
    TLModStaticField* hard = TLMod_FindStaticField("Terraria", "Main", "hardMode");
    CHECK(boss1 && hard && !TLMod_FindStaticField("Terraria", "NPC", "downedNothing"));
    bool b = true;
    CHECK(TLMod_ReadStaticBool(boss1, &b) && !b);
    mock_set_flag("downedBoss1", true);
    mock_set_flag("hardMode", true);
    CHECK(TLMod_ReadStaticBool(boss1, &b) && b);
    CHECK(TLMod_ReadStaticBool(hard, &b) && b);

    if (int rc = TestItems(lib)) return rc;
    if (int rc = TestDraw(lib)) return rc;
    if (int rc = TestSaves(lib)) return rc;

    std::printf("TODOS OS TESTES PASSARAM\n");
    return 0;
}
