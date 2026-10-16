// Hooks de save/load (personagem e mundo) e acesso aos slots de item que o
// jogo grava. Ver o comentário em bridge.h. RVAs e offsets em terraria_offsets.h.
#include <atomic>
#include <cstring>
#include <string>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

typedef void (*InternalSavePlayerFileFn)(Il2CppObject* playerFile, const MethodInfo* method);
typedef Il2CppObject* (*LoadPlayerFn)(Il2CppObject* path, bool cloudSave, const MethodInfo* method);
typedef void (*InternalSaveWorldFn)(bool useCloudSaving, bool resetTime, const MethodInfo* method);
typedef void (*LoadWorldFn)(bool loadFromCloud, const MethodInfo* method);

namespace {

struct SaveResolved {
    bool ready = false;

    // Começam nos valores do dump; FieldOffset troca pelos do runtime.
    size_t itemType = terraria::item_off::type;
    size_t itemNetId = terraria::item_off::netID;
    size_t itemStack = terraria::item_off::stack;
    size_t itemPrefix = terraria::item_off::prefix;
    size_t itemFavorited = terraria::item_off::favorited;

    size_t chestItem = terraria::chest_off::item;
    size_t chestX = terraria::chest_off::x;
    size_t chestY = terraria::chest_off::y;

    size_t loadoutArmor = terraria::loadout_off::Armor;
    size_t loadoutDye = terraria::loadout_off::Dye;

    size_t inventory = terraria::player_off::inventory;
    size_t armor = terraria::player_off::armor;
    size_t dye = terraria::player_off::dye;
    size_t miscEquips = terraria::player_off::miscEquips;
    size_t miscDyes = terraria::player_off::miscDyes;
    size_t bank[4] = {terraria::player_off::bank, terraria::player_off::bank2, terraria::player_off::bank3,
                      terraria::player_off::bank4};
    size_t loadouts = terraria::player_off::Loadouts;
    size_t temporarySlots = terraria::player_off::_temporaryItemSlots;
    size_t loadStatus = terraria::player_off::loadStatus;

    size_t filePath = terraria::file_data_off::_path;
    size_t fileCloud = terraria::file_data_off::_isCloudSave;
    size_t playerFilePlayer = terraria::player_file_data_off::_player;

    FieldInfo* mainChest = nullptr;           // static Chest[] Main.chest
    FieldInfo* mainActiveWorld = nullptr;     // static WorldFileData Main.ActiveWorldFileData
    FieldInfo* mainCreativeMenu = nullptr;    // static CreativeUI Main.CreativeMenu
    FieldInfo* worldGenLoadFailed = nullptr;  // static bool WorldGen.loadFailed

    const MethodInfo* getMouseItem = nullptr;
    const MethodInfo* getGuideItem = nullptr;
    const MethodInfo* getReforgeItem = nullptr;
    const MethodInfo* creativeGetItem = nullptr;  // CreativeUI.GetItemByIndex(int)

    const MethodInfo* internalSavePlayerFile = nullptr;
    const MethodInfo* loadPlayer = nullptr;
    const MethodInfo* internalSaveWorld = nullptr;
    const MethodInfo* loadWorld = nullptr;
};
SaveResolved s;

// Slots dos originais: mesmo contrato do g_origHurtSlot (ler atomicamente).
void* g_origSavePlayerSlot = nullptr;
void* g_origLoadPlayerSlot = nullptr;
void* g_origSaveWorldSlot = nullptr;
void* g_origLoadWorldSlot = nullptr;

std::atomic<TLModSaveCallback> g_saveCallback{nullptr};

template <typename Fn>
Fn LoadOrig(void* const& slot) {
    return reinterpret_cast<Fn>(__atomic_load_n(&slot, __ATOMIC_ACQUIRE));
}

// System.String (UTF-16) -> UTF-8.
std::string Utf8(Il2CppObject* str) {
    std::string out;
    if (!str) return out;
    int32_t len = FieldAt<int32_t>(str, kStringLengthOffset);
    const char16_t* p = reinterpret_cast<const char16_t*>(reinterpret_cast<uint8_t*>(str) + kStringCharsOffset);
    for (int32_t i = 0; i < len; ++i) {
        uint32_t c = p[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len && p[i + 1] >= 0xDC00 && p[i + 1] <= 0xDFFF)
            c = 0x10000 + ((c - 0xD800) << 10) + (p[++i] - 0xDC00);
        else if (c >= 0xD800 && c <= 0xDFFF)
            c = 0xFFFD;  // metade de par substituto solta
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

template <typename T>
T StaticValue(FieldInfo* f) {
    T v{};
    if (f) api.il2cpp_field_static_get_value(f, &v);
    return v;
}

uint32_t Length(Il2CppArray* arr) { return arr ? api.il2cpp_array_length(arr) : 0; }

Il2CppObject* At(Il2CppArray* arr, int32_t i) {
    if (!arr || i < 0 || static_cast<uint32_t>(i) >= api.il2cpp_array_length(arr)) return nullptr;
    return ArrayData<Il2CppObject*>(arr)[i];
}

Il2CppObject* ChestAt(int32_t index) { return At(StaticValue<Il2CppArray*>(s.mainChest), index); }

// Item[] do contêiner. HELD não é array (são getters).
Il2CppArray* ItemArray(Il2CppObject* player, TLModItemContainer c, int32_t group) {
    if (c == TLMOD_ITEMS_CHEST) {
        Il2CppObject* chest = ChestAt(group);
        return chest ? FieldAt<Il2CppArray*>(chest, s.chestItem) : nullptr;
    }
    if (!player) return nullptr;
    switch (c) {
        case TLMOD_ITEMS_INVENTORY: return FieldAt<Il2CppArray*>(player, s.inventory);
        case TLMOD_ITEMS_ARMOR: return FieldAt<Il2CppArray*>(player, s.armor);
        case TLMOD_ITEMS_DYE: return FieldAt<Il2CppArray*>(player, s.dye);
        case TLMOD_ITEMS_MISC_EQUIPS: return FieldAt<Il2CppArray*>(player, s.miscEquips);
        case TLMOD_ITEMS_MISC_DYES: return FieldAt<Il2CppArray*>(player, s.miscDyes);
        case TLMOD_ITEMS_BANK:
        case TLMOD_ITEMS_BANK2:
        case TLMOD_ITEMS_BANK3:
        case TLMOD_ITEMS_BANK4: {
            Il2CppObject* bank = FieldAt<Il2CppObject*>(player, s.bank[c - TLMOD_ITEMS_BANK]);
            return bank ? FieldAt<Il2CppArray*>(bank, s.chestItem) : nullptr;
        }
        case TLMOD_ITEMS_LOADOUT_ARMOR:
        case TLMOD_ITEMS_LOADOUT_DYE: {
            Il2CppObject* loadout = At(FieldAt<Il2CppArray*>(player, s.loadouts), group);
            if (!loadout) return nullptr;
            return FieldAt<Il2CppArray*>(loadout, c == TLMOD_ITEMS_LOADOUT_ARMOR ? s.loadoutArmor : s.loadoutDye);
        }
        case TLMOD_ITEMS_TEMPORARY: return FieldAt<Il2CppArray*>(player, s.temporarySlots);
        default: return nullptr;
    }
}

// runtime_invoke de um getter/método que devolve um objeto (tipo-referência).
Il2CppObject* InvokeObject(const MethodInfo* m, void* obj, void** params, const char* what) {
    if (!m) return nullptr;
    Il2CppException* exc = nullptr;
    Il2CppObject* r = api.il2cpp_runtime_invoke(m, obj, params, &exc);
    if (exc) {
        bridge::LogException(what, exc);
        return nullptr;
    }
    return r;
}

Il2CppObject* HeldItem(int32_t index) {
    switch (index) {
        case 0: return InvokeObject(s.getMouseItem, nullptr, nullptr, "Main.get_mouseItem");
        case 1: {
            Il2CppObject* menu = StaticValue<Il2CppObject*>(s.mainCreativeMenu);
            int32_t slot = 0;  // CreativeUI: 0 = slot de sacrifício
            void* params[] = {&slot};
            return menu ? InvokeObject(s.creativeGetItem, menu, params, "CreativeUI.GetItemByIndex") : nullptr;
        }
        case 2: return InvokeObject(s.getGuideItem, nullptr, nullptr, "Main.get_guideItem");
        case 3: return InvokeObject(s.getReforgeItem, nullptr, nullptr, "Main.get_reforgeItem");
        default: return nullptr;
    }
}

// --- Dados do arquivo ------------------------------------------------------------
std::string FilePath(Il2CppObject* fileData) {
    return fileData ? Utf8(FieldAt<Il2CppObject*>(fileData, s.filePath)) : std::string();
}

Il2CppObject* ActiveWorld() { return StaticValue<Il2CppObject*>(s.mainActiveWorld); }

void Notify(TLModSaveCallback cb, const TLModSaveEvent& ev, TLModSavePhase phase) {
    if (cb) cb(&ev, phase);
}

// Reentrada: o save original é chamado via il2cpp_runtime_invoke, que entra
// de novo no hook (o methodPointer é o endereço com o hook). Na segunda vez o
// hook só repassa para o original.
thread_local bool t_inPlayerSave = false;
thread_local bool t_inWorldSave = false;

// Avisa "antes", roda o save original e avisa "depois", inclusive quando o
// save lança exceção (ok = false). Devolve a exceção para quem chamou
// relançar.
//
// Por que runtime_invoke e não um destrutor (RAII): o IL2CPP lança exceções
// gerenciadas como exceções C++ do libc++abi embutido na libil2cpp.so. Esta
// .so tem a sua própria cópia estática; um frame nosso com limpeza no meio do
// desenrolar mistura as duas cópias. runtime_invoke captura a exceção dentro da
// libil2cpp e a devolve; il2cpp_raise_exception a relança de lá. Nossos frames
// só são atravessados por CFI, como nos outros hooks.
__attribute__((noinline)) Il2CppException* RunSave(TLModSaveCallback cb, TLModSaveKind kind, Il2CppObject* fileData,
                                                   bool cloud, Il2CppObject* player, const MethodInfo* method,
                                                   void** params, bool& inSave) {
    std::string path = FilePath(fileData);
    TLModSaveEvent ev{};
    ev.kind = kind;
    ev.path = path.c_str();
    ev.cloud = cloud;
    ev.player = player;
    ev.ok = true;
    Notify(cb, ev, TLMOD_BEFORE_SAVE);

    Il2CppException* exc = nullptr;
    inSave = true;
    api.il2cpp_runtime_invoke(method, nullptr, params, &exc);
    inSave = false;

    ev.ok = exc == nullptr;
    Notify(cb, ev, TLMOD_AFTER_SAVE);
    return exc;
}

// Depois do load, fora do frame do hook (que fica sem objetos com destrutor,
// ver RunSave).
__attribute__((noinline)) void NotifyPlayerLoaded(TLModSaveCallback cb, Il2CppObject* data, Il2CppObject* pathArg) {
    Il2CppObject* player = FieldAt<Il2CppObject*>(data, s.playerFilePlayer);
    if (!player) return;
    // O caminho do PlayerFileData é o que o save vai usar depois.
    std::string path = FilePath(data);
    if (path.empty()) path = Utf8(pathArg);
    TLModSaveEvent ev{};
    ev.kind = TLMOD_SAVE_PLAYER;
    ev.path = path.c_str();
    ev.cloud = FieldAt<bool>(data, s.fileCloud);
    ev.player = player;
    ev.ok = FieldAt<int32_t>(player, s.loadStatus) == 0;
    Notify(cb, ev, TLMOD_AFTER_LOAD);
}

__attribute__((noinline)) void NotifyWorldLoaded(TLModSaveCallback cb, bool cloud) {
    std::string path = FilePath(ActiveWorld());
    if (path.empty()) return;
    TLModSaveEvent ev{};
    ev.kind = TLMOD_SAVE_WORLD;
    ev.path = path.c_str();
    ev.cloud = cloud;
    ev.player = nullptr;
    ev.ok = !StaticValue<bool>(s.worldGenLoadFailed);
    Notify(cb, ev, TLMOD_AFTER_LOAD);
}

}  // namespace

// ===========================================================================
// Resolução e instalação (chamadas por TLMod_Init)
// ===========================================================================
namespace bridge {

std::string Utf8String(Il2CppObject* str) { return Utf8(str); }

bool ResolveSaves() {
    const Il2CppImage* img = Image();
    Il2CppClass* item = api.il2cpp_class_from_name(img, terraria::kNamespace, terraria::kItemClass);
    Il2CppClass* chest = api.il2cpp_class_from_name(img, terraria::kNamespace, terraria::kChestClass);
    Il2CppClass* loadout = api.il2cpp_class_from_name(img, terraria::kNamespace, terraria::kEquipmentLoadoutClass);
    Il2CppClass* worldGen = api.il2cpp_class_from_name(img, terraria::kNamespace, terraria::kWorldGenClass);
    Il2CppClass* fileData = api.il2cpp_class_from_name(img, terraria::kIoNamespace, terraria::kFileDataClass);
    Il2CppClass* playerFile = api.il2cpp_class_from_name(img, terraria::kIoNamespace, terraria::kPlayerFileDataClass);
    Il2CppClass* worldFile = api.il2cpp_class_from_name(img, terraria::kIoNamespace, terraria::kWorldFileClass);
    Il2CppClass* creativeUi = api.il2cpp_class_from_name(img, "Terraria.GameContent.Creative", "CreativeUI");
    Il2CppClass* player = PlayerClass();
    if (!item || !chest || !loadout || !fileData || !playerFile || !worldFile || !player) {
        LOGW("save: classe ausente (Item=%p Chest=%p EquipmentLoadout=%p FileData=%p PlayerFileData=%p "
             "WorldFile=%p); hooks de save desativados",
             static_cast<void*>(item), static_cast<void*>(chest), static_cast<void*>(loadout),
             static_cast<void*>(fileData), static_cast<void*>(playerFile), static_cast<void*>(worldFile));
        return false;
    }

    s.itemType = FieldOffset(item, "type", terraria::item_off::type);
    s.itemNetId = FieldOffset(item, "netID", terraria::item_off::netID);
    s.itemStack = FieldOffset(item, "stack", terraria::item_off::stack);
    s.itemPrefix = FieldOffset(item, "prefix", terraria::item_off::prefix);
    s.itemFavorited = FieldOffset(item, "favorited", terraria::item_off::favorited);
    s.chestItem = FieldOffset(chest, "item", terraria::chest_off::item);
    s.chestX = FieldOffset(chest, "x", terraria::chest_off::x);
    s.chestY = FieldOffset(chest, "y", terraria::chest_off::y);
    s.loadoutArmor = FieldOffset(loadout, "Armor", terraria::loadout_off::Armor);
    s.loadoutDye = FieldOffset(loadout, "Dye", terraria::loadout_off::Dye);
    s.inventory = FieldOffset(player, "inventory", terraria::player_off::inventory);
    s.armor = FieldOffset(player, "armor", terraria::player_off::armor);
    s.dye = FieldOffset(player, "dye", terraria::player_off::dye);
    s.miscEquips = FieldOffset(player, "miscEquips", terraria::player_off::miscEquips);
    s.miscDyes = FieldOffset(player, "miscDyes", terraria::player_off::miscDyes);
    s.bank[0] = FieldOffset(player, "bank", terraria::player_off::bank);
    s.bank[1] = FieldOffset(player, "bank2", terraria::player_off::bank2);
    s.bank[2] = FieldOffset(player, "bank3", terraria::player_off::bank3);
    s.bank[3] = FieldOffset(player, "bank4", terraria::player_off::bank4);
    s.loadouts = FieldOffset(player, "Loadouts", terraria::player_off::Loadouts);
    s.temporarySlots = FieldOffset(player, "_temporaryItemSlots", terraria::player_off::_temporaryItemSlots);
    s.loadStatus = FieldOffset(player, "loadStatus", terraria::player_off::loadStatus);
    s.filePath = FieldOffset(fileData, "_path", terraria::file_data_off::_path);
    s.fileCloud = FieldOffset(fileData, "_isCloudSave", terraria::file_data_off::_isCloudSave);
    s.playerFilePlayer = FieldOffset(playerFile, "_player", terraria::player_file_data_off::_player);

    Il2CppClass* main = MainClass();
    s.mainChest = api.il2cpp_class_get_field_from_name(main, "chest");
    s.mainActiveWorld = api.il2cpp_class_get_field_from_name(main, "ActiveWorldFileData");
    s.mainCreativeMenu = api.il2cpp_class_get_field_from_name(main, "CreativeMenu");
    s.worldGenLoadFailed = worldGen ? api.il2cpp_class_get_field_from_name(worldGen, "loadFailed") : nullptr;
    s.getMouseItem = api.il2cpp_class_get_method_from_name(main, "get_mouseItem", terraria::argc::Main_get_mouseItem);
    s.getGuideItem = api.il2cpp_class_get_method_from_name(main, "get_guideItem", terraria::argc::Main_get_guideItem);
    s.getReforgeItem =
        api.il2cpp_class_get_method_from_name(main, "get_reforgeItem", terraria::argc::Main_get_reforgeItem);
    s.creativeGetItem = creativeUi ? api.il2cpp_class_get_method_from_name(creativeUi, "GetItemByIndex",
                                                                           terraria::argc::CreativeUI_GetItemByIndex)
                                   : nullptr;

    s.internalSavePlayerFile = api.il2cpp_class_get_method_from_name(
        player, "InternalSavePlayerFile", terraria::argc::Player_InternalSavePlayerFile);
    s.loadPlayer = api.il2cpp_class_get_method_from_name(player, "LoadPlayer", terraria::argc::Player_LoadPlayer);
    s.internalSaveWorld = api.il2cpp_class_get_method_from_name(worldFile, "InternalSaveWorld",
                                                                terraria::argc::WorldFile_InternalSaveWorld);
    s.loadWorld = api.il2cpp_class_get_method_from_name(worldFile, "LoadWorld", terraria::argc::WorldFile_LoadWorld);

    // Sem os quatro hooks não dá para garantir a regra "nenhum ID de mod no
    // arquivo": melhor não instalar nenhum e avisar.
    if (!s.internalSavePlayerFile || !s.loadPlayer || !s.internalSaveWorld || !s.loadWorld || !s.mainChest ||
        !s.mainActiveWorld) {
        LOGW("save: faltou resolver InternalSavePlayerFile=%p LoadPlayer=%p InternalSaveWorld=%p LoadWorld=%p "
             "Main.chest=%p Main.ActiveWorldFileData=%p; hooks de save desativados",
             static_cast<const void*>(s.internalSavePlayerFile), static_cast<const void*>(s.loadPlayer),
             static_cast<const void*>(s.internalSaveWorld), static_cast<const void*>(s.loadWorld),
             static_cast<void*>(s.mainChest), static_cast<void*>(s.mainActiveWorld));
        return false;
    }
    if (!s.getMouseItem || !s.getGuideItem || !s.getReforgeItem || !s.creativeGetItem || !s.mainCreativeMenu)
        LOGW("save: algum item temporário (mouse/sacrifício/Guia/Reforja) não resolveu; esse slot fica de fora");
    if (!s.worldGenLoadFailed) LOGW("save: WorldGen.loadFailed não encontrado; todo LoadWorld conta como ok");
    s.ready = true;
    return true;
}

void InstallSaveHooks() {
    InstallHook("Player.InternalSavePlayerFile", s.internalSavePlayerFile, terraria::rva::Player_InternalSavePlayerFile,
                reinterpret_cast<void*>(&TLMod_Hooked_PlayerInternalSavePlayerFile), &g_origSavePlayerSlot);
    InstallHook("Player.LoadPlayer", s.loadPlayer, terraria::rva::Player_LoadPlayer,
                reinterpret_cast<void*>(&TLMod_Hooked_PlayerLoadPlayer), &g_origLoadPlayerSlot);
    InstallHook("WorldFile.InternalSaveWorld", s.internalSaveWorld, terraria::rva::WorldFile_InternalSaveWorld,
                reinterpret_cast<void*>(&TLMod_Hooked_WorldFileInternalSaveWorld), &g_origSaveWorldSlot);
    InstallHook("WorldFile.LoadWorld", s.loadWorld, terraria::rva::WorldFile_LoadWorld,
                reinterpret_cast<void*>(&TLMod_Hooked_WorldFileLoadWorld), &g_origLoadWorldSlot);
}

}  // namespace bridge

// ===========================================================================
// API pública
// ===========================================================================
extern "C" {

void TLMod_SetSaveCallback(TLModSaveCallback cb) { g_saveCallback.store(cb, std::memory_order_release); }

int32_t TLMod_ItemSlotCount(Il2CppObject* player, TLModItemContainer container, int32_t group) {
    if (!s.ready) return -1;
    if (container == TLMOD_ITEMS_HELD) return 4;
    Il2CppArray* arr = ItemArray(player, container, group);
    if (!arr) return -1;
    auto n = static_cast<int32_t>(Length(arr));
    // O save grava inventory[0..57]; o 58 é só da interface.
    return container == TLMOD_ITEMS_INVENTORY && n > 58 ? 58 : n;
}

Il2CppObject* TLMod_ItemSlot(Il2CppObject* player, TLModItemContainer container, int32_t group, int32_t index) {
    if (!s.ready || index < 0) return nullptr;
    if (container == TLMOD_ITEMS_HELD) return HeldItem(index);
    if (index >= TLMod_ItemSlotCount(player, container, group)) return nullptr;
    return At(ItemArray(player, container, group), index);
}

bool TLMod_ReadItem(Il2CppObject* item, TLModItemFields* out) {
    if (!item || !out || !s.ready) return false;
    out->type = FieldAt<int32_t>(item, s.itemType);
    out->netID = FieldAt<int32_t>(item, s.itemNetId);
    out->stack = FieldAt<int32_t>(item, s.itemStack);
    out->prefix = FieldAt<uint8_t>(item, s.itemPrefix);
    out->favorited = FieldAt<bool>(item, s.itemFavorited);
    return true;
}

bool TLMod_WriteItem(Il2CppObject* item, const TLModItemFields* in) {
    if (!item || !in || !s.ready) return false;
    FieldAt<int32_t>(item, s.itemType) = in->type;
    FieldAt<int32_t>(item, s.itemNetId) = in->netID;
    FieldAt<int32_t>(item, s.itemStack) = in->stack;
    FieldAt<uint8_t>(item, s.itemPrefix) = in->prefix;
    FieldAt<bool>(item, s.itemFavorited) = in->favorited;
    return true;
}

int32_t TLMod_ChestCount(void) {
    return s.ready ? static_cast<int32_t>(Length(StaticValue<Il2CppArray*>(s.mainChest))) : 0;
}

bool TLMod_ChestPosition(int32_t index, int32_t* x, int32_t* y) {
    Il2CppObject* chest = s.ready ? ChestAt(index) : nullptr;
    if (!chest || !x || !y) return false;
    *x = FieldAt<int32_t>(chest, s.chestX);
    *y = FieldAt<int32_t>(chest, s.chestY);
    return true;
}

// ---------------------------------------------------------------------------
// Handlers dos hooks. Rodam na thread que salva/carrega.
// ---------------------------------------------------------------------------

// Player.InternalSavePlayerFile (RVA 0x1044040). Só SavePlayer chama.
// Sem objetos com destrutor neste frame: exceções passam direto (ver RunSave).
void TLMod_Hooked_PlayerInternalSavePlayerFile(Il2CppObject* playerFile, const MethodInfo* method) {
    auto orig = LoadOrig<InternalSavePlayerFileFn>(g_origSavePlayerSlot);
    if (!orig) return;
    TLModSaveCallback cb = g_saveCallback.load(std::memory_order_acquire);
    Il2CppObject* player = playerFile ? FieldAt<Il2CppObject*>(playerFile, s.playerFilePlayer) : nullptr;
    if (t_inPlayerSave || !cb || !player) {
        orig(playerFile, method);
        return;
    }
    void* params[] = {playerFile};
    Il2CppException* exc = RunSave(cb, TLMOD_SAVE_PLAYER, playerFile, FieldAt<bool>(playerFile, s.fileCloud), player,
                                   s.internalSavePlayerFile, params, t_inPlayerSave);
    if (exc) api.il2cpp_raise_exception(exc);
}

// Player.LoadPlayer (RVA 0x1049FA8): devolve o PlayerFileData já lido.
Il2CppObject* TLMod_Hooked_PlayerLoadPlayer(Il2CppObject* path, bool cloudSave, const MethodInfo* method) {
    auto orig = LoadOrig<LoadPlayerFn>(g_origLoadPlayerSlot);
    if (!orig) return nullptr;
    Il2CppObject* data = orig(path, cloudSave, method);
    TLModSaveCallback cb = g_saveCallback.load(std::memory_order_acquire);
    if (cb && data) NotifyPlayerLoaded(cb, data, path);
    return data;
}

// WorldFile.InternalSaveWorld (RVA 0x14F1AC8). Só SaveWorld chama; grava em
// Main.ActiveWorldFileData.Path (= Main.worldPathName).
void TLMod_Hooked_WorldFileInternalSaveWorld(bool useCloudSaving, bool resetTime, const MethodInfo* method) {
    auto orig = LoadOrig<InternalSaveWorldFn>(g_origSaveWorldSlot);
    if (!orig) return;
    TLModSaveCallback cb = g_saveCallback.load(std::memory_order_acquire);
    Il2CppObject* world = ActiveWorld();
    if (t_inWorldSave || !cb || !world) {
        orig(useCloudSaving, resetTime, method);
        return;
    }
    void* params[] = {&useCloudSaving, &resetTime};
    Il2CppException* exc = RunSave(cb, TLMOD_SAVE_WORLD, world, useCloudSaving, nullptr, s.internalSaveWorld,
                                   params, t_inWorldSave);
    if (exc) api.il2cpp_raise_exception(exc);
}

// WorldFile.LoadWorld (RVA 0x14EC048). Pode chamar SaveWorld lá dentro
// (conversão de versão): o runtime trata o save de um mundo ainda não aplicado.
void TLMod_Hooked_WorldFileLoadWorld(bool loadFromCloud, const MethodInfo* method) {
    auto orig = LoadOrig<LoadWorldFn>(g_origLoadWorldSlot);
    if (!orig) return;
    orig(loadFromCloud, method);
    TLModSaveCallback cb = g_saveCallback.load(std::memory_order_acquire);
    if (cb) NotifyWorldLoaded(cb, loadFromCloud);
}

}  // extern "C"
