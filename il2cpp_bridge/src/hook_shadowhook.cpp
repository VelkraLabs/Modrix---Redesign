#include "hook_shadowhook.h"

#include <shadowhook.h>

#include "log.h"

namespace {

bool EnsureShadowHookInit() {
    // Inicializa só na primeira instalação, a partir da thread de init da ponte.
    // Não chamar no construtor da .so: o shadowhook_init mexe no linker e o
    // construtor roda com o lock do linker preso (risco de deadlock).
    // Chamadas repetidas devolvem o resultado da primeira.
    int rc = shadowhook_init(SHADOWHOOK_MODE_SHARED, /*debuggable=*/false);
    if (rc != 0) {
        LOGE("shadowhook_init falhou: %d (%s)", rc, shadowhook_to_errmsg(rc));
        return false;
    }
    return true;
}

}  // namespace

bool Modrix_ShadowHookInstaller(void* target, void* replacement, void** original) {
    if (!EnsureShadowHookInit()) return false;

    // Modo multi: convive com outros hooks no mesmo alvo e tem o melhor
    // desempenho por chamada. Exige `original` não nulo e sempre gravável,
    // porque o ShadowHook encadeia os proxies por ele. A ponte já evita
    // recursão com o contador t_hurtDepth em Modrix_Hooked_PlayerHurt.
    void* stub = shadowhook_hook_func_addr_2(target, replacement, original,
                                             SHADOWHOOK_HOOK_WITH_MULTI_MODE);
    if (!stub) {
        int err = shadowhook_get_errno();
        LOGE("shadowhook_hook_func_addr_2(%p) falhou: %d (%s)", target, err,
             shadowhook_to_errmsg(err));
        return false;
    }
    LOGI("ShadowHook %s: hook em %p -> %p (stub %p)", shadowhook_get_version(), target,
         replacement, stub);
    return true;
}
