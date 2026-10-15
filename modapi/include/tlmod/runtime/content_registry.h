#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "tlmod/tlmod_api.h"

namespace tlmod::rt {

class GameAdapter;

// Faixa de IDs de um tipo de conteúdo.
//
//   [0, vanillaCount)          IDs originais do jogo (valores do dump)
//   [vanillaCount, firstModId) folga reservada para futuras atualizações do jogo
//   [firstModId, maxId]        IDs de mods, atribuídos em sequência
//
// firstModId é FIXO e bem acima dos originais. Se uma versão do jogo
// ultrapassar firstModId, o tipo é desativado para mods (erro no log) em vez
// de arriscar colisão. maxId vem do tipo usado pelo jogo para guardar/enviar
// o ID (short na rede para itens, ushort para tiles etc.).
struct KindRange {
    const char* name;            // "item"
    int32_t dumpVanillaCount;    // do dump; -1 = só conhecido em runtime
    int32_t firstModId;
    int32_t maxId;
};

// Item original usado como casca quando o mod não escolhe (ItemID.Lens = 38:
// material sem uso, sem tile, sem tiro). Igual a terraria::kDefaultItemShell,
// que o offset_diff confere contra o dump.
constexpr TLModId kDefaultItemShell = 38;

// Item de mod: roda numa casca original (ver TLModItemDef.shell_type).
struct ItemDefinition {
    TLModId shellType = kDefaultItemShell;
    std::string displayName;
    std::string tooltip;
    std::string texture;  // PNG, caminho absoluto já resolvido na pasta do mod; vazio = o da casca
    int32_t width = 0, height = 0, maxStack = 1, value = 0, rarity = 0, damage = 0, useTime = 0;
};

// NPC de mod: roda dentro de uma "casca" original (ver TLModNpcDef).
struct NpcDefinition {
    std::string displayName;
    TLModId shellType = 0;
    int32_t lifeMax = 0, damage = -1, defense = -1;
    bool boss = false;
    void* user = nullptr;
    TLModNpcSpawnFn onSpawn = nullptr;
    TLModNpcAiFn onAi = nullptr;
    std::string texture;     // caminho absoluto do PNG; vazio = o da casca
    int32_t frameCount = 1;  // quadros na folha (vertical)
    int32_t frameTicks = 8;  // ticks por quadro (animação automática)
};

using ContentPayload = std::variant<std::monostate, ItemDefinition, NpcDefinition>;

struct ContentEntry {
    TLModContentKind kind;
    TLModId id;
    std::string key;      // "mod_id:nome", estável: use para salvar/sincronizar
    std::string modId;
    std::string name;
    ContentPayload payload;

    const ItemDefinition* Item() const { return std::get_if<ItemDefinition>(&payload); }
    const NpcDefinition* Npc() const { return std::get_if<NpcDefinition>(&payload); }
};

class ContentRegistry {
public:
    // Faixas padrão do Terraria Mobile (ver content_registry.cpp).
    static const std::array<KindRange, TLMOD_CONTENT_KIND_COUNT>& DefaultRanges();

    explicit ContentRegistry(const GameAdapter* game,
                             std::array<KindRange, TLMOD_CONTENT_KIND_COUNT> ranges = DefaultRanges());

    // Reserva o próximo ID do tipo para "mod_id:nome". Falha (e explica em
    // *error) se o nome for inválido, a chave já existir, o tipo estiver
    // desativado ou a faixa tiver acabado.
    std::optional<TLModId> Register(TLModContentKind kind, const std::string& modId,
                                     const std::string& name, ContentPayload payload,
                                     std::string* error);

    std::optional<TLModId> Find(TLModContentKind kind, const std::string& key) const;
    const ContentEntry* Get(TLModContentKind kind, TLModId id) const;

    bool IsModId(TLModContentKind kind, TLModId id) const;
    bool KindEnabled(TLModContentKind kind) const { return enabled_[kind]; }
    const KindRange& Range(TLModContentKind kind) const { return ranges_[kind]; }
    std::optional<int32_t> VanillaCount(TLModContentKind kind) const { return vanilla_[kind]; }

    const std::vector<ContentEntry>& Entries(TLModContentKind kind) const { return entries_[kind]; }

    // Desfaz registros feitos depois do checkpoint: usado quando o init de um
    // mod falha, para o conteúdo dele não ficar órfão com IDs ocupados.
    struct Checkpoint {
        std::array<size_t, TLMOD_CONTENT_KIND_COUNT> sizes{};
    };
    Checkpoint Mark() const;
    void Rollback(const Checkpoint& cp);

private:
    std::array<KindRange, TLMOD_CONTENT_KIND_COUNT> ranges_;
    std::array<std::optional<int32_t>, TLMOD_CONTENT_KIND_COUNT> vanilla_{};
    std::array<bool, TLMOD_CONTENT_KIND_COUNT> enabled_{};
    std::array<std::vector<ContentEntry>, TLMOD_CONTENT_KIND_COUNT> entries_;
    std::array<std::map<std::string, TLModId>, TLMOD_CONTENT_KIND_COUNT> byKey_;
};

}  // namespace tlmod::rt
