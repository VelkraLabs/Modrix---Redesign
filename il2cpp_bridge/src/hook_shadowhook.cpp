#include "hook_shadowhook.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include <shadowhook.h>

#include "log.h"

// libshadowhook_nothing.so embutida (src/sh_nothing_blob.S).
extern "C" const unsigned char tlmod_sh_nothing[];
extern "C" const unsigned char tlmod_sh_nothing_end[];

// Do ShadowHook (sh_linker.c): monitor do linker. Ver tlmod_sh_linker_init.
extern "C" int sh_linker_init(void);

namespace {

constexpr const char* kNothingName = "libshadowhook_nothing.so";
std::string g_nothingPath;  // escrito antes do shadowhook_init, só lido depois

std::string PackageName() {
    std::ifstream in("/proc/self/cmdline", std::ios::binary);
    std::string name;
    std::getline(in, name, '\0');
    return name.substr(0, name.find(':'));
}

bool IsDir(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// code_cache do app (privado, fora do armazenamento externo noexec). Sem JNI
// aqui: /data/user/<usuário>/<pacote>, com /data/data/<pacote> de reserva.
// Cria só as subpastas abaixo do code_cache, que já existe. "_ponte" não
// colide com as pastas dos mods (code_cache/tlmod/<mod_id>): um id de mod
// não começa com "_".
std::string PrivateDir() {
    std::string pkg = PackageName();
    if (pkg.empty()) return {};
    char buf[256];
    std::snprintf(buf, sizeof(buf), "/data/user/%u/%s/code_cache", static_cast<unsigned>(::getuid() / 100000),
                  pkg.c_str());
    std::string base = IsDir(buf) ? buf : "/data/data/" + pkg + "/code_cache";
    if (!IsDir(base)) return {};
    for (const char* sub : {"/tlmod", "/tlmod/_ponte"}) {
        std::string d = base + sub;
        if (::mkdir(d.c_str(), 0700) != 0 && errno != EEXIST) return {};
    }
    std::string dir = base + "/tlmod/_ponte";
    return IsDir(dir) ? dir : std::string();
}

bool SameContent(const std::string& path, const unsigned char* data, size_t size) {
    std::ifstream in(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return bytes.size() == size && std::memcmp(bytes.data(), data, size) == 0;
}

// Grava a biblioteca auxiliar (atômico, somente leitura) e devolve o caminho.
std::string ExtractNothing() {
    const auto size = static_cast<size_t>(tlmod_sh_nothing_end - tlmod_sh_nothing);
    std::string dir = PrivateDir();
    if (dir.empty()) {
        LOGW("ShadowHook: sem pasta privada (code_cache) para %s: %s", kNothingName, std::strerror(errno));
        return {};
    }
    std::string path = dir + "/" + kNothingName;
    if (SameContent(path, tlmod_sh_nothing, size)) {
        ::chmod(path.c_str(), 0444);
        return path;
    }
    std::string tmp = path + ".tmp-" + std::to_string(::getpid());
    ::unlink(tmp.c_str());
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    bool ok = fd >= 0;
    for (size_t done = 0; ok && done < size;) {
        ssize_t n = ::write(fd, tlmod_sh_nothing + done, size - done);
        if (n <= 0) ok = false;
        else done += static_cast<size_t>(n);
    }
    ok = ok && ::fsync(fd) == 0;
    if (fd >= 0) ::close(fd);
    ok = ok && ::chmod(tmp.c_str(), 0444) == 0 && ::rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok) {
        LOGW("ShadowHook: não foi possível gravar %s: %s", path.c_str(), std::strerror(errno));
        ::unlink(tmp.c_str());
        return {};
    }
    return path;
}

std::string Prop(const char* name) {
    char value[PROP_VALUE_MAX] = {};
    __system_property_get(name, value);
    return value;
}

bool EnsureShadowHookInit() {
    // Inicializa só na primeira instalação, a partir da thread de init da ponte.
    // Não chamar no construtor da .so: o shadowhook_init mexe no linker e o
    // construtor roda com o lock do linker preso (risco de deadlock).
    // Chamadas repetidas devolvem o resultado da primeira.
    static bool prepared = false;
    if (!prepared) {
        prepared = true;
        LOGI("Android %s (API %s), %s %s", Prop("ro.build.version.release").c_str(),
             Prop("ro.build.version.sdk").c_str(), Prop("ro.product.manufacturer").c_str(),
             Prop("ro.product.model").c_str());
        g_nothingPath = ExtractNothing();
        if (!g_nothingPath.empty()) LOGI("ShadowHook: biblioteca auxiliar em %s", g_nothingPath.c_str());
    }
    // debuggable = true: os detalhes de uma falha do ShadowHook vão para o
    // logcat (tag shadowhook_tag), que é onde ele os escreve.
    int rc = shadowhook_init(SHADOWHOOK_MODE_SHARED, /*debuggable=*/true);
    if (rc != 0) {
        LOGE("shadowhook_init falhou: %d (%s)", rc, shadowhook_to_errmsg(rc));
        return false;
    }
    return true;
}

}  // namespace

// Chamado no lugar do dlopen dentro de sh_linker.c (ver CMakeLists.txt): o
// ShadowHook abre a biblioteca auxiliar pelo nome curto, que só seria achado
// na pasta de bibliotecas do APK. Aqui ela vem da cópia na pasta privada.
extern "C" void* tlmod_sh_dlopen(const char* name, int flags) {
    if (name && std::strcmp(name, kNothingName) == 0 && !g_nothingPath.empty()) {
        void* h = dlopen(g_nothingPath.c_str(), flags);
        if (!h) {
            const char* e = dlerror();
            LOGE("ShadowHook: dlopen(%s) falhou: %s", g_nothingPath.c_str(), e ? e : "?");
        }
        return h;
    }
    return dlopen(name, flags);
}

// Chamado no lugar de sh_linker_init dentro de shadowhook_init. O monitor do
// linker só serve para hooks por nome de símbolo em bibliotecas que ainda vão
// ser carregadas; os hooks da ponte são por endereço, numa libil2cpp.so já
// carregada. Se o monitor falhar (símbolos do linker diferentes numa versão
// nova do Android, por exemplo), o resto do ShadowHook segue funcionando.
extern "C" int tlmod_sh_linker_init(void) {
    int r = sh_linker_init();
    if (r != 0)
        LOGW("ShadowHook: monitor do linker indisponível (detalhes no logcat, tag shadowhook_tag); "
             "os hooks por endereço seguem funcionando");
    else
        LOGI("ShadowHook: monitor do linker ok");
    return 0;
}

bool TLMod_ShadowHookInstaller(void* target, void* replacement, void** original) {
    if (!EnsureShadowHookInit()) return false;

    // Modo multi: convive com outros hooks no mesmo alvo e tem o melhor
    // desempenho por chamada. Exige `original` não nulo e sempre gravável,
    // porque o ShadowHook encadeia os proxies por ele. A ponte já evita
    // recursão com o contador t_hurtDepth em TLMod_Hooked_PlayerHurt.
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
