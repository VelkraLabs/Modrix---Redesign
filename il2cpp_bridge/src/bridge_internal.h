// Interno à ponte: compartilhado entre bridge.cpp e npc_hooks.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "il2cpp_api.h"

namespace bridge {

bool Ready();
Il2CppDomain* Domain();
const Il2CppImage* Image();  // Assembly-CSharp.dll
Il2CppClass* MainClass();    // Terraria.Main
Il2CppClass* PlayerClass();  // Terraria.Player
const Il2CppImage* FindImage(const char* imageName);  // ex.: "UnityEngine.CoreModule.dll"

void LogException(const char* where, Il2CppException* exc);

// Offset de um campo de instância (procura também nas classes-base). Se o
// campo não existir, devolve `fallback` (valor do dump) e avisa no log; se
// existir com valor diferente do dump, avisa que a build é outra.
size_t FieldOffset(Il2CppClass* klass, const char* name, size_t fallback);

// Instala um inline hook com o TLModHookInstaller configurado.
//   method: MethodInfo resolvido pela API; o alvo é method->methodPointer
//   dumpRva: RVA do dump, só para conferir se a build é a mesma
//   slot: variável GLOBAL onde a engine grava (e pode reescrever) o endereço
//         que chama a original; ler com __atomic_load_n
bool InstallHook(const char* name, const MethodInfo* method, uintptr_t dumpRva, void* replacement,
                 void** slot);

// NPCs (npc_hooks.cpp). Chamados por TLMod_Init depois de resolver Player.
bool ResolveNpc();
void InstallNpcHooks();

// Frame / estado do mundo (world_state.cpp).
bool ResolveWorldState();
void InstallWorldStateHooks();
// true se quem chama é a thread do Main.DoUpdate (depois do primeiro frame).
bool OnGameThread();

// System.String (UTF-16) -> UTF-8 (save_hooks.cpp). Nulo vira "".
std::string Utf8String(Il2CppObject* str);

// Ferramentas de teste (tools.cpp): nomes, hora, vida/mana. Opcional.
bool ResolveTools();

// Itens de mod: SetDefaults/RebuildTooltip (item_hooks.cpp).
bool ResolveItems();
void InstallItemHooks();

// Texturas de mod e hooks de desenho (draw_hooks.cpp).
bool ResolveDraw();
void InstallDrawHooks();
// Telas que leem TextureAssets.Item sozinhas e nome de NPC (screen_hooks.cpp).
void InstallScreenHooks();
// Botão Mods no menu principal (menu_hooks.cpp). Opcional.
void InstallMenuHooks();
// Oficina de Mods nativa (workshop_screen.cpp): resolve as peças da tela de
// mundos. DrawWorkshop: chamado no lugar do
// GUIMainMenu.Draw; true se desenhou a Oficina (o menu não é desenhado).
bool ResolveWorkshop();
bool DrawWorkshop();
// Aba Mods nas Configurações (settings_tab.cpp). Opcional; usa drawx::ItemIconTexture.
void InstallSettingsHooks();

// Receitas de mod (recipe_hooks.cpp). Depois de ResolveItems (usa TLMod_SetItemDefaults).
// TickRecipes: no frame (Main.DoUpdate), aplica se o SetupRecipes já passou sem elas.
bool ResolveRecipes();
void InstallRecipeHooks();
void TickRecipes();

// Propriedades de um item original para avaliar uma casca (item_traits.cpp).
bool ResolveItemTraits();

// Save/load de personagem e mundo (save_hooks.cpp).
bool ResolveSaves();
void InstallSaveHooks();

}  // namespace bridge
