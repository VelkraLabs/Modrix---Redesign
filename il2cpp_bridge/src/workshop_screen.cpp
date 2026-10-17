// Oficina de Mods nativa: uma tela no lugar do menu principal, desenhada com
// as peças da tela de seleção de mundos (WorldSelectMenu_Layout), como o
// tModLoader faz no PC com a interface do próprio jogo.
//
//   Lista:    fundo, divisória, título, a lista que rola (GUIDraggableItemGrid,
//             com arraste e momento do jogo) e Voltar. Cada linha usa o fundo de
//             linha de mundo e os textos de nome, tempo, modo e tamanho.
//   Detalhes: tocar numa linha abre o texto completo do mod (descrição, autores,
//             erro inteiro), com Voltar e o botão "Novo" virado em Ativar/Desativar.
//
// A lista do jogo só aceita delegates gerenciados. Os três vêm de métodos que já
// existem: GUIWorldSelectMenu.WorldScale (devolve 1), GUIWorldSelectMenu.WorldOver
// (grava worldOver numa instância nossa) e GUIItemGrid.DummyDraw (vazio, sem
// chamadas diretas), que ganha um hook e desenha a linha enquanto a Oficina
// desenha a lista. As funções do jogo são chamadas direto, pelo methodPointer,
// com as assinaturas conferidas na desmontagem (Nullable<Color> vai num
// registrador; Nullable<Matrix>, por ser maior que 16 bytes, por referência).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

namespace {

namespace off = terraria;
namespace ws = terraria::world_select_layout_off;

struct NullableMatrix {  // Nullable<Matrix>: 16 floats + hasValue
    float m[16];
    bool hasValue;
};

typedef void (*PanelDrawFn)(Il2CppObject* layout, bool over, uint64_t backing, uint64_t border, uint64_t highlight,
                            const MethodInfo*);
typedef void (*PanelDrawAtFn)(Il2CppObject* layout, TLModXnaVector2 position, TLModXnaVector2 size, bool over,
                              const MethodInfo*);
typedef TLModXnaRectangle (*PanelRegionFn)(Il2CppObject* layout, const MethodInfo*);
typedef void (*StringDrawFn)(Il2CppObject* layout, Il2CppObject* value, const MethodInfo*);
typedef bool (*StringButtonFn)(Il2CppObject* layout, Il2CppObject* value, float* scale, bool forcedPressed,
                               bool disabled, const MethodInfo*);
typedef void (*SetGridRegionFn)(TLModXnaRectangle region, const MethodInfo*);
typedef void (*GridDrawFn)(Il2CppObject* layout, Il2CppObject* scale, Il2CppObject* over, Il2CppObject* draw,
                           int32_t count, float* scroll, float* momentum, int32_t* dragging,
                           TLModXnaVector2* dragPosition, TLModXnaVector2 itemSize, bool drawMissing,
                           bool mouseScroll, bool disableDrag, const MethodInfo*);
typedef int32_t (*TransactionDrawFn)(Il2CppObject* layout, Il2CppObject* item, Il2CppObject* label, bool disabled,
                                     float* scale, bool forcedPressed, bool hasControllerFocus, bool forceOver,
                                     bool disablePressedState, const MethodInfo* method);
typedef void (*BeginFn)(Il2CppObject* batch, int32_t sort, Il2CppObject* blend, Il2CppObject* sampler,
                        Il2CppObject* depth, Il2CppObject* raster, Il2CppObject* effect, NullableMatrix matrix,
                        bool deferred, const MethodInfo*);
typedef void (*InstanceFn)(Il2CppObject* self, const MethodInfo*);
typedef void (*CopyFn)(Il2CppObject* self, Il2CppObject* other, const MethodInfo*);
typedef void (*DelegateCtorFn)(Il2CppObject* self, Il2CppObject* target, const MethodInfo* method, const MethodInfo*);
typedef bool (*KeyUpFn)(int32_t key, const MethodInfo*);
typedef int32_t (*StaticIntFn)(const MethodInfo*);
typedef bool (*StaticBoolFn)(const MethodInfo*);
typedef void (*DummyDrawFn)(Il2CppObject* layout, int32_t index, TLModXnaVector2 position, float scale,
                            const MethodInfo* method);

constexpr int32_t kInputClicked = 0;     // GUITransactionButton.InputState.Clicked
constexpr int32_t kKeyEscape = 27;       // KeyCode.Escape: o voltar do Android
constexpr int32_t kControlGridItem = 2;  // ControlAnchor.ControlId.GridItem
constexpr int32_t kAnchorTopLeft = 9;    // LayoutCalculator.AnchorType.TopLeft
constexpr float kTapDistance = 30.0f;    // como o SINGLE_CLICK_DISTANCE da seleção de mundos
// Cores do estado (Color: R, G, B, A em bytes).
constexpr uint32_t Rgba(uint32_t r, uint32_t g, uint32_t b) { return r | (g << 8) | (b << 16) | (0xFFu << 24); }
constexpr uint32_t kStatusColors[] = {Rgba(130, 225, 120), Rgba(170, 170, 170), Rgba(255, 110, 110),
                                      Rgba(255, 210, 90)};

struct Resolved {
    bool ready = false;
    FieldInfo* layoutInstance = nullptr;  // static WorldSelectMenu_Layout.Instance
    FieldInfo* spriteBatch = nullptr;     // static Main.spriteBatch
    FieldInfo* dummyDraw = nullptr;       // static GUIItemGrid.dummyDraw
    FieldInfo* gridRegion = nullptr;      // static GUIDraggableItemGrid.fullRegion
    Il2CppClass* worldSelectClass = nullptr;
    Il2CppClass* scaleDelegateClass = nullptr;
    Il2CppClass* overDelegateClass = nullptr;
    Il2CppClass* drawDelegateClass = nullptr;
    Il2CppClass* stringLayoutClass = nullptr;
    size_t panelSize = off::panel_layout_off::Size;
    size_t worldOver = off::gui_world_select_menu_off::worldOver;
    const MethodInfo* panelDraw = nullptr;
    const MethodInfo* panelDrawAt = nullptr;
    const MethodInfo* panelRegion = nullptr;
    const MethodInfo* stringDraw = nullptr;
    const MethodInfo* stringDrawColor = nullptr;  // GUIString.Draw(layout, string, Color, bool)
    const MethodInfo* stringButton = nullptr;
    const MethodInfo* setGridRegion = nullptr;
    const MethodInfo* gridDraw = nullptr;
    const MethodInfo* transactionDraw = nullptr;
    const MethodInfo* begin = nullptr;
    const MethodInfo* end = nullptr;
    const MethodInfo* worldScale = nullptr;
    const MethodInfo* worldOverMethod = nullptr;
    const MethodInfo* dummyDrawMethod = nullptr;
    const MethodInfo* scaleCtor = nullptr;
    const MethodInfo* overCtor = nullptr;
    const MethodInfo* drawCtor = nullptr;
    const MethodInfo* stringCtor = nullptr;
    const MethodInfo* stringCopy = nullptr;
    const MethodInfo* keyUp = nullptr;
    const MethodInfo* mouseX = nullptr;
    const MethodInfo* mouseY = nullptr;
    const MethodInfo* mouseLeft = nullptr;
};
Resolved r;

void* g_origDummyDraw = nullptr;

// Fonte dos dados (runtime). Cópia imutável publicada atomicamente.
std::atomic<const TLModWorkshopSource*> g_source{nullptr};
std::atomic<bool> g_openRequested{false};

// ---- Estado da tela: só na thread do jogo ------------------------------------
enum class Page { Closed, List, Detail };
Page g_page = Page::Closed;
int32_t g_detail = -1;
float g_scroll = 0, g_momentum = 0;
int32_t g_dragging = 0;
TLModXnaVector2 g_dragPosition{};
float g_titleScale = 0, g_backScale = 0, g_newScale = 0;
bool g_drawingRows = false;
bool g_prevDown = false;
int32_t g_pressIndex = -1;
TLModXnaVector2 g_pressAt{};
TLModXnaRectangle g_listRegion{};  // a área da lista no último desenho (a página de detalhes usa)

struct Row {
    uint32_t name = 0, version = 0, status = 0, summary = 0, details = 0, toggle = 0;  // GC handles
    int32_t kind = 0;
    TLModXnaRectangle rect{};  // onde foi desenhada neste frame
    bool drawn = false;
};
std::vector<Row> g_rows;
uint64_t g_revision = ~0ull;
std::vector<uint32_t> g_oldHandles;  // handles de strings substituídas (a API não tem gchandle_free)

struct Handles {
    uint32_t scaleDel = 0, overDel = 0, drawDel = 0, overTarget = 0, detailText = 0;
    uint32_t title = 0, back = 0, close = 0, empty = 0;
};
Handles h;

template <typename Fn>
Fn Ptr(const MethodInfo* m) {
    return reinterpret_cast<Fn>(m->methodPointer);
}
template <typename Fn>
Fn LoadOrig(void* const& slot) {
    return reinterpret_cast<Fn>(__atomic_load_n(&slot, __ATOMIC_ACQUIRE));
}
Il2CppObject* Target(uint32_t handle) { return handle ? api.il2cpp_gchandle_get_target(handle) : nullptr; }
Il2CppObject* StaticObject(FieldInfo* f) {
    Il2CppObject* obj = nullptr;
    if (f) api.il2cpp_field_static_get_value(f, &obj);
    return obj;
}
uint32_t Pin(Il2CppObject* obj) { return obj ? api.il2cpp_gchandle_new(obj, false) : 0; }
uint32_t PinString(const char* s) { return Pin(api.il2cpp_string_new(s ? s : "")); }

const MethodInfo* Method(Il2CppClass* k, const char* name, int argc) {
    return k ? api.il2cpp_class_get_method_from_name(k, name, argc) : nullptr;
}
const MethodInfo* Method(const char* ns, const char* klass, const char* name, int argc) {
    return Method(api.il2cpp_class_from_name(bridge::Image(), ns, klass), name, argc);
}
// Overload pelo tipo exato de um parâmetro (vários Draw com o mesmo número de
// parâmetros). `ref` aparece como "&" no fim do nome do tipo.
const MethodInfo* MethodWithParam(Il2CppClass* k, const char* name, uint32_t argc, uint32_t index,
                                  const char* typeName) {
    void* iter = nullptr;
    while (const MethodInfo* m = k ? api.il2cpp_class_get_methods(k, &iter) : nullptr) {
        if (std::strcmp(api.il2cpp_method_get_name(m), name) != 0 || api.il2cpp_method_get_param_count(m) != argc)
            continue;
        char* type = api.il2cpp_type_get_name(api.il2cpp_method_get_param(m, index));
        bool match = type && std::strcmp(type, typeName) == 0;
        api.il2cpp_free(type);
        if (match) return m;
    }
    return nullptr;
}

Il2CppObject* Layout() { return StaticObject(r.layoutInstance); }
Il2CppObject* Part(Il2CppObject* layout, size_t offset) { return FieldAt<Il2CppObject*>(layout, offset); }

// ---- Chamadas ao jogo ---------------------------------------------------------
void PanelDraw(Il2CppObject* panel) {
    if (panel) Ptr<PanelDrawFn>(r.panelDraw)(panel, false, 0, 0, 0, r.panelDraw);
}
void StringDraw(Il2CppObject* layout, uint32_t text) {
    Il2CppObject* s = Target(text);
    if (layout && s) Ptr<StringDrawFn>(r.stringDraw)(layout, s, r.stringDraw);
}
void StringDrawColor(Il2CppObject* layout, uint32_t text, uint32_t color) {
    typedef void (*Fn)(Il2CppObject*, Il2CppObject*, uint32_t, bool, const MethodInfo*);
    Il2CppObject* s = Target(text);
    if (layout && s) Ptr<Fn>(r.stringDrawColor)(layout, s, color, false, r.stringDrawColor);
}
int32_t Button(Il2CppObject* layout, uint32_t label, float* scale) {
    Il2CppObject* s = Target(label);
    if (!layout || !s) return -1;
    return Ptr<TransactionDrawFn>(r.transactionDraw)(layout, nullptr, s, false, scale, false, false, false, false,
                                                     r.transactionDraw);
}
void Title(Il2CppObject* layout, uint32_t text) {
    Il2CppObject* s = Target(text);
    if (layout && s) Ptr<StringButtonFn>(r.stringButton)(layout, s, &g_titleScale, false, false, r.stringButton);
}
void SetGridItemRegion(const TLModXnaRectangle& rect) { Ptr<SetGridRegionFn>(r.setGridRegion)(rect, r.setGridRegion); }
// Recomeça o lote de sprites como a seleção de mundos faz antes e depois da lista.
void RestartBatch() {
    Il2CppObject* batch = StaticObject(r.spriteBatch);
    if (!batch) return;
    Ptr<InstanceFn>(r.end)(batch, r.end);
    NullableMatrix none{};
    Ptr<BeginFn>(r.begin)(batch, 0, nullptr, nullptr, nullptr, nullptr, nullptr, none, true, r.begin);
}
bool BackKey() { return r.keyUp && Ptr<KeyUpFn>(r.keyUp)(kKeyEscape, r.keyUp); }
void PlaySound(int32_t type) {
    // O som é opcional: o botão Mods do menu usa o mesmo método (menu_hooks.cpp).
    if (const MethodInfo* m = Method("Terraria.Audio", "SoundEngine", "PlaySound", off::argc::SoundEngine_PlaySound)) {
        int32_t x = -1, y = -1, style = 1;
        float volume = 1.0f, pitch = 0.0f;
        void* params[] = {&type, &x, &y, &style, &volume, &pitch};
        Il2CppException* exc = nullptr;
        api.il2cpp_runtime_invoke(m, nullptr, params, &exc);
    }
}

// ---- Dados ------------------------------------------------------------------
// Refaz as strings quando a revisão da fonte muda (ao abrir e depois de um toggle).
void Refresh(bool force) {
    const TLModWorkshopSource* src = g_source.load(std::memory_order_acquire);
    if (!src) return;
    uint64_t rev = src->revision ? src->revision(src->user) : 0;
    if (!force && rev == g_revision) return;
    g_revision = rev;
    for (const Row& row : g_rows)
        for (uint32_t hd : {row.name, row.version, row.status, row.summary, row.details, row.toggle})
            if (hd) g_oldHandles.push_back(hd);
    g_rows.clear();
    int32_t n = src->count ? src->count(src->user) : 0;
    for (int32_t i = 0; i < n; ++i) {
        TLModWorkshopEntry e{};
        if (!src->entry || !src->entry(src->user, i, &e)) break;
        Row row;
        row.name = PinString(e.name);
        row.version = PinString(e.version);
        row.status = PinString(e.status);
        row.summary = PinString(e.summary);
        row.details = PinString(e.details);
        row.toggle = PinString(e.toggleLabel);
        row.kind = e.statusKind >= 0 && e.statusKind < 4 ? e.statusKind : 0;
        g_rows.push_back(row);
    }
}

// Delegates e objetos próprios: criados uma vez, na thread do jogo.
bool EnsureObjects() {
    if (h.scaleDel) return true;
    Il2CppObject* target = api.il2cpp_object_new(r.worldSelectClass);  // só WorldOver escreve nele
    Il2CppObject* scale = api.il2cpp_object_new(r.scaleDelegateClass);
    Il2CppObject* over = api.il2cpp_object_new(r.overDelegateClass);
    if (!target || !scale || !over) return false;
    Ptr<DelegateCtorFn>(r.scaleCtor)(scale, target, r.worldScale, r.scaleCtor);
    Ptr<DelegateCtorFn>(r.overCtor)(over, target, r.worldOverMethod, r.overCtor);
    Il2CppObject* draw = StaticObject(r.dummyDraw);
    if (!draw) {  // o static ainda não existe: um delegate novo para o mesmo DummyDraw
        draw = api.il2cpp_object_new(r.drawDelegateClass);
        if (!draw) return false;
        Ptr<DelegateCtorFn>(r.drawCtor)(draw, nullptr, r.dummyDrawMethod, r.drawCtor);
    }
    // Texto da página de detalhes: cópia do nome da linha (fonte e cor do jogo),
    // ancorada no canto da região GridItem, com quebra de linha.
    Il2CppObject* text = api.il2cpp_object_new(r.stringLayoutClass);
    if (!text) return false;
    Ptr<InstanceFn>(r.stringCtor)(text, r.stringCtor);
    if (Il2CppObject* layout = Layout()) Ptr<CopyFn>(r.stringCopy)(text, Part(layout, ws::ItemName), r.stringCopy);
    namespace sl = off::string_layout_off;
    FieldAt<int32_t>(text, sl::AnchorControl) = kControlGridItem;
    FieldAt<int32_t>(text, sl::Anchor) = kAnchorTopLeft;
    FieldAt<float>(text, sl::Location) = FieldAt<float>(text, sl::Location + 4) = 0.0f;
    FieldAt<int32_t>(text, sl::Alignment) = kAnchorTopLeft;
    FieldAt<bool>(text, sl::MultiLineUseAlignment) = true;
    FieldAt<int32_t>(text, sl::MaxLines) = 40;
    h.overTarget = Pin(target);
    h.scaleDel = Pin(scale);
    h.overDel = Pin(over);
    h.drawDel = Pin(draw);
    h.detailText = Pin(text);
    h.title = PinString("Oficina de Mods");
    h.back = PinString("Voltar");
    h.empty = PinString("Nenhum mod instalado");
    return true;
}

// ---- Toque nas linhas -----------------------------------------------------------
int32_t RowAt(float x, float y) {
    TLModXnaRectangle area{};
    if (r.gridRegion) api.il2cpp_field_static_get_value(r.gridRegion, &area);
    auto inside = [&](const TLModXnaRectangle& q) {
        return x >= q.x && x < q.x + q.width && y >= q.y && y < q.y + q.height;
    };
    if (area.width > 0 && !inside(area)) return -1;
    for (size_t i = 0; i < g_rows.size(); ++i)
        if (g_rows[i].drawn && inside(g_rows[i].rect)) return static_cast<int32_t>(i);
    return -1;
}

// Um toque (soltar perto de onde apertou, sem arrastar a lista) abre a linha.
int32_t TappedRow() {
    const bool down = Ptr<StaticBoolFn>(r.mouseLeft)(r.mouseLeft);
    const float x = static_cast<float>(Ptr<StaticIntFn>(r.mouseX)(r.mouseX));
    const float y = static_cast<float>(Ptr<StaticIntFn>(r.mouseY)(r.mouseY));
    int32_t tapped = -1;
    if (down && !g_prevDown) {
        g_pressIndex = RowAt(x, y);
        g_pressAt = {x, y};
    } else if (!down && g_prevDown) {
        if (g_pressIndex >= 0 && std::hypot(x - g_pressAt.x, y - g_pressAt.y) < kTapDistance &&
            RowAt(x, y) == g_pressIndex)
            tapped = g_pressIndex;
        g_pressIndex = -1;
    }
    g_prevDown = down;
    return tapped;
}

// ---- Páginas -------------------------------------------------------------------
void DrawList(Il2CppObject* layout) {
    PanelDraw(Part(layout, ws::Backing));
    PanelDraw(Part(layout, ws::MenuDivider));
    Il2CppObject* backing = Part(layout, ws::ItemBacking);
    TLModXnaVector2 itemSize = backing ? FieldAt<TLModXnaVector2>(backing, r.panelSize) : TLModXnaVector2{};
    FieldAt<int32_t>(Target(h.overTarget), r.worldOver) = -1;
    for (Row& row : g_rows) row.drawn = false;
    g_drawingRows = true;
    Ptr<GridDrawFn>(r.gridDraw)(Part(layout, ws::WorldGrid), Target(h.scaleDel), Target(h.overDel), Target(h.drawDel),
                                static_cast<int32_t>(g_rows.size()), &g_scroll, &g_momentum, &g_dragging,
                                &g_dragPosition, itemSize, false, true, false, r.gridDraw);
    g_drawingRows = false;
    if (r.gridRegion) api.il2cpp_field_static_get_value(r.gridRegion, &g_listRegion);
    RestartBatch();  // a lista recorta o desenho; o resto da tela vem sem recorte
    Title(Part(layout, ws::Title), h.title);
    if (g_rows.empty()) {
        Il2CppObject* text = Target(h.detailText);
        FieldAt<float>(text, off::string_layout_off::WrapWidth) = static_cast<float>(g_listRegion.width);
        SetGridItemRegion(g_listRegion);
        StringDraw(text, h.empty);
    }
    const int32_t tapped = TappedRow();
    if (tapped >= 0) {
        PlaySound(12);
        g_page = Page::Detail;
        g_detail = tapped;
        g_newScale = 0;
        return;
    }
    if (Button(Part(layout, ws::Back), h.back, &g_backScale) == kInputClicked || BackKey()) {
        PlaySound(11);
        g_page = Page::Closed;
    }
}

void DrawDetail(Il2CppObject* layout) {
    PanelDraw(Part(layout, ws::Backing));
    PanelDraw(Part(layout, ws::MenuDivider));
    if (g_detail < 0 || static_cast<size_t>(g_detail) >= g_rows.size()) {
        g_page = Page::List;
        return;
    }
    const Row& row = g_rows[static_cast<size_t>(g_detail)];
    Title(Part(layout, ws::Title), row.name);
    // O texto ocupa a área da lista (abaixo do título), com uma margem.
    TLModXnaRectangle area = g_listRegion;
    if (area.width <= 0) area = Ptr<PanelRegionFn>(r.panelRegion)(Part(layout, ws::Backing), r.panelRegion);
    const int32_t pad = 12;
    area = {area.x + pad, area.y + pad, std::max(0, area.width - 2 * pad), std::max(0, area.height - 2 * pad)};
    Il2CppObject* text = Target(h.detailText);
    FieldAt<float>(text, off::string_layout_off::WrapWidth) = static_cast<float>(area.width);
    SetGridItemRegion(area);
    StringDraw(text, row.details);
    g_prevDown = Ptr<StaticBoolFn>(r.mouseLeft)(r.mouseLeft);  // um toque aqui não vale para a lista
    g_pressIndex = -1;
    if (Button(Part(layout, ws::New), row.toggle, &g_newScale) == kInputClicked) {
        PlaySound(12);
        const TLModWorkshopSource* src = g_source.load(std::memory_order_acquire);
        if (src && src->toggle) src->toggle(src->user, g_detail);
        Refresh(false);
    }
    if (Button(Part(layout, ws::Back), h.back, &g_backScale) == kInputClicked || BackKey()) {
        PlaySound(11);
        g_page = Page::List;
    }
}

}  // namespace

namespace bridge {

bool ResolveWorkshop() {
    const Il2CppImage* img = Image();
    Il2CppClass* layout = api.il2cpp_class_from_name(img, "", off::kWorldSelectLayoutClass);
    r.layoutInstance = layout ? api.il2cpp_class_get_field_from_name(layout, "Instance") : nullptr;
    r.spriteBatch = api.il2cpp_class_get_field_from_name(MainClass(), "spriteBatch");
    Il2CppClass* grid = api.il2cpp_class_from_name(img, "", "GUIItemGrid");
    r.dummyDraw = grid ? api.il2cpp_class_get_field_from_name(grid, "dummyDraw") : nullptr;
    Il2CppClass* dragGrid = api.il2cpp_class_from_name(img, "", "GUIDraggableItemGrid");
    r.gridRegion = dragGrid ? api.il2cpp_class_get_field_from_name(dragGrid, "fullRegion") : nullptr;
    r.worldSelectClass = api.il2cpp_class_from_name(img, "", "GUIWorldSelectMenu");
    // Delegates aninhados em GUIItemGrid.
    void* iter = nullptr;
    while (Il2CppClass* nested = grid ? api.il2cpp_class_get_nested_types(grid, &iter) : nullptr) {
        const char* name = api.il2cpp_class_get_name(nested);
        if (std::strcmp(name, "GetItemScaleHandler") == 0) r.scaleDelegateClass = nested;
        else if (std::strcmp(name, "CursorOver") == 0) r.overDelegateClass = nested;
        else if (std::strcmp(name, "DrawItemHandler") == 0) r.drawDelegateClass = nested;
    }
    r.stringLayoutClass = api.il2cpp_class_from_name(img, "", "String_Layout");
    if (Il2CppClass* panel = api.il2cpp_class_from_name(img, "", "Panel_Layout"))
        r.panelSize = FieldOffset(panel, "Size", r.panelSize);
    if (r.worldSelectClass) r.worldOver = FieldOffset(r.worldSelectClass, "worldOver", r.worldOver);

    namespace argc = off::argc;
    Il2CppClass* guiPanel = api.il2cpp_class_from_name(img, "", "GUIPanel");
    r.panelDraw = Method(guiPanel, "Draw", argc::GUIPanel_Draw);
    r.panelDrawAt = Method(guiPanel, "DrawAtPosition", argc::GUIPanel_DrawAtPosition);
    r.panelRegion = Method(guiPanel, "Region", argc::GUIPanel_Region);
    Il2CppClass* guiString = api.il2cpp_class_from_name(img, "", "GUIString");
    r.stringDraw = MethodWithParam(guiString, "Draw", argc::GUIString_Draw, 1, "System.String");
    r.stringDrawColor = MethodWithParam(guiString, "Draw", 4, 1, "System.String");
    r.stringButton = Method("", "GUIStringButton", "DrawButton", argc::GUIStringButton_DrawButton);
    r.setGridRegion = Method("", "ControlAnchor", "SetGridItemRegion", argc::ControlAnchor_SetGridItemRegion);
    r.gridDraw =
        MethodWithParam(dragGrid, "Draw", argc::GUIDraggableItemGrid_Draw, 9, "Microsoft.Xna.Framework.Vector2");
    r.transactionDraw = MethodWithParam(api.il2cpp_class_from_name(img, "", "GUITransactionButton"), "Draw",
                                        argc::GUITransactionButton_Draw, 1, "Terraria.Item");
    Il2CppClass* batch = api.il2cpp_class_from_name(img, "Microsoft.Xna.Framework.Graphics", "SpriteBatch");
    r.begin = Method(batch, "Begin", argc::SpriteBatch_Begin);
    r.end = Method(batch, "End", argc::SpriteBatch_End);
    r.worldScale = Method(r.worldSelectClass, "WorldScale", argc::GUIWorldSelectMenu_WorldScale);
    r.worldOverMethod = Method(r.worldSelectClass, "WorldOver", argc::GUIWorldSelectMenu_WorldOver);
    r.dummyDrawMethod = Method(grid, "DummyDraw", argc::GUIItemGrid_DummyDraw);
    r.scaleCtor = Method(r.scaleDelegateClass, ".ctor", 2);
    r.overCtor = Method(r.overDelegateClass, ".ctor", 2);
    r.drawCtor = Method(r.drawDelegateClass, ".ctor", 2);
    r.stringCtor = Method(r.stringLayoutClass, ".ctor", 0);
    r.stringCopy = Method(r.stringLayoutClass, "Copy", argc::StringLayout_Copy);
    r.keyUp = Method("", "KeyboardInput", "GetKeyUp", argc::KeyboardInput_GetKeyUp);
    r.mouseX = Method(MainClass(), "get_mouseX", argc::Main_get_mouseX);
    r.mouseY = Method(MainClass(), "get_mouseY", argc::Main_get_mouseY);
    r.mouseLeft = Method(MainClass(), "get_mouseLeft", argc::Main_get_mouseLeft);

    const void* needed[] = {r.layoutInstance, r.spriteBatch, r.gridRegion, r.worldSelectClass, r.scaleDelegateClass,
                            r.overDelegateClass, r.drawDelegateClass, r.stringLayoutClass, r.panelDraw, r.panelDrawAt,
                            r.panelRegion, r.stringDraw, r.stringDrawColor, r.stringButton, r.setGridRegion,
                            r.gridDraw, r.transactionDraw, r.begin, r.end, r.worldScale, r.worldOverMethod,
                            r.dummyDrawMethod, r.scaleCtor, r.overCtor, r.drawCtor, r.stringCtor, r.stringCopy,
                            r.mouseX, r.mouseY, r.mouseLeft};
    int missing = 0;
    for (const void* p : needed) missing += p == nullptr;
    if (missing) {
        LOGW("Oficina nativa: %d peça(s) da tela de mundos não resolveram; o botão Mods usa o painel", missing);
        return false;
    }
    if (!r.keyUp) LOGW("Oficina nativa: KeyboardInput.GetKeyUp ausente; o voltar do Android não fecha a Oficina");
    if (!InstallHook("GUIItemGrid.DummyDraw", r.dummyDrawMethod, off::rva::GUIItemGrid_DummyDraw,
                     reinterpret_cast<void*>(&TLMod_Hooked_GUIItemGridDummyDraw), &g_origDummyDraw))
        return false;
    r.ready = true;
    LOGI("Oficina nativa pronta (peças da tela de seleção de mundos)");
    return true;
}

// Chamado pelo hook do GUIMainMenu.Draw: com a Oficina aberta, desenha ela no
// lugar do menu e devolve true.
bool DrawWorkshop() {
    if (g_openRequested.exchange(false, std::memory_order_acq_rel) && r.ready &&
        g_source.load(std::memory_order_acquire)) {
        g_page = Page::List;
        g_detail = -1;
        g_scroll = g_momentum = 0;
        g_dragging = 0;
        g_titleScale = g_backScale = 0;
        g_prevDown = true;  // o dedo que tocou em "Mods" ainda pode estar na tela
        g_pressIndex = -1;
        Refresh(true);
    }
    if (g_page == Page::Closed) return false;
    Il2CppObject* layout = Layout();
    if (!r.ready || !layout || !EnsureObjects()) {
        g_page = Page::Closed;
        return false;
    }
    Refresh(false);
    RestartBatch();
    if (g_page == Page::List) DrawList(layout);
    else DrawDetail(layout);
    return true;
}

}  // namespace bridge

extern "C" {

void TLMod_SetWorkshopSource(const TLModWorkshopSource* source) {
    g_source.store(source ? new TLModWorkshopSource(*source) : nullptr, std::memory_order_release);
}
bool TLMod_WorkshopAvailable(void) { return r.ready && g_source.load(std::memory_order_acquire); }
void TLMod_OpenWorkshop(void) { g_openRequested.store(true, std::memory_order_release); }
bool TLMod_WorkshopOpen(void) { return g_page != Page::Closed; }

// GUIItemGrid.DummyDraw: vazio no jogo. Enquanto a Oficina desenha a lista, é a
// linha `index` (o GUIDraggableItemGrid já descontou a rolagem em `position`).
void TLMod_Hooked_GUIItemGridDummyDraw(Il2CppObject* gridLayout, int32_t index, TLModXnaVector2 position, float scale,
                                       const MethodInfo* method) {
    if (!g_drawingRows || index < 0 || static_cast<size_t>(index) >= g_rows.size()) {
        if (auto orig = LoadOrig<DummyDrawFn>(g_origDummyDraw)) orig(gridLayout, index, position, scale, method);
        return;
    }
    Il2CppObject* layout = Layout();
    Row& row = g_rows[static_cast<size_t>(index)];
    const bool pressed = g_pressIndex == index;
    Il2CppObject* backing = Part(layout, pressed ? ws::ItemSelectedBacking : ws::ItemBacking);
    if (!backing) backing = Part(layout, ws::ItemBacking);
    TLModXnaVector2 size = FieldAt<TLModXnaVector2>(backing, r.panelSize);
    row.rect = {static_cast<int32_t>(position.x), static_cast<int32_t>(position.y), static_cast<int32_t>(size.x),
                static_cast<int32_t>(size.y)};
    row.drawn = true;
    SetGridItemRegion(row.rect);
    const bool over = FieldAt<int32_t>(Target(h.overTarget), r.worldOver) == index;
    Ptr<PanelDrawAtFn>(r.panelDrawAt)(backing, position, size, over, r.panelDrawAt);
    for (size_t part : {ws::ItemSizeBacking, ws::ItemModeBacking, ws::ItemTimeBacking, ws::ItemNameDivider})
        PanelDraw(Part(layout, part));
    StringDraw(Part(layout, ws::ItemName), row.name);
    StringDraw(Part(layout, ws::ItemTime), row.version);
    StringDrawColor(Part(layout, ws::ItemMode), row.status, kStatusColors[row.kind]);
    StringDraw(Part(layout, ws::ItemSize), row.summary);
}

}  // extern "C"
