// Ferramentas de teste: a aba "Ferramentas" do painel.
//
// O painel mostra o que este mod descreve no canal "panel" (seções, botões,
// campos de busca) e devolve os toques como comandos de texto no mesmo canal.
// Comandos chegam na thread da UI: o que mexe no jogo (hora, cura, matar) vai
// para uma fila atendida no OnFrame, na thread do jogo. Dar item e invocar NPC
// já são seguros em qualquer thread (o runtime enfileira).
//
// Comandos:
//   time dawn|noon|dusk|midnight   freeze        heal     god     kill
//   give <id> <quantidade>         spawn <id> [<id>...]   search <item|npc> <texto>
#include <tlmod/tlmod.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kKillRadius = 2400.0f;  // ~150 blocos: a tela inteira e um pouco mais
constexpr size_t kMaxResults = 30;

// Bosses com botão próprio. Os nomes vêm do catálogo do jogo (no idioma dele);
// o daqui só aparece se o catálogo não tiver o ID.
struct QuickBoss {
    std::vector<TLModId> ids;
    const char* fallback;
};
const QuickBoss kBosses[] = {
    {{50}, "Rei Slime"},          {{4}, "Olho de Cthulhu"},          {{13}, "Devorador de Mundos"},
    {{266}, "Cérebro de Cthulhu"}, {{222}, "Abelha Rainha"},          {{35}, "Esqueletron"},
    {{668}, "Deerclops"},         {{657}, "Rainha Slime"},           {{125, 126}, "Os Gêmeos"},
    {{134}, "O Destruidor"},      {{127}, "Esqueletron Primordial"}, {{262}, "Plantera"},
    {{245}, "Golem"},             {{370}, "Duque Peixe-Dragão"},     {{636}, "Imperatriz da Luz"},
    {{439}, "Cultista Lunático"},
};

// --- JSON e texto -----------------------------------------------------------------
std::string Json(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out + "\"";
}

// Minúsculas e sem acento (UTF-8 Latin-1: À..ÿ), para "zenite" achar "Zênite".
std::string Fold(const std::string& s) {
    static const char* const kMap =  // 0xC0..0xFF, depois de tirar o acento
        "aaaaaaaceeeeiiiidnooooo*ouuuuyts"
        "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = static_cast<unsigned char>(s[++i]);
            unsigned cp = 0xC0 + (d & 0x3F);
            out += kMap[cp - 0xC0];
        } else {
            out += static_cast<char>(std::tolower(c));
        }
    }
    return out;
}

bool AllDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// Hora do Terraria: o dia começa às 4:30 (54000 ticks), a noite às 19:30 (32400).
std::string Clock(bool day, double time) {
    double hours = (day ? 4.5 : 19.5) + time / 3600.0;
    if (hours >= 24.0) hours -= 24.0;
    int h = static_cast<int>(hours);
    int m = static_cast<int>((hours - h) * 60.0);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", h, m);
    return buf;
}

// --- Estado -------------------------------------------------------------------------
struct Result {
    TLModId id;
    std::string name, internal;
};

struct Search {
    std::string query;
    std::vector<Result> results;
    bool pending = false;  // catálogo ainda não pronto quando buscou
};

enum class Action { TimeJump, Heal, Kill };
struct Pending {
    Action action;
    bool day = true;
    double time = 0;
};

class DevTools final : public tlmod::DataProvider, public tlmod::CommandHandler, public tlmod::FrameListener {
public:
    explicit DevTools(tlmod::Host host) : host_(host) {}

    // --- Thread do jogo ---------------------------------------------------------------
    void OnFrame() override {
        std::vector<Pending> todo;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            todo.swap(pending_);
        }
        for (const Pending& p : todo) {
            switch (p.action) {
                case Action::TimeJump:
                    if (host_.SetTime(p.day, p.time, /*jump=*/true)) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        heldDay_ = p.day, heldTime_ = p.time;  // congelada: segura a hora nova
                    }
                    break;
                case Action::Heal:
                    host_.HealPlayer();
                    break;
                case Action::Kill: {
                    int32_t n = host_.KillHostileNpcs(kKillRadius);
                    host_.Info("kill: " + std::to_string(n) + " inimigos mortos");
                    break;
                }
            }
        }
        bool day = true;
        double time = 0;
        bool have = host_.GetTime(day, time);
        std::lock_guard<std::mutex> lock(mutex_);
        if (have && freeze_) {
            if (!holding_) heldDay_ = day, heldTime_ = time, holding_ = true;
            host_.SetTime(heldDay_, heldTime_, /*jump=*/false);
            day = heldDay_, time = heldTime_;
        } else {
            holding_ = false;
        }
        haveTime_ = have;
        day_ = day, time_ = time;
    }

    static bool OnHurt(void* user, TLModPlayerHurt* ev, bool after) {
        auto* self = static_cast<DevTools*>(user);
        if (after || !self->god_.load()) return true;
        return ev->player != self->host_.LocalPlayer();  // false = cancela o golpe
    }

    // --- Thread da UI -----------------------------------------------------------------
    bool Handle(const std::string& command, std::string& json) override {
        std::istringstream in(command);
        std::string verb;
        in >> verb;
        std::string toast;
        if (verb == "time") {
            std::string at;
            in >> at;
            Pending p{Action::TimeJump};
            if (at == "dawn") p.day = true, p.time = 0, toast = "Amanhecer (4:30)";
            else if (at == "noon") p.day = true, p.time = 27000, toast = "Meio-dia";
            else if (at == "dusk") p.day = false, p.time = 0, toast = "Anoitecer (19:30)";
            else if (at == "midnight") p.day = false, p.time = 16200, toast = "Meia-noite";
            else return false;
            Queue(p);
        } else if (verb == "freeze") {
            std::lock_guard<std::mutex> lock(mutex_);
            freeze_ = !freeze_;
            toast = freeze_ ? "Hora congelada" : "Hora voltou a andar";
        } else if (verb == "heal") {
            Queue({Action::Heal});
            toast = "Vida e mana cheias";
        } else if (verb == "god") {
            god_ = !god_.load();
            toast = god_ ? "Invencível: ligado" : "Invencível: desligado";
        } else if (verb == "kill") {
            Queue({Action::Kill});
            toast = "Inimigos próximos mortos";
        } else if (verb == "give") {
            TLModId id = 0;
            int32_t stack = 0;
            if (!(in >> id >> stack) || stack < 1) return false;
            if (!host_.GiveItem(id, stack)) return false;
            toast = std::to_string(stack) + "x " + NameOf(TLMOD_CONTENT_ITEM, id) + " no inventário";
        } else if (verb == "spawn") {
            std::vector<TLModId> ids;
            for (TLModId id; in >> id;) ids.push_back(id);
            if (ids.empty()) return false;
            float dx = 480.0f;  // 30 blocos à direita, 15 acima; o segundo um pouco mais longe
            for (TLModId id : ids) {
                if (!host_.SpawnNpcNearPlayer(id, dx, -240.0f)) return false;
                dx += 160.0f;
            }
            toast = NameOf(TLMOD_CONTENT_NPC, ids.front()) + (ids.size() > 1 ? " e outros" : "") + " a caminho";
        } else if (verb == "search") {
            std::string which, query;
            in >> which;
            std::getline(in >> std::ws, query);
            if (which != "item" && which != "npc") return false;
            RunSearch(which == "item" ? TLMOD_CONTENT_ITEM : TLMOD_CONTENT_NPC, query);
        } else {
            return false;
        }
        if (!toast.empty()) json = "{\"toast\":" + Json(toast) + "}";
        return true;
    }

    // O que o painel desenha.
    bool Build(std::string& json) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string s = "{\"title\":\"Ferramentas\",\"sections\":[";

        // Hora
        std::string note = !haveTime_ ? "Entre num mundo para mudar a hora."
                                      : "Agora: " + Clock(day_, time_) + (day_ ? " (dia)" : " (noite)") +
                                            (freeze_ ? ", congelada" : "");
        s += "{\"title\":\"Hora do dia\",\"note\":" + Json(note) + ",\"rows\":[{\"buttons\":[" +
             Button("Amanhecer", "time dawn") + "," + Button("Meio-dia", "time noon") + "," +
             Button("Anoitecer", "time dusk") + "," + Button("Meia-noite", "time midnight") + "]},{\"buttons\":[" +
             Button("Congelar hora", "freeze", freeze_) + "]}]},";

        // Jogador
        s += "{\"title\":\"Jogador\",\"rows\":[{\"buttons\":[" + Button("Curar", "heal") + "," +
             Button("Invencível", "god", god_.load()) + "," + Button("Matar inimigos", "kill") + "]}]},";

        // Bosses rápidos + NPCs de mod + busca
        s += "{\"title\":\"Invocar NPC ou boss\"," + SearchJson("npc", "Nome ou ID do NPC", npcSearch_) +
             ",\"rows\":[";
        bool first = true;
        auto row = [&](const std::string& text, const std::string& detail, const std::string& buttons) {
            s += std::string(first ? "" : ",") + "{\"text\":" + Json(text) + ",\"detail\":" + Json(detail) +
                 ",\"buttons\":[" + buttons + "]}";
            first = false;
        };
        for (const Result& r : npcSearch_.results)
            row(r.name, "ID " + std::to_string(r.id) + " · " + r.internal, Button("Invocar", "spawn " + std::to_string(r.id)));
        if (npcSearch_.results.empty()) {
            for (const Result& r : ModContent(TLMOD_CONTENT_NPC))
                row(r.name, "mod · ID " + std::to_string(r.id), Button("Invocar", "spawn " + std::to_string(r.id)));
            std::string buttons;
            for (const QuickBoss& b : kBosses) {
                std::string cmd = "spawn";
                for (TLModId id : b.ids) cmd += " " + std::to_string(id);
                std::string name = b.ids.size() > 1 ? b.fallback : NameOr(TLMOD_CONTENT_NPC, b.ids[0], b.fallback);
                buttons += (buttons.empty() ? "" : ",") + Button(name, cmd);
            }
            row("Bosses", "invocados perto de você", buttons);
        }
        s += "]},";

        // Itens: busca; sem busca, os itens de mod
        s += "{\"title\":\"Dar item\"," + SearchJson("item", "Nome ou ID do item", itemSearch_) + ",\"rows\":[";
        first = true;
        const std::vector<Result> items =
            itemSearch_.results.empty() ? ModContent(TLMOD_CONTENT_ITEM) : itemSearch_.results;
        for (const Result& r : items) {
            std::string id = std::to_string(r.id);
            row(r.name, (r.id >= 4096 ? "mod · ID " : "ID ") + id + " · " + r.internal,
                Button("+1", "give " + id + " 1") + "," + Button("+10", "give " + id + " 10") + "," +
                    Button("+100", "give " + id + " 100"));
        }
        s += "]}]}";
        json = std::move(s);
        return true;
    }

private:
    void Queue(Pending p) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(p);
    }

    static std::string Button(const std::string& label, const std::string& command, bool active = false) {
        return "{\"label\":" + Json(label) + ",\"command\":" + Json(command) +
               (active ? ",\"active\":true" : "") + "}";
    }

    std::string SearchJson(const char* id, const char* placeholder, const Search& search) const {
        std::string note;
        if (search.pending) note = "Os nomes ainda estão carregando; busque de novo em alguns segundos.";
        else if (!search.query.empty() && search.results.empty()) note = "Nada encontrado para \"" + search.query + "\".";
        else if (search.results.size() >= kMaxResults) note = "Mostrando os " + std::to_string(kMaxResults) + " primeiros.";
        return "\"search\":{\"id\":" + Json(id) + ",\"placeholder\":" + Json(placeholder) +
               ",\"query\":" + Json(search.query) + "},\"note\":" + Json(note);
    }

    std::string NameOf(TLModContentKind kind, TLModId id) const {
        return NameOr(kind, id, (kind == TLMOD_CONTENT_ITEM ? "item " : "NPC ") + std::to_string(id));
    }
    std::string NameOr(TLModContentKind kind, TLModId id, const std::string& fallback) const {
        std::string name;
        return host_.ContentName(kind, id, false, name) >= 0 && !name.empty() ? name : fallback;
    }

    std::vector<Result> ModContent(TLModContentKind kind) const {
        std::vector<Result> out;
        for (TLModId id : host_.ContentIds(kind)) {
            std::string internal;
            if (host_.ContentName(kind, id, true, internal) < 0 || internal.find(':') == std::string::npos) continue;
            out.push_back({id, NameOf(kind, id), internal});
        }
        return out;
    }

    void RunSearch(TLModContentKind kind, const std::string& query) {
        Search search;
        search.query = query;
        const std::string q = Fold(query);
        int32_t status = 0;
        std::vector<TLModId> ids = host_.ContentIds(kind, &status);
        if (status == -2) {
            search.pending = true;
        } else if (!q.empty()) {
            // 0 = ID ou nome exato, 1 = começa com, 2 = contém
            std::vector<std::pair<int, Result>> found;
            const bool numeric = AllDigits(q);
            for (TLModId id : ids) {
                std::string name, internal;
                host_.ContentName(kind, id, false, name);
                host_.ContentName(kind, id, true, internal);
                const std::string fn = Fold(name), fi = Fold(internal);
                int rank = -1;
                if (numeric) rank = std::to_string(id) == q ? 0 : -1;
                else if (fn == q || fi == q) rank = 0;
                else if (fn.rfind(q, 0) == 0 || fi.rfind(q, 0) == 0) rank = 1;
                else if (fn.find(q) != std::string::npos || fi.find(q) != std::string::npos) rank = 2;
                if (rank >= 0) found.push_back({rank, {id, name.empty() ? internal : name, internal}});
            }
            std::stable_sort(found.begin(), found.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            for (size_t i = 0; i < found.size() && i < kMaxResults; ++i) search.results.push_back(found[i].second);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        (kind == TLMOD_CONTENT_ITEM ? itemSearch_ : npcSearch_) = std::move(search);
    }

    tlmod::Host host_;
    std::mutex mutex_;  // tudo abaixo, menos god_
    std::vector<Pending> pending_;
    bool freeze_ = false, holding_ = false, heldDay_ = true;
    double heldTime_ = 0;
    bool haveTime_ = false, day_ = true;
    double time_ = 0;
    Search itemSearch_, npcSearch_;
    std::atomic<bool> god_{false};
};

DevTools* g_tools = nullptr;  // vive enquanto o jogo roda

}  // namespace

static bool OnLoad(tlmod::Host& host) {
    g_tools = new DevTools(host);
    if (!host.OnFrame(g_tools) || !host.Commands("panel", g_tools) || !host.Provide("panel", g_tools)) {
        host.Error("este runtime não tem a API de ferramentas (subscribe_frame/register_command)");
        return false;
    }
    host.OnPlayerHurt(&DevTools::OnHurt, g_tools);
    host.Info("Ferramentas prontas: aba Ferramentas no painel");
    return true;
}

TLMOD_MOD(OnLoad)
