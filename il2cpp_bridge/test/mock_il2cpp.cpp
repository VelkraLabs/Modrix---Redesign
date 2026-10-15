// libil2cpp.so FALSA para teste no host (x86_64/arm64 Linux).
//
// Exporta os mesmos símbolos que a ponte resolve via dlsym e simula
// Terraria.Player / Terraria.Main com os offsets reais do dump
// (statLife @0x704, statDefense @0x6F8 ...). Não tem nada da Unity dentro.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

// Il2CppArray em 64 bits: klass, monitor, bounds, max_length, depois os dados (0x20).
struct Il2CppArray {
    void* klass;
    void* monitor;
    void* bounds;
    uintptr_t max_length;
};

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
const Il2CppAssembly* g_assemblies[] = {&g_asmCorlib, &g_asmCsharp};
Il2CppDomain g_domain{};
Il2CppThread g_thread{};
thread_local Il2CppThread* t_current = nullptr;

Il2CppClass g_player{"Terraria", "Player", {}, {}};
Il2CppClass g_main{"Terraria", "Main", {}, {}};
FieldInfo g_fStatLife{"statLife", kStatLife, nullptr};
FieldInfo g_fWhoAmI{"whoAmI", 0x10, nullptr};  // de Entity; o IL2CPP real busca nas classes-base

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
                 kNpcLife = 0x1A0, kNpcLifeMax = 0x1A4, kNpcSize = 0x200;
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
    g_initialized = true;
}
__attribute__((visibility("default"))) void* mock_local_player() { return g_localPlayer; }
__attribute__((visibility("default"))) bool mock_patch_function(void* target, void* replacement) {
    if (target == reinterpret_cast<void*>(&MockPlayerHurt)) g_hurtEntry = reinterpret_cast<HurtFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockNewNPC)) g_newNpcEntry = reinterpret_cast<NewNpcFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockNpcSetDefaults)) g_setDefaultsEntry = reinterpret_cast<SetDefaultsFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockNpcAI)) g_aiEntry = reinterpret_cast<AiFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockMainDoUpdate)) g_doUpdateEntry = reinterpret_cast<DoUpdateFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockInternalSavePlayerFile)) g_savePlayerEntry = reinterpret_cast<SavePlayerFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockLoadPlayer)) g_loadPlayerEntry = reinterpret_cast<LoadPlayerFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockInternalSaveWorld)) g_saveWorldEntry = reinterpret_cast<SaveWorldFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockLoadWorld)) g_loadWorldEntry = reinterpret_cast<LoadWorldFn>(replacement);
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

// ---- exports da API IL2CPP --------------------------------------------------
const Il2CppImage* il2cpp_get_corlib() { return g_initialized ? &g_corlib : nullptr; }
Il2CppDomain* il2cpp_domain_get() { return g_initialized ? &g_domain : nullptr; }
const Il2CppAssembly** il2cpp_domain_get_assemblies(const Il2CppDomain*, size_t* size) {
    *size = 2;
    return g_assemblies;
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
const MethodInfo* il2cpp_class_get_method_from_name(Il2CppClass* k, const char* name, int argc) {
    for (MethodInfo* m : k->methods)
        if (!std::strcmp(m->name, name) && (argc < 0 || m->argc == argc)) return m;
    return nullptr;
}
Il2CppObject* il2cpp_runtime_invoke(const MethodInfo* m, void* obj, void** params,
                                    Il2CppException** exc) {
    *exc = nullptr;
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
void il2cpp_format_exception(const Il2CppException*, char* msg, int size) {
    std::strncpy(msg, "mock exception", size);
}

}  // extern "C"
