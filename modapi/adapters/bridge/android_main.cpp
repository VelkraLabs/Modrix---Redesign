// Entrada do runtime no Android: libtlmod_runtime.so depende de
// libtlmod_bridge.so, então o linker roda o construtor da ponte primeiro
// (ela dispara TLMod_Init). Aqui ligamos o log em arquivo (tlmod/tlmod.log,
// com as linhas da ponte também), esperamos a ponte ficar pronta, lemos a
// pasta de mods e carregamos tudo. As bibliotecas dos mods são copiadas para
// code_cache/tlmod/<mod_id>/ antes do dlopen: o armazenamento externo é noexec.
#include <android/log.h>
#include <pthread.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "bridge.h"
#include "bridge_adapter.h"
#include "tlmod/runtime/log.h"
#include "tlmod/runtime/mod_loader.h"
#include "tlmod/overlay/config_store.h"
#include "tlmod/overlay/panel.h"
#include "overlay_android.h"

namespace {

void LogcatSink(TLModLogLevel level, const std::string& source, const std::string& msg) {
    static const int kPrio[] = {ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR};
    int prio = (level >= TLMOD_LOG_DEBUG && level <= TLMOD_LOG_ERROR) ? kPrio[level] : ANDROID_LOG_INFO;
    __android_log_print(prio, "TLMod", "[%s] %s", source.c_str(), msg.c_str());
}

// Linhas da ponte: já foram para o logcat; aqui só vão para a memória e o arquivo.
void OnBridgeLog(int level, int64_t timeMs, const char* message) {
    tlmod::rt::RecordLog(static_cast<TLModLogLevel>(level), "ponte", message ? message : "", timeMs);
}

tlmod::overlay::PanelController* g_panel = nullptr;  // escrito antes de ligar o botão

// Botão "Mods" do menu principal (thread do jogo, dentro do desenho do menu):
// só pede a Oficina; ela abre no próximo Tick, na thread de UI.
void OnMenuModsButton() {
    if (g_panel) g_panel->RequestWorkshop();
}

std::string PackageName() {
    std::ifstream in("/proc/self/cmdline", std::ios::binary);
    std::string name;
    std::getline(in, name, '\0');
    return name.substr(0, name.find(':'));  // processos auxiliares: "pkg:servico"
}

// Ordem: $TLMOD_MODS_DIR; armazenamento externo do app (o usuário consegue
// copiar mods para lá sem root); armazenamento interno.
std::filesystem::path ModsDir() {
    if (const char* env = std::getenv("TLMOD_MODS_DIR")) return env;
    std::string pkg = PackageName();
    std::filesystem::path external = "/storage/emulated/0/Android/data/" + pkg + "/files/tlmod/mods";
    std::error_code ec;
    if (std::filesystem::is_directory(external, ec)) return external;
    return "/data/data/" + pkg + "/files/tlmod/mods";
}

// Pasta privada para as cópias das bibliotecas dos mods.
std::filesystem::path LibraryCacheDir() {
    std::string error;
    std::string dir = tlmod::overlay::AppCodeCacheDir(10000, &error);
    if (dir.empty()) {
        dir = "/data/data/" + PackageName() + "/code_cache";
        tlmod::rt::Log(TLMOD_LOG_WARN, "runtime",
                       "Context.getCodeCacheDir indisponível (" + error + "); usando " + dir);
    }
    return std::filesystem::path(dir) / "tlmod";
}

void* RuntimeMain(void*) {
    tlmod::rt::SetLogSink(&LogcatSink);
    TLMod_SetLogCallback(&OnBridgeLog);

    std::filesystem::path dir = ModsDir();
    std::string logError;
    if (!tlmod::rt::SetLogFile(dir.parent_path() / "tlmod.log", &logError))
        tlmod::rt::Log(TLMOD_LOG_WARN, "runtime", "sem log em arquivo: " + logError);
    tlmod::rt::Log(TLMOD_LOG_INFO, "runtime", "pasta de mods: " + dir.string());

    if (!TLMod_WaitReady(120000)) {
        tlmod::rt::Log(TLMOD_LOG_ERROR, "runtime", "ponte IL2CPP não ficou pronta; mods não carregados");
        return nullptr;
    }
    TLMod_AttachCurrentThread();  // mods podem consultar o jogo durante o init

    // Vivem até o processo acabar: eventos e mods guardam ponteiros para eles.
    static tlmod::rt::BridgeGameAdapter adapter;
    static tlmod::rt::ModLoader loader(&adapter);

    // Mods desligados no painel: lidos ANTES de carregar (valem nesta abertura).
    static tlmod::overlay::ConfigStore config(dir.parent_path() / "tlmod_config.json");
    std::string configError;
    if (!config.Load(&configError)) tlmod::rt::Log(TLMOD_LOG_WARN, "runtime", configError);
    loader.SetDisabled(config.Disabled());
    // Saves na nuvem não têm um caminho local confiável: o arquivo paralelo
    // dos itens de mod fica na pasta do TLMod.
    loader.Persistence().SetCloudDirectory(dir.parent_path() / "cloud_saves");
    std::filesystem::path cache = LibraryCacheDir();
    tlmod::rt::Log(TLMOD_LOG_INFO, "runtime", "cópias das bibliotecas dos mods: " + cache.string());
    loader.SetLibraryCacheDir(cache);

    loader.DiscoverDirectory(dir);
    size_t n = loader.LoadAll();
    tlmod::rt::Log(TLMOD_LOG_INFO, "runtime",
                    std::to_string(n) + " de " + std::to_string(loader.Mods().size()) + " mods carregados");
    for (const auto& m : loader.Mods()) {
        if (m.state != tlmod::rt::ModState::Loaded)
            tlmod::rt::Log(TLMOD_LOG_WARN, m.manifest.id, std::string(ToString(m.state)) + ": " + m.error);
    }

    // Painel dentro do jogo (Views Android presas à Activity).
    static tlmod::overlay::LoaderDataSource source(loader);
    static tlmod::overlay::PanelController panel(source, config);
    std::string overlayError;
    if (!tlmod::overlay::StartOverlay(&panel, 60000, &overlayError)) {
        tlmod::rt::Log(TLMOD_LOG_ERROR, "overlay", "painel não instalado: " + overlayError);
        return nullptr;
    }
    // Oficina de Mods: botão "Mods" desenhado pelo jogo no menu principal.
    g_panel = &panel;
    TLMod_SetMainMenuButton("Mods", &OnMenuModsButton);
    return nullptr;
}

__attribute__((constructor)) void TLModRuntimeOnLoad() {
    pthread_t th;
    if (pthread_create(&th, nullptr, &RuntimeMain, nullptr) == 0) pthread_detach(th);
}

}  // namespace
