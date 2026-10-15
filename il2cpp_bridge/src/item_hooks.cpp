// Itens de mod numa casca original: hooks em Item.SetDefaults e
// Item.RebuildTooltip, atributos, nome e tooltip. Ver o comentário em bridge.h.
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

typedef void (*ItemSetDefaultsFn)(Il2CppObject* self, int32_t type, bool noMatCheck, Il2CppObject* variant,
                                  const MethodInfo* method);
typedef void (*ItemRebuildTooltipFn)(Il2CppObject* self, const MethodInfo* method);

namespace {

struct ItemResolved {
    bool ready = false;
    // Começam nos valores do dump; FieldOffset troca pelos do runtime.
    size_t type = terraria::item_off::type;
    size_t netID = terraria::item_off::netID;
    size_t width = terraria::item_off::width;
    size_t height = terraria::item_off::height;
    size_t damage = terraria::item_off::damage;
    size_t useTime = terraria::item_off::useTime;
    size_t useAnimation = terraria::item_off::useAnimation;
    size_t useStyle = terraria::item_off::useStyle;
    size_t melee = terraria::item_off::melee;
    size_t maxStack = terraria::item_off::maxStack;
    size_t value = terraria::item_off::value;
    size_t rare = terraria::item_off::rare;
    size_t nameOverride = terraria::item_off::_nameOverride;
    size_t toolTip = terraria::item_off::ToolTip;

    Il2CppClass* tooltipClass = nullptr;  // Terraria.UI.ItemTooltip
    Il2CppClass* textClass = nullptr;     // Terraria.Localization.LocalizedText
    size_t tooltipValidator = terraria::item_tooltip_off::_validatorKey;
    size_t tooltipText = terraria::item_tooltip_off::_text;
    FieldInfo* tooltipNone = nullptr;       // static ItemTooltip.None
    FieldInfo* tooltipGlobalKey = nullptr;  // static ulong ItemTooltip._globalValidatorKey
    const MethodInfo* textCtor = nullptr;   // LocalizedText(string key, string text)

    const MethodInfo* setDefaults3 = nullptr;  // SetDefaults(int, bool, ItemVariant): hook
    const MethodInfo* setDefaults1 = nullptr;  // SetDefaults(int): runtime_invoke
    const MethodInfo* rebuildTooltip = nullptr;
    const MethodInfo* prefix = nullptr;
};
ItemResolved r;

void* g_origSetDefaultsSlot = nullptr;
void* g_origRebuildTooltipSlot = nullptr;

// Mesma publicação dos callbacks de NPC: cópia imutável, antigas não liberadas.
std::atomic<const TLModItemCallbacks*> g_callbacks{nullptr};

template <typename Fn>
Fn LoadOrig(void* const& slot) {
    return reinterpret_cast<Fn>(__atomic_load_n(&slot, __ATOMIC_ACQUIRE));
}

// Objetos gerenciados criados aqui (nomes, tooltips) vivem o processo todo,
// presos por GC handle: um item pode guardá-los a qualquer momento.
std::mutex g_cacheMutex;
std::unordered_map<std::string, uint32_t> g_strings;
std::unordered_map<std::string, uint32_t> g_tooltips;

Il2CppObject* Pin(std::unordered_map<std::string, uint32_t>& cache, const std::string& key, Il2CppObject* obj) {
    if (!obj) return nullptr;
    cache[key] = api.il2cpp_gchandle_new(obj, false);
    return obj;
}

Il2CppObject* CachedStringLocked(const std::string& text) {
    auto it = g_strings.find(text);
    if (it != g_strings.end()) return api.il2cpp_gchandle_get_target(it->second);
    return Pin(g_strings, text, api.il2cpp_string_new(text.c_str()));
}

bool Invoke(const MethodInfo* m, void* obj, void** params, const char* what, Il2CppObject** result = nullptr) {
    if (!m) return false;
    Il2CppException* exc = nullptr;
    Il2CppObject* ret = api.il2cpp_runtime_invoke(m, obj, params, &exc);
    if (exc) {
        bridge::LogException(what, exc);
        return false;
    }
    if (result) *result = ret;
    return true;
}

// ItemTooltip cujo _text é um LocalizedText com o nosso texto. ValidateTooltip
// (chamado por Lines/GetLine) refaz as linhas a partir de _text.Value quando
// _validatorKey difere da chave global, inclusive depois de trocar o idioma.
Il2CppObject* CachedTooltipLocked(const std::string& text) {
    auto it = g_tooltips.find(text);
    if (it != g_tooltips.end()) return api.il2cpp_gchandle_get_target(it->second);
    if (!r.tooltipClass || !r.textClass || !r.textCtor) return nullptr;
    Il2CppObject* localized = api.il2cpp_object_new(r.textClass);
    void* params[] = {CachedStringLocked("TLMod.ModItemTooltip"), CachedStringLocked(text)};
    if (!localized || !Invoke(r.textCtor, localized, params, "LocalizedText..ctor")) return nullptr;
    Il2CppObject* tooltip = api.il2cpp_object_new(r.tooltipClass);
    if (!tooltip) return nullptr;
    SetRefField(tooltip, r.tooltipText, localized);
    uint64_t global = 0;
    if (r.tooltipGlobalKey) api.il2cpp_field_static_get_value(r.tooltipGlobalKey, &global);
    FieldAt<uint64_t>(tooltip, r.tooltipValidator) = ~global;  // força a primeira validação
    return Pin(g_tooltips, text, tooltip);
}

bool IsModNetId(int32_t netId) { return netId >= terraria::kItemVanillaCount; }

}  // namespace

// ===========================================================================
// Resolução e instalação (chamadas por TLMod_Init)
// ===========================================================================
namespace bridge {

bool ResolveItems() {
    const Il2CppImage* img = Image();
    Il2CppClass* item = api.il2cpp_class_from_name(img, terraria::kNamespace, terraria::kItemClass);
    if (!item) {
        LOGW("Terraria.Item não encontrada; itens de mod desativados");
        return false;
    }
    r.type = FieldOffset(item, "type", terraria::item_off::type);
    r.netID = FieldOffset(item, "netID", terraria::item_off::netID);
    r.width = FieldOffset(item, "width", terraria::item_off::width);
    r.height = FieldOffset(item, "height", terraria::item_off::height);
    r.damage = FieldOffset(item, "damage", terraria::item_off::damage);
    r.useTime = FieldOffset(item, "useTime", terraria::item_off::useTime);
    r.useAnimation = FieldOffset(item, "useAnimation", terraria::item_off::useAnimation);
    r.useStyle = FieldOffset(item, "useStyle", terraria::item_off::useStyle);
    r.melee = FieldOffset(item, "melee", terraria::item_off::melee);
    r.maxStack = FieldOffset(item, "maxStack", terraria::item_off::maxStack);
    r.value = FieldOffset(item, "value", terraria::item_off::value);
    r.rare = FieldOffset(item, "rare", terraria::item_off::rare);
    r.nameOverride = FieldOffset(item, "_nameOverride", terraria::item_off::_nameOverride);
    r.toolTip = FieldOffset(item, "ToolTip", terraria::item_off::ToolTip);

    r.setDefaults3 = api.il2cpp_class_get_method_from_name(item, "SetDefaults", terraria::argc::Item_SetDefaults);
    r.setDefaults1 = api.il2cpp_class_get_method_from_name(item, "SetDefaults", 1);
    r.rebuildTooltip =
        api.il2cpp_class_get_method_from_name(item, "RebuildTooltip", terraria::argc::Item_RebuildTooltip);
    r.prefix = api.il2cpp_class_get_method_from_name(item, "Prefix", terraria::argc::Item_Prefix);

    r.tooltipClass = api.il2cpp_class_from_name(img, "Terraria.UI", terraria::kItemTooltipClass);
    r.textClass = api.il2cpp_class_from_name(img, "Terraria.Localization", terraria::kLocalizedTextClass);
    if (r.tooltipClass) {
        r.tooltipValidator = FieldOffset(r.tooltipClass, "_validatorKey", terraria::item_tooltip_off::_validatorKey);
        r.tooltipText = FieldOffset(r.tooltipClass, "_text", terraria::item_tooltip_off::_text);
        r.tooltipNone = api.il2cpp_class_get_field_from_name(r.tooltipClass, "None");
        r.tooltipGlobalKey = api.il2cpp_class_get_field_from_name(r.tooltipClass, "_globalValidatorKey");
    }
    if (r.textClass) r.textCtor = api.il2cpp_class_get_method_from_name(r.textClass, ".ctor", 2);

    // Sem os dois hooks, um ID de mod chegaria às tabelas do jogo: nada de itens de mod.
    if (!r.setDefaults3 || !r.setDefaults1 || !r.rebuildTooltip) {
        LOGW("Item: faltou resolver SetDefaults(3)=%p SetDefaults(1)=%p RebuildTooltip=%p; itens de mod desativados",
             static_cast<const void*>(r.setDefaults3), static_cast<const void*>(r.setDefaults1),
             static_cast<const void*>(r.rebuildTooltip));
        return false;
    }
    if (!r.tooltipClass || !r.textClass || !r.textCtor || !r.tooltipNone)
        LOGW("Item: ItemTooltip/LocalizedText não resolveram; itens de mod ficam sem tooltip");
    if (!r.prefix) LOGW("Item.Prefix não encontrado; itens de mod reinseridos ficam sem prefixo");
    r.ready = true;
    return true;
}

void InstallItemHooks() {
    // RebuildTooltip primeiro: com o SetDefaults ativo, um netID de mod já pode existir.
    bool tooltip = InstallHook("Item.RebuildTooltip", r.rebuildTooltip, terraria::rva::Item_RebuildTooltip,
                               reinterpret_cast<void*>(&TLMod_Hooked_ItemRebuildTooltip), &g_origRebuildTooltipSlot);
    if (!tooltip) {
        LOGE("sem o hook de RebuildTooltip um netID de mod estoura Lang.GetTooltip; itens de mod desativados");
        r.ready = false;
        return;
    }
    if (!InstallHook("Item.SetDefaults", r.setDefaults3, terraria::rva::Item_SetDefaults,
                     reinterpret_cast<void*>(&TLMod_Hooked_ItemSetDefaults), &g_origSetDefaultsSlot))
        r.ready = false;
}

}  // namespace bridge

// ===========================================================================
// API pública
// ===========================================================================
extern "C" {

void TLMod_SetItemCallbacks(const TLModItemCallbacks* callbacks) {
    g_callbacks.store(callbacks ? new TLModItemCallbacks(*callbacks) : nullptr, std::memory_order_release);
}

bool TLMod_ReadItemStats(Il2CppObject* item, TLModItemStats* out) {
    if (!item || !out || !r.ready) return false;
    out->width = FieldAt<int32_t>(item, r.width);
    out->height = FieldAt<int32_t>(item, r.height);
    out->damage = FieldAt<int32_t>(item, r.damage);
    out->useTime = FieldAt<int32_t>(item, r.useTime);
    out->useAnimation = FieldAt<int32_t>(item, r.useAnimation);
    out->useStyle = FieldAt<uint8_t>(item, r.useStyle);
    out->melee = FieldAt<bool>(item, r.melee);
    out->maxStack = FieldAt<int32_t>(item, r.maxStack);
    out->value = FieldAt<int32_t>(item, r.value);
    out->rare = FieldAt<int32_t>(item, r.rare);
    return true;
}

bool TLMod_WriteItemStats(Il2CppObject* item, const TLModItemStats* in) {
    if (!item || !in || !r.ready) return false;
    FieldAt<int32_t>(item, r.width) = in->width;
    FieldAt<int32_t>(item, r.height) = in->height;
    FieldAt<int32_t>(item, r.damage) = in->damage;
    FieldAt<int32_t>(item, r.useTime) = in->useTime;
    FieldAt<int32_t>(item, r.useAnimation) = in->useAnimation;
    FieldAt<uint8_t>(item, r.useStyle) = in->useStyle;
    FieldAt<bool>(item, r.melee) = in->melee;
    FieldAt<int32_t>(item, r.maxStack) = in->maxStack;
    FieldAt<int32_t>(item, r.value) = in->value;
    FieldAt<int32_t>(item, r.rare) = in->rare;
    return true;
}

bool TLMod_SetItemName(Il2CppObject* item, const char* utf8) {
    if (!item || !r.ready) return false;
    Il2CppObject* name = nullptr;
    if (utf8) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        name = CachedStringLocked(utf8);
        if (!name) return false;
    }
    SetRefField(item, r.nameOverride, name);
    return true;
}

bool TLMod_SetItemTooltip(Il2CppObject* item, const char* utf8) {
    if (!item || !r.ready) return false;
    Il2CppObject* tooltip = nullptr;
    if (utf8 && *utf8) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        tooltip = CachedTooltipLocked(utf8);
    }
    // Nunca nulo: o jogo lê ToolTip.Lines sem conferir.
    if (!tooltip && r.tooltipNone) api.il2cpp_field_static_get_value(r.tooltipNone, &tooltip);
    if (!tooltip) return false;
    SetRefField(item, r.toolTip, tooltip);
    return true;
}

bool TLMod_SetItemDefaults(Il2CppObject* item, int32_t type) {
    if (!item || !r.ready) return false;
    void* params[] = {&type};
    return Invoke(r.setDefaults1, item, params, "Item.SetDefaults");
}

bool TLMod_ApplyItemPrefix(Il2CppObject* item, int32_t prefix) {
    if (!item || !r.ready) return false;
    if (prefix <= 0) return true;  // sem prefixo: nada a aplicar
    void* params[] = {&prefix};
    return Invoke(r.prefix, item, params, "Item.Prefix");
}

// ---------------------------------------------------------------------------
// Handlers dos hooks. Qualquer thread (load, save, jogo). Sem objetos com
// destrutor nos frames: exceções do jogo passam direto (ver save_hooks.cpp).
// ---------------------------------------------------------------------------

// Item.SetDefaults(int, bool, ItemVariant) (RVA 0xBAD034): todo SetDefaults,
// netDefaults e Refresh do jogo passa por aqui.
void TLMod_Hooked_ItemSetDefaults(Il2CppObject* self, int32_t type, bool noMatCheck, Il2CppObject* variant,
                                  const MethodInfo* method) {
    auto orig = LoadOrig<ItemSetDefaultsFn>(g_origSetDefaultsSlot);
    if (!orig) return;
    const TLModItemCallbacks* cb = g_callbacks.load(std::memory_order_acquire);
    int32_t shell = (cb && cb->remap_type && self) ? cb->remap_type(type) : type;
    orig(self, shell, noMatCheck, variant, method);  // o jogo só vê a casca
    if (shell == type || !self) return;
    FieldAt<int32_t>(self, r.netID) = type;  // a identidade do item de mod
    if (cb->on_defaults) cb->on_defaults(self, type, shell);
}

// Item.RebuildTooltip (RVA 0xBAAFE8): ToolTip = Lang.GetTooltip(netID). Com um
// netID de mod, _itemTooltipCache[netID] lançaria IndexOutOfRangeException.
void TLMod_Hooked_ItemRebuildTooltip(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<ItemRebuildTooltipFn>(g_origRebuildTooltipSlot);
    if (!orig) return;
    if (self && IsModNetId(FieldAt<int32_t>(self, r.netID))) {
        const TLModItemCallbacks* cb = g_callbacks.load(std::memory_order_acquire);
        if (cb && cb->on_rebuild_tooltip)
            cb->on_rebuild_tooltip(self, FieldAt<int32_t>(self, r.netID));
        else
            TLMod_SetItemTooltip(self, nullptr);
        return;
    }
    orig(self, method);
}

}  // extern "C"
