#include "tlmod/overlay/panel.h"

#include "json.h"
#include "json_writer.h"
#include "tlmod/overlay/config_store.h"

namespace tlmod::overlay {

using rt::ModState;
namespace json = rt::json;

// ---------------------------------------------------------------------------
// Tema: a ÚNICA fonte de cores e medidas. Vai dentro do view-model, então as
// Views Android e o gerador de prévia desenham com os mesmos valores.
// Cores #AARRGGBB; medidas em dp/sp do Android.
// ---------------------------------------------------------------------------
namespace theme {
constexpr const char* kPanelBg = "#EB121821";
constexpr const char* kPanelBorder = "#FF33415A";
constexpr const char* kHeaderBg = "#FF1A2230";
constexpr const char* kText = "#FFF2F4F8";
constexpr const char* kTextDim = "#FF9AA5B8";
constexpr const char* kAccent = "#FFF2B544";
constexpr const char* kTabActiveBg = "#FF2B3A52";
constexpr const char* kRowBg = "#FF1C2432";
constexpr const char* kFabBg = "#E61E2836";
constexpr const char* kDefeated = "#FF5BD17A";
constexpr const char* kPending = "#FFE8C25A";
constexpr const char* kUnknown = "#FF8A93A3";
constexpr const char* kError = "#FFFF6B6B";
constexpr const char* kSwitchOn = "#FF5BD17A";
constexpr const char* kSwitchOff = "#FF4A5568";
constexpr int kPanelWidthDp = 340;
constexpr int kPanelMaxHeightDp = 440;
constexpr int kFabSizeDp = 44;
constexpr int kMarginDp = 12;
constexpr int kCornerDp = 12;
constexpr int kTitleSp = 16;
constexpr int kTextSp = 14;
constexpr int kSmallSp = 12;
}  // namespace theme

const char* StateLabel(ModState s) {
    switch (s) {
        case ModState::Loaded: return "Carregado";
        case ModState::Failed: return "Erro";
        case ModState::Rejected: return "Rejeitado";
        case ModState::Skipped: return "Não carregado";
        case ModState::Disabled: return "Desativado";
        case ModState::Discovered: return "Não carregado";
    }
    return "?";
}

namespace {

const char* StateKey(ModState s) {
    switch (s) {
        case ModState::Loaded: return "loaded";
        case ModState::Failed: return "error";
        case ModState::Rejected: return "rejected";
        case ModState::Disabled: return "disabled";
        case ModState::Skipped:
        case ModState::Discovered: return "skipped";
    }
    return "skipped";
}

const char* StatusLabel(const std::string& status) {
    if (status == "defeated") return "Derrotado";
    if (status == "pending") return "Pendente";
    return "Desconhecido";
}

const char* CategoryLabel(const std::string& category) {
    if (category == "miniboss") return "Miniboss";
    if (category == "event") return "Evento";
    return "Boss";
}

std::string Str(const json::Value* v, const char* key) {
    const json::Value* f = v ? v->Get(key) : nullptr;
    const std::string* s = f ? f->AsString() : nullptr;
    return s ? *s : std::string();
}

int Num(const json::Value* v, const char* key) {
    const json::Value* f = v ? v->Get(key) : nullptr;
    const double* d = f ? f->AsNumber() : nullptr;
    return d ? static_cast<int>(*d) : 0;
}

ChecklistView Message(std::string text) {
    ChecklistView v;
    v.mode = ChecklistView::Mode::Message;
    v.message = std::move(text);
    return v;
}

bool SameView(const ChecklistView& a, const ChecklistView& b) {
    if (a.mode != b.mode || a.message != b.message || a.next != b.next || a.hardmode != b.hardmode ||
        a.summary.size() != b.summary.size() || a.rows.size() != b.rows.size())
        return false;
    for (size_t i = 0; i < a.summary.size(); ++i)
        if (a.summary[i].label != b.summary[i].label || a.summary[i].value != b.summary[i].value) return false;
    for (size_t i = 0; i < a.rows.size(); ++i)
        if (a.rows[i].name != b.rows[i].name || a.rows[i].status != b.rows[i].status ||
            a.rows[i].header != b.rows[i].header)
            return false;
    return true;
}

// JSON do canal checklist (schema 1, ver mods/boss_checklist/README.md) -> view.
ChecklistView BuildChecklist(const std::string& text) {
    std::string err;
    auto doc = json::Parse(text, &err);
    if (!doc || !doc->AsObject()) return Message("Erro ao ler o checklist: " + err);

    const json::Value* inWorld = doc->Get("in_world");
    if (!inWorld || inWorld->IsNull()) return Message("Aguardando o jogo…");
    if (const bool* b = inWorld->AsBool(); b && !*b) return Message("Entre num mundo");

    ChecklistView v;
    v.mode = ChecklistView::Mode::Ready;
    const json::Value* hm = doc->Get("hardmode");
    v.hardmode = hm && hm->AsBool() && *hm->AsBool();

    const json::Value* summary = doc->Get("summary");
    struct Group {
        const char* key;
        const char* label;
    };
    for (Group g : {Group{"bosses", "Bosses"}, Group{"minibosses", "Minibosses"}, Group{"events", "Eventos"}}) {
        const json::Value* c = summary ? summary->Get(g.key) : nullptr;
        if (!c) continue;
        v.summary.push_back({g.label, std::to_string(Num(c, "defeated")) + "/" + std::to_string(Num(c, "total"))});
    }

    const json::Value* next = doc->Get("next");
    if (next && next->AsObject()) {
        v.next = "Próximo: " + Str(next, "name");
    } else {
        int pending = summary ? Num(summary->Get("bosses"), "pending") : 0;
        int unknown = summary ? Num(summary->Get("bosses"), "unknown") : 0;
        v.next = (pending == 0 && unknown == 0) ? "Todos os bosses derrotados!" : "Próximo: —";
    }

    const json::Value* entries = doc->Get("entries");
    bool sawHardmode = false;
    if (entries && entries->AsArray()) {
        v.rows.push_back({true, "Pré-hardmode", "", "", ""});
        for (const json::Value& e : *entries->AsArray()) {
            const json::Value* hmFlag = e.Get("hardmode");
            if (!sawHardmode && hmFlag && hmFlag->AsBool() && *hmFlag->AsBool()) {
                v.rows.push_back({true, "Hardmode", "", "", ""});
                sawHardmode = true;
            }
            std::string status = Str(&e, "status");
            v.rows.push_back({false, Str(&e, "name"), CategoryLabel(Str(&e, "category")), status,
                              StatusLabel(status)});
        }
    }
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// LoaderDataSource
// ---------------------------------------------------------------------------
std::vector<ModInfo> LoaderDataSource::Mods() const {
    std::vector<ModInfo> out;
    for (const rt::ModRecord& m : loader_.Mods()) {
        ModInfo i;
        i.id = m.manifest.id;
        i.name = m.manifest.name.empty() ? m.manifest.id : m.manifest.name;
        i.version = m.state == ModState::Rejected && m.manifest.name.empty() ? "" : m.manifest.version.ToString();
        i.state = m.state;
        i.error = m.error;
        out.push_back(std::move(i));
    }
    return out;
}

std::optional<std::string> LoaderDataSource::Query(const std::string& modId, const std::string& channel,
                                                   std::string* error) const {
    return loader_.Query(modId, channel, error);
}

// ---------------------------------------------------------------------------
// PanelController
// ---------------------------------------------------------------------------
PanelController::PanelController(const PanelDataSource& source, ConfigStore& config)
    : source_(source), config_(config) {
    checklist_ = Message("Aguardando o jogo…");
}

void PanelController::Bump() { ++version_; }

void PanelController::SetOpen(bool open) {
    if (open_ == open) return;
    open_ = open;
    if (open) checklistDirty_ = true;  // ao abrir, a próxima Tick busca na hora
    Bump();
}

void PanelController::SelectTab(Tab tab) {
    if (tab_ == tab) return;
    tab_ = tab;
    if (tab == Tab::Checklist) checklistDirty_ = true;
    Bump();
}

bool PanelController::ToggleMod(const std::string& modId, std::string* error) {
    if (!modsLoaded_) RebuildMods();
    bool known = false;
    for (const ModInfo& m : mods_) known = known || m.id == modId;
    if (!known) {
        if (error) *error = "mod desconhecido: " + modId;
        return false;
    }
    std::string err;
    if (!config_.SetEnabled(modId, !config_.IsEnabled(modId), &err)) {
        modsNotice_ = "Não foi possível salvar: " + err;
        Bump();
        if (error) *error = err;
        return false;
    }
    RebuildMods();
    Bump();
    return true;
}

bool PanelController::HandleAction(const std::string& action, const std::string& arg) {
    if (action == "toggle_open") ToggleOpen();
    else if (action == "close") SetOpen(false);
    else if (action == "tab" && arg == "mods") SelectTab(Tab::Mods);
    else if (action == "tab" && arg == "checklist") SelectTab(Tab::Checklist);
    else if (action == "toggle_mod") return ToggleMod(arg);
    else return false;
    return true;
}

void PanelController::RebuildMods() {
    if (!modsLoaded_) {
        mods_ = source_.Mods();  // estado dos mods não muda com o jogo rodando
        modsLoaded_ = true;
    }
    modRows_.clear();
    bool anyPending = false;
    for (const ModInfo& m : mods_) {
        ModRow r;
        r.id = m.id;
        r.title = m.name;
        r.subtitle = (m.version.empty() ? "" : "v" + m.version + " · ") + StateLabel(m.state);
        r.state = StateKey(m.state);
        if (m.state != ModState::Loaded && m.state != ModState::Disabled) r.detail = m.error;
        r.enabled = config_.IsEnabled(m.id);
        bool wasEnabled = m.state != ModState::Disabled;
        if (r.enabled != wasEnabled) {
            r.pending = r.enabled ? "Será ativado ao reiniciar o jogo" : "Será desativado ao reiniciar o jogo";
            anyPending = true;
        }
        modRows_.push_back(std::move(r));
    }
    if (mods_.empty()) modsNotice_ = "Nenhum mod instalado.";
    else if (anyPending) modsNotice_ = "Reinicie o jogo para aplicar as mudanças.";
    else modsNotice_.clear();
}

void PanelController::RefreshChecklist() {
    ++checklistQueries_;
    std::string err;
    ChecklistView next;
    if (auto text = source_.Query(kChecklistMod, kChecklistChannel, &err)) {
        next = BuildChecklist(*text);
    } else {
        bool installed = false;
        for (const ModInfo& m : mods_) installed = installed || (m.id == kChecklistMod && m.state == ModState::Loaded);
        next = Message(installed ? "Erro ao ler o checklist: " + err
                                 : "O mod Boss Checklist não está carregado.");
    }
    if (!SameView(next, checklist_)) {
        checklist_ = std::move(next);
        Bump();
    }
}

bool PanelController::Tick(int64_t nowMs) {
    uint64_t before = version_;
    if (!modsLoaded_) {  // primeira vez: monta a lista de mods
        RebuildMods();
        Bump();
    }
    if (open_ && tab_ == Tab::Checklist &&
        (checklistDirty_ || nowMs - lastChecklistMs_ >= kChecklistRefreshMs)) {
        checklistDirty_ = false;
        lastChecklistMs_ = nowMs;
        RefreshChecklist();
    }
    return version_ != before;
}

std::string PanelController::SnapshotJson() const {
    JsonWriter w;
    w.BeginObject();
    w.Key("version").Int(static_cast<int64_t>(version_));
    w.Key("open").Bool(open_);
    w.Key("tab").String(tab_ == Tab::Mods ? "mods" : "checklist");
    w.Key("title").String("TLMod");

    w.Key("theme").BeginObject();
    w.Key("panel_bg").String(theme::kPanelBg);
    w.Key("panel_border").String(theme::kPanelBorder);
    w.Key("header_bg").String(theme::kHeaderBg);
    w.Key("text").String(theme::kText);
    w.Key("text_dim").String(theme::kTextDim);
    w.Key("accent").String(theme::kAccent);
    w.Key("tab_active_bg").String(theme::kTabActiveBg);
    w.Key("row_bg").String(theme::kRowBg);
    w.Key("fab_bg").String(theme::kFabBg);
    w.Key("defeated").String(theme::kDefeated);
    w.Key("pending").String(theme::kPending);
    w.Key("unknown").String(theme::kUnknown);
    w.Key("error").String(theme::kError);
    w.Key("switch_on").String(theme::kSwitchOn);
    w.Key("switch_off").String(theme::kSwitchOff);
    w.Key("panel_width_dp").Int(theme::kPanelWidthDp);
    w.Key("panel_max_height_dp").Int(theme::kPanelMaxHeightDp);
    w.Key("fab_size_dp").Int(theme::kFabSizeDp);
    w.Key("margin_dp").Int(theme::kMarginDp);
    w.Key("corner_dp").Int(theme::kCornerDp);
    w.Key("title_sp").Int(theme::kTitleSp);
    w.Key("text_sp").Int(theme::kTextSp);
    w.Key("small_sp").Int(theme::kSmallSp);
    w.EndObject();

    w.Key("fab").BeginObject().Key("label").String("M").EndObject();

    w.Key("tabs").BeginArray();
    w.BeginObject().Key("id").String("mods").Key("label").String("Mods").Key("active").Bool(tab_ == Tab::Mods).EndObject();
    w.BeginObject().Key("id").String("checklist").Key("label").String("Boss Checklist")
        .Key("active").Bool(tab_ == Tab::Checklist).EndObject();
    w.EndArray();

    w.Key("mods").BeginObject();
    w.Key("notice").String(modsNotice_);
    w.Key("rows").BeginArray();
    for (const ModRow& r : modRows_) {
        w.BeginObject();
        w.Key("id").String(r.id);
        w.Key("title").String(r.title);
        w.Key("subtitle").String(r.subtitle);
        w.Key("state").String(r.state);
        w.Key("detail").String(r.detail);
        w.Key("enabled").Bool(r.enabled);
        w.Key("pending").String(r.pending);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();

    w.Key("checklist").BeginObject();
    w.Key("mode").String(checklist_.mode == ChecklistView::Mode::Ready ? "ready" : "message");
    w.Key("message").String(checklist_.message);
    w.Key("hardmode").Bool(checklist_.hardmode);
    w.Key("next").String(checklist_.next);
    w.Key("summary").BeginArray();
    for (const SummaryItem& s : checklist_.summary)
        w.BeginObject().Key("label").String(s.label).Key("value").String(s.value).EndObject();
    w.EndArray();
    w.Key("rows").BeginArray();
    for (const BossRow& r : checklist_.rows) {
        w.BeginObject();
        w.Key("header").Bool(r.header);
        w.Key("name").String(r.name);
        w.Key("category").String(r.category);
        w.Key("status").String(r.status);
        w.Key("status_label").String(r.statusLabel);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();

    w.EndObject();
    return w.str();
}

}  // namespace tlmod::overlay
