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

    // Itens de mod: Item.SetDefaults (0xBAD034) e Item.RebuildTooltip (0xBAAFE8),
    // via TLMod_SetItemCallbacks. GiveItem e o frame rodam na thread do jogo.
    void SetItemHooks(ItemHooks hooks) override;
    int32_t GiveItem(int32_t type, int32_t stack) override;
    void SetFrameHandler(std::function<void()> handler) override;

    // Save/load: Player.InternalSavePlayerFile (0x1044040), Player.LoadPlayer
    // (0x1049FA8), WorldFile.InternalSaveWorld (0x14F1AC8), WorldFile.LoadWorld
    // (0x14EC048), via TLMod_SetSaveCallback.
    void SetPersistenceHooks(PersistenceHooks hooks) override;
};

}  // namespace tlmod::rt
