// Oficina de Mods nativa: uma tela no lugar do menu principal, desenhada com
// as peças da tela de seleção de mundos (WorldSelectMenu_Layout), como o
// tModLoader faz no PC com a interface do próprio jogo.
//
//   Lista:    fundo, divisória, título, uma linha por mod e Voltar. Cada linha usa
//             o fundo de linha de mundo e os textos de nome, modo, tamanho e tempo
//             (estado, versão, resumo). As linhas vão de cima para baixo, a partir
//             da posição da primeira linha de mundo, até a divisória acima do
//             Voltar; arrastar rola de linha em linha.
//   Detalhes: tocar numa linha abre o texto completo do mod (descrição, autores,
//             erro inteiro), quebrado pelo próprio jogo (ApplyMultiLineAlignment)
//             e desenhado linha a linha na mesma área, com rolagem; Voltar e o
//             botão "Novo" virado em Ativar/Desativar.
//
// Os textos ancorados em GridItem (os da linha de mundo) se posicionam pela
// região de ControlAnchor.SetGridItemRegion, como na seleção de mundos. As
// funções do jogo são chamadas direto, pelo methodPointer, com as assinaturas
// conferidas na desmontagem (Nullable<Color> vai num registrador;
// Nullable<Matrix>, por ser maior que 16 bytes, por referência).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "draw_internal.h"
#include "terraria_offsets.h"

using namespace il2cpp;

namespace {

namespace off = terraria;
namespace ws = terraria::world_select_layout_off;
namespace sl = terraria::string_layout_off;

struct NullableMatrix {  // Nullable<Matrix>: 16 floats + hasValue
    float m[16];
    bool hasValue;
};

typedef void (*PanelDrawFn)(Il2CppObject* layout, bool over, uint64_t backing, uint64_t border, uint64_t highlight,
                            const MethodInfo*);
typedef void (*PanelDrawAtFn)(Il2CppObject* layout, TLModXnaVector2 position, TLModXnaVector2 size, bool over,
                              const MethodInfo*);
typedef TLModXnaRectangle (*RegionFn)(Il2CppObject* layout, const MethodInfo*);
typedef void (*StringDrawFn)(Il2CppObject* layout, Il2CppObject* value, const MethodInfo*);
typedef void (*StringDrawColorFn)(Il2CppObject* layout, Il2CppObject* value, uint32_t color, bool multiline,
                                  const MethodInfo*);
typedef TLModXnaRectangle (*StringRegionFn)(Il2CppObject* layout, Il2CppObject* value, const MethodInfo*);
typedef Il2CppObject* (*WrapFn)(Il2CppObject* layout, Il2CppObject* value, int32_t* lineCount, const MethodInfo*);
typedef bool (*StringButtonFn)(Il2CppObject* layout, Il2CppObject* value, float* scale, bool forcedPressed,
                               bool disabled, const MethodInfo*);
typedef void (*SetGridRegionFn)(TLModXnaRectangle region, const MethodInfo*);
typedef TLModXnaVector2 (*AnchoredFn)(int32_t control, int32_t anchor, TLModXnaVector2 position, const MethodInfo*);
typedef int32_t (*TransactionDrawFn)(Il2CppObject* layout, Il2CppObject* item, Il2CppObject* label, bool disabled,
                                     float* scale, bool forcedPressed, bool hasControllerFocus, bool forceOver,
                                     bool disablePressedState, const MethodInfo* method);
typedef void (*BeginFn)(Il2CppObject* batch, int32_t sort, Il2CppObject* blend, Il2CppObject* sampler,
                        Il2CppObject* depth, Il2CppObject* raster, Il2CppObject* effect, NullableMatrix matrix,
                        bool deferred, const MethodInfo*);
typedef void (*InstanceFn)(Il2CppObject* self, const MethodInfo*);
typedef void (*CopyFn)(Il2CppObject* self, Il2CppObject* other, const MethodInfo*);
typedef bool (*KeyUpFn)(int32_t key, const MethodInfo*);
typedef int32_t (*StaticIntFn)(const MethodInfo*);
typedef bool (*StaticBoolFn)(const MethodInfo*);
typedef TLModXnaVector2 (*StaticVectorFn)(const MethodInfo*);
typedef void (*DrawCentredFn)(Il2CppObject* batch, TLModXnaVector2 position, float scale, Il2CppObject* texture,
                              uint64_t color, const MethodInfo*);

constexpr int32_t kInputClicked = 0;     // GUITransactionButton.InputState.Clicked
constexpr int32_t kKeyEscape = 27;       // KeyCode.Escape: o voltar do Android
constexpr int32_t kControlGridItem = 2;  // ControlAnchor.ControlId.GridItem
constexpr int32_t kAnchorTopLeft = 9;    // LayoutCalculator.AnchorType.TopLeft
constexpr float kTapDistance = 30.0f;    // como o SINGLE_CLICK_DISTANCE da seleção de mundos
constexpr float kDragStart = 10.0f;      // a partir daqui o toque vira arraste
constexpr int32_t kPad = 12;             // margem do texto de detalhes
// Color do XNA nesta build: bytes A, B, G, R (o construtor grava r no byte 3 e a no 0).
constexpr uint32_t Rgba(uint32_t r, uint32_t g, uint32_t b) { return 0xFFu | (b << 8) | (g << 16) | (r << 24); }
constexpr uint32_t kStatusColors[] = {Rgba(130, 225, 120), Rgba(170, 170, 170), Rgba(255, 110, 110),
                                      Rgba(255, 210, 90)};

struct Resolved {
    bool ready = false;
    FieldInfo* layoutInstance = nullptr;  // static WorldSelectMenu_Layout.Instance
    FieldInfo* spriteBatch = nullptr;     // static Main.spriteBatch
    Il2CppClass* stringLayoutClass = nullptr;
    size_t panelSize = off::panel_layout_off::Size;
    const MethodInfo* panelDraw = nullptr;
    const MethodInfo* panelDrawAt = nullptr;
    const MethodInfo* panelRegion = nullptr;
    const MethodInfo* stringDraw = nullptr;
    const MethodInfo* stringDrawColor = nullptr;  // GUIString.Draw(layout, string, Color, bool)
    const MethodInfo* stringRegion = nullptr;     // GUIString.GetRegion(layout, string)
    const MethodInfo* wrap = nullptr;             // GUIString.ApplyMultiLineAlignment(layout, string, out int)
    const MethodInfo* stringButton = nullptr;
    const MethodInfo* setGridRegion = nullptr;
    const MethodInfo* anchored = nullptr;  // LayoutCalculator.GetAnchoredPosition
    const MethodInfo* transactionDraw = nullptr;
    const MethodInfo* begin = nullptr;
    const MethodInfo* end = nullptr;
    const MethodInfo* stringCtor = nullptr;
    const MethodInfo* stringCopy = nullptr;
    const MethodInfo* keyUp = nullptr;
    const MethodInfo* mouseX = nullptr;
    const MethodInfo* mouseY = nullptr;
    const MethodInfo* mouseLeft = nullptr;
    // Mouse.GetUICursorPosition(): o toque no espaço da interface (o dos layouts).
    // Main.mouseX/Y é em pixels da tela, que no celular é maior (ex.: 1600 contra 969).
    const MethodInfo* uiCursor = nullptr;
    const MethodInfo* drawCentred = nullptr;  // LayoutCalculator.DrawCentred(batch, Vector2, float, Texture2D, Color?)
};
Resolved r;

// Fonte dos dados (runtime). Cópia imutável publicada atomicamente.
std::atomic<const TLModWorkshopSource*> g_source{nullptr};
std::atomic<bool> g_openRequested{false};

// ---- Estado da tela: só na thread do jogo ------------------------------------
enum class Page { Closed, List, Detail };
Page g_page = Page::Closed;
int32_t g_detail = -1;
float g_titleScale = 0, g_backScale = 0, g_newScale = 0;

// Toque e arraste numa área (a lista ou o texto de detalhes).
struct Touch {
    bool prevDown = false;
    bool pressed = false;   // começou dentro da área
    bool dragging = false;  // passou de kDragStart
    TLModXnaVector2 at{};   // onde apertou
    float startScroll = 0;
};
Touch g_touch;
float g_listScroll = 0;    // pixels; encaixa numa linha ao soltar
float g_detailScroll = 0;  // pixels; encaixa numa linha de texto ao soltar

struct Row {
    uint32_t name = 0, version = 0, status = 0, summary = 0, details = 0, toggle = 0;  // GC handles
    int32_t kind = 0;
    int32_t iconItem = 0;  // item cujo ícone vai na moldura da linha (0 = nenhum)
    TLModXnaRectangle rect{};  // onde foi desenhada neste frame
    bool drawn = false;
};
std::vector<Row> g_rows;
uint64_t g_revision = ~0ull;
std::vector<uint32_t> g_oldHandles;  // handles de strings substituídas (a API não tem gchandle_free)

// Texto de detalhes já quebrado em linhas (refeito ao abrir outro mod ou mudar a revisão).
std::vector<uint32_t> g_detailLines;
int32_t g_detailFor = -1;
uint64_t g_detailRevision = ~0ull;
float g_lineHeight = 0;

struct Handles {
    uint32_t line = 0;  // String_Layout de uma linha: cópia do nome da linha de mundo, no canto do GridItem
    uint32_t wrap = 0;  // a mesma, com quebra (só para ApplyMultiLineAlignment)
    uint32_t title = 0, back = 0, empty = 0, probe1 = 0, probe2 = 0;
};
Handles h;

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
uint32_t Pin(Il2CppObject* obj) { return obj ? api.il2cpp_gchandle_new(obj, false) : 0; }
uint32_t PinString(const char* s) { return Pin(api.il2cpp_string_new(s ? s : "")); }
void Retire(std::vector<uint32_t>& handles) {
    for (uint32_t hd : handles)
        if (hd) g_oldHandles.push_back(hd);
    handles.clear();
}

const MethodInfo* Method(Il2CppClass* k, const char* name, int argc) {
    return k ? api.il2cpp_class_get_method_from_name(k, name, argc) : nullptr;
}
const MethodInfo* Method(const char* ns, const char* klass, const char* name, int argc) {
    return Method(api.il2cpp_class_from_name(bridge::Image(), ns, klass), name, argc);
}
// Overload pelo tipo exato de um parâmetro (vários Draw com o mesmo número de parâmetros).
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
TLModXnaRectangle PanelRegion(Il2CppObject* panel) {
    return panel ? Ptr<RegionFn>(r.panelRegion)(panel, r.panelRegion) : TLModXnaRectangle{};
}
void StringDraw(Il2CppObject* layout, uint32_t text) {
    Il2CppObject* s = Target(text);
    if (layout && s) Ptr<StringDrawFn>(r.stringDraw)(layout, s, r.stringDraw);
}
void StringDrawColor(Il2CppObject* layout, uint32_t text, uint32_t color) {
    Il2CppObject* s = Target(text);
    if (layout && s) Ptr<StringDrawColorFn>(r.stringDrawColor)(layout, s, color, false, r.stringDrawColor);
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
// Recomeça o lote de sprites como a seleção de mundos faz.
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
        row.iconItem = e.iconItem;
        g_rows.push_back(row);
    }
}

// String_Layout próprio: cópia do nome da linha de mundo (fonte e cor do jogo),
// no canto superior esquerdo da região GridItem.
Il2CppObject* NewTextLayout(Il2CppObject* layout, bool wrap) {
    Il2CppObject* text = api.il2cpp_object_new(r.stringLayoutClass);
    if (!text) return nullptr;
    Ptr<InstanceFn>(r.stringCtor)(text, r.stringCtor);
    Ptr<CopyFn>(r.stringCopy)(text, Part(layout, ws::ItemName), r.stringCopy);
    FieldAt<int32_t>(text, sl::AnchorControl) = kControlGridItem;
    FieldAt<int32_t>(text, sl::Anchor) = kAnchorTopLeft;
    FieldAt<float>(text, sl::Location) = FieldAt<float>(text, sl::Location + 4) = 0.0f;
    FieldAt<int32_t>(text, sl::Alignment) = kAnchorTopLeft;
    FieldAt<bool>(text, sl::MultiLineUseAlignment) = wrap;
    FieldAt<int32_t>(text, sl::MaxLines) = wrap ? 1000 : 1;
    return text;
}

bool EnsureObjects(Il2CppObject* layout) {
    if (h.line) return true;
    Il2CppObject* line = NewTextLayout(layout, false);
    Il2CppObject* wrap = NewTextLayout(layout, true);
    if (!line || !wrap) return false;
    h.line = Pin(line);
    h.wrap = Pin(wrap);
    h.title = PinString("Oficina de Mods");
    h.back = PinString("Voltar");
    h.empty = PinString("Nenhum mod instalado");
    h.probe1 = PinString("Ag");
    h.probe2 = PinString("Ag\nAg");
    return true;
}

// ---- Geometria da lista --------------------------------------------------------
// A lista começa onde a seleção de mundos põe a primeira linha (FirstItemLocation
// do WorldGrid) e vai até a divisória acima do Voltar.
struct ListArea {
    TLModXnaRectangle rect{};  // área visível
    TLModXnaVector2 rowSize{};
    float stride = 0;          // altura da linha + espaço
};
ListArea Area(Il2CppObject* layout) {
    ListArea a;
    Il2CppObject* grid = Part(layout, ws::WorldGrid);
    Il2CppObject* backing = Part(layout, ws::ItemBacking);
    if (!grid || !backing) return a;
    namespace ig = off::item_grid_layout_off;
    TLModXnaVector2 first = Ptr<AnchoredFn>(r.anchored)(
        FieldAt<int32_t>(grid, ig::FirstAnchorControl), FieldAt<int32_t>(grid, ig::FirstItemAnchor),
        FieldAt<TLModXnaVector2>(grid, ig::FirstItemLocation), r.anchored);
    a.rowSize = FieldAt<TLModXnaVector2>(backing, r.panelSize);
    const float gap = std::max(FieldAt<TLModXnaVector2>(grid, ig::ExtraElementSpacing).y, 4.0f);
    a.stride = a.rowSize.y + gap;
    TLModXnaRectangle divider = PanelRegion(Part(layout, ws::MenuDivider));
    TLModXnaRectangle panel = PanelRegion(Part(layout, ws::Backing));
    float bottom = divider.height > 0 && divider.y > first.y + a.rowSize.y
                       ? static_cast<float>(divider.y) - 2.0f
                       : static_cast<float>(panel.y + panel.height) - 2.0f * a.rowSize.y;
    a.rect = {static_cast<int32_t>(first.x), static_cast<int32_t>(first.y), static_cast<int32_t>(a.rowSize.x),
              static_cast<int32_t>(std::max(bottom - first.y, a.rowSize.y))};
    return a;
}

bool Inside(const TLModXnaRectangle& q, float x, float y) {
    return x >= q.x && x < q.x + q.width && y >= q.y && y < q.y + q.height;
}

// Toque numa área com rolagem: arrastar muda `scroll` (limitado a [0, maxScroll]);
// ao soltar, a rolagem encaixa num múltiplo de `step`. Devolve true num toque
// curto (soltar perto de onde apertou, sem arrastar); `at` = onde foi.
int g_pressesLogged = 0;  // os primeiros toques vão para o log (os dois espaços de coordenadas)

bool TrackTouch(const TLModXnaRectangle& area, float* scroll, float maxScroll, float step, TLModXnaVector2* at) {
    const bool down = Ptr<StaticBoolFn>(r.mouseLeft)(r.mouseLeft);
    const int32_t mx = Ptr<StaticIntFn>(r.mouseX)(r.mouseX), my = Ptr<StaticIntFn>(r.mouseY)(r.mouseY);
    const TLModXnaVector2 ui = r.uiCursor ? Ptr<StaticVectorFn>(r.uiCursor)(r.uiCursor)
                                          : TLModXnaVector2{static_cast<float>(mx), static_cast<float>(my)};
    const float x = ui.x, y = ui.y;
    bool tapped = false;
    if (down && !g_touch.prevDown && g_pressesLogged < 3) {
        ++g_pressesLogged;
        LOGI("Oficina: toque em (%.0f, %.0f) na interface, (%d, %d) em Main.mouse; área (%d, %d) %dx%d", x, y, mx, my,
             area.x, area.y, area.width, area.height);
    }
    if (down && !g_touch.prevDown) {
        g_touch.pressed = Inside(area, x, y);
        g_touch.dragging = false;
        g_touch.at = {x, y};
        g_touch.startScroll = *scroll;
    } else if (down && g_touch.pressed) {
        if (std::fabs(y - g_touch.at.y) > kDragStart) g_touch.dragging = true;
        if (g_touch.dragging) *scroll = std::clamp(g_touch.startScroll - (y - g_touch.at.y), 0.0f, maxScroll);
    } else if (!down && g_touch.prevDown && g_touch.pressed) {
        if (g_touch.dragging && step > 0) *scroll = std::clamp(std::round(*scroll / step) * step, 0.0f, maxScroll);
        else if (std::hypot(x - g_touch.at.x, y - g_touch.at.y) < kTapDistance) tapped = true;
        if (at) *at = g_touch.at;
        g_touch.pressed = g_touch.dragging = false;
    }
    g_touch.prevDown = down;
    return tapped;
}

// ---- Páginas -------------------------------------------------------------------
void DrawRow(Il2CppObject* layout, Row& row, const TLModXnaVector2& position, const TLModXnaVector2& size,
             bool pressed) {
    Il2CppObject* backing = Part(layout, pressed ? ws::ItemSelectedBacking : ws::ItemBacking);
    if (!backing) backing = Part(layout, ws::ItemBacking);
    row.rect = {static_cast<int32_t>(position.x), static_cast<int32_t>(position.y), static_cast<int32_t>(size.x),
                static_cast<int32_t>(size.y)};
    row.drawn = true;
    SetGridItemRegion(row.rect);
    Ptr<PanelDrawAtFn>(r.panelDrawAt)(backing, position, size, pressed, r.panelDrawAt);
    for (size_t part : {ws::ItemSizeBacking, ws::ItemModeBacking, ws::ItemTimeBacking, ws::ItemNameDivider})
        PanelDraw(Part(layout, part));
    // Ícone: a moldura da imagem do mundo, com o item centralizado (até 70% dela, no máximo 2x).
    Il2CppObject* frame = Part(layout, ws::ItemImage);
    PanelDraw(frame);
    int32_t w = 0, hgt = 0;
    Il2CppObject* texture = row.iconItem > 0 ? drawx::ItemIconTexture(row.iconItem, &w, &hgt) : nullptr;
    Il2CppObject* batch = StaticObject(r.spriteBatch);
    if (texture && frame && batch && r.drawCentred) {
        const TLModXnaRectangle box = PanelRegion(frame);
        const float side = static_cast<float>(std::max(1, std::max(w, hgt)));
        const float scale = std::min(2.0f, 0.7f * static_cast<float>(std::min(box.width, box.height)) / side);
        const TLModXnaVector2 center{static_cast<float>(box.x) + 0.5f * static_cast<float>(box.width),
                                     static_cast<float>(box.y) + 0.5f * static_cast<float>(box.height)};
        Ptr<DrawCentredFn>(r.drawCentred)(batch, center, scale, texture, 0, r.drawCentred);
    }
    // Na linha de mundo: modo (esquerda), tamanho (meio) e tempo (direita, o mais largo).
    StringDraw(Part(layout, ws::ItemName), row.name);
    StringDrawColor(Part(layout, ws::ItemMode), row.status, kStatusColors[row.kind]);
    StringDraw(Part(layout, ws::ItemSize), row.version);
    StringDraw(Part(layout, ws::ItemTime), row.summary);
}

void DrawList(Il2CppObject* layout) {
    PanelDraw(Part(layout, ws::Backing));
    PanelDraw(Part(layout, ws::MenuDivider));
    Title(Part(layout, ws::Title), h.title);
    const ListArea a = Area(layout);
    const float total = static_cast<float>(g_rows.size()) * a.stride;
    // Só linhas inteiras: a rolagem máxima deixa a última linha encostada embaixo.
    const float gap = a.stride - a.rowSize.y;
    const int visible = a.stride > 0 ? std::max(1, static_cast<int>((a.rect.height + gap) / a.stride)) : 1;
    const float maxScroll = std::max(0.0f, total - static_cast<float>(visible) * a.stride);
    g_listScroll = std::clamp(g_listScroll, 0.0f, maxScroll);
    TLModXnaVector2 at{};
    const bool tapped = TrackTouch(a.rect, &g_listScroll, maxScroll, a.stride, &at);
    const int pressedRow = g_touch.pressed && !g_touch.dragging && a.stride > 0
                               ? static_cast<int>((g_touch.at.y - a.rect.y + g_listScroll) / a.stride)
                               : -1;
    for (Row& row : g_rows) row.drawn = false;
    for (size_t i = 0; i < g_rows.size(); ++i) {
        const float y = static_cast<float>(a.rect.y) + static_cast<float>(i) * a.stride - g_listScroll;
        if (y < a.rect.y - 0.5f || y + a.rowSize.y > a.rect.y + a.rect.height + 0.5f) continue;  // só linhas inteiras
        DrawRow(layout, g_rows[i], {static_cast<float>(a.rect.x), y}, a.rowSize, static_cast<int>(i) == pressedRow);
    }
    if (g_rows.empty()) {
        SetGridItemRegion(a.rect);
        StringDraw(Target(h.line), h.empty);
    }
    if (tapped) {
        for (size_t i = 0; i < g_rows.size(); ++i) {
            if (!g_rows[i].drawn || !Inside(g_rows[i].rect, at.x, at.y)) continue;
            PlaySound(12);
            g_page = Page::Detail;
            g_detail = static_cast<int32_t>(i);
            g_detailScroll = 0;
            g_newScale = 0;
            return;
        }
    }
    if (Button(Part(layout, ws::Back), h.back, &g_backScale) == kInputClicked || BackKey()) {
        PlaySound(11);
        g_page = Page::Closed;
    }
}

// Quebra o texto do mod na largura da área (pelo jogo) e guarda uma string por linha.
void PrepareDetail(const Row& row, float width) {
    if (g_detailFor == g_detail && g_detailRevision == g_revision) return;
    g_detailFor = g_detail;
    g_detailRevision = g_revision;
    Retire(g_detailLines);
    Il2CppObject* wrap = Target(h.wrap);
    FieldAt<float>(wrap, sl::WrapWidth) = width;
    int32_t count = 0;
    Il2CppObject* wrapped = Ptr<WrapFn>(r.wrap)(wrap, Target(row.details), &count, r.wrap);
    std::string text = bridge::Utf8String(wrapped ? wrapped : Target(row.details));
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        g_detailLines.push_back(PinString(text.substr(start, nl - start).c_str()));
        start = nl + 1;
    }
    // Altura de linha da fonte: a diferença entre duas linhas e uma.
    Il2CppObject* line = Target(h.line);
    TLModXnaRectangle one = Ptr<StringRegionFn>(r.stringRegion)(line, Target(h.probe1), r.stringRegion);
    TLModXnaRectangle two = Ptr<StringRegionFn>(r.stringRegion)(line, Target(h.probe2), r.stringRegion);
    g_lineHeight = static_cast<float>(two.height > one.height ? two.height - one.height : one.height);
    if (g_lineHeight <= 0) g_lineHeight = 20.0f;
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
    const ListArea a = Area(layout);
    const TLModXnaRectangle area{a.rect.x + kPad, a.rect.y + kPad / 2, std::max(0, a.rect.width - 2 * kPad),
                                 std::max(0, a.rect.height - kPad)};
    PrepareDetail(row, static_cast<float>(area.width));
    const int visible = std::max(1, static_cast<int>(static_cast<float>(area.height) / g_lineHeight));
    const int lines = static_cast<int>(g_detailLines.size());
    const float maxScroll = static_cast<float>(std::max(0, lines - visible)) * g_lineHeight;
    TrackTouch(a.rect, &g_detailScroll, maxScroll, g_lineHeight, nullptr);
    const int first = static_cast<int>(std::floor(g_detailScroll / g_lineHeight));
    const float offset = g_detailScroll - static_cast<float>(first) * g_lineHeight;
    for (int k = first; k < lines; ++k) {
        const float y = static_cast<float>(area.y) + static_cast<float>(k - first) * g_lineHeight - offset;
        if (y < area.y - 0.5f || y + g_lineHeight > area.y + area.height + 0.5f) continue;  // só linhas inteiras
        SetGridItemRegion({area.x, static_cast<int32_t>(y), area.width, static_cast<int32_t>(g_lineHeight)});
        StringDraw(Target(h.line), g_detailLines[static_cast<size_t>(k)]);
    }
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
    r.stringLayoutClass = api.il2cpp_class_from_name(img, "", "String_Layout");
    if (Il2CppClass* panel = api.il2cpp_class_from_name(img, "", "Panel_Layout"))
        r.panelSize = FieldOffset(panel, "Size", r.panelSize);

    namespace argc = off::argc;
    Il2CppClass* guiPanel = api.il2cpp_class_from_name(img, "", "GUIPanel");
    r.panelDraw = Method(guiPanel, "Draw", argc::GUIPanel_Draw);
    r.panelDrawAt = Method(guiPanel, "DrawAtPosition", argc::GUIPanel_DrawAtPosition);
    r.panelRegion = Method(guiPanel, "Region", argc::GUIPanel_Region);
    Il2CppClass* guiString = api.il2cpp_class_from_name(img, "", "GUIString");
    r.stringDraw = MethodWithParam(guiString, "Draw", argc::GUIString_Draw, 1, "System.String");
    r.stringDrawColor = MethodWithParam(guiString, "Draw", 4, 1, "System.String");
    r.stringRegion = Method(guiString, "GetRegion", argc::GUIString_GetRegion);
    r.wrap = Method(guiString, "ApplyMultiLineAlignment", argc::GUIString_ApplyMultiLineAlignment);
    r.stringButton = Method("", "GUIStringButton", "DrawButton", argc::GUIStringButton_DrawButton);
    r.setGridRegion = Method("", "ControlAnchor", "SetGridItemRegion", argc::ControlAnchor_SetGridItemRegion);
    r.anchored = Method("", "LayoutCalculator", "GetAnchoredPosition", argc::LayoutCalculator_GetAnchoredPosition);
    r.transactionDraw = MethodWithParam(api.il2cpp_class_from_name(img, "", "GUITransactionButton"), "Draw",
                                        argc::GUITransactionButton_Draw, 1, "Terraria.Item");
    Il2CppClass* batch = api.il2cpp_class_from_name(img, "Microsoft.Xna.Framework.Graphics", "SpriteBatch");
    r.begin = Method(batch, "Begin", argc::SpriteBatch_Begin);
    r.end = Method(batch, "End", argc::SpriteBatch_End);
    r.stringCtor = Method(r.stringLayoutClass, ".ctor", 0);
    r.stringCopy = Method(r.stringLayoutClass, "Copy", argc::StringLayout_Copy);
    r.keyUp = Method("", "KeyboardInput", "GetKeyUp", argc::KeyboardInput_GetKeyUp);
    r.mouseX = Method(MainClass(), "get_mouseX", argc::Main_get_mouseX);
    r.mouseY = Method(MainClass(), "get_mouseY", argc::Main_get_mouseY);
    r.mouseLeft = Method(MainClass(), "get_mouseLeft", argc::Main_get_mouseLeft);
    r.uiCursor = Method("Microsoft.Xna.Framework.Input", "Mouse", "GetUICursorPosition", argc::Mouse_GetUICursorPosition);
    r.drawCentred = MethodWithParam(api.il2cpp_class_from_name(img, "", "LayoutCalculator"), "DrawCentred",
                                    argc::LayoutCalculator_DrawCentred, 1, "Microsoft.Xna.Framework.Vector2");

    const void* needed[] = {r.layoutInstance, r.spriteBatch,   r.stringLayoutClass, r.panelDraw,    r.panelDrawAt,
                            r.panelRegion,    r.stringDraw,    r.stringDrawColor,   r.stringRegion, r.wrap,
                            r.stringButton,   r.setGridRegion, r.anchored,          r.transactionDraw, r.begin,
                            r.end,            r.stringCtor,    r.stringCopy,        r.mouseX,       r.mouseY,
                            r.mouseLeft};
    int missing = 0;
    for (const void* p : needed) missing += p == nullptr;
    if (missing) {
        LOGW("Oficina nativa: %d peça(s) da tela de mundos não resolveram; o botão Mods usa o painel", missing);
        return false;
    }
    if (!r.keyUp) LOGW("Oficina nativa: KeyboardInput.GetKeyUp ausente; o voltar do Android não fecha a Oficina");
    if (!r.drawCentred) LOGW("Oficina nativa: LayoutCalculator.DrawCentred ausente; linhas sem ícone");
    if (!r.uiCursor) LOGW("Oficina nativa: Mouse.GetUICursorPosition ausente; toques por Main.mouseX/Y");
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
        g_listScroll = g_detailScroll = 0;
        g_titleScale = g_backScale = 0;
        g_touch = Touch{};
        g_touch.prevDown = true;  // o dedo que tocou em "Mods" ainda pode estar na tela
        Refresh(true);
    }
    if (g_page == Page::Closed) return false;
    Il2CppObject* layout = Layout();
    if (!r.ready || !layout || !EnsureObjects(layout)) {
        g_page = Page::Closed;
        return false;
    }
    Refresh(false);
    RestartBatch();
    const Page before = g_page;
    if (g_page == Page::List) DrawList(layout);
    else DrawDetail(layout);
    if (g_page != before) {  // um toque não atravessa para a página seguinte
        g_touch = Touch{};
        g_touch.prevDown = true;
    }
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

}  // extern "C"
