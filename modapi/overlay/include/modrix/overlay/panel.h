// Lógica do painel de mods (sem desenho).
//
// O controlador guarda o estado (aberto/fechado, aba), executa as ações
// (abrir, trocar de aba, ligar/desligar mod), busca os dados (ModLoader e o
// canal do Boss Checklist) e produz um VIEW-MODEL em JSON. Quem desenha (Views
// Android no jogo, ou o gerador de prévia no host) só lê esse JSON: não
// decide nada, não formata texto e não conhece o runtime.
//
// Threads: o controlador não é thread-safe; use-o numa thread só (no
// Android, a thread de UI). As fontes de dados (ModLoader::Query) são seguras
// para chamar dessa thread.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "modrix/runtime/mod_loader.h"

namespace modrix::overlay {

class ConfigStore;

// ---------------------------------------------------------------------------
// Fontes de dados
// ---------------------------------------------------------------------------
struct ModInfo {
    std::string id;
    std::string name;
    std::string version;
    rt::ModState state = rt::ModState::Discovered;
    std::string error;
};

class PanelDataSource {
public:
    virtual ~PanelDataSource() = default;
    virtual std::vector<ModInfo> Mods() const = 0;
    virtual std::optional<std::string> Query(const std::string& modId, const std::string& channel,
                                             std::string* error) const = 0;
};

// Fonte real: lê do ModLoader (depois de LoadAll).
class LoaderDataSource final : public PanelDataSource {
public:
    explicit LoaderDataSource(const rt::ModLoader& loader) : loader_(loader) {}
    std::vector<ModInfo> Mods() const override;
    std::optional<std::string> Query(const std::string& modId, const std::string& channel,
                                     std::string* error) const override;

private:
    const rt::ModLoader& loader_;
};

// ---------------------------------------------------------------------------
// View-model
// ---------------------------------------------------------------------------
enum class Tab { Mods, Checklist };

struct ModRow {
    std::string id;
    std::string title;      // nome do manifest
    std::string subtitle;   // "v0.1.0 · Carregado"
    std::string state;      // loaded | error | disabled | skipped | rejected
    std::string detail;     // mensagem de erro, se houver
    bool enabled = true;    // valor do interruptor (configuração)
    std::string pending;    // "Será desativado ao reiniciar o jogo" ou vazio
};

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

// Canal do Boss Checklist que o painel consome.
constexpr const char* kChecklistMod = "modrix.boss_checklist";
constexpr const char* kChecklistChannel = "checklist";

class PanelController {
public:
    // Intervalo de atualização da aba Boss Checklist com o painel aberto.
    static constexpr int64_t kChecklistRefreshMs = 1000;

    PanelController(const PanelDataSource& source, ConfigStore& config);

    // --- Ações ---------------------------------------------------------------
    void ToggleOpen() { SetOpen(!open_); }
    void SetOpen(bool open);
    void SelectTab(Tab tab);
    // Inverte o interruptor do mod e salva. Não carrega nem descarrega nada.
    bool ToggleMod(const std::string& modId, std::string* error = nullptr);
    // Ponto de entrada do renderizador: "toggle_open", "close",
    // "tab" (arg "mods" | "checklist"), "toggle_mod" (arg = id do mod).
    bool HandleAction(const std::string& action, const std::string& arg);

    // Chamado periodicamente pelo renderizador (ex.: a cada 250 ms).
    // Busca dados só com o painel aberto. Devolve true se o view-model mudou.
    bool Tick(int64_t nowMs);

    // --- Estado --------------------------------------------------------------
    bool IsOpen() const { return open_; }
    Tab ActiveTab() const { return tab_; }
    uint64_t Version() const { return version_; }
    const std::vector<ModRow>& ModRows() const { return modRows_; }
    const std::string& ModsNotice() const { return modsNotice_; }
    const ChecklistView& Checklist() const { return checklist_; }
    // Quantas vezes o canal do checklist foi consultado (para testes).
    int ChecklistQueries() const { return checklistQueries_; }

    // View-model completo para o renderizador (inclui o tema).
    std::string SnapshotJson() const;

private:
    void RebuildMods();
    void RefreshChecklist();
    void Bump();

    const PanelDataSource& source_;
    ConfigStore& config_;
    bool open_ = false;
    Tab tab_ = Tab::Mods;
    uint64_t version_ = 1;

    std::vector<ModInfo> mods_;     // lidos uma vez: não mudam com o jogo rodando
    bool modsLoaded_ = false;
    std::vector<ModRow> modRows_;
    std::string modsNotice_;

    ChecklistView checklist_;
    bool checklistDirty_ = true;
    int64_t lastChecklistMs_ = INT64_MIN / 2;
    int checklistQueries_ = 0;
};

// Rótulos em português usados no view-model (expostos para testes).
const char* StateLabel(rt::ModState s);

}  // namespace modrix::overlay
