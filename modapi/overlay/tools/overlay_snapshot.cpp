// Gera view-models do painel a partir de dados fixos, para a prévia em PNG.
//   overlay_snapshot sample_checklist.json <saída>/
// Escreve snapshot_checklist.json e snapshot_mods.json: exatamente o JSON que
// o renderizador Android recebe do PanelController.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "modrix/overlay/config_store.h"
#include "modrix/overlay/panel.h"

using namespace modrix;
namespace fs = std::filesystem;

class StaticSource final : public overlay::PanelDataSource {
public:
    explicit StaticSource(std::string checklist) : checklist_(std::move(checklist)) {}
    std::vector<overlay::ModInfo> Mods() const override {
        return {
            {"modrix.boss_checklist", "Boss Checklist", "0.1.0", rt::ModState::Loaded, ""},
            {"modrix.example_item", "Exemplo: Rubi Fictício", "0.1.0", rt::ModState::Loaded, ""},
            {"modrix.example_boss", "Exemplo: Titã de Brasa", "0.1.0", rt::ModState::Disabled,
             "desativado nas configurações"},
            {"acme.swords", "Espadas Lendárias", "1.4.0", rt::ModState::Rejected,
             "dependência \"acme.core\" (^2.0.0) ausente ou rejeitada"},
        };
    }
    std::optional<std::string> Query(const std::string&, const std::string&, std::string*) const override {
        return checklist_;
    }

private:
    std::string checklist_;
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: %s sample_checklist.json <saída>/\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    fs::path out = argv[2];
    fs::create_directories(out);

    StaticSource source(ss.str());
    overlay::ConfigStore config(out / "preview_config.json");
    std::string err;
    config.SetEnabled("modrix.example_item", false, &err);  // mostra o aviso "ao reiniciar"
    overlay::PanelController panel(source, config);
    panel.SetOpen(true);
    panel.SelectTab(overlay::Tab::Checklist);
    panel.Tick(0);
    std::ofstream(out / "snapshot_checklist.json") << panel.SnapshotJson();

    panel.SelectTab(overlay::Tab::Mods);
    panel.Tick(1);
    std::ofstream(out / "snapshot_mods.json") << panel.SnapshotJson();
    fs::remove(out / "preview_config.json");
    std::printf("snapshots em %s\n", out.string().c_str());
    return 0;
}
