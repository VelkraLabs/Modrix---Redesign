// Botão "Mods" no menu principal, desenhado pelo próprio jogo: depois do
// GUIMainMenu.Draw original, o mesmo GUITransactionButton.Draw dos outros
// botões, com o layout de "Comprar o jogo completo" (MainMenu_Layout.
// PurchaseFullGame). Esse botão só existe na versão de teste (Main.TrialMode);
// fora dela a posição fica livre. O clique toca o som do menu e chama o
// callback de TLMod_SetMainMenuButton (a Oficina de Mods do overlay).
//
// O frame do hook só tem PODs (exceções do jogo passam direto); o que usa
// objetos C++ fica em funções noinline.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

typedef void (*MainMenuDrawFn)(Il2CppObject* self, const MethodInfo* method);
typedef TLModXnaRectangle (*ButtonRegionFn)(Il2CppObject* layout, const MethodInfo* method);
typedef TLModXnaRectangle (*ControlRegionFn)(int32_t controlId, const MethodInfo* method);
typedef int32_t (*TransactionDrawFn)(Il2CppObject* layout, Il2CppObject* item, Il2CppObject* label, bool disabled,
                                     float* scale, bool forcedPressed, bool hasControllerFocus, bool forceOver,
                                     bool disablePressedState, const MethodInfo* method);

namespace {

namespace off = terraria;

constexpr int32_t kInputClicked = 0;  // GUITransactionButton.InputState.Clicked
constexpr int32_t kMenuSound = 10;    // o som dos botões do menu (SoundEngine.PlaySound(10, -1, -1, 1, 1, 0))

struct MenuResolved {
    FieldInfo* layoutInstance = nullptr;  // static MainMenu_Layout.Instance
    FieldInfo* trialMode = nullptr;       // static bool Main.TrialMode
    FieldInfo* guiActive = nullptr;       // static GUIInstance.Active
    size_t guiSettings = off::gui_instance_off::GUISettings;
    size_t settingsVisible = off::gui_settings_off::Visible;
    size_t purchase = off::main_menu_layout_off::PurchaseFullGame;
    size_t anchorControl = off::button_layout_off::AnchorControl;
    size_t anchor = off::button_layout_off::Anchor;
    size_t location = off::button_layout_off::Location;
    const MethodInfo* transactionDraw = nullptr;  // GUITransactionButton.Draw(layout, Item, string, ...)
    const MethodInfo* playSound = nullptr;        // static SoundEngine.PlaySound(int, int, int, int, float, float)
    const MethodInfo* buttonRegion = nullptr;     // static Rectangle GUITransactionButton.GetRegion(layout): pixels
    // static Rectangle ControlAnchor.GetControlRegion(ControlId): a tela da interface
    // (Screen = 0), no mesmo espaço das regiões dos botões e das Location.
    const MethodInfo* controlRegion = nullptr;
};
MenuResolved m;

// Os botões do jogo, para o log e para conferir que a posição está livre.
struct GameButton {
    const char* name;
    size_t offset;
};
GameButton g_gameButtons[] = {
    {"Singleplayer", off::main_menu_layout_off::Singleplayer}, {"Multiplayer", off::main_menu_layout_off::Multiplayer},
    {"Achievements", off::main_menu_layout_off::Achievements}, {"Settings", off::main_menu_layout_off::Settings},
    {"Tutorial", off::main_menu_layout_off::Tutorial},         {"Credits", off::main_menu_layout_off::Credits},
    {"PlayerProfile", off::main_menu_layout_off::PlayerProfile},
};

void* g_origMainMenuDraw = nullptr;

// Publicado por TLMod_SetMainMenuButton (qualquer thread); cópia imutável, as
// antigas não são liberadas (o desenho pode estar lendo).
struct MenuButton {
    std::string label;
    TLModMenuButtonCallback onClick = nullptr;
};
std::atomic<const MenuButton*> g_button{nullptr};

// Só na thread do jogo (dentro do desenho do menu).
float g_scale = 0.0f;           // animação do botão (como os scale0..5 do GUIMainMenu)
const MenuButton* g_labelFor = nullptr;
uint32_t g_labelHandle = 0;     // GC handle da string do rótulo
int32_t g_arrangedW = -1, g_arrangedH = -1;  // tela para a qual a coluna foi arrumada
bool g_blocked = false;         // sem lugar para o botão Mods nesta tela

// Posições originais (Location) da coluna do menu e do PurchaseFullGame: a
// arrumação parte sempre delas (a tela pode mudar de tamanho).
struct SavedPlace {
    Il2CppObject* layout;
    const char* name;
    float x, y;
};
std::vector<SavedPlace> g_column;
SavedPlace g_modsPlace{};
bool g_saved = false;

template <typename Fn>
Fn LoadOrig(void* const& slot) {
    return reinterpret_cast<Fn>(__atomic_load_n(&slot, __ATOMIC_ACQUIRE));
}

Il2CppObject* StaticObject(FieldInfo* f) {
    Il2CppObject* obj = nullptr;
    if (f) api.il2cpp_field_static_get_value(f, &obj);
    return obj;
}

bool SamePlace(Il2CppObject* a, Il2CppObject* b) {
    return FieldAt<int32_t>(a, m.anchorControl) == FieldAt<int32_t>(b, m.anchorControl) &&
           FieldAt<int32_t>(a, m.anchor) == FieldAt<int32_t>(b, m.anchor) &&
           std::fabs(FieldAt<float>(a, m.location) - FieldAt<float>(b, m.location)) < 1.0f &&
           std::fabs(FieldAt<float>(a, m.location + 4) - FieldAt<float>(b, m.location + 4)) < 1.0f;
}

TLModXnaRectangle Region(Il2CppObject* layout) {
    return reinterpret_cast<ButtonRegionFn>(m.buttonRegion->methodPointer)(layout, m.buttonRegion);
}
TLModXnaRectangle ScreenRegion() {
    return reinterpret_cast<ControlRegionFn>(m.controlRegion->methodPointer)(0, m.controlRegion);
}
void Place(Il2CppObject* b, float x, float y) {
    FieldAt<float>(b, m.location) = x;
    FieldAt<float>(b, m.location + 4) = y;
}

// A coluna do menu: os botões do jogo com a mesma âncora e o mesmo X do
// Singleplayer, de cima para baixo. Guardada uma vez, com as posições originais.
void SaveColumn(Il2CppObject* layout, Il2CppObject* mods) {
    g_saved = true;
    Il2CppObject* first = FieldAt<Il2CppObject*>(layout, g_gameButtons[0].offset);
    for (const GameButton& gb : g_gameButtons) {
        Il2CppObject* b = FieldAt<Il2CppObject*>(layout, gb.offset);
        if (!b || !first || FieldAt<int32_t>(b, m.anchor) != FieldAt<int32_t>(first, m.anchor) ||
            FieldAt<int32_t>(b, m.anchorControl) != FieldAt<int32_t>(first, m.anchorControl) ||
            std::fabs(FieldAt<float>(b, m.location) - FieldAt<float>(first, m.location)) >= 1.0f)
            continue;
        g_column.push_back({b, gb.name, FieldAt<float>(b, m.location), FieldAt<float>(b, m.location + 4)});
    }
    std::sort(g_column.begin(), g_column.end(), [](const SavedPlace& a, const SavedPlace& b) { return a.y < b.y; });
    g_modsPlace = {mods, "Mods", FieldAt<float>(mods, m.location), FieldAt<float>(mods, m.location + 4)};
}

std::string Describe(const SavedPlace& p) {
    TLModXnaRectangle r = Region(p.layout);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s (%.0f, %.0f) = %dx%d em (%d, %d)", p.name, FieldAt<float>(p.layout, m.location),
                  FieldAt<float>(p.layout, m.location + 4), r.width, r.height, r.x, r.y);
    return buf;
}

// Põe o botão Mods onde caiba, conferindo em pixels (GUITransactionButton.GetRegion
// e a região Screen do ControlAnchor). Em ordem de preferência:
//   1. a posição do PurchaseFullGame (uma linha abaixo da coluna), se couber;
//   2. a coluna inteira reespaçada por igual, com o topo no lugar, se os 7 botões
//      couberem sem se sobrepor;
//   3. ao lado do último botão da coluna.
// Refeito quando o tamanho da tela muda; o log registra a decisão.
__attribute__((noinline)) void Arrange(Il2CppObject* layout, Il2CppObject* mods, const TLModXnaRectangle& screen) {
    if (!g_saved) SaveColumn(layout, mods);
    g_arrangedW = screen.width;
    g_arrangedH = screen.height;
    const int32_t screenW = screen.width, screenH = screen.height;
    g_blocked = false;
    const int count = static_cast<int>(g_column.size());
    for (int i = 0; i < count; ++i) Place(g_column[i].layout, g_column[i].x, g_column[i].y);
    Place(mods, g_modsPlace.x, g_modsPlace.y);
    const int32_t limit = screen.y + screenH - screenH / 50;  // 2% de margem embaixo
    std::string column;
    for (int i = 0; i < count; ++i) column += std::string(i ? "; " : "") + Describe(g_column[i]);
    LOGI("menu: tela %dx%d; coluna: %s", screenW, screenH, column.c_str());
    for (int i = 0; i < count; ++i)
        if (SamePlace(g_column[i].layout, mods)) {
            g_blocked = true;
            LOGW("menu: a posição de PurchaseFullGame coincide com %s; botão Mods não desenhado", g_column[i].name);
            return;
        }
    TLModXnaRectangle r = Region(mods);
    if (r.y + r.height <= limit || count < 2) {
        LOGI("menu: botão Mods na posição de PurchaseFullGame: %s", Describe(g_modsPlace).c_str());
        return;
    }
    // Pixels por unidade de layout, medidos entre os dois primeiros botões.
    const SavedPlace& top = g_column[0];
    const TLModXnaRectangle r0 = Region(top.layout), r1 = Region(g_column[1].layout);
    const float units = g_column[1].y - top.y;
    const float k = units != 0.0f ? static_cast<float>(r1.y - r0.y) / units : 0.0f;
    const int n = count + 1;
    const float spacingPx = static_cast<float>(limit - r0.y - r.height) / static_cast<float>(n - 1);
    if (k > 0.0f && spacingPx >= static_cast<float>(r.height) + 2.0f) {
        const float step = spacingPx / k;
        for (int i = 0; i < count; ++i)
            Place(g_column[i].layout, g_column[i].x, top.y + step * static_cast<float>(i));
        Place(mods, top.x, top.y + step * static_cast<float>(count));
        LOGI("menu: a coluna não cabia com o botão Mods embaixo; botões reespaçados de %.0f em %.0f: %s", units, step,
             Describe(g_modsPlace).c_str());
        return;
    }
    // Ao lado do último botão da coluna, com o mesmo vão que há entre as linhas.
    const SavedPlace& last = g_column[count - 1];
    const float gapPx = static_cast<float>(r1.y - r0.y - r0.height);
    const float dx = k > 0.0f ? (static_cast<float>(r0.width) + std::max(gapPx, 8.0f)) / k : 0.0f;
    Place(mods, last.x + dx, last.y);
    r = Region(mods);
    if (dx <= 0.0f || r.x + r.width > screen.x + screenW) {
        g_blocked = true;
        LOGW("menu: sem lugar para o botão Mods nesta tela (%dx%d); botão não desenhado", screenW, screenH);
        return;
    }
    LOGI("menu: sem altura para 7 botões; botão Mods ao lado de %s: %s", last.name, Describe(g_modsPlace).c_str());
}

// String gerenciada do rótulo, criada na thread do jogo e presa por GC handle.
__attribute__((noinline)) Il2CppObject* Label(const MenuButton* btn) {
    if (btn != g_labelFor) {
        // O handle anterior fica (a API resolvida não tem gchandle_free): o
        // rótulo só muda se o runtime trocar o botão, o que é raro.
        Il2CppObject* str = api.il2cpp_string_new(btn->label.c_str());
        g_labelHandle = str ? api.il2cpp_gchandle_new(str, false) : 0;
        g_labelFor = btn;
    }
    return g_labelHandle ? api.il2cpp_gchandle_get_target(g_labelHandle) : nullptr;
}

__attribute__((noinline)) void PlayMenuSound() {
    if (!m.playSound) return;
    int32_t type = kMenuSound, x = -1, y = -1, style = 1;
    float volume = 1.0f, pitch = 0.0f;
    void* params[] = {&type, &x, &y, &style, &volume, &pitch};
    Il2CppException* exc = nullptr;
    api.il2cpp_runtime_invoke(m.playSound, nullptr, params, &exc);
    if (exc) bridge::LogException("SoundEngine.PlaySound", exc);
}

// Configurações abertas por cima do menu (GUIInstance.Active.GUISettings.Visible).
bool SettingsOpen() {
    Il2CppObject* gui = StaticObject(m.guiActive);
    Il2CppObject* settings = gui ? FieldAt<Il2CppObject*>(gui, m.guiSettings) : nullptr;
    return settings && FieldAt<bool>(settings, m.settingsVisible);
}

// Depois do menu original. Só PODs no frame: GUITransactionButton.Draw é
// chamado direto, como o jogo faz.
void DrawModsButton() {
    const MenuButton* btn = g_button.load(std::memory_order_acquire);
    if (!btn || !btn->onClick || !m.transactionDraw) return;
    bool trial = false;
    if (m.trialMode) api.il2cpp_field_static_get_value(m.trialMode, &trial);
    if (trial) return;  // o jogo desenha "Comprar o jogo completo" nesse lugar
    Il2CppObject* layout = StaticObject(m.layoutInstance);
    Il2CppObject* mods = layout ? FieldAt<Il2CppObject*>(layout, m.purchase) : nullptr;
    if (!mods) return;
    if (m.buttonRegion && m.controlRegion) {
        const TLModXnaRectangle screen = ScreenRegion();
        if (screen.height > 0 && (screen.width != g_arrangedW || screen.height != g_arrangedH))
            Arrange(layout, mods, screen);
    }
    if (g_blocked) return;
    Il2CppObject* label = Label(btn);
    if (!label) return;
    // Com Configurações aberta por cima, os botões do menu ficam desativados.
    const bool disabled = SettingsOpen();
    auto draw = reinterpret_cast<TransactionDrawFn>(m.transactionDraw->methodPointer);
    int32_t state = draw(mods, nullptr, label, disabled, &g_scale, false, false, false, false, m.transactionDraw);
    if (state == kInputClicked && !disabled) {
        PlayMenuSound();
        btn->onClick();
    }
}

}  // namespace

namespace bridge {

void InstallMenuHooks() {
    const Il2CppImage* img = Image();
    Il2CppClass* layout = api.il2cpp_class_from_name(img, "", off::kMainMenuLayoutClass);
    if (layout) {
        m.layoutInstance = api.il2cpp_class_get_field_from_name(layout, "Instance");
        m.purchase = FieldOffset(layout, "PurchaseFullGame", m.purchase);
        for (GameButton& gb : g_gameButtons) gb.offset = FieldOffset(layout, gb.name, gb.offset);
    }
    if (Il2CppClass* button = api.il2cpp_class_from_name(img, "", "Button_Layout")) {
        m.anchorControl = FieldOffset(button, "AnchorControl", m.anchorControl);
        m.anchor = FieldOffset(button, "Anchor", m.anchor);
        m.location = FieldOffset(button, "Location", m.location);
    }
    m.trialMode = api.il2cpp_class_get_field_from_name(MainClass(), "TrialMode");
    if (Il2CppClass* gui = api.il2cpp_class_from_name(img, "", off::kGuiInstanceClass)) {
        m.guiActive = api.il2cpp_class_get_field_from_name(gui, "Active");
        m.guiSettings = FieldOffset(gui, "GUISettings", m.guiSettings);
    }
    if (Il2CppClass* settings = api.il2cpp_class_from_name(img, "", "GUISettings"))
        m.settingsVisible = FieldOffset(settings, "Visible", m.settingsVisible);
    if (Il2CppClass* tb = api.il2cpp_class_from_name(img, "", "GUITransactionButton")) {
        m.transactionDraw = api.il2cpp_class_get_method_from_name(tb, "Draw", off::argc::GUITransactionButton_Draw);
        m.buttonRegion =
            api.il2cpp_class_get_method_from_name(tb, "GetRegion", off::argc::GUITransactionButton_GetRegion);
    }
    if (Il2CppClass* anchor = api.il2cpp_class_from_name(img, "", "ControlAnchor"))
        m.controlRegion =
            api.il2cpp_class_get_method_from_name(anchor, "GetControlRegion", off::argc::ControlAnchor_GetControlRegion);
    if (!m.buttonRegion || !m.controlRegion)
        LOGW("menu: GUITransactionButton.GetRegion ou ControlAnchor.GetControlRegion ausente; botão Mods sem "
             "conferir se cabe");
    if (Il2CppClass* sound = api.il2cpp_class_from_name(img, "Terraria.Audio", "SoundEngine"))
        m.playSound = api.il2cpp_class_get_method_from_name(sound, "PlaySound", off::argc::SoundEngine_PlaySound);
    if (!m.layoutInstance || !m.transactionDraw) {
        LOGW("menu: MainMenu_Layout.Instance=%p GUITransactionButton.Draw=%p; sem botão Mods",
             static_cast<void*>(m.layoutInstance), static_cast<const void*>(m.transactionDraw));
        return;
    }
    if (!m.trialMode) LOGW("menu: Main.TrialMode não encontrado; o botão Mods aparece também na versão de teste");
    Il2CppClass* menu = api.il2cpp_class_from_name(img, "", "GUIMainMenu");
    if (!InstallHook("GUIMainMenu.Draw",
                menu ? api.il2cpp_class_get_method_from_name(menu, "Draw", off::argc::GUIMainMenu_Draw) : nullptr,
                off::rva::GUIMainMenu_Draw, reinterpret_cast<void*>(&TLMod_Hooked_GUIMainMenuDraw),
                &g_origMainMenuDraw))
        return;
    bridge::ResolveWorkshop();
}

}  // namespace bridge

extern "C" {

void TLMod_SetMainMenuButton(const char* label, TLModMenuButtonCallback onClick) {
    const MenuButton* next = nullptr;
    if (label && *label && onClick) next = new MenuButton{label, onClick};
    g_button.store(next, std::memory_order_release);
}

void TLMod_Hooked_GUIMainMenuDraw(Il2CppObject* self, const MethodInfo* method) {
    if (bridge::DrawWorkshop()) return;  // a Oficina de Mods ocupa a tela do menu
    auto orig = LoadOrig<MainMenuDrawFn>(g_origMainMenuDraw);
    if (orig) orig(self, method);
    DrawModsButton();
    // Botões de mods do menu (o do Log), menos com as Configurações abertas por cima:
    // as telas do jogo vêm depois e as regiões de toque não têm ordem.
    if (!SettingsOpen()) bridge::DrawHudButtons(true);
}

}  // extern "C"
