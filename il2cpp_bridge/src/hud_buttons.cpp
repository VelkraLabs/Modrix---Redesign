// Botões de mods na tela, desenhados pelo próprio jogo (ver bridge.h, TLModHudSource).
//
//   GUIVirtualInputController.DrawControls   hook (screen_hooks.cpp): depois dos
//                                            controles de toque, DrawHudButtons(false)
//   GUIMainMenu.Draw                         hook (menu_hooks.cpp): depois do menu,
//                                            DrawHudButtons(true), só os botões `menu`
//
// Cada botão é uma cópia do layout de uma aba das Configurações (a moldura quadrada
// dos botões do jogo), desenhada pelo GUITransactionButton.Draw(layout, Texture2D, ...)
// com o ícone de um item (DrawItem). O tamanho vem de overloadSize/overloadedSize
// (a região e o fundo seguem esse tamanho); a posição, da Location com âncora Screen /
// TopLeft, convertida de pixels medindo a região em duas posições. A opacidade
// multiplica as cores do layout (o XNA usa alfa pré-multiplicado: os 4 bytes juntos).
//
// O DrawControls só roda sem tela do jogo aberta (GUIVirtualInputController.
// InGameUIOpen: pausa e Configurações, opções, Fancy UI, mapa em tela cheia) e a
// camada dos controles é desenhada antes de Opções, Fancy UI, Inventário e Hotbar
// (Main.SetupDrawInterfaceLayers): as telas do jogo ficam por cima. Com o inventário
// aberto (Main.playerInventory) os botões não são desenhados: as regiões de toque não
// têm ordem, e um toque no inventário valeria também para o botão embaixo dele.
//
// Na thread do jogo. O frame dos hooks só tem PODs; o resto fica em funções noinline.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "bridge.h"
#include "bridge_internal.h"
#include "draw_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

namespace {

namespace off = terraria;
namespace tbl = terraria::transaction_button_layout_off;

typedef int32_t (*TextureDrawFn)(Il2CppObject* layout, Il2CppObject* texture, Il2CppObject* label, bool disabled,
                                 float* scale, uint64_t overloadedItemColour, bool forcedPressed,
                                 bool hasControllerFocus, bool forceOver, bool disablePressedState,
                                 const MethodInfo* method);
typedef TLModXnaRectangle (*RegionFn)(Il2CppObject* layout, const MethodInfo* method);
typedef TLModXnaRectangle (*ControlRegionFn)(int32_t controlId, const MethodInfo* method);
typedef bool (*StaticBoolFn)(const MethodInfo* method);
typedef void (*SetBoolFn)(bool value, const MethodInfo* method);

constexpr int32_t kInputClicked = 0;     // GUITransactionButton.InputState.Clicked
constexpr int32_t kTapSound = 12;        // o som das abas e dos botões do jogo
constexpr int32_t kAnchorScreen = 0;     // ControlAnchor.ControlId.Screen
constexpr int32_t kAnchorTopLeft = 9;    // LayoutCalculator.AnchorType.TopLeft
constexpr float kProbe = 100.0f;         // unidades de Location entre as duas medidas
constexpr float kPlacementMax = 10000.0f;
// Primeiro botão sem posição salva: centro em 4% da largura e 24% da altura da tela (onde o
// "M" do overlay ficava); os outros em coluna abaixo dele, com 1/5 do botão entre eles.
constexpr float kDefaultX = 0.04f, kDefaultY = 0.24f, kGap = 0.2f;
constexpr float kIconFill = 32.0f / 52.0f;  // o item no slot do inventário: 32 de 52

// As cores de TransactionButton_Layout (e das bases) que a opacidade multiplica.
const char* const kColourFields[] = {
    "Color", "DisabledColour", "PressedColour", "IconColour", "DisabledIconColour", "PressedIconColour",
    "LabelHighlightTint", "BackingPressedHighlightColour", "DisabledLabelColour", "PressedLabelColour",
    "ItemColour", "DisabledItemColour", "PressedItemColour", "HighlightDisabledColour", "HighlightColour",
    "HighlightPressedColour", "DividerHighlightColour", "DividerHighlightBorderColour", "DividerPressedColour",
    "DividerPressedBorderColour", "DividerPressedHighlightColour", "DividerPressedHighlightBorderColour",
    "DividerDisabledColour", "DividerDisabledBorderColour", "DividerDisabledHighlightColour",
    "DividerDisabledHighlightBorderColour", "DisabledBorderColour", "NormalBorderColour", "PressedBorderColour"};
constexpr size_t kColourCount = sizeof(kColourFields) / sizeof(kColourFields[0]);

struct Resolved {
    bool ready = false;
    FieldInfo* layoutInstance = nullptr;  // static SettingsOverlay_Layout.Instance
    size_t categories = off::settings_overlay_layout_off::Categories;
    size_t templateTab = off::settings_categories_off::Sound;
    size_t anchorControl = off::button_layout_off::AnchorControl;
    size_t anchor = off::button_layout_off::Anchor;
    size_t location = off::button_layout_off::Location;
    size_t drawIcon = tbl::DrawIcon, drawItem = tbl::DrawItem, iconOffset = tbl::IconOffset;
    size_t disableItemResize = tbl::DisableItemResize, forceItemScale = tbl::ForceItemScale;
    size_t itemOffset = tbl::ItemOffset, forcedItemScale = tbl::ForcedItemScale;
    size_t overloadSize = tbl::overloadSize, overloadedSize = tbl::overloadedSize;
    size_t layoutEnd = tbl::PressedBorderColour + 4;
    size_t colours[kColourCount] = {};
    size_t colourCount = 0;
    const MethodInfo* textureDraw = nullptr;
    const MethodInfo* buttonRegion = nullptr;
    const MethodInfo* controlRegion = nullptr;
    const MethodInfo* playerInventory = nullptr;  // static bool Main.get_playerInventory()
    const MethodInfo* setMouseLeftRelease = nullptr;  // static Main.set_mouseLeftRelease(bool): consome o toque
    const MethodInfo* playSound = nullptr;
};
Resolved r;

std::atomic<const TLModHudSource*> g_source{nullptr};
std::atomic<bool> g_broken{false};

// ---- Só na thread do jogo -------------------------------------------------------
struct Button {
    std::string id;
    uint32_t layout = 0;  // GC handle da cópia do layout
    float scale = 0.0f;   // animação do GUITransactionButton
    bool clicked = false; // Clicked no frame anterior (um toque vira um clique só)
    int32_t opacity = -1; // opacidade aplicada às cores da cópia
};
std::vector<Button> g_buttons;
uint32_t g_emptyHandle = 0;
uint8_t g_templateColours[kColourCount][4] = {};
bool g_templateSaved = false;
// Medida do template e conversão pixels <-> Location, refeitas quando a tela ou o tamanho mudam.
struct Calibration {
    int32_t screenW = -1, screenH = -1, size = -1;
    float kx = 0, ky = 0;      // pixels por unidade de Location
    float cx0 = 0, cy0 = 0;    // centro da região com Location (0, 0)
    float base = 0;            // lado do botão do jogo a 100%, em pixels da interface
    TLModXnaVector2 middle{};  // meio da região, a partir do canto (o ItemOffset que centraliza o ícone)
    bool ok = false;
};
Calibration g_cal;
bool g_loggedPlace = false;
int g_tapsLogged = 0;
constexpr int kTapsToLog = 10;  // os primeiros toques vão para o log
int g_missingTemplate = 0;
constexpr int kTemplateWaitFrames = 600;  // ~10 s a 60 fps

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
const MethodInfo* Method(Il2CppClass* k, const char* name, int argc) {
    return k ? api.il2cpp_class_get_method_from_name(k, name, argc) : nullptr;
}
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
TLModXnaRectangle Region(Il2CppObject* layout) { return Ptr<RegionFn>(r.buttonRegion)(layout, r.buttonRegion); }
void Place(Il2CppObject* l, float x, float y) {
    FieldAt<float>(l, r.location) = x;
    FieldAt<float>(l, r.location + 4) = y;
}
void SetSize(Il2CppObject* l, float side) {
    FieldAt<bool>(l, r.overloadSize) = side > 0;
    FieldAt<float>(l, r.overloadedSize) = side;
    FieldAt<float>(l, r.overloadedSize + 4) = side;
}

Il2CppObject* Template() {
    Il2CppObject* layout = StaticObject(r.layoutInstance);
    Il2CppObject* cats = layout ? FieldAt<Il2CppObject*>(layout, r.categories) : nullptr;
    return cats ? FieldAt<Il2CppObject*>(cats, r.templateTab) : nullptr;
}

// Cópia rasa do layout da aba, com âncora no canto da tela e o ícone pelo item.
Il2CppObject* CloneTemplate(Il2CppObject* source) {
    Il2CppObject* copy = api.il2cpp_object_new(api.il2cpp_object_get_class(source));
    if (!copy) return nullptr;
    constexpr size_t kHeader = 2 * sizeof(void*);  // klass e monitor: os campos começam em 0x10
    std::memcpy(reinterpret_cast<uint8_t*>(copy) + kHeader, reinterpret_cast<uint8_t*>(source) + kHeader,
                r.layoutEnd - kHeader);
    FieldAt<int32_t>(copy, r.anchorControl) = kAnchorScreen;
    FieldAt<int32_t>(copy, r.anchor) = kAnchorTopLeft;
    FieldAt<bool>(copy, r.drawIcon) = false;
    FieldAt<bool>(copy, r.drawItem) = false;  // ligado no desenho, quando a textura existe
    // O jogo põe o centro do item a ItemOffset do canto da região, e o encolhe para caber no
    // fundo da aba menos o ItemOffset. Com o botão quadrado (e não a aba 46x38), a ponte dá o
    // meio da região e a escala do item (Calibrate e Draw).
    FieldAt<bool>(copy, r.forceItemScale) = true;
    FieldAt<bool>(copy, r.disableItemResize) = true;
    return copy;
}

void ApplyOpacity(Il2CppObject* l, int32_t pct) {
    const uint32_t a = static_cast<uint32_t>(std::clamp(pct, 0, 100));
    for (size_t i = 0; i < r.colourCount; ++i) {
        uint8_t* c = &FieldAt<uint8_t>(l, r.colours[i]);
        for (int b = 0; b < 4; ++b) c[b] = static_cast<uint8_t>(g_templateColours[i][b] * a / 100);
    }
}

// Lado a 100% (a aba do jogo, sem overload) e Location -> pixels, numa cópia de medida.
__attribute__((noinline)) bool Calibrate(Il2CppObject* tmpl, const TLModXnaRectangle& screen, int32_t side) {
    Il2CppObject* probe = CloneTemplate(tmpl);
    if (!probe) return false;
    SetSize(probe, 0.0f);
    Place(probe, 0.0f, 0.0f);
    const TLModXnaRectangle natural = Region(probe);
    g_cal.base = static_cast<float>(std::max(natural.width, natural.height));
    const float px = side > 0 ? static_cast<float>(side) : g_cal.base;
    SetSize(probe, px);
    const TLModXnaRectangle a = Region(probe);
    Place(probe, kProbe, kProbe);
    const TLModXnaRectangle b = Region(probe);
    g_cal.kx = static_cast<float>(b.x - a.x) / kProbe;
    g_cal.ky = static_cast<float>(b.y - a.y) / kProbe;
    g_cal.middle = {0.5f * static_cast<float>(a.width), 0.5f * static_cast<float>(a.height)};
    g_cal.cx0 = static_cast<float>(a.x) + g_cal.middle.x;
    g_cal.cy0 = static_cast<float>(a.y) + g_cal.middle.y;
    g_cal.screenW = screen.width;
    g_cal.screenH = screen.height;
    g_cal.ok = g_cal.kx > 0 && g_cal.ky > 0 && g_cal.base > 0;
    LOGI("botões de mods: tela %dx%d; botão do jogo %dx%d; %.2f x %.2f px por unidade; %s", screen.width,
         screen.height, natural.width, natural.height, g_cal.kx, g_cal.ky, g_cal.ok ? "ok" : "sem medida");
    return g_cal.ok;
}

Button& ButtonFor(const char* id) {
    for (Button& b : g_buttons)
        if (b.id == id) return b;
    g_buttons.push_back(Button{id ? id : ""});
    return g_buttons.back();
}

__attribute__((noinline)) void PlaySound(int32_t type) {
    if (!r.playSound) return;
    int32_t x = -1, y = -1, style = 1;
    float volume = 1.0f, pitch = 0.0f;
    void* params[] = {&type, &x, &y, &style, &volume, &pitch};
    Il2CppException* exc = nullptr;
    api.il2cpp_runtime_invoke(r.playSound, nullptr, params, &exc);
}

__attribute__((noinline)) void Draw(bool menu) {
    const TLModHudSource* src = g_source.load(std::memory_order_acquire);
    if (!src || !src->snapshot || !src->button) return;
    if (!menu && r.playerInventory && Ptr<StaticBoolFn>(r.playerInventory)(r.playerInventory)) return;
    TLModHudLayout layout{100, 100};
    const int32_t count = src->snapshot(src->user, &layout);
    if (count <= 0) return;
    Il2CppObject* tmpl = Template();
    if (!tmpl) {
        // Os layouts sobem com a interface: um pouco de espera é normal; muito, desiste.
        if (++g_missingTemplate == kTemplateWaitFrames) {
            g_broken.store(true, std::memory_order_release);
            LOGW("botões de mods: SettingsOverlay_Layout não apareceu em %d frames; botões pelo overlay",
                 kTemplateWaitFrames);
        }
        return;
    }
    if (!g_templateSaved) {
        for (size_t i = 0; i < r.colourCount; ++i) std::memcpy(g_templateColours[i], &FieldAt<uint8_t>(tmpl, r.colours[i]), 4);
        g_templateSaved = true;
    }
    const TLModXnaRectangle screen = Ptr<ControlRegionFn>(r.controlRegion)(0, r.controlRegion);
    if (screen.width <= 0 || screen.height <= 0) return;
    if (screen.width != g_cal.screenW || screen.height != g_cal.screenH || g_cal.base <= 0) Calibrate(tmpl, screen, 0);
    if (!g_cal.ok) return;
    const float side = std::max(8.0f, std::round(g_cal.base * static_cast<float>(layout.scalePct) / 100.0f));
    if (static_cast<int32_t>(side) != g_cal.size) {
        Calibrate(tmpl, screen, static_cast<int32_t>(side));  // o centro com Location (0, 0) muda com o tamanho
        g_cal.size = static_cast<int32_t>(side);
    }
    const float W = static_cast<float>(screen.width), H = static_cast<float>(screen.height);
    const float half = 0.5f * side;
    float colX = static_cast<float>(screen.x) + kDefaultX * W, colY = static_cast<float>(screen.y) + kDefaultY * H;
    bool first = true;
    if (!g_emptyHandle) {
        Il2CppObject* empty = api.il2cpp_string_new("");
        g_emptyHandle = empty ? api.il2cpp_gchandle_new(empty, false) : 0;
    }
    const uint64_t a = static_cast<uint64_t>(std::clamp(layout.opacityPct, 0, 100)) * 255 / 100;
    const uint64_t colour = a | (a << 8) | (a << 16) | (a << 24) | (1ull << 32);  // Color? pré-multiplicada, com valor
    for (int32_t i = 0; i < count; ++i) {
        TLModHudButton b{};
        if (!src->button(src->user, i, &b) || !b.id || (menu && !b.menu)) continue;
        // Centro: a posição salva ou a coluna padrão (o primeiro botão a abre; os outros a seguem).
        float cx, cy;
        if (b.placed) {
            cx = static_cast<float>(screen.x) + static_cast<float>(b.x) / kPlacementMax * W;
            cy = static_cast<float>(screen.y) + static_cast<float>(b.y) / kPlacementMax * H;
            if (first) colX = cx, colY = cy;
        } else {
            if (!first) colY += side * (1.0f + kGap);
            cx = colX, cy = colY;
        }
        first = false;
        cx = std::clamp(cx, static_cast<float>(screen.x) + half, static_cast<float>(screen.x) + W - half);
        cy = std::clamp(cy, static_cast<float>(screen.y) + half, static_cast<float>(screen.y) + H - half);
        Button& btn = ButtonFor(b.id);
        Il2CppObject* l = Target(btn.layout);
        if (!l) {
            l = CloneTemplate(tmpl);
            btn.layout = l ? api.il2cpp_gchandle_new(l, false) : 0;
            btn.opacity = -1;
            if (!l) continue;
        }
        if (btn.opacity != layout.opacityPct) {
            ApplyOpacity(l, layout.opacityPct);
            btn.opacity = layout.opacityPct;
        }
        SetSize(l, side);
        Place(l, (cx - g_cal.cx0) / g_cal.kx, (cy - g_cal.cy0) / g_cal.ky);
        int32_t w = 0, h = 0;
        Il2CppObject* texture = b.iconItem > 0 ? drawx::ItemIconTexture(b.iconItem, &w, &h) : nullptr;
        FieldAt<bool>(l, r.drawItem) = texture != nullptr;
        // O item no meio do botão, no tamanho do inventário: até kIconFill do lado, sem
        // passar do tamanho natural vezes o tamanho escolhido (arte em pixel não cresce à toa).
        FieldAt<TLModXnaVector2>(l, r.itemOffset) = g_cal.middle;
        const float longest = static_cast<float>(std::max(w, h));
        FieldAt<float>(l, r.forcedItemScale) =
            longest > 0 ? std::min(kIconFill * side / longest, side / g_cal.base) : 1.0f;
        const int32_t state = Ptr<TextureDrawFn>(r.textureDraw)(l, texture, Target(g_emptyHandle), false, &btn.scale,
                                                                colour, b.active, false, false, false, r.textureDraw);
        if (!g_loggedPlace) {
            const TLModXnaRectangle q = Region(l);
            LOGI("botões de mods: %s em (%d, %d) %dx%d", b.id, q.x, q.y, q.width, q.height);
        }
        // Um toque = um clique: Main.mouseLeftRelease só volta a false quando quem trata o
        // clique o apaga (o jogo faz isso depois de cada botão). Sem isso, com o dedo na tela,
        // o botão "clica" em todo frame e a tela abre e fecha sem parar. A borda (Clicked agora
        // e não no frame anterior) fica como segunda trava.
        const bool clicked = state == kInputClicked;
        if (clicked && !btn.clicked) {
            if (r.setMouseLeftRelease) Ptr<SetBoolFn>(r.setMouseLeftRelease)(false, r.setMouseLeftRelease);
            PlaySound(kTapSound);
            if (g_tapsLogged < kTapsToLog) {
                ++g_tapsLogged;
                LOGI("botões de mods: toque em %s", b.id);
            }
            if (src->tap) src->tap(src->user, b.id);
        }
        btn.clicked = clicked;
    }
    g_loggedPlace = true;
}

}  // namespace

namespace bridge {

void ResolveHud() {
    const Il2CppImage* img = Image();
    Il2CppClass* layout = api.il2cpp_class_from_name(img, "", off::kSettingsOverlayLayoutClass);
    Il2CppClass* cats = api.il2cpp_class_from_name(img, "", "SettingsOverlayCategories_Layout");
    Il2CppClass* tb = api.il2cpp_class_from_name(img, "", "TransactionButton_Layout");
    if (layout) {
        r.layoutInstance = api.il2cpp_class_get_field_from_name(layout, "Instance");
        r.categories = FieldOffset(layout, "Categories", r.categories);
    }
    if (cats) r.templateTab = FieldOffset(cats, "Sound", r.templateTab);
    if (Il2CppClass* button = api.il2cpp_class_from_name(img, "", "Button_Layout")) {
        r.anchorControl = FieldOffset(button, "AnchorControl", r.anchorControl);
        r.anchor = FieldOffset(button, "Anchor", r.anchor);
        r.location = FieldOffset(button, "Location", r.location);
    }
    if (tb) {
        r.drawIcon = FieldOffset(tb, "DrawIcon", r.drawIcon);
        r.drawItem = FieldOffset(tb, "DrawItem", r.drawItem);
        r.iconOffset = FieldOffset(tb, "IconOffset", r.iconOffset);
        r.disableItemResize = FieldOffset(tb, "DisableItemResize", r.disableItemResize);
        r.forceItemScale = FieldOffset(tb, "ForceItemScale", r.forceItemScale);
        r.forcedItemScale = FieldOffset(tb, "ForcedItemScale", r.forcedItemScale);
        r.itemOffset = FieldOffset(tb, "ItemOffset", r.itemOffset);
        r.overloadSize = FieldOffset(tb, "overloadSize", r.overloadSize);
        r.overloadedSize = FieldOffset(tb, "overloadedSize", r.overloadedSize);
        r.layoutEnd = FieldOffset(tb, "PressedBorderColour", r.layoutEnd - 4) + 4;
        // As cores, pelo nome (nas bases também); as que faltarem ficam sem opacidade.
        for (const char* name : kColourFields)
            if (FieldInfo* f = api.il2cpp_class_get_field_from_name(tb, name))
                r.colours[r.colourCount++] = api.il2cpp_field_get_offset(f);
    }
    Il2CppClass* transaction = api.il2cpp_class_from_name(img, "", "GUITransactionButton");
    r.textureDraw = MethodWithParamSuffix(transaction, "Draw", off::argc::GUITransactionButton_DrawTexture, 1, "Texture2D");
    r.buttonRegion = Method(transaction, "GetRegion", off::argc::GUITransactionButton_GetRegion);
    r.controlRegion = Method(api.il2cpp_class_from_name(img, "", "ControlAnchor"), "GetControlRegion",
                             off::argc::ControlAnchor_GetControlRegion);
    r.playerInventory = Method(MainClass(), "get_playerInventory", off::argc::Main_get_playerInventory);
    r.setMouseLeftRelease = Method(MainClass(), "set_mouseLeftRelease", off::argc::Main_set_mouseLeftRelease);
    if (!r.setMouseLeftRelease) LOGW("botões de mods: Main.set_mouseLeftRelease ausente; o toque não é consumido");
    r.playSound = Method(api.il2cpp_class_from_name(img, "Terraria.Audio", "SoundEngine"), "PlaySound",
                         off::argc::SoundEngine_PlaySound);
    const void* needed[] = {r.layoutInstance, tb, r.textureDraw, r.buttonRegion, r.controlRegion};
    int missing = 0;
    for (const void* p : needed) missing += p == nullptr;
    if (!VirtualControlsHooked()) {
        LOGW("botões de mods: sem o hook de GUIVirtualInputController.DrawControls; botões pelo overlay");
        return;
    }
    if (missing) {
        LOGW("botões de mods: %d peça(s) do jogo não resolveram; botões pelo overlay", missing);
        return;
    }
    if (!r.playerInventory) LOGW("botões de mods: Main.get_playerInventory ausente; botões também com o inventário aberto");
    if (r.colourCount < kColourCount)
        LOGW("botões de mods: %zu de %zu cores do layout encontradas", r.colourCount, kColourCount);
    r.ready = true;
    LOGI("botões de mods: desenhados pelo jogo (%zu cores para a opacidade)", r.colourCount);
}

void DrawHudButtons(bool menu) {
    if (!r.ready || g_broken.load(std::memory_order_relaxed)) return;
    Draw(menu);
}

}  // namespace bridge

extern "C" {

void TLMod_SetHudSource(const TLModHudSource* source) {
    g_source.store(source ? new TLModHudSource(*source) : nullptr, std::memory_order_release);
}

bool TLMod_HudAvailable(void) { return r.ready && !g_broken.load(std::memory_order_acquire); }

}  // extern "C"
