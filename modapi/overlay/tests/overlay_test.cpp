// Testes da lógica do painel (sem desenho) com o jogo simulado e os mods reais.
//   overlay_test <build>/dist/mods
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../../tests/fake_game.h"
#include "json.h"
#include "tlmod/overlay/config_store.h"
#include "tlmod/overlay/panel.h"
#include "tlmod/runtime/log.h"
#include "tlmod/runtime/mod_loader.h"

using namespace tlmod;
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

static void QuietSink(TLModLogLevel, const std::string&, const std::string&) {}

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
    std::printf("\n== configuração (tlmod_config.json)\n");
    fs::path file = tmp / "cfg" / "tlmod_config.json";
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
    overlay::ConfigStore readonly(tmp / "not_a_dir" / "tlmod_config.json");
    CHECK(!readonly.SetEnabled("a.mod", false, &err) && readonly.IsEnabled("a.mod"));
}

// ---------------------------------------------------------------------------
static void TestLoaderDisabled(const fs::path& mods) {
    std::printf("\n== ModLoader respeita mods desligados\n");
    FakeGame game;
    rt::ModLoader loader(&game);
    loader.SetDisabled({"tlmod.example_item"});
    loader.DiscoverDirectory(mods);
    rt::ModManifest dep;
    dep.apiVersion = TLMOD_API_VERSION;
    dep.id = "test.needs_item";
    dep.name = "Precisa do item";
    dep.version = *rt::SemVer::Parse("1.0.0");
    dep.dependencies.push_back({"tlmod.example_item", *rt::VersionReq::Parse("*"), false});
    loader.AddBuiltin(dep, [](const TLModHostApi*, TLModContext*) { return 0; });
    loader.LoadAll();
    CHECK(loader.Find("tlmod.example_item")->state == rt::ModState::Disabled);
    CHECK(!loader.Registry().Find(TLMOD_CONTENT_ITEM, "tlmod.example_item:fictional_ruby"));  // nem foi aberto
    CHECK(loader.Find("test.needs_item")->state == rt::ModState::Skipped &&
          loader.Find("test.needs_item")->error.find("desativada") != std::string::npos);
    CHECK(loader.Find("tlmod.boss_checklist")->state == rt::ModState::Loaded);
}

// ---------------------------------------------------------------------------
static void TestPanel(const fs::path& mods, const fs::path& tmp) {
    std::printf("\n== painel: aba Mods\n");
    overlay::ConfigStore cfg(tmp / "panel" / "tlmod_config.json");
    std::string err;
    CHECK(cfg.SetEnabled("tlmod.example_boss", false, &err));  // desligado numa sessão anterior

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

    const overlay::ModRow* item = Row(panel, "tlmod.example_item");
    const overlay::ModRow* boss = Row(panel, "tlmod.example_boss");
    const overlay::ModRow* bc = Row(panel, "tlmod.boss_checklist");
    CHECK(item && item->title == "Exemplo: Rubi Fictício" && item->subtitle == "v0.1.0 · Carregado" && item->enabled);
    CHECK(boss && boss->state == "disabled" && boss->subtitle == "v0.1.0 · Desativado" && !boss->enabled);
    CHECK(bc && bc->state == "loaded" && boss->pending.empty() && panel.ModsNotice().empty());

    // Desligar: grava, avisa que vale ao reiniciar, e NÃO descarrega nada.
    uint64_t v = panel.Version();
    CHECK(panel.HandleAction("toggle_mod", "tlmod.example_item") && panel.Version() > v);
    item = Row(panel, "tlmod.example_item");
    CHECK(!item->enabled && item->pending == "Será desativado ao reiniciar o jogo");
    CHECK(panel.ModsNotice() == "Reinicie o jogo para aplicar as mudanças.");
    CHECK(!cfg.IsEnabled("tlmod.example_item") &&
          ReadFile(cfg.Path()).find("tlmod.example_item") != std::string::npos);
    CHECK(loader.Find("tlmod.example_item")->state == rt::ModState::Loaded);  // segue carregado
    CHECK(loader.Registry().Find(TLMOD_CONTENT_ITEM, "tlmod.example_item:fictional_ruby") == 8192);

    // Religar um desativado: vale na próxima abertura.
    CHECK(panel.ToggleMod("tlmod.example_boss"));
    CHECK(Row(panel, "tlmod.example_boss")->pending == "Será ativado ao reiniciar o jogo");
    // Voltar ao estado de origem limpa os avisos.
    panel.ToggleMod("tlmod.example_item");
    panel.ToggleMod("tlmod.example_boss");
    CHECK(Row(panel, "tlmod.example_item")->pending.empty() && panel.ModsNotice().empty());
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
    for (TLModProgress f : {TLMOD_DOWNED_KING_SLIME, TLMOD_DOWNED_EYE_OF_CTHULHU, TLMOD_DOWNED_EVIL_BOSS})
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
    game.progress[TLMOD_DOWNED_QUEEN_BEE] = true;
    game.progress[TLMOD_DOWNED_DEERCLOPS] = std::nullopt;
    CHECK(panel.Tick(24000) && panel.Checklist().next == "Próximo: Skeletron");
    CHECK(Boss(panel.Checklist(), "Deerclops")->statusLabel == "Desconhecido");

    // Fechar para de consultar.
    panel.HandleAction("close", "");
    int queries = panel.ChecklistQueries();
    for (int t = 25; t < 40; ++t) panel.Tick(t * 1000);
    CHECK(!panel.IsOpen() && panel.ChecklistQueries() == queries);

    // Reabrir atualiza na hora, sem esperar o intervalo.
    game.progress[TLMOD_PROGRESS_HARDMODE] = true;
    panel.SetOpen(true);
    CHECK(panel.Tick(40001) && panel.Checklist().hardmode && panel.ChecklistQueries() == queries + 1);

    std::printf("\n== snapshot (view-model para o renderizador)\n");
    std::string snap = panel.SnapshotJson();
    std::string jerr;
    auto doc = rt::json::Parse(snap, &jerr);
    CHECK(doc.has_value());
    CHECK(snap.find("Próximo: Skeletron") != std::string::npos);  // acentos em UTF-8, sem escape
    CHECK(doc && *doc->Get("tab")->AsString() == "checklist" && *doc->Get("open")->AsBool());
    CHECK(doc && doc->Get("theme")->Get("defeated") && *doc->Get("theme")->Get("panel_width_dp")->AsNumber() == 400);
    CHECK(doc && doc->Get("mods")->Get("rows")->AsArray()->size() == 3);
}

// ---------------------------------------------------------------------------
static void TestChecklistNotLoaded(const fs::path& mods, const fs::path& tmp) {
    std::printf("\n== painel: Boss Checklist desligado\n");
    overlay::ConfigStore cfg(tmp / "nochk" / "tlmod_config.json");
    std::string err;
    cfg.SetEnabled("tlmod.boss_checklist", false, &err);
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

// Erro de carga inteiro no painel e aba Log com as últimas linhas do log real.
static void TestErrorAndLog(const fs::path& tmp) {
    std::printf("\n== erro completo e aba Log\n");
    // dlerror longo, com a causa no fim (o que antes ficava escondido pelas reticências).
    fs::path mods = tmp / "mods_err";
    fs::create_directories(mods / "broken" / "lib" / "linux-x86_64");
    std::ofstream(mods / "broken" / "manifest.json") << R"({"tlmod_api": 1, "id": "test.broken", "name": "Quebrado",
        "version": "1.0.0", "entry": {"linux-x86_64": "lib/linux-x86_64/libbroken.so"}})";
    std::ofstream(mods / "broken" / "lib" / "linux-x86_64" / "libbroken.so") << "sem ELF";
    rt::ResetLogForTest();
    if (!std::getenv("TLMOD_VERBOSE")) rt::SetLogSink(&QuietSink);
    const fs::path logFile = tmp / "tlmod" / "tlmod.log";
    CHECK(rt::SetLogFile(logFile));

    FakeGame game;
    rt::ModLoader loader(&game);
    loader.SetLibraryCacheDir(tmp / "code_cache" / "tlmod");
    loader.DiscoverDirectory(mods);
    loader.LoadAll();
    const rt::ModRecord* rec = loader.Find("test.broken");
    CHECK(rec && rec->state == rt::ModState::Failed && rec->error.size() > 120);

    overlay::LoaderDataSource source(loader);
    overlay::ConfigStore config(tmp / "cfg_err" / "tlmod_config.json");
    PanelController panel(source, config);
    panel.Tick(0);
    const overlay::ModRow* row = Row(panel, "test.broken");
    CHECK(row && rec && row->detail == rec->error);  // inteiro, sem corte
    auto snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    const rt::json::Value* jrow = snap ? &(*snap->Get("mods")->Get("rows")->AsArray())[0] : nullptr;
    CHECK(jrow && rec && *jrow->Get("detail")->AsString() == rec->error);
    CHECK(rec && rec->error.find("file too short") != std::string::npos);

    // Aba Log: fechado não busca; aberto na aba, mostra as últimas linhas e o arquivo.
    CHECK(panel.LogTab().lines.empty());
    CHECK(panel.HandleAction("toggle_open", "") && panel.HandleAction("tab", "log"));
    CHECK(panel.ActiveTab() == Tab::Log);
    CHECK(panel.Tick(10));
    const overlay::LogView& lv = panel.LogTab();
    CHECK(lv.file == logFile.string() && !lv.lines.empty());
    bool sawError = false;
    for (const auto& l : lv.lines) sawError = sawError || (l.level == "E" && l.text.find("[test.broken]") != std::string::npos);
    CHECK(sawError);
    CHECK(lv.lines.size() <= PanelController::kLogTailLines);
    // Linha nova: aparece no próximo refresh (1 s), não antes.
    rt::Log(TLMOD_LOG_WARN, "teste", "linha nova");
    CHECK(!panel.Tick(500));
    CHECK(panel.Tick(10 + PanelController::kLogRefreshMs) && panel.LogTab().lines.back().text.find("W [teste] linha nova") != std::string::npos);
    CHECK(!panel.Tick(10 + 2 * PanelController::kLogRefreshMs));  // nada mudou: não redesenha
    snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    CHECK(snap && *snap->Get("tab")->AsString() == "log" && snap->Get("tabs")->AsArray()->size() == 3);
    CHECK(snap && *snap->Get("log")->Get("file")->AsString() == logFile.string());
    CHECK(snap && snap->Get("log")->Get("lines")->AsArray()->size() == panel.LogTab().lines.size());
    CHECK(ReadFile(logFile).find("W [teste] linha nova") != std::string::npos);
    rt::ResetLogForTest();
    if (!std::getenv("TLMOD_VERBOSE")) rt::SetLogSink(&QuietSink);
}

// Aba de mod: o tlmod.devtools real ganha a aba "Ferramentas" e recebe os toques.
static void TestModTab(const fs::path& modsSrc, const fs::path& tmp) {
    std::printf("\n== aba de mod (Ferramentas)\n");
    fs::path mods = tmp / "mods_tools";
    fs::create_directories(mods);
    for (const char* m : {"devtools", "example_item"})
        fs::copy(modsSrc / m, mods / m, fs::copy_options::recursive);
    FakeGame game;
    game.vanilla[TLMOD_CONTENT_ITEM] = FakeGame::kItemCount;
    game.vanilla[TLMOD_CONTENT_NPC] = FakeGame::kVanillaNpcs;
    rt::ModLoader loader(&game);
    loader.DiscoverDirectory(mods);
    CHECK(loader.LoadAll() == 2);
    for (int i = 0; i < 40 && !loader.CatalogReady(); ++i) game.Frame();

    overlay::LoaderDataSource source(loader);
    CHECK(source.PanelMods() == std::vector<std::string>{"tlmod.devtools"});
    overlay::ConfigStore config(tmp / "cfg_tools" / "tlmod_config.json");
    PanelController panel(source, config);
    panel.Tick(0);
    CHECK(panel.ModTabs().size() == 1 && panel.ModTabs()[0].title == "Ferramentas");
    auto snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    const auto& tabs = *snap->Get("tabs")->AsArray();
    CHECK(tabs.size() == 4 && *tabs[3].Get("id")->AsString() == "mod:tlmod.devtools" &&
          *tabs[3].Get("label")->AsString() == "Ferramentas");
    CHECK(snap->Get("modtab")->IsNull());

    CHECK(panel.HandleAction("toggle_open", "") && panel.HandleAction("tab", "mod:tlmod.devtools"));
    CHECK(!panel.HandleAction("tab", "mod:nao.existe"));
    CHECK(panel.ActiveTab() == Tab::ModTab && panel.ActiveModTab() == "tlmod.devtools");
    panel.Tick(10);
    snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    CHECK(snap && *snap->Get("tab")->AsString() == "mod:tlmod.devtools");
    const rt::json::Value* content = snap->Get("modtab")->Get("content");
    CHECK(content && content->Get("sections")->AsArray()->size() == 4);
    CHECK(*(*content->Get("sections")->AsArray())[3].Get("search")->Get("id")->AsString() == "item");

    // Busca pelo campo do painel e toque num botão: o mod recebe o texto e responde.
    CHECK(panel.HandleAction("mod_command", "search item lente"));
    CHECK(panel.Tick(20) && panel.SnapshotJson().find("\"give 38 10\"") != std::string::npos);
    game.inWorld = true;
    CHECK(panel.HandleAction("mod_command", "give 38 10"));
    CHECK(panel.ModTabs()[0].notice == "10x Lente no invent\xc3\xa1rio");
    CHECK(!panel.HandleAction("mod_command", "abracadabra") &&
          panel.ModTabs()[0].notice.rfind("N\xc3\xa3o deu certo", 0) == 0);
    panel.Tick(30);
    CHECK(!panel.ModTabs()[0].notice.empty());
    panel.Tick(30 + PanelController::kModNoticeMs);
    CHECK(panel.ModTabs()[0].notice.empty());  // o aviso some sozinho
    // A aba acompanha o estado do mod (aqui, a hora que o jogo andou).
    game.Frame();
    std::string before = panel.SnapshotJson();
    game.worldTime->second += 3600;
    game.Frame();
    CHECK(panel.Tick(30 + PanelController::kModNoticeMs + PanelController::kModTabRefreshMs) &&
          panel.SnapshotJson() != before);
    // Outra aba: comando não vai para lugar nenhum.
    CHECK(panel.HandleAction("tab", "mods") && panel.ActiveModTab().empty() && !panel.HandleAction("mod_command", "heal"));
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s <build>/dist/mods\n", argv[0]);
        return 2;
    }
    if (!std::getenv("TLMOD_VERBOSE")) rt::SetLogSink(&QuietSink);
    fs::path tmp = fs::temp_directory_path() / "tlmod_overlay_test";
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
    TestErrorAndLog(tmp);
    TestModTab(argv[1], tmp);
    fs::remove_all(tmp);
    std::printf("\n%d checks, %d falha%s: %s\n", g_checks, g_failures, g_failures == 1 ? "" : "s",
                g_failures ? "FALHOU" : "TODOS OS TESTES PASSARAM");
    return g_failures ? 1 : 0;
}
