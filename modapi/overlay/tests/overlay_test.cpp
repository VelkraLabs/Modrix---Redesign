// Testes da lógica do painel (sem desenho) com o jogo simulado e os mods reais.
//   overlay_test <build>/dist/mods
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "../../tests/fake_game.h"
#include "json.h"
#include "tlmod/overlay/config_store.h"
#include "tlmod/overlay/panel.h"
#include "tlmod/overlay/settings_tab.h"
#include "tlmod/runtime/log.h"
#include "tlmod/runtime/mod_loader.h"

using namespace tlmod;
using overlay::ChecklistView;
using overlay::PanelController;
using overlay::Surface;
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
    std::printf("\n== painel: mods e interruptores\n");
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
    CHECK(panel.OpenSurface() == Surface::None && panel.Tick(0));
    for (int t = 1; t <= 10; ++t) panel.Tick(t * 1000);
    CHECK(panel.ChecklistQueries() == 0);

    const overlay::ModRow* item = Row(panel, "tlmod.example_item");
    const overlay::ModRow* boss = Row(panel, "tlmod.example_boss");
    const overlay::ModRow* bc = Row(panel, "tlmod.boss_checklist");
    CHECK(item && item->title == "Exemplo: Rubi Fictício" && item->subtitle == "v0.1.0 · Carregado" && item->enabled);
    CHECK(boss && boss->state == "disabled" && boss->subtitle == "v0.1.0 · Desativado" && !boss->enabled);
    CHECK(bc && bc->state == "loaded" && boss->pending.empty() && panel.ModsNotice().empty());
    // Ícone da Oficina nativa: o item registrado pelo mod; sem conteúdo, o Livro; desativado,
    // pelo que o manifest declara (o chefe declara um NPC: o Olho de Aparência Suspeita).
    auto icon = [&](const std::string& id) {
        for (const overlay::ModInfo& m : source.Mods())
            if (m.id == id) return m.iconItem;
        return -1;
    };
    CHECK(icon("tlmod.example_item") == 8192 && icon("tlmod.example_boss") == 43 &&
          icon("tlmod.boss_checklist") == 149);

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

    std::printf("\n== painel: gaveta do Boss Checklist\n");
    // Sem mundo (o jogo ainda não rodou um frame): sem ícones; o "M" abre o Log.
    CHECK(!panel.LaunchersVisible() && panel.ChecklistInstalled());
    CHECK(panel.HandleAction("toggle_open", "") && panel.OpenSurface() == Surface::Log);
    // O ícone do checklist abre a gaveta no lugar do Log (uma superfície por vez).
    CHECK(panel.HandleAction("open", "checklist") && panel.OpenSurface() == Surface::Checklist);

    // Antes do primeiro frame do jogo.
    panel.Tick(20000);
    CHECK(panel.ChecklistQueries() == 1 && panel.Checklist().mode == ChecklistView::Mode::Message &&
          panel.Checklist().message == "Aguardando o jogo…");

    // No menu do jogo: os ícones somem e a gaveta fecha (sem consultar mais).
    game.inWorld = false;
    CHECK(panel.Tick(20500) && panel.OpenSurface() == Surface::None && !panel.LaunchersVisible());
    CHECK(!panel.Tick(21000) && panel.ChecklistQueries() == 1);

    // Num mundo, início de jogo: os ícones voltam, e a gaveta reabre já com os dados.
    game.inWorld = true;
    for (auto& p : game.progress) p = false;
    for (TLModProgress f : {TLMOD_DOWNED_KING_SLIME, TLMOD_DOWNED_EYE_OF_CTHULHU, TLMOD_DOWNED_EVIL_BOSS})
        game.progress[f] = true;
    CHECK(panel.Tick(21500) && panel.LaunchersVisible() && panel.OpenSurface() == Surface::None);
    CHECK(panel.HandleAction("open", "checklist") && panel.Tick(22000));
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
    CHECK(!panel.Tick(23000) && panel.Version() == before && panel.ChecklistQueries() == 3);

    // Um boss cai: na próxima atualização aparece.
    game.progress[TLMOD_DOWNED_QUEEN_BEE] = true;
    game.progress[TLMOD_DOWNED_DEERCLOPS] = std::nullopt;
    CHECK(panel.Tick(24000) && panel.Checklist().next == "Próximo: Skeletron");
    CHECK(Boss(panel.Checklist(), "Deerclops")->statusLabel == "Desconhecido");

    // Tocar no ícone de novo fecha; fechada, para de consultar.
    CHECK(panel.HandleAction("open", "checklist") && panel.OpenSurface() == Surface::None);
    int queries = panel.ChecklistQueries();
    for (int t = 25; t < 40; ++t) panel.Tick(t * 1000);
    CHECK(panel.ChecklistQueries() == queries);

    // Reabrir atualiza na hora, sem esperar o intervalo.
    game.progress[TLMOD_PROGRESS_HARDMODE] = true;
    panel.HandleAction("open", "checklist");
    CHECK(panel.Tick(40001) && panel.Checklist().hardmode && panel.ChecklistQueries() == queries + 1);

    std::printf("\n== snapshot (view-model para o renderizador)\n");
    std::string snap = panel.SnapshotJson();
    std::string jerr;
    auto doc = rt::json::Parse(snap, &jerr);
    CHECK(doc.has_value());
    CHECK(snap.find("Próximo: Skeletron") != std::string::npos);  // acentos em UTF-8, sem escape
    CHECK(doc && *doc->Get("surface")->AsString() == "checklist" && *doc->Get("launchers_visible")->AsBool());
    CHECK(doc && doc->Get("theme")->Get("defeated") && *doc->Get("theme")->Get("panel_width_dp")->AsNumber() == 400);
    CHECK(doc && *doc->Get("theme")->Get("drawer_width_dp")->AsNumber() == 360);
    const auto* launchers = doc ? doc->Get("launchers")->AsArray() : nullptr;
    CHECK(launchers && launchers->size() == 1 && *(*launchers)[0].Get("id")->AsString() == "checklist" &&
          *(*launchers)[0].Get("style")->AsString() == "drawer" && *(*launchers)[0].Get("icon")->AsString() == "checklist" &&
          *(*launchers)[0].Get("active")->AsBool());
    CHECK(doc && !doc->Get("tabs") && !doc->Get("mods"));  // o "M" não tem mais abas
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
    panel.Show(Surface::Checklist);
    panel.Tick(0);
    CHECK(panel.Checklist().message == "O mod Boss Checklist não está carregado.");
    // Sem o mod, sem o ícone.
    auto snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    CHECK(!panel.ChecklistInstalled() && snap && snap->Get("launchers")->AsArray()->empty());
    // Mod só de NPC carregado: o ícone é o Olho de Aparência Suspeita.
    for (const overlay::ModInfo& m : source.Mods())
        if (m.id == "tlmod.example_boss") CHECK(m.state == rt::ModState::Loaded && m.iconItem == 43);
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
    CHECK(rec && rec->error.find("file too short") != std::string::npos);

    // Log (o "M"): fechado não busca; aberto, mostra as últimas linhas e o arquivo.
    CHECK(panel.LogTab().lines.empty());
    CHECK(panel.HandleAction("toggle_open", "") && panel.OpenSurface() == Surface::Log);
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
    auto snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    CHECK(snap && *snap->Get("surface")->AsString() == "log" && *snap->Get("panel_title")->AsString() == "TLMod · Log");
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
    const auto& launchers = *snap->Get("launchers")->AsArray();
    CHECK(launchers.size() == 1 && *launchers[0].Get("id")->AsString() == "mod:tlmod.devtools" &&
          *launchers[0].Get("label")->AsString() == "Ferramentas" && *launchers[0].Get("icon")->AsString() == "tools" &&
          *launchers[0].Get("style")->AsString() == "modal" && *launchers[0].Get("letter")->AsString() == "F");
    CHECK(snap->Get("modtab")->IsNull());

    CHECK(panel.HandleAction("open", "mod:tlmod.devtools"));
    CHECK(!panel.HandleAction("open", "mod:nao.existe"));
    CHECK(panel.OpenSurface() == Surface::ModTab && panel.ActiveModTab() == "tlmod.devtools");
    panel.Tick(10);
    snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    CHECK(snap && *snap->Get("surface")->AsString() == "mod:tlmod.devtools" &&
          *snap->Get("modtab")->Get("title")->AsString() == "Ferramentas");
    const rt::json::Value* content = snap->Get("modtab")->Get("content");
    CHECK(content && content->Get("sections")->AsArray()->size() == 4);
    CHECK(*(*content->Get("sections")->AsArray())[3].Get("search")->Get("id")->AsString() == "item");

    // Busca pelo campo do painel e toque num botão: o mod recebe o texto e responde.
    CHECK(panel.HandleAction("mod_command", "search item lente"));
    CHECK(panel.Tick(20) && panel.SnapshotJson().find("\"give 38 10\"") != std::string::npos);
    game.inWorld = true;
    CHECK(panel.HandleAction("mod_command", "give 38 10"));
    CHECK(panel.ModTabs()[0].notice == "10x Lente entregue (o que n\xc3\xa3o couber cai no ch\xc3\xa3o)");
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
    // Sair do mundo fecha o modal.
    game.inWorld = false;
    CHECK(panel.Tick(100000) && panel.OpenSurface() == Surface::None && !panel.LaunchersVisible());
    // O "M" (Log) aberto: comando não vai para lugar nenhum.
    CHECK(panel.HandleAction("toggle_open", "") && panel.ActiveModTab().empty() && !panel.HandleAction("mod_command", "heal"));
}

// ---------------------------------------------------------------------------
// Oficina de Mods: aberta pelo botão do menu do jogo (outra thread) ou pela aba Mods.
static void TestWorkshop(const fs::path& mods, const fs::path& tmp) {
    std::printf("\n== Oficina de Mods\n");
    fs::path dir = tmp / "mods_ws";
    fs::copy(mods, dir, fs::copy_options::recursive);
    fs::create_directories(dir / "broken" / "lib" / "linux-x86_64");
    std::ofstream(dir / "broken" / "manifest.json") << R"({"tlmod_api": 1, "id": "test.broken", "name": "Quebrado",
        "version": "1.0.0", "entry": {"linux-x86_64": "lib/linux-x86_64/libbroken.so"}})";
    std::ofstream(dir / "broken" / "lib" / "linux-x86_64" / "libbroken.so") << "sem ELF";
    overlay::ConfigStore cfg(tmp / "ws" / "tlmod_config.json");
    std::string err;
    CHECK(cfg.SetEnabled("tlmod.example_boss", false, &err));

    FakeGame game;
    rt::ModLoader loader(&game);
    loader.SetDisabled(cfg.Disabled());
    loader.SetLibraryCacheDir(tmp / "code_cache_ws" / "tlmod");
    loader.DiscoverDirectory(dir);
    loader.LoadAll();
    overlay::LoaderDataSource source(loader);
    PanelController panel(source, cfg);
    panel.Tick(0);
    panel.Show(Surface::Log);

    // Pedido da thread do jogo: só abre no Tick (thread do renderizador), e fecha o painel.
    std::thread([&] { panel.RequestWorkshop(); }).join();
    CHECK(!panel.IsWorkshopOpen() && panel.OpenSurface() == Surface::Log);
    CHECK(panel.Tick(1) && panel.IsWorkshopOpen() && panel.OpenSurface() == Surface::None);
    CHECK(!panel.Tick(2));  // o pedido foi consumido

    auto snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    const rt::json::Value* ws = snap ? snap->Get("workshop") : nullptr;
    CHECK(ws && *ws->Get("open")->AsBool() && *ws->Get("title")->AsString() == "Oficina de Mods");
    CHECK(ws && *ws->Get("summary")->AsString() == "4 mods instalados · 2 ativos");
    CHECK(snap && !*snap->Get("fab")->Get("visible")->AsBool());  // o botão flutuante some
    const auto* sections = ws ? ws->Get("sections")->AsArray() : nullptr;
    CHECK(sections && sections->size() == 3);
    auto section = [&](size_t i) { return *(*sections)[i].Get("title")->AsString(); };
    auto rowsOf = [&](size_t i) { return (*sections)[i].Get("rows")->AsArray(); };
    CHECK(sections && section(0) == "Ativos (2)" && section(1) == "Desativados (1)" && section(2) == "Com erro (1)");
    // Em ordem de nome: Boss Checklist antes de "Exemplo: Rubi Fictício".
    const rt::json::Value* ruby = sections ? &(*rowsOf(0))[1] : nullptr;
    CHECK(sections && *(*rowsOf(0))[0].Get("id")->AsString() == "tlmod.boss_checklist");
    CHECK(ruby && *ruby->Get("id")->AsString() == "tlmod.example_item" && *ruby->Get("version")->AsString() == "v0.1.0");
    CHECK(ruby && *ruby->Get("authors")->AsString() == "por TLMod" &&
          *ruby->Get("content")->AsString() == "Adiciona: 1 item" &&
          ruby->Get("description")->AsString()->find("registra um único item") != std::string::npos);
    const rt::json::Value* broken = sections ? &(*rowsOf(2))[0] : nullptr;
    CHECK(broken && *broken->Get("state_label")->AsString() == "Erro" && broken->Get("error")->AsString()->empty() &&
          *broken->Get("error_toggle")->AsString() == "Ver o erro");

    // Ver o erro: a mensagem inteira; tocar de novo esconde.
    CHECK(panel.HandleAction("workshop_expand", "test.broken") && panel.IsExpanded("test.broken"));
    snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    broken = &(*(*snap->Get("workshop")->Get("sections")->AsArray())[2].Get("rows")->AsArray())[0];
    CHECK(*broken->Get("error")->AsString() == loader.Find("test.broken")->error &&
          *broken->Get("error_toggle")->AsString() == "Esconder o erro");
    CHECK(panel.HandleAction("workshop_expand", "test.broken") && !panel.IsExpanded("test.broken"));

    // Ligar/desligar na Oficina: vale ao reiniciar; o mod não muda de seção.
    CHECK(panel.HandleAction("toggle_mod", "tlmod.example_boss") && cfg.IsEnabled("tlmod.example_boss"));
    snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    const rt::json::Value* boss = &(*(*snap->Get("workshop")->Get("sections")->AsArray())[1].Get("rows")->AsArray())[0];
    CHECK(*boss->Get("id")->AsString() == "tlmod.example_boss" && *boss->Get("enabled")->AsBool() &&
          *boss->Get("pending")->AsString() == "Será ativado ao reiniciar o jogo");
    CHECK(*snap->Get("workshop")->Get("notice")->AsString() == "Reinicie o jogo para aplicar as mudanças.");

    // Fechar; a ação workshop_open também abre (Oficina do painel no lugar da do jogo).
    CHECK(panel.HandleAction("workshop_close", "") && !panel.IsWorkshopOpen());
    snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    CHECK(*snap->Get("fab")->Get("visible")->AsBool());
    CHECK(panel.HandleAction("workshop_open", "") && panel.IsWorkshopOpen());

    // Mudança feita por fora (a Oficina nativa, na thread do jogo): o próximo Tick refaz as linhas.
    const bool before = Row(panel, "tlmod.example_item")->enabled;
    CHECK(cfg.SetEnabled("tlmod.example_item", !before, &err));
    CHECK(panel.Tick(10) && Row(panel, "tlmod.example_item")->enabled == !before);
    CHECK(!panel.Tick(11));
}

// ---------------------------------------------------------------------------
// Oficina nativa (a tela do jogo): entradas prontas e a configuração vista pelas duas threads.
static void TestNativeWorkshop(const fs::path& tmp) {
    std::printf("\n== Oficina nativa: entradas e configuração compartilhada\n");
    std::vector<overlay::ModInfo> mods = {
        {"b.gems", "gemas", "0.2.0", rt::ModState::Failed, "dlopen failed: símbolo ausente", "Acme", "Gemas.", "4 itens"},
        {"a.ruby", "Rubi", "0.1.0", rt::ModState::Loaded, "", "TLMod", "Um rubi.", "1 item"},
        {"c.boss", "Boss", "0.1.0", rt::ModState::Disabled, "desativado", "", "", "1 NPC"},
        {"d.check", "Boss Checklist", "0.1.0", rt::ModState::Loaded, "", "", "", "", 149},
    };
    mods[1].iconItem = 8192;
    overlay::ConfigStore cfg(tmp / "native" / "tlmod_config.json");
    std::string err;
    CHECK(cfg.SetEnabled("c.boss", false, &err));
    auto entries = overlay::BuildWorkshopEntries(mods, cfg);
    // Ativos (por nome), desativados, com erro.
    CHECK(entries.size() == 4 && entries[0].id == "d.check" && entries[1].id == "a.ruby" && entries[2].id == "c.boss" &&
          entries[3].id == "b.gems");
    CHECK(entries[1].status == "Ativo" && entries[1].kind == 0 && entries[1].version == "v0.1.0" &&
          entries[1].summary == "1 item" && entries[1].toggleLabel == "Desativar");
    CHECK(entries[1].details.find("por TLMod") != std::string::npos &&
          entries[1].details.find("Adiciona: 1 item") != std::string::npos &&
          entries[1].details.find("Estado: Carregado") != std::string::npos);
    CHECK(entries[2].status == "Desativado" && entries[2].kind == 1 && entries[2].toggleLabel == "Ativar");
    CHECK(entries[1].iconItem == 8192 && entries[0].iconItem == 149 && entries[2].iconItem == 0);
    CHECK(entries[0].summary.empty() && entries[2].summary == "1 NPC");  // sem conteúdo nem autores: vazio
    CHECK(entries[3].status == "Erro" && entries[3].kind == 2 &&
          entries[3].details.find("Erro:\ndlopen failed: símbolo ausente") != std::string::npos);
    // Desligar um ativo: "Será desativado", cor de aviso, e a pendência no texto.
    uint64_t rev = cfg.Revision();
    CHECK(cfg.SetEnabled("a.ruby", false, &err) && cfg.Revision() > rev);
    entries = overlay::BuildWorkshopEntries(mods, cfg);
    CHECK(entries[1].status == "Será desativado" && entries[1].kind == 3 && entries[1].toggleLabel == "Ativar" &&
          entries[1].details.find("(Será desativado ao reiniciar o jogo)") != std::string::npos);
    rev = cfg.Revision();
    CHECK(cfg.SetEnabled("a.ruby", false, &err) && cfg.Revision() == rev);  // sem mudança, mesma revisão

    // A Oficina nativa (thread do jogo) muda a configuração enquanto o painel lê (thread de UI).
    std::thread game([&] {
        for (int i = 0; i < 200; ++i) cfg.SetEnabled("d.check", i % 2 == 0, nullptr);
    });
    int reads = 0;
    for (int i = 0; i < 200; ++i) reads += cfg.IsEnabled("d.check") ? 1 : 0, (void)cfg.Disabled();
    game.join();
    CHECK(reads >= 0 && !cfg.IsEnabled("d.check"));  // o último SetEnabled (i = 199) desliga
}

// ---------------------------------------------------------------------------
// Botões de mods na tela: a configuração (tamanho, opacidade, posições), a aba Mods
// das Configurações do jogo e o modo de edição do painel.
static void TestButtonLayout(const fs::path& mods, const fs::path& tmp) {
    std::printf("\n== botões de mods: configuração, aba Mods e modo de edição\n");
    using overlay::ButtonLayout;
    using overlay::ButtonPlacement;
    using overlay::SettingsRow;
    fs::path file = tmp / "buttons" / "tlmod_config.json";
    overlay::ConfigStore cfg(file);
    std::string err;
    CHECK(cfg.Load(&err) && cfg.Buttons() == ButtonLayout{});
    CHECK(cfg.Buttons().scalePct == 100 && cfg.Buttons().opacityPct == 100 && cfg.Buttons().positions.empty());
    const uint64_t rev = cfg.Revision();
    CHECK(cfg.SetButtonScale(130, &err) && cfg.SetButtonOpacity(10, &err) && cfg.Revision() > rev);
    CHECK(cfg.Buttons().scalePct == 130 && cfg.Buttons().opacityPct == 30);  // ajustada ao mínimo
    CHECK(cfg.SetButtonPosition("menu", {12000, -5}, &err) &&
          (cfg.Buttons().positions.at("menu") == ButtonPlacement{10000, 0}));
    CHECK(!cfg.SetButtonPosition("", {1, 1}, &err));
    const uint64_t same = cfg.Revision();
    CHECK(cfg.SetButtonScale(130, &err) && cfg.Revision() == same);  // nada mudou: nem grava
    CHECK(cfg.SetEnabled("a.mod", false, &err));  // a outra parte da configuração continua
    const std::string text = ReadFile(file);
    CHECK(text.find(R"("buttons":{"scale":130,"opacity":30,"positions":{"menu":[10000,0]}})") != std::string::npos);
    CHECK(text.find(R"("disabled_mods":["a.mod"])") != std::string::npos);
    overlay::ConfigStore reread(file);
    CHECK(reread.Load(&err) && reread.Buttons() == cfg.Buttons() && !reread.IsEnabled("a.mod"));
    // Valores fora da faixa ou inválidos: ajustados ou ignorados, sem erro (é só aparência).
    std::ofstream(file) << R"({"schema":1,"disabled_mods":["x"],"buttons":{"scale":999,"opacity":"muito",
        "positions":{"menu":[5000,5000],"checklist":[1],"":[1,2],"mod:a":[-3,20000]}}})";
    overlay::ConfigStore lenient(file);
    CHECK(lenient.Load(&err) && !lenient.IsEnabled("x"));
    ButtonLayout b = lenient.Buttons();
    CHECK(b.scalePct == 150 && b.opacityPct == 100 && b.positions.size() == 2);
    CHECK((b.positions.at("menu") == ButtonPlacement{5000, 5000}) && (b.positions.at("mod:a") == ButtonPlacement{0, 10000}));
    std::ofstream(file) << R"({"schema":1,"buttons":7})";
    CHECK(lenient.Load(&err) && lenient.Buttons() == ButtonLayout{});
    CHECK(cfg.ResetButtons(&err) && cfg.Buttons() == ButtonLayout{} && !cfg.IsEnabled("a.mod"));

    FakeGame game;
    game.inWorld = false;  // no menu principal
    rt::ModLoader loader(&game);
    loader.DiscoverDirectory(mods);
    loader.LoadAll();
    overlay::LoaderDataSource source(loader);
    PanelController panel(source, cfg);
    panel.Tick(0);

    // Aba Mods: um botão e duas barras de 10 em 10%.
    overlay::ModSettingsTab tab(cfg, &panel);
    std::vector<SettingsRow> rows = tab.Rows();
    CHECK(rows.size() == 3 && rows[0].kind == SettingsRow::Kind::Button && rows[0].value == "Editar");
    CHECK(rows[1].kind == SettingsRow::Kind::Slider && rows[1].steps == 10 && rows[1].step == 5 && rows[1].value == "100%");
    CHECK(rows[2].kind == SettingsRow::Kind::Slider && rows[2].steps == 7 && rows[2].step == 7 && rows[2].value == "100%");
    tab.SetStep(1, 8);
    tab.SetStep(2, 0);
    CHECK(cfg.Buttons().scalePct == 130 && cfg.Buttons().opacityPct == 30);
    tab.SetStep(1, 99);  // além do fim: o máximo
    rows = tab.Rows();
    CHECK(rows[1].step == 10 && rows[1].value == "150%" && rows[2].step == 0 && rows[2].value == "30%");
    CHECK(!tab.Click(1));  // barra: o toque não faz nada
    CHECK(panel.Tick(5));  // a configuração mudou por fora: redesenha
    auto snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    const rt::json::Value* layout = snap ? snap->Get("layout") : nullptr;
    CHECK(layout && !*layout->Get("editing")->AsBool() && *layout->Get("scale_pct")->AsNumber() == 150 &&
          *layout->Get("opacity_pct")->AsNumber() == 30 && *layout->Get("placement_max")->AsNumber() == 10000);
    CHECK(snap && *snap->Get("fab")->Get("id")->AsString() == overlay::kMenuButtonId);
    CHECK(!panel.LaunchersVisible());  // fora de um mundo

    // "Editar": pede o modo de edição (da thread do jogo) e fecha as Configurações.
    CHECK(tab.Click(0) && !panel.IsEditingLayout());
    CHECK(panel.Tick(10) && panel.IsEditingLayout() && panel.LaunchersVisible());  // todos os botões, mesmo no menu
    snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    CHECK(snap && *snap->Get("layout")->Get("editing")->AsBool() && *snap->Get("launchers_visible")->AsBool());
    CHECK(snap && snap->Get("launchers")->AsArray()->size() == 1);  // o Boss Checklist
    // No modo de edição os botões só se movem.
    CHECK(!panel.HandleAction("toggle_open", "") && !panel.HandleAction("open", "checklist"));
    CHECK(panel.OpenSurface() == Surface::None);
    CHECK(panel.HandleAction("layout_move", "menu 1500 2500") &&
          (cfg.Buttons().positions.at("menu") == ButtonPlacement{1500, 2500}));
    CHECK(panel.HandleAction("layout_move", "checklist 9000 100"));
    CHECK(!panel.HandleAction("layout_move", "menu 12") && !panel.HandleAction("layout_move", "menu x 3") &&
          !panel.HandleAction("layout_move", "menu 1 2 3") && !panel.HandleAction("layout_move", ""));
    panel.Tick(20);
    snap = rt::json::Parse(panel.SnapshotJson(), nullptr);
    const rt::json::Value* positions = snap ? snap->Get("layout")->Get("positions") : nullptr;
    CHECK(positions && positions->Get("menu") && *(*positions->Get("menu")->AsArray())[1].AsNumber() == 2500 &&
          positions->Get("checklist"));
    CHECK(panel.HandleAction("layout_reset", "") && cfg.Buttons() == ButtonLayout{});
    CHECK(panel.HandleAction("layout_done", "") && !panel.IsEditingLayout() && !panel.LaunchersVisible());
    CHECK(panel.HandleAction("toggle_open", "") && panel.OpenSurface() == Surface::Log);  // de volta ao normal

    // Abrir o modo de edição fecha o Log; a Oficina fecha o modo de edição.
    CHECK(panel.HandleAction("layout_edit", "") && panel.IsEditingLayout() && panel.OpenSurface() == Surface::None);
    CHECK(panel.HandleAction("workshop_open", "") && panel.IsWorkshopOpen() && !panel.IsEditingLayout());
    panel.HandleAction("workshop_close", "");

    // "Padrão" da aba e sem overlay.
    tab.SetStep(1, 0);
    tab.Reset();
    CHECK(cfg.Buttons() == ButtonLayout{});
    overlay::ModSettingsTab noPanel(cfg, nullptr);
    CHECK(!noPanel.Click(0));
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
    TestWorkshop(mods, tmp);
    TestNativeWorkshop(tmp);
    TestButtonLayout(mods, tmp);
    fs::remove_all(tmp);
    std::printf("\n%d checks, %d falha%s: %s\n", g_checks, g_failures, g_failures == 1 ? "" : "s",
                g_failures ? "FALHOU" : "TODOS OS TESTES PASSARAM");
    return g_failures ? 1 : 0;
}
