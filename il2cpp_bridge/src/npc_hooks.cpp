// Hooks de NPC: NewNPC (spawn), SetDefaults (slot muda de dono) e AI (tick).
// Ver o diagrama em bridge.h. RVAs e offsets em terraria_offsets.h.
#include <atomic>
#include <cstddef>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

// NPCSpawnParams é passado por valor: o layout precisa bater com o do jogo.
static_assert(sizeof(NPCSpawnParams) == 0x20, "NPCSpawnParams: 32 bytes no dump");
static_assert(offsetof(NPCSpawnParams, playerCountForMultiplayerDifficultyOverride) == 0x08, "");
static_assert(offsetof(NPCSpawnParams, gameModeData) == 0x10, "");
static_assert(offsetof(NPCSpawnParams, strengthMultiplierOverride) == 0x18, "");

namespace {

struct NpcResolved {
    Il2CppClass* klass = nullptr;          // Terraria.NPC
    FieldInfo* mainNpcField = nullptr;     // static NPC[] Main.npc
    const MethodInfo* newNpc = nullptr;    // static int NewNPC(10 args)
    const MethodInfo* setDefaults = nullptr;
    const MethodInfo* aiMethod = nullptr;

    // Começam nos valores do dump; FieldOffset troca pelos do runtime.
    size_t whoAmI = terraria::npc_off::whoAmI;
    size_t active = terraria::npc_off::active;
    size_t position = terraria::npc_off::position;
    size_t velocity = terraria::npc_off::velocity;
    size_t type = terraria::npc_off::type;
    size_t ai = terraria::npc_off::ai;
    size_t target = terraria::npc_off::target;
    size_t damage = terraria::npc_off::damage;
    size_t defense = terraria::npc_off::defense;
    size_t life = terraria::npc_off::life;
    size_t lifeMax = terraria::npc_off::lifeMax;
    // Ferramentas de teste (TLMod_KillHostileNpcs); opcionais.
    size_t width = terraria::npc_off::width;
    size_t height = terraria::npc_off::height;
    size_t friendly = terraria::npc_off::friendly;
    size_t townNPC = terraria::npc_off::townNPC;
    size_t dontTakeDamage = terraria::npc_off::dontTakeDamage;
    const MethodInfo* strike = nullptr;  // double StrikeNPCNoInteraction(int, float, int, bool, bool, bool)
};
NpcResolved n;

// Slots globais dos originais (mesmo contrato do g_origHurtSlot: a engine de
// hook pode reescrevê-los; sempre ler atomicamente).
void* g_origNewNpcSlot = nullptr;
void* g_origSetDefaultsSlot = nullptr;
void* g_origAiSlot = nullptr;

template <typename Fn>
Fn LoadOrig(void* const& slot) {
    return reinterpret_cast<Fn>(__atomic_load_n(&slot, __ATOMIC_ACQUIRE));
}

// Callbacks publicados como cópia imutável. As antigas não são liberadas:
// um hook pode estar no meio de uma chamada na thread do jogo.
std::atomic<const TLModNpcCallbacks*> g_callbacks{nullptr};

bool Resolved() { return n.klass && n.mainNpcField; }

Il2CppArray* MainNpcArray() {
    Il2CppArray* arr = nullptr;
    api.il2cpp_field_static_get_value(n.mainNpcField, &arr);
    return arr;
}

// whoAmI é o caminho rápido, mas pode não estar preenchido ainda (dentro de
// NewNPC o SetDefaults roda antes de whoAmI = índice). Confere em Main.npc e,
// se não bater, procura o objeto no array.
int32_t IndexOf(Il2CppObject* npc) {
    if (!npc || !Resolved()) return -1;
    int32_t w = FieldAt<int32_t>(npc, n.whoAmI);
    if (w >= 0 && w < terraria::kMaxNPCs && TLMod_GetNpcByIndex(w) == npc) return w;
    return TLMod_FindNpcIndex(npc);
}

}  // namespace

// ===========================================================================
// Resolução e instalação (chamadas por TLMod_Init)
// ===========================================================================
namespace bridge {

bool ResolveNpc() {
    n.klass = api.il2cpp_class_from_name(Image(), terraria::kNamespace, terraria::kNpcClass);
    if (!n.klass) {
        LOGW("Terraria.NPC não encontrada; hooks de NPC desativados");
        return false;
    }
    n.whoAmI = FieldOffset(n.klass, "whoAmI", terraria::npc_off::whoAmI);
    n.active = FieldOffset(n.klass, "active", terraria::npc_off::active);
    n.position = FieldOffset(n.klass, "position", terraria::npc_off::position);
    n.velocity = FieldOffset(n.klass, "velocity", terraria::npc_off::velocity);
    n.type = FieldOffset(n.klass, "type", terraria::npc_off::type);
    n.ai = FieldOffset(n.klass, "ai", terraria::npc_off::ai);
    n.target = FieldOffset(n.klass, "target", terraria::npc_off::target);
    n.damage = FieldOffset(n.klass, "damage", terraria::npc_off::damage);
    n.defense = FieldOffset(n.klass, "defense", terraria::npc_off::defense);
    n.life = FieldOffset(n.klass, "life", terraria::npc_off::life);
    n.lifeMax = FieldOffset(n.klass, "lifeMax", terraria::npc_off::lifeMax);
    n.width = FieldOffset(n.klass, "width", terraria::npc_off::width);
    n.height = FieldOffset(n.klass, "height", terraria::npc_off::height);
    n.friendly = FieldOffset(n.klass, "friendly", terraria::npc_off::friendly);
    n.townNPC = FieldOffset(n.klass, "townNPC", terraria::npc_off::townNPC);
    n.dontTakeDamage = FieldOffset(n.klass, "dontTakeDamage", terraria::npc_off::dontTakeDamage);
    n.strike = api.il2cpp_class_get_method_from_name(n.klass, "StrikeNPCNoInteraction",
                                                     terraria::argc::NPC_StrikeNPCNoInteraction);

    // static NPC[] Main.npc (static_fields+0x5B0 no dump)
    n.mainNpcField = api.il2cpp_class_get_field_from_name(MainClass(), "npc");

    n.newNpc = api.il2cpp_class_get_method_from_name(n.klass, "NewNPC", terraria::argc::NPC_NewNPC);
    n.setDefaults = api.il2cpp_class_get_method_from_name(n.klass, "SetDefaults",
                                                          terraria::argc::NPC_SetDefaults);
    n.aiMethod = api.il2cpp_class_get_method_from_name(n.klass, "AI", terraria::argc::NPC_AI);

    if (!n.mainNpcField || !n.newNpc || !n.setDefaults || !n.aiMethod) {
        LOGW("NPC: faltou resolver Main.npc=%p NewNPC=%p SetDefaults=%p AI=%p; hooks de NPC desativados",
             static_cast<void*>(n.mainNpcField), static_cast<const void*>(n.newNpc),
             static_cast<const void*>(n.setDefaults), static_cast<const void*>(n.aiMethod));
        n.klass = nullptr;
        return false;
    }
    LOGI("Terraria.NPC=%p, type @0x%zx, life @0x%zx", static_cast<void*>(n.klass), n.type, n.life);
    return true;
}

void InstallNpcHooks() {
    InstallHook("NPC.SetDefaults", n.setDefaults, terraria::rva::NPC_SetDefaults,
                reinterpret_cast<void*>(&TLMod_Hooked_NpcSetDefaults), &g_origSetDefaultsSlot);
    InstallHook("NPC.AI", n.aiMethod, terraria::rva::NPC_AI, reinterpret_cast<void*>(&TLMod_Hooked_NpcAI),
                &g_origAiSlot);
    InstallHook("NPC.NewNPC", n.newNpc, terraria::rva::NPC_NewNPC,
                reinterpret_cast<void*>(&TLMod_Hooked_NpcNewNPC), &g_origNewNpcSlot);
}

}  // namespace bridge

// ===========================================================================
// API pública
// ===========================================================================
extern "C" {

void TLMod_SetNpcCallbacks(const TLModNpcCallbacks* callbacks) {
    g_callbacks.store(callbacks ? new TLModNpcCallbacks(*callbacks) : nullptr, std::memory_order_release);
}

Il2CppObject* TLMod_GetNpcByIndex(int32_t index) {
    if (!Resolved() || index < 0) return nullptr;
    Il2CppArray* arr = MainNpcArray();
    if (!arr || static_cast<uint32_t>(index) >= api.il2cpp_array_length(arr)) return nullptr;
    return ArrayData<Il2CppObject*>(arr)[index];
}

int32_t TLMod_FindNpcIndex(Il2CppObject* npc) {
    if (!npc || !Resolved()) return -1;
    Il2CppArray* arr = MainNpcArray();
    if (!arr) return -1;
    uint32_t len = api.il2cpp_array_length(arr);
    Il2CppObject** data = ArrayData<Il2CppObject*>(arr);
    for (uint32_t i = 0; i < len; ++i)
        if (data[i] == npc) return static_cast<int32_t>(i);
    return -1;
}

bool TLMod_ReadNpc(Il2CppObject* npc, TLModNpcFields* out) {
    if (!npc || !out || !Resolved()) return false;
    out->index = FieldAt<int32_t>(npc, n.whoAmI);
    out->active = FieldAt<bool>(npc, n.active);
    out->type = FieldAt<int32_t>(npc, n.type);
    out->x = FieldAt<float>(npc, n.position);
    out->y = FieldAt<float>(npc, n.position + 4);
    out->vx = FieldAt<float>(npc, n.velocity);
    out->vy = FieldAt<float>(npc, n.velocity + 4);
    out->life = FieldAt<int32_t>(npc, n.life);
    out->lifeMax = FieldAt<int32_t>(npc, n.lifeMax);
    out->damage = FieldAt<int32_t>(npc, n.damage);
    out->defense = FieldAt<int32_t>(npc, n.defense);
    out->target = FieldAt<int32_t>(npc, n.target);
    Il2CppArray* ai = FieldAt<Il2CppArray*>(npc, n.ai);
    uint32_t len = ai ? api.il2cpp_array_length(ai) : 0;
    for (uint32_t i = 0; i < 4; ++i) out->ai[i] = i < len ? ArrayData<float>(ai)[i] : 0.0f;
    return true;
}

bool TLMod_WriteNpc(Il2CppObject* npc, const TLModNpcFields* in) {
    if (!npc || !in || !Resolved()) return false;
    FieldAt<float>(npc, n.position) = in->x;
    FieldAt<float>(npc, n.position + 4) = in->y;
    FieldAt<float>(npc, n.velocity) = in->vx;
    FieldAt<float>(npc, n.velocity + 4) = in->vy;
    FieldAt<int32_t>(npc, n.life) = in->life;
    FieldAt<int32_t>(npc, n.lifeMax) = in->lifeMax;
    FieldAt<int32_t>(npc, n.damage) = in->damage;
    FieldAt<int32_t>(npc, n.defense) = in->defense;
    FieldAt<int32_t>(npc, n.target) = in->target;
    Il2CppArray* ai = FieldAt<Il2CppArray*>(npc, n.ai);
    uint32_t len = ai ? api.il2cpp_array_length(ai) : 0;
    for (uint32_t i = 0; i < 4 && i < len; ++i) ArrayData<float>(ai)[i] = in->ai[i];
    return true;
}

int32_t TLMod_SpawnNpc(int32_t x, int32_t y, int32_t type) {
    if (!Resolved()) return terraria::kMaxNPCs;
    // Tipos-valor vão por ponteiro; tipos-referência (IEntitySource) vão como o
    // próprio objeto: nulo aqui. TODO: confirmar no jogo que NewNPC aceita
    // source nulo nesta build (no PC 1.4.4 é só a origem do spawn).
    Il2CppObject* source = nullptr;
    int32_t start = 0, target = 255;
    float zero = 0.0f;
    void* params[] = {source, &x, &y, &type, &start, &zero, &zero, &zero, &zero, &target};
    Il2CppException* exc = nullptr;
    Il2CppObject* boxed = api.il2cpp_runtime_invoke(n.newNpc, nullptr, params, &exc);
    if (exc || !boxed) {
        if (exc) bridge::LogException("NPC.NewNPC", exc);
        return terraria::kMaxNPCs;
    }
    return *static_cast<int32_t*>(api.il2cpp_object_unbox(boxed));
}

int32_t TLMod_KillHostileNpcs(float x, float y, float radius) {
    if (!Resolved() || !n.strike) return 0;
    int32_t killed = 0;
    const float r2 = radius * radius;
    for (int32_t i = 0; i < terraria::kMaxNPCs; ++i) {
        Il2CppObject* npc = TLMod_GetNpcByIndex(i);
        if (!npc || !FieldAt<bool>(npc, n.active)) continue;
        if (FieldAt<bool>(npc, n.friendly) || FieldAt<bool>(npc, n.townNPC) || FieldAt<bool>(npc, n.dontTakeDamage))
            continue;
        const int32_t lifeMax = FieldAt<int32_t>(npc, n.lifeMax);
        if (lifeMax <= 5) continue;  // bichinhos (coelho, pássaro...): têm 5 de vida
        float cx = FieldAt<float>(npc, n.position) + FieldAt<int32_t>(npc, n.width) * 0.5f;
        float cy = FieldAt<float>(npc, n.position + 4) + FieldAt<int32_t>(npc, n.height) * 0.5f;
        if ((cx - x) * (cx - x) + (cy - y) * (cy - y) > r2) continue;
        // Dano maior que qualquer vida + defesa; o jogo cuida de morte, loot e
        // partes (minhocas dividem a vida pelo realLife).
        int32_t damage = lifeMax > 100000000 ? 2000000000 : lifeMax * 10 + 1000000;
        float knockback = 0.0f;
        int32_t dir = 0;
        bool crit = false, noEffect = false, fromNet = false;
        void* params[] = {&damage, &knockback, &dir, &crit, &noEffect, &fromNet};
        Il2CppException* exc = nullptr;
        api.il2cpp_runtime_invoke(n.strike, npc, params, &exc);
        if (exc) {
            bridge::LogException("NPC.StrikeNPCNoInteraction", exc);
            continue;
        }
        if (!FieldAt<bool>(npc, n.active) || FieldAt<int32_t>(npc, n.life) <= 0) ++killed;
    }
    return killed;
}

// ---------------------------------------------------------------------------
// Handlers dos hooks. Rodam na thread do jogo.
// ---------------------------------------------------------------------------

// NPC.NewNPC (RVA 0xEE4630). Ponto de REGISTRO DE SPAWN: todo NPC criado no
// mundo passa por aqui (spawn natural via NPC.SpawnNPC, invocações, itens,
// estátuas...), exceto os que chegam pela rede no cliente.
int32_t TLMod_Hooked_NpcNewNPC(Il2CppObject* source, int32_t x, int32_t y, int32_t type,
                                int32_t start, float ai0, float ai1, float ai2, float ai3,
                                int32_t target, const MethodInfo* method) {
    auto orig = LoadOrig<NpcNewNpcFn>(g_origNewNpcSlot);
    if (!orig) return terraria::kMaxNPCs;
    const TLModNpcCallbacks* cb = g_callbacks.load(std::memory_order_acquire);

    // Pré: um ID de mod vira o tipo "casca" original antes de o jogo tocar nele.
    int32_t spawnType = (cb && cb->remap_spawn_type) ? cb->remap_spawn_type(type) : type;

    int32_t index = orig(source, x, y, spawnType, start, ai0, ai1, ai2, ai3, target, method);

    // Pós: o slot já foi preenchido (SetDefaults rodou lá dentro).
    if (cb && cb->on_spawned) {
        bool ok = index >= 0 && index < terraria::kMaxNPCs;
        cb->on_spawned(ok ? TLMod_GetNpcByIndex(index) : nullptr, index, type, spawnType);
    }
    return index;
}

// NPC.SetDefaults (RVA 0xDAF1BC). O NPC vai virar `type`: por spawn (dentro
// de NewNPC), por Transform (fases de boss, slimes), ou pela rede. Quem marca
// slots como "NPC de mod" precisa desmarcar aqui; NewNPC remarca no pós.
void TLMod_Hooked_NpcSetDefaults(Il2CppObject* self, int32_t type, NPCSpawnParams spawnparams,
                                  const MethodInfo* method) {
    auto orig = LoadOrig<NpcSetDefaultsFn>(g_origSetDefaultsSlot);
    if (!orig) return;
    const TLModNpcCallbacks* cb = g_callbacks.load(std::memory_order_acquire);
    if (cb && cb->on_set_defaults) cb->on_set_defaults(self, IndexOf(self), type);
    orig(self, type, spawnparams, method);
}

// NPC.AI (RVA 0xDCE54C). Chamado só por NPC.UpdateNPC (0xF4A2E8), que
// Main.DoUpdateInWorld (0xCBBDF0) chama para i = 0..199 a cada tick (60/s).
// PONTO DE EXTENSÃO POR FRAME: o comportamento do mod roda aqui, antes da AI
// original, e pode substituí-la. Movimento, gravidade e colisão continuam em
// UpdateNPC depois do AI, então o mod só precisa ajustar velocity/ai[].
void TLMod_Hooked_NpcAI(Il2CppObject* self, const MethodInfo* method) {
    auto orig = LoadOrig<NpcAiFn>(g_origAiSlot);
    if (!orig) return;
    const TLModNpcCallbacks* cb = g_callbacks.load(std::memory_order_acquire);
    if (cb && cb->on_ai && cb->on_ai(self, IndexOf(self))) return;  // mod substituiu a AI
    orig(self, method);
}

}  // extern "C"
