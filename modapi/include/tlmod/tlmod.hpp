// Camada C++ opcional, só de header, sobre tlmod_api.h.
//
// Tudo aqui é compilado DENTRO do mod: na fronteira com o runtime só passam
// as structs C de tlmod_api.h, então o mod pode usar outra versão de
// compilador/libc++ sem quebrar a ABI.
//
// Uso:
//   static bool OnLoad(tlmod::Host& host) { ...; return true; }
//   TLMOD_MOD(OnLoad)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tlmod_api.h"

namespace tlmod {

struct ItemDef {
    std::string name;          // chave local, declarada no manifest
    std::string displayName;
    std::string tooltip;
    std::string texture;       // relativo à pasta do mod
    int32_t width = 20;
    int32_t height = 20;
    int32_t maxStack = 1;
    int32_t value = 0;
    int32_t rarity = 0;
    int32_t damage = 0;
    int32_t useTime = 20;
    TLModId shellType = 0;     // item original usado como casca; 0 = padrão (Lente)
};

struct NpcDef {
    std::string name;          // chave local, declarada em content.npcs
    std::string displayName;
    TLModId shellType = 0;    // NPC original usado como casca
    int32_t lifeMax = 0;       // <= 0: o da casca
    int32_t damage = -1;       // < 0: o da casca
    int32_t defense = -1;      // < 0: o da casca
    bool boss = false;
    std::string texture;       // PNG relativo à pasta do mod; vazio = o da casca
    int32_t frameCount = 1;    // quadros empilhados na vertical
    int32_t frameTicks = 8;    // ticks por quadro (animação automática)
};

struct RecipeDef {
    TLModId result = TLMOD_INVALID_ID;
    int32_t resultStack = 1;
    std::vector<TLModRecipeIngredient> ingredients;  // {item, stack}
    std::vector<int32_t> stations;                   // TileID; vazio = à mão
    bool needWater = false, needLava = false, needHoney = false;
};

// Comportamento de um NPC de mod. Uma instância por TIPO (não por NPC): o
// estado de cada NPC vive em TLModNpcState (ai[] / local[]).
class NpcBehavior {
public:
    virtual ~NpcBehavior() = default;
    virtual void OnSpawn(TLModNpcState& /*npc*/) {}
    // true = substitui a AI da casca neste tick.
    virtual bool OnAI(TLModNpcState& /*npc*/) { return false; }
};

// Provedor de dados para a interface: devolve um documento JSON.
class DataProvider {
public:
    virtual ~DataProvider() = default;
    virtual bool Build(std::string& json) = 0;

private:
    friend class Host;
    const TLModHostApi* api_ = nullptr;  // preenchido por Host::Provide
};

// Comandos da interface (ver TLModCommandFn): Handle roda em qualquer thread.
class CommandHandler {
public:
    virtual ~CommandHandler() = default;
    // true = aceito. `json` (opcional) é a resposta, um documento JSON.
    virtual bool Handle(const std::string& command, std::string& json) = 0;
};

// Uma vez por frame na thread do jogo (menu e mundo).
class FrameListener {
public:
    virtual ~FrameListener() = default;
    virtual void OnFrame() = 0;
};

class Host {
public:
    Host(const TLModHostApi* api, TLModContext* ctx) : api_(api), ctx_(ctx) {}

    const char* ModId() const { return api_->mod_id(ctx_); }
    const char* ModVersion() const { return api_->mod_version(ctx_); }
    const char* ModDir() const { return api_->mod_dir(ctx_); }

    void Log(TLModLogLevel level, const std::string& msg) const {
        api_->log(ctx_, level, msg.c_str());
    }
    void Info(const std::string& msg) const { Log(TLMOD_LOG_INFO, msg); }
    void Warn(const std::string& msg) const { Log(TLMOD_LOG_WARN, msg); }
    void Error(const std::string& msg) const { Log(TLMOD_LOG_ERROR, msg); }

    TLModId RegisterItem(const ItemDef& def) const {
        TLModItemDef c{};
        c.struct_size = sizeof(c);
        c.name = def.name.c_str();
        c.display_name = def.displayName.c_str();
        c.tooltip = def.tooltip.empty() ? nullptr : def.tooltip.c_str();
        c.texture = def.texture.empty() ? nullptr : def.texture.c_str();
        c.width = def.width;
        c.height = def.height;
        c.max_stack = def.maxStack;
        c.value = def.value;
        c.rarity = def.rarity;
        c.damage = def.damage;
        c.use_time = def.useTime;
        c.shell_type = def.shellType;
        return api_->register_item(ctx_, &c);
    }

    TLModId Find(TLModContentKind kind, const std::string& key) const {
        return api_->find_content(ctx_, kind, key.c_str());
    }

    // `behavior` precisa viver enquanto o jogo roda (ex.: variável estática).
    TLModId RegisterNpc(const NpcDef& def, NpcBehavior* behavior) const {
        if (!HasNpcApi()) return TLMOD_INVALID_ID;
        TLModNpcDef c{};
        c.struct_size = sizeof(c);
        c.name = def.name.c_str();
        c.display_name = def.displayName.c_str();
        c.shell_type = def.shellType;
        c.life_max = def.lifeMax;
        c.damage = def.damage;
        c.defense = def.defense;
        c.boss = def.boss;
        c.user = behavior;
        c.on_spawn = behavior ? &SpawnTrampoline : nullptr;
        c.on_ai = behavior ? &AiTrampoline : nullptr;
        c.texture = def.texture.empty() ? nullptr : def.texture.c_str();
        c.frame_count = def.frameCount;
        c.frame_ticks = def.frameTicks;
        return api_->register_npc(ctx_, &c);
    }
    int32_t SpawnNpc(TLModId type, float x, float y) const {
        return HasNpcApi() ? api_->spawn_npc(ctx_, type, x, y) : -1;
    }

    // -1/0/1, ver tlmod_api.h. Seguros em qualquer thread.
    int32_t InWorld() const { return HasProgressApi() ? api_->in_world(ctx_) : -1; }
    int32_t Progress(TLModProgress flag) const { return HasProgressApi() ? api_->progress(ctx_, flag) : -1; }

    // `provider` precisa viver enquanto o jogo roda (ex.: objeto estático).
    bool Provide(const std::string& channel, DataProvider* provider) const {
        if (!HasProgressApi() || !provider) return false;
        provider->api_ = api_;
        return api_->register_provider(ctx_, channel.c_str(), &ProviderTrampoline, provider);
    }

    // Entrega no próximo frame com um mundo carregado (ver tlmod_api.h).
    bool GiveItem(TLModId type, int32_t stack = 1) const {
        return HasGiveApi() && api_->give_item(ctx_, type, stack);
    }

    // Cria o NPC perto do jogador quando ele estiver num mundo (ver tlmod_api.h).
    bool SpawnNpcNearPlayer(TLModId type, float dx, float dy) const {
        return HasSpawnNearApi() && api_->spawn_npc_near_player(ctx_, type, dx, dy);
    }

    // --- Ferramentas (ver tlmod_api.h). Sem suporte no runtime: false/-1. ---
    // `listener`/`handler` precisam viver enquanto o jogo roda.
    bool OnFrame(FrameListener* listener) const {
        return HasToolsApi() && listener && api_->subscribe_frame(ctx_, &FrameTrampoline, listener);
    }
    bool Commands(const std::string& channel, CommandHandler* handler) const {
        if (!HasToolsApi() || !handler) return false;
        handler_api_ = api_;
        return api_->register_command(ctx_, channel.c_str(), &CommandTrampoline, handler);
    }
    // -1 = sem nome, -2 = catálogo ainda não pronto; senão o nome em `out`.
    int32_t ContentName(TLModContentKind kind, TLModId id, bool internalName, std::string& out) const {
        if (!HasToolsApi()) return -1;
        char buf[256];
        int32_t n = api_->content_name(ctx_, kind, id, internalName, buf, sizeof(buf));
        if (n >= 0) out.assign(buf, static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1);
        return n;
    }
    // IDs com nome; -2 em `status` se o catálogo ainda não está pronto.
    std::vector<TLModId> ContentIds(TLModContentKind kind, int32_t* status = nullptr) const {
        std::vector<TLModId> ids;
        int32_t n = HasToolsApi() ? api_->content_ids(ctx_, kind, nullptr, 0) : -1;
        if (status) *status = n;
        if (n > 0) {
            ids.resize(static_cast<size_t>(n));
            n = api_->content_ids(ctx_, kind, ids.data(), n);
            ids.resize(static_cast<size_t>(n > 0 ? n : 0));
        }
        return ids;
    }
    bool GetTime(bool& day, double& time) const { return HasToolsApi() && api_->get_time(ctx_, &day, &time); }
    bool SetTime(bool day, double time, bool jump) const {
        return HasToolsApi() && api_->set_time(ctx_, day, time, jump);
    }
    bool HealPlayer() const { return HasToolsApi() && api_->heal_player(ctx_); }
    int32_t KillHostileNpcs(float radius) const { return HasToolsApi() ? api_->kill_hostile_npcs(ctx_, radius) : -1; }

    // Só no init. false = runtime antigo (sem receitas) ou definição inválida (log).
    bool RegisterRecipe(const RecipeDef& def) const {
        if (!HasRecipeApi()) return false;
        TLModRecipeDef c{};
        c.struct_size = sizeof(c);
        c.result = def.result;
        c.result_stack = def.resultStack;
        c.ingredients = def.ingredients.data();
        c.ingredient_count = static_cast<int32_t>(def.ingredients.size());
        c.stations = def.stations.data();
        c.station_count = static_cast<int32_t>(def.stations.size());
        c.need_water = def.needWater;
        c.need_lava = def.needLava;
        c.need_honey = def.needHoney;
        return api_->register_recipe(ctx_, &c);
    }

    bool OnPlayerHurt(TLModPlayerHurtFn fn, void* user = nullptr) const {
        return api_->subscribe_player_hurt(ctx_, fn, user);
    }
    TLModPlayer LocalPlayer() const { return api_->local_player(ctx_); }
    int32_t PlayerLife(TLModPlayer p) const { return api_->player_life(ctx_, p); }

private:
    static bool ProviderTrampoline(void* user, TLModBuffer* out) {
        auto* p = static_cast<DataProvider*>(user);
        std::string json;
        if (!p->Build(json)) return false;
        p->api_->buffer_append(out, json.data(), json.size());
        return true;
    }
    bool HasProgressApi() const {
        return api_->struct_size >= offsetof(TLModHostApi, buffer_append) + sizeof(api_->buffer_append);
    }
    bool HasGiveApi() const {
        return api_->struct_size >= offsetof(TLModHostApi, give_item) + sizeof(api_->give_item);
    }
    bool HasToolsApi() const {
        return api_->struct_size >= offsetof(TLModHostApi, kill_hostile_npcs) + sizeof(api_->kill_hostile_npcs);
    }
    bool HasRecipeApi() const {
        return api_->struct_size >= offsetof(TLModHostApi, register_recipe) + sizeof(api_->register_recipe);
    }
    static void FrameTrampoline(void* user) { static_cast<FrameListener*>(user)->OnFrame(); }
    static bool CommandTrampoline(void* user, const char* command, TLModBuffer* out) {
        std::string json;
        if (!static_cast<CommandHandler*>(user)->Handle(command ? command : "", json)) return false;
        if (!json.empty()) handler_api_->buffer_append(out, json.data(), json.size());
        return true;
    }
    static inline const TLModHostApi* handler_api_ = nullptr;  // um runtime por processo
    bool HasSpawnNearApi() const {
        return api_->struct_size >=
               offsetof(TLModHostApi, spawn_npc_near_player) + sizeof(api_->spawn_npc_near_player);
    }
    bool HasNpcApi() const {
        return api_->struct_size >= offsetof(TLModHostApi, spawn_npc) + sizeof(api_->spawn_npc);
    }
    static void SpawnTrampoline(void* user, TLModNpcState* npc) {
        static_cast<NpcBehavior*>(user)->OnSpawn(*npc);
    }
    static bool AiTrampoline(void* user, TLModNpcState* npc) {
        return static_cast<NpcBehavior*>(user)->OnAI(*npc);
    }

    const TLModHostApi* api_;
    TLModContext* ctx_;
};

// Confere a versão da ABI antes de qualquer chamada. Exige só o núcleo da
// tabela (até player_life): funções acrescentadas depois (NPCs) são checadas
// uma a uma, então o mod roda também num runtime mais antigo, sem elas.
inline bool CompatibleHost(const TLModHostApi* api) {
    return api && api->api_version == TLMOD_API_VERSION &&
           api->struct_size >= offsetof(TLModHostApi, register_npc);
}

}  // namespace tlmod

// Define o ponto de entrada exportado a partir de uma função bool(tlmod::Host&).
#define TLMOD_MOD(OnLoadFn)                                                          \
    extern "C" TLMOD_MOD_EXPORT int tlmod_mod_init(const TLModHostApi* api,       \
                                                     TLModContext* ctx) {          \
        if (!::tlmod::CompatibleHost(api)) return -1;                               \
        ::tlmod::Host host(api, ctx);                                               \
        return OnLoadFn(host) ? 0 : 1;                                               \
    }
