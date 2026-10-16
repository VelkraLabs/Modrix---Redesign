// Gera view-models do painel a partir de dados fixos, para a prévia em PNG.
//   overlay_snapshot sample_checklist.json <saída>/ [sample_panel.json]
// Escreve snapshot_checklist.json, snapshot_mods.json e snapshot_log.json (e,
// com o terceiro argumento, snapshot_tools.json com a aba do tlmod.devtools):
// exatamente o JSON que o renderizador Android recebe do PanelController.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "tlmod/overlay/config_store.h"
#include "tlmod/overlay/panel.h"

using namespace tlmod;
namespace fs = std::filesystem;

constexpr const char* kModsDir = "/storage/emulated/0/Android/data/com.and.games505.TerrariaPaid/files/tlmod/mods";
// dlerror() real de uma biblioteca que precisa de um símbolo ausente: a causa fica no fim.
constexpr const char* kDlError =
    "não foi possível carregar lib/android-arm64/libgems.so (cópia em /data/user/0/com.and.games505.TerrariaPaid/"
    "code_cache/tlmod/acme.gems/libgems.so): dlopen failed: cannot locate symbol \"_ZN5acme4core8RegistryC1Ev\" "
    "referenced by \"/data/user/0/com.and.games505.TerrariaPaid/code_cache/tlmod/acme.gems/libgems.so\"";

class StaticSource final : public overlay::PanelDataSource {
public:
    StaticSource(std::string checklist, std::string panel)
        : checklist_(std::move(checklist)), panel_(std::move(panel)) {}
    std::vector<overlay::ModInfo> Mods() const override {
        return {
            {"tlmod.boss_checklist", "Boss Checklist", "0.1.0", rt::ModState::Loaded, ""},
            {"tlmod.example_item", "Exemplo: Rubi Fictício", "0.1.0", rt::ModState::Loaded, ""},
            {"tlmod.example_boss", "Exemplo: Titã de Brasa", "0.1.0", rt::ModState::Disabled,
             "desativado nas configurações"},
            {"acme.swords", "Espadas Lendárias", "1.4.0", rt::ModState::Rejected,
             "dependência \"acme.core\" (^2.0.0) ausente ou rejeitada"},
            {"acme.gems", "Gemas", "0.2.0", rt::ModState::Failed, kDlError},
        };
    }
    std::vector<rt::LogEntry> LogTail(size_t) const override {
        const int64_t t0 = 1760620800000;  // horário fixo: a prévia não muda a cada execução
        return {
            {t0, TLMOD_LOG_INFO, "ponte", "libil2cpp.so em 0x7a1c400000"},
            {t0 + 40, TLMOD_LOG_INFO, "ponte", "hook em Player.Hurt instalado (0x7a1d9a15b8)"},
            {t0 + 900, TLMOD_LOG_INFO, "runtime", "pasta de mods: " + std::string(kModsDir)},
            {t0 + 950, TLMOD_LOG_INFO, "tlmod.example_item",
             "biblioteca copiada para /data/user/0/com.and.games505.TerrariaPaid/code_cache/tlmod/tlmod.example_item/"
             "libexample_item.so (SHA-256 2e569a44f298c36022049d324c2f8659060003e67459ff2700895f456f2940c5)"},
            {t0 + 960, TLMOD_LOG_ERROR, "acme.gems", kDlError},
            {t0 + 990, TLMOD_LOG_WARN, "runtime", "3 de 4 mods carregados"},
        };
    }
    std::string LogFile() const override { return std::string(kModsDir).substr(0, std::string(kModsDir).size() - 5) + "/tlmod.log"; }
    std::optional<std::string> Query(const std::string& modId, const std::string&, std::string*) const override {
        return modId == "tlmod.devtools" ? panel_ : checklist_;
    }
    std::vector<std::string> PanelMods() const override {
        return panel_.empty() ? std::vector<std::string>{} : std::vector<std::string>{"tlmod.devtools"};
    }
    std::optional<std::string> Command(const std::string&, const std::string&, const std::string&,
                                       std::string*) const override {
        return std::string("{\"toast\":\"10x Estrela Cadente no invent\u00e1rio\"}");
    }

private:
    std::string checklist_, panel_;
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: %s sample_checklist.json <saída>/ [sample_panel.json]\n", argv[0]);
        return 2;
    }
    auto read = [](const char* path) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    fs::path out = argv[2];
    fs::create_directories(out);

    StaticSource source(read(argv[1]), argc > 3 ? read(argv[3]) : std::string());
    overlay::ConfigStore config(out / "preview_config.json");
    std::string err;
    config.SetEnabled("tlmod.example_item", false, &err);  // mostra o aviso "ao reiniciar"
    overlay::PanelController panel(source, config);
    panel.SetOpen(true);
    panel.SelectTab(overlay::Tab::Checklist);
    panel.Tick(0);
    std::ofstream(out / "snapshot_checklist.json") << panel.SnapshotJson();

    panel.SelectTab(overlay::Tab::Mods);
    panel.Tick(1);
    std::ofstream(out / "snapshot_mods.json") << panel.SnapshotJson();

    panel.SelectTab(overlay::Tab::Log);
    panel.Tick(2);
    std::ofstream(out / "snapshot_log.json") << panel.SnapshotJson();

    if (argc > 3) {  // aba Ferramentas, logo depois de um toque em "+10"
        panel.SelectModTab("tlmod.devtools");
        panel.Tick(3);
        panel.HandleAction("mod_command", "give 75 10");
        panel.Tick(4);
        std::ofstream(out / "snapshot_tools.json") << panel.SnapshotJson();
    }
    fs::remove(out / "preview_config.json");
    std::printf("snapshots em %s\n", out.string().c_str());
    return 0;
}
