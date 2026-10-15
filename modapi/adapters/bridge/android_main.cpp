// Entrada do runtime no Android: libmodrix_runtime.so depende de
// libmodrix_bridge.so, então o linker roda o construtor da ponte primeiro
// (ela dispara Modrix_Init). Aqui esperamos a ponte ficar pronta, lemos a
// pasta de mods e carregamos tudo.
#include <android/log.h>
#include <pthread.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "bridge.h"
#include "bridge_adapter.h"
#include "modrix/runtime/log.h"
#include "modrix/runtime/mod_loader.h"
#include "modrix/overlay/config_store.h"
#include "modrix/overlay/panel.h"
#include "overlay_android.h"

namespace {

void LogcatSink(ModrixLogLevel level, const std::string& source, const std::string& msg) {
    static const int kPrio[] = {ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR};
    int prio = (level >= MODRIX_LOG_DEBUG && level <= MODRIX_LOG_ERROR) ? kPrio[level] : ANDROID_LOG_INFO;
    __android_log_print(prio, "Modrix", "[%s] %s", source.c_str(), msg.c_str());
}

std::string PackageName() {
    std::ifstream in("/proc/self/cmdline", std::ios::binary);
    std::string name;
    std::getline(in, name, '\0');
    return name.substr(0, name.find(':'));  // processos auxiliares: "pkg:servico"
}

// Ordem: $MODRIX_MODS_DIR; armazenamento externo do app (o usuário consegue
// copiar mods para lá sem root); armazenamento interno.
std::filesystem::path ModsDir() {
    if (const char* env = std::getenv("MODRIX_MODS_DIR")) return env;
    std::string pkg = PackageName();
    std::filesystem::path external = "/storage/emulated/0/Android/data/" + pkg + "/files/modrix/mods";
    std::error_code ec;
    if (std::filesystem::is_directory(external, ec)) return external;
    return "/data/data/" + pkg + "/files/modrix/mods";
}

void* RuntimeMain(void*) {
    modrix::rt::SetLogSink(&LogcatSink);
    if (!Modrix_WaitReady(120000)) {
        modrix::rt::Log(MODRIX_LOG_ERROR, "runtime", "ponte IL2CPP não ficou pronta; mods não carregados");
        return nullptr;
    }
    Modrix_AttachCurrentThread();  // mods podem consultar o jogo durante o init

    // Vivem até o processo acabar: eventos e mods guardam ponteiros para eles.
    static modrix::rt::BridgeGameAdapter adapter;
    static modrix::rt::ModLoader loader(&adapter);

    std::filesystem::path dir = ModsDir();
    modrix::rt::Log(MODRIX_LOG_INFO, "runtime", "pasta de mods: " + dir.string());

    // Mods desligados no painel: lidos ANTES de carregar (valem nesta abertura).
    static modrix::overlay::ConfigStore config(dir.parent_path() / "modrix_config.json");
    std::string configError;
    if (!config.Load(&configError)) modrix::rt::Log(MODRIX_LOG_WARN, "runtime", configError);
    loader.SetDisabled(config.Disabled());

    loader.DiscoverDirectory(dir);
    size_t n = loader.LoadAll();
    modrix::rt::Log(MODRIX_LOG_INFO, "runtime",
                    std::to_string(n) + " de " + std::to_string(loader.Mods().size()) + " mods carregados");
    for (const auto& m : loader.Mods()) {
        if (m.state != modrix::rt::ModState::Loaded)
            modrix::rt::Log(MODRIX_LOG_WARN, m.manifest.id, std::string(ToString(m.state)) + ": " + m.error);
    }

    // Painel dentro do jogo (Views Android presas à Activity).
    static modrix::overlay::LoaderDataSource source(loader);
    static modrix::overlay::PanelController panel(source, config);
    std::string overlayError;
    if (!modrix::overlay::StartOverlay(&panel, 60000, &overlayError))
        modrix::rt::Log(MODRIX_LOG_ERROR, "overlay", "painel não instalado: " + overlayError);
    return nullptr;
}

__attribute__((constructor)) void ModrixRuntimeOnLoad() {
    pthread_t th;
    if (pthread_create(&th, nullptr, &RuntimeMain, nullptr) == 0) pthread_detach(th);
}

}  // namespace
