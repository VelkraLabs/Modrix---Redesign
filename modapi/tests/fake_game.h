// GameAdapter falso para testes: simula players e o fluxo de Player.Hurt.
#pragma once

#include <array>

#include "modrix/runtime/game_adapter.h"

class FakeGame final : public modrix::rt::GameAdapter {
public:
    std::array<std::optional<int32_t>, MODRIX_CONTENT_KIND_COUNT> vanilla{};
    std::array<int32_t, 4> life{400, 300, 200, 100};
    ModrixPlayer local = 0;
    HurtHandler handler;

    const char* Name() const override { return "fake"; }
    std::optional<int32_t> VanillaCount(ModrixContentKind k) const override { return vanilla[k]; }
    void SetPlayerHurtHandler(HurtHandler h) override { handler = std::move(h); }
    ModrixPlayer LocalPlayer() const override { return local; }
    int32_t PlayerLife(ModrixPlayer p) const override {
        return (p >= 0 && p < static_cast<int>(life.size())) ? life[static_cast<size_t>(p)] : -1;
    }

    // Faz o "jogo" aplicar dano: passa pelos mods (pré), aplica, avisa (pós).
    // Retorna o dano aplicado, ou 0 se algum mod cancelou.
    int32_t Hurt(ModrixPlayer p, int32_t damage) {
        auto& hp = life[static_cast<size_t>(p)];
        ModrixPlayerHurt ev{p, damage, false, false, hp, 0};
        if (handler && !handler(ev, false)) return 0;
        hp -= ev.damage;
        ev.life_after = hp;
        if (handler) handler(ev, true);
        return ev.damage;
    }
};
