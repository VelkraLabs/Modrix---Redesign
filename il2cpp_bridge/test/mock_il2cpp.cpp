// libil2cpp.so FALSA para teste no host (x86_64/arm64 Linux).
//
// Exporta os mesmos símbolos que a ponte resolve via dlsym e simula
// Terraria.Player / Terraria.Main com os offsets reais do dump
// (statLife @0x704, statDefense @0x6F8 ...). Não tem nada da Unity dentro.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {

typedef void (*Il2CppMethodPointer)();

struct Il2CppObject {
    void* klass;
    void* monitor;
};
typedef Il2CppObject Il2CppException;

struct MethodInfo {
    Il2CppMethodPointer methodPointer;  // mesmo primeiro campo do MethodInfo real
    const char* name;
    int argc;
    const char* param0 = nullptr;  // nome do tipo do 1º parâmetro (il2cpp_method_get_param)
};

struct FieldInfo {
    const char* name;
    size_t offset;
    void* staticAddr;              // não nulo para campos estáticos
    size_t size = sizeof(void*);   // bytes copiados por il2cpp_field_static_get_value
    void* (*tls)() = nullptr;      // campos [ThreadStatic]: endereço na thread atual
};

struct Il2CppClass {
    const char* ns;
    const char* name;
    std::vector<FieldInfo*> fields;
    std::vector<MethodInfo*> methods;
    size_t size = 0x200;  // il2cpp_object_new
    std::vector<Il2CppClass*> nested = {};  // il2cpp_class_get_nested_types
};

struct Il2CppImage {
    const char* name;
    std::vector<Il2CppClass*> classes;
};
struct Il2CppAssembly {
    Il2CppImage* image;
};
struct Il2CppDomain {
    int dummy;
};
struct Il2CppThread {
    int dummy;
};
struct Il2CppType;

// Il2CppArray em 64 bits: klass, monitor, bounds, max_length, depois os dados (0x20).
struct Il2CppArray {
    void* klass;
    void* monitor;
    void* bounds;
    uintptr_t max_length;
};

Il2CppObject* il2cpp_string_new(const char* utf8);

}  // extern "C"

namespace {

constexpr size_t kStatDefense = 0x6F8;
constexpr size_t kStatLifeMax2 = 0x700;
constexpr size_t kStatLife = 0x704;
constexpr size_t kPlayerSize = 0xD80;

std::atomic<bool> g_initialized{false};  // a thread de TLMod_Init consulta em loop
Il2CppImage g_corlib{"mscorlib.dll", {}};
Il2CppImage g_csharp{"Assembly-CSharp.dll", {}};
Il2CppAssembly g_asmCorlib{&g_corlib};
Il2CppAssembly g_asmCsharp{&g_csharp};
Il2CppDomain g_domain{};
Il2CppThread g_thread{};
thread_local Il2CppThread* t_current = nullptr;

Il2CppClass g_player{"Terraria", "Player", {}, {}};
Il2CppClass g_main{"Terraria", "Main", {}, {}};
FieldInfo g_fStatLife{"statLife", kStatLife, nullptr};
FieldInfo g_fWhoAmI{"whoAmI", 0x10, nullptr};  // de Entity; o IL2CPP real busca nas classes-base
FieldInfo g_fPlayerEntity[] = {{"active", 0x14, nullptr}, {"position", 0x20, nullptr},
                               {"width", 0x48, nullptr},  {"height", 0x4C, nullptr},
                               {"dead", 0x534, nullptr}};

alignas(16) uint8_t g_localPlayer[kPlayerSize];
alignas(16) uint8_t g_playersArray[sizeof(Il2CppArray) + 255 * sizeof(void*)];
Il2CppArray* g_mainPlayer = reinterpret_cast<Il2CppArray*>(g_playersArray);
FieldInfo g_fMainPlayer{"player", 0x678, &g_mainPlayer};
int32_t g_myPlayer = 0;

struct BoxedInt {
    Il2CppObject header;
    int32_t value;
};
BoxedInt g_box{};
struct BoxedDouble {
    Il2CppObject header;
    double value;
};
BoxedDouble g_dbl{};

int32_t& I32(void* obj, size_t off) {
    return *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(obj) + off);
}

// "Código do jogo" para Player.Hurt: mesma ABI que o IL2CPP gera.
double MockPlayerHurt(Il2CppObject* self, Il2CppObject*, int32_t damage, int32_t, bool, bool,
                      bool, int32_t, bool, const MethodInfo*) {
    int32_t eff = std::max(1, damage - I32(self, kStatDefense) / 2);
    I32(self, kStatLife) -= eff;
    return eff;
}
void MockPlayerHeal(Il2CppObject* self, int32_t amount, const MethodInfo*) {
    I32(self, kStatLife) = std::min(I32(self, kStatLife) + amount, I32(self, kStatLifeMax2));
}

MethodInfo g_mHurt{reinterpret_cast<Il2CppMethodPointer>(&MockPlayerHurt), "Hurt", 8};
MethodInfo g_mHeal{reinterpret_cast<Il2CppMethodPointer>(&MockPlayerHeal), "Heal", 1};
MethodInfo g_mGetMyPlayer{nullptr, "get_myPlayer", 0};

// Ponto de chamada do "jogo". Um inline hook real reescreve o início de
// MockPlayerHurt; aqui o "patch" troca este ponteiro (mock_patch_function).
using HurtFn = decltype(&MockPlayerHurt);
HurtFn g_hurtEntry = &MockPlayerHurt;

// ---- NPCs ---------------------------------------------------------------------
// Offsets do dump (terraria_offsets.h, npc_off).
constexpr size_t kNpcWhoAmI = 0x10, kNpcActive = 0x14, kNpcPos = 0x20, kNpcVel = 0x28, kNpcType = 0x154,
                 kNpcAi = 0x158, kNpcTarget = 0x178, kNpcDamage = 0x17C, kNpcDefense = 0x180,
                 kNpcLife = 0x1A0, kNpcLifeMax = 0x1A4, kNpcSize = 0x260;
constexpr int kMaxNpcs = 200;
constexpr int32_t kNpcVanillaCount = 688;

struct FloatArray4 {
    Il2CppArray header;
    float data[4];
};
alignas(16) uint8_t g_npcs[kMaxNpcs][kNpcSize];
FloatArray4 g_npcAi[kMaxNpcs];
alignas(16) uint8_t g_npcArray[sizeof(Il2CppArray) + kMaxNpcs * sizeof(void*)];
Il2CppArray* g_mainNpc = reinterpret_cast<Il2CppArray*>(g_npcArray);

template <typename T>
T& At(void* obj, size_t off) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(obj) + off);
}

Il2CppClass g_npc{"Terraria", "NPC", {}, {}};
FieldInfo g_fNpc[] = {{"whoAmI", kNpcWhoAmI, nullptr},   {"active", kNpcActive, nullptr},
                      {"position", kNpcPos, nullptr},    {"velocity", kNpcVel, nullptr},
                      {"type", kNpcType, nullptr},       {"ai", kNpcAi, nullptr},
                      {"target", kNpcTarget, nullptr},   {"damage", kNpcDamage, nullptr},
                      {"defense", kNpcDefense, nullptr}, {"life", kNpcLife, nullptr},
                      {"lifeMax", kNpcLifeMax, nullptr}};
FieldInfo g_fMainNpc{"npc", 0x5B0, &g_mainNpc};

struct SpawnParams {  // NPCSpawnParams: 32 bytes, por valor
    uint64_t a, b;
    void* c;
    uint64_t d;
};

// "Código do jogo" para SetDefaults: o jogo real indexa arrays por tipo, então
// um tipo fora da faixa original estoura. O mock aborta para o teste provar
// que o ID de mod nunca chega aqui.
void MockNpcSetDefaults(Il2CppObject* self, int32_t type, SpawnParams, const MethodInfo*) {
    if (type >= kNpcVanillaCount || type < -65) std::abort();
    At<int32_t>(self, kNpcType) = type;
    At<int32_t>(self, kNpcLife) = At<int32_t>(self, kNpcLifeMax) = 100 + type;
    At<int32_t>(self, kNpcDamage) = 10;
    At<int32_t>(self, kNpcDefense) = 5;
    auto* ai = At<FloatArray4*>(self, kNpcAi);
    for (float& f : ai->data) f = 0.0f;
}
using SetDefaultsFn = decltype(&MockNpcSetDefaults);
SetDefaultsFn g_setDefaultsEntry = &MockNpcSetDefaults;
MethodInfo g_mSetDefaults{reinterpret_cast<Il2CppMethodPointer>(&MockNpcSetDefaults), "SetDefaults", 2};

// AI original de exemplo: conta ticks em ai[0] e anda para a direita.
void MockNpcAI(Il2CppObject* self, const MethodInfo*) {
    At<FloatArray4*>(self, kNpcAi)->data[0] += 1.0f;
    At<float>(self, kNpcVel) = 1.0f;
}
using AiFn = decltype(&MockNpcAI);
AiFn g_aiEntry = &MockNpcAI;
MethodInfo g_mAI{reinterpret_cast<Il2CppMethodPointer>(&MockNpcAI), "AI", 0};

// NewNPC como o do jogo: escolhe slot, chama SetDefaults ANTES de preencher
// whoAmI (o objeto ainda tem whoAmI = 0), depois posição/active/whoAmI.
MethodInfo g_mNewNPC{nullptr, "NewNPC", 10};
int32_t MockNewNPC(Il2CppObject*, int32_t x, int32_t y, int32_t type, int32_t start, float ai0, float ai1,
                   float ai2, float ai3, int32_t target, const MethodInfo*) {
    for (int i = start; i < kMaxNpcs; ++i) {
        void* npc = g_npcs[i];
        if (At<bool>(npc, kNpcActive)) continue;
        At<int32_t>(npc, kNpcWhoAmI) = 0;  // objeto "novo"
        g_setDefaultsEntry(static_cast<Il2CppObject*>(npc), type, SpawnParams{}, &g_mSetDefaults);
        At<int32_t>(npc, kNpcWhoAmI) = i;
        At<bool>(npc, kNpcActive) = true;
        At<float>(npc, kNpcPos) = static_cast<float>(x);
        At<float>(npc, kNpcPos + 4) = static_cast<float>(y);
        At<int32_t>(npc, kNpcTarget) = target;
        float* ai = At<FloatArray4*>(npc, kNpcAi)->data;
        ai[0] = ai0, ai[1] = ai1, ai[2] = ai2, ai[3] = ai3;
        return i;
    }
    return kMaxNpcs;
}
using NewNpcFn = decltype(&MockNewNPC);
NewNpcFn g_newNpcEntry = &MockNewNPC;

// ---- Estado do mundo -------------------------------------------------------------
// Flags estáticas (bool, 1 byte) com os nomes do dump.
struct BoolStatic {
    const char* klass;
    FieldInfo field;
    bool value;
};
BoolStatic g_flags[] = {
    {"Main", {"hardMode", 0x360, nullptr, 1}, false},
    {"NPC", {"downedSlimeKing", 0x92, nullptr, 1}, false},
    {"NPC", {"downedBoss1", 0x8E, nullptr, 1}, false},
    {"NPC", {"downedBoss2", 0x8F, nullptr, 1}, false},
    {"NPC", {"downedQueenBee", 0x91, nullptr, 1}, false},
    {"NPC", {"downedBoss3", 0x90, nullptr, 1}, false},
    {"NPC", {"downedDeerclops", 0xA8, nullptr, 1}, false},
    {"NPC", {"downedQueenSlime", 0xA7, nullptr, 1}, false},
    {"NPC", {"downedMechBoss1", 0xCA, nullptr, 1}, false},
    {"NPC", {"downedMechBoss2", 0xCB, nullptr, 1}, false},
    {"NPC", {"downedMechBoss3", 0xCC, nullptr, 1}, false},
    {"NPC", {"downedPlantBoss", 0x97, nullptr, 1}, false},
    {"NPC", {"downedGolemBoss", 0x98, nullptr, 1}, false},
    {"NPC", {"downedFishron", 0x9A, nullptr, 1}, false},
    {"NPC", {"downedEmpressOfLight", 0xA6, nullptr, 1}, false},
    {"NPC", {"downedAncientCultist", 0xA0, nullptr, 1}, false},
    {"NPC", {"downedMoonlord", 0xA1, nullptr, 1}, false},
    {"NPC", {"downedGoblins", 0x93, nullptr, 1}, false},
    {"NPC", {"downedFrost", 0x94, nullptr, 1}, false},
    {"NPC", {"downedPirates", 0x95, nullptr, 1}, false},
    {"NPC", {"downedMartians", 0x99, nullptr, 1}, false},
    {"NPC", {"downedHalloweenTree", 0x9B, nullptr, 1}, false},
    {"NPC", {"downedHalloweenKing", 0x9C, nullptr, 1}, false},
    {"NPC", {"downedChristmasTree", 0x9E, nullptr, 1}, false},
    {"NPC", {"downedChristmasSantank", 0x9F, nullptr, 1}, false},
    {"NPC", {"downedChristmasIceQueen", 0x9D, nullptr, 1}, false},
    {"NPC", {"downedTowerSolar", 0xA2, nullptr, 1}, false},
    {"NPC", {"downedTowerVortex", 0xA3, nullptr, 1}, false},
    {"NPC", {"downedTowerNebula", 0xA4, nullptr, 1}, false},
    {"NPC", {"downedTowerStardust", 0xA5, nullptr, 1}, false},
};

// LocalUserGameState: Instance é ThreadStatic, como no jogo.
struct LocalUserGameState {
    Il2CppObject header;
    uint8_t pad[0x320 - sizeof(Il2CppObject)];
    bool gameMenu;  // @0x320
};
LocalUserGameState g_localUser{};
thread_local Il2CppObject* tl_localUserInstance = nullptr;
void* LocalUserInstanceAddr() { return &tl_localUserInstance; }
Il2CppClass g_localUserClass{"", "LocalUserGameState", {}, {}};
FieldInfo g_fLocalUserInstance{"Instance", 0, nullptr, sizeof(void*), &LocalUserInstanceAddr};
FieldInfo g_fGameMenu{"gameMenu", 0x320, nullptr};

// Main.DoUpdate: o "frame" do jogo.
// Telas que leem TextureAssets.Item sozinhas (fim do arquivo).
bool PatchScreens(void* target, void* replacement);
bool InvokeScreens(const MethodInfo* m, void* obj, void** params, Il2CppObject** result);
void InitScreens();
void InitTools();
Il2CppObject* NewMockItem();

int g_framesRun = 0;
void MockMainDoUpdate(Il2CppObject*, Il2CppObject*, const MethodInfo*) { ++g_framesRun; }
using DoUpdateFn = decltype(&MockMainDoUpdate);
DoUpdateFn g_doUpdateEntry = &MockMainDoUpdate;
MethodInfo g_mDoUpdate{reinterpret_cast<Il2CppMethodPointer>(&MockMainDoUpdate), "DoUpdate", 1};

// ---- Save/load ---------------------------------------------------------------------
// Offsets do dump (terraria_offsets.h: item_off, chest_off, loadout_off, file_data_off...).
constexpr size_t kItemFavorited = 0x69, kItemType = 0xBC, kItemStack = 0xCC, kItemNetId = 0x198,
                 kItemPrefix = 0x1A0, kItemSize = 0x1B0;
constexpr size_t kChestItem = 0x10, kChestX = 0x18, kChestY = 0x1C, kChestSize = 0x40;
constexpr size_t kLoadoutArmor = 0x10, kLoadoutDye = 0x18, kLoadoutSize = 0x28;
constexpr size_t kFilePath = 0x10, kFileCloud = 0x18, kFilePlayer = 0x40, kFileSize = 0x68;
constexpr size_t kPInventory = 0x4A0, kPArmor = 0x3F8, kPDye = 0x400, kPMiscEquips = 0x408, kPMiscDyes = 0x410,
                 kPBank = 0x4B8, kPLoadouts = 0xD60, kPTemporary = 0xD78, kPLoadStatus = 0x1C0;
constexpr int32_t kItemCount = 5456;  // ItemID.Count
constexpr int kMaxChests = 8000;

// "Heap" do teste: nada é liberado (o GC do jogo cuidaria disso).
Il2CppObject* NewObject(size_t size) { return static_cast<Il2CppObject*>(std::calloc(1, size)); }
Il2CppArray* NewArray(uint32_t n) {
    auto* a = static_cast<Il2CppArray*>(std::calloc(1, sizeof(Il2CppArray) + n * sizeof(void*)));
    a->max_length = n;
    return a;
}
void*& Elem(Il2CppArray* a, uint32_t i) { return reinterpret_cast<void**>(a + 1)[i]; }
Il2CppArray* NewItemArray(uint32_t n) {
    Il2CppArray* a = NewArray(n);
    for (uint32_t i = 0; i < n; ++i) Elem(a, i) = NewObject(kItemSize);
    return a;
}
Il2CppObject* NewChest(int32_t x, int32_t y) {
    Il2CppObject* c = NewObject(kChestSize);
    At<Il2CppArray*>(c, kChestItem) = NewItemArray(40);
    At<int32_t>(c, kChestX) = x;
    At<int32_t>(c, kChestY) = y;
    return c;
}
// System.String: klass, monitor, int32 length, UTF-16.
Il2CppObject* NewString(const char16_t* text) {
    size_t n = 0;
    while (text[n]) ++n;
    Il2CppObject* str = NewObject(0x14 + (n + 1) * sizeof(char16_t));
    At<int32_t>(str, 0x10) = static_cast<int32_t>(n);
    std::memcpy(reinterpret_cast<uint8_t*>(str) + 0x14, text, n * sizeof(char16_t));
    return str;
}

Il2CppClass g_item{"Terraria", "Item", {}, {}};
FieldInfo g_fItem[] = {{"type", kItemType, nullptr},     {"netID", kItemNetId, nullptr},
                       {"stack", kItemStack, nullptr},   {"prefix", kItemPrefix, nullptr},
                       {"favorited", kItemFavorited, nullptr}};
Il2CppClass g_chest{"Terraria", "Chest", {}, {}};
FieldInfo g_fChest[] = {{"item", kChestItem, nullptr}, {"x", kChestX, nullptr}, {"y", kChestY, nullptr}};
Il2CppClass g_loadout{"Terraria", "EquipmentLoadout", {}, {}};
FieldInfo g_fLoadout[] = {{"Armor", kLoadoutArmor, nullptr}, {"Dye", kLoadoutDye, nullptr}};
Il2CppClass g_fileData{"Terraria.IO", "FileData", {}, {}};
FieldInfo g_fFileData[] = {{"_path", kFilePath, nullptr}, {"_isCloudSave", kFileCloud, nullptr}};
Il2CppClass g_playerFileData{"Terraria.IO", "PlayerFileData", {}, {}};
FieldInfo g_fPlayerFilePlayer{"_player", kFilePlayer, nullptr};
Il2CppClass g_worldFile{"Terraria.IO", "WorldFile", {}, {}};
Il2CppClass g_worldGen{"Terraria", "WorldGen", {}, {}};
bool g_loadFailed = false;
FieldInfo g_fLoadFailed{"loadFailed", 0xC5, &g_loadFailed, 1};
Il2CppClass g_creativeUi{"Terraria.GameContent.Creative", "CreativeUI", {}, {}};
FieldInfo g_fPlayerItems[] = {{"inventory", kPInventory, nullptr},   {"armor", kPArmor, nullptr},
                              {"dye", kPDye, nullptr},               {"miscEquips", kPMiscEquips, nullptr},
                              {"miscDyes", kPMiscDyes, nullptr},     {"bank", kPBank, nullptr},
                              {"bank2", kPBank + 8, nullptr},        {"bank3", kPBank + 16, nullptr},
                              {"bank4", kPBank + 24, nullptr},       {"Loadouts", kPLoadouts, nullptr},
                              {"_temporaryItemSlots", kPTemporary, nullptr},
                              {"loadStatus", kPLoadStatus, nullptr}};

Il2CppArray* g_chests = nullptr;  // static Chest[] Main.chest
FieldInfo g_fMainChest{"chest", 0x5F8, &g_chests};
Il2CppObject* g_activeWorld = nullptr;  // static WorldFileData Main.ActiveWorldFileData
FieldInfo g_fActiveWorld{"ActiveWorldFileData", 0x730, &g_activeWorld};
Il2CppObject* g_creativeMenu = nullptr;  // static CreativeUI Main.CreativeMenu
FieldInfo g_fCreativeMenu{"CreativeMenu", 0x50, &g_creativeMenu};
Il2CppObject* g_playerFile = nullptr;  // PlayerFileData do personagem local
Il2CppObject* g_held[4] = {};          // mouse, sacrifício, Guia, Reforja

MethodInfo g_mGetMouseItem{nullptr, "get_mouseItem", 0};
MethodInfo g_mGetGuideItem{nullptr, "get_guideItem", 0};
MethodInfo g_mGetReforgeItem{nullptr, "get_reforgeItem", 0};
MethodInfo g_mCreativeGetItem{nullptr, "GetItemByIndex", 1};

// Exceção gerenciada do "jogo" (no IL2CPP real, Il2CppExceptionWrapper).
struct MockManagedException {
    Il2CppException* exc;
};
Il2CppObject g_saveException{};
bool g_throwOnSave = false;
std::vector<int32_t> g_savedIds;  // netIDs que o último save gravou, na ordem
int g_worldLoads = 0;

int32_t NetId(void* item) { return item ? At<int32_t>(item, kItemNetId) : 0; }
void RecordItems(Il2CppArray* items, uint32_t count) {
    for (uint32_t i = 0; i < count && i < items->max_length; ++i) g_savedIds.push_back(NetId(Elem(items, i)));
}

// Player.InternalSavePlayerFile: grava os netIDs na ordem do jogo real.
void MockInternalSavePlayerFile(Il2CppObject* playerFile, const MethodInfo*) {
    g_savedIds.clear();
    void* p = At<void*>(playerFile, kFilePlayer);
    RecordItems(At<Il2CppArray*>(p, kPArmor), 20);
    RecordItems(At<Il2CppArray*>(p, kPDye), 10);
    RecordItems(At<Il2CppArray*>(p, kPInventory), 58);
    RecordItems(At<Il2CppArray*>(p, kPMiscEquips), 5);
    RecordItems(At<Il2CppArray*>(p, kPMiscDyes), 5);
    for (int b = 0; b < 4; ++b) RecordItems(At<Il2CppArray*>(At<void*>(p, kPBank + 8 * b), kChestItem), 40);
    for (Il2CppObject* held : g_held) g_savedIds.push_back(NetId(held));  // SaveTemporaryItemSlotContents
    Il2CppArray* loadouts = At<Il2CppArray*>(p, kPLoadouts);
    for (uint32_t l = 0; l < loadouts->max_length; ++l) {
        RecordItems(At<Il2CppArray*>(Elem(loadouts, l), kLoadoutArmor), 20);
        RecordItems(At<Il2CppArray*>(Elem(loadouts, l), kLoadoutDye), 10);
    }
    if (g_throwOnSave) throw MockManagedException{&g_saveException};
}
using SavePlayerFn = decltype(&MockInternalSavePlayerFile);
SavePlayerFn g_savePlayerEntry = &MockInternalSavePlayerFile;
MethodInfo g_mInternalSavePlayerFile{reinterpret_cast<Il2CppMethodPointer>(&MockInternalSavePlayerFile),
                                     "InternalSavePlayerFile", 1};

// Player.LoadPlayer: "lê" o arquivo para o PlayerFileData do personagem local.
Il2CppObject* MockLoadPlayer(Il2CppObject* path, bool cloud, const MethodInfo*) {
    At<Il2CppObject*>(g_playerFile, kFilePath) = path;
    At<bool>(g_playerFile, kFileCloud) = cloud;
    return g_playerFile;
}
using LoadPlayerFn = decltype(&MockLoadPlayer);
LoadPlayerFn g_loadPlayerEntry = &MockLoadPlayer;
MethodInfo g_mLoadPlayer{reinterpret_cast<Il2CppMethodPointer>(&MockLoadPlayer), "LoadPlayer", 2};

// WorldFile.InternalSaveWorld -> SaveChests. Como no jogo, um type fora de
// ItemID.Sets.ItemsThatShouldNotBeInInventory (tamanho ItemID.Count) estoura.
void MockInternalSaveWorld(bool, bool, const MethodInfo*) {
    g_savedIds.clear();
    for (int c = 0; c < kMaxChests; ++c) {
        void* chest = Elem(g_chests, static_cast<uint32_t>(c));
        if (!chest) continue;
        Il2CppArray* items = At<Il2CppArray*>(chest, kChestItem);
        for (uint32_t i = 0; i < 40; ++i) {
            void* item = Elem(items, i);
            if (At<int32_t>(item, kItemType) >= kItemCount) throw MockManagedException{&g_saveException};
            g_savedIds.push_back(NetId(item));
        }
    }
    if (g_throwOnSave) throw MockManagedException{&g_saveException};
}
using SaveWorldFn = decltype(&MockInternalSaveWorld);
SaveWorldFn g_saveWorldEntry = &MockInternalSaveWorld;
MethodInfo g_mInternalSaveWorld{reinterpret_cast<Il2CppMethodPointer>(&MockInternalSaveWorld), "InternalSaveWorld", 2};

void MockLoadWorld(bool, const MethodInfo*) { ++g_worldLoads; }
using LoadWorldFn = decltype(&MockLoadWorld);
LoadWorldFn g_loadWorldEntry = &MockLoadWorld;
MethodInfo g_mLoadWorld{reinterpret_cast<Il2CppMethodPointer>(&MockLoadWorld), "LoadWorld", 1};

// ---- Itens: SetDefaults / RebuildTooltip / ItemTooltip -----------------------------
// Offsets do dump (terraria_offsets.h: item_off, item_tooltip_off).
constexpr size_t kItemWidth = 0x48, kItemHeight = 0x4C, kItemNameOverride = 0x58, kItemMelee = 0x7D,
                 kItemUseStyle = 0xC1, kItemUseAnimation = 0xC4, kItemUseTime = 0xC8, kItemMaxStack = 0xD0,
                 kItemDamage = 0xF0, kItemToolTip = 0x138, kItemRare = 0x14C, kItemValue = 0x178;
constexpr size_t kTooltipValidator = 0x18, kTooltipText = 0x20, kTextKey = 0x10, kTextValue = 0x18;
FieldInfo g_fItemStats[] = {{"width", kItemWidth, nullptr},          {"height", kItemHeight, nullptr},
                            {"_nameOverride", kItemNameOverride, nullptr}, {"melee", kItemMelee, nullptr},
                            {"useStyle", kItemUseStyle, nullptr},    {"useAnimation", kItemUseAnimation, nullptr},
                            {"useTime", kItemUseTime, nullptr},      {"maxStack", kItemMaxStack, nullptr},
                            {"damage", kItemDamage, nullptr},        {"ToolTip", kItemToolTip, nullptr},
                            {"rare", kItemRare, nullptr},            {"value", kItemValue, nullptr}};

Il2CppClass g_itemTooltip{"Terraria.UI", "ItemTooltip", {}, {}, 0x38};
Il2CppClass g_localizedText{"Terraria.Localization", "LocalizedText", {}, {}, 0x20};
Il2CppObject* g_tooltipNone = nullptr;      // static ItemTooltip.None
uint64_t g_globalValidatorKey = 7;          // static ItemTooltip._globalValidatorKey
FieldInfo g_fTooltip[] = {{"_validatorKey", kTooltipValidator, nullptr}, {"_text", kTooltipText, nullptr}};
FieldInfo g_fTooltipNone{"None", 0x0, &g_tooltipNone};
FieldInfo g_fTooltipKey{"_globalValidatorKey", 0x10, &g_globalValidatorKey, sizeof(uint64_t)};
MethodInfo g_mTextCtor{nullptr, ".ctor", 2};
Il2CppObject* g_vanillaTooltip = nullptr;   // o que Lang.GetTooltip devolve no mock
int g_barriers = 0;                          // il2cpp_gc_wbarrier_set_field chamadas
std::vector<Il2CppObject*> g_handles;        // GC handles

// "Tabelas do jogo" indexadas pelo tipo do item, com o tamanho original
// (ItemID.Count). O jogo real lança IndexOutOfRangeException; o mock aborta,
// para o teste provar que um ID de mod nunca chega a elas.
std::vector<int32_t> g_itemMaxStackTable(kItemCount, 9999);
int32_t& TableAt(std::vector<int32_t>& table, int32_t index) {
    if (index < 0 || static_cast<size_t>(index) >= table.size()) std::abort();
    return table[static_cast<size_t>(index)];
}

void MockItemRebuildTooltip(Il2CppObject* self, const MethodInfo*);
using RebuildFn = decltype(&MockItemRebuildTooltip);
RebuildFn g_rebuildEntry = &MockItemRebuildTooltip;
MethodInfo g_mRebuildTooltip{reinterpret_cast<Il2CppMethodPointer>(&MockItemRebuildTooltip), "RebuildTooltip", 0};

// Lang.GetTooltip(netID): _itemTooltipCache[netID].
void MockItemRebuildTooltip(Il2CppObject* self, const MethodInfo*) {
    TableAt(g_itemMaxStackTable, At<int32_t>(self, kItemNetId));
    At<Il2CppObject*>(self, kItemToolTip) = g_vanillaTooltip;
}

// ---- Propriedades de alguns itens originais (ItemID.Sets + campos do SetDefaults) ----
// Offsets do dump (terraria_offsets.h: item_off, item_id_sets_static_off).
constexpr size_t kItemAccessory = 0x6B, kItemConsumable = 0x6D, kItemRanged = 0x7F, kItemCreateTile = 0xE4,
                 kItemCreateWall = 0xE8, kItemDefense = 0x118, kItemHeadSlot = 0x11C, kItemBodySlot = 0x120,
                 kItemLegSlot = 0x124, kItemShoot = 0x158, kItemAmmo = 0x160, kItemUseAmmo = 0x164,
                 kItemMountType = 0x184;
const char* const kTraitFieldNames[] = {"accessory", "potion",   "consumable", "channel",    "vanity",    "magic",
                                        "ranged",    "summon",   "sentry",     "fishingPole", "bait",     "makeNPC",
                                        "pick",      "axe",      "hammer",     "createTile", "createWall", "healLife",
                                        "healMana",  "defense",  "headSlot",   "bodySlot",   "legSlot",   "shoot",
                                        "ammo",      "useAmmo",  "mana",       "buffType",   "mountType"};
const size_t kTraitFieldOffsets[] = {0x6B, 0x6C, 0x6D, 0x6A, 0x77, 0x7E, 0x7F, 0x80, 0x81, 0x9C,
                                     0xA0, 0xA4, 0xD4, 0xD8, 0xDC, 0xE4, 0xE8, 0xF8, 0xFC, 0x118,
                                     0x11C, 0x120, 0x124, 0x158, 0x160, 0x164, 0x170, 0x17C, 0x184};
FieldInfo g_fItemTraits[sizeof(kTraitFieldOffsets) / sizeof(size_t)];
MethodInfo g_mItemCtor{nullptr, ".ctor", 0};

// ItemID.Sets: alguns arrays por type, preenchidos para os itens dos testes.
Il2CppClass g_itemId{"Terraria.ID", "ItemID", {}, {}};
Il2CppClass g_itemIdSets{"", "Sets", {}, {}};
struct SetArray {
    const char* name;
    size_t offset;
    Il2CppArray* array;
    FieldInfo field;
};
SetArray g_sets[] = {{"ItemsThatShouldNotBeInInventory", 0x28, nullptr, {}}, {"ShimmerTransformToItem", 0x50, nullptr, {}},
                     {"CanGetPrefixes", 0xA8, nullptr, {}},  {"IsAMaterial", 0xC8, nullptr, {}},
                     {"ToolTipDamageMultiplier", 0xF0, nullptr, {}}, {"IsAPickup", 0xF8, nullptr, {}},
                     {"IsFishingCrate", 0x180, nullptr, {}}, {"CommonCoin", 0x1B8, nullptr, {}},
                     {"ExtractinatorMode", 0x1D0, nullptr, {}}, {"BossBag", 0x2A0, nullptr, {}}};
template <typename T>
T& SetAt(const char* name, int32_t type) {
    for (SetArray& s : g_sets)
        if (!std::strcmp(s.name, name)) return reinterpret_cast<T*>(s.array + 1)[type];
    std::abort();
}
void InitItemSets() {
    for (SetArray& s : g_sets) {
        s.array = NewArray(kItemCount);  // 8 bytes por elemento: cabe bool, int e float
        s.field = FieldInfo{s.name, s.offset, &s.array};
        g_itemIdSets.fields.push_back(&s.field);
    }
    for (int32_t i = 0; i < kItemCount; ++i) {
        SetAt<bool>("CanGetPrefixes", i) = true;
        SetAt<int32_t>("ShimmerTransformToItem", i) = -1;
        SetAt<int32_t>("ExtractinatorMode", i) = -1;
        SetAt<float>("ToolTipDamageMultiplier", i) = 1.0f;
    }
    SetAt<bool>("IsAMaterial", 38) = true;        // Lente
    SetAt<bool>("CanGetPrefixes", 40) = false;    // Flecha de Madeira
    SetAt<bool>("IsAPickup", 58) = true;          // Coração
    SetAt<bool>("CommonCoin", 71) = true;         // Moeda de Cobre
    SetAt<bool>("IsFishingCrate", 2334) = true;   // Caixa de Madeira
    SetAt<bool>("BossBag", 3319) = true;          // Bolsa do Olho de Cthulhu
    SetAt<int32_t>("ShimmerTransformToItem", 3507) = 3508;
    SetAt<int32_t>("ExtractinatorMode", 3347) = 1;  // Fóssil Sólido... qualquer um
    g_itemId.nested = {&g_itemIdSets};
    g_csharp.classes.push_back(&g_itemId);
}
// O que o SetDefaults do jogo põe nesses campos para os mesmos itens.
void ApplyMockTraits(Il2CppObject* self, int32_t type) {
    for (size_t off : kTraitFieldOffsets) {
        if (off == kItemCreateTile || off == kItemCreateWall || off == kItemHeadSlot || off == kItemBodySlot ||
            off == kItemLegSlot || off == kItemMountType)
            At<int32_t>(self, off) = -1;
        else if (off < 0x90)
            At<bool>(self, off) = false;
        else if (off == 0xA4)
            At<int16_t>(self, off) = 0;
        else
            At<int32_t>(self, off) = 0;
    }
    switch (type) {
        case 8: At<int32_t>(self, kItemCreateTile) = 4, At<bool>(self, kItemConsumable) = true; break;  // Tocha
        case 24: At<int32_t>(self, kItemDamage) = 7, At<uint8_t>(self, kItemUseStyle) = 1, At<bool>(self, kItemMelee) = true; break;
        case 39:  // Arco de Madeira: consome flechas e dispara
            At<int32_t>(self, kItemDamage) = 4, At<uint8_t>(self, kItemUseStyle) = 5, At<int32_t>(self, kItemUseAmmo) = 40;
            At<int32_t>(self, kItemShoot) = 1, At<bool>(self, kItemRanged) = true;
            break;
        case 40:  // Flecha de Madeira: é munição
            At<int32_t>(self, kItemDamage) = 5, At<int32_t>(self, kItemAmmo) = 40, At<int32_t>(self, kItemShoot) = 1;
            At<bool>(self, kItemConsumable) = true, At<bool>(self, kItemRanged) = true;
            break;
        case 216: At<bool>(self, kItemAccessory) = true, At<int32_t>(self, kItemDefense) = 1; break;  // Algema
        case 729: At<int32_t>(self, kItemCreateWall) = 4, At<bool>(self, kItemConsumable) = true; break;
        default: break;
    }
}

// Item.SetDefaults(int, bool, ItemVariant): zera, indexa tabelas por type e
// termina em RebuildTooltip, como o do jogo.
void MockItemSetDefaults(Il2CppObject* self, int32_t type, bool, Il2CppObject*, const MethodInfo*) {
    At<int32_t>(self, kItemType) = At<int32_t>(self, kItemNetId) = type;
    At<int32_t>(self, kItemStack) = type ? 1 : 0;
    At<int32_t>(self, kItemMaxStack) = type ? TableAt(g_itemMaxStackTable, type) : 0;
    At<int32_t>(self, kItemDamage) = At<int32_t>(self, kItemUseTime) = At<int32_t>(self, kItemUseAnimation) = 0;
    At<uint8_t>(self, kItemUseStyle) = 0;
    At<bool>(self, kItemMelee) = false;
    At<int32_t>(self, kItemValue) = At<int32_t>(self, kItemRare) = 0;
    At<int32_t>(self, kItemWidth) = 12;
    At<int32_t>(self, kItemHeight) = 20;
    At<uint8_t>(self, kItemPrefix) = 0;
    At<Il2CppObject*>(self, kItemNameOverride) = nullptr;
    ApplyMockTraits(self, type);
    g_rebuildEntry(self, &g_mRebuildTooltip);
}
using ItemSetDefaultsFn = decltype(&MockItemSetDefaults);
ItemSetDefaultsFn g_itemSetDefaultsEntry = &MockItemSetDefaults;
MethodInfo g_mItemSetDefaults3{reinterpret_cast<Il2CppMethodPointer>(&MockItemSetDefaults), "SetDefaults", 3};
MethodInfo g_mItemSetDefaults1{nullptr, "SetDefaults", 1};  // no jogo: `b` para o de 3
MethodInfo g_mItemPrefix{nullptr, "Prefix", 1};

void InitItems() {
    for (FieldInfo& f : g_fItemStats) g_item.fields.push_back(&f);
    g_item.methods = {&g_mItemSetDefaults3, &g_mItemSetDefaults1, &g_mRebuildTooltip, &g_mItemPrefix, &g_mItemCtor};
    for (size_t i = 0; i < sizeof(kTraitFieldOffsets) / sizeof(size_t); ++i) {
        g_fItemTraits[i] = FieldInfo{kTraitFieldNames[i], kTraitFieldOffsets[i], nullptr};
        g_item.fields.push_back(&g_fItemTraits[i]);
    }
    InitItemSets();
    g_item.size = kItemSize;
    g_itemTooltip.fields = {&g_fTooltip[0], &g_fTooltip[1], &g_fTooltipNone, &g_fTooltipKey};
    g_localizedText.methods = {&g_mTextCtor};
    g_csharp.classes.push_back(&g_itemTooltip);
    g_csharp.classes.push_back(&g_localizedText);
    g_tooltipNone = NewObject(0x38);
    g_vanillaTooltip = NewObject(0x38);
}

// ---- Texturas e desenho ------------------------------------------------------------
// Imagem UnityEngine.CoreModule.dll com UnityEngine.Texture/Texture2D; o
// invólucro XNA, Asset<Texture2D> e TextureAssets do Assembly-CSharp.
Il2CppImage g_core{"UnityEngine.CoreModule.dll", {}};
Il2CppAssembly g_asmCore{&g_core};
const Il2CppAssembly* g_assemblies3[] = {&g_asmCorlib, &g_asmCsharp, &g_asmCore};

// UnityEngine.Texture2D do mock: tamanho, pixels (linha de baixo primeiro) e estado.
struct UnityTex {
    Il2CppObject header;
    int32_t width, height, format;
    bool mip;
    int32_t filter = 1, wrap = 0;  // padrão do Unity: Bilinear, Repeat
    bool applied = false;
    uint8_t* pixels;
};
Il2CppClass g_unityTexture{"UnityEngine", "Texture", {}, {}};
Il2CppClass g_unityTexture2D{"UnityEngine", "Texture2D", {}, {}, sizeof(UnityTex)};
MethodInfo g_mUnityCtor{nullptr, ".ctor", 4};
MethodInfo g_mRawSize{nullptr, "GetRawImageDataSize", 0};
MethodInfo g_mWritable{nullptr, "GetWritableImageData", 1};
MethodInfo g_mApply{nullptr, "Apply", 2};
MethodInfo g_mSetFilter{nullptr, "set_filterMode", 1};
MethodInfo g_mSetWrap{nullptr, "set_wrapMode", 1};

// Microsoft.Xna.Framework.Graphics.Texture2D: Width 0x44, Height 0x48, _unityTexture 0x78.
constexpr size_t kXnaWidth = 0x44, kXnaHeight = 0x48, kXnaUnity = 0x78, kXnaSize = 0xA8;
Il2CppClass g_xnaTexture2D{"Microsoft.Xna.Framework.Graphics", "Texture2D", {}, {}, kXnaSize};
MethodInfo g_mXnaCtorRender{nullptr, ".ctor", 1, "UnityEngine.RenderTexture"};
MethodInfo g_mXnaCtorUnity{nullptr, ".ctor", 1, "UnityEngine.Texture2D"};

// Asset<Texture2D>: Name 0x10, State 0x18, Value 0x28.
constexpr size_t kAssetName = 0x10, kAssetState = 0x18, kAssetValue = 0x28, kAssetSize = 0x30;
Il2CppClass g_assetClass{"ReLogic.Content", "Asset`1", {}, {}, kAssetSize};
FieldInfo g_fAsset[] = {{"<Name>k__BackingField", kAssetName, nullptr},
                        {"<State>k__BackingField", kAssetState, nullptr},
                        {"<Value>k__BackingField", kAssetValue, nullptr}};

// TextureAssets.Item / .Npc: um Asset carregado por tipo, cada um com um XNA próprio.
constexpr uint32_t kNpcTypes = 688;
Il2CppArray* g_itemAssets = nullptr;
Il2CppArray* g_npcAssets = nullptr;
Il2CppArray* g_npcFrameCount = nullptr;  // int[] Main.npcFrameCount
Il2CppClass g_textureAssets{"Terraria.GameContent", "TextureAssets", {}, {}};
FieldInfo g_fItemAssets{"Item", 0x560, &g_itemAssets};
FieldInfo g_fNpcAssets{"Npc", 0x570, &g_npcAssets};
FieldInfo g_fNpcFrameCount{"npcFrameCount", 0x1138, &g_npcFrameCount};
FieldInfo g_fNpcFrame{"frame", 0x1C0, nullptr};

// PlayerDrawSet (struct): heldItem em 0x218 sem cabeçalho; o runtime devolve +0x10.
Il2CppClass g_playerDrawSet{"Terraria.DataStructures", "PlayerDrawSet", {}, {}};
FieldInfo g_fHeldItem{"heldItem", 0x218 + 0x10, nullptr};

Il2CppObject* NewAsset() {
    Il2CppObject* a = NewObject(kAssetSize);
    a->klass = &g_assetClass;
    At<int32_t>(a, kAssetState) = 2;
    Il2CppObject* xna = NewObject(kXnaSize);
    xna->klass = &g_xnaTexture2D;
    At<Il2CppObject*>(a, kAssetValue) = xna;
    return a;
}

// O que o "jogo" desenhou por último: o XNA Texture2D lido de TextureAssets e,
// para NPC, npcFrameCount[type] e NPC.frame no momento do desenho.
struct XnaVector2 { float x, y; };
struct XnaColor { uint32_t packed; };
struct XnaRect { int32_t x, y, w, h; };
Il2CppObject* g_lastDrawn = nullptr;
bool g_lastAnimated = false;              // Main.itemAnimations[type] existia no desenho
Il2CppArray* g_itemAnimations = nullptr;  // DrawAnimation[] Main.itemAnimations
FieldInfo g_fItemAnimations{"itemAnimations", 0x10E8, &g_itemAnimations};
int32_t g_lastFrameCount = 0;
XnaRect g_lastFrame{};
std::vector<Il2CppObject*> g_drawLog;

Il2CppObject* ReadItemTexture(void* item) {  // TextureAssets.Item[item.type].Value (ldr direto)
    int32_t type = At<int32_t>(item, kItemType);
    if (type < 0 || static_cast<uint32_t>(type) >= g_itemAssets->max_length) std::abort();
    return At<Il2CppObject*>(Elem(g_itemAssets, static_cast<uint32_t>(type)), kAssetValue);
}
void DrewItem(void* item) {
    g_lastDrawn = ReadItemTexture(item);
    g_lastAnimated = Elem(g_itemAnimations, static_cast<uint32_t>(At<int32_t>(item, kItemType))) != nullptr;
    g_drawLog.push_back(g_lastDrawn);
}

void MockGridDrawItem(Il2CppObject*, Il2CppObject*, Il2CppObject*, Il2CppArray* inv, int32_t slot, XnaVector2,
                      XnaColor, float, const MethodInfo*) {
    DrewItem(Elem(inv, static_cast<uint32_t>(slot)));
}
using GridFn = decltype(&MockGridDrawItem);
GridFn g_inventoryEntry = &MockGridDrawItem;
void MockHotbarDrawItem(Il2CppObject* a, Il2CppObject* b, Il2CppObject* c, Il2CppArray* inv, int32_t slot, XnaVector2 p,
                        XnaColor col, float s, const MethodInfo* m) {
    MockGridDrawItem(a, b, c, inv, slot, p, col, s, m);
}
GridFn g_hotbarEntry = &MockHotbarDrawItem;
void MockMainDrawItem(Il2CppObject*, Il2CppObject* item, int32_t, const MethodInfo*) { DrewItem(item); }
using MainDrawItemFn = decltype(&MockMainDrawItem);
MainDrawItemFn g_mainDrawItemEntry = &MockMainDrawItem;
float MockDrawItemIcon(Il2CppObject* item, int32_t, Il2CppObject*, XnaVector2, float scale, float, XnaColor,
                       const MethodInfo*) {
    DrewItem(item);
    return scale;
}
using IconFn = decltype(&MockDrawItemIcon);
IconFn g_iconEntry = &MockDrawItemIcon;
void MockHeldItem(void* drawinfo, const MethodInfo*) { DrewItem(At<void*>(drawinfo, 0x218)); }
using HeldFn = decltype(&MockHeldItem);
HeldFn g_heldEntry = &MockHeldItem;
void MockDrawNpcDirect(Il2CppObject*, Il2CppObject*, Il2CppObject* npc, bool, XnaVector2, Il2CppObject*, XnaRect*,
                       const MethodInfo*) {
    int32_t type = At<int32_t>(npc, kNpcType);
    if (type < 0 || static_cast<uint32_t>(type) >= kNpcTypes) std::abort();
    g_lastDrawn = At<Il2CppObject*>(Elem(g_npcAssets, static_cast<uint32_t>(type)), kAssetValue);
    g_lastFrameCount = reinterpret_cast<int32_t*>(g_npcFrameCount + 1)[type];
    g_lastFrame = At<XnaRect>(npc, 0x1C0);
    g_drawLog.push_back(g_lastDrawn);
}
using NpcDrawFn = decltype(&MockDrawNpcDirect);
NpcDrawFn g_npcDrawEntry = &MockDrawNpcDirect;

Il2CppClass g_guiInventory{"", "GUIInventory", {}, {}};
Il2CppClass g_guiHotbar{"", "GUIHotbar", {}, {}};
Il2CppClass g_itemSlot{"Terraria.UI", "ItemSlot", {}, {}};
Il2CppClass g_playerDrawLayers{"Terraria.DataStructures", "PlayerDrawLayers", {}, {}};
MethodInfo g_mInvDraw{reinterpret_cast<Il2CppMethodPointer>(&MockGridDrawItem), "DrawItem", 7};
MethodInfo g_mHotbarDraw{reinterpret_cast<Il2CppMethodPointer>(&MockHotbarDrawItem), "DrawHotbarItem", 7};
MethodInfo g_mMainDrawItem{reinterpret_cast<Il2CppMethodPointer>(&MockMainDrawItem), "DrawItem", 2};
MethodInfo g_mIconDraw{reinterpret_cast<Il2CppMethodPointer>(&MockDrawItemIcon), "DrawItemIcon", 7};
MethodInfo g_mHeldDraw{reinterpret_cast<Il2CppMethodPointer>(&MockHeldItem), "DrawPlayer_27_HeldItem", 1};
MethodInfo g_mNpcDraw{reinterpret_cast<Il2CppMethodPointer>(&MockDrawNpcDirect), "DrawNPCDirect", 6};

struct BoxedU64 {
    Il2CppObject header;
    uint64_t value;
};
BoxedU64 g_box64{};

bool g_assetsLoaded = false;  // o jogo ainda não preencheu TextureAssets

void InitDraw() {
    g_core.classes = {&g_unityTexture, &g_unityTexture2D};
    g_unityTexture.methods = {&g_mSetFilter, &g_mSetWrap};
    g_unityTexture2D.methods = {&g_mUnityCtor, &g_mRawSize, &g_mWritable, &g_mApply};
    g_xnaTexture2D.methods = {&g_mXnaCtorRender, &g_mXnaCtorUnity};
    for (FieldInfo& f : g_fAsset) g_assetClass.fields.push_back(&f);
    g_textureAssets.fields = {&g_fItemAssets, &g_fNpcAssets};
    g_main.fields.push_back(&g_fNpcFrameCount);
    g_main.fields.push_back(&g_fItemAnimations);
    g_itemAnimations = NewArray(static_cast<uint32_t>(kItemCount));
    g_npc.fields.push_back(&g_fNpcFrame);
    g_playerDrawSet.fields = {&g_fHeldItem};
    g_guiInventory.methods = {&g_mInvDraw};
    g_guiHotbar.methods = {&g_mHotbarDraw};
    g_main.methods.push_back(&g_mMainDrawItem);
    g_main.methods.push_back(&g_mNpcDraw);
    g_itemSlot.methods = {&g_mIconDraw};
    g_playerDrawLayers.methods = {&g_mHeldDraw};
    g_csharp.classes.insert(g_csharp.classes.end(), {&g_xnaTexture2D, &g_assetClass, &g_textureAssets, &g_playerDrawSet,
                                                     &g_guiInventory, &g_guiHotbar, &g_itemSlot, &g_playerDrawLayers});
    g_itemAssets = NewArray(static_cast<uint32_t>(kItemCount));
    g_npcAssets = NewArray(kNpcTypes);
    g_npcFrameCount = NewArray(kNpcTypes);  // int[] cabe no array de ponteiros
    for (uint32_t i = 0; i < kNpcTypes; ++i) reinterpret_cast<int32_t*>(g_npcFrameCount + 1)[i] = 6;
}
void FillTextureAssets() {
    for (uint32_t i = 0; i < static_cast<uint32_t>(kItemCount); ++i) Elem(g_itemAssets, i) = NewAsset();
    for (uint32_t i = 0; i < kNpcTypes; ++i) Elem(g_npcAssets, i) = NewAsset();
    g_assetsLoaded = true;
}

void InitSaveWorld() {
    g_item.fields.clear();
    for (FieldInfo& f : g_fItem) g_item.fields.push_back(&f);
    g_chest.fields.clear();
    for (FieldInfo& f : g_fChest) g_chest.fields.push_back(&f);
    g_loadout.fields = {&g_fLoadout[0], &g_fLoadout[1]};
    g_fileData.fields = {&g_fFileData[0], &g_fFileData[1]};
    g_playerFileData.fields = {&g_fPlayerFilePlayer};
    g_worldGen.fields = {&g_fLoadFailed};
    g_worldFile.methods = {&g_mInternalSaveWorld, &g_mLoadWorld};
    g_creativeUi.methods = {&g_mCreativeGetItem};
    for (FieldInfo& f : g_fPlayerItems) g_player.fields.push_back(&f);
    g_player.methods.push_back(&g_mInternalSavePlayerFile);
    g_player.methods.push_back(&g_mLoadPlayer);
    g_main.fields.push_back(&g_fMainChest);
    g_main.fields.push_back(&g_fActiveWorld);
    g_main.fields.push_back(&g_fCreativeMenu);
    g_main.methods.push_back(&g_mGetMouseItem);
    g_main.methods.push_back(&g_mGetGuideItem);
    g_main.methods.push_back(&g_mGetReforgeItem);
    g_csharp.classes.insert(g_csharp.classes.end(), {&g_item, &g_chest, &g_loadout, &g_fileData, &g_playerFileData,
                                                     &g_worldFile, &g_worldGen, &g_creativeUi});

    // Personagem: arrays com o tamanho do jogo; bancos e loadouts como objetos.
    void* p = g_localPlayer;
    At<Il2CppArray*>(p, kPInventory) = NewItemArray(59);
    At<Il2CppArray*>(p, kPArmor) = NewItemArray(20);
    At<Il2CppArray*>(p, kPDye) = NewItemArray(10);
    At<Il2CppArray*>(p, kPMiscEquips) = NewItemArray(5);
    At<Il2CppArray*>(p, kPMiscDyes) = NewItemArray(5);
    for (int b = 0; b < 4; ++b) At<Il2CppObject*>(p, kPBank + 8 * b) = NewChest(-1, -1);
    Il2CppArray* loadouts = NewArray(3);
    for (uint32_t l = 0; l < 3; ++l) {
        Il2CppObject* lo = NewObject(kLoadoutSize);
        At<Il2CppArray*>(lo, kLoadoutArmor) = NewItemArray(20);
        At<Il2CppArray*>(lo, kLoadoutDye) = NewItemArray(10);
        Elem(loadouts, l) = lo;
    }
    At<Il2CppArray*>(p, kPLoadouts) = loadouts;
    At<Il2CppArray*>(p, kPTemporary) = NewItemArray(4);
    for (Il2CppObject*& h : g_held) h = NewObject(kItemSize);
    g_creativeMenu = NewObject(0x40);

    g_playerFile = NewObject(kFileSize);
    At<void*>(g_playerFile, kFilePlayer) = p;
    At<Il2CppObject*>(g_playerFile, kFilePath) = NewString(u"/saves/Players/Jogador.plr");
    g_activeWorld = NewObject(kFileSize);
    At<Il2CppObject*>(g_activeWorld, kFilePath) = NewString(u"/saves/Worlds/Mundo.wld");
    g_chests = NewArray(kMaxChests);
}

// ---- Ferramentas de teste: nomes, hora, vida/mana, matar inimigos -----------------
Il2CppClass g_lang{"Terraria", "Lang", {}, {}};
MethodInfo g_mItemNameValue{nullptr, "GetItemNameValue", 1};
MethodInfo g_mNpcNameValue{nullptr, "GetNPCNameValue", 1};
Il2CppClass g_idDictionary{"ReLogic.Reflection", "IdDictionary", {}, {}};
MethodInfo g_mIdGetName{nullptr, "GetName", 1};
Il2CppClass g_npcIdClass{"Terraria.ID", "NPCID", {}, {}};
Il2CppObject* g_itemSearch = nullptr;
Il2CppObject* g_npcSearch = nullptr;
FieldInfo g_fItemSearch{"Search", 0x8, &g_itemSearch};
FieldInfo g_fNpcSearch{"Search", 0x10, &g_npcSearch};
bool g_dayTime = true;
double g_time = 1000.0;
FieldInfo g_fDayTime{"dayTime", 0x4B8, &g_dayTime, sizeof(bool)};
FieldInfo g_fTime{"time", 0x4C0, &g_time, sizeof(double)};
MethodInfo g_mSkipToTime{nullptr, "SkipToTime", 2};
int g_skipToTimeCalls = 0;
FieldInfo g_fPlayerMana[] = {{"statLifeMax2", 0x700, nullptr}, {"statMana", 0x708, nullptr},
                             {"statManaMax2", 0x710, nullptr}};
constexpr size_t kNpcWidth = 0x48, kNpcHeight = 0x4C, kNpcDontTakeDamage = 0x208, kNpcTownNpc = 0x21C,
                 kNpcFriendly = 0x238;
FieldInfo g_fNpcTools[] = {{"width", kNpcWidth, nullptr},     {"height", kNpcHeight, nullptr},
                           {"friendly", kNpcFriendly, nullptr}, {"townNPC", kNpcTownNpc, nullptr},
                           {"dontTakeDamage", kNpcDontTakeDamage, nullptr}};
MethodInfo g_mStrikeNoInteraction{nullptr, "StrikeNPCNoInteraction", 6};
Il2CppObject g_keyNotFound{};
// Item.NewItem: Main.item[i] montado pelo SetDefaults (com hook) sobre o retângulo.
// Como o jogo, indexa tabelas por type: um ID fora dos originais lança.
MethodInfo g_mNewItem{nullptr, "NewItem", 11};
Il2CppArray* g_worldItems = nullptr;  // Main.item
FieldInfo g_fMainItem{"item", 0x5A0, &g_worldItems};
int32_t g_worldItemCount = 0;
Il2CppObject g_indexOutOfRange{};

// Nomes como o jogo em português daria (alguns fixos, o resto genérico).
std::string MockItemName(int32_t id) {
    if (id <= 0 || id >= kItemCount) return "";
    if (id == 38) return "Lente";
    if (id == 4956) return "Zênite";
    return "Item " + std::to_string(id);
}
std::string MockItemInternal(int32_t id) {
    if (id == 38) return "Lens";
    if (id == 4956) return "Zenith";
    return id > 0 && id < kItemCount ? "Item" + std::to_string(id) : "";
}
std::string MockNpcName(int32_t id) {
    if (id <= 0 || id >= kNpcVanillaCount) return "";
    return id == 4 ? "Olho de Cthulhu" : "NPC " + std::to_string(id);
}

void InitTools() {
    g_lang.methods = {&g_mItemNameValue, &g_mNpcNameValue};
    g_idDictionary.methods = {&g_mIdGetName};
    g_itemSearch = NewObject(0x40);
    g_itemSearch->klass = &g_idDictionary;
    g_npcSearch = NewObject(0x40);
    g_npcSearch->klass = &g_idDictionary;
    g_itemId.fields.push_back(&g_fItemSearch);
    g_npcIdClass.fields = {&g_fNpcSearch};
    g_main.fields.insert(g_main.fields.end(), {&g_fDayTime, &g_fTime});
    g_main.methods.push_back(&g_mSkipToTime);
    for (FieldInfo& f : g_fPlayerMana) g_player.fields.push_back(&f);
    for (FieldInfo& f : g_fNpcTools) g_npc.fields.push_back(&f);
    g_npc.methods.push_back(&g_mStrikeNoInteraction);
    g_item.methods.push_back(&g_mNewItem);
    g_worldItems = NewArray(401);
    for (uint32_t i = 0; i < 401; ++i) Elem(g_worldItems, i) = NewMockItem();
    g_main.fields.push_back(&g_fMainItem);
    g_csharp.classes.insert(g_csharp.classes.end(), {&g_lang, &g_idDictionary, &g_npcIdClass});
    I32(g_localPlayer, 0x708) = 20;   // statMana
    I32(g_localPlayer, 0x710) = 200;  // statManaMax2
}

// Devolve true se `m` é uma das ferramentas (e preenche o retorno).
bool InvokeTools(const MethodInfo* m, void* obj, void** params, Il2CppObject** ret, Il2CppException** exc) {
    if (m == &g_mItemNameValue || m == &g_mNpcNameValue) {
        int32_t id = *static_cast<int32_t*>(params[0]);
        std::string s = m == &g_mItemNameValue ? MockItemName(id) : MockNpcName(id);
        *ret = il2cpp_string_new(s.c_str());  // o jogo devolve "" para tipo sem nome
        return true;
    }
    if (m == &g_mIdGetName) {
        int32_t id = *static_cast<int32_t*>(params[0]);
        std::string s = obj == g_itemSearch ? MockItemInternal(id) : (id == 4 ? "EyeofCthulhu" : "");
        if (s.empty()) *exc = &g_keyNotFound;  // Dictionary: KeyNotFoundException
        else *ret = il2cpp_string_new(s.c_str());
        return true;
    }
    if (m == &g_mSkipToTime) {
        ++g_skipToTimeCalls;
        g_time = *static_cast<int32_t*>(params[0]);
        g_dayTime = *static_cast<bool*>(params[1]);
        return true;
    }
    if (m == &g_mNewItem) {
        const int32_t x = *static_cast<int32_t*>(params[1]), y = *static_cast<int32_t*>(params[2]);
        const int32_t type = *static_cast<int32_t*>(params[5]);
        if (type <= 0 || type >= kItemCount) {
            *exc = &g_indexOutOfRange;
            return true;
        }
        Il2CppObject* item = static_cast<Il2CppObject*>(Elem(g_worldItems, static_cast<uint32_t>(g_worldItemCount)));
        g_itemSetDefaultsEntry(item, type, false, nullptr, &g_mItemSetDefaults3);
        At<int32_t>(item, kItemStack) = *static_cast<int32_t*>(params[6]);
        At<float>(item, 0x20) = static_cast<float>(x);  // position (Entity)
        At<float>(item, 0x24) = static_cast<float>(y);
        g_box.value = g_worldItemCount++;
        *ret = &g_box.header;
        return true;
    }
    if (m == &g_mStrikeNoInteraction) {
        int32_t dmg = *static_cast<int32_t*>(params[0]) - At<int32_t>(obj, kNpcDefense) / 2;
        At<int32_t>(obj, kNpcLife) -= std::max(1, dmg);
        if (At<int32_t>(obj, kNpcLife) <= 0) At<bool>(obj, kNpcActive) = false;  // checkDead
        g_dbl.value = dmg;
        *ret = &g_dbl.header;
        return true;
    }
    return false;
}


}  // namespace

extern "C" {

// ---- controle do teste ------------------------------------------------------
__attribute__((visibility("default"))) void mock_il2cpp_init() {
    g_csharp.classes = {&g_player, &g_main, &g_npc, &g_localUserClass};
    g_mNewNPC.methodPointer = reinterpret_cast<Il2CppMethodPointer>(&MockNewNPC);
    g_npc.fields.clear();
    for (FieldInfo& f : g_fNpc) g_npc.fields.push_back(&f);
    g_npc.methods = {&g_mNewNPC, &g_mSetDefaults, &g_mAI};
    std::memset(g_npcs, 0, sizeof(g_npcs));
    g_mainNpc->max_length = kMaxNpcs;
    for (int i = 0; i < kMaxNpcs; ++i) {
        g_npcAi[i].header.max_length = 4;
        At<FloatArray4*>(g_npcs[i], kNpcAi) = &g_npcAi[i];
        reinterpret_cast<void**>(g_mainNpc + 1)[i] = g_npcs[i];
    }
    g_player.fields = {&g_fStatLife, &g_fWhoAmI};
    for (FieldInfo& f : g_fPlayerEntity) g_player.fields.push_back(&f);
    g_player.methods = {&g_mHurt, &g_mHeal};
    g_main.fields = {&g_fMainPlayer, &g_fMainNpc};
    g_main.methods = {&g_mGetMyPlayer, &g_mDoUpdate};

    std::memset(g_localPlayer, 0, sizeof(g_localPlayer));
    I32(g_localPlayer, kStatDefense) = 10;
    I32(g_localPlayer, kStatLifeMax2) = 500;
    I32(g_localPlayer, kStatLife) = 400;
    g_mainPlayer->max_length = 255;
    reinterpret_cast<void**>(g_mainPlayer + 1)[0] = g_localPlayer;
    // Por último: as linhas acima reatribuem as listas de campos.
    for (BoolStatic& f : g_flags) {
        f.value = false;
        f.field.staticAddr = &f.value;
        (std::strcmp(f.klass, "Main") == 0 ? g_main : g_npc).fields.push_back(&f.field);
    }
    g_localUserClass.fields = {&g_fLocalUserInstance, &g_fGameMenu};
    g_localUser.gameMenu = true;
    InitSaveWorld();
    InitItems();
    InitDraw();
    InitScreens();
    InitTools();
    g_initialized = true;
}
__attribute__((visibility("default"))) void* mock_local_player() { return g_localPlayer; }
__attribute__((visibility("default"))) int mock_skip_to_time_calls() { return g_skipToTimeCalls; }
// Itens criados por Item.NewItem, em ordem (Main.item[0..count)).
__attribute__((visibility("default"))) int32_t mock_world_item_count() { return g_worldItemCount; }
__attribute__((visibility("default"))) void* mock_world_item(int32_t i) {
    return Elem(g_worldItems, static_cast<uint32_t>(i));
}
__attribute__((visibility("default"))) bool mock_patch_function(void* target, void* replacement) {
    if (PatchScreens(target, replacement)) return true;
    if (target == reinterpret_cast<void*>(&MockPlayerHurt)) g_hurtEntry = reinterpret_cast<HurtFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockNewNPC)) g_newNpcEntry = reinterpret_cast<NewNpcFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockNpcSetDefaults)) g_setDefaultsEntry = reinterpret_cast<SetDefaultsFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockNpcAI)) g_aiEntry = reinterpret_cast<AiFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockMainDoUpdate)) g_doUpdateEntry = reinterpret_cast<DoUpdateFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockInternalSavePlayerFile)) g_savePlayerEntry = reinterpret_cast<SavePlayerFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockLoadPlayer)) g_loadPlayerEntry = reinterpret_cast<LoadPlayerFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockInternalSaveWorld)) g_saveWorldEntry = reinterpret_cast<SaveWorldFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockLoadWorld)) g_loadWorldEntry = reinterpret_cast<LoadWorldFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockGridDrawItem)) g_inventoryEntry = reinterpret_cast<GridFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockHotbarDrawItem)) g_hotbarEntry = reinterpret_cast<GridFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockMainDrawItem)) g_mainDrawItemEntry = reinterpret_cast<MainDrawItemFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockDrawItemIcon)) g_iconEntry = reinterpret_cast<IconFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockHeldItem)) g_heldEntry = reinterpret_cast<HeldFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockDrawNpcDirect)) g_npcDrawEntry = reinterpret_cast<NpcDrawFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockItemSetDefaults)) g_itemSetDefaultsEntry = reinterpret_cast<ItemSetDefaultsFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockItemRebuildTooltip)) g_rebuildEntry = reinterpret_cast<RebuildFn>(replacement);
    else return false;
    return true;
}
// Simula o jogo criando um NPC (spawn natural, invocação...).
__attribute__((visibility("default"))) int32_t mock_game_new_npc(int32_t x, int32_t y, int32_t type) {
    return g_newNpcEntry(nullptr, x, y, type, 0, 0, 0, 0, 0, 255, &g_mNewNPC);
}
// Simula Main.DoUpdateInWorld -> NPC.UpdateNPC(i) -> NPC.AI() para os ativos.
__attribute__((visibility("default"))) void mock_game_update_npcs() {
    for (int i = 0; i < kMaxNpcs; ++i)
        if (At<bool>(g_npcs[i], kNpcActive)) g_aiEntry(reinterpret_cast<Il2CppObject*>(g_npcs[i]), &g_mAI);
}
// Simula NPC.Transform(newType), que chama SetDefaults num NPC já existente.
__attribute__((visibility("default"))) void mock_game_transform(int32_t index, int32_t newType) {
    g_setDefaultsEntry(reinterpret_cast<Il2CppObject*>(g_npcs[index]), newType, SpawnParams{}, &g_mSetDefaults);
}
__attribute__((visibility("default"))) void* mock_npc(int32_t index) { return g_npcs[index]; }

// A thread que chama vira a "thread do jogo": recebe a Instance ThreadStatic.
__attribute__((visibility("default"))) void mock_become_game_thread() { tl_localUserInstance = &g_localUser.header; }
__attribute__((visibility("default"))) void mock_set_game_menu(bool inMenu) { g_localUser.gameMenu = inMenu; }
__attribute__((visibility("default"))) bool mock_set_flag(const char* name, bool value) {
    for (BoolStatic& f : g_flags)
        if (std::strcmp(f.field.name, name) == 0) return (f.value = value), true;
    return false;
}
// Um frame: Main.Update -> Main.DoUpdate (passa pelo hook se instalado).
__attribute__((visibility("default"))) int mock_game_frame() {
    g_doUpdateEntry(nullptr, nullptr, &g_mDoUpdate);
    return g_framesRun;
}
// Simula o jogo aplicando dano ao player (passa pelo hook se instalado).
__attribute__((visibility("default"))) double mock_game_hurt_player(void* player, int32_t dmg) {
    return g_hurtEntry(static_cast<Il2CppObject*>(player), nullptr, dmg, 1, false, false, false,
                       -1, true, &g_mHurt);
}

// ---- Save/load ---------------------------------------------------------------------
// O "jogo" salva/carrega pelo endereço (com hook, se instalado). Exceções do
// jogo saem como MockManagedException (o teste pega com catch (...)).
__attribute__((visibility("default"))) void mock_game_save_player() { g_savePlayerEntry(g_playerFile, &g_mInternalSavePlayerFile); }
__attribute__((visibility("default"))) void* mock_game_load_player(const char16_t* path, bool cloud) {
    return g_loadPlayerEntry(NewString(path), cloud, &g_mLoadPlayer);
}
__attribute__((visibility("default"))) void mock_game_save_world() { g_saveWorldEntry(false, false, &g_mInternalSaveWorld); }
__attribute__((visibility("default"))) int mock_game_load_world(bool fail) {
    g_loadFailed = fail;
    g_loadWorldEntry(false, &g_mLoadWorld);
    return g_worldLoads;
}
__attribute__((visibility("default"))) void mock_set_world_path(const char16_t* path) {
    At<Il2CppObject*>(g_activeWorld, kFilePath) = NewString(path);
}
__attribute__((visibility("default"))) void mock_set_load_status(int32_t status) {
    At<int32_t>(g_localPlayer, kPLoadStatus) = status;
}
__attribute__((visibility("default"))) void mock_set_throw_on_save(bool v) { g_throwOnSave = v; }
// netIDs gravados pelo último save; devolve a quantidade.
__attribute__((visibility("default"))) size_t mock_saved_ids(const int32_t** ids) {
    *ids = g_savedIds.data();
    return g_savedIds.size();
}
// Cria um baú em Main.chest[index] (posição em tiles).
__attribute__((visibility("default"))) void mock_add_chest(int32_t index, int32_t x, int32_t y) {
    Elem(g_chests, static_cast<uint32_t>(index)) = NewChest(x, y);
}
__attribute__((visibility("default"))) void mock_remove_chest(int32_t index) {
    Elem(g_chests, static_cast<uint32_t>(index)) = nullptr;
}
__attribute__((visibility("default"))) void* mock_held_item(int32_t index) { return g_held[index]; }

// ---- Itens -------------------------------------------------------------------------
__attribute__((visibility("default"))) void* mock_new_item() { return NewObject(kItemSize); }
// O jogo cria um item (Item.NewItem, loja, receita...): SetDefaults pelo endereço.
__attribute__((visibility("default"))) void mock_game_item_set_defaults(void* item, int32_t type) {
    g_itemSetDefaultsEntry(static_cast<Il2CppObject*>(item), type, false, nullptr, &g_mItemSetDefaults3);
}
// Telas de criação (GUICrafting.CraftButtonOver) chamam RebuildTooltip direto.
__attribute__((visibility("default"))) void mock_game_rebuild_tooltip(void* item) {
    g_rebuildEntry(static_cast<Il2CppObject*>(item), &g_mRebuildTooltip);
}
// Item.Clone = MemberwiseClone.
__attribute__((visibility("default"))) void* mock_game_clone_item(void* item) {
    void* copy = NewObject(kItemSize);
    std::memcpy(copy, item, kItemSize);
    return copy;
}
// Texto (UTF-8) de uma System.String do mock; vazio se nula.
std::string Utf8Of(Il2CppObject* str) {
    std::string out;
    if (!str) return out;
    int32_t n = At<int32_t>(str, 0x10);
    const char16_t* p = reinterpret_cast<const char16_t*>(reinterpret_cast<uint8_t*>(str) + 0x14);
    for (int32_t i = 0; i < n; ++i) {
        char16_t c = p[i];
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) out += static_cast<char>(0xC0 | (c >> 6)), out += static_cast<char>(0x80 | (c & 0x3F));
        else out += static_cast<char>(0xE0 | (c >> 12)), out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)),
             out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}
// Item.Name: _nameOverride ?? nome original (aqui "vanilla:<type>").
__attribute__((visibility("default"))) size_t mock_item_name(void* item, char* buf, size_t size) {
    Il2CppObject* over = At<Il2CppObject*>(item, kItemNameOverride);
    std::string s = over ? Utf8Of(over) : "vanilla:" + std::to_string(At<int32_t>(item, kItemType));
    std::snprintf(buf, size, "%s", s.c_str());
    return s.size();
}
// ToolTip._text.Value; "vanilla" para o tooltip original e "none" para ItemTooltip.None.
__attribute__((visibility("default"))) size_t mock_item_tooltip(void* item, char* buf, size_t size) {
    Il2CppObject* tt = At<Il2CppObject*>(item, kItemToolTip);
    std::string s = !tt ? "null" : tt == g_vanillaTooltip ? "vanilla" : tt == g_tooltipNone ? "none" : "";
    if (s.empty()) {
        Il2CppObject* text = At<Il2CppObject*>(tt, kTooltipText);
        s = text ? Utf8Of(At<Il2CppObject*>(text, kTextValue)) : "sem _text";
        if (At<uint64_t>(tt, kTooltipValidator) == g_globalValidatorKey) s = "não valida: " + s;
    }
    std::snprintf(buf, size, "%s", s.c_str());
    return s.size();
}
__attribute__((visibility("default"))) int mock_barriers() { return g_barriers; }
__attribute__((visibility("default"))) size_t mock_gchandles() { return g_handles.size(); }

// ---- Texturas e desenho ----------------------------------------------------------------
// O jogo termina de carregar os assets (antes disso TLMod_CreateTexture devolve -1).
__attribute__((visibility("default"))) void mock_load_texture_assets() { FillTextureAssets(); }
// Desenhos do "jogo", pelo endereço (com hook). O inventário usa o Player.inventory.
__attribute__((visibility("default"))) void* mock_game_draw_inventory(int32_t slot) {
    g_inventoryEntry(nullptr, nullptr, nullptr, At<Il2CppArray*>(g_localPlayer, kPInventory), slot, {}, {}, 1.0f, &g_mInvDraw);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void* mock_game_draw_hotbar(int32_t slot) {
    g_hotbarEntry(nullptr, nullptr, nullptr, At<Il2CppArray*>(g_localPlayer, kPInventory), slot, {}, {}, 1.0f, &g_mHotbarDraw);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void* mock_game_draw_world_item(void* item) {
    g_mainDrawItemEntry(nullptr, static_cast<Il2CppObject*>(item), 0, &g_mMainDrawItem);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void* mock_game_draw_item_icon(void* item) {
    g_iconEntry(static_cast<Il2CppObject*>(item), 0, nullptr, {}, 1.0f, 32.0f, {}, &g_mIconDraw);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void* mock_game_draw_held(void* item) {
    alignas(16) uint8_t drawinfo[0x400] = {};
    At<void*>(drawinfo, 0x218) = item;
    g_heldEntry(drawinfo, &g_mHeldDraw);
    return g_lastDrawn;
}
// Desenha Main.npc[index]; devolve a textura e preenche quadros e frame vistos no desenho.
__attribute__((visibility("default"))) void* mock_game_draw_npc(int32_t index, int32_t* frameCount, int32_t* frame) {
    g_npcDrawEntry(nullptr, nullptr, reinterpret_cast<Il2CppObject*>(g_npcs[index]), false, {}, nullptr, nullptr, &g_mNpcDraw);
    *frameCount = g_lastFrameCount;
    std::memcpy(frame, &g_lastFrame, sizeof(g_lastFrame));
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void* mock_item_asset_texture(int32_t type) {
    return At<Il2CppObject*>(Elem(g_itemAssets, static_cast<uint32_t>(type)), kAssetValue);
}
__attribute__((visibility("default"))) void* mock_npc_asset_texture(int32_t type) {
    return At<Il2CppObject*>(Elem(g_npcAssets, static_cast<uint32_t>(type)), kAssetValue);
}
__attribute__((visibility("default"))) int32_t mock_npc_frame_count(int32_t type) {
    return reinterpret_cast<int32_t*>(g_npcFrameCount + 1)[type];
}
// Textura do Unity por trás de um XNA Texture2D: tamanho, filtro, wrap, Apply e pixel (x, y)
// com y = 0 no topo da imagem (o Unity guarda de baixo para cima).
__attribute__((visibility("default"))) bool mock_texture_info(void* xna, int32_t* w, int32_t* h, int32_t* filter,
                                                              int32_t* wrap, bool* applied) {
    auto* t = At<UnityTex*>(xna, kXnaUnity);
    if (!t) return false;
    *w = t->width, *h = t->height, *filter = t->filter, *wrap = t->wrap, *applied = t->applied;
    return At<int32_t>(xna, kXnaWidth) == t->width && At<int32_t>(xna, kXnaHeight) == t->height;
}
__attribute__((visibility("default"))) uint32_t mock_texture_pixel(void* xna, int32_t x, int32_t y) {
    auto* t = At<UnityTex*>(xna, kXnaUnity);
    uint32_t v;
    std::memcpy(&v, t->pixels + (static_cast<size_t>(t->height - 1 - y) * t->width + x) * 4, 4);
    return v;
}

// ---- exports da API IL2CPP --------------------------------------------------
const Il2CppImage* il2cpp_get_corlib() { return g_initialized ? &g_corlib : nullptr; }
Il2CppDomain* il2cpp_domain_get() { return g_initialized ? &g_domain : nullptr; }
const Il2CppAssembly** il2cpp_domain_get_assemblies(const Il2CppDomain*, size_t* size) {
    *size = 3;
    return g_assemblies3;
}
const Il2CppImage* il2cpp_assembly_get_image(const Il2CppAssembly* a) { return a->image; }
const char* il2cpp_image_get_name(const Il2CppImage* img) { return img->name; }
Il2CppClass* il2cpp_class_from_name(const Il2CppImage* img, const char* ns, const char* name) {
    for (Il2CppClass* k : img->classes)
        if (!std::strcmp(k->ns, ns) && !std::strcmp(k->name, name)) return k;
    return nullptr;
}
FieldInfo* il2cpp_class_get_field_from_name(Il2CppClass* k, const char* name) {
    for (FieldInfo* f : k->fields)
        if (!std::strcmp(f->name, name)) return f;
    return nullptr;
}
size_t il2cpp_field_get_offset(FieldInfo* f) { return f->offset; }
void il2cpp_field_static_get_value(FieldInfo* f, void* out) {
    std::memcpy(out, f->tls ? f->tls() : f->staticAddr, f->size);
}
void il2cpp_field_static_set_value(FieldInfo* f, void* value) {
    std::memcpy(f->tls ? f->tls() : f->staticAddr, value, f->size);
}
const MethodInfo* il2cpp_class_get_method_from_name(Il2CppClass* k, const char* name, int argc) {
    for (MethodInfo* m : k->methods)
        if (!std::strcmp(m->name, name) && (argc < 0 || m->argc == argc)) return m;
    return nullptr;
}
Il2CppObject* il2cpp_runtime_invoke(const MethodInfo* m, void* obj, void** params,
                                    Il2CppException** exc) {
    *exc = nullptr;
    if (Il2CppObject* ret = nullptr; InvokeTools(m, obj, params, &ret, exc)) return ret;
    if (m == &g_mGetMyPlayer) {
        g_box.value = g_myPlayer;
        return &g_box.header;
    }
    if (m == &g_mNewNPC) {  // como o invoker real: chama o endereço (já com hook)
        g_box.value = g_newNpcEntry(static_cast<Il2CppObject*>(params[0]), *static_cast<int32_t*>(params[1]),
                                    *static_cast<int32_t*>(params[2]), *static_cast<int32_t*>(params[3]),
                                    *static_cast<int32_t*>(params[4]), *static_cast<float*>(params[5]),
                                    *static_cast<float*>(params[6]), *static_cast<float*>(params[7]),
                                    *static_cast<float*>(params[8]), *static_cast<int32_t*>(params[9]), m);
        return &g_box.header;
    }
    if (m == &g_mHeal) {
        MockPlayerHeal(static_cast<Il2CppObject*>(obj), *static_cast<int32_t*>(params[0]), m);
        return nullptr;
    }
    // Como o invoker real: chama o endereço (com hook) e captura a exceção gerenciada.
    try {
        if (m == &g_mInternalSavePlayerFile) {
            g_savePlayerEntry(static_cast<Il2CppObject*>(params[0]), m);
            return nullptr;
        }
        if (m == &g_mInternalSaveWorld) {
            g_saveWorldEntry(*static_cast<bool*>(params[0]), *static_cast<bool*>(params[1]), m);
            return nullptr;
        }
    } catch (const MockManagedException& e) {
        *exc = e.exc;
        return nullptr;
    }
    if (m == &g_mItemSetDefaults1) {  // SetDefaults(int) -> SetDefaults(int, false, null), com hook
        g_itemSetDefaultsEntry(static_cast<Il2CppObject*>(obj), *static_cast<int32_t*>(params[0]), false, nullptr,
                               &g_mItemSetDefaults3);
        return nullptr;
    }
    if (m == &g_mItemCtor) return nullptr;
    if (Il2CppObject* r = nullptr; InvokeScreens(m, obj, params, &r)) return r;
    if (m == &g_mItemPrefix) {
        At<uint8_t>(obj, kItemPrefix) = static_cast<uint8_t>(*static_cast<int32_t*>(params[0]));
        g_box.value = 1;
        return &g_box.header;
    }
    if (m == &g_mTextCtor) {  // LocalizedText(string key, string text)
        At<void*>(obj, kTextKey) = params[0];
        At<void*>(obj, kTextValue) = params[1];
        return nullptr;
    }
    if (m == &g_mUnityCtor) {
        auto* t = reinterpret_cast<UnityTex*>(obj);
        t->width = *static_cast<int32_t*>(params[0]);
        t->height = *static_cast<int32_t*>(params[1]);
        t->format = *static_cast<int32_t*>(params[2]);
        t->mip = *static_cast<bool*>(params[3]);
        if (t->format != 4 || t->mip) std::abort();  // a ponte pede RGBA32 sem mipmap
        t->pixels = static_cast<uint8_t*>(std::calloc(static_cast<size_t>(t->width) * t->height, 4));
        return nullptr;
    }
    if (m == &g_mSetFilter) return reinterpret_cast<UnityTex*>(obj)->filter = *static_cast<int32_t*>(params[0]), nullptr;
    if (m == &g_mSetWrap) return reinterpret_cast<UnityTex*>(obj)->wrap = *static_cast<int32_t*>(params[0]), nullptr;
    if (m == &g_mRawSize) {
        auto* t = reinterpret_cast<UnityTex*>(obj);
        g_box64.value = static_cast<uint64_t>(t->width) * t->height * 4;
        return &g_box64.header;
    }
    if (m == &g_mWritable) {
        g_box64.value = reinterpret_cast<uint64_t>(reinterpret_cast<UnityTex*>(obj)->pixels);
        return &g_box64.header;
    }
    if (m == &g_mApply) return reinterpret_cast<UnityTex*>(obj)->applied = true, nullptr;
    if (m == &g_mXnaCtorUnity) {  // XNA Texture2D(UnityEngine.Texture2D)
        auto* t = static_cast<UnityTex*>(params[0]);
        At<int32_t>(obj, kXnaWidth) = t->width;
        At<int32_t>(obj, kXnaHeight) = t->height;
        At<void*>(obj, kXnaUnity) = t;
        return nullptr;
    }
    if (m == &g_mXnaCtorRender) std::abort();  // a ponte escolheu o .ctor errado
    if (m == &g_mGetMouseItem) return g_held[0];
    if (m == &g_mGetGuideItem) return g_held[2];
    if (m == &g_mGetReforgeItem) return g_held[3];
    if (m == &g_mCreativeGetItem && obj == g_creativeMenu && *static_cast<int32_t*>(params[0]) == 0) return g_held[1];
    std::abort();
}
void* il2cpp_object_unbox(Il2CppObject* o) { return o + 1; }
uint32_t il2cpp_array_length(Il2CppArray* a) { return static_cast<uint32_t>(a->max_length); }
Il2CppThread* il2cpp_thread_attach(Il2CppDomain*) { return t_current = &g_thread; }
Il2CppThread* il2cpp_thread_current() { return t_current; }
void il2cpp_thread_detach(Il2CppThread*) { t_current = nullptr; }
void il2cpp_raise_exception(Il2CppException* exc) { throw MockManagedException{exc}; }
Il2CppObject* il2cpp_object_new(const Il2CppClass* k) {
    Il2CppObject* o = NewObject(k->size);
    o->klass = const_cast<Il2CppClass*>(k);
    return o;
}
Il2CppObject* il2cpp_string_new(const char* utf8) {  // UTF-8 -> UTF-16 (BMP)
    std::u16string u;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8); *p;) {
        char32_t c = *p++;
        if (c >= 0xE0) c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F), p += 2;
        else if (c >= 0xC0) c = ((c & 0x1F) << 6) | (p[0] & 0x3F), p += 1;
        u += static_cast<char16_t>(c);
    }
    return NewString(u.c_str());
}
Il2CppClass* il2cpp_object_get_class(Il2CppObject* obj) { return static_cast<Il2CppClass*>(obj->klass); }
const MethodInfo* il2cpp_class_get_methods(Il2CppClass* k, void** iter) {
    auto i = reinterpret_cast<uintptr_t>(*iter);
    if (i >= k->methods.size()) return nullptr;
    *iter = reinterpret_cast<void*>(i + 1);
    return k->methods[i];
}
const char* il2cpp_method_get_name(const MethodInfo* m) { return m->name; }
Il2CppClass* il2cpp_class_get_nested_types(Il2CppClass* k, void** iter) {
    auto i = reinterpret_cast<uintptr_t>(*iter);
    if (i >= k->nested.size()) return nullptr;
    *iter = reinterpret_cast<void*>(i + 1);
    return k->nested[i];
}
const char* il2cpp_class_get_name(Il2CppClass* k) { return k->name; }
uint32_t il2cpp_method_get_param_count(const MethodInfo* m) { return static_cast<uint32_t>(m->argc); }
const Il2CppType* il2cpp_method_get_param(const MethodInfo* m, uint32_t) {
    return reinterpret_cast<const Il2CppType*>(m->param0);
}
char* il2cpp_type_get_name(const Il2CppType* t) { return t ? strdup(reinterpret_cast<const char*>(t)) : nullptr; }
void il2cpp_free(void* p) { std::free(p); }
uint32_t il2cpp_gchandle_new(Il2CppObject* obj, bool) {
    g_handles.push_back(obj);
    return static_cast<uint32_t>(g_handles.size());
}
Il2CppObject* il2cpp_gchandle_get_target(uint32_t handle) { return handle ? g_handles[handle - 1] : nullptr; }
void il2cpp_gc_wbarrier_set_field(Il2CppObject*, void** target, void* value) {
    ++g_barriers;
    *target = value;
}
void il2cpp_format_exception(const Il2CppException*, char* msg, int size) {
    std::strncpy(msg, "mock exception", size);
}

}  // extern "C"

// ---- Telas que leem TextureAssets.Item sozinhas (screen_hooks.cpp) ------------------
// Cada "tela" acha o item como o jogo (ver os relatórios no README) e registra a
// textura lida de TextureAssets.Item[type] e o nome que mostraria (Item.Name).
namespace {
std::string g_lastName;
void DrewNamed(void* item) {
    DrewItem(item);
    Il2CppObject* over = At<Il2CppObject*>(item, kItemNameOverride);
    g_lastName = over ? Utf8Of(over) : "vanilla:" + std::to_string(At<int32_t>(item, kItemType));
}
Il2CppObject* NewMockItem() {
    Il2CppObject* it = NewObject(kItemSize);
    it->klass = &g_item;
    return it;
}

// GUIInstance.Active com GUIReforge (+0x1A8) e GUICraftGuide (+0xB0).
Il2CppClass g_guiInstance{"", "GUIInstance", {}, {}};
Il2CppObject* g_guiActive = nullptr;
FieldInfo g_fGuiActive{"Active", 0x0, &g_guiActive};
FieldInfo g_fGuiFields[] = {{"GUIReforge", 0x1A8, nullptr}, {"GUICraftGuide", 0xB0, nullptr}};
Il2CppClass g_guiReforge{"", "GUIReforge", {}, {}};
Il2CppClass g_guiCraftGuide{"", "GUICraftGuide", {}, {}};
MethodInfo g_mGuiReforgeItem{nullptr, "get_ReforgeItem", 0};
MethodInfo g_mGuiGuideItem{nullptr, "get_guideItem", 0};
Il2CppObject* g_reforgeItem = nullptr;
Il2CppObject* g_guideItem = nullptr;

// Receitas: Main.recipe, Main.availableRecipe / focusRecipe (getters estáticos).
Il2CppClass g_recipe{"Terraria", "Recipe", {}, {}};
FieldInfo g_fRecipe[] = {{"createItem", 0x10, nullptr}, {"requiredItem", 0x18, nullptr}};
Il2CppArray* g_recipes = nullptr;
FieldInfo g_fMainRecipe{"recipe", 0x668, &g_recipes};
Il2CppArray* g_availableRecipe = nullptr;  // int[]
int32_t g_focusRecipe = 0;
MethodInfo g_mGetAvailableRecipe{nullptr, "get_availableRecipe", 0};
MethodInfo g_mGetFocusRecipe{nullptr, "get_focusRecipe", 0};

// Botão de transação: GUITransactionButton.Draw(layout, Item, label, disabled, ref scale, 4 bools).
// Devolve Clicked (0) para o layout em g_clickLayout (um toque simulado), senão None (3).
Il2CppObject* g_clickLayout = nullptr;
std::vector<std::pair<Il2CppObject*, std::string>> g_buttonsDrawn;  // (layout, rótulo) do último menu
int32_t MockTransactionDraw(Il2CppObject* layout, Il2CppObject* item, Il2CppObject* label, bool, float*, bool, bool,
                            bool, bool, const MethodInfo*) {
    if (item && At<int32_t>(item, kItemType) >= 1 && At<int32_t>(item, kItemStack) >= 1) DrewNamed(item);
    if (label) g_buttonsDrawn.emplace_back(layout, Utf8Of(label));
    return layout && layout == g_clickLayout ? 0 : 3;
}
using TransactionFn = decltype(&MockTransactionDraw);
TransactionFn g_transactionEntry = &MockTransactionDraw;
Il2CppClass g_guiTransaction{"", "GUITransactionButton", {}, {}};
MethodInfo g_mTransactionDraw{reinterpret_cast<Il2CppMethodPointer>(&MockTransactionDraw), "Draw", 9, "Terraria.Item"};

// Menu principal: MainMenu_Layout.Instance com um TransactionButton_Layout por
// botão; GUIMainMenu.Draw desenha os 6 do jogo (PurchaseFullGame só com TrialMode).
Il2CppClass g_mainMenuLayout{"", "MainMenu_Layout", {}, {}};
Il2CppObject* g_menuLayout = nullptr;
FieldInfo g_fMenuLayoutInstance{"Instance", 0x0, &g_menuLayout};
const char* const kMenuButtonNames[] = {"Singleplayer", "Multiplayer", "Achievements", "Settings",
                                        "Tutorial",     "Credits",     "PurchaseFullGame", "PlayerProfile"};
FieldInfo g_fMenuButtons[8] = {};
Il2CppClass g_buttonLayout{"", "Button_Layout", {}, {}};
FieldInfo g_fButtonLayout[] = {{"AnchorControl", 0x10, nullptr}, {"Anchor", 0x14, nullptr}, {"Location", 0x18, nullptr}};
bool g_trialMode = false;
FieldInfo g_fTrialMode{"TrialMode", 0x11EC, &g_trialMode, sizeof(bool)};
Il2CppClass g_guiSettings{"", "GUISettings", {}, {}};
FieldInfo g_fGuiSettingsVisible{"Visible", 0x10, nullptr};
FieldInfo g_fGuiSettings{"GUISettings", 0x1C0, nullptr};
Il2CppClass g_guiMainMenu{"", "GUIMainMenu", {}, {}};
Il2CppClass g_soundEngine{"Terraria.Audio", "SoundEngine", {}, {}};
MethodInfo g_mPlaySound{nullptr, "PlaySound", 6};
int g_soundsPlayed = 0, g_lastSound = -1;
Il2CppObject* MenuButton(int i) { return At<Il2CppObject*>(g_menuLayout, 0x20 + 8 * static_cast<size_t>(i)); }
// Tela e regiões em pixels: 2 px por unidade, âncora no centro, botão de 240x40 unidades.
int32_t g_screenW = 1600, g_screenH = 1200;
struct MockRect {
    int32_t x, y, w, h;
};
MockRect MockButtonRegion(Il2CppObject* layout, const MethodInfo*) {
    float x = At<float>(layout, 0x18), y = At<float>(layout, 0x1C);
    return {g_screenW / 2 + static_cast<int32_t>(x * 2), g_screenH / 2 + static_cast<int32_t>(y * 2), 480, 80};
}
MockRect MockControlRegion(int32_t id, const MethodInfo*) { return id == 0 ? MockRect{0, 0, g_screenW, g_screenH} : MockRect{}; }
MethodInfo g_mButtonRegion{reinterpret_cast<Il2CppMethodPointer>(&MockButtonRegion), "GetRegion", 1};
MethodInfo g_mControlRegion{reinterpret_cast<Il2CppMethodPointer>(&MockControlRegion), "GetControlRegion", 1};
Il2CppClass g_controlAnchor{"", "ControlAnchor", {}, {}};
void MockMainMenuDraw(Il2CppObject*, const MethodInfo*) {
    static const char* const kLabels[] = {"Um Jogador", "Multijogador", "Conquistas", "Configurações",
                                          "Tutorial", "Créditos"};
    float scale = 1.0f;
    for (int i = 0; i < 6; ++i)
        g_transactionEntry(MenuButton(i), nullptr, il2cpp_string_new(kLabels[i]), false, &scale, false, false, false,
                           false, &g_mTransactionDraw);
    if (g_trialMode)
        g_transactionEntry(MenuButton(6), nullptr, il2cpp_string_new("Comprar"), false, &scale, false, false, false,
                           false, &g_mTransactionDraw);
}
using MainMenuFn = decltype(&MockMainMenuDraw);
MainMenuFn g_mainMenuEntry = &MockMainMenuDraw;
MethodInfo g_mMainMenuDraw{reinterpret_cast<Il2CppMethodPointer>(&MockMainMenuDraw), "Draw", 0};

// ---- Oficina nativa: as peças da tela de seleção de mundos ------------------------
// O "desenho" vira linhas de texto em g_frame ("titulo:...", "nome:...", "texto@y:...").
std::vector<std::string> g_frame;
struct MockVec2 {
    float x, y;
};
struct MockRectI {
    int32_t x, y, w, h;
};
Il2CppClass g_worldSelectLayout{"", "WorldSelectMenu_Layout", {}, {}};
Il2CppObject* g_wsLayout = nullptr;
FieldInfo g_fWsInstance{"Instance", 0x0, &g_wsLayout};
Il2CppObject* WsPart(size_t off) { return At<Il2CppObject*>(g_wsLayout, off); }
Il2CppClass g_panelLayout{"", "Panel_Layout", {}, {}};
FieldInfo g_fPanelSize{"Size", 0x20, nullptr};
Il2CppClass g_stringLayout{"", "String_Layout", {}, {}};
const char* PartName(Il2CppObject* layout) {
    static const std::pair<size_t, const char*> kParts[] = {
        {0x100, "nome"}, {0x118, "tempo"}, {0x128, "modo"}, {0x138, "tamanho"}, {0x28, "titulo"},
        {0x30, "novo"},  {0x38, "voltar"}};
    for (const auto& [off, name] : kParts)
        if (g_wsLayout && WsPart(off) == layout) return name;
    return "texto";  // os String_Layout criados pela Oficina
}
// Tela de 969x454 como a do aparelho: a 1ª linha em (100, 100), linhas de 400x50 com
// 10 de espaço; a divisória acima do Voltar em y = 380.
MockVec2 MockAnchored(int32_t, int32_t, MockVec2 pos, const MethodInfo*) { return {pos.x + 100, pos.y + 100}; }
MethodInfo g_mAnchored{reinterpret_cast<Il2CppMethodPointer>(&MockAnchored), "GetAnchoredPosition", 3};
Il2CppClass g_layoutCalculator{"", "LayoutCalculator", {}, {&g_mAnchored}};
void MockPanelDraw(Il2CppObject*, bool, uint64_t, uint64_t, uint64_t, const MethodInfo*) {}
void MockPanelDrawAt(Il2CppObject*, MockVec2, MockVec2, bool, const MethodInfo*) {}
MockRectI MockPanelRegion(Il2CppObject* panel, const MethodInfo*) {
    return panel == WsPart(0x40) ? MockRectI{80, 380, 440, 4} : MockRectI{80, 60, 440, 400};
}
MethodInfo g_mPanelDraw{reinterpret_cast<Il2CppMethodPointer>(&MockPanelDraw), "Draw", 5};
MethodInfo g_mPanelDrawAt{reinterpret_cast<Il2CppMethodPointer>(&MockPanelDrawAt), "DrawAtPosition", 4};
MethodInfo g_mPanelRegion{reinterpret_cast<Il2CppMethodPointer>(&MockPanelRegion), "Region", 1};
Il2CppClass g_guiPanel{"", "GUIPanel", {}, {&g_mPanelDraw, &g_mPanelDrawAt, &g_mPanelRegion}};
MockRectI g_lastGridItem{};
// Textos do Oficina: "texto@<y da região GridItem>:" para as linhas de detalhes.
void MockStringDraw(Il2CppObject* layout, Il2CppObject* value, const MethodInfo*) {
    std::string name = PartName(layout);
    if (name == "texto") name += "@" + std::to_string(g_lastGridItem.y);
    g_frame.push_back(name + ":" + Utf8Of(value));
}
void MockStringDrawColor(Il2CppObject* layout, Il2CppObject* value, uint32_t color, bool, const MethodInfo*) {
    char hex[16];
    std::snprintf(hex, sizeof(hex), "#%08x", color);
    g_frame.push_back(std::string(PartName(layout)) + ":" + Utf8Of(value) + hex);
}
// Quebra simulada: 10 unidades por caractere, por palavra.
Il2CppObject* MockWrap(Il2CppObject* layout, Il2CppObject* value, int32_t* lines, const MethodInfo*) {
    const size_t width = static_cast<size_t>(At<float>(layout, 0x34) / 10.0f);
    std::string in = Utf8Of(value), out, line;
    *lines = 1;
    auto flush = [&] { out += line; line.clear(); };
    for (size_t i = 0; i <= in.size(); ++i) {
        if (i == in.size() || in[i] == '\n') {
            flush();
            if (i < in.size()) out += '\n', ++*lines;
            continue;
        }
        if (in[i] == ' ' && line.size() >= width) {
            flush();
            out += '\n';
            ++*lines;
            continue;
        }
        line += in[i];
    }
    return il2cpp_string_new(out.c_str());
}
MockRectI MockStringRegion(Il2CppObject*, Il2CppObject* value, const MethodInfo*) {
    std::string s = Utf8Of(value);
    return {0, 0, 10 * static_cast<int32_t>(s.size()), 20 * (1 + static_cast<int32_t>(std::count(s.begin(), s.end(), '\n')))};
}
MethodInfo g_mStringDraw{reinterpret_cast<Il2CppMethodPointer>(&MockStringDraw), "Draw", 2, "System.String"};
MethodInfo g_mStringDrawColor{reinterpret_cast<Il2CppMethodPointer>(&MockStringDrawColor), "Draw", 4, "System.String"};
MethodInfo g_mWrap{reinterpret_cast<Il2CppMethodPointer>(&MockWrap), "ApplyMultiLineAlignment", 3};
MethodInfo g_mStringRegion{reinterpret_cast<Il2CppMethodPointer>(&MockStringRegion), "GetRegion", 2};
Il2CppClass g_guiString{"", "GUIString", {}, {&g_mStringDraw, &g_mStringDrawColor, &g_mWrap, &g_mStringRegion}};
bool MockStringButton(Il2CppObject* layout, Il2CppObject* value, float*, bool, bool, const MethodInfo*) {
    g_frame.push_back(std::string(PartName(layout)) + ":" + Utf8Of(value));
    return false;
}
MethodInfo g_mStringButton{reinterpret_cast<Il2CppMethodPointer>(&MockStringButton), "DrawButton", 5};
Il2CppClass g_guiStringButton{"", "GUIStringButton", {}, {&g_mStringButton}};
void MockSetGridItemRegion(MockRectI region, const MethodInfo*) { g_lastGridItem = region; }
MethodInfo g_mSetGridItemRegion{reinterpret_cast<Il2CppMethodPointer>(&MockSetGridItemRegion), "SetGridItemRegion", 1};
int g_batchBegins = 0, g_batchEnds = 0, g_workshopErrors = 0;
Il2CppObject* g_spriteBatch = nullptr;
FieldInfo g_fSpriteBatch{"spriteBatch", 0xC98, &g_spriteBatch};
struct MockNullableMatrix {
    float m[16];
    bool hasValue;
};
void MockBegin(Il2CppObject*, int32_t, Il2CppObject*, Il2CppObject*, Il2CppObject*, Il2CppObject*, Il2CppObject*,
               MockNullableMatrix matrix, bool deferred, const MethodInfo*) {
    if (matrix.hasValue || !deferred) ++g_workshopErrors;  // a Oficina começa como a tela de mundos
    ++g_batchBegins;
}
void MockEnd(Il2CppObject*, const MethodInfo*) { ++g_batchEnds; }
MethodInfo g_mBegin{reinterpret_cast<Il2CppMethodPointer>(&MockBegin), "Begin", 8};
MethodInfo g_mEnd{reinterpret_cast<Il2CppMethodPointer>(&MockEnd), "End", 0};
Il2CppClass g_spriteBatchClass{"Microsoft.Xna.Framework.Graphics", "SpriteBatch", {}, {&g_mBegin, &g_mEnd}};
void MockStringLayoutCtor(Il2CppObject*, const MethodInfo*) {}
void MockStringLayoutCopy(Il2CppObject* self, Il2CppObject* other, const MethodInfo*) {
    std::memcpy(reinterpret_cast<uint8_t*>(self) + 0x10, reinterpret_cast<uint8_t*>(other) + 0x10, 0x30);
}
MethodInfo g_mStringLayoutCtor{reinterpret_cast<Il2CppMethodPointer>(&MockStringLayoutCtor), ".ctor", 0};
MethodInfo g_mStringLayoutCopy{reinterpret_cast<Il2CppMethodPointer>(&MockStringLayoutCopy), "Copy", 1};
// Como no aparelho: Main.mouseX/Y em pixels da tela (1,65 por unidade da interface) e
// Mouse.GetUICursorPosition no espaço dos layouts.
int32_t g_mouseX = 0, g_mouseY = 0;
MockVec2 g_uiCursor{};
bool g_mouseDown = false, g_escape = false;
MockVec2 MockUICursor(const MethodInfo*) { return g_uiCursor; }
MethodInfo g_mUICursor{reinterpret_cast<Il2CppMethodPointer>(&MockUICursor), "GetUICursorPosition", 0};
Il2CppClass g_mouseClass{"Microsoft.Xna.Framework.Input", "Mouse", {}, {&g_mUICursor}};
// Ícones da Oficina: LayoutCalculator.DrawCentred(batch, posição, escala, textura, cor).
std::vector<Il2CppObject*> g_icons;
void MockDrawCentred(Il2CppObject* batch, MockVec2 pos, float scale, Il2CppObject* texture, uint64_t,
                     const MethodInfo*) {
    if (!batch || !texture || !(scale > 0.0f)) ++g_workshopErrors;
    char line[64];
    std::snprintf(line, sizeof(line), "icone@%.0f,%.0f", pos.x, pos.y);
    g_frame.push_back(line);
    g_icons.push_back(texture);
}
MethodInfo g_mDrawCentred{reinterpret_cast<Il2CppMethodPointer>(&MockDrawCentred), "DrawCentred", 5,
                          "Microsoft.Xna.Framework.Vector2"};
bool MockKeyUp(int32_t key, const MethodInfo*) {
    bool hit = key == 27 && g_escape;
    if (hit) g_escape = false;
    return hit;
}
MethodInfo g_mKeyUp{reinterpret_cast<Il2CppMethodPointer>(&MockKeyUp), "GetKeyUp", 1};
Il2CppClass g_keyboardInput{"", "KeyboardInput", {}, {&g_mKeyUp}};
int32_t MockMouseX(const MethodInfo*) { return g_mouseX; }
int32_t MockMouseY(const MethodInfo*) { return g_mouseY; }
bool MockMouseLeft(const MethodInfo*) { return g_mouseDown; }
MethodInfo g_mMouseX{reinterpret_cast<Il2CppMethodPointer>(&MockMouseX), "get_mouseX", 0};
MethodInfo g_mMouseY{reinterpret_cast<Il2CppMethodPointer>(&MockMouseY), "get_mouseY", 0};
MethodInfo g_mMouseLeft{reinterpret_cast<Il2CppMethodPointer>(&MockMouseLeft), "get_mouseLeft", 0};

void InitWorkshop() {
    g_wsLayout = NewObject(0x200);
    for (size_t off = 0x18; off <= 0x158; off += 8) {
        Il2CppObject* part = NewObject(0x150);
        At<float>(part, 0x20) = 400.0f;  // Panel_Layout.Size: linha de 400x50
        At<float>(part, 0x24) = 50.0f;
        At<Il2CppObject*>(g_wsLayout, off) = part;
    }
    Il2CppObject* grid = WsPart(0x20);  // WorldGrid: 1ª linha em (0, 0) + âncora, 10 entre as linhas
    At<float>(grid, 0x30) = 0.0f;
    At<float>(grid, 0x34) = 10.0f;
    At<float>(grid, 0x40) = At<float>(grid, 0x44) = 0.0f;
    g_worldSelectLayout.fields = {&g_fWsInstance};
    g_panelLayout.fields = {&g_fPanelSize};
    g_stringLayout.methods = {&g_mStringLayoutCtor, &g_mStringLayoutCopy};
    g_stringLayout.size = 0x60;
    g_controlAnchor.methods.push_back(&g_mSetGridItemRegion);
    g_spriteBatch = NewObject(0x40);
    g_main.fields.push_back(&g_fSpriteBatch);
    g_main.methods.insert(g_main.methods.end(), {&g_mMouseX, &g_mMouseY, &g_mMouseLeft});
    g_layoutCalculator.methods.push_back(&g_mDrawCentred);
    g_csharp.classes.insert(g_csharp.classes.end(),
                            {&g_worldSelectLayout, &g_panelLayout, &g_stringLayout, &g_guiPanel, &g_guiString,
                             &g_guiStringButton, &g_spriteBatchClass, &g_keyboardInput, &g_layoutCalculator,
                             &g_mouseClass});
}

// Reforja: GUIInstance.Active.GUIReforge.get_ReforgeItem().
void MockReforgePopupDrawItem(Il2CppObject*, const MethodInfo*) {
    if (g_reforgeItem) DrewNamed(g_reforgeItem);
}
using InstanceFn = decltype(&MockReforgePopupDrawItem);
InstanceFn g_reforgePopupEntry = &MockReforgePopupDrawItem;
Il2CppClass g_guiReforgePopup{"", "GUIReforgePopup", {}, {}};
MethodInfo g_mReforgePopupDrawItem{reinterpret_cast<Il2CppMethodPointer>(&MockReforgePopupDrawItem), "DrawItem", 0};

// Pesquisa da Jornada: GetResearchItem(); CanBeResearched olha só o type.
Il2CppObject* g_researchItem = nullptr;
void MockDrawResearchItem(Il2CppObject*, const MethodInfo*) {
    if (g_researchItem) DrewNamed(g_researchItem);
}
InstanceFn g_researchEntry = &MockDrawResearchItem;
int32_t MockCanBeResearched(Il2CppObject*, Il2CppObject* item, int32_t* percent, const MethodInfo*) {
    *percent = 50;
    return item && At<int32_t>(item, kItemType) > 0 ? 1 : 0;  // Valid / Invalid
}
using CanResearchFn = decltype(&MockCanBeResearched);
CanResearchFn g_canResearchEntry = &MockCanBeResearched;
Il2CppClass g_guiResearch{"", "GUIResearch", {}, {}};
MethodInfo g_mDrawResearchItem{reinterpret_cast<Il2CppMethodPointer>(&MockDrawResearchItem), "DrawResearchItem", 0};
MethodInfo g_mCanBeResearched{reinterpret_cast<Il2CppMethodPointer>(&MockCanBeResearched), "CanBeResearched", 2};
MethodInfo g_mGetResearchItem{nullptr, "GetResearchItem", 0};

// Linha "Pesquise mais N" do tooltip na Jornada: o catálogo pelo type (cap 25 para
// type > 0); GetLinesInfo acrescenta a linha quando o catálogo tem um limite.
bool MockSacrificeCap(Il2CppObject*, int32_t itemId, int32_t* amountNeeded, const MethodInfo*) {
    *amountNeeded = itemId > 0 ? 25 : 0;
    return itemId > 0;
}
using SacrificeCapFn = decltype(&MockSacrificeCap);
SacrificeCapFn g_sacrificeCapEntry = &MockSacrificeCap;
Il2CppClass g_sacrificesCatalog{"Terraria.GameContent.Creative", "CreativeItemSacrificesCatalog", {}, {}};
MethodInfo g_mSacrificeCap{reinterpret_cast<Il2CppMethodPointer>(&MockSacrificeCap),
                           "TryGetSacrificeCountCapToUnlockInfiniteItems", 2};
extern MethodInfo g_mTooltipLines;
void MockTooltipLines(Il2CppObject* item, int32_t*, int32_t* researchLine, int32_t*, float, int32_t* numLines,
                      Il2CppArray*, Il2CppArray*, Il2CppArray*, int32_t*, const MethodInfo*) {
    int32_t amount = 0;
    *researchLine = -1;
    if (g_sacrificeCapEntry(nullptr, At<int32_t>(item, kItemType), &amount, &g_mSacrificeCap) && amount > 0)
        *researchLine = (*numLines)++;
}
using TooltipLinesFn = decltype(&MockTooltipLines);
TooltipLinesFn g_tooltipLinesEntry = &MockTooltipLines;
MethodInfo g_mTooltipLines{reinterpret_cast<Il2CppMethodPointer>(&MockTooltipLines),
                           "MouseText_DrawItemTooltip_GetLinesInfo", 10};

// Guia: focusGuideRecipe (+0xCC) >= 0 ? Main.recipe[availableGuideRecipe[focus]].createItem : guideItem.
constexpr size_t kPopupFocus = 0xCC, kPopupAvailable = 0xD0;
Il2CppObject* g_guidePopup = nullptr;
void MockGuidePopupDrawItem(Il2CppObject* self, const MethodInfo*) {
    int32_t focus = At<int32_t>(self, kPopupFocus);
    Il2CppObject* item = g_guideItem;
    if (focus >= 0) {
        int32_t r = reinterpret_cast<int32_t*>(At<Il2CppArray*>(self, kPopupAvailable) + 1)[focus];
        item = At<Il2CppObject*>(Elem(g_recipes, static_cast<uint32_t>(r)), 0x10);
    }
    if (item) DrewNamed(item);
}
InstanceFn g_guidePopupEntry = &MockGuidePopupDrawItem;
Il2CppClass g_guiCraftGuidePopup{"", "GUICraftGuidePopup", {}, {}};
FieldInfo g_fPopup[] = {{"focusGuideRecipe", kPopupFocus, nullptr}, {"availableGuideRecipe", kPopupAvailable, nullptr}};
MethodInfo g_mGuidePopupDrawItem{reinterpret_cast<Il2CppMethodPointer>(&MockGuidePopupDrawItem), "DrawItem", 0};

// Receitas: ingrediente da receita em foco / do tooltip (CraftHover +0x88).
constexpr size_t kCraftHover = 0x88;
Il2CppObject* g_crafting = nullptr;
Il2CppObject* RequiredItem(Il2CppObject* recipe, int32_t index) {
    return static_cast<Il2CppObject*>(Elem(At<Il2CppArray*>(recipe, 0x18), static_cast<uint32_t>(index)));
}
void MockMaterialItemDraw(Il2CppObject*, Il2CppObject*, int32_t index, XnaVector2, float, const MethodInfo*) {
    int32_t r = reinterpret_cast<int32_t*>(g_availableRecipe + 1)[g_focusRecipe];
    Il2CppObject* item = RequiredItem(static_cast<Il2CppObject*>(Elem(g_recipes, static_cast<uint32_t>(r))), index);
    if (At<int32_t>(item, kItemType) != 0) DrewNamed(item);
}
using MaterialFn = decltype(&MockMaterialItemDraw);
MaterialFn g_materialEntry = &MockMaterialItemDraw;
void MockMaterialTooltipDraw(Il2CppObject* self, Il2CppObject*, int32_t index, XnaVector2, float, const MethodInfo*) {
    Il2CppObject* item = RequiredItem(At<Il2CppObject*>(self, kCraftHover), index);
    if (At<int32_t>(item, kItemType) != 0) DrewNamed(item);
}
MaterialFn g_materialTooltipEntry = &MockMaterialTooltipDraw;
Il2CppClass g_guiCrafting{"", "GUICrafting", {}, {}};
FieldInfo g_fCraftHover{"CraftHover", kCraftHover, nullptr};
MethodInfo g_mMaterialDraw{reinterpret_cast<Il2CppMethodPointer>(&MockMaterialItemDraw), "MaterialItemDraw", 4};
MethodInfo g_mMaterialTooltipDraw{reinterpret_cast<Il2CppMethodPointer>(&MockMaterialTooltipDraw),
                                  "MaterialItemTooltipDraw", 4};

// Controles de toque, botões rápidos e ícone do cursor: item na mão
// (inventory[selectedItem], +0x3F0), gancho (QuickGrapple_GetItemToUse), munição (TryGetAmmo).
constexpr size_t kPSelectedItem = 0x3F0;
Il2CppObject* g_grappleItem = nullptr;
Il2CppObject* g_ammoItem = nullptr;
Il2CppObject* g_drawnFirst = nullptr;   // 1ª textura do desenho (botão de uso / item no cursor)
Il2CppObject* g_drawnSecond = nullptr;  // 2ª (gancho / munição)
Il2CppObject* HeldItem() {
    return static_cast<Il2CppObject*>(Elem(At<Il2CppArray*>(g_localPlayer, kPInventory),
                                           static_cast<uint32_t>(At<int32_t>(g_localPlayer, kPSelectedItem))));
}
void MockDrawControls(Il2CppObject*, const MethodInfo*) {
    g_drawnFirst = g_drawnSecond = nullptr;
    if (At<int32_t>(HeldItem(), kItemType) > 0) DrewItem(HeldItem()), g_drawnFirst = g_lastDrawn;
    if (g_grappleItem) DrewItem(g_grappleItem), g_drawnSecond = g_lastDrawn;
}
InstanceFn g_controlsEntry = &MockDrawControls;
Il2CppClass g_guiVirtualInput{"", "GUIVirtualInputController", {}, {}};
MethodInfo g_mDrawControls{reinterpret_cast<Il2CppMethodPointer>(&MockDrawControls), "DrawControls", 0};
MethodInfo g_mQuickGrapple{nullptr, "QuickGrapple_GetItemToUse", 0};

void MockQuickActionDraw(int32_t action, Il2CppObject*, bool, float*, const MethodInfo*) {
    if ((action == 4 || action == 22) && g_grappleItem) {
        DrewItem(g_grappleItem);
    } else {  // Heal: ícone fixo (ItemID 28)
        g_lastDrawn = At<Il2CppObject*>(Elem(g_itemAssets, 28), kAssetValue);
    }
}
using QuickFn = decltype(&MockQuickActionDraw);
QuickFn g_quickEntry = &MockQuickActionDraw;
void MockQuickActionDrawTexture(Il2CppObject*, Il2CppObject*, int32_t, float*, const MethodInfo*) { std::abort(); }
Il2CppClass g_guiQuickAction{"", "GUIQuickActionButton", {}, {}};
MethodInfo g_mQuickDrawTexture{reinterpret_cast<Il2CppMethodPointer>(&MockQuickActionDrawTexture), "Draw", 4,
                               "QuickActionButton_Layout"};
MethodInfo g_mQuickDraw{reinterpret_cast<Il2CppMethodPointer>(&MockQuickActionDraw), "Draw", 4,
                        "QuickActionButton_Layout.QuickActionControl"};

void MockInteractItemIcon(Il2CppObject*, const MethodInfo*) {
    g_drawnFirst = g_drawnSecond = nullptr;
    if (At<int32_t>(HeldItem(), kItemType) > 0) DrewItem(HeldItem()), g_drawnFirst = g_lastDrawn;
    if (g_ammoItem) DrewItem(g_ammoItem), g_drawnSecond = g_lastDrawn;
}
InstanceFn g_interactEntry = &MockInteractItemIcon;
MethodInfo g_mInteractIcon{reinterpret_cast<Il2CppMethodPointer>(&MockInteractItemIcon),
                           "DrawInterface_40_InteractItemIcon", 0};
MethodInfo g_mTryGetAmmo{nullptr, "TryGetAmmo", 5};

// Chat: ItemTagHandler.ItemSnippet (aninhada), _item +0x30; UniqueDraw mede pela textura.
constexpr size_t kSnippetItem = 0x30;
bool MockUniqueDraw(Il2CppObject* self, bool, XnaVector2* size, Il2CppObject*, XnaVector2, XnaColor, float,
                    const MethodInfo*) {
    DrewNamed(At<Il2CppObject*>(self, kSnippetItem));
    *size = XnaVector2{static_cast<float>(At<int32_t>(g_lastDrawn, kXnaWidth)),
                       static_cast<float>(At<int32_t>(g_lastDrawn, kXnaHeight))};
    return true;
}
using UniqueFn = decltype(&MockUniqueDraw);
UniqueFn g_uniqueEntry = &MockUniqueDraw;
Il2CppClass g_itemTagHandler{"Terraria.GameContent.UI.Chat", "ItemTagHandler", {}, {}};
Il2CppClass g_itemSnippet{"", "ItemSnippet", {}, {}, 0x40};
FieldInfo g_fSnippetItem{"_item", kSnippetItem, nullptr};
MethodInfo g_mUniqueDraw{reinterpret_cast<Il2CppMethodPointer>(&MockUniqueDraw), "UniqueDraw", 6};

// Nome de NPC: TypeName = Lang.GetNPCNameValue(netID); GetTypeNetName = NetworkText.FromKey.
constexpr size_t kNetworkTextValue = 0x10, kNetworkTextLiteral = 0x18;
Il2CppClass g_networkText{"Terraria.Localization", "NetworkText", {}, {}, 0x20};
MethodInfo g_mFromLiteral{nullptr, "FromLiteral", 1};
Il2CppObject* MockNpcTypeName(Il2CppObject* self, const MethodInfo*) {
    return il2cpp_string_new(("npc:" + std::to_string(At<int32_t>(self, kNpcType))).c_str());
}
using NpcNameFn = decltype(&MockNpcTypeName);
NpcNameFn g_typeNameEntry = &MockNpcTypeName;
Il2CppObject* MockNpcTypeNetName(Il2CppObject* self, const MethodInfo*) {
    Il2CppObject* t = il2cpp_object_new(&g_networkText);
    At<Il2CppObject*>(t, kNetworkTextValue) =
        il2cpp_string_new(("NPCName." + std::to_string(At<int32_t>(self, kNpcType))).c_str());
    return t;
}
NpcNameFn g_typeNetNameEntry = &MockNpcTypeNetName;
MethodInfo g_mTypeName{reinterpret_cast<Il2CppMethodPointer>(&MockNpcTypeName), "get_TypeName", 0};
MethodInfo g_mTypeNetName{reinterpret_cast<Il2CppMethodPointer>(&MockNpcTypeNetName), "GetTypeNetName", 0};

// runtime_invoke dos métodos das telas (nullptr = não é um deles).
bool InvokeScreens(const MethodInfo* m, void* obj, void** params, Il2CppObject** result) {
    if (m == &g_mGuiReforgeItem) return *result = g_reforgeItem, true;
    if (m == &g_mGuiGuideItem) return *result = g_guideItem, true;
    if (m == &g_mGetResearchItem) return *result = g_researchItem, true;
    if (m == &g_mGetAvailableRecipe) return *result = reinterpret_cast<Il2CppObject*>(g_availableRecipe), true;
    if (m == &g_mGetFocusRecipe) {
        g_box.value = g_focusRecipe;
        return *result = &g_box.header, true;
    }
    if (m == &g_mQuickGrapple) return *result = g_grappleItem, true;
    if (m == &g_mTryGetAmmo) {
        *static_cast<Il2CppObject**>(params[1]) = g_ammoItem;
        g_box.value = g_ammoItem != nullptr;
        return *result = &g_box.header, true;
    }
    if (m == &g_mPlaySound) {
        ++g_soundsPlayed;
        g_lastSound = *static_cast<int32_t*>(params[0]);
        return *result = nullptr, true;
    }
    if (m == &g_mFromLiteral) {
        Il2CppObject* t = il2cpp_object_new(&g_networkText);
        At<Il2CppObject*>(t, kNetworkTextLiteral) = static_cast<Il2CppObject*>(params[0]);
        return *result = t, true;
    }
    (void)obj;
    return false;
}

bool PatchScreens(void* target, void* r) {
    if (target == reinterpret_cast<void*>(&MockTransactionDraw)) g_transactionEntry = reinterpret_cast<TransactionFn>(r);
    else if (target == reinterpret_cast<void*>(&MockReforgePopupDrawItem)) g_reforgePopupEntry = reinterpret_cast<InstanceFn>(r);
    else if (target == reinterpret_cast<void*>(&MockDrawResearchItem)) g_researchEntry = reinterpret_cast<InstanceFn>(r);
    else if (target == reinterpret_cast<void*>(&MockCanBeResearched)) g_canResearchEntry = reinterpret_cast<CanResearchFn>(r);
    else if (target == reinterpret_cast<void*>(&MockMainMenuDraw)) g_mainMenuEntry = reinterpret_cast<MainMenuFn>(r);
    else if (target == reinterpret_cast<void*>(&MockSacrificeCap)) g_sacrificeCapEntry = reinterpret_cast<SacrificeCapFn>(r);
    else if (target == reinterpret_cast<void*>(&MockTooltipLines)) g_tooltipLinesEntry = reinterpret_cast<TooltipLinesFn>(r);
    else if (target == reinterpret_cast<void*>(&MockGuidePopupDrawItem)) g_guidePopupEntry = reinterpret_cast<InstanceFn>(r);
    else if (target == reinterpret_cast<void*>(&MockMaterialItemDraw)) g_materialEntry = reinterpret_cast<MaterialFn>(r);
    else if (target == reinterpret_cast<void*>(&MockMaterialTooltipDraw)) g_materialTooltipEntry = reinterpret_cast<MaterialFn>(r);
    else if (target == reinterpret_cast<void*>(&MockDrawControls)) g_controlsEntry = reinterpret_cast<InstanceFn>(r);
    else if (target == reinterpret_cast<void*>(&MockQuickActionDraw)) g_quickEntry = reinterpret_cast<QuickFn>(r);
    else if (target == reinterpret_cast<void*>(&MockInteractItemIcon)) g_interactEntry = reinterpret_cast<InstanceFn>(r);
    else if (target == reinterpret_cast<void*>(&MockUniqueDraw)) g_uniqueEntry = reinterpret_cast<UniqueFn>(r);
    else if (target == reinterpret_cast<void*>(&MockNpcTypeName)) g_typeNameEntry = reinterpret_cast<NpcNameFn>(r);
    else if (target == reinterpret_cast<void*>(&MockNpcTypeNetName)) g_typeNetNameEntry = reinterpret_cast<NpcNameFn>(r);
    else return false;
    return true;
}

void InitScreens() {
    g_guiActive = NewObject(0x400);
    At<Il2CppObject*>(g_guiActive, 0x1A8) = NewObject(0x40);  // GUIReforge
    At<Il2CppObject*>(g_guiActive, 0xB0) = NewObject(0x40);   // GUICraftGuide
    g_guiInstance.fields = {&g_fGuiActive, &g_fGuiFields[0], &g_fGuiFields[1], &g_fGuiSettings};
    At<Il2CppObject*>(g_guiActive, 0x1C0) = NewObject(0x40);  // GUISettings
    g_guiSettings.fields = {&g_fGuiSettingsVisible};
    // Menu principal: as posições do log de um aparelho (SM-A075M): coluna em
    // X = -120, de 50 em 50 (Singleplayer, Multiplayer, Tutorial, Achievements,
    // Credits, Settings); PurchaseFullGame uma linha abaixo; PlayerProfile à parte.
    g_menuLayout = NewObject(0x80);
    static const float kY[] = {-100, -50, 50, 150, 0, 100, 200, 50};
    for (int i = 0; i < 8; ++i) {
        g_fMenuButtons[i] = FieldInfo{kMenuButtonNames[i], 0x20 + 8 * static_cast<size_t>(i), nullptr};
        g_mainMenuLayout.fields.push_back(&g_fMenuButtons[i]);
        Il2CppObject* b = NewObject(0x150);
        At<int32_t>(b, 0x14) = i == 7 ? 36 : 18;  // PlayerProfile: BottomRight; os outros: CentreBoth
        At<float>(b, 0x18) = i == 7 ? 258.0f : -120.0f;
        At<float>(b, 0x1C) = kY[i];
        At<Il2CppObject*>(g_menuLayout, 0x20 + 8 * static_cast<size_t>(i)) = b;
    }
    g_controlAnchor.methods = {&g_mControlRegion};
    g_mainMenuLayout.fields.push_back(&g_fMenuLayoutInstance);
    g_buttonLayout.fields = {&g_fButtonLayout[0], &g_fButtonLayout[1], &g_fButtonLayout[2]};
    g_main.fields.push_back(&g_fTrialMode);
    g_guiMainMenu.methods = {&g_mMainMenuDraw};
    g_soundEngine.methods = {&g_mPlaySound};
    g_guiReforge.methods = {&g_mGuiReforgeItem};
    g_guiCraftGuide.methods = {&g_mGuiGuideItem};
    g_recipe.fields = {&g_fRecipe[0], &g_fRecipe[1]};
    g_recipes = NewArray(8);
    for (uint32_t i = 0; i < 8; ++i) {
        Il2CppObject* r = NewObject(0x40);
        At<Il2CppObject*>(r, 0x10) = NewMockItem();
        Il2CppArray* req = NewArray(15);
        for (uint32_t k = 0; k < 15; ++k) Elem(req, k) = NewMockItem();
        At<Il2CppArray*>(r, 0x18) = req;
        Elem(g_recipes, i) = r;
    }
    g_availableRecipe = NewArray(8);
    g_main.fields.push_back(&g_fMainRecipe);
    g_main.methods.insert(g_main.methods.end(), {&g_mGetAvailableRecipe, &g_mGetFocusRecipe, &g_mInteractIcon,
                                                 &g_mTryGetAmmo, &g_mTooltipLines});
    g_guiTransaction.methods = {&g_mTransactionDraw, &g_mButtonRegion};
    g_guiReforgePopup.methods = {&g_mReforgePopupDrawItem};
    g_guiResearch.methods = {&g_mDrawResearchItem, &g_mCanBeResearched, &g_mGetResearchItem};
    g_sacrificesCatalog.methods = {&g_mSacrificeCap};
    g_guidePopup = NewObject(0x100);
    At<int32_t>(g_guidePopup, kPopupFocus) = -1;
    At<Il2CppArray*>(g_guidePopup, kPopupAvailable) = NewArray(8);
    g_guiCraftGuidePopup.fields = {&g_fPopup[0], &g_fPopup[1]};
    g_guiCraftGuidePopup.methods = {&g_mGuidePopupDrawItem};
    g_crafting = NewObject(0x100);
    g_guiCrafting.fields = {&g_fCraftHover};
    g_guiCrafting.methods = {&g_mMaterialDraw, &g_mMaterialTooltipDraw};
    g_guiVirtualInput.methods = {&g_mDrawControls};
    g_player.methods.push_back(&g_mQuickGrapple);
    g_guiQuickAction.methods = {&g_mQuickDrawTexture, &g_mQuickDraw};  // o errado primeiro
    g_itemSnippet.fields = {&g_fSnippetItem};
    g_itemSnippet.methods = {&g_mUniqueDraw};
    g_itemTagHandler.nested = {&g_itemSnippet};
    g_networkText.methods = {&g_mFromLiteral};
    g_npc.methods.insert(g_npc.methods.end(), {&g_mTypeName, &g_mTypeNetName});
    g_csharp.classes.insert(g_csharp.classes.end(),
                            {&g_guiInstance, &g_guiReforge, &g_guiCraftGuide, &g_recipe, &g_guiTransaction,
                             &g_guiReforgePopup, &g_guiResearch, &g_guiCraftGuidePopup, &g_guiCrafting,
                             &g_guiVirtualInput, &g_guiQuickAction, &g_itemTagHandler, &g_networkText,
                             &g_sacrificesCatalog, &g_mainMenuLayout, &g_buttonLayout, &g_guiSettings,
                             &g_guiMainMenu, &g_soundEngine, &g_controlAnchor});
    InitWorkshop();
}

}  // namespace

// ---- exports das telas (o "jogo" chamando pelo endereço, com hook) ---------------------
extern "C" {
__attribute__((visibility("default"))) size_t mock_last_name(char* buf, size_t size) {
    std::snprintf(buf, size, "%s", g_lastName.c_str());
    return g_lastName.size();
}
__attribute__((visibility("default"))) bool mock_last_animated() { return g_lastAnimated; }
__attribute__((visibility("default"))) void mock_set_item_animated(int32_t type, bool animated) {
    Elem(g_itemAnimations, static_cast<uint32_t>(type)) = animated ? NewObject(0x20) : nullptr;
}
__attribute__((visibility("default"))) void* mock_game_transaction(void* item) {
    g_lastDrawn = nullptr;
    float scale = 1.0f;
    g_transactionEntry(nullptr, static_cast<Il2CppObject*>(item), nullptr, false, &scale, false, false, false, false,
                       &g_mTransactionDraw);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void* mock_game_reforge(void* item) {
    g_reforgeItem = static_cast<Il2CppObject*>(item);
    g_reforgePopupEntry(nullptr, &g_mReforgePopupDrawItem);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void* mock_game_research(void* item) {
    g_researchItem = static_cast<Il2CppObject*>(item);
    g_researchEntry(NewObject(0x40), &g_mDrawResearchItem);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) int32_t mock_game_can_research(void* item, int32_t* percent) {
    return g_canResearchEntry(nullptr, static_cast<Il2CppObject*>(item), percent, &g_mCanBeResearched);
}
// Menu principal: desenha (com toque simulado no botão Mods se `clickMods`) e
// devolve os rótulos desenhados, separados por '|'; `modsLabel` = o rótulo
// desenhado na posição de PurchaseFullGame ("" se nenhum).
__attribute__((visibility("default"))) size_t mock_game_main_menu(bool clickMods, char* modsLabel, size_t size) {
    g_buttonsDrawn.clear();
    g_clickLayout = clickMods ? MenuButton(6) : nullptr;
    g_mainMenuEntry(NewObject(0x40), &g_mMainMenuDraw);
    g_clickLayout = nullptr;
    std::string mods;
    for (const auto& [layout, label] : g_buttonsDrawn)
        if (layout == MenuButton(6)) mods = label;
    std::snprintf(modsLabel, size, "%s", mods.c_str());
    return g_buttonsDrawn.size();
}
// Textura do i-ésimo ícone desenhado no último frame da Oficina (nulo se não houve).
__attribute__((visibility("default"))) void* mock_workshop_icon(size_t index) {
    return index < g_icons.size() ? g_icons[index] : nullptr;
}
__attribute__((visibility("default"))) void mock_set_trial_mode(bool trial) { g_trialMode = trial; }
// Oficina: um frame do menu com o toque/teclas dados. `click`: 0 nada, 1 Voltar, 2 Novo
// (o botão de ativar/desativar). Devolve as linhas desenhadas separadas por '\n'
// ("menu" se o menu do jogo foi desenhado).
__attribute__((visibility("default"))) size_t mock_workshop_frame(int32_t x, int32_t y, bool down, bool escape,
                                                                   int click, char* out, size_t size) {
    g_uiCursor = {static_cast<float>(x), static_cast<float>(y)};
    g_mouseX = x * 33 / 20, g_mouseY = y * 33 / 20;
    g_mouseDown = down, g_escape = escape;
    g_clickLayout = click == 1 ? WsPart(0x38) : click == 2 ? WsPart(0x30) : nullptr;
    g_frame.clear();
    g_icons.clear();
    g_buttonsDrawn.clear();
    g_mainMenuEntry(NewObject(0x40), &g_mMainMenuDraw);
    g_clickLayout = nullptr;
    std::string all;
    for (const auto& [layout, label] : g_buttonsDrawn)
        all += (layout == MenuButton(0) ? "menu" : std::string("botao:") + PartName(layout) + ":" + label) + "\n";
    for (const std::string& line : g_frame) all += line + "\n";
    std::snprintf(out, size, "%s", all.c_str());
    return all.size();
}
__attribute__((visibility("default"))) int mock_workshop_errors(int* begins, int* ends) {
    *begins = g_batchBegins;
    *ends = g_batchEnds;
    return g_workshopErrors;
}
__attribute__((visibility("default"))) void mock_set_screen(int32_t w, int32_t h) { g_screenW = w, g_screenH = h; }
// Location do botão i do MainMenu_Layout (0 = Singleplayer ... 6 = PurchaseFullGame/Mods).
__attribute__((visibility("default"))) void mock_menu_button_location(int i, float* x, float* y) {
    *x = At<float>(MenuButton(i), 0x18);
    *y = At<float>(MenuButton(i), 0x1C);
}
__attribute__((visibility("default"))) void mock_set_settings_visible(bool visible) {
    At<bool>(At<Il2CppObject*>(g_guiActive, 0x1C0), 0x10) = visible;
}
__attribute__((visibility("default"))) int mock_sounds_played(int* last) {
    *last = g_lastSound;
    return g_soundsPlayed;
}
// Tooltip: índice da linha "Pesquise mais N" (-1 = sem a linha), como no jogo.
__attribute__((visibility("default"))) int32_t mock_game_tooltip_research_line(void* item) {
    int32_t yoyo = -1, research = -1, materials = -1, lines = 1, setBonus = -1;
    g_tooltipLinesEntry(static_cast<Il2CppObject*>(item), &yoyo, &research, &materials, 0.0f, &lines, nullptr,
                        nullptr, nullptr, &setBonus, &g_mTooltipLines);
    return research;
}
// O catálogo chamado por outra tela (fora do tooltip).
__attribute__((visibility("default"))) bool mock_game_sacrifice_cap(int32_t type, int32_t* amount) {
    return g_sacrificeCapEntry(nullptr, type, amount, &g_mSacrificeCap);
}
// Guia: focus -1 = o item da guia; senão a receita availableGuideRecipe[focus] = recipe.
__attribute__((visibility("default"))) void* mock_game_guide(void* guideItem, int32_t focus, int32_t recipe) {
    g_guideItem = static_cast<Il2CppObject*>(guideItem);
    At<int32_t>(g_guidePopup, kPopupFocus) = focus;
    if (focus >= 0)
        reinterpret_cast<int32_t*>(At<Il2CppArray*>(g_guidePopup, kPopupAvailable) + 1)[focus] = recipe;
    g_guidePopupEntry(g_guidePopup, &g_mGuidePopupDrawItem);
    return g_lastDrawn;
}
// Main.recipe[r]: createItem e requiredItem[k] (para montar com SetDefaults pelo teste).
__attribute__((visibility("default"))) void* mock_recipe_item(int32_t r, int32_t k) {
    Il2CppObject* recipe = static_cast<Il2CppObject*>(Elem(g_recipes, static_cast<uint32_t>(r)));
    return k < 0 ? At<Il2CppObject*>(recipe, 0x10) : RequiredItem(recipe, k);
}
__attribute__((visibility("default"))) void* mock_game_material(int32_t recipe, int32_t index) {
    g_focusRecipe = 3;
    reinterpret_cast<int32_t*>(g_availableRecipe + 1)[3] = recipe;
    g_materialEntry(g_crafting, nullptr, index, {}, 1.0f, &g_mMaterialDraw);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void* mock_game_material_tooltip(int32_t recipe, int32_t index) {
    At<Il2CppObject*>(g_crafting, kCraftHover) = static_cast<Il2CppObject*>(Elem(g_recipes, static_cast<uint32_t>(recipe)));
    g_materialTooltipEntry(g_crafting, nullptr, index, {}, 1.0f, &g_mMaterialTooltipDraw);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void mock_set_hands(int32_t selected, void* grapple, void* ammo) {
    At<int32_t>(g_localPlayer, kPSelectedItem) = selected;
    g_grappleItem = static_cast<Il2CppObject*>(grapple);
    g_ammoItem = static_cast<Il2CppObject*>(ammo);
}
__attribute__((visibility("default"))) void mock_game_controls(void** use, void** grapple) {
    g_controlsEntry(nullptr, &g_mDrawControls);
    *use = g_drawnFirst, *grapple = g_drawnSecond;
}
__attribute__((visibility("default"))) void* mock_game_quick_action(int32_t action) {
    float scale = 1.0f;
    g_quickEntry(action, nullptr, false, &scale, &g_mQuickDraw);
    return g_lastDrawn;
}
__attribute__((visibility("default"))) void mock_game_interact_icon(void** held, void** ammo) {
    static Il2CppObject* mainInstance = NewObject(0x40);  // Main.instance (DrawInterface_40 é de instância)
    g_interactEntry(mainInstance, &g_mInteractIcon);
    *held = g_drawnFirst, *ammo = g_drawnSecond;
}
// [i:ID] no chat: um ItemSnippet com o item; devolve a textura e a largura medida.
__attribute__((visibility("default"))) void* mock_game_chat_item(void* item, float* width) {
    Il2CppObject* snippet = il2cpp_object_new(&g_itemSnippet);
    At<void*>(snippet, kSnippetItem) = item;
    XnaVector2 size{};
    g_uniqueEntry(snippet, false, &size, nullptr, {}, {}, 1.0f, &g_mUniqueDraw);
    *width = size.x;
    return g_lastDrawn;
}
// NPC.TypeName e GetTypeNetName de Main.npc[index]; o NetworkText vira "literal:..." ou "key:...".
__attribute__((visibility("default"))) size_t mock_npc_type_name(int32_t index, char* buf, size_t size) {
    std::string s = Utf8Of(g_typeNameEntry(reinterpret_cast<Il2CppObject*>(g_npcs[index]), &g_mTypeName));
    std::snprintf(buf, size, "%s", s.c_str());
    return s.size();
}
__attribute__((visibility("default"))) size_t mock_npc_type_net_name(int32_t index, char* buf, size_t size) {
    Il2CppObject* t = g_typeNetNameEntry(reinterpret_cast<Il2CppObject*>(g_npcs[index]), &g_mTypeNetName);
    Il2CppObject* lit = At<Il2CppObject*>(t, kNetworkTextLiteral);
    std::string s = lit ? "literal:" + Utf8Of(lit) : "key:" + Utf8Of(At<Il2CppObject*>(t, kNetworkTextValue));
    std::snprintf(buf, size, "%s", s.c_str());
    return s.size();
}
}  // extern "C"
