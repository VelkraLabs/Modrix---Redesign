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

    bool OnPlayerHurt(ModrixPlayerHurtFn fn, void* user = nullptr) const {
        return api_->subscribe_player_hurt(ctx_, fn, user);
    }
    ModrixPlayer LocalPlayer() const { return api_->local_player(ctx_); }
    int32_t PlayerLife(ModrixPlayer p) const { return api_->player_life(ctx_, p); }

private:
    const ModrixHostApi* api_;
    ModrixContext* ctx_;
};

// Confere a versão da ABI antes de qualquer chamada.
inline bool CompatibleHost(const ModrixHostApi* api) {
    return api && api->api_version == MODRIX_API_VERSION &&
           api->struct_size >= sizeof(ModrixHostApi);
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
