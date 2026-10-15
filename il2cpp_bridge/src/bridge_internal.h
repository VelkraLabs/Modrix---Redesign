// Interno à ponte: compartilhado entre bridge.cpp e npc_hooks.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "il2cpp_api.h"

namespace bridge {

bool Ready();
Il2CppDomain* Domain();
const Il2CppImage* Image();  // Assembly-CSharp.dll
Il2CppClass* MainClass();    // Terraria.Main
Il2CppClass* PlayerClass();  // Terraria.Player

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

// Save/load de personagem e mundo (save_hooks.cpp).
bool ResolveSaves();
void InstallSaveHooks();

}  // namespace bridge
