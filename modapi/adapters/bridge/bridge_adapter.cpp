#include "bridge_adapter.h"

#include <atomic>

#include "bridge.h"            // il2cpp_bridge/include
#include "terraria_offsets.h"  // kMaxNPCs

namespace tlmod::rt {
namespace {

// Handler atual. Trocado por ponteiro atômico porque o hook de Player.Hurt
// pode estar rodando na thread do jogo enquanto o runtime instala o handler.
// Handlers antigos não são liberados (são poucos e evitam use-after-free).
std::atomic<GameAdapter::HurtHandler*> g_handler{nullptr};

bool OnBridgeHurt(TLModHurtEvent* ev, bool after) {
    GameAdapter::HurtHandler* h = g_handler.load(std::memory_order_acquire);
    if (!h || !*h) return true;

    TLModPlayerHurt mev{};
    mev.player = TLMod_GetPlayerIndex(ev->player);
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

// --- Estado do mundo -------------------------------------------------------------
// TLModProgress -> campo estático do jogo (nomes conferidos no dump.cs).
// Mapeamento das mecânicas (MechBoss1..3) segue o código do PC 1.4.4.
struct ProgressField {
    TLModProgress flag;
    const char* klass;
    const char* field;
};
constexpr ProgressField kProgressFields[] = {
    {TLMOD_PROGRESS_HARDMODE, "Main", "hardMode"},  // static_fields+0x360
    {TLMOD_DOWNED_KING_SLIME, "NPC", "downedSlimeKing"},
    {TLMOD_DOWNED_EYE_OF_CTHULHU, "NPC", "downedBoss1"},
    {TLMOD_DOWNED_EVIL_BOSS, "NPC", "downedBoss2"},
    {TLMOD_DOWNED_QUEEN_BEE, "NPC", "downedQueenBee"},
    {TLMOD_DOWNED_SKELETRON, "NPC", "downedBoss3"},
    {TLMOD_DOWNED_DEERCLOPS, "NPC", "downedDeerclops"},
    {TLMOD_DOWNED_QUEEN_SLIME, "NPC", "downedQueenSlime"},
    {TLMOD_DOWNED_DESTROYER, "NPC", "downedMechBoss1"},
    {TLMOD_DOWNED_TWINS, "NPC", "downedMechBoss2"},
    {TLMOD_DOWNED_SKELETRON_PRIME, "NPC", "downedMechBoss3"},
    {TLMOD_DOWNED_PLANTERA, "NPC", "downedPlantBoss"},
    {TLMOD_DOWNED_GOLEM, "NPC", "downedGolemBoss"},
    {TLMOD_DOWNED_DUKE_FISHRON, "NPC", "downedFishron"},
    {TLMOD_DOWNED_EMPRESS_OF_LIGHT, "NPC", "downedEmpressOfLight"},
    {TLMOD_DOWNED_LUNATIC_CULTIST, "NPC", "downedAncientCultist"},
    {TLMOD_DOWNED_MOON_LORD, "NPC", "downedMoonlord"},
    {TLMOD_DOWNED_GOBLIN_ARMY, "NPC", "downedGoblins"},
    {TLMOD_DOWNED_FROST_LEGION, "NPC", "downedFrost"},
    {TLMOD_DOWNED_PIRATES, "NPC", "downedPirates"},
    {TLMOD_DOWNED_MARTIANS, "NPC", "downedMartians"},
    {TLMOD_DOWNED_MOURNING_WOOD, "NPC", "downedHalloweenTree"},
    {TLMOD_DOWNED_PUMPKING, "NPC", "downedHalloweenKing"},
    {TLMOD_DOWNED_EVERSCREAM, "NPC", "downedChristmasTree"},
    {TLMOD_DOWNED_SANTA_NK1, "NPC", "downedChristmasSantank"},
    {TLMOD_DOWNED_ICE_QUEEN, "NPC", "downedChristmasIceQueen"},
    {TLMOD_DOWNED_SOLAR_PILLAR, "NPC", "downedTowerSolar"},
    {TLMOD_DOWNED_VORTEX_PILLAR, "NPC", "downedTowerVortex"},
    {TLMOD_DOWNED_NEBULA_PILLAR, "NPC", "downedTowerNebula"},
    {TLMOD_DOWNED_STARDUST_PILLAR, "NPC", "downedTowerStardust"},
};
static_assert(sizeof(kProgressFields) / sizeof(kProgressFields[0]) == TLMOD_PROGRESS_COUNT,
              "toda flag de TLModProgress precisa de um campo");
static_assert(TLMOD_PROGRESS_COUNT <= 31, "a fotografia usa 31 bits de valor + 31 de 'conhecido'");

// Fotografia num único uint64_t, para leitura consistente de qualquer thread:
//   bits  0..30  valor da flag i
//   bit     31   está num mundo
//   bits 32..62  flag i conhecida (campo resolvido e lido)
//   bit     63   "está num mundo" conhecido (já houve um frame com Instance)
constexpr int kKnownShift = 32;
constexpr uint64_t kInWorldBit = 1ull << 31;
constexpr uint64_t kInWorldKnownBit = 1ull << 63;
std::atomic<uint64_t> g_snapshot{0};

// Só na thread do jogo (dentro de Main.DoUpdate).
void OnFrame() {
    static TLModStaticField* fields[TLMOD_PROGRESS_COUNT] = {};
    static bool resolved = false;
    if (!resolved) {  // primeira vez: resolve os 30 campos por nome
        for (const ProgressField& f : kProgressFields)
            fields[f.flag] = TLMod_FindStaticField("Terraria", f.klass, f.field);
        resolved = true;
    }
    uint64_t snap = 0;
    int32_t menu = TLMod_IsGameMenu();
    if (menu >= 0) snap |= kInWorldKnownBit | (menu == 0 ? kInWorldBit : 0);
    for (int i = 0; i < TLMOD_PROGRESS_COUNT; ++i) {
        bool v = false;
        if (TLMod_ReadStaticBool(fields[i], &v))
            snap |= (1ull << (kKnownShift + i)) | (v ? 1ull << i : 0);
    }
    g_snapshot.store(snap, std::memory_order_release);
}

}  // namespace

BridgeGameAdapter::BridgeGameAdapter() { TLMod_SetFrameCallback(&OnFrame); }

std::optional<bool> BridgeGameAdapter::InWorld() const {
    uint64_t s = g_snapshot.load(std::memory_order_acquire);
    if (!(s & kInWorldKnownBit)) return std::nullopt;
    return (s & kInWorldBit) != 0;
}

std::optional<bool> BridgeGameAdapter::Progress(TLModProgress flag) const {
    if (flag < 0 || flag >= TLMOD_PROGRESS_COUNT) return std::nullopt;
    uint64_t s = g_snapshot.load(std::memory_order_acquire);
    if (!(s & (1ull << (kKnownShift + flag)))) return std::nullopt;
    return (s & (1ull << flag)) != 0;
}

std::optional<int32_t> BridgeGameAdapter::VanillaCount(TLModContentKind) const {
    // ItemID.Count, NPCID.Count etc. são `const` no C#: o compilador embute o
    // valor e não existe campo para ler em runtime. O registro usa os valores
    // do dump (ContentRegistry::DefaultRanges). WallID.Count é static readonly
    // e poderia ser lido via il2cpp_field_static_get_value numa próxima versão.
    return std::nullopt;
}

void BridgeGameAdapter::SetPlayerHurtHandler(HurtHandler handler) {
    HurtHandler* next = handler ? new HurtHandler(std::move(handler)) : nullptr;
    g_handler.store(next, std::memory_order_release);
    TLMod_SetHurtCallback(next ? &OnBridgeHurt : nullptr);
}

TLModPlayer BridgeGameAdapter::LocalPlayer() const {
    return TLMod_GetPlayerIndex(TLMod_GetLocalPlayer());
}

int32_t BridgeGameAdapter::PlayerLife(TLModPlayer player) const {
    Il2CppObject* p = TLMod_GetPlayerByIndex(player);
    return p ? TLMod_GetStatLife(p) : -1;
}

void BridgeGameAdapter::SetNpcHooks(NpcHooks hooks) {
    bool any = hooks.remapSpawnType || hooks.onSpawned || hooks.onSetDefaults || hooks.onAi;
    g_npcHooks.store(any ? new NpcHooks(std::move(hooks)) : nullptr, std::memory_order_release);
    TLModNpcCallbacks cb{&NpcRemap, &NpcSpawned, &NpcSetDefaults, &NpcAi};
    TLMod_SetNpcCallbacks(any ? &cb : nullptr);
}

int32_t BridgeGameAdapter::MaxNpcs() const { return terraria::kMaxNPCs; }

bool BridgeGameAdapter::ReadNpc(int32_t index, TLModNpcState& out) const {
    TLModNpcFields f{};
    if (!TLMod_ReadNpc(TLMod_GetNpcByIndex(index), &f) || !f.active) return false;
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

bool BridgeGameAdapter::WriteNpc(int32_t index, const TLModNpcState& in) {
    Il2CppObject* npc = TLMod_GetNpcByIndex(index);
    TLModNpcFields f{};
    if (!TLMod_ReadNpc(npc, &f)) return false;
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
    return TLMod_WriteNpc(npc, &f);
}

int32_t BridgeGameAdapter::SpawnNpc(int32_t type, float x, float y) {
    int32_t index = TLMod_SpawnNpc(static_cast<int32_t>(x), static_cast<int32_t>(y), type);
    return (index >= 0 && index < terraria::kMaxNPCs) ? index : -1;
}

}  // namespace tlmod::rt
