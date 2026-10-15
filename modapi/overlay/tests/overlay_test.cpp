// Testes da lógica do painel (sem desenho) com o jogo simulado e os mods reais.
//   overlay_test <build>/dist/mods
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../../tests/fake_game.h"
#include "json.h"
#include "modrix/overlay/config_store.h"
#include "modrix/overlay/panel.h"
#include "modrix/runtime/log.h"
#include "modrix/runtime/mod_loader.h"

using namespace modrix;
using overlay::ChecklistView;
using overlay::PanelController;
using overlay::Tab;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) {                                                          \
            std::printf("ok    %s\n", #cond);                                \
        } else {                                                             \
            std::printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static void QuietSink(ModrixLogLevel, const std::string&, const std::string&) {}

static std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static const overlay::ModRow* Row(const PanelController& c, const std::string& id) {
    for (const auto& r : c.ModRows())
        if (r.id == id) return &r;
    return nullptr;
}

static const overlay::BossRow* Boss(const ChecklistView& v, const std::string& name) {
    for (const auto& r : v.rows)
        if (!r.header && r.name == name) return &r;
    return nullptr;
}

// ---------------------------------------------------------------------------
static void TestConfigStore(const fs::path& tmp) {
    std::printf("\n== configuração (modrix_config.json)\n");
    fs::path file = tmp / "cfg" / "modrix_config.json";
    overlay::ConfigStore cfg(file);
    std::string err;
    CHECK(cfg.Load(&err) && cfg.Disabled().empty());  // arquivo ausente = tudo ligado
    CHECK(cfg.SetEnabled("a.mod", false, &err) && !cfg.IsEnabled("a.mod"));
    CHECK(ReadFile(file).find("\"disabled_mods\":[\"a.mod\"]") != std::string::npos);
    CHECK(!fs::exists(fs::path(file).concat(".tmp")));  // gravação atômica não deixa resto

    overlay::ConfigStore reread(file);
    CHECK(reread.Load(&err) && !reread.IsEnabled("a.mod") && reread.IsEnabled("b.mod"));
    CHECK(cfg.SetEnabled("a.mod", true, &err) && ReadFile(file).find("\"disabled_mods\":[]") != std::string::npos);

    std::ofstream(file) << "{ quebrado";
    overlay::ConfigStore broken(file);
    CHECK(!broken.Load(&err) && broken.Disabled().empty() && err.find("inválida") != std::string::npos);

    // Não dá para gravar (o "diretório" é um arquivo): nada muda em memória.
    std::ofstream(tmp / "not_a_dir") << "x";
    overlay::ConfigStore readonly(tmp / "not_a_dir" / "modrix_config.json");
    CHECK(!readonly.SetEnabled("a.mod", false, &err) && readonly.IsEnabled("a.mod"));
}

// ---------------------------------------------------------------------------
static void TestLoaderDisabled(const fs::path& mods) {
    std::printf("\n== ModLoader respeita mods desligados\n");
    FakeGame game;
    rt::ModLoader loader(&game);
    loader.SetDisabled({"modrix.example_item"});
    loader.DiscoverDirectory(mods);
    rt::ModManifest dep;
    dep.apiVersion = MODRIX_API_VERSION;
    dep.id = "test.needs_item";
    dep.name = "Precisa do item";
    dep.version = *rt::SemVer::Parse("1.0.0");
    dep.dependencies.push_back({"modrix.example_item", *rt::VersionReq::Parse("*"), false});
    loader.AddBuiltin(dep, [](const ModrixHostApi*, ModrixContext*) { return 0; });
    loader.LoadAll();
    CHECK(loader.Find("modrix.example_item")->state == rt::ModState::Disabled);
    CHECK(!loader.Registry().Find(MODRIX_CONTENT_ITEM, "modrix.example_item:fictional_ruby"));  // nem foi aberto
    CHECK(loader.Find("test.needs_item")->state == rt::ModState::Skipped &&
          loader.Find("test.needs_item")->error.find("desativada") != std::string::npos);
    CHECK(loader.Find("modrix.boss_checklist")->state == rt::ModState::Loaded);
}

// ---------------------------------------------------------------------------
static void TestPanel(const fs::path& mods, const fs::path& tmp) {
    std::printf("\n== painel: aba Mods\n");
    overlay::ConfigStore cfg(tmp / "panel" / "modrix_config.json");
    std::string err;
    CHECK(cfg.SetEnabled("modrix.example_boss", false, &err));  // desligado numa sessão anterior

    FakeGame game;
    rt::ModLoader loader(&game);
    loader.SetDisabled(cfg.Disabled());
    loader.DiscoverDirectory(mods);
    loader.LoadAll();
    overlay::LoaderDataSource source(loader);
    PanelController panel(source, cfg);

    // Fechado: monta a lista uma vez, mas nunca consulta o checklist.
    CHECK(!panel.IsOpen() && panel.Tick(0));
    for (int t = 1; t <= 10; ++t) panel.Tick(t * 1000);
    CHECK(panel.ChecklistQueries() == 0);

    const overlay::ModRow* item = Row(panel, "modrix.example_item");
    const overlay::ModRow* boss = Row(panel, "modrix.example_boss");
    const overlay::ModRow* bc = Row(panel, "modrix.boss_checklist");
    CHECK(item && item->title == "Exemplo: Rubi Fictício" && item->subtitle == "v0.1.0 · Carregado" && item->enabled);
    CHECK(boss && boss->state == "disabled" && boss->subtitle == "v0.1.0 · Desativado" && !boss->enabled);
    CHECK(bc && bc->state == "loaded" && boss->pending.empty() && panel.ModsNotice().empty());

    // Desligar: grava, avisa que vale ao reiniciar, e NÃO descarrega nada.
    uint64_t v = panel.Version();
    CHECK(panel.HandleAction("toggle_mod", "modrix.example_item") && panel.Version() > v);
    item = Row(panel, "modrix.example_item");
    CHECK(!item->enabled && item->pending == "Será desativado ao reiniciar o jogo");
    CHECK(panel.ModsNotice() == "Reinicie o jogo para aplicar as mudanças.");
    CHECK(!cfg.IsEnabled("modrix.example_item") &&
          ReadFile(cfg.Path()).find("modrix.example_item") != std::string::npos);
    CHECK(loader.Find("modrix.example_item")->state == rt::ModState::Loaded);  // segue carregado
    CHECK(loader.Registry().Find(MODRIX_CONTENT_ITEM, "modrix.example_item:fictional_ruby") == 8192);

    // Religar um desativado: vale na próxima abertura.
    CHECK(panel.ToggleMod("modrix.example_boss"));
    CHECK(Row(panel, "modrix.example_boss")->pending == "Será ativado ao reiniciar o jogo");
    // Voltar ao estado de origem limpa os avisos.
    panel.ToggleMod("modrix.example_item");
    panel.ToggleMod("modrix.example_boss");
    CHECK(Row(panel, "modrix.example_item")->pending.empty() && panel.ModsNotice().empty());
    CHECK(!panel.ToggleMod("nao.existe", &err) && err.find("desconhecido") != std::string::npos);

    std::printf("\n== painel: aba Boss Checklist\n");
    CHECK(panel.HandleAction("toggle_open", "") && panel.IsOpen());
    CHECK(panel.HandleAction("tab", "checklist") && panel.ActiveTab() == Tab::Checklist);

    // Antes do primeiro frame do jogo.
    panel.Tick(20000);
    CHECK(panel.ChecklistQueries() == 1 && panel.Checklist().mode == ChecklistView::Mode::Message &&
          panel.Checklist().message == "Aguardando o jogo…");

    // No menu do jogo.
    game.inWorld = false;
    CHECK(!panel.Tick(20500) && panel.ChecklistQueries() == 1);  // antes do intervalo: não consulta
    CHECK(panel.Tick(21000) && panel.Checklist().message == "Entre num mundo");

    // Num mundo, início de jogo.
    game.inWorld = true;
    for (auto& p : game.progress) p = false;
    for (ModrixProgress f : {MODRIX_DOWNED_KING_SLIME, MODRIX_DOWNED_EYE_OF_CTHULHU, MODRIX_DOWNED_EVIL_BOSS})
        game.progress[f] = true;
    CHECK(panel.Tick(22000));
    const ChecklistView& c = panel.Checklist();
    CHECK(c.mode == ChecklistView::Mode::Ready && !c.hardmode);
    CHECK(c.summary.size() == 3 && c.summary[0].label == "Bosses" && c.summary[0].value == "3/17");
    CHECK(c.next == "Próximo: Queen Bee");
    CHECK(c.rows.front().header && c.rows.front().name == "Pré-hardmode");
    CHECK(Boss(c, "King Slime")->statusLabel == "Derrotado" && Boss(c, "Skeletron")->statusLabel == "Pendente");
    CHECK(Boss(c, "Goblin Army")->category == "Evento" && Boss(c, "Pumpking")->category == "Miniboss");
    size_t headers = 0;
    for (const auto& r : c.rows) headers += r.header;
    CHECK(headers == 2 && c.rows.size() == 32);  // 30 entradas + "Pré-hardmode" + "Hardmode"

    // Sem mudança no jogo: consulta, mas não "muda" o view-model.
    uint64_t before = panel.Version();
    CHECK(!panel.Tick(23000) && panel.Version() == before && panel.ChecklistQueries() == 4);

    // Um boss cai: na próxima atualização aparece.
    game.progress[MODRIX_DOWNED_QUEEN_BEE] = true;
    game.progress[MODRIX_DOWNED_DEERCLOPS] = std::nullopt;
    CHECK(panel.Tick(24000) && panel.Checklist().next == "Próximo: Skeletron");
    CHECK(Boss(panel.Checklist(), "Deerclops")->statusLabel == "Desconhecido");

    // Fechar para de consultar.
    panel.HandleAction("close", "");
    int queries = panel.ChecklistQueries();
    for (int t = 25; t < 40; ++t) panel.Tick(t * 1000);
    CHECK(!panel.IsOpen() && panel.ChecklistQueries() == queries);

    // Reabrir atualiza na hora, sem esperar o intervalo.
    game.progress[MODRIX_PROGRESS_HARDMODE] = true;
    panel.SetOpen(true);
    CHECK(panel.Tick(40001) && panel.Checklist().hardmode && panel.ChecklistQueries() == queries + 1);

    std::printf("\n== snapshot (view-model para o renderizador)\n");
    std::string snap = panel.SnapshotJson();
    std::string jerr;
    auto doc = rt::json::Parse(snap, &jerr);
    CHECK(doc.has_value());
    CHECK(snap.find("Próximo: Skeletron") != std::string::npos);  // acentos em UTF-8, sem escape
    CHECK(doc && *doc->Get("tab")->AsString() == "checklist" && *doc->Get("open")->AsBool());
    CHECK(doc && doc->Get("theme")->Get("defeated") && *doc->Get("theme")->Get("panel_width_dp")->AsNumber() == 340);
    CHECK(doc && doc->Get("mods")->Get("rows")->AsArray()->size() == 3);
}

// ---------------------------------------------------------------------------
static void TestChecklistNotLoaded(const fs::path& mods, const fs::path& tmp) {
    std::printf("\n== painel: Boss Checklist desligado\n");
    overlay::ConfigStore cfg(tmp / "nochk" / "modrix_config.json");
    std::string err;
    cfg.SetEnabled("modrix.boss_checklist", false, &err);
    FakeGame game;
    game.inWorld = true;
    rt::ModLoader loader(&game);
    loader.SetDisabled(cfg.Disabled());
    loader.DiscoverDirectory(mods);
    loader.LoadAll();
    overlay::LoaderDataSource source(loader);
    PanelController panel(source, cfg);
    panel.SetOpen(true);
    panel.SelectTab(Tab::Checklist);
    panel.Tick(0);
    CHECK(panel.Checklist().message == "O mod Boss Checklist não está carregado.");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s <build>/dist/mods\n", argv[0]);
        return 2;
    }
    if (!std::getenv("MODRIX_VERBOSE")) rt::SetLogSink(&QuietSink);
    fs::path tmp = fs::temp_directory_path() / "modrix_overlay_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    // Só os três mods de exemplo, sem lixo de build.
    fs::path mods = tmp / "mods";
    fs::create_directories(mods);
    for (const char* m : {"example_item", "example_boss", "boss_checklist"})
        fs::copy(fs::path(argv[1]) / m, mods / m, fs::copy_options::recursive);

    TestConfigStore(tmp);
    TestLoaderDisabled(mods);
    TestPanel(mods, tmp);
    TestChecklistNotLoaded(mods, tmp);
    fs::remove_all(tmp);
    std::printf("\n%d checks, %d falha%s: %s\n", g_checks, g_failures, g_failures == 1 ? "" : "s",
                g_failures ? "FALHOU" : "TODOS OS TESTES PASSARAM");
    return g_failures ? 1 : 0;
}
