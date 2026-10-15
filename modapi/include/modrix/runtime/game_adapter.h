#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "modrix/modrix_api.h"

namespace modrix::rt {

// Tudo o que o runtime precisa do jogo. O runtime e a API de mods dependem só
// desta interface; o adaptador concreto (ponte IL2CPP no Android, um falso nos
// testes, futuramente outra plataforma) fica atrás dela.
class GameAdapter {
public:
    virtual ~GameAdapter() = default;

    virtual const char* Name() const = 0;

    // Quantidade de IDs originais do tipo, se conhecida. O registro usa isso
    // para garantir que a faixa dos mods começa acima de tudo que é original.
    virtual std::optional<int32_t> VanillaCount(ModrixContentKind kind) const = 0;

    // Um único handler: o runtime faz a distribuição para os mods.
    using HurtHandler = std::function<bool(ModrixPlayerHurt& ev, bool after)>;
    virtual void SetPlayerHurtHandler(HurtHandler handler) = 0;

    virtual ModrixPlayer LocalPlayer() const = 0;
    virtual int32_t PlayerLife(ModrixPlayer player) const = 0;  // -1 se inválido
};

}  // namespace modrix::rt
