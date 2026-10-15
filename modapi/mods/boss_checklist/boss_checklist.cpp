// Boss Checklist: lê o progresso do mundo atual e expõe a lista para a
// interface pelo canal "modrix.boss_checklist/checklist" (JSON).
//
// De onde vem cada estado (camadas abaixo do mod):
//   host.Progress(flag) -> runtime -> GameAdapter::Progress
//     -> fotografia tirada a cada frame em Main.DoUpdate (RVA 0xCADDB8)
//     -> static bool Terraria.NPC.downed* / Terraria.Main.hardMode (dump.cs)
//   host.InWorld() -> LocalUserGameState.Instance.gameMenu (ThreadStatic)
//
// O provedor roda na thread de quem pede (a UI), então só usa chamadas
// seguras em qualquer thread (InWorld/Progress). Nada aqui desenha UI.
#include <modrix/modrix.hpp>

#include <cstdio>
#include <optional>
#include <string>

namespace {

enum class Category { Boss, MiniBoss, Event };

struct BossInfo {
    const char* key;   // estável: a UI localiza o nome por ela
    const char* name;  // nome canônico (inglês, como no jogo)
    float order;       // posição na progressão (1 = primeiro)
    Category category;
    bool hardmode;     // só existe depois do Wall of Flesh
    ModrixProgress flag;
};

// Ordem de progressão usual (pré-hardmode -> hardmode -> lua).
constexpr BossInfo kBosses[] = {
    {"king_slime", "King Slime", 1.0f, Category::Boss, false, MODRIX_DOWNED_KING_SLIME},
    {"eye_of_cthulhu", "Eye of Cthulhu", 2.0f, Category::Boss, false, MODRIX_DOWNED_EYE_OF_CTHULHU},
    {"goblin_army", "Goblin Army", 2.5f, Category::Event, false, MODRIX_DOWNED_GOBLIN_ARMY},
    // downedBoss2 é um só para os dois: depende de o mundo ser Corrupção ou Carmesim.
    {"evil_boss", "Eater of Worlds / Brain of Cthulhu", 3.0f, Category::Boss, false, MODRIX_DOWNED_EVIL_BOSS},
    {"queen_bee", "Queen Bee", 4.0f, Category::Boss, false, MODRIX_DOWNED_QUEEN_BEE},
    {"skeletron", "Skeletron", 5.0f, Category::Boss, false, MODRIX_DOWNED_SKELETRON},
    {"deerclops", "Deerclops", 6.0f, Category::Boss, false, MODRIX_DOWNED_DEERCLOPS},
    // Não existe "downedWallOfFlesh": matá-lo é o que liga Main.hardMode.
    {"wall_of_flesh", "Wall of Flesh", 7.0f, Category::Boss, false, MODRIX_PROGRESS_HARDMODE},
    {"pirate_invasion", "Pirate Invasion", 7.5f, Category::Event, true, MODRIX_DOWNED_PIRATES},
    {"frost_legion", "Frost Legion", 7.6f, Category::Event, true, MODRIX_DOWNED_FROST_LEGION},
    {"queen_slime", "Queen Slime", 8.0f, Category::Boss, true, MODRIX_DOWNED_QUEEN_SLIME},
    {"the_twins", "The Twins", 9.0f, Category::Boss, true, MODRIX_DOWNED_TWINS},
    {"the_destroyer", "The Destroyer", 10.0f, Category::Boss, true, MODRIX_DOWNED_DESTROYER},
    {"skeletron_prime", "Skeletron Prime", 11.0f, Category::Boss, true, MODRIX_DOWNED_SKELETRON_PRIME},
    {"plantera", "Plantera", 12.0f, Category::Boss, true, MODRIX_DOWNED_PLANTERA},
    {"mourning_wood", "Mourning Wood", 12.1f, Category::MiniBoss, true, MODRIX_DOWNED_MOURNING_WOOD},
    {"pumpking", "Pumpking", 12.2f, Category::MiniBoss, true, MODRIX_DOWNED_PUMPKING},
    {"everscream", "Everscream", 12.3f, Category::MiniBoss, true, MODRIX_DOWNED_EVERSCREAM},
    {"santa_nk1", "Santa-NK1", 12.4f, Category::MiniBoss, true, MODRIX_DOWNED_SANTA_NK1},
    {"ice_queen", "Ice Queen", 12.5f, Category::MiniBoss, true, MODRIX_DOWNED_ICE_QUEEN},
    {"golem", "Golem", 13.0f, Category::Boss, true, MODRIX_DOWNED_GOLEM},
    {"martian_madness", "Martian Madness", 13.5f, Category::Event, true, MODRIX_DOWNED_MARTIANS},
    {"duke_fishron", "Duke Fishron", 14.0f, Category::Boss, true, MODRIX_DOWNED_DUKE_FISHRON},
    {"empress_of_light", "Empress of Light", 15.0f, Category::Boss, true, MODRIX_DOWNED_EMPRESS_OF_LIGHT},
    {"lunatic_cultist", "Lunatic Cultist", 16.0f, Category::Boss, true, MODRIX_DOWNED_LUNATIC_CULTIST},
    {"solar_pillar", "Solar Pillar", 16.1f, Category::MiniBoss, true, MODRIX_DOWNED_SOLAR_PILLAR},
    {"vortex_pillar", "Vortex Pillar", 16.2f, Category::MiniBoss, true, MODRIX_DOWNED_VORTEX_PILLAR},
    {"nebula_pillar", "Nebula Pillar", 16.3f, Category::MiniBoss, true, MODRIX_DOWNED_NEBULA_PILLAR},
    {"stardust_pillar", "Stardust Pillar", 16.4f, Category::MiniBoss, true, MODRIX_DOWNED_STARDUST_PILLAR},
    {"moon_lord", "Moon Lord", 17.0f, Category::Boss, true, MODRIX_DOWNED_MOON_LORD},
};

const char* CategoryName(Category c) {
    switch (c) {
        case Category::Boss: return "boss";
        case Category::MiniBoss: return "miniboss";
        case Category::Event: return "event";
    }
    return "boss";
}

enum class Status { Defeated, Pending, Unknown };
const char* StatusName(Status s) {
    return s == Status::Defeated ? "defeated" : s == Status::Pending ? "pending" : "unknown";
}

// Os textos da tabela são ASCII sem aspas; ainda assim, escapa por garantia.
void AppendString(std::string& out, const char* s) {
    out += '"';
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') out += '\\';
        out += *s;
    }
    out += '"';
}

void AppendNumber(std::string& out, float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    out += buf;
}

struct Counts {
    int defeated = 0, pending = 0, unknown = 0, total = 0;
    void Add(Status s) {
        ++total;
        (s == Status::Defeated ? defeated : s == Status::Pending ? pending : unknown)++;
    }
    void Append(std::string& out) const {
        out += "{\"defeated\":" + std::to_string(defeated) + ",\"pending\":" + std::to_string(pending) +
               ",\"unknown\":" + std::to_string(unknown) + ",\"total\":" + std::to_string(total) + "}";
    }
};

class ChecklistProvider final : public modrix::DataProvider {
public:
    void Bind(const modrix::Host& host) { host_.emplace(host); }

    // Documento (schema 1):
    // { "schema":1, "in_world":bool|null, "hardmode":bool|null,
    //   "summary":{"bosses":Counts,"minibosses":Counts,"events":Counts},
    //   "next":{"key","name","order"}|null,     <- próximo BOSS pendente na ordem
    //   "entries":[{"key","name","category","order","hardmode","status"}] }
    // status: "defeated" | "pending" | "unknown" (fora de um mundo, ou flag
    // ainda não lida/mapeada). Fora de um mundo, tudo é "unknown": as flags
    // estáticas guardariam o último mundo jogado.
    bool Build(std::string& json) override {
        if (!host_) return false;
        int32_t inWorld = host_->InWorld();
        int32_t hardmode = host_->Progress(MODRIX_PROGRESS_HARDMODE);

        Counts bosses, minibosses, events;
        const BossInfo* next = nullptr;
        std::string entries;
        for (const BossInfo& b : kBosses) {
            int32_t v = host_->Progress(b.flag);  // -1 fora de um mundo
            Status st = v == 1 ? Status::Defeated : v == 0 ? Status::Pending : Status::Unknown;
            (b.category == Category::Boss ? bosses : b.category == Category::MiniBoss ? minibosses : events).Add(st);
            if (!next && b.category == Category::Boss && st == Status::Pending) next = &b;

            if (!entries.empty()) entries += ',';
            entries += "{\"key\":";
            AppendString(entries, b.key);
            entries += ",\"name\":";
            AppendString(entries, b.name);
            entries += ",\"category\":";
            AppendString(entries, CategoryName(b.category));
            entries += ",\"order\":";
            AppendNumber(entries, b.order);
            entries += std::string(",\"hardmode\":") + (b.hardmode ? "true" : "false");
            entries += ",\"status\":";
            AppendString(entries, StatusName(st));
            entries += '}';
        }

        auto tri = [](int32_t v) { return v < 0 ? std::string("null") : v ? std::string("true") : std::string("false"); };
        json = "{\"schema\":1,\"in_world\":" + tri(inWorld) + ",\"hardmode\":" + tri(hardmode) + ",\"summary\":{";
        json += "\"bosses\":";
        bosses.Append(json);
        json += ",\"minibosses\":";
        minibosses.Append(json);
        json += ",\"events\":";
        events.Append(json);
        json += "},\"next\":";
        if (next) {
            json += "{\"key\":";
            AppendString(json, next->key);
            json += ",\"name\":";
            AppendString(json, next->name);
            json += ",\"order\":";
            AppendNumber(json, next->order);
            json += '}';
        } else {
            json += "null";
        }
        json += ",\"entries\":[" + entries + "]}";
        return true;
    }

private:
    // Host = dois ponteiros (tabela da API e contexto), válidos enquanto o jogo roda.
    std::optional<modrix::Host> host_;
};

ChecklistProvider g_provider;  // estático: nada a liberar

}  // namespace

static bool OnLoad(modrix::Host& host) {
    g_provider.Bind(host);
    if (!host.Provide("checklist", &g_provider)) return false;
    host.Info("Boss Checklist pronto: " + std::to_string(sizeof(kBosses) / sizeof(kBosses[0])) +
              " entradas no canal checklist");
    return true;
}

MODRIX_MOD(OnLoad)
