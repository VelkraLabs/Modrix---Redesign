// API pública da ponte Modrix <-> IL2CPP (Terraria Mobile).
#pragma once

#include <cstddef>
#include <cstdint>

#include "il2cpp_api.h"

// Tudo abaixo é exportado da .so (o resto do código usa visibility=hidden).
#pragma GCC visibility push(default)
extern "C" {

// Assinatura nativa que o IL2CPP gera para
//   double Terraria.Player.Hurt(PlayerDeathReason, int, int, bool, bool, bool, int, bool)
// Em métodos de instância o `this` vem primeiro e o IL2CPP (2021.3) acrescenta
// um `const MethodInfo*` oculto no final. Assinatura do script.json do dump:
//   double Terraria_Player__Hurt(Terraria_Player_o* __this,
//       Terraria_DataStructures_PlayerDeathReason_o* damageSource, int32_t Damage,
//       int32_t hitDirection, bool pvp, bool quiet, bool Crit, int32_t cooldownCounter,
//       bool dodgeable, const MethodInfo* method);
typedef double (*PlayerHurtFn)(Il2CppObject* self, Il2CppObject* damageSource, int32_t damage,
                               int32_t hitDirection, bool pvp, bool quiet, bool crit,
                               int32_t cooldownCounter, bool dodgeable, const MethodInfo* method);

// Contexto entregue ao callback do mod a cada Player.Hurt.
struct ModrixHurtEvent {
    Il2CppObject* player;
    int32_t damage;         // dano pedido (antes de defesa)
    int32_t hitDirection;
    bool pvp;
    bool crit;
    int32_t lifeBefore;     // statLife antes do Hurt original
    int32_t lifeAfter;      // statLife depois (preenchido só no pós-callback)
    double result;          // retorno do Hurt original (dano efetivo)
};

// Callback do mod. Chamado duas vezes por Hurt:
//   pre  (after == false): pode alterar ev->damage; retornar false cancela o golpe.
//   post (after == true):  só observa; o retorno é ignorado.
typedef bool (*ModrixHurtCallback)(ModrixHurtEvent* ev, bool after);

// Função para instalar um inline hook. No Android a ponte usa o ShadowHook
// (src/hook_shadowhook.cpp); no teste de host, um instalador falso.
// Deve gravar em *original o endereço que chama a função original. O slot
// `original` aponta para memória global da ponte, válida pelo processo todo:
// a engine pode guardá-lo e reescrevê-lo depois (ShadowHook, modo multi).
typedef bool (*ModrixHookInstaller)(void* target, void* replacement, void** original);

// Configuração (chamar antes de Modrix_Init / Modrix_StartInitThread).
void Modrix_SetLibraryName(const char* name);  // padrão: "libil2cpp.so"
void Modrix_SetHookInstaller(ModrixHookInstaller installer);
void Modrix_SetHurtCallback(ModrixHurtCallback cb);

// Inicialização síncrona: espera a libil2cpp.so e o runtime subirem (até
// timeoutMs), resolve Terraria.Player e instala o hook em Player.Hurt.
bool Modrix_Init(int timeoutMs);

// Dispara Modrix_Init numa thread separada (usado pelo construtor da .so).
void Modrix_StartInitThread(void);

// Leitura de statLife pelo offset resolvido em runtime (fallback: 0x704).
int32_t Modrix_GetStatLife(Il2CppObject* player);

// Player local via Main.player[Main.myPlayer] (usa il2cpp_runtime_invoke).
// Precisa rodar numa thread anexada ao IL2CPP.
Il2CppObject* Modrix_GetLocalPlayer(void);

// Chama Player.Heal(amount) via il2cpp_runtime_invoke.
bool Modrix_Heal(Il2CppObject* player, int32_t amount);

// Função que o hook de Player.Hurt aponta. Exportada para que um instalador
// externo (ex.: script Frida, ou o loader do Modrix) possa usá-la diretamente.
double Modrix_Hooked_PlayerHurt(Il2CppObject* self, Il2CppObject* damageSource, int32_t damage,
                                int32_t hitDirection, bool pvp, bool quiet, bool crit,
                                int32_t cooldownCounter, bool dodgeable, const MethodInfo* method);

// Endereço da função original (trampoline) após o hook; nulo se não instalado.
PlayerHurtFn Modrix_OriginalPlayerHurt(void);

}  // extern "C"
#pragma GCC visibility pop
