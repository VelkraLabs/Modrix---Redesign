// Botão "Mods" no menu principal, desenhado pelo próprio jogo: depois do
// GUIMainMenu.Draw original, o mesmo GUITransactionButton.Draw dos outros
// botões, com o layout de "Comprar o jogo completo" (MainMenu_Layout.
// PurchaseFullGame). Esse botão só existe na versão de teste (Main.TrialMode);
// fora dela a posição fica livre. O clique toca o som do menu e chama o
// callback de TLMod_SetMainMenuButton (a Oficina de Mods do overlay).
//
// O frame do hook só tem PODs (exceções do jogo passam direto); o que usa
// objetos C++ fica em funções noinline.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

typedef void (*MainMenuDrawFn)(Il2CppObject* self, const MethodInfo* method);
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
bool g_checked = false;         // posição conferida e registrada no log
bool g_blocked = false;         // a posição do PurchaseFullGame coincide com outro botão

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

// Primeira vez: registra onde cada botão fica (para ajustar a posição com o
// log de um aparelho) e confere que o lugar do botão Mods está livre.
__attribute__((noinline)) void CheckPlacement(Il2CppObject* layout, Il2CppObject* mods) {
    g_checked = true;
    auto describe = [](Il2CppObject* b) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "controle %d, âncora %d, (%.0f, %.0f)", FieldAt<int32_t>(b, m.anchorControl),
                      FieldAt<int32_t>(b, m.anchor), FieldAt<float>(b, m.location),
                      FieldAt<float>(b, m.location + 4));
        return std::string(buf);
    };
    std::string list;
    for (const GameButton& gb : g_gameButtons) {
        Il2CppObject* b = FieldAt<Il2CppObject*>(layout, gb.offset);
        if (!b) continue;
        list += std::string(list.empty() ? "" : "; ") + gb.name + ": " + describe(b);
        if (SamePlace(b, mods)) {
            g_blocked = true;
            LOGW("menu: a posição do botão Mods coincide com %s; botão não desenhado", gb.name);
        }
    }
    LOGI("menu: botões do jogo: %s", list.c_str());
    if (!g_blocked) LOGI("menu: botão Mods em %s (posição de PurchaseFullGame)", describe(mods).c_str());
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
    if (!g_checked) CheckPlacement(layout, mods);
    if (g_blocked) return;
    Il2CppObject* label = Label(btn);
    if (!label) return;
    // Com Configurações aberta por cima, os botões do menu ficam desativados.
    Il2CppObject* gui = StaticObject(m.guiActive);
    Il2CppObject* settings = gui ? FieldAt<Il2CppObject*>(gui, m.guiSettings) : nullptr;
    const bool disabled = settings && FieldAt<bool>(settings, m.settingsVisible);
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
    if (Il2CppClass* tb = api.il2cpp_class_from_name(img, "", "GUITransactionButton"))
        m.transactionDraw = api.il2cpp_class_get_method_from_name(tb, "Draw", off::argc::GUITransactionButton_Draw);
    if (Il2CppClass* sound = api.il2cpp_class_from_name(img, "Terraria.Audio", "SoundEngine"))
        m.playSound = api.il2cpp_class_get_method_from_name(sound, "PlaySound", off::argc::SoundEngine_PlaySound);
    if (!m.layoutInstance || !m.transactionDraw) {
        LOGW("menu: MainMenu_Layout.Instance=%p GUITransactionButton.Draw=%p; sem botão Mods",
             static_cast<void*>(m.layoutInstance), static_cast<const void*>(m.transactionDraw));
        return;
    }
    if (!m.trialMode) LOGW("menu: Main.TrialMode não encontrado; o botão Mods aparece também na versão de teste");
    Il2CppClass* menu = api.il2cpp_class_from_name(img, "", "GUIMainMenu");
    InstallHook("GUIMainMenu.Draw",
                menu ? api.il2cpp_class_get_method_from_name(menu, "Draw", off::argc::GUIMainMenu_Draw) : nullptr,
                off::rva::GUIMainMenu_Draw, reinterpret_cast<void*>(&TLMod_Hooked_GUIMainMenuDraw),
                &g_origMainMenuDraw);
}

}  // namespace bridge

extern "C" {

void TLMod_SetMainMenuButton(const char* label, TLModMenuButtonCallback onClick) {
    const MenuButton* next = nullptr;
    if (label && *label && onClick) next = new MenuButton{label, onClick};
    g_button.store(next, std::memory_order_release);
}

void TLMod_Hooked_GUIMainMenuDraw(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<MainMenuDrawFn>(g_origMainMenuDraw);
    if (orig) orig(self, method);
    DrawModsButton();
}

}  // extern "C"
