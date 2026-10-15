#pragma once

#include "tlmod/runtime/game_adapter.h"

namespace tlmod::rt {

// GameAdapter sobre a ponte IL2CPP (il2cpp_bridge). É o único ponto em que
// tipos do IL2CPP (Il2CppObject*) aparecem; tudo que sai daqui para o runtime
// e para os mods é índice de player, inteiro ou struct C da API.
class BridgeGameAdapter final : public GameAdapter {
public:
    // Registra o callback por frame da ponte (Main.DoUpdate), que fotografa
    // o estado do mundo na thread do jogo.
    BridgeGameAdapter();

    const char* Name() const override { return "il2cpp-bridge (Terraria Mobile)"; }
    std::optional<int32_t> VanillaCount(TLModContentKind kind) const override;
    void SetPlayerHurtHandler(HurtHandler handler) override;
    TLModPlayer LocalPlayer() const override;
    int32_t PlayerLife(TLModPlayer player) const override;

    // NPCs: NPC.NewNPC (0xEE4630), NPC.SetDefaults (0xDAF1BC) e NPC.AI
    // (0xDCE54C, chamado por NPC.UpdateNPC 0xF4A2E8), via callbacks da ponte.
    void SetNpcHooks(NpcHooks hooks) override;
    int32_t MaxNpcs() const override;
    bool ReadNpc(int32_t index, TLModNpcState& out) const override;
    bool WriteNpc(int32_t index, const TLModNpcState& in) override;
    int32_t SpawnNpc(int32_t type, float x, float y) override;

    // Leem a fotografia do último frame: seguros em qualquer thread.
    std::optional<bool> InWorld() const override;
    std::optional<bool> Progress(TLModProgress flag) const override;
};

}  // namespace tlmod::rt
