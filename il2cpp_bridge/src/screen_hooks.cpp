// Telas que leem TextureAssets.Item[item.type] por conta própria (fora dos hooks
// de draw_hooks.cpp) e o nome de NPC de mod. Mesmo padrão: achar o Item que a
// tela vai desenhar, trocar a textura da casca só durante a chamada original e
// devolver no fim. Ver "Texturas de mod" no modapi/README.md.
//
// Os frames dos hooks (TLMod_Hooked_*) só têm PODs: o que precisa de objetos
// C++ (cache de strings) fica em funções noinline fora deles.
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "bridge.h"
#include "bridge_internal.h"
#include "draw_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;
using drawx::ItemSwap;

typedef int32_t (*TransactionDrawFn)(Il2CppObject* layout, Il2CppObject* item, Il2CppObject* label, bool disabled,
                                     float* scale, bool forcedPressed, bool hasControllerFocus, bool forceOver,
                                     bool disablePressedState, const MethodInfo* method);
typedef void (*InstanceDrawFn)(Il2CppObject* self, const MethodInfo* method);
typedef int32_t (*CanBeResearchedFn)(Il2CppObject* self, Il2CppObject* item, int32_t* percent,
                                     const MethodInfo* method);
typedef void (*GridItemDrawFn)(Il2CppObject* self, Il2CppObject* layout, int32_t index, TLModXnaVector2 position,
                               float scale, const MethodInfo* method);
typedef void (*QuickActionDrawFn)(int32_t actionType, Il2CppObject* layout, bool pressed, float* scale,
                                  const MethodInfo* method);
typedef bool (*UniqueDrawFn)(Il2CppObject* self, bool justCheckingString, TLModXnaVector2* size,
                             Il2CppObject* spriteBatch, TLModXnaVector2 position, TLModXnaColor color, float scale,
                             const MethodInfo* method);
typedef Il2CppObject* (*NpcNameFn)(Il2CppObject* self, const MethodInfo* method);

namespace {

namespace off = terraria;

struct ScreenResolved {
    // GUIInstance.Active e as telas dele.
    FieldInfo* guiActive = nullptr;
    size_t guiReforge = off::gui_instance_off::GUIReforge;
    size_t guiCraftGuide = off::gui_instance_off::GUICraftGuide;
    const MethodInfo* getReforgeItem = nullptr;  // GUIReforge.get_ReforgeItem()
    const MethodInfo* getGuideItem = nullptr;    // GUICraftGuide.get_guideItem()
    const MethodInfo* getResearchItem = nullptr; // GUIResearch.GetResearchItem()
    // Receitas.
    FieldInfo* mainRecipe = nullptr;                 // static Recipe[] Main.recipe
    const MethodInfo* getAvailableRecipe = nullptr;  // static int[] Main.get_availableRecipe()
    const MethodInfo* getFocusRecipe = nullptr;      // static int Main.get_focusRecipe()
    size_t recipeCreateItem = off::recipe_off::createItem;
    size_t recipeRequiredItem = off::recipe_off::requiredItem;
    size_t craftHover = off::gui_crafting_off::CraftHover;
    size_t focusGuideRecipe = off::gui_craft_guide_popup_off::focusGuideRecipe;
    size_t availableGuideRecipe = off::gui_craft_guide_popup_off::availableGuideRecipe;
    // Jogador local.
    size_t selectedItem = off::player_off::selectedItem;
    const MethodInfo* quickGrapple = nullptr;  // Player.QuickGrapple_GetItemToUse()
    const MethodInfo* tryGetAmmo = nullptr;    // Main.TryGetAmmo(Item, out Item, out Color, out float, out Vector2)
    // Chat.
    size_t snippetItem = off::item_snippet_off::_item;
    // Nome de NPC.
    const MethodInfo* fromLiteral = nullptr;  // static NetworkText.FromLiteral(string)
};
ScreenResolved s;

void* g_origTransaction = nullptr;
void* g_origReforgePopup = nullptr;
void* g_origResearchItem = nullptr;
void* g_origCanBeResearched = nullptr;
void* g_origGuidePopup = nullptr;
void* g_origMaterialDraw = nullptr;
void* g_origMaterialTooltipDraw = nullptr;
void* g_origVirtualControls = nullptr;
void* g_origQuickAction = nullptr;
void* g_origInteractIcon = nullptr;
void* g_origUniqueDraw = nullptr;
void* g_origNpcTypeName = nullptr;
void* g_origNpcTypeNetName = nullptr;

constexpr int32_t kQuickGrapple = 4, kQuickGrappleFire = 22;  // QuickActionButton_Layout.QuickActionControl
constexpr int32_t kResearchInvalid = 0;                        // GUIResearch.ResearchState.Invalid

template <typename Fn>
Fn LoadOrig(void* const& slot) {
    return reinterpret_cast<Fn>(__atomic_load_n(&slot, __ATOMIC_ACQUIRE));
}

Il2CppObject* Call(const MethodInfo* m, void* obj, void** params, const char* what) {
    if (!m) return nullptr;
    Il2CppException* exc = nullptr;
    Il2CppObject* r = api.il2cpp_runtime_invoke(m, obj, params, &exc);
    if (exc) {
        bridge::LogException(what, exc);
        return nullptr;
    }
    return r;
}

Il2CppObject* ArrayItem(Il2CppArray* arr, int32_t i) {
    if (!arr || i < 0 || static_cast<uint32_t>(i) >= api.il2cpp_array_length(arr)) return nullptr;
    return ArrayData<Il2CppObject*>(arr)[i];
}

Il2CppObject* GuiScreen(size_t field) {
    Il2CppObject* active = nullptr;
    if (s.guiActive) api.il2cpp_field_static_get_value(s.guiActive, &active);
    return active ? FieldAt<Il2CppObject*>(active, field) : nullptr;
}

Il2CppObject* Recipe(int32_t index) {
    Il2CppArray* recipes = nullptr;
    if (s.mainRecipe) api.il2cpp_field_static_get_value(s.mainRecipe, &recipes);
    return ArrayItem(recipes, index);
}

// ---- Onde cada tela acha o item (mesma expressão do jogo, ver o README) ----------

// GUIReforgePopup.DrawItem: GUIInstance.Active.GUIReforge.get_ReforgeItem().
Il2CppObject* ReforgeItem() {
    Il2CppObject* reforge = GuiScreen(s.guiReforge);
    return reforge ? Call(s.getReforgeItem, reforge, nullptr, "GUIReforge.get_ReforgeItem") : nullptr;
}

// GUICraftGuidePopup.DrawItem: a receita em foco (createItem) ou o item da guia.
Il2CppObject* GuidePopupItem(Il2CppObject* popup) {
    int32_t focus = FieldAt<int32_t>(popup, s.focusGuideRecipe);
    if (focus >= 0) {
        Il2CppArray* available = FieldAt<Il2CppArray*>(popup, s.availableGuideRecipe);
        if (!available || static_cast<uint32_t>(focus) >= api.il2cpp_array_length(available)) return nullptr;
        Il2CppObject* recipe = Recipe(ArrayData<int32_t>(available)[focus]);
        return recipe ? FieldAt<Il2CppObject*>(recipe, s.recipeCreateItem) : nullptr;
    }
    Il2CppObject* guide = GuiScreen(s.guiCraftGuide);
    return guide ? Call(s.getGuideItem, guide, nullptr, "GUICraftGuide.get_guideItem") : nullptr;
}

// GUICrafting.MaterialItemDraw: Main.recipe[availableRecipe[focusRecipe]].requiredItem[index].
Il2CppObject* FocusMaterial(int32_t index) {
    auto* available = reinterpret_cast<Il2CppArray*>(Call(s.getAvailableRecipe, nullptr, nullptr, "Main.availableRecipe"));
    Il2CppObject* boxed = Call(s.getFocusRecipe, nullptr, nullptr, "Main.focusRecipe");
    if (!available || !boxed) return nullptr;
    int32_t focus = *static_cast<int32_t*>(api.il2cpp_object_unbox(boxed));
    if (focus < 0 || static_cast<uint32_t>(focus) >= api.il2cpp_array_length(available)) return nullptr;
    Il2CppObject* recipe = Recipe(ArrayData<int32_t>(available)[focus]);
    return recipe ? ArrayItem(FieldAt<Il2CppArray*>(recipe, s.recipeRequiredItem), index) : nullptr;
}

// GUICrafting.MaterialItemTooltipDraw: this.CraftHover.requiredItem[index].
Il2CppObject* HoverMaterial(Il2CppObject* crafting, int32_t index) {
    Il2CppObject* recipe = FieldAt<Il2CppObject*>(crafting, s.craftHover);
    return recipe ? ArrayItem(FieldAt<Il2CppArray*>(recipe, s.recipeRequiredItem), index) : nullptr;
}

Il2CppObject* SelectedItem() {
    Il2CppObject* player = TLMod_GetLocalPlayer();
    return player ? TLMod_ItemSlot(player, TLMOD_ITEMS_INVENTORY, 0, FieldAt<int32_t>(player, s.selectedItem)) : nullptr;
}

Il2CppObject* GrappleItem() {
    Il2CppObject* player = TLMod_GetLocalPlayer();
    return player ? Call(s.quickGrapple, player, nullptr, "Player.QuickGrapple_GetItemToUse") : nullptr;
}

// Main.TryGetAmmo(item, out ammo, ...): a munição que o ícone do cursor mostra.
Il2CppObject* AmmoFor(Il2CppObject* main, Il2CppObject* item) {
    if (!main || !item || !s.tryGetAmmo) return nullptr;
    Il2CppObject* ammo = nullptr;
    uint32_t color = 0;
    float scale = 0;
    TLModXnaVector2 offset{};
    void* params[] = {item, &ammo, &color, &scale, &offset};
    Il2CppObject* ok = Call(s.tryGetAmmo, main, params, "Main.TryGetAmmo");
    return ok && *static_cast<bool*>(api.il2cpp_object_unbox(ok)) ? ammo : nullptr;
}

// Duas trocas na mesma chamada (item na mão + munição, item + gancho). Se os
// dois tiverem o mesmo type, só o primeiro troca: a textura é uma por type.
struct TwoSwaps {
    ItemSwap first, second;
    bool firstOn, secondOn;
};
void BeginTwo(Il2CppObject* a, Il2CppObject* b, TwoSwaps* t) {
    t->firstOn = drawx::BeginItem(a, &t->first);
    t->secondOn = false;
    if (b && b != a && !(a && drawx::ItemType(a) == drawx::ItemType(b))) t->secondOn = drawx::BeginItem(b, &t->second);
}
void EndTwo(const TwoSwaps& t) {
    if (t.secondOn) drawx::EndItem(t.second);
    if (t.firstOn) drawx::EndItem(t.first);
}

// ---- Nome de NPC de mod --------------------------------------------------------------
// Strings gerenciadas criadas uma vez por texto, presas por GC handle.
__attribute__((noinline)) Il2CppObject* ModNpcName(Il2CppObject* npc) {
    static std::mutex mutex;
    static std::unordered_map<std::string, uint32_t> cache;
    const TLModDrawCallbacks* cb = drawx::Callbacks();
    if (!npc || !cb || !cb->npc_name) return nullptr;
    const char* name = cb->npc_name(npc, drawx::NpcIndex(npc));
    if (!name) return nullptr;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(name);
    if (it != cache.end()) return api.il2cpp_gchandle_get_target(it->second);
    Il2CppObject* str = api.il2cpp_string_new(name);
    if (!str) return nullptr;
    cache.emplace(name, api.il2cpp_gchandle_new(str, false));
    return str;
}

const MethodInfo* Method(const char* ns, const char* klass, const char* name, int argc) {
    Il2CppClass* k = api.il2cpp_class_from_name(bridge::Image(), ns, klass);
    return k ? api.il2cpp_class_get_method_from_name(k, name, argc) : nullptr;
}

// Overload pelo tipo do 1º parâmetro (dois Draw de 4 parâmetros em GUIQuickActionButton).
const MethodInfo* MethodWithParam0(Il2CppClass* k, const char* name, uint32_t argc, const char* param0Contains) {
    void* iter = nullptr;
    while (const MethodInfo* m = k ? api.il2cpp_class_get_methods(k, &iter) : nullptr) {
        if (std::strcmp(api.il2cpp_method_get_name(m), name) != 0 || api.il2cpp_method_get_param_count(m) != argc)
            continue;
        char* type = api.il2cpp_type_get_name(api.il2cpp_method_get_param(m, 0));
        bool match = type && std::strstr(type, param0Contains);
        api.il2cpp_free(type);
        if (match) return m;
    }
    return nullptr;
}

Il2CppClass* Nested(Il2CppClass* outer, const char* name) {
    void* iter = nullptr;
    while (Il2CppClass* k = outer ? api.il2cpp_class_get_nested_types(outer, &iter) : nullptr)
        if (std::strcmp(api.il2cpp_class_get_name(k), name) == 0) return k;
    return nullptr;
}

}  // namespace

// ===========================================================================
// Resolução e instalação (chamadas por TLMod_Init, depois de ResolveDraw)
// ===========================================================================
namespace bridge {

void InstallScreenHooks() {
    const Il2CppImage* img = Image();
    Il2CppClass* gui = api.il2cpp_class_from_name(img, "", off::kGuiInstanceClass);
    if (gui) {
        s.guiActive = api.il2cpp_class_get_field_from_name(gui, "Active");
        s.guiReforge = FieldOffset(gui, "GUIReforge", s.guiReforge);
        s.guiCraftGuide = FieldOffset(gui, "GUICraftGuide", s.guiCraftGuide);
    }
    s.getReforgeItem = Method("", "GUIReforge", "get_ReforgeItem", 0);
    s.getGuideItem = Method("", "GUICraftGuide", "get_guideItem", 0);
    s.getResearchItem = Method("", "GUIResearch", "GetResearchItem", 0);
    s.mainRecipe = api.il2cpp_class_get_field_from_name(MainClass(), "recipe");
    s.getAvailableRecipe = api.il2cpp_class_get_method_from_name(MainClass(), "get_availableRecipe", 0);
    s.getFocusRecipe = api.il2cpp_class_get_method_from_name(MainClass(), "get_focusRecipe", 0);
    s.tryGetAmmo = api.il2cpp_class_get_method_from_name(MainClass(), "TryGetAmmo", 5);
    if (Il2CppClass* recipe = api.il2cpp_class_from_name(img, off::kNamespace, "Recipe")) {
        s.recipeCreateItem = FieldOffset(recipe, "createItem", s.recipeCreateItem);
        s.recipeRequiredItem = FieldOffset(recipe, "requiredItem", s.recipeRequiredItem);
    }
    if (Il2CppClass* k = api.il2cpp_class_from_name(img, "", "GUICrafting"))
        s.craftHover = FieldOffset(k, "CraftHover", s.craftHover);
    if (Il2CppClass* k = api.il2cpp_class_from_name(img, "", "GUICraftGuidePopup")) {
        s.focusGuideRecipe = FieldOffset(k, "focusGuideRecipe", s.focusGuideRecipe);
        s.availableGuideRecipe = FieldOffset(k, "availableGuideRecipe", s.availableGuideRecipe);
    }
    s.selectedItem = FieldOffset(PlayerClass(), "selectedItem", s.selectedItem);
    s.quickGrapple = api.il2cpp_class_get_method_from_name(PlayerClass(), "QuickGrapple_GetItemToUse", 0);
    Il2CppClass* tagHandler = api.il2cpp_class_from_name(img, "Terraria.GameContent.UI.Chat", "ItemTagHandler");
    Il2CppClass* snippet = Nested(tagHandler, "ItemSnippet");
    if (snippet) s.snippetItem = FieldOffset(snippet, "_item", s.snippetItem);
    s.fromLiteral = Method("Terraria.Localization", "NetworkText", "FromLiteral", 1);

    namespace rva = off::rva;
    namespace argc = off::argc;
    struct Hook {
        const char* name;
        const MethodInfo* method;
        uintptr_t rva;
        void* handler;
        void** slot;
    };
    Il2CppClass* quick = api.il2cpp_class_from_name(img, "", "GUIQuickActionButton");
    const Hook hooks[] = {
        {"GUITransactionButton.Draw(Item)",
         Method("", "GUITransactionButton", "Draw", argc::GUITransactionButton_Draw), rva::GUITransactionButton_Draw,
         reinterpret_cast<void*>(&TLMod_Hooked_GUITransactionButtonDraw), &g_origTransaction},
        {"GUIReforgePopup.DrawItem", Method("", "GUIReforgePopup", "DrawItem", argc::GUIReforgePopup_DrawItem),
         rva::GUIReforgePopup_DrawItem, reinterpret_cast<void*>(&TLMod_Hooked_GUIReforgePopupDrawItem),
         &g_origReforgePopup},
        {"GUIResearch.DrawResearchItem",
         Method("", "GUIResearch", "DrawResearchItem", argc::GUIResearch_DrawResearchItem),
         rva::GUIResearch_DrawResearchItem, reinterpret_cast<void*>(&TLMod_Hooked_GUIResearchDrawResearchItem),
         &g_origResearchItem},
        {"GUIResearch.CanBeResearched",
         Method("", "GUIResearch", "CanBeResearched", argc::GUIResearch_CanBeResearched),
         rva::GUIResearch_CanBeResearched, reinterpret_cast<void*>(&TLMod_Hooked_GUIResearchCanBeResearched),
         &g_origCanBeResearched},
        {"GUICraftGuidePopup.DrawItem",
         Method("", "GUICraftGuidePopup", "DrawItem", argc::GUICraftGuidePopup_DrawItem),
         rva::GUICraftGuidePopup_DrawItem, reinterpret_cast<void*>(&TLMod_Hooked_GUICraftGuidePopupDrawItem),
         &g_origGuidePopup},
        {"GUICrafting.MaterialItemDraw",
         Method("", "GUICrafting", "MaterialItemDraw", argc::GUICrafting_MaterialItemDraw),
         rva::GUICrafting_MaterialItemDraw, reinterpret_cast<void*>(&TLMod_Hooked_GUICraftingMaterialItemDraw),
         &g_origMaterialDraw},
        {"GUICrafting.MaterialItemTooltipDraw",
         Method("", "GUICrafting", "MaterialItemTooltipDraw", argc::GUICrafting_MaterialItemTooltipDraw),
         rva::GUICrafting_MaterialItemTooltipDraw,
         reinterpret_cast<void*>(&TLMod_Hooked_GUICraftingMaterialItemTooltipDraw), &g_origMaterialTooltipDraw},
        {"GUIVirtualInputController.DrawControls",
         Method("", "GUIVirtualInputController", "DrawControls", argc::GUIVirtualInputController_DrawControls),
         rva::GUIVirtualInputController_DrawControls,
         reinterpret_cast<void*>(&TLMod_Hooked_GUIVirtualInputControllerDrawControls), &g_origVirtualControls},
        {"GUIQuickActionButton.Draw(QuickActionControl)",
         MethodWithParam0(quick, "Draw", argc::GUIQuickActionButton_Draw, "QuickActionControl"),
         rva::GUIQuickActionButton_Draw, reinterpret_cast<void*>(&TLMod_Hooked_GUIQuickActionButtonDraw),
         &g_origQuickAction},
        {"Main.DrawInterface_40_InteractItemIcon",
         api.il2cpp_class_get_method_from_name(MainClass(), "DrawInterface_40_InteractItemIcon",
                                               argc::Main_DrawInterface_40_InteractItemIcon),
         rva::Main_DrawInterface_40_InteractItemIcon,
         reinterpret_cast<void*>(&TLMod_Hooked_MainDrawInterfaceInteractItemIcon), &g_origInteractIcon},
        {"ItemTagHandler.ItemSnippet.UniqueDraw",
         snippet ? api.il2cpp_class_get_method_from_name(snippet, "UniqueDraw", argc::ItemSnippet_UniqueDraw) : nullptr,
         rva::ItemSnippet_UniqueDraw, reinterpret_cast<void*>(&TLMod_Hooked_ItemSnippetUniqueDraw), &g_origUniqueDraw},
        {"NPC.get_TypeName", Method(off::kNamespace, off::kNpcClass, "get_TypeName", argc::NPC_get_TypeName),
         rva::NPC_get_TypeName, reinterpret_cast<void*>(&TLMod_Hooked_NpcGetTypeName), &g_origNpcTypeName},
        {"NPC.GetTypeNetName", Method(off::kNamespace, off::kNpcClass, "GetTypeNetName", argc::NPC_GetTypeNetName),
         rva::NPC_GetTypeNetName, reinterpret_cast<void*>(&TLMod_Hooked_NpcGetTypeNetName), &g_origNpcTypeNetName},
    };
    for (const Hook& h : hooks) InstallHook(h.name, h.method, h.rva, h.handler, h.slot);
}

}  // namespace bridge

// ---------------------------------------------------------------------------
// Handlers (thread principal; o nome de NPC, qualquer thread). Só PODs.
// ---------------------------------------------------------------------------
extern "C" {

int32_t TLMod_Hooked_GUITransactionButtonDraw(Il2CppObject* layout, Il2CppObject* item, Il2CppObject* label,
                                              bool disabled, float* scale, bool forcedPressed,
                                              bool hasControllerFocus, bool forceOver, bool disablePressedState,
                                              const MethodInfo* method) {
    auto orig = LoadOrig<TransactionDrawFn>(g_origTransaction);
    if (!orig) return 0;
    ItemSwap swap{};
    bool swapped = drawx::BeginItem(item, &swap);
    int32_t r = orig(layout, item, label, disabled, scale, forcedPressed, hasControllerFocus, forceOver,
                     disablePressedState, method);
    if (swapped) drawx::EndItem(swap);
    return r;
}

void TLMod_Hooked_GUIReforgePopupDrawItem(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<InstanceDrawFn>(g_origReforgePopup);
    if (!orig) return;
    ItemSwap swap{};
    bool swapped = drawx::ItemTexturesActive() && drawx::BeginItem(ReforgeItem(), &swap);
    orig(self, method);
    if (swapped) drawx::EndItem(swap);
}

void TLMod_Hooked_GUIResearchDrawResearchItem(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<InstanceDrawFn>(g_origResearchItem);
    if (!orig) return;
    ItemSwap swap{};
    bool swapped = self && drawx::ItemTexturesActive() &&
                   drawx::BeginItem(Call(s.getResearchItem, self, nullptr, "GUIResearch.GetResearchItem"), &swap);
    orig(self, method);
    if (swapped) drawx::EndItem(swap);
}

// A pesquisa só olha o type: um item de mod seria consumido e desbloquearia a
// casca para duplicação. Bloqueado ("não pode ser pesquisado").
int32_t TLMod_Hooked_GUIResearchCanBeResearched(Il2CppObject* self, Il2CppObject* item, int32_t* percent,
                                                const MethodInfo* method) {
    auto orig = LoadOrig<CanBeResearchedFn>(g_origCanBeResearched);
    if (drawx::IsModItem(item) || !orig) {
        if (percent) *percent = 0;
        return kResearchInvalid;
    }
    return orig(self, item, percent, method);
}

void TLMod_Hooked_GUICraftGuidePopupDrawItem(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<InstanceDrawFn>(g_origGuidePopup);
    if (!orig) return;
    ItemSwap swap{};
    bool swapped = self && drawx::ItemTexturesActive() && drawx::BeginItem(GuidePopupItem(self), &swap);
    orig(self, method);
    if (swapped) drawx::EndItem(swap);
}

void TLMod_Hooked_GUICraftingMaterialItemDraw(Il2CppObject* self, Il2CppObject* layout, int32_t index,
                                              TLModXnaVector2 position, float scale, const MethodInfo* method) {
    auto orig = LoadOrig<GridItemDrawFn>(g_origMaterialDraw);
    if (!orig) return;
    ItemSwap swap{};
    bool swapped = drawx::ItemTexturesActive() && drawx::BeginItem(FocusMaterial(index), &swap);
    orig(self, layout, index, position, scale, method);
    if (swapped) drawx::EndItem(swap);
}

void TLMod_Hooked_GUICraftingMaterialItemTooltipDraw(Il2CppObject* self, Il2CppObject* layout, int32_t index,
                                                     TLModXnaVector2 position, float scale, const MethodInfo* method) {
    auto orig = LoadOrig<GridItemDrawFn>(g_origMaterialTooltipDraw);
    if (!orig) return;
    ItemSwap swap{};
    bool swapped = self && drawx::BeginItem(HoverMaterial(self, index), &swap);
    orig(self, layout, index, position, scale, method);
    if (swapped) drawx::EndItem(swap);
}

// Controles de toque: o botão de uso mostra o item na mão; o do gancho, o gancho.
void TLMod_Hooked_GUIVirtualInputControllerDrawControls(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<InstanceDrawFn>(g_origVirtualControls);
    if (!orig) return;
    TwoSwaps swaps{};
    if (drawx::ItemTexturesActive()) BeginTwo(SelectedItem(), GrappleItem(), &swaps);
    orig(self, method);
    EndTwo(swaps);
}

void TLMod_Hooked_GUIQuickActionButtonDraw(int32_t actionType, Il2CppObject* layout, bool pressed, float* scale,
                                           const MethodInfo* method) {
    auto orig = LoadOrig<QuickActionDrawFn>(g_origQuickAction);
    if (!orig) return;
    ItemSwap swap{};
    bool grapple = actionType == kQuickGrapple || actionType == kQuickGrappleFire;
    bool swapped = grapple && drawx::ItemTexturesActive() && drawx::BeginItem(GrappleItem(), &swap);
    orig(actionType, layout, pressed, scale, method);
    if (swapped) drawx::EndItem(swap);
}

// Ícone junto do cursor: o item na mão e a munição que ele usa.
void TLMod_Hooked_MainDrawInterfaceInteractItemIcon(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<InstanceDrawFn>(g_origInteractIcon);
    if (!orig) return;
    TwoSwaps swaps{};
    if (drawx::ItemTexturesActive()) {
        Il2CppObject* held = SelectedItem();
        BeginTwo(held, AmmoFor(self, held), &swaps);
    }
    orig(self, method);
    EndTwo(swaps);
}

bool TLMod_Hooked_ItemSnippetUniqueDraw(Il2CppObject* self, bool justCheckingString, TLModXnaVector2* size,
                                        Il2CppObject* spriteBatch, TLModXnaVector2 position, TLModXnaColor color,
                                        float scale, const MethodInfo* method) {
    auto orig = LoadOrig<UniqueDrawFn>(g_origUniqueDraw);
    if (!orig) return false;
    ItemSwap swap{};
    bool swapped = self && drawx::BeginItem(FieldAt<Il2CppObject*>(self, s.snippetItem), &swap);
    bool r = orig(self, justCheckingString, size, spriteBatch, position, color, scale, method);
    if (swapped) drawx::EndItem(swap);
    return r;
}

Il2CppObject* TLMod_Hooked_NpcGetTypeName(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<NpcNameFn>(g_origNpcTypeName);
    if (Il2CppObject* name = ModNpcName(self)) return name;
    return orig ? orig(self, method) : nullptr;
}

// Anúncios ("X despertou!", "X foi derrotado") e mensagens de morte: texto literal.
Il2CppObject* TLMod_Hooked_NpcGetTypeNetName(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<NpcNameFn>(g_origNpcTypeNetName);
    if (Il2CppObject* name = ModNpcName(self)) {
        void* params[] = {name};
        if (Il2CppObject* text = Call(s.fromLiteral, nullptr, params, "NetworkText.FromLiteral")) return text;
    }
    return orig ? orig(self, method) : nullptr;
}

}  // extern "C"
