// Aba "Mods" nas Configurações do jogo (ver bridge.h, TLModSettingsTab).
//
//   GUISettingsOverlay.DrawCategories    hook pós: uma 9ª aba, a cópia do layout da
//                                        última aba com a textura do item (DrawItem),
//                                        depois dela; o toque chama SetCategory(20)
//   GUISettingsOverlay.SetCategory(20)   hook pós: o jogo deixa a lista vazia (nenhuma
//                                        entrada é da categoria 20); entram as nossas:
//                                        GUIOpenUIButtonSetting (botão) e GUISliderSetting
//                                        (barra), com GUISettingID a partir de 0x7100
//   DrawTitle / ResetPageSettings        com a aba Mods aberta: o nosso título e o reset
//   entradas (GetOption, GetValue...)    com o nosso id: os textos e valores da fonte
//
// O jogo desenha e trata o toque das entradas como as suas (rolagem, som, região de
// toque): só os textos e o que o toque faz vêm daqui. O botão que pede para fechar
// marca o pedido, e o DrawCategories do mesmo frame (depois das entradas) chama Close.
//
// Na thread do jogo. O frame dos hooks só tem PODs; o resto fica em funções noinline.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bridge.h"
#include "bridge_internal.h"
#include "draw_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

typedef void (*VoidFn)(Il2CppObject* self, const MethodInfo* method);
typedef void (*SetCategoryFn)(Il2CppObject* self, int32_t category, const MethodInfo* method);
typedef Il2CppObject* (*StringFn)(Il2CppObject* self, const MethodInfo* method);
typedef float (*FloatFn)(Il2CppObject* self, const MethodInfo* method);
typedef int32_t (*IntFn)(Il2CppObject* self, const MethodInfo* method);
typedef void (*SetFloatFn)(Il2CppObject* self, float value, const MethodInfo* method);

namespace {

namespace off = terraria;
namespace so = terraria::settings_overlay_off;
namespace tbl = terraria::transaction_button_layout_off;

typedef int32_t (*TextureDrawFn)(Il2CppObject* layout, Il2CppObject* texture, Il2CppObject* label, bool disabled,
                                 float* scale, uint64_t overloadedItemColour, bool forcedPressed,
                                 bool hasControllerFocus, bool forceOver, bool disablePressedState,
                                 const MethodInfo* method);
typedef TLModXnaRectangle (*RegionFn)(Il2CppObject* layout, const MethodInfo* method);
typedef TLModXnaRectangle (*ControlRegionFn)(int32_t controlId, const MethodInfo* method);
typedef bool (*StringButtonFn)(Il2CppObject* layout, Il2CppObject* value, float* scale, bool forcePressed,
                               bool disabled, const MethodInfo* method);

constexpr int32_t kInputClicked = 0;  // GUITransactionButton.InputState.Clicked
constexpr int32_t kTabSound = 12;     // o som do toque nas abas
// Color? com valor: branco (Color: 4 bytes) e hasValue no 5º byte.
constexpr uint64_t kWhiteItem = 0xFFFFFFFFull | (1ull << 32);

struct Resolved {
    bool ready = false;
    FieldInfo* layoutInstance = nullptr;  // static SettingsOverlay_Layout.Instance
    size_t title = off::settings_overlay_layout_off::Title;
    size_t categories = off::settings_overlay_layout_off::Categories;
    size_t backing = off::settings_categories_off::Backing;
    size_t categorySettings = so::_categorySettings;
    size_t selected = so::Selected;
    size_t titleScale = so::titleScale;
    size_t timeSince = so::TimeSinceItemChange;
    size_t entryId = off::setting_entry_off::_id;
    size_t drawIcon = tbl::DrawIcon, drawItem = tbl::DrawItem, iconOffset = tbl::IconOffset;
    size_t disableItemResize = tbl::DisableItemResize, forceItemScale = tbl::ForceItemScale;
    size_t itemOffset = tbl::ItemOffset, itemColour = tbl::ItemColour;
    size_t disabledItemColour = tbl::DisabledItemColour, pressedItemColour = tbl::PressedItemColour;
    size_t layoutEnd = tbl::PressedBorderColour + 4;  // fim dos campos de TransactionButton_Layout
    size_t anchorControl = off::button_layout_off::AnchorControl;
    size_t anchor = off::button_layout_off::Anchor;
    size_t location = off::button_layout_off::Location;
    Il2CppClass* buttonClass = nullptr;  // GUIOpenUIButtonSetting
    Il2CppClass* sliderClass = nullptr;  // GUISliderSetting
    const MethodInfo* buttonCtor = nullptr;
    const MethodInfo* sliderCtor = nullptr;
    const MethodInfo* setCategory = nullptr;
    const MethodInfo* drawCategories = nullptr;
    const MethodInfo* drawTitle = nullptr;
    const MethodInfo* resetPage = nullptr;
    const MethodInfo* close = nullptr;
    const MethodInfo* getOption = nullptr;
    const MethodInfo* getCaption = nullptr;
    const MethodInfo* openUI = nullptr;
    const MethodInfo* sliderTitle = nullptr;
    const MethodInfo* sliderValue = nullptr;
    const MethodInfo* sliderMax = nullptr;
    const MethodInfo* sliderSet = nullptr;
    const MethodInfo* sliderString = nullptr;
    const MethodInfo* textureDraw = nullptr;   // GUITransactionButton.Draw(layout, Texture2D, ...)
    const MethodInfo* buttonRegion = nullptr;  // GUITransactionButton.GetRegion
    const MethodInfo* controlRegion = nullptr; // ControlAnchor.GetControlRegion (Screen = 0)
    const MethodInfo* panelRegion = nullptr;   // GUIPanel.Region
    const MethodInfo* drawButton = nullptr;    // GUIStringButton.DrawButton
    const MethodInfo* playSound = nullptr;
};
Resolved r;

// As abas do jogo, na ordem do DrawCategories.
struct GameTab {
    const char* name;
    size_t offset;
};
GameTab g_gameTabs[] = {
    {"General", off::settings_categories_off::General},
    {"Cursor", off::settings_categories_off::Cursor},
    {"GameplayControls", off::settings_categories_off::GameplayControls},
    {"KeyboardMouse", off::settings_categories_off::KeyboardMouse},
    {"Video", off::settings_categories_off::Video},
    {"Language", off::settings_categories_off::Language},
    {"Interface", off::settings_categories_off::Interface},
    {"Sound", off::settings_categories_off::Sound},
};

void* g_origSetCategory = nullptr;
void* g_origDrawCategories = nullptr;
void* g_origDrawTitle = nullptr;
void* g_origResetPage = nullptr;
void* g_origGetOption = nullptr;
void* g_origGetCaption = nullptr;
void* g_origOpenUI = nullptr;
void* g_origSliderTitle = nullptr;
void* g_origSliderValue = nullptr;
void* g_origSliderMax = nullptr;
void* g_origSliderSet = nullptr;
void* g_origSliderString = nullptr;

// Fonte publicada por TLMod_SetSettingsTab (qualquer thread): cópia imutável, as
// antigas não são liberadas (o desenho pode estar lendo).
struct TabSource {
    TLModSettingsTab tab;
    std::string title;
};
std::atomic<const TabSource*> g_source{nullptr};

// ---- Só na thread do jogo ------------------------------------------------------
// Uma linha: os textos já como strings do jogo (presas por GC handle; a API não tem
// gchandle_free: a antiga fica, e só muda quando o texto muda).
struct RowCache {
    int32_t kind = -1;
    std::string label, value;
    uint32_t labelHandle = 0, valueHandle = 0;
    int32_t steps = 0, step = 0;
    uint32_t entry = 0;  // a entrada do jogo (GUIOpenUIButtonSetting/GUISliderSetting)
};
std::vector<RowCache> g_rows;
uint32_t g_titleHandle = 0, g_emptyHandle = 0;
const TabSource* g_titleFor = nullptr;
const MethodInfo* g_listAdd = nullptr;  // List<GUISettingEntry>.Add
bool g_closeRequested = false;
float g_tabScale = 0.0f;
// A aba: cópia do layout da última aba do jogo, refeita quando a tela muda de tamanho.
uint32_t g_tabLayout = 0;
int32_t g_arrangedW = -1, g_arrangedH = -1;
bool g_tabBlocked = false;
bool g_loggedOpen = false;
// Posições originais das abas do jogo (a arrumação pode reespaçá-las).
struct TabPlace {
    Il2CppObject* layout;
    const char* name;
    float x, y;
};
std::vector<TabPlace> g_tabPlaces;
bool g_tabsSaved = false;

template <typename Fn>
Fn LoadOrig(void* const& slot) {
    return reinterpret_cast<Fn>(__atomic_load_n(&slot, __ATOMIC_ACQUIRE));
}
template <typename Fn>
Fn Ptr(const MethodInfo* m) {
    return reinterpret_cast<Fn>(m->methodPointer);
}
Il2CppObject* Target(uint32_t handle) { return handle ? api.il2cpp_gchandle_get_target(handle) : nullptr; }
Il2CppObject* StaticObject(FieldInfo* f) {
    Il2CppObject* obj = nullptr;
    if (f) api.il2cpp_field_static_get_value(f, &obj);
    return obj;
}
uint32_t PinString(const char* s) {
    Il2CppObject* str = api.il2cpp_string_new(s ? s : "");
    return str ? api.il2cpp_gchandle_new(str, false) : 0;
}
const MethodInfo* Method(Il2CppClass* k, const char* name, int argc) {
    return k ? api.il2cpp_class_get_method_from_name(k, name, argc) : nullptr;
}
// Overload pelo fim do nome do tipo de um parâmetro ("Texture2D").
const MethodInfo* MethodWithParamSuffix(Il2CppClass* k, const char* name, uint32_t argc, uint32_t index,
                                        const char* suffix) {
    void* iter = nullptr;
    const size_t n = std::strlen(suffix);
    while (const MethodInfo* m = k ? api.il2cpp_class_get_methods(k, &iter) : nullptr) {
        if (std::strcmp(api.il2cpp_method_get_name(m), name) != 0 || api.il2cpp_method_get_param_count(m) != argc)
            continue;
        char* type = api.il2cpp_type_get_name(api.il2cpp_method_get_param(m, index));
        const size_t len = type ? std::strlen(type) : 0;
        bool match = len >= n && std::strcmp(type + len - n, suffix) == 0;
        api.il2cpp_free(type);
        if (match) return m;
    }
    return nullptr;
}

const TabSource* Source() { return g_source.load(std::memory_order_acquire); }
bool IsMods(Il2CppObject* settings) {
    return settings && FieldAt<int32_t>(settings, r.selected) == off::kSettingsModsCategory;
}
Il2CppObject* Empty() {
    if (!g_emptyHandle) g_emptyHandle = PinString("");
    return Target(g_emptyHandle);
}

__attribute__((noinline)) void PlaySound(int32_t type) {
    if (!r.playSound) return;
    int32_t x = -1, y = -1, style = 1;
    float volume = 1.0f, pitch = 0.0f;
    void* params[] = {&type, &x, &y, &style, &volume, &pitch};
    Il2CppException* exc = nullptr;
    api.il2cpp_runtime_invoke(r.playSound, nullptr, params, &exc);
}

// ---- Linhas --------------------------------------------------------------------
// Índice da linha de uma entrada nossa (pelo GUISettingID), ou -1.
int32_t RowIndex(Il2CppObject* entry) {
    if (!entry) return -1;
    const int32_t i = FieldAt<int32_t>(entry, r.entryId) - off::kSettingsModsIdBase;
    return i >= 0 && i < TLMOD_SETTINGS_MAX_ROWS ? i : -1;
}

void SetText(std::string& text, uint32_t& handle, const char* now) {
    const char* s = now ? now : "";
    if (handle && text == s) return;
    text = s;
    handle = PinString(s);
}

// Relê a linha i da fonte. false = a fonte não tem essa linha.
__attribute__((noinline)) bool RefreshRow(int32_t i) {
    const TabSource* src = Source();
    if (!src || i < 0 || !src->tab.row) return false;
    if (static_cast<size_t>(i) >= g_rows.size()) g_rows.resize(static_cast<size_t>(i) + 1);
    TLModSettingRow row{};
    if (!src->tab.row(src->tab.user, i, &row)) return false;
    RowCache& c = g_rows[static_cast<size_t>(i)];
    c.kind = row.kind;
    SetText(c.label, c.labelHandle, row.label);
    SetText(c.value, c.valueHandle, row.value);
    c.steps = std::max(0, row.steps);
    c.step = std::clamp(row.step, 0, c.steps);
    return true;
}

// A linha de uma entrada nossa, relida, ou nulo.
RowCache* RowOf(Il2CppObject* entry) {
    const int32_t i = RowIndex(entry);
    return i >= 0 && RefreshRow(i) ? &g_rows[static_cast<size_t>(i)] : nullptr;
}

__attribute__((noinline)) Il2CppObject* NewEntry(int32_t kind, int32_t id) {
    const bool slider = kind == TLMOD_SETTING_SLIDER;
    Il2CppObject* obj = api.il2cpp_object_new(slider ? r.sliderClass : r.buttonClass);
    if (!obj) return nullptr;
    void* params[] = {&id};
    Il2CppException* exc = nullptr;
    api.il2cpp_runtime_invoke(slider ? r.sliderCtor : r.buttonCtor, obj, params, &exc);
    if (exc) {
        bridge::LogException(slider ? "GUISliderSetting..ctor" : "GUIOpenUIButtonSetting..ctor", exc);
        return nullptr;
    }
    return obj;
}

// Depois do SetCategory(20) original (a lista ficou vazia): as nossas entradas.
__attribute__((noinline)) void FillEntries(Il2CppObject* settings) {
    const TabSource* src = Source();
    Il2CppObject* list = FieldAt<Il2CppObject*>(settings, r.categorySettings);
    if (!src || !list || !src->tab.count) return;
    if (!g_listAdd) g_listAdd = Method(api.il2cpp_object_get_class(list), "Add", 1);
    if (!g_listAdd) {
        LOGW("Configurações: List<GUISettingEntry>.Add não encontrado; aba Mods vazia");
        return;
    }
    const int32_t count = std::clamp(src->tab.count(src->tab.user), 0, TLMOD_SETTINGS_MAX_ROWS);
    int32_t added = 0;
    for (int32_t i = 0; i < count; ++i) {
        if (!RefreshRow(i)) break;
        RowCache& c = g_rows[static_cast<size_t>(i)];
        Il2CppObject* entry = Target(c.entry);
        // A entrada é do tipo da linha: refeita se a fonte trocou o tipo.
        const bool slider = c.kind == TLMOD_SETTING_SLIDER;
        if (!entry || api.il2cpp_object_get_class(entry) != (slider ? r.sliderClass : r.buttonClass)) {
            entry = NewEntry(c.kind, off::kSettingsModsIdBase + i);
            c.entry = entry ? api.il2cpp_gchandle_new(entry, false) : 0;
        }
        if (!entry) continue;
        void* params[] = {entry};
        Il2CppException* exc = nullptr;
        api.il2cpp_runtime_invoke(g_listAdd, list, params, &exc);
        if (exc) {
            bridge::LogException("List<GUISettingEntry>.Add", exc);
            return;
        }
        ++added;
    }
    if (!g_loggedOpen) {
        g_loggedOpen = true;
        LOGI("Configurações: aba Mods aberta (%d linhas)", added);
    }
}

// ---- A aba ---------------------------------------------------------------------
TLModXnaRectangle Region(Il2CppObject* layout) { return Ptr<RegionFn>(r.buttonRegion)(layout, r.buttonRegion); }
float LocX(Il2CppObject* l) { return FieldAt<float>(l, r.location); }
float LocY(Il2CppObject* l) { return FieldAt<float>(l, r.location + 4); }
void Place(Il2CppObject* l, float x, float y) {
    FieldAt<float>(l, r.location) = x;
    FieldAt<float>(l, r.location + 4) = y;
}
bool SameAnchor(Il2CppObject* a, Il2CppObject* b) {
    return FieldAt<int32_t>(a, r.anchorControl) == FieldAt<int32_t>(b, r.anchorControl) &&
           FieldAt<int32_t>(a, r.anchor) == FieldAt<int32_t>(b, r.anchor);
}
bool Inside(const TLModXnaRectangle& q, const TLModXnaRectangle& box) {
    constexpr int32_t kSlack = 2;
    return q.x >= box.x - kSlack && q.y >= box.y - kSlack && q.x + q.width <= box.x + box.width + kSlack &&
           q.y + q.height <= box.y + box.height + kSlack;
}
TLModXnaRectangle Union(const TLModXnaRectangle& a, const TLModXnaRectangle& b) {
    if (a.width <= 0 || a.height <= 0) return b;
    const int32_t x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const int32_t x1 = std::max(a.x + a.width, b.x + b.width), y1 = std::max(a.y + a.height, b.y + b.height);
    return {x0, y0, x1 - x0, y1 - y0};
}
std::string Describe(const char* name, Il2CppObject* l) {
    const TLModXnaRectangle q = Region(l);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s (%.0f, %.0f) = %dx%d em (%d, %d)", name, LocX(l), LocY(l), q.width, q.height,
                  q.x, q.y);
    return buf;
}

// Cópia rasa de um TransactionButton_Layout (os sub-layouts e texturas são do jogo).
Il2CppObject* CloneLayout(Il2CppObject* source) {
    Il2CppObject* copy = api.il2cpp_object_new(api.il2cpp_object_get_class(source));
    if (!copy) return nullptr;
    constexpr size_t kHeader = 2 * sizeof(void*);  // klass e monitor: os campos começam em 0x10
    std::memcpy(reinterpret_cast<uint8_t*>(copy) + kHeader, reinterpret_cast<uint8_t*>(source) + kHeader,
                r.layoutEnd - kHeader);
    // O ícone é a textura do item (DrawItem), no lugar do da aba, com as cores do jogo.
    FieldAt<bool>(copy, r.drawIcon) = false;
    FieldAt<bool>(copy, r.drawItem) = false;  // ligado no desenho, quando a textura existe
    FieldAt<bool>(copy, r.forceItemScale) = false;
    FieldAt<bool>(copy, r.disableItemResize) = false;
    FieldAt<TLModXnaVector2>(copy, r.itemOffset) = FieldAt<TLModXnaVector2>(source, r.iconOffset);
    FieldAt<uint32_t>(copy, r.itemColour) = 0xFFFFFFFFu;
    FieldAt<uint32_t>(copy, r.pressedItemColour) = 0xFFFFFFFFu;
    FieldAt<uint32_t>(copy, r.disabledItemColour) = 0xFF8C8C8Cu;
    return copy;
}

// Põe a aba Mods depois da última aba do jogo, no mesmo passo entre elas, conferindo
// em pixels (GetRegion) que cabe na faixa das abas. Em ordem de preferência:
//   1. depois da última (à direita, ou embaixo se as abas forem uma coluna);
//   2. antes da primeira;
//   3. as 9 reespaçadas por igual na faixa, se couberem sem se sobrepor;
//   4. depois da última mesmo assim, se ficar dentro da tela.
// Refeito quando o tamanho da tela muda; o log registra a decisão.
__attribute__((noinline)) void Arrange(Il2CppObject* cats, const TLModXnaRectangle& screen) {
    g_arrangedW = screen.width;
    g_arrangedH = screen.height;
    g_tabBlocked = true;
    if (!g_tabsSaved) {
        g_tabsSaved = true;
        for (const GameTab& t : g_gameTabs)
            if (Il2CppObject* l = FieldAt<Il2CppObject*>(cats, t.offset)) g_tabPlaces.push_back({l, t.name, LocX(l), LocY(l)});
    }
    for (const TabPlace& p : g_tabPlaces) Place(p.layout, p.x, p.y);
    if (g_tabPlaces.size() < 2) {
        LOGW("Configurações: só %zu abas do jogo encontradas; sem aba Mods", g_tabPlaces.size());
        return;
    }
    struct Tab {
        const TabPlace* place;
        TLModXnaRectangle rect;
    };
    std::vector<Tab> tabs;
    TLModXnaRectangle bar{};
    for (const TabPlace& p : g_tabPlaces) {
        tabs.push_back({&p, Region(p.layout)});
        bar = Union(bar, tabs.back().rect);
    }
    Il2CppObject* backing = FieldAt<Il2CppObject*>(cats, r.backing);
    if (backing && r.panelRegion) bar = Union(bar, Ptr<RegionFn>(r.panelRegion)(backing, r.panelRegion));
    // Linha ou coluna: o eixo em que as abas mais se espalham.
    const bool horizontal = bar.width >= bar.height;
    auto along = [&](const TLModXnaRectangle& q) { return horizontal ? q.x : q.y; };
    std::sort(tabs.begin(), tabs.end(), [&](const Tab& a, const Tab& b) { return along(a.rect) < along(b.rect); });
    std::string list;
    for (const Tab& t : tabs) list += (list.empty() ? "" : "; ") + Describe(t.place->name, t.place->layout);
    LOGI("Configurações: tela %dx%d; faixa das abas %dx%d em (%d, %d); abas: %s", screen.width, screen.height,
         bar.width, bar.height, bar.x, bar.y, list.c_str());

    const Tab& first = tabs.front();
    const Tab& second = tabs[1];
    const Tab& prev = tabs[tabs.size() - 2];
    const Tab& last = tabs.back();
    // Uma cópia nova a cada arrumação (só quando a tela muda de tamanho; o handle antigo fica).
    Il2CppObject* clone = CloneLayout(last.place->layout);
    g_tabLayout = clone ? api.il2cpp_gchandle_new(clone, false) : 0;
    if (!clone) return;
    const TLModXnaRectangle bounds = bar;
    auto fits = [&](Il2CppObject* l) {
        const TLModXnaRectangle q = Region(l);
        return Inside(q, bounds) && Inside(q, screen);
    };
    // 1. Depois da última.
    if (SameAnchor(last.place->layout, prev.place->layout)) {
        Place(clone, 2.0f * last.place->x - prev.place->x, 2.0f * last.place->y - prev.place->y);
        if (fits(clone)) {
            g_tabBlocked = false;
            LOGI("Configurações: aba Mods depois de %s: %s", last.place->name, Describe("Mods", clone).c_str());
            return;
        }
    }
    // 2. Antes da primeira.
    if (SameAnchor(first.place->layout, second.place->layout) && SameAnchor(first.place->layout, last.place->layout)) {
        Place(clone, 2.0f * first.place->x - second.place->x, 2.0f * first.place->y - second.place->y);
        if (fits(clone)) {
            g_tabBlocked = false;
            LOGI("Configurações: aba Mods antes de %s: %s", first.place->name, Describe("Mods", clone).c_str());
            return;
        }
    }
    // 3. As 9 por igual na faixa (todas com a mesma âncora, para converter pixels em unidades).
    bool sameAnchor = true;
    for (const Tab& t : tabs) sameAnchor = sameAnchor && SameAnchor(t.place->layout, first.place->layout);
    const float units = horizontal ? last.place->x - first.place->x : last.place->y - first.place->y;
    const float pixels = static_cast<float>(along(last.rect) - along(first.rect));
    const float k = units != 0.0f ? pixels / units : 0.0f;  // pixels por unidade de layout
    const int n = static_cast<int>(tabs.size()) + 1;
    const float size = static_cast<float>(horizontal ? first.rect.width : first.rect.height);
    const float span = static_cast<float>(horizontal ? bounds.width : bounds.height);
    const float stepPx = n > 1 ? (span - size) / static_cast<float>(n - 1) : 0.0f;
    if (sameAnchor && k > 0.0f && stepPx >= size) {
        const float start = static_cast<float>(horizontal ? bounds.x : bounds.y);
        for (int i = 0; i < n; ++i) {
            const float delta = (start + stepPx * static_cast<float>(i) - static_cast<float>(along(first.rect))) / k;
            Il2CppObject* l = i + 1 < n ? tabs[static_cast<size_t>(i)].place->layout : clone;
            if (horizontal) Place(l, first.place->x + delta, last.place->y);
            else Place(l, last.place->x, first.place->y + delta);
        }
        g_tabBlocked = false;
        LOGI("Configurações: as abas não cabiam com mais uma; 9 abas reespaçadas na faixa: %s",
             Describe("Mods", clone).c_str());
        return;
    }
    // 4. Depois da última, mesmo fora da faixa.
    Place(clone, 2.0f * last.place->x - prev.place->x, 2.0f * last.place->y - prev.place->y);
    if (Inside(Region(clone), screen)) {
        g_tabBlocked = false;
        LOGW("Configurações: aba Mods fora da faixa das abas: %s", Describe("Mods", clone).c_str());
        return;
    }
    LOGW("Configurações: sem lugar para a aba Mods nesta tela (%dx%d)", screen.width, screen.height);
}

__attribute__((noinline)) void DrawModsTab(Il2CppObject* settings) {
    const TabSource* src = Source();
    if (!src) return;
    Il2CppObject* layout = StaticObject(r.layoutInstance);
    Il2CppObject* cats = layout ? FieldAt<Il2CppObject*>(layout, r.categories) : nullptr;
    if (!cats) return;
    const TLModXnaRectangle screen = Ptr<ControlRegionFn>(r.controlRegion)(0, r.controlRegion);
    if (screen.width > 0 && (screen.width != g_arrangedW || screen.height != g_arrangedH)) Arrange(cats, screen);
    Il2CppObject* tab = Target(g_tabLayout);
    if (g_tabBlocked || !tab) return;
    int32_t w = 0, h = 0;
    Il2CppObject* texture = src->tab.iconItem > 0 ? drawx::ItemIconTexture(src->tab.iconItem, &w, &h) : nullptr;
    FieldAt<bool>(tab, r.drawItem) = texture != nullptr;  // sem textura ainda: a aba sem ícone
    const bool selected = IsMods(settings);
    const int32_t state = Ptr<TextureDrawFn>(r.textureDraw)(tab, texture, Empty(), false, &g_tabScale, kWhiteItem,
                                                            selected, false, false, false, r.textureDraw);
    if (state == kInputClicked && !selected) {
        PlaySound(kTabSound);
        g_loggedOpen = false;
        // Pelo hook, como o jogo chamaria (o endereço original já desvia para ele no aparelho).
        TLMod_Hooked_SettingsSetCategory(settings, off::kSettingsModsCategory, r.setCategory);
        FieldAt<float>(settings, r.timeSince) = 0.0f;
    }
}

__attribute__((noinline)) bool DrawModsTitle(Il2CppObject* settings) {
    const TabSource* src = Source();
    Il2CppObject* layout = StaticObject(r.layoutInstance);
    Il2CppObject* title = layout ? FieldAt<Il2CppObject*>(layout, r.title) : nullptr;
    if (!src || !title) return false;
    if (src != g_titleFor) {
        g_titleHandle = PinString(src->title.c_str());
        g_titleFor = src;
    }
    Il2CppObject* text = Target(g_titleHandle);
    if (!text) return false;
    Ptr<StringButtonFn>(r.drawButton)(title, text, &FieldAt<float>(settings, r.titleScale), false, false,
                                      r.drawButton);
    return true;
}

__attribute__((noinline)) bool ClickRow(Il2CppObject* entry) {
    const TabSource* src = Source();
    const int32_t i = RowIndex(entry);
    if (!src || i < 0) return false;
    if (src->tab.click && src->tab.click(src->tab.user, i)) g_closeRequested = true;
    return true;
}

__attribute__((noinline)) void SetRowStep(RowCache* row, int32_t index, float value) {
    const TabSource* src = Source();
    if (!src || !src->tab.setStep || row->steps <= 0) return;
    const int32_t step = std::clamp(static_cast<int32_t>(std::lround(value * static_cast<float>(row->steps))), 0,
                                    row->steps);
    if (step == row->step) return;
    src->tab.setStep(src->tab.user, index, step);
    RefreshRow(index);
}

}  // namespace

namespace bridge {

void InstallSettingsHooks() {
    const Il2CppImage* img = Image();
    namespace argc = off::argc;
    Il2CppClass* overlay = api.il2cpp_class_from_name(img, "", off::kSettingsOverlayClass);
    Il2CppClass* layout = api.il2cpp_class_from_name(img, "", off::kSettingsOverlayLayoutClass);
    Il2CppClass* cats = api.il2cpp_class_from_name(img, "", "SettingsOverlayCategories_Layout");
    Il2CppClass* entry = api.il2cpp_class_from_name(img, "", "GUISettingEntry");
    Il2CppClass* tbLayout = api.il2cpp_class_from_name(img, "", "TransactionButton_Layout");
    r.buttonClass = api.il2cpp_class_from_name(img, "", "GUIOpenUIButtonSetting");
    r.sliderClass = api.il2cpp_class_from_name(img, "", "GUISliderSetting");
    if (layout) {
        r.layoutInstance = api.il2cpp_class_get_field_from_name(layout, "Instance");
        r.title = FieldOffset(layout, "Title", r.title);
        r.categories = FieldOffset(layout, "Categories", r.categories);
    }
    if (cats) {
        r.backing = FieldOffset(cats, "Backing", r.backing);
        for (GameTab& t : g_gameTabs) t.offset = FieldOffset(cats, t.name, t.offset);
    }
    if (overlay) {
        r.categorySettings = FieldOffset(overlay, "_categorySettings", r.categorySettings);
        r.selected = FieldOffset(overlay, "Selected", r.selected);
        r.titleScale = FieldOffset(overlay, "titleScale", r.titleScale);
        r.timeSince = FieldOffset(overlay, "TimeSinceItemChange", r.timeSince);
    }
    if (entry) r.entryId = FieldOffset(entry, "_id", r.entryId);
    if (tbLayout) {
        r.drawIcon = FieldOffset(tbLayout, "DrawIcon", r.drawIcon);
        r.drawItem = FieldOffset(tbLayout, "DrawItem", r.drawItem);
        r.iconOffset = FieldOffset(tbLayout, "IconOffset", r.iconOffset);
        r.disableItemResize = FieldOffset(tbLayout, "DisableItemResize", r.disableItemResize);
        r.forceItemScale = FieldOffset(tbLayout, "ForceItemScale", r.forceItemScale);
        r.itemOffset = FieldOffset(tbLayout, "ItemOffset", r.itemOffset);
        r.itemColour = FieldOffset(tbLayout, "ItemColour", r.itemColour);
        r.disabledItemColour = FieldOffset(tbLayout, "DisabledItemColour", r.disabledItemColour);
        r.pressedItemColour = FieldOffset(tbLayout, "PressedItemColour", r.pressedItemColour);
        r.layoutEnd = FieldOffset(tbLayout, "PressedBorderColour", r.layoutEnd - 4) + 4;
    }
    if (Il2CppClass* button = api.il2cpp_class_from_name(img, "", "Button_Layout")) {
        r.anchorControl = FieldOffset(button, "AnchorControl", r.anchorControl);
        r.anchor = FieldOffset(button, "Anchor", r.anchor);
        r.location = FieldOffset(button, "Location", r.location);
    }
    r.setCategory = Method(overlay, "SetCategory", argc::GUISettingsOverlay_SetCategory);
    r.drawCategories = Method(overlay, "DrawCategories", argc::GUISettingsOverlay_DrawCategories);
    r.drawTitle = Method(overlay, "DrawTitle", argc::GUISettingsOverlay_DrawTitle);
    r.resetPage = Method(overlay, "ResetPageSettings", argc::GUISettingsOverlay_ResetPageSettings);
    r.close = Method(overlay, "Close", argc::GUISettingsOverlay_Close);
    r.buttonCtor = Method(r.buttonClass, ".ctor", argc::GUISettingEntry_ctor);
    r.sliderCtor = Method(r.sliderClass, ".ctor", argc::GUISettingEntry_ctor);
    r.getOption = Method(r.buttonClass, "GetOption", argc::GUIOpenUIButtonSetting_GetOption);
    r.getCaption = Method(r.buttonClass, "GetButtonCaption", argc::GUIOpenUIButtonSetting_GetButtonCaption);
    r.openUI = Method(r.buttonClass, "OpenUI", argc::GUIOpenUIButtonSetting_OpenUI);
    r.sliderTitle = Method(r.sliderClass, "GetTitle", argc::GUISliderSetting_GetTitle);
    r.sliderValue = Method(r.sliderClass, "GetValue", argc::GUISliderSetting_GetValue);
    r.sliderMax = Method(r.sliderClass, "GetMaxValue", argc::GUISliderSetting_GetMaxValue);
    r.sliderSet = Method(r.sliderClass, "SetValue", argc::GUISliderSetting_SetValue);
    r.sliderString = Method(r.sliderClass, "GetStringValue", argc::GUISliderSetting_GetStringValue);
    Il2CppClass* transaction = api.il2cpp_class_from_name(img, "", "GUITransactionButton");
    r.textureDraw = MethodWithParamSuffix(transaction, "Draw", argc::GUITransactionButton_DrawTexture, 1, "Texture2D");
    r.buttonRegion = Method(transaction, "GetRegion", argc::GUITransactionButton_GetRegion);
    r.controlRegion = Method(api.il2cpp_class_from_name(img, "", "ControlAnchor"), "GetControlRegion",
                             argc::ControlAnchor_GetControlRegion);
    r.panelRegion = Method(api.il2cpp_class_from_name(img, "", "GUIPanel"), "Region", argc::GUIPanel_Region);
    r.drawButton = Method(api.il2cpp_class_from_name(img, "", "GUIStringButton"), "DrawButton",
                          argc::GUIStringButton_DrawButton);
    r.playSound = Method(api.il2cpp_class_from_name(img, "Terraria.Audio", "SoundEngine"), "PlaySound",
                         argc::SoundEngine_PlaySound);

    const void* needed[] = {r.layoutInstance, r.buttonClass,  r.sliderClass,   r.buttonCtor,    r.sliderCtor,
                            r.setCategory,    r.drawCategories, r.drawTitle,   r.resetPage,     r.close,
                            r.getOption,      r.getCaption,   r.openUI,        r.sliderTitle,   r.sliderValue,
                            r.sliderMax,      r.sliderSet,    r.sliderString,  r.textureDraw,   r.buttonRegion,
                            r.controlRegion,  r.drawButton};
    int missing = 0;
    for (const void* p : needed) missing += p == nullptr;
    if (missing) {
        LOGW("Configurações: %d peça(s) da tela não resolveram; sem aba Mods", missing);
        return;
    }
    if (!r.panelRegion) LOGW("Configurações: GUIPanel.Region ausente; a aba Mods confere só as outras abas");
    struct Hook {
        const char* name;
        const MethodInfo* method;
        uintptr_t rva;
        void* fn;
        void** slot;
    };
    namespace rva = off::rva;
    const Hook hooks[] = {
        {"GUIOpenUIButtonSetting.GetOption", r.getOption, rva::GUIOpenUIButtonSetting_GetOption,
         reinterpret_cast<void*>(&TLMod_Hooked_OpenUIButtonGetOption), &g_origGetOption},
        {"GUIOpenUIButtonSetting.GetButtonCaption", r.getCaption, rva::GUIOpenUIButtonSetting_GetButtonCaption,
         reinterpret_cast<void*>(&TLMod_Hooked_OpenUIButtonGetButtonCaption), &g_origGetCaption},
        {"GUIOpenUIButtonSetting.OpenUI", r.openUI, rva::GUIOpenUIButtonSetting_OpenUI,
         reinterpret_cast<void*>(&TLMod_Hooked_OpenUIButtonOpenUI), &g_origOpenUI},
        {"GUISliderSetting.GetTitle", r.sliderTitle, rva::GUISliderSetting_GetTitle,
         reinterpret_cast<void*>(&TLMod_Hooked_SliderGetTitle), &g_origSliderTitle},
        {"GUISliderSetting.GetValue", r.sliderValue, rva::GUISliderSetting_GetValue,
         reinterpret_cast<void*>(&TLMod_Hooked_SliderGetValue), &g_origSliderValue},
        {"GUISliderSetting.GetMaxValue", r.sliderMax, rva::GUISliderSetting_GetMaxValue,
         reinterpret_cast<void*>(&TLMod_Hooked_SliderGetMaxValue), &g_origSliderMax},
        {"GUISliderSetting.SetValue", r.sliderSet, rva::GUISliderSetting_SetValue,
         reinterpret_cast<void*>(&TLMod_Hooked_SliderSetValue), &g_origSliderSet},
        {"GUISliderSetting.GetStringValue", r.sliderString, rva::GUISliderSetting_GetStringValue,
         reinterpret_cast<void*>(&TLMod_Hooked_SliderGetStringValue), &g_origSliderString},
        {"GUISettingsOverlay.SetCategory", r.setCategory, rva::GUISettingsOverlay_SetCategory,
         reinterpret_cast<void*>(&TLMod_Hooked_SettingsSetCategory), &g_origSetCategory},
        {"GUISettingsOverlay.DrawTitle", r.drawTitle, rva::GUISettingsOverlay_DrawTitle,
         reinterpret_cast<void*>(&TLMod_Hooked_SettingsDrawTitle), &g_origDrawTitle},
        {"GUISettingsOverlay.ResetPageSettings", r.resetPage, rva::GUISettingsOverlay_ResetPageSettings,
         reinterpret_cast<void*>(&TLMod_Hooked_SettingsResetPageSettings), &g_origResetPage},
        // Por último: sem ele a aba não aparece, e nada dos de cima chega a ser usado.
        {"GUISettingsOverlay.DrawCategories", r.drawCategories, rva::GUISettingsOverlay_DrawCategories,
         reinterpret_cast<void*>(&TLMod_Hooked_SettingsDrawCategories), &g_origDrawCategories},
    };
    for (const Hook& h : hooks)
        if (!InstallHook(h.name, h.method, h.rva, h.fn, h.slot)) {
            LOGW("Configurações: hook de %s faltou; sem aba Mods", h.name);
            return;
        }
    r.ready = true;
    LOGI("Configurações: aba Mods pronta");
}

}  // namespace bridge

extern "C" {

void TLMod_SetSettingsTab(const TLModSettingsTab* tab) {
    const TabSource* next = nullptr;
    if (tab && tab->count && tab->row) next = new TabSource{*tab, tab->title ? tab->title : "Mods"};
    g_source.store(next, std::memory_order_release);
}

bool TLMod_SettingsTabAvailable(void) { return r.ready && g_source.load(std::memory_order_acquire); }

// GUISettingsOverlay.SetCategory (RVA 0xA67B94): a original refaz a lista; com a
// categoria da aba Mods (que nenhuma entrada do jogo tem), entram as nossas.
void TLMod_Hooked_SettingsSetCategory(Il2CppObject* self, int32_t category, const MethodInfo* method) {
    auto orig = LoadOrig<SetCategoryFn>(g_origSetCategory);
    if (orig) orig(self, category, method);
    if (category == off::kSettingsModsCategory && self) FillEntries(self);
}

// GUISettingsOverlay.DrawCategories (RVA 0xA68C68): as abas do jogo e depois a nossa.
// Roda depois de DrawCategoryOptions (as entradas): o pedido de fechar de um botão
// nosso, feito no mesmo frame, é atendido aqui.
void TLMod_Hooked_SettingsDrawCategories(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<VoidFn>(g_origDrawCategories);
    if (orig) orig(self, method);
    if (!self) return;
    DrawModsTab(self);
    if (g_closeRequested) {
        g_closeRequested = false;
        Ptr<VoidFn>(r.close)(self, r.close);
    }
}

// GUISettingsOverlay.DrawTitle (RVA 0xA699E4): o título da aba Mods.
void TLMod_Hooked_SettingsDrawTitle(Il2CppObject* self, const MethodInfo* method) {
    if (IsMods(self) && DrawModsTitle(self)) return;
    auto orig = LoadOrig<VoidFn>(g_origDrawTitle);
    if (orig) orig(self, method);
}

// GUISettingsOverlay.ResetPageSettings (RVA 0xA6B3C8): "Padrão" com a aba Mods aberta.
void TLMod_Hooked_SettingsResetPageSettings(Il2CppObject* self, const MethodInfo* method) {
    const TabSource* src = Source();
    if (IsMods(self)) {
        if (src && src->tab.reset) src->tab.reset(src->tab.user);
        return;
    }
    auto orig = LoadOrig<VoidFn>(g_origResetPage);
    if (orig) orig(self, method);
}

// GUIOpenUIButtonSetting (RVAs 0xA8BF14, 0xA8C2B4, 0xA8C434): o texto da linha, o do
// botão e o toque.
Il2CppObject* TLMod_Hooked_OpenUIButtonGetOption(Il2CppObject* self, const MethodInfo* method) {
    if (RowIndex(self) >= 0) {
        RowCache* row = RowOf(self);
        return row ? Target(row->labelHandle) : Empty();
    }
    auto orig = LoadOrig<StringFn>(g_origGetOption);
    return orig ? orig(self, method) : nullptr;
}

Il2CppObject* TLMod_Hooked_OpenUIButtonGetButtonCaption(Il2CppObject* self, const MethodInfo* method) {
    if (RowIndex(self) >= 0) {
        RowCache* row = RowOf(self);
        return row ? Target(row->valueHandle) : Empty();
    }
    auto orig = LoadOrig<StringFn>(g_origGetCaption);
    return orig ? orig(self, method) : nullptr;
}

void TLMod_Hooked_OpenUIButtonOpenUI(Il2CppObject* self, const MethodInfo* method) {
    if (ClickRow(self)) return;
    auto orig = LoadOrig<VoidFn>(g_origOpenUI);
    if (orig) orig(self, method);
}

// GUISliderSetting (RVAs 0xA93E18, 0xA9395C, 0xA94220, 0xA942E8, 0xA948E4): a barra
// trabalha em 0..1 com GetMaxValue passos (GUISlider.QuantiseValue).
Il2CppObject* TLMod_Hooked_SliderGetTitle(Il2CppObject* self, const MethodInfo* method) {
    if (RowIndex(self) >= 0) {
        RowCache* row = RowOf(self);
        return row ? Target(row->labelHandle) : Empty();
    }
    auto orig = LoadOrig<StringFn>(g_origSliderTitle);
    return orig ? orig(self, method) : nullptr;
}

float TLMod_Hooked_SliderGetValue(Il2CppObject* self, const MethodInfo* method) {
    if (RowIndex(self) >= 0) {
        RowCache* row = RowOf(self);
        return row && row->steps > 0 ? static_cast<float>(row->step) / static_cast<float>(row->steps) : 0.0f;
    }
    auto orig = LoadOrig<FloatFn>(g_origSliderValue);
    return orig ? orig(self, method) : 0.0f;
}

int32_t TLMod_Hooked_SliderGetMaxValue(Il2CppObject* self, const MethodInfo* method) {
    if (RowIndex(self) >= 0) {
        RowCache* row = RowOf(self);
        return row ? row->steps : 0;
    }
    auto orig = LoadOrig<IntFn>(g_origSliderMax);
    return orig ? orig(self, method) : 0;
}

void TLMod_Hooked_SliderSetValue(Il2CppObject* self, float value, const MethodInfo* method) {
    const int32_t i = RowIndex(self);
    if (i >= 0) {
        if (RowCache* row = RowOf(self)) SetRowStep(row, i, value);
        return;
    }
    auto orig = LoadOrig<SetFloatFn>(g_origSliderSet);
    if (orig) orig(self, value, method);
}

Il2CppObject* TLMod_Hooked_SliderGetStringValue(Il2CppObject* self, const MethodInfo* method) {
    if (RowIndex(self) >= 0) {
        RowCache* row = RowOf(self);
        return row ? Target(row->valueHandle) : Empty();
    }
    auto orig = LoadOrig<StringFn>(g_origSliderString);
    return orig ? orig(self, method) : nullptr;
}

}  // extern "C"
