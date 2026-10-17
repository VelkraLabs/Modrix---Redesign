// Lógica do overlay de mods (sem desenho).
//
// Superfícies, uma aberta por vez:
//   - botão "M": painel com o Log do TLMod;
//   - ícone do Boss Checklist: gaveta que desliza da esquerda para a direita;
//   - ícone de cada mod com canal "panel" (ex.: Ferramentas): modal no centro.
// Os ícones só aparecem com um mundo carregado. A Oficina de Mods (painel) cobre
// tudo quando a do jogo não está disponível.
//
// Modo de edição dos botões (aberto pela aba Mods das Configurações do jogo):
// o "M" e todos os ícones aparecem, mesmo fora de um mundo, e o renderizador
// deixa arrastá-los; cada soltura grava a posição na ConfigStore. Tamanho e
// opacidade também vêm da ConfigStore (ButtonLayout).
//
// O controlador guarda o estado, executa as ações (abrir/fechar, comando de mod,
// ligar/desligar mod), busca os dados (ModLoader e o canal do Boss Checklist) e
// produz um VIEW-MODEL em JSON. Quem desenha (Views
// Android no jogo, ou o gerador de prévia no host) só lê esse JSON: não
// decide nada, não formata texto e não conhece o runtime.
//
// Threads: o controlador não é thread-safe; use-o numa thread só (no
// Android, a thread de UI). As fontes de dados (ModLoader::Query) são seguras
// para chamar dessa thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "tlmod/overlay/config_store.h"
#include "tlmod/runtime/log.h"
#include "tlmod/runtime/mod_loader.h"

namespace tlmod::overlay {

// ---------------------------------------------------------------------------
// Fontes de dados
// ---------------------------------------------------------------------------
struct ModInfo {
    std::string id;
    std::string name;
    std::string version;
    rt::ModState state = rt::ModState::Discovered;
    std::string error;
    std::string authors;      // "Ana, Bia"
    std::string description;
    std::string content;      // "2 itens, 1 NPC" (do "content" do manifest)
    int32_t iconItem = 0;     // item cujo ícone representa o mod na Oficina (0 = nenhum)
};

class PanelDataSource {
public:
    virtual ~PanelDataSource() = default;
    virtual std::vector<ModInfo> Mods() const = 0;
    virtual std::optional<std::string> Query(const std::string& modId, const std::string& channel,
                                             std::string* error) const = 0;
    // Últimas linhas do log (da mais antiga para a mais nova) e o arquivo onde ele é gravado.
    virtual std::vector<rt::LogEntry> LogTail(size_t /*max*/) const { return {}; }
    // Há um mundo carregado? Qualquer thread. nullopt = ainda não se sabe.
    virtual std::optional<bool> InWorld() const { return std::nullopt; }
    virtual std::string LogFile() const { return {}; }
    // Mods com aba própria no painel: provedor E canal de comando "panel".
    virtual std::vector<std::string> PanelMods() const { return {}; }
    virtual std::optional<std::string> Command(const std::string& /*modId*/, const std::string& /*channel*/,
                                               const std::string& /*command*/, std::string* error) const {
        if (error) *error = "sem canal de comando";
        return std::nullopt;
    }
};

// Fonte real: lê do ModLoader (depois de LoadAll).
class LoaderDataSource final : public PanelDataSource {
public:
    explicit LoaderDataSource(const rt::ModLoader& loader) : loader_(loader) {}
    std::vector<ModInfo> Mods() const override;
    std::optional<std::string> Query(const std::string& modId, const std::string& channel,
                                     std::string* error) const override;
    std::vector<rt::LogEntry> LogTail(size_t max) const override { return rt::RecentLog(max); }
    std::string LogFile() const override { return rt::LogFilePath().string(); }
    std::optional<bool> InWorld() const override { return loader_.InWorld(); }
    std::vector<std::string> PanelMods() const override;
    std::optional<std::string> Command(const std::string& modId, const std::string& channel,
                                       const std::string& command, std::string* error) const override {
        return loader_.Command(modId, channel, command, error);
    }

private:
    const rt::ModLoader& loader_;
};

// ---------------------------------------------------------------------------
// View-model
// ---------------------------------------------------------------------------
// O que está aberto por cima do jogo (além da Oficina).
enum class Surface { None, Log, Checklist, ModTab };

struct ModRow {
    std::string id;
    std::string title;      // nome do manifest
    std::string subtitle;   // "v0.1.0 · Carregado"
    std::string state;      // loaded | error | disabled | skipped | rejected
    std::string detail;     // mensagem de erro inteira, se houver (o renderizador quebra linha, não corta)
    bool enabled = true;    // valor do interruptor (configuração)
    std::string pending;    // "Será desativado ao reiniciar o jogo" ou vazio
    // Só a Oficina de Mods usa:
    std::string name;        // nome do mod
    std::string version;     // "0.1.0" (vazio se o manifest não foi lido)
    std::string stateLabel;  // "Carregado", "Erro"...
    std::string authors;
    std::string description;
    std::string content;
};

// Oficina de Mods: a tela grande de gerenciar mods, aberta pelo botão "Mods"
// do menu principal do jogo (ou pela aba Mods do painel). Seções pelo estado
// desta sessão: Ativos (carregados), Desativados e Com erro. O interruptor muda
// a configuração, que vale na próxima abertura do jogo (uma .so carregada não
// é descarregada); a linha avisa o que vai mudar.

struct SummaryItem {
    std::string label;  // "Bosses"
    std::string value;  // "9/17"
};

struct BossRow {
    bool header = false;      // linha de seção ("Pré-hardmode", "Hardmode")
    std::string name;
    std::string category;     // "Boss" | "Miniboss" | "Evento"
    std::string status;       // defeated | pending | unknown
    std::string statusLabel;  // "Derrotado" | "Pendente" | "Desconhecido"
};

struct ChecklistView {
    enum class Mode { Message, Ready };
    Mode mode = Mode::Message;
    std::string message;            // quando Mode::Message
    std::vector<SummaryItem> summary;
    std::string next;               // "Próximo: Queen Slime"
    bool hardmode = false;
    std::vector<BossRow> rows;
};

struct LogLine {
    std::string text;   // linha formatada (LogEntry::Format)
    std::string level;  // "D" | "I" | "W" | "E"
};

struct LogView {
    std::string file;             // caminho do tlmod.log, ou vazio se não há arquivo
    std::vector<LogLine> lines;   // da mais antiga para a mais nova
};

// Modal de um mod (ex.: tlmod.devtools). O conteúdo é o JSON do canal "panel"
// do mod, já validado, repassado ao renderizador como está:
//   {"title": "...", "icon": "tools", "sections": [{"title", "note", "search": {"id", "placeholder", "query"},
//     "rows": [{"text", "detail", "buttons": [{"label", "command", "active"}]}]}]}
// "icon" escolhe o desenho do ícone na tela (kLauncherIcons); sem ele, a inicial do título.
// Um toque num botão manda o texto de "command" de volta ao mod; o campo de
// busca manda "search <id> <texto>".
struct ModTab {
    std::string modId;
    std::string title;    // "title" do JSON do mod; o nome do mod se faltar
    std::string icon;     // "icon" do JSON do mod, se for um dos kLauncherIcons; senão vazio
    int32_t iconItem = 0; // "icon_item" do JSON (item do jogo para o botão na tela); 0 = pelo "icon"
    std::string content;  // JSON do canal "panel" (objeto)
    std::string notice;   // "toast" da última resposta, ou erro
};
constexpr const char* kModPanelChannel = "panel";
// Ícones que o renderizador sabe desenhar.
constexpr const char* kLauncherIcons[] = {"checklist", "tools", "star", "gem", "sword", "book"};
// Id do "M" nas posições salvas (ButtonLayout::positions); os ícones usam o id do lançador.
constexpr const char* kMenuButtonId = "menu";
// Itens do jogo usados como ícone dos botões desenhados pelo jogo (ver HudButton).
constexpr int32_t kMenuButtonItem = 149;  // Livro: o Log
// O item de cada kLauncherIcons, na mesma ordem: Olho de Aparência Suspeita, Chave
// Inglesa Multicolorida, Estrela Cadente, Diamante, Espada Larga de Ferro, Tomo de Feitiço.
constexpr int32_t kLauncherIconItems[] = {43, 3611, 75, 182, 4, 531};
constexpr int32_t kDefaultLauncherItem = 149;  // ícone sem desenho conhecido: o Livro

// Um botão de mod na tela, desenhado pelo próprio jogo (il2cpp_bridge/src/hud_buttons.cpp).
struct HudButton {
    std::string id;          // kMenuButtonId, "checklist" ou "mod:<id>"
    int32_t iconItem = 0;
    bool active = false;     // a tela dele está aberta
    bool menu = false;       // também no menu principal (só o "M")
    std::optional<ButtonPlacement> place;  // posição salva; sem ela, a coluna padrão
};
// O que a ponte desenha agora. Vazio = nenhum botão (modo de edição, Oficina aberta).
struct HudSnapshot {
    std::vector<HudButton> buttons;
    int32_t scalePct = ButtonLayout::kScaleDefault;
    int32_t opacityPct = ButtonLayout::kOpacityDefault;
};

// Canal do Boss Checklist que o painel consome.
constexpr const char* kChecklistMod = "tlmod.boss_checklist";
constexpr const char* kChecklistChannel = "checklist";

class PanelController {
public:
    // Intervalo de atualização da aba Boss Checklist com o painel aberto.
    static constexpr int64_t kChecklistRefreshMs = 1000;
    // Aba Log: quantas linhas e de quanto em quanto tempo, com o painel aberto nela.
    static constexpr size_t kLogTailLines = 150;
    static constexpr int64_t kLogRefreshMs = 1000;
    // Abas de mod: atualização com o painel aberto nelas, e quanto tempo o aviso fica.
    static constexpr int64_t kModTabRefreshMs = 500;
    static constexpr int64_t kModNoticeMs = 4000;

    PanelController(const PanelDataSource& source, ConfigStore& config);

    // --- Ações ---------------------------------------------------------------
    // Abre `s` e fecha a que estava aberta; abrir a mesma de novo fecha (toque no
    // mesmo ícone). ModTab precisa de `modId`. false = mod sem aba.
    bool Toggle(Surface s, const std::string& modId = "");
    bool Show(Surface s, const std::string& modId = "");
    void Close();
    // Inverte o interruptor do mod e salva. Não carrega nem descarrega nada.
    bool ToggleMod(const std::string& modId, std::string* error = nullptr);
    // Ponto de entrada do renderizador: "toggle_open" (o "M": o Log), "close",
    // "open" (arg "log" | "checklist" | "mod:<id>": alterna), "toggle_mod" (arg = id do mod),
    // "mod_command" (arg = comando para o mod do modal aberto), "workshop_open",
    // "workshop_close", "workshop_expand" (arg = id do mod: mostra/esconde o erro),
    // "layout_edit", "layout_done", "layout_reset" e "layout_move" (arg = "<id> <x> <y>":
    // o centro do botão em décimos de milésimo da tela, ver ButtonPlacement).
    bool HandleAction(const std::string& action, const std::string& arg);

    // Modo de edição dos botões. Abrir fecha a superfície aberta e a Oficina.
    void SetLayoutEditing(bool editing);
    // Pedido de qualquer thread (a aba Mods das Configurações roda na thread do
    // jogo): o modo de edição abre no próximo Tick.
    void RequestLayoutEdit() { layoutEditRequested_.store(true, std::memory_order_release); }
    bool IsEditingLayout() const { return editingLayout_; }
    // Grava a posição de um botão ("menu", "checklist" ou "mod:<id>").
    bool MoveButton(const std::string& id, int32_t x, int32_t y, std::string* error = nullptr);

    // Botões desenhados pelo jogo. `probe` diz se eles estão de pé (TLMod_HudAvailable);
    // com eles, o renderizador esconde os botões dele fora do modo de edição. Chamar
    // antes de ligar o renderizador.
    void SetNativeHud(std::function<bool()> probe) { nativeProbe_ = std::move(probe); }
    bool NativeHud() const { return nativeHud_; }
    // Qualquer thread (a ponte desenha na thread do jogo): os botões de agora.
    HudSnapshot Hud() const {
        std::lock_guard<std::mutex> lock(hudMutex_);
        return hud_;
    }
    // Qualquer thread: um botão do jogo foi tocado; o painel atende no próximo Tick.
    void HudTap(const std::string& id) {
        std::lock_guard<std::mutex> lock(hudMutex_);
        hudTaps_.push_back(id);
    }

    // Oficina de Mods. Abrir fecha a superfície aberta (a Oficina fica por cima de tudo).
    void SetWorkshopOpen(bool open);
    // Pedido de qualquer thread (o botão do menu roda na thread do jogo): a
    // Oficina abre no próximo Tick, na thread do renderizador.
    void RequestWorkshop() { workshopRequested_.store(true, std::memory_order_release); }
    bool IsWorkshopOpen() const { return workshopOpen_; }
    bool IsExpanded(const std::string& modId) const { return expanded_.count(modId) != 0; }

    // Chamado periodicamente pelo renderizador (ex.: a cada 250 ms).
    // Busca dados só da superfície aberta. Devolve true se o view-model mudou.
    bool Tick(int64_t nowMs);

    // --- Estado --------------------------------------------------------------
    Surface OpenSurface() const { return surface_; }
    // Ícones na tela: com um mundo carregado e sem a Oficina aberta, ou no modo de edição.
    bool LaunchersVisible() const { return (inWorld_ && !workshopOpen_) || editingLayout_; }
    uint64_t Version() const { return version_; }
    const std::vector<ModRow>& ModRows() const { return modRows_; }
    const std::string& ModsNotice() const { return modsNotice_; }
    const ChecklistView& Checklist() const { return checklist_; }
    const LogView& LogTab() const { return log_; }
    const std::vector<ModTab>& ModTabs() const { return modTabs_; }
    const std::string& ActiveModTab() const { return activeMod_; }  // vazio fora do modal de um mod
    bool ModCommand(const std::string& command);
    bool ChecklistInstalled() const;
    // Quantas vezes o canal do checklist foi consultado (para testes).
    int ChecklistQueries() const { return checklistQueries_; }

    // View-model completo para o renderizador (inclui o tema).
    std::string SnapshotJson() const;

private:
    void RebuildMods();
    void RefreshChecklist();
    void RefreshLog();
    void RefreshModTab(ModTab& tab);
    ModTab* FindModTab(const std::string& modId);
    void Bump();

    const PanelDataSource& source_;
    ConfigStore& config_;
    Surface surface_ = Surface::None;
    bool inWorld_ = false;
    uint64_t version_ = 1;

    bool workshopOpen_ = false;
    std::atomic<bool> workshopRequested_{false};
    bool editingLayout_ = false;
    std::atomic<bool> layoutEditRequested_{false};
    std::function<bool()> nativeProbe_;
    bool nativeHud_ = false;
    mutable std::mutex hudMutex_;   // hud_ e hudTaps_ (a ponte lê e toca da thread do jogo)
    HudSnapshot hud_;
    std::vector<std::string> hudTaps_;
    void PublishHud();
    std::string layoutNotice_;  // erro ao salvar uma posição
    std::set<std::string> expanded_;  // mods com o erro aberto na Oficina
    uint64_t configRevision_ = 0;     // a configuração pode mudar por fora (Oficina nativa)

    std::vector<ModInfo> mods_;     // lidos uma vez: não mudam com o jogo rodando
    bool modsLoaded_ = false;
    std::vector<ModRow> modRows_;
    std::string modsNotice_;

    ChecklistView checklist_;
    bool checklistDirty_ = true;
    int64_t lastChecklistMs_ = INT64_MIN / 2;
    int checklistQueries_ = 0;

    LogView log_;
    bool logDirty_ = true;
    int64_t lastLogMs_ = INT64_MIN / 2;

    std::vector<ModTab> modTabs_;
    std::string activeMod_;
    bool modDirty_ = true;
    int64_t lastModMs_ = INT64_MIN / 2;
    bool noticeFresh_ = false;  // aviso novo: o relógio começa no próximo Tick
    int64_t noticeMs_ = 0;
};

// Rótulos em português usados no view-model (expostos para testes).
const char* StateLabel(rt::ModState s);

// Uma linha da Oficina de Mods nativa (a tela do jogo, il2cpp_bridge/src/
// workshop_screen.cpp): textos prontos para a linha e para a página de detalhes.
struct WorkshopEntry {
    std::string id;
    std::string name;
    std::string version;      // "v0.1.0" (vazio se o manifest não foi lido)
    std::string status;       // "Ativo", "Desativado", "Será desativado", "Erro"...
    int kind = 0;             // 0 ativo, 1 desativado, 2 erro, 3 mudança pendente
    std::string summary;      // "1 item" (sem conteúdo: "por <autores>")
    std::string details;      // página de detalhes, várias linhas
    std::string toggleLabel;  // "Desativar" / "Ativar"
    int32_t iconItem = 0;     // ícone no quadro da linha (0 = nenhum)
};
// Em ordem: ativos, desativados e com erro (cada grupo por nome), como as
// seções da Oficina do painel. O interruptor vem da configuração.
std::vector<WorkshopEntry> BuildWorkshopEntries(const std::vector<ModInfo>& mods, const ConfigStore& config);

}  // namespace tlmod::overlay
