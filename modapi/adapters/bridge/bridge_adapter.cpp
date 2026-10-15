#include "bridge_adapter.h"

#include <atomic>

#include "bridge.h"  // il2cpp_bridge/include

namespace modrix::rt {
namespace {

// Handler atual. Trocado por ponteiro atômico porque o hook de Player.Hurt
// pode estar rodando na thread do jogo enquanto o runtime instala o handler.
// Handlers antigos não são liberados (são poucos e evitam use-after-free).
std::atomic<GameAdapter::HurtHandler*> g_handler{nullptr};

bool OnBridgeHurt(ModrixHurtEvent* ev, bool after) {
    GameAdapter::HurtHandler* h = g_handler.load(std::memory_order_acquire);
    if (!h || !*h) return true;

    ModrixPlayerHurt mev{};
    mev.player = Modrix_GetPlayerIndex(ev->player);
    mev.damage = ev->damage;
    mev.crit = ev->crit;
    mev.pvp = ev->pvp;
    mev.life_before = ev->lifeBefore;
    mev.life_after = ev->lifeAfter;

    bool keep = (*h)(mev, after);
    if (!after) ev->damage = mev.damage;  // mods podem alterar o dano na fase pré
    return keep;
}

}  // namespace

std::optional<int32_t> BridgeGameAdapter::VanillaCount(ModrixContentKind) const {
    // ItemID.Count, NPCID.Count etc. são `const` no C#: o compilador embute o
    // valor e não existe campo para ler em runtime. O registro usa os valores
    // do dump (ContentRegistry::DefaultRanges). WallID.Count é static readonly
    // e poderia ser lido via il2cpp_field_static_get_value numa próxima versão.
    return std::nullopt;
}

void BridgeGameAdapter::SetPlayerHurtHandler(HurtHandler handler) {
    HurtHandler* next = handler ? new HurtHandler(std::move(handler)) : nullptr;
    g_handler.store(next, std::memory_order_release);
    Modrix_SetHurtCallback(next ? &OnBridgeHurt : nullptr);
}

ModrixPlayer BridgeGameAdapter::LocalPlayer() const {
    return Modrix_GetPlayerIndex(Modrix_GetLocalPlayer());
}

int32_t BridgeGameAdapter::PlayerLife(ModrixPlayer player) const {
    Il2CppObject* p = Modrix_GetPlayerByIndex(player);
    return p ? Modrix_GetStatLife(p) : -1;
}

}  // namespace modrix::rt
