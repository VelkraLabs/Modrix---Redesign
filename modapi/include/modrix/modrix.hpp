// Camada C++ opcional, só de header, sobre modrix_api.h.
//
// Tudo aqui é compilado DENTRO do mod: na fronteira com o runtime só passam
// as structs C de modrix_api.h, então o mod pode usar outra versão de
// compilador/libc++ sem quebrar a ABI.
//
// Uso:
//   static bool OnLoad(modrix::Host& host) { ...; return true; }
//   MODRIX_MOD(OnLoad)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "modrix_api.h"

namespace modrix {

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
};

struct NpcDef {
    std::string name;          // chave local, declarada em content.npcs
    std::string displayName;
    ModrixId shellType = 0;    // NPC original usado como casca
    int32_t lifeMax = 0;       // <= 0: o da casca
    int32_t damage = -1;       // < 0: o da casca
    int32_t defense = -1;      // < 0: o da casca
    bool boss = false;
};

// Comportamento de um NPC de mod. Uma instância por TIPO (não por NPC): o
// estado de cada NPC vive em ModrixNpcState (ai[] / local[]).
class NpcBehavior {
public:
    virtual ~NpcBehavior() = default;
    virtual void OnSpawn(ModrixNpcState& /*npc*/) {}
    // true = substitui a AI da casca neste tick.
    virtual bool OnAI(ModrixNpcState& /*npc*/) { return false; }
};

// Provedor de dados para a interface: devolve um documento JSON.
class DataProvider {
public:
    virtual ~DataProvider() = default;
    virtual bool Build(std::string& json) = 0;

private:
    friend class Host;
    const ModrixHostApi* api_ = nullptr;  // preenchido por Host::Provide
};

class Host {
public:
    Host(const ModrixHostApi* api, ModrixContext* ctx) : api_(api), ctx_(ctx) {}

    const char* ModId() const { return api_->mod_id(ctx_); }
    const char* ModVersion() const { return api_->mod_version(ctx_); }
    const char* ModDir() const { return api_->mod_dir(ctx_); }

    void Log(ModrixLogLevel level, const std::string& msg) const {
        api_->log(ctx_, level, msg.c_str());
    }
    void Info(const std::string& msg) const { Log(MODRIX_LOG_INFO, msg); }
    void Warn(const std::string& msg) const { Log(MODRIX_LOG_WARN, msg); }
    void Error(const std::string& msg) const { Log(MODRIX_LOG_ERROR, msg); }

    ModrixId RegisterItem(const ItemDef& def) const {
        ModrixItemDef c{};
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
        return api_->register_item(ctx_, &c);
    }

    ModrixId Find(ModrixContentKind kind, const std::string& key) const {
        return api_->find_content(ctx_, kind, key.c_str());
    }

    // `behavior` precisa viver enquanto o jogo roda (ex.: variável estática).
    ModrixId RegisterNpc(const NpcDef& def, NpcBehavior* behavior) const {
        if (!HasNpcApi()) return MODRIX_INVALID_ID;
        ModrixNpcDef c{};
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
        return api_->register_npc(ctx_, &c);
    }
    int32_t SpawnNpc(ModrixId type, float x, float y) const {
        return HasNpcApi() ? api_->spawn_npc(ctx_, type, x, y) : -1;
    }

    // -1/0/1, ver modrix_api.h. Seguros em qualquer thread.
    int32_t InWorld() const { return HasProgressApi() ? api_->in_world(ctx_) : -1; }
    int32_t Progress(ModrixProgress flag) const { return HasProgressApi() ? api_->progress(ctx_, flag) : -1; }

    // `provider` precisa viver enquanto o jogo roda (ex.: objeto estático).
    bool Provide(const std::string& channel, DataProvider* provider) const {
        if (!HasProgressApi() || !provider) return false;
        provider->api_ = api_;
        return api_->register_provider(ctx_, channel.c_str(), &ProviderTrampoline, provider);
    }

    bool OnPlayerHurt(ModrixPlayerHurtFn fn, void* user = nullptr) const {
        return api_->subscribe_player_hurt(ctx_, fn, user);
    }
    ModrixPlayer LocalPlayer() const { return api_->local_player(ctx_); }
    int32_t PlayerLife(ModrixPlayer p) const { return api_->player_life(ctx_, p); }

private:
    static bool ProviderTrampoline(void* user, ModrixBuffer* out) {
        auto* p = static_cast<DataProvider*>(user);
        std::string json;
        if (!p->Build(json)) return false;
        p->api_->buffer_append(out, json.data(), json.size());
        return true;
    }
    bool HasProgressApi() const {
        return api_->struct_size >= offsetof(ModrixHostApi, buffer_append) + sizeof(api_->buffer_append);
    }
    bool HasNpcApi() const {
        return api_->struct_size >= offsetof(ModrixHostApi, spawn_npc) + sizeof(api_->spawn_npc);
    }
    static void SpawnTrampoline(void* user, ModrixNpcState* npc) {
        static_cast<NpcBehavior*>(user)->OnSpawn(*npc);
    }
    static bool AiTrampoline(void* user, ModrixNpcState* npc) {
        return static_cast<NpcBehavior*>(user)->OnAI(*npc);
    }

    const ModrixHostApi* api_;
    ModrixContext* ctx_;
};

// Confere a versão da ABI antes de qualquer chamada. Exige só o núcleo da
// tabela (até player_life): funções acrescentadas depois (NPCs) são checadas
// uma a uma, então o mod roda também num runtime mais antigo, sem elas.
inline bool CompatibleHost(const ModrixHostApi* api) {
    return api && api->api_version == MODRIX_API_VERSION &&
           api->struct_size >= offsetof(ModrixHostApi, register_npc);
}

}  // namespace modrix

// Define o ponto de entrada exportado a partir de uma função bool(modrix::Host&).
#define MODRIX_MOD(OnLoadFn)                                                          \
    extern "C" MODRIX_MOD_EXPORT int modrix_mod_init(const ModrixHostApi* api,       \
                                                     ModrixContext* ctx) {          \
        if (!::modrix::CompatibleHost(api)) return -1;                               \
        ::modrix::Host host(api, ctx);                                               \
        return OnLoadFn(host) ? 0 : 1;                                               \
    }
