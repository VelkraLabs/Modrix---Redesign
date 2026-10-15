#include "il2cpp_api.h"

#include <dlfcn.h>

#include "log.h"

namespace il2cpp {

Api api;
uintptr_t g_base = 0;

bool LoadApi(const char* libName) {
    // RTLD_NOLOAD: só obtém o handle se o jogo já carregou a lib. Nunca a
    // carregamos por conta própria, senão o IL2CPP subiria fora de ordem.
    void* handle = dlopen(libName, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) return false;

    bool ok = true;
#define IL2CPP_RESOLVE(ret, name, args)                                       \
    api.name = reinterpret_cast<ret(*) args>(dlsym(handle, #name));           \
    if (!api.name) {                                                          \
        LOGE("export ausente: %s", #name);                                    \
        ok = false;                                                           \
    }
    IL2CPP_API_LIST(IL2CPP_RESOLVE)
#undef IL2CPP_RESOLVE

    // Base de carga da libil2cpp.so: necessária para converter RVA do dump em
    // endereço real (base + RVA).
    Dl_info info{};
    if (ok && dladdr(reinterpret_cast<void*>(api.il2cpp_domain_get), &info) && info.dli_fbase) {
        g_base = reinterpret_cast<uintptr_t>(info.dli_fbase);
    } else {
        LOGE("não foi possível obter a base da %s", libName);
        ok = false;
    }

    // O handle extra só incrementa o refcount; a lib continua carregada pelo jogo.
    dlclose(handle);
    return ok;
}

}  // namespace il2cpp
