#include "bridge_adapter.h"

#include <atomic>

#include "bridge.h"            // il2cpp_bridge/include
#include "terraria_offsets.h"  // kMaxNPCs

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

// Ganchos de NPC: mesma publicação atômica do handler de Hurt.
std::atomic<GameAdapter::NpcHooks*> g_npcHooks{nullptr};

int32_t NpcRemap(int32_t type) {
    GameAdapter::NpcHooks* h = g_npcHooks.load(std::memory_order_acquire);
    return (h && h->remapSpawnType) ? h->remapSpawnType(type) : type;
}
void NpcSpawned(Il2CppObject*, int32_t index, int32_t requested, int32_t spawned) {
    GameAdapter::NpcHooks* h = g_npcHooks.load(std::memory_order_acquire);
    if (h && h->onSpawned) h->onSpawned(index, requested, spawned);
}
void NpcSetDefaults(Il2CppObject*, int32_t index, int32_t type) {
    GameAdapter::NpcHooks* h = g_npcHooks.load(std::memory_order_acquire);
    if (h && h->onSetDefaults) h->onSetDefaults(index, type);
}
bool NpcAi(Il2CppObject*, int32_t index) {
    GameAdapter::NpcHooks* h = g_npcHooks.load(std::memory_order_acquire);
    return h && h->onAi && index >= 0 && h->onAi(index);
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

void BridgeGameAdapter::SetNpcHooks(NpcHooks hooks) {
    bool any = hooks.remapSpawnType || hooks.onSpawned || hooks.onSetDefaults || hooks.onAi;
    g_npcHooks.store(any ? new NpcHooks(std::move(hooks)) : nullptr, std::memory_order_release);
    ModrixNpcCallbacks cb{&NpcRemap, &NpcSpawned, &NpcSetDefaults, &NpcAi};
    Modrix_SetNpcCallbacks(any ? &cb : nullptr);
}

int32_t BridgeGameAdapter::MaxNpcs() const { return terraria::kMaxNPCs; }

bool BridgeGameAdapter::ReadNpc(int32_t index, ModrixNpcState& out) const {
    ModrixNpcFields f{};
    if (!Modrix_ReadNpc(Modrix_GetNpcByIndex(index), &f) || !f.active) return false;
    out.index = index;
    out.type = out.shell_type = f.type;
    out.x = f.x;
    out.y = f.y;
    out.vx = f.vx;
    out.vy = f.vy;
    out.life = f.life;
    out.life_max = f.lifeMax;
    out.damage = f.damage;
    out.defense = f.defense;
    out.target = f.target;
    for (int i = 0; i < 4; ++i) out.ai[i] = f.ai[i];
    return true;
}

bool BridgeGameAdapter::WriteNpc(int32_t index, const ModrixNpcState& in) {
    Il2CppObject* npc = Modrix_GetNpcByIndex(index);
    ModrixNpcFields f{};
    if (!Modrix_ReadNpc(npc, &f)) return false;
    f.x = in.x;
    f.y = in.y;
    f.vx = in.vx;
    f.vy = in.vy;
    f.life = in.life;
    f.lifeMax = in.life_max;
    f.damage = in.damage;
    f.defense = in.defense;
    f.target = in.target;
    for (int i = 0; i < 4; ++i) f.ai[i] = in.ai[i];
    return Modrix_WriteNpc(npc, &f);
}

int32_t BridgeGameAdapter::SpawnNpc(int32_t type, float x, float y) {
    int32_t index = Modrix_SpawnNpc(static_cast<int32_t>(x), static_cast<int32_t>(y), type);
    return (index >= 0 && index < terraria::kMaxNPCs) ? index : -1;
}

}  // namespace modrix::rt
