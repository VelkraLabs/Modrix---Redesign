#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "tlmod/tlmod_api.h"

namespace tlmod::rt {

// Tudo o que o runtime precisa do jogo. O runtime e a API de mods dependem só
// desta interface; o adaptador concreto (ponte IL2CPP no Android, um falso nos
// testes, futuramente outra plataforma) fica atrás dela.
class GameAdapter {
public:
    virtual ~GameAdapter() = default;

    virtual const char* Name() const = 0;

    // Quantidade de IDs originais do tipo, se conhecida. O registro usa isso
    // para garantir que a faixa dos mods começa acima de tudo que é original.
    virtual std::optional<int32_t> VanillaCount(TLModContentKind kind) const = 0;

    // Um único handler: o runtime faz a distribuição para os mods.
    using HurtHandler = std::function<bool(TLModPlayerHurt& ev, bool after)>;
    virtual void SetPlayerHurtHandler(HurtHandler handler) = 0;

    virtual TLModPlayer LocalPlayer() const = 0;
    virtual int32_t PlayerLife(TLModPlayer player) const = 0;  // -1 se inválido

    // --- NPCs -----------------------------------------------------------------
    // Ganchos que o adaptador liga aos pontos do jogo (no Terraria: hooks em
    // NPC.NewNPC, NPC.SetDefaults e NPC.AI). Todos rodam na thread do jogo.
    struct NpcHooks {
        // Antes de criar um NPC: tipo pedido -> tipo que o jogo deve criar.
        std::function<int32_t(int32_t requestedType)> remapSpawnType;
        // Depois de criar. index fora de [0, MaxNpcs()) = o jogo não achou slot.
        std::function<void(int32_t index, int32_t requestedType, int32_t spawnedType)> onSpawned;
        // O slot `index` vai ser reinicializado como `newType` (spawn, transformação, rede).
        std::function<void(int32_t index, int32_t newType)> onSetDefaults;
        // Uma vez por NPC ativo por tick. true = pular a AI original.
        std::function<bool(int32_t index)> onAi;
    };
    virtual void SetNpcHooks(NpcHooks hooks) = 0;

    virtual int32_t MaxNpcs() const = 0;
    // Lê/escreve o estado do NPC no slot. ReadNpc preenche type e shell_type
    // com o tipo que o jogo vê; local[] não é do jogo (o runtime cuida).
    virtual bool ReadNpc(int32_t index, TLModNpcState& out) const = 0;
    virtual bool WriteNpc(int32_t index, const TLModNpcState& in) = 0;
    // Cria um NPC pelo caminho normal do jogo (passa pelos ganchos). -1 se falhar.
    virtual int32_t SpawnNpc(int32_t type, float x, float y) = 0;

    // --- Estado do mundo -----------------------------------------------------
    // Precisam ser seguros em QUALQUER thread (a UI consulta fora da thread do
    // jogo). nullopt = desconhecido.
    virtual std::optional<bool> InWorld() const = 0;
    virtual std::optional<bool> Progress(TLModProgress flag) const = 0;
};

}  // namespace tlmod::rt
