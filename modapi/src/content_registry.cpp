#include "modrix/runtime/content_registry.h"

#include "modrix/runtime/game_adapter.h"
#include "modrix/runtime/log.h"
#include "modrix/runtime/manifest.h"

namespace modrix::rt {

// Faixas do Terraria Mobile (Unity 2021.3.56f2). Contagens originais vêm de
// Terraria.ID.*.Count no dump.cs; WallID.Count é static readonly (só existe
// em runtime) e por isso fica -1 aqui.
//
//   tipo        originais   1º ID de mod  máx.    limite vem de
//   item           5456        8192      32767   short na rede (NetMessage)
//   npc     -65..687            4096     32767   short (IDs negativos também são originais)
//   projectile     1022        4096      32767   short
//   tile            693        4096      65535   ushort (Tile.type)
//   wall              ?        4096      65535   ushort
//   buff            355        4096      65535   ushort na rede
//
// A folga entre o último original e o 1º ID de mod absorve itens que futuras
// versões do jogo acrescentarem, sem mudar IDs de mods.
const std::array<KindRange, MODRIX_CONTENT_KIND_COUNT>& ContentRegistry::DefaultRanges() {
    static const std::array<KindRange, MODRIX_CONTENT_KIND_COUNT> kRanges = {{
        {"item", 5456, 8192, 32767},
        {"npc", 688, 4096, 32767},
        {"projectile", 1022, 4096, 32767},
        {"tile", 693, 4096, 65535},
        {"wall", -1, 4096, 65535},
        {"buff", 355, 4096, 65535},
    }};
    return kRanges;
}

ContentRegistry::ContentRegistry(const GameAdapter* game,
                                 std::array<KindRange, MODRIX_CONTENT_KIND_COUNT> ranges)
    : ranges_(ranges) {
    for (int k = 0; k < MODRIX_CONTENT_KIND_COUNT; ++k) {
        auto kind = static_cast<ModrixContentKind>(k);
        const KindRange& r = ranges_[k];
        // Valor em runtime (do jogo) tem prioridade sobre o do dump.
        std::optional<int32_t> count = game ? game->VanillaCount(kind) : std::nullopt;
        if (!count && r.dumpVanillaCount >= 0) count = r.dumpVanillaCount;
        vanilla_[k] = count;

        enabled_[k] = r.firstModId <= r.maxId;
        if (count && *count > r.firstModId) {
            // O jogo cresceu além da folga: desativar é melhor que colidir.
            Log(MODRIX_LOG_ERROR, "registry",
                std::string(r.name) + ": o jogo tem " + std::to_string(*count) +
                    " IDs originais, acima do 1º ID de mod (" + std::to_string(r.firstModId) +
                    "). Conteúdo de mod desse tipo fica desativado.");
            enabled_[k] = false;
        }
    }
}

std::optional<ModrixId> ContentRegistry::Register(ModrixContentKind kind, const std::string& modId,
                                                  const std::string& name, ContentPayload payload,
                                                  std::string* error) {
    auto fail = [&](const std::string& msg) -> std::optional<ModrixId> {
        if (error) *error = msg;
        return std::nullopt;
    };
    if (kind < 0 || kind >= MODRIX_CONTENT_KIND_COUNT) return fail("tipo de conteúdo inválido");
    const KindRange& r = ranges_[kind];
    if (!IsValidModId(modId)) return fail("id de mod inválido");
    if (!IsValidContentName(name)) return fail("nome \"" + name + "\" inválido; use [a-z0-9_]{1,64}");
    if (!enabled_[kind]) return fail(std::string(r.name) + ": conteúdo de mod desativado");

    std::string key = modId + ":" + name;
    if (byKey_[kind].count(key)) return fail(std::string(r.name) + " \"" + key + "\" já registrado");

    int64_t next = static_cast<int64_t>(r.firstModId) + static_cast<int64_t>(entries_[kind].size());
    if (next > r.maxId) {
        return fail(std::string(r.name) + ": faixa de IDs de mods esgotada (" +
                    std::to_string(r.firstModId) + ".." + std::to_string(r.maxId) + ")");
    }
    auto id = static_cast<ModrixId>(next);
    entries_[kind].push_back({kind, id, key, modId, name, std::move(payload)});
    byKey_[kind][key] = id;
    return id;
}

std::optional<ModrixId> ContentRegistry::Find(ModrixContentKind kind, const std::string& key) const {
    if (kind < 0 || kind >= MODRIX_CONTENT_KIND_COUNT) return std::nullopt;
    auto it = byKey_[kind].find(key);
    if (it == byKey_[kind].end()) return std::nullopt;
    return it->second;
}

bool ContentRegistry::IsModId(ModrixContentKind kind, ModrixId id) const {
    if (kind < 0 || kind >= MODRIX_CONTENT_KIND_COUNT) return false;
    return id >= ranges_[kind].firstModId &&
           id < ranges_[kind].firstModId + static_cast<int64_t>(entries_[kind].size());
}

ContentRegistry::Checkpoint ContentRegistry::Mark() const {
    Checkpoint cp;
    for (int k = 0; k < MODRIX_CONTENT_KIND_COUNT; ++k) cp.sizes[k] = entries_[k].size();
    return cp;
}

void ContentRegistry::Rollback(const Checkpoint& cp) {
    for (int k = 0; k < MODRIX_CONTENT_KIND_COUNT; ++k) {
        while (entries_[k].size() > cp.sizes[k]) {
            byKey_[k].erase(entries_[k].back().key);
            entries_[k].pop_back();
        }
    }
}

const ContentEntry* ContentRegistry::Get(ModrixContentKind kind, ModrixId id) const {
    if (!IsModId(kind, id)) return nullptr;
    return &entries_[kind][static_cast<size_t>(id - ranges_[kind].firstModId)];
}

}  // namespace modrix::rt
