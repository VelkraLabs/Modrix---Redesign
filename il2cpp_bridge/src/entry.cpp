// Ponto de entrada no Android: quando a libmodrix_bridge.so é carregada no
// processo do jogo (System.loadLibrary injetado no smali, wrap.sh/LD_PRELOAD,
// ou um loader do Modrix), o construtor dispara a inicialização numa thread
// própria, que espera a libil2cpp.so e o il2cpp_init antes de qualquer coisa.
//
// Não compilado no teste de host, que chama Modrix_Init diretamente.
#include "bridge.h"

__attribute__((constructor)) static void ModrixBridgeOnLoad() {
    // Plugue o instalador de hook antes de iniciar, por exemplo com Dobby:
    //   Modrix_SetHookInstaller([](void* t, void* r, void** o) {
    //       return DobbyHook(t, (dobby_dummy_func_t)r, (dobby_dummy_func_t*)o) == 0;
    //   });
    // e o callback do mod:
    //   Modrix_SetHurtCallback(MyMod_OnPlayerHurt);
    Modrix_StartInitThread();
}
