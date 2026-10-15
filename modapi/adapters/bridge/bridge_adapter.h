#pragma once

#include "modrix/runtime/game_adapter.h"

namespace modrix::rt {

// GameAdapter sobre a ponte IL2CPP (il2cpp_bridge). É o único ponto em que
// tipos do IL2CPP (Il2CppObject*) aparecem; tudo que sai daqui para o runtime
// e para os mods é índice de player, inteiro ou struct C da API.
class BridgeGameAdapter final : public GameAdapter {
public:
    const char* Name() const override { return "il2cpp-bridge (Terraria Mobile)"; }
    std::optional<int32_t> VanillaCount(ModrixContentKind kind) const override;
    void SetPlayerHurtHandler(HurtHandler handler) override;
    ModrixPlayer LocalPlayer() const override;
    int32_t PlayerLife(ModrixPlayer player) const override;
};

}  // namespace modrix::rt
