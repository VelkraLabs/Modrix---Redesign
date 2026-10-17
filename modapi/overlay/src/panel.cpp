#include "tlmod/overlay/panel.h"

#include <algorithm>
#include <cctype>

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
constexpr int kPanelWidthDp = 400;  // 4 abas (Mods, Boss Checklist, Log, Ferramentas)
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

// "2 itens, 1 NPC": o que o mod declara no "content" do manifest.
std::string ContentSummary(const rt::ModManifest& m) {
    static const char* const kNames[TLMOD_CONTENT_KIND_COUNT][2] = {
        {"item", "itens"},  {"NPC", "NPCs"},     {"projétil", "projéteis"},
        {"bloco", "blocos"}, {"parede", "paredes"}, {"efeito", "efeitos"}};
    std::string out;
    for (int k = 0; k < TLMOD_CONTENT_KIND_COUNT; ++k) {
        size_t n = m.content[static_cast<size_t>(k)].size();
        if (n == 0) continue;
        out += (out.empty() ? "" : ", ") + std::to_string(n) + " " + kNames[k][n == 1 ? 0 : 1];
    }
    return out;
}

// Ícone do mod na Oficina: o primeiro item que ele registrou; sem itens, um
// item do jogo que lembre o conteúdo (Olho de Aparência Suspeita para mods de
// NPC, Livro para o resto, inclusive os que não carregaram).
int32_t IconItem(const rt::ContentRegistry& registry, const std::string& modId) {
    constexpr int32_t kNpcIcon = 43, kDefaultIcon = 149;
    for (const rt::ContentEntry& e : registry.Entries(TLMOD_CONTENT_ITEM))
        if (e.modId == modId) return e.id;
    for (const rt::ContentEntry& e : registry.Entries(TLMOD_CONTENT_NPC))
        if (e.modId == modId) return kNpcIcon;
    return kDefaultIcon;
}

// Oficina de Mods (ver panel.h): seções pelo estado desta sessão, em ordem de nome.
void WriteWorkshop(rt::JsonWriter& w, bool open, const std::vector<ModRow>& rows, const std::string& notice,
                   const std::set<std::string>& expanded) {
    struct Section {
        const char* id;
        const char* title;
    };
    static const Section kSections[] = {{"loaded", "Ativos"}, {"disabled", "Desativados"}, {"error", "Com erro"}};
    auto sectionOf = [](const ModRow& r) {
        return r.state == "loaded" ? "loaded" : r.state == "disabled" ? "disabled" : "error";
    };
    std::vector<const ModRow*> sorted;
    for (const ModRow& r : rows) sorted.push_back(&r);
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const ModRow* a, const ModRow* b) { return lower(a->name) < lower(b->name); });
    size_t active = 0;
    for (const ModRow* r : sorted) active += r->state == "loaded";

    w.Key("workshop").BeginObject();
    w.Key("open").Bool(open);
    w.Key("title").String("Oficina de Mods");
    const std::string installed = std::to_string(rows.size()) + (rows.size() == 1 ? " mod instalado" : " mods instalados");
    w.Key("summary").String(rows.empty() ? "Nenhum mod instalado"
                                         : installed + " · " + std::to_string(active) + (active == 1 ? " ativo" : " ativos"));
    w.Key("notice").String(rows.empty() ? "" : notice);
    w.Key("empty").String(rows.empty() ? "Coloque os mods na pasta tlmod/mods do Terraria e abra o jogo de novo." : "");
    w.Key("footer").String("Ligar ou desligar um mod vale na próxima vez que o jogo abrir.");
    w.Key("close_label").String("Fechar");
    w.Key("sections").BeginArray();
    for (const Section& s : kSections) {
        size_t n = 0;
        for (const ModRow* r : sorted) n += std::string(sectionOf(*r)) == s.id;
        if (n == 0) continue;
        w.BeginObject();
        w.Key("id").String(s.id);
        w.Key("title").String(std::string(s.title) + " (" + std::to_string(n) + ")");
        w.Key("rows").BeginArray();
        for (const ModRow* r : sorted) {
            if (std::string(sectionOf(*r)) != s.id) continue;
            const bool open = expanded.count(r->id) != 0;
            w.BeginObject();
            w.Key("id").String(r->id);
            w.Key("name").String(r->name);
            w.Key("version").String(r->version.empty() ? "" : "v" + r->version);
            w.Key("authors").String(r->authors.empty() ? "" : "por " + r->authors);
            w.Key("description").String(r->description);
            w.Key("content").String(r->content.empty() ? "" : "Adiciona: " + r->content);
            w.Key("state").String(r->state);
            w.Key("state_label").String(r->stateLabel);
            w.Key("enabled").Bool(r->enabled);
            w.Key("pending").String(r->pending);
            w.Key("error").String(open ? r->detail : "");
            w.Key("error_toggle").String(r->detail.empty() ? "" : open ? "Esconder o erro" : "Ver o erro");
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
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
// Oficina de Mods nativa
// ---------------------------------------------------------------------------
std::vector<WorkshopEntry> BuildWorkshopEntries(const std::vector<ModInfo>& mods, const ConfigStore& config) {
    auto group = [](ModState s) { return s == ModState::Loaded ? 0 : s == ModState::Disabled ? 1 : 2; };
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::vector<const ModInfo*> sorted;
    for (const ModInfo& m : mods) sorted.push_back(&m);
    std::stable_sort(sorted.begin(), sorted.end(), [&](const ModInfo* a, const ModInfo* b) {
        if (group(a->state) != group(b->state)) return group(a->state) < group(b->state);
        return lower(a->name) < lower(b->name);
    });
    std::vector<WorkshopEntry> out;
    for (const ModInfo* m : sorted) {
        WorkshopEntry e;
        e.id = m->id;
        e.name = m->name;
        e.version = m->version.empty() ? "" : "v" + m->version;
        const bool enabled = config.IsEnabled(m->id);
        const bool wasEnabled = m->state != ModState::Disabled;
        std::string pending;
        if (enabled != wasEnabled)
            pending = enabled ? "Será ativado ao reiniciar o jogo" : "Será desativado ao reiniciar o jogo";
        switch (group(m->state)) {
            case 0:
                e.status = enabled ? "Ativo" : "Será desativado";
                e.kind = enabled ? 0 : 3;
                break;
            case 1:
                e.status = enabled ? "Será ativado" : "Desativado";
                e.kind = enabled ? 3 : 1;
                break;
            default:
                e.status = StateLabel(m->state);
                e.kind = 2;
                break;
        }
        // O resumo vai num quadro da linha: sem conteúdo novo, os autores.
        e.summary = !m->content.empty() ? m->content : m->authors.empty() ? "" : "por " + m->authors;
        e.toggleLabel = enabled ? "Desativar" : "Ativar";
        e.iconItem = m->iconItem;
        std::string d = m->name + (e.version.empty() ? "" : "  " + e.version);
        if (!m->authors.empty()) d += "\npor " + m->authors;
        if (!m->description.empty()) d += "\n\n" + m->description;
        if (!m->content.empty()) d += "\n\nAdiciona: " + m->content;
        d += "\n\nEstado: " + std::string(StateLabel(m->state));
        if (!pending.empty()) d += " (" + pending + ")";
        if (group(m->state) == 2 && !m->error.empty()) d += "\n\nErro:\n" + m->error;
        d += "\n\nLigar ou desligar vale na próxima vez que o jogo abrir.";
        e.details = std::move(d);
        out.push_back(std::move(e));
    }
    return out;
}

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
        for (const std::string& a : m.manifest.authors) i.authors += (i.authors.empty() ? "" : ", ") + a;
        i.description = m.manifest.description;
        i.content = ContentSummary(m.manifest);
        i.iconItem = IconItem(loader_.Registry(), m.manifest.id);
        out.push_back(std::move(i));
    }
    return out;
}

std::vector<std::string> LoaderDataSource::PanelMods() const {
    const std::string suffix = std::string("/") + kModPanelChannel;
    std::vector<std::string> commands = loader_.CommandChannels(), out;
    for (const std::string& key : loader_.Providers()) {
        if (key.size() <= suffix.size() || key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        if (std::find(commands.begin(), commands.end(), key) == commands.end()) continue;
        out.push_back(key.substr(0, key.size() - suffix.size()));
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
    if (open) checklistDirty_ = logDirty_ = true;  // ao abrir, a próxima Tick busca na hora
    Bump();
}

void PanelController::SelectTab(Tab tab) {
    if (tab_ == tab && tab != Tab::ModTab) return;
    if (tab == Tab::ModTab) return;  // abas de mod: SelectModTab
    activeMod_.clear();
    tab_ = tab;
    if (tab == Tab::Checklist) checklistDirty_ = true;
    if (tab == Tab::Log) logDirty_ = true;
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
    else if (action == "tab" && arg == "log") SelectTab(Tab::Log);
    else if (action == "tab" && arg.rfind("mod:", 0) == 0) return SelectModTab(arg.substr(4));
    else if (action == "mod_command") return ModCommand(arg);
    else if (action == "toggle_mod") return ToggleMod(arg);
    else if (action == "workshop_open") SetWorkshopOpen(true);
    else if (action == "workshop_close") SetWorkshopOpen(false);
    else if (action == "workshop_expand") {
        if (!expanded_.erase(arg)) expanded_.insert(arg);
        Bump();
    } else return false;
    return true;
}

void PanelController::SetWorkshopOpen(bool open) {
    if (open && !modsLoaded_) RebuildMods();
    if (workshopOpen_ == open && (!open || !open_)) return;
    workshopOpen_ = open;
    if (open) open_ = false;  // a Oficina cobre a tela; o painel volta pelo botão flutuante
    Bump();
}

void PanelController::RebuildMods() {
    if (!modsLoaded_) {
        mods_ = source_.Mods();  // estado dos mods não muda com o jogo rodando
        modsLoaded_ = true;
        for (const std::string& id : source_.PanelMods()) {
            ModTab tab;
            tab.modId = id;
            tab.title = id;
            for (const ModInfo& m : mods_)
                if (m.id == id) tab.title = m.name;
            modTabs_.push_back(std::move(tab));
            RefreshModTab(modTabs_.back());  // título do próprio mod já no primeiro desenho
        }
    }
    configRevision_ = config_.Revision();
    modRows_.clear();
    bool anyPending = false;
    for (const ModInfo& m : mods_) {
        ModRow r;
        r.id = m.id;
        r.title = m.name;
        r.subtitle = (m.version.empty() ? "" : "v" + m.version + " · ") + StateLabel(m.state);
        r.state = StateKey(m.state);
        if (m.state != ModState::Loaded && m.state != ModState::Disabled) r.detail = m.error;
        r.name = m.name;
        r.version = m.version;
        r.stateLabel = StateLabel(m.state);
        r.authors = m.authors;
        r.description = m.description;
        r.content = m.content;
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

ModTab* PanelController::FindModTab(const std::string& modId) {
    for (ModTab& t : modTabs_)
        if (t.modId == modId) return &t;
    return nullptr;
}

bool PanelController::SelectModTab(const std::string& modId) {
    if (!modsLoaded_) RebuildMods();
    ModTab* tab = FindModTab(modId);
    if (!tab) return false;
    if (tab_ == Tab::ModTab && activeMod_ == modId) return true;
    tab_ = Tab::ModTab;
    activeMod_ = modId;
    modDirty_ = true;
    Bump();
    return true;
}

bool PanelController::ModCommand(const std::string& command) {
    ModTab* tab = tab_ == Tab::ModTab ? FindModTab(activeMod_) : nullptr;
    if (!tab) return false;
    std::string err;
    auto reply = source_.Command(tab->modId, kModPanelChannel, command, &err);
    if (reply) {
        auto doc = json::Parse(*reply, nullptr);
        tab->notice = Str(doc ? &*doc : nullptr, "toast");
    } else {
        tab->notice = "Não deu certo: " + err;
    }
    noticeFresh_ = !tab->notice.empty();
    modDirty_ = true;  // o estado do mod pode ter mudado: busca já no próximo Tick
    Bump();
    return reply.has_value();
}

void PanelController::RefreshModTab(ModTab& tab) {
    std::string err;
    auto text = source_.Query(tab.modId, kModPanelChannel, &err);
    std::string next = text ? *text : "{}";
    if (!text && tab.notice.empty()) {
        tab.notice = "Erro ao ler a aba: " + err;
        noticeFresh_ = true;
        Bump();
    }
    if (next == tab.content) return;
    tab.content = std::move(next);
    auto doc = json::Parse(tab.content, nullptr);
    std::string title = Str(doc ? &*doc : nullptr, "title");
    if (!title.empty()) tab.title = title;
    Bump();
}

void PanelController::RefreshLog() {
    static const char* const kLevel[] = {"D", "I", "W", "E"};
    LogView next;
    next.file = source_.LogFile();
    for (const rt::LogEntry& e : source_.LogTail(kLogTailLines)) {
        int idx = (e.level >= TLMOD_LOG_DEBUG && e.level <= TLMOD_LOG_ERROR) ? e.level : TLMOD_LOG_INFO;
        next.lines.push_back({e.Format(), kLevel[idx]});
    }
    bool same = next.file == log_.file && next.lines.size() == log_.lines.size();
    for (size_t i = 0; same && i < next.lines.size(); ++i) same = next.lines[i].text == log_.lines[i].text;
    if (!same) {
        log_ = std::move(next);
        Bump();
    }
}

bool PanelController::Tick(int64_t nowMs) {
    uint64_t before = version_;
    if (!modsLoaded_ || config_.Revision() != configRevision_) {  // primeira vez, ou mudou por fora
        RebuildMods();
        Bump();
    }
    if (workshopRequested_.exchange(false, std::memory_order_acq_rel)) SetWorkshopOpen(true);
    if (open_ && tab_ == Tab::ModTab && (modDirty_ || nowMs - lastModMs_ >= kModTabRefreshMs)) {
        modDirty_ = false;
        lastModMs_ = nowMs;
        if (ModTab* tab = FindModTab(activeMod_)) RefreshModTab(*tab);
    }
    // Aviso de uma aba de mod: some sozinho depois de kModNoticeMs.
    if (noticeFresh_) {
        noticeFresh_ = false;
        noticeMs_ = nowMs;
    } else if (nowMs - noticeMs_ >= kModNoticeMs) {
        for (ModTab& t : modTabs_)
            if (!t.notice.empty()) t.notice.clear(), Bump();
    }
    if (open_ && tab_ == Tab::Checklist &&
        (checklistDirty_ || nowMs - lastChecklistMs_ >= kChecklistRefreshMs)) {
        checklistDirty_ = false;
        lastChecklistMs_ = nowMs;
        RefreshChecklist();
    }
    if (open_ && tab_ == Tab::Log && (logDirty_ || nowMs - lastLogMs_ >= kLogRefreshMs)) {
        logDirty_ = false;
        lastLogMs_ = nowMs;
        RefreshLog();
    }
    return version_ != before;
}

std::string PanelController::SnapshotJson() const {
    rt::JsonWriter w;
    w.BeginObject();
    w.Key("version").Int(static_cast<int64_t>(version_));
    w.Key("open").Bool(open_);
    w.Key("tab").String(tab_ == Tab::Mods        ? "mods"
                        : tab_ == Tab::Checklist ? "checklist"
                        : tab_ == Tab::Log       ? "log"
                                                 : "mod:" + activeMod_);
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

    w.Key("fab").BeginObject().Key("label").String("M").Key("visible").Bool(!workshopOpen_).EndObject();

    w.Key("tabs").BeginArray();
    w.BeginObject().Key("id").String("mods").Key("label").String("Mods").Key("active").Bool(tab_ == Tab::Mods).EndObject();
    w.BeginObject().Key("id").String("checklist").Key("label").String("Boss Checklist")
        .Key("active").Bool(tab_ == Tab::Checklist).EndObject();
    w.BeginObject().Key("id").String("log").Key("label").String("Log").Key("active").Bool(tab_ == Tab::Log).EndObject();
    for (const ModTab& t : modTabs_)
        w.BeginObject().Key("id").String("mod:" + t.modId).Key("label").String(t.title)
            .Key("active").Bool(tab_ == Tab::ModTab && activeMod_ == t.modId).EndObject();
    w.EndArray();

    // Aba de mod aberta: o JSON do mod vai como está (já validado pelo runtime).
    w.Key("modtab");
    const ModTab* open = nullptr;
    for (const ModTab& t : modTabs_)
        if (tab_ == Tab::ModTab && t.modId == activeMod_) open = &t;
    if (open) {
        w.BeginObject();
        w.Key("id").String(open->modId);
        w.Key("notice").String(open->notice);
        w.Key("content").Raw(open->content.empty() ? "{}" : open->content);
        w.EndObject();
    } else {
        w.Null();
    }

    w.Key("mods").BeginObject();
    w.Key("notice").String(modsNotice_);
    w.Key("workshop_button").String("Abrir a Oficina de Mods");
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

    WriteWorkshop(w, workshopOpen_, modRows_, modsNotice_, expanded_);

    w.Key("log").BeginObject();
    w.Key("file").String(log_.file);
    w.Key("empty").String(log_.lines.empty() ? "Nenhuma linha de log ainda." : "");
    w.Key("lines").BeginArray();
    for (const LogLine& l : log_.lines)
        w.BeginObject().Key("text").String(l.text).Key("level").String(l.level).EndObject();
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
