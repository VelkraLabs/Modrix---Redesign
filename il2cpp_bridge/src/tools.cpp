// Ferramentas de teste (mod tlmod.devtools): nomes de itens e NPCs, hora do
// mundo, vida/mana do jogador, item no chão (o que não coube no inventário). Tudo por il2cpp_runtime_invoke ou campo, sem
// hook; só na thread do jogo. Matar NPCs fica em npc_hooks.cpp (usa Main.npc).
#include <algorithm>
#include <cstring>
#include <string>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

namespace {

struct ToolsResolved {
    const MethodInfo* itemName = nullptr;  // static string Lang.GetItemNameValue(int)
    const MethodInfo* npcName = nullptr;   // static string Lang.GetNPCNameValue(int)
    FieldInfo* itemSearch = nullptr;       // static IdDictionary ItemID.Search
    FieldInfo* npcSearch = nullptr;        // static IdDictionary NPCID.Search
    const MethodInfo* getName = nullptr;   // string IdDictionary.GetName(int)
    FieldInfo* dayTime = nullptr;          // static bool Main.dayTime
    FieldInfo* time = nullptr;             // static double Main.time
    const MethodInfo* skipToTime = nullptr;  // static void Main.SkipToTime(int, bool)
    size_t statLife = terraria::player_off::statLife;
    size_t statLifeMax2 = terraria::player_off::statLifeMax2;
    size_t statMana = terraria::player_off::statMana;
    size_t statManaMax2 = terraria::player_off::statManaMax2;
    size_t position = terraria::player_off::position;
    size_t width = terraria::player_off::width;
    size_t height = terraria::player_off::height;
    bool player = false;
    const MethodInfo* newItem = nullptr;  // static int Item.NewItem(IEntitySource, int X, ...)
    FieldInfo* mainItem = nullptr;        // static Item[] Main.item
};
ToolsResolved t;

Il2CppObject* StaticObject(FieldInfo* f) {
    Il2CppObject* obj = nullptr;
    if (f) api.il2cpp_field_static_get_value(f, &obj);
    return obj;
}

// Chama um método que devolve string; exceção (ex.: KeyNotFound num ID sem
// nome interno) vira "sem nome", sem poluir o log: o catálogo pergunta por
// todos os IDs.
bool InvokeString(const MethodInfo* m, void* obj, int32_t arg, std::string* out) {
    if (!m) return false;
    void* params[] = {&arg};
    Il2CppException* exc = nullptr;
    Il2CppObject* str = api.il2cpp_runtime_invoke(m, obj, params, &exc);
    if (exc || !str) return false;
    *out = bridge::Utf8String(str);
    return true;
}

}  // namespace

namespace bridge {

bool ResolveTools() {
    const Il2CppImage* img = Image();
    Il2CppClass* lang = api.il2cpp_class_from_name(img, terraria::kNamespace, "Lang");
    if (lang) {
        t.itemName = api.il2cpp_class_get_method_from_name(lang, "GetItemNameValue",
                                                           terraria::argc::Lang_GetItemNameValue);
        t.npcName = api.il2cpp_class_get_method_from_name(lang, "GetNPCNameValue",
                                                          terraria::argc::Lang_GetNPCNameValue);
    }
    if (Il2CppClass* itemId = api.il2cpp_class_from_name(img, "Terraria.ID", "ItemID"))
        t.itemSearch = api.il2cpp_class_get_field_from_name(itemId, "Search");
    if (Il2CppClass* npcId = api.il2cpp_class_from_name(img, "Terraria.ID", "NPCID"))
        t.npcSearch = api.il2cpp_class_get_field_from_name(npcId, "Search");
    if (Il2CppClass* dict = api.il2cpp_class_from_name(img, "ReLogic.Reflection", "IdDictionary"))
        t.getName = api.il2cpp_class_get_method_from_name(dict, "GetName", terraria::argc::IdDictionary_GetName);
    Il2CppClass* main = MainClass();
    t.dayTime = api.il2cpp_class_get_field_from_name(main, "dayTime");
    t.time = api.il2cpp_class_get_field_from_name(main, "time");
    t.skipToTime = api.il2cpp_class_get_method_from_name(main, "SkipToTime", terraria::argc::Main_SkipToTime);
    Il2CppClass* player = PlayerClass();
    t.statLife = FieldOffset(player, "statLife", terraria::player_off::statLife);
    t.statLifeMax2 = FieldOffset(player, "statLifeMax2", terraria::player_off::statLifeMax2);
    t.statMana = FieldOffset(player, "statMana", terraria::player_off::statMana);
    t.statManaMax2 = FieldOffset(player, "statManaMax2", terraria::player_off::statManaMax2);
    t.position = FieldOffset(player, "position", terraria::player_off::position);
    t.width = FieldOffset(player, "width", terraria::player_off::width);
    t.height = FieldOffset(player, "height", terraria::player_off::height);
    t.player = true;
    if (Il2CppClass* item = api.il2cpp_class_from_name(img, terraria::kNamespace, terraria::kItemClass))
        t.newItem = api.il2cpp_class_get_method_from_name(item, "NewItem", terraria::argc::Item_NewItem);
    t.mainItem = api.il2cpp_class_get_field_from_name(main, "item");
    LOGI("ferramentas: nomes %s/%s, nomes internos %s, hora %s, SkipToTime %s, item no chão %s",
         t.itemName ? "ok" : "-", t.npcName ? "ok" : "-", t.getName && t.itemSearch && t.npcSearch ? "ok" : "-",
         t.dayTime && t.time ? "ok" : "-", t.skipToTime ? "ok" : "-", t.newItem && t.mainItem ? "ok" : "-");
    return t.itemName && t.npcName && t.dayTime && t.time;
}

}  // namespace bridge

extern "C" {

int32_t TLMod_ContentName(TLModNameKind kind, int32_t type, bool internalName, char* buf, int32_t size) {
    if (!bridge::Ready()) return -1;
    std::string name;
    bool ok;
    if (internalName) {
        Il2CppObject* dict = StaticObject(kind == TLMOD_NAME_ITEM ? t.itemSearch : t.npcSearch);
        ok = dict && InvokeString(t.getName, dict, type, &name);
    } else {
        ok = InvokeString(kind == TLMOD_NAME_ITEM ? t.itemName : t.npcName, nullptr, type, &name);
    }
    if (!ok || name.empty()) return -1;
    if (buf && size > 0) {
        size_t n = std::min(name.size(), static_cast<size_t>(size - 1));
        std::memcpy(buf, name.data(), n);
        buf[n] = '\0';
    }
    return static_cast<int32_t>(name.size());
}

bool TLMod_GetWorldTime(bool* day, double* time) {
    if (!t.dayTime || !t.time || !day || !time) return false;
    api.il2cpp_field_static_get_value(t.dayTime, day);
    api.il2cpp_field_static_get_value(t.time, time);
    return true;
}

bool TLMod_SetWorldTime(bool day, double time, bool jump) {
    if (jump && t.skipToTime) {
        int32_t ticks = static_cast<int32_t>(time);
        void* params[] = {&ticks, &day};
        Il2CppException* exc = nullptr;
        api.il2cpp_runtime_invoke(t.skipToTime, nullptr, params, &exc);
        if (!exc) return true;
        bridge::LogException("Main.SkipToTime", exc);  // cai para a gravação direta
    }
    if (!t.dayTime || !t.time) return false;
    api.il2cpp_field_static_set_value(t.dayTime, &day);
    api.il2cpp_field_static_set_value(t.time, &time);
    return true;
}

bool TLMod_RestorePlayer(Il2CppObject* player) {
    if (!player || !t.player) return false;
    FieldAt<int32_t>(player, t.statLife) = FieldAt<int32_t>(player, t.statLifeMax2);
    FieldAt<int32_t>(player, t.statMana) = FieldAt<int32_t>(player, t.statManaMax2);
    return true;
}

Il2CppObject* TLMod_DropItemAtPlayer(Il2CppObject* player, int32_t type) {
    if (!player || !t.player || !t.newItem || !t.mainItem || type <= 0) return nullptr;
    Il2CppObject* source = nullptr;
    int32_t x = static_cast<int32_t>(FieldAt<float>(player, t.position));
    int32_t y = static_cast<int32_t>(FieldAt<float>(player, t.position + 4));
    int32_t w = FieldAt<int32_t>(player, t.width), h = FieldAt<int32_t>(player, t.height);
    int32_t stack = 1, pfix = 0;
    bool noBroadcast = false, noGrabDelay = false, reverseLookup = false;
    void* params[] = {source, &x, &y, &w, &h, &type, &stack, &noBroadcast, &pfix, &noGrabDelay, &reverseLookup};
    Il2CppException* exc = nullptr;
    Il2CppObject* boxed = api.il2cpp_runtime_invoke(t.newItem, nullptr, params, &exc);
    if (exc || !boxed) {
        if (exc) bridge::LogException("Item.NewItem", exc);
        return nullptr;
    }
    int32_t index = *static_cast<int32_t*>(api.il2cpp_object_unbox(boxed));
    Il2CppArray* items = reinterpret_cast<Il2CppArray*>(StaticObject(t.mainItem));
    // 400 (Main.maxItems): o jogo não criou (geração de mundo, ou o tipo em cache).
    if (!items || index < 0 || index >= terraria::kMaxWorldItems ||
        static_cast<uint32_t>(index) >= api.il2cpp_array_length(items))
        return nullptr;
    return ArrayData<Il2CppObject*>(items)[index];
}

}  // extern "C"
