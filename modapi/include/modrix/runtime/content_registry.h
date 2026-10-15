#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "modrix/modrix_api.h"

namespace modrix::rt {

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

struct ItemDefinition {
    std::string displayName;
    std::string tooltip;
    std::string texture;  // caminho absoluto, já resolvido na pasta do mod
    int32_t width = 0, height = 0, maxStack = 1, value = 0, rarity = 0, damage = 0, useTime = 0;
};

struct ContentEntry {
    ModrixContentKind kind;
    ModrixId id;
    std::string key;      // "mod_id:nome", estável: use para salvar/sincronizar
    std::string modId;
    std::string name;
    std::optional<ItemDefinition> item;  // preenchido quando kind == ITEM
};

class ContentRegistry {
public:
    // Faixas padrão do Terraria Mobile (ver content_registry.cpp).
    static const std::array<KindRange, MODRIX_CONTENT_KIND_COUNT>& DefaultRanges();

    explicit ContentRegistry(const GameAdapter* game,
                             std::array<KindRange, MODRIX_CONTENT_KIND_COUNT> ranges = DefaultRanges());

    // Reserva o próximo ID do tipo para "mod_id:nome". Falha (e explica em
    // *error) se o nome for inválido, a chave já existir, o tipo estiver
    // desativado ou a faixa tiver acabado.
    std::optional<ModrixId> Register(ModrixContentKind kind, const std::string& modId,
                                     const std::string& name, std::optional<ItemDefinition> item,
                                     std::string* error);

    std::optional<ModrixId> Find(ModrixContentKind kind, const std::string& key) const;
    const ContentEntry* Get(ModrixContentKind kind, ModrixId id) const;

    bool IsModId(ModrixContentKind kind, ModrixId id) const;
    bool KindEnabled(ModrixContentKind kind) const { return enabled_[kind]; }
    const KindRange& Range(ModrixContentKind kind) const { return ranges_[kind]; }
    std::optional<int32_t> VanillaCount(ModrixContentKind kind) const { return vanilla_[kind]; }

    const std::vector<ContentEntry>& Entries(ModrixContentKind kind) const { return entries_[kind]; }

    // Desfaz registros feitos depois do checkpoint: usado quando o init de um
    // mod falha, para o conteúdo dele não ficar órfão com IDs ocupados.
    struct Checkpoint {
        std::array<size_t, MODRIX_CONTENT_KIND_COUNT> sizes{};
    };
    Checkpoint Mark() const;
    void Rollback(const Checkpoint& cp);

private:
    std::array<KindRange, MODRIX_CONTENT_KIND_COUNT> ranges_;
    std::array<std::optional<int32_t>, MODRIX_CONTENT_KIND_COUNT> vanilla_{};
    std::array<bool, MODRIX_CONTENT_KIND_COUNT> enabled_{};
    std::array<std::vector<ContentEntry>, MODRIX_CONTENT_KIND_COUNT> entries_;
    std::array<std::map<std::string, ModrixId>, MODRIX_CONTENT_KIND_COUNT> byKey_;
};

}  // namespace modrix::rt
