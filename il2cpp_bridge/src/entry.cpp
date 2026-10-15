// Ponto de entrada no Android: quando a libtlmod_bridge.so é carregada no
// processo do jogo (System.loadLibrary injetado no smali, wrap.sh/LD_PRELOAD,
// ou um loader do TLMod), o construtor dispara a inicialização numa thread
// própria, que espera a libil2cpp.so e o il2cpp_init antes de qualquer coisa.
//
// Não compilado no teste de host, que chama TLMod_Init diretamente.
#include "bridge.h"
#include "hook_shadowhook.h"

__attribute__((constructor)) static void TLModBridgeOnLoad() {
    // O construtor só registra o instalador. O shadowhook_init e o hook em
    // Player.Hurt acontecem depois, na thread de TLMod_Init, quando o
    // IL2CPP já resolveu o MethodInfo de Hurt.
    TLMod_SetHookInstaller(&TLMod_ShadowHookInstaller);
    // O callback do mod entra aqui, por exemplo:
    //   TLMod_SetHurtCallback(MyMod_OnPlayerHurt);
    TLMod_StartInitThread();
}
