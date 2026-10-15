// libil2cpp.so FALSA para teste no host (x86_64/arm64 Linux).
//
// Exporta os mesmos símbolos que a ponte resolve via dlsym e simula
// Terraria.Player / Terraria.Main com os offsets reais do dump
// (statLife @0x704, statDefense @0x6F8 ...). Não tem nada da Unity dentro.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {

typedef void (*Il2CppMethodPointer)();

struct Il2CppObject {
    void* klass;
    void* monitor;
};
typedef Il2CppObject Il2CppException;

struct MethodInfo {
    Il2CppMethodPointer methodPointer;  // mesmo primeiro campo do MethodInfo real
    const char* name;
    int argc;
};

struct FieldInfo {
    const char* name;
    size_t offset;
    void* staticAddr;  // não nulo para campos estáticos
};

struct Il2CppClass {
    const char* ns;
    const char* name;
    std::vector<FieldInfo*> fields;
    std::vector<MethodInfo*> methods;
};

struct Il2CppImage {
    const char* name;
    std::vector<Il2CppClass*> classes;
};
struct Il2CppAssembly {
    Il2CppImage* image;
};
struct Il2CppDomain {
    int dummy;
};
struct Il2CppThread {
    int dummy;
};

// Il2CppArray em 64 bits: klass, monitor, bounds, max_length, depois os dados (0x20).
struct Il2CppArray {
    void* klass;
    void* monitor;
    void* bounds;
    uintptr_t max_length;
};

}  // extern "C"

namespace {

constexpr size_t kStatDefense = 0x6F8;
constexpr size_t kStatLifeMax2 = 0x700;
constexpr size_t kStatLife = 0x704;
constexpr size_t kPlayerSize = 0xD80;

bool g_initialized = false;
Il2CppImage g_corlib{"mscorlib.dll", {}};
Il2CppImage g_csharp{"Assembly-CSharp.dll", {}};
Il2CppAssembly g_asmCorlib{&g_corlib};
Il2CppAssembly g_asmCsharp{&g_csharp};
const Il2CppAssembly* g_assemblies[] = {&g_asmCorlib, &g_asmCsharp};
Il2CppDomain g_domain{};
Il2CppThread g_thread{};
thread_local Il2CppThread* t_current = nullptr;

Il2CppClass g_player{"Terraria", "Player", {}, {}};
Il2CppClass g_main{"Terraria", "Main", {}, {}};
FieldInfo g_fStatLife{"statLife", kStatLife, nullptr};
FieldInfo g_fWhoAmI{"whoAmI", 0x10, nullptr};  // de Entity; o IL2CPP real busca nas classes-base

alignas(16) uint8_t g_localPlayer[kPlayerSize];
alignas(16) uint8_t g_playersArray[sizeof(Il2CppArray) + 255 * sizeof(void*)];
Il2CppArray* g_mainPlayer = reinterpret_cast<Il2CppArray*>(g_playersArray);
FieldInfo g_fMainPlayer{"player", 0x678, &g_mainPlayer};
int32_t g_myPlayer = 0;

struct BoxedInt {
    Il2CppObject header;
    int32_t value;
};
BoxedInt g_box{};

int32_t& I32(void* obj, size_t off) {
    return *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(obj) + off);
}

// "Código do jogo" para Player.Hurt: mesma ABI que o IL2CPP gera.
double MockPlayerHurt(Il2CppObject* self, Il2CppObject*, int32_t damage, int32_t, bool, bool,
                      bool, int32_t, bool, const MethodInfo*) {
    int32_t eff = std::max(1, damage - I32(self, kStatDefense) / 2);
    I32(self, kStatLife) -= eff;
    return eff;
}
void MockPlayerHeal(Il2CppObject* self, int32_t amount, const MethodInfo*) {
    I32(self, kStatLife) = std::min(I32(self, kStatLife) + amount, I32(self, kStatLifeMax2));
}

MethodInfo g_mHurt{reinterpret_cast<Il2CppMethodPointer>(&MockPlayerHurt), "Hurt", 8};
MethodInfo g_mHeal{reinterpret_cast<Il2CppMethodPointer>(&MockPlayerHeal), "Heal", 1};
MethodInfo g_mGetMyPlayer{nullptr, "get_myPlayer", 0};

// Ponto de chamada do "jogo". Um inline hook real reescreve o início de
// MockPlayerHurt; aqui o "patch" troca este ponteiro (mock_patch_function).
using HurtFn = decltype(&MockPlayerHurt);
HurtFn g_hurtEntry = &MockPlayerHurt;

// ---- NPCs ---------------------------------------------------------------------
// Offsets do dump (terraria_offsets.h, npc_off).
constexpr size_t kNpcWhoAmI = 0x10, kNpcActive = 0x14, kNpcPos = 0x20, kNpcVel = 0x28, kNpcType = 0x154,
                 kNpcAi = 0x158, kNpcTarget = 0x178, kNpcDamage = 0x17C, kNpcDefense = 0x180,
                 kNpcLife = 0x1A0, kNpcLifeMax = 0x1A4, kNpcSize = 0x200;
constexpr int kMaxNpcs = 200;
constexpr int32_t kNpcVanillaCount = 688;

struct FloatArray4 {
    Il2CppArray header;
    float data[4];
};
alignas(16) uint8_t g_npcs[kMaxNpcs][kNpcSize];
FloatArray4 g_npcAi[kMaxNpcs];
alignas(16) uint8_t g_npcArray[sizeof(Il2CppArray) + kMaxNpcs * sizeof(void*)];
Il2CppArray* g_mainNpc = reinterpret_cast<Il2CppArray*>(g_npcArray);

template <typename T>
T& At(void* obj, size_t off) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(obj) + off);
}

Il2CppClass g_npc{"Terraria", "NPC", {}, {}};
FieldInfo g_fNpc[] = {{"whoAmI", kNpcWhoAmI, nullptr},   {"active", kNpcActive, nullptr},
                      {"position", kNpcPos, nullptr},    {"velocity", kNpcVel, nullptr},
                      {"type", kNpcType, nullptr},       {"ai", kNpcAi, nullptr},
                      {"target", kNpcTarget, nullptr},   {"damage", kNpcDamage, nullptr},
                      {"defense", kNpcDefense, nullptr}, {"life", kNpcLife, nullptr},
                      {"lifeMax", kNpcLifeMax, nullptr}};
FieldInfo g_fMainNpc{"npc", 0x5B0, &g_mainNpc};

struct SpawnParams {  // NPCSpawnParams: 32 bytes, por valor
    uint64_t a, b;
    void* c;
    uint64_t d;
};

// "Código do jogo" para SetDefaults: o jogo real indexa arrays por tipo, então
// um tipo fora da faixa original estoura. O mock aborta para o teste provar
// que o ID de mod nunca chega aqui.
void MockNpcSetDefaults(Il2CppObject* self, int32_t type, SpawnParams, const MethodInfo*) {
    if (type >= kNpcVanillaCount || type < -65) std::abort();
    At<int32_t>(self, kNpcType) = type;
    At<int32_t>(self, kNpcLife) = At<int32_t>(self, kNpcLifeMax) = 100 + type;
    At<int32_t>(self, kNpcDamage) = 10;
    At<int32_t>(self, kNpcDefense) = 5;
    auto* ai = At<FloatArray4*>(self, kNpcAi);
    for (float& f : ai->data) f = 0.0f;
}
using SetDefaultsFn = decltype(&MockNpcSetDefaults);
SetDefaultsFn g_setDefaultsEntry = &MockNpcSetDefaults;
MethodInfo g_mSetDefaults{reinterpret_cast<Il2CppMethodPointer>(&MockNpcSetDefaults), "SetDefaults", 2};

// AI original de exemplo: conta ticks em ai[0] e anda para a direita.
void MockNpcAI(Il2CppObject* self, const MethodInfo*) {
    At<FloatArray4*>(self, kNpcAi)->data[0] += 1.0f;
    At<float>(self, kNpcVel) = 1.0f;
}
using AiFn = decltype(&MockNpcAI);
AiFn g_aiEntry = &MockNpcAI;
MethodInfo g_mAI{reinterpret_cast<Il2CppMethodPointer>(&MockNpcAI), "AI", 0};

// NewNPC como o do jogo: escolhe slot, chama SetDefaults ANTES de preencher
// whoAmI (o objeto ainda tem whoAmI = 0), depois posição/active/whoAmI.
MethodInfo g_mNewNPC{nullptr, "NewNPC", 10};
int32_t MockNewNPC(Il2CppObject*, int32_t x, int32_t y, int32_t type, int32_t start, float ai0, float ai1,
                   float ai2, float ai3, int32_t target, const MethodInfo*) {
    for (int i = start; i < kMaxNpcs; ++i) {
        void* npc = g_npcs[i];
        if (At<bool>(npc, kNpcActive)) continue;
        At<int32_t>(npc, kNpcWhoAmI) = 0;  // objeto "novo"
        g_setDefaultsEntry(static_cast<Il2CppObject*>(npc), type, SpawnParams{}, &g_mSetDefaults);
        At<int32_t>(npc, kNpcWhoAmI) = i;
        At<bool>(npc, kNpcActive) = true;
        At<float>(npc, kNpcPos) = static_cast<float>(x);
        At<float>(npc, kNpcPos + 4) = static_cast<float>(y);
        At<int32_t>(npc, kNpcTarget) = target;
        float* ai = At<FloatArray4*>(npc, kNpcAi)->data;
        ai[0] = ai0, ai[1] = ai1, ai[2] = ai2, ai[3] = ai3;
        return i;
    }
    return kMaxNpcs;
}
using NewNpcFn = decltype(&MockNewNPC);
NewNpcFn g_newNpcEntry = &MockNewNPC;

}  // namespace

extern "C" {

// ---- controle do teste ------------------------------------------------------
__attribute__((visibility("default"))) void mock_il2cpp_init() {
    g_csharp.classes = {&g_player, &g_main, &g_npc};
    g_mNewNPC.methodPointer = reinterpret_cast<Il2CppMethodPointer>(&MockNewNPC);
    g_npc.fields.clear();
    for (FieldInfo& f : g_fNpc) g_npc.fields.push_back(&f);
    g_npc.methods = {&g_mNewNPC, &g_mSetDefaults, &g_mAI};
    std::memset(g_npcs, 0, sizeof(g_npcs));
    g_mainNpc->max_length = kMaxNpcs;
    for (int i = 0; i < kMaxNpcs; ++i) {
        g_npcAi[i].header.max_length = 4;
        At<FloatArray4*>(g_npcs[i], kNpcAi) = &g_npcAi[i];
        reinterpret_cast<void**>(g_mainNpc + 1)[i] = g_npcs[i];
    }
    g_player.fields = {&g_fStatLife, &g_fWhoAmI};
    g_player.methods = {&g_mHurt, &g_mHeal};
    g_main.fields = {&g_fMainPlayer, &g_fMainNpc};
    g_main.methods = {&g_mGetMyPlayer};

    std::memset(g_localPlayer, 0, sizeof(g_localPlayer));
    I32(g_localPlayer, kStatDefense) = 10;
    I32(g_localPlayer, kStatLifeMax2) = 500;
    I32(g_localPlayer, kStatLife) = 400;
    g_mainPlayer->max_length = 255;
    reinterpret_cast<void**>(g_mainPlayer + 1)[0] = g_localPlayer;
    g_initialized = true;
}
__attribute__((visibility("default"))) void* mock_local_player() { return g_localPlayer; }
__attribute__((visibility("default"))) bool mock_patch_function(void* target, void* replacement) {
    if (target == reinterpret_cast<void*>(&MockPlayerHurt)) g_hurtEntry = reinterpret_cast<HurtFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockNewNPC)) g_newNpcEntry = reinterpret_cast<NewNpcFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockNpcSetDefaults)) g_setDefaultsEntry = reinterpret_cast<SetDefaultsFn>(replacement);
    else if (target == reinterpret_cast<void*>(&MockNpcAI)) g_aiEntry = reinterpret_cast<AiFn>(replacement);
    else return false;
    return true;
}
// Simula o jogo criando um NPC (spawn natural, invocação...).
__attribute__((visibility("default"))) int32_t mock_game_new_npc(int32_t x, int32_t y, int32_t type) {
    return g_newNpcEntry(nullptr, x, y, type, 0, 0, 0, 0, 0, 255, &g_mNewNPC);
}
// Simula Main.DoUpdateInWorld -> NPC.UpdateNPC(i) -> NPC.AI() para os ativos.
__attribute__((visibility("default"))) void mock_game_update_npcs() {
    for (int i = 0; i < kMaxNpcs; ++i)
        if (At<bool>(g_npcs[i], kNpcActive)) g_aiEntry(reinterpret_cast<Il2CppObject*>(g_npcs[i]), &g_mAI);
}
// Simula NPC.Transform(newType), que chama SetDefaults num NPC já existente.
__attribute__((visibility("default"))) void mock_game_transform(int32_t index, int32_t newType) {
    g_setDefaultsEntry(reinterpret_cast<Il2CppObject*>(g_npcs[index]), newType, SpawnParams{}, &g_mSetDefaults);
}
__attribute__((visibility("default"))) void* mock_npc(int32_t index) { return g_npcs[index]; }
// Simula o jogo aplicando dano ao player (passa pelo hook se instalado).
__attribute__((visibility("default"))) double mock_game_hurt_player(void* player, int32_t dmg) {
    return g_hurtEntry(static_cast<Il2CppObject*>(player), nullptr, dmg, 1, false, false, false,
                       -1, true, &g_mHurt);
}

// ---- exports da API IL2CPP --------------------------------------------------
const Il2CppImage* il2cpp_get_corlib() { return g_initialized ? &g_corlib : nullptr; }
Il2CppDomain* il2cpp_domain_get() { return g_initialized ? &g_domain : nullptr; }
const Il2CppAssembly** il2cpp_domain_get_assemblies(const Il2CppDomain*, size_t* size) {
    *size = 2;
    return g_assemblies;
}
const Il2CppImage* il2cpp_assembly_get_image(const Il2CppAssembly* a) { return a->image; }
const char* il2cpp_image_get_name(const Il2CppImage* img) { return img->name; }
Il2CppClass* il2cpp_class_from_name(const Il2CppImage* img, const char* ns, const char* name) {
    for (Il2CppClass* k : img->classes)
        if (!std::strcmp(k->ns, ns) && !std::strcmp(k->name, name)) return k;
    return nullptr;
}
FieldInfo* il2cpp_class_get_field_from_name(Il2CppClass* k, const char* name) {
    for (FieldInfo* f : k->fields)
        if (!std::strcmp(f->name, name)) return f;
    return nullptr;
}
size_t il2cpp_field_get_offset(FieldInfo* f) { return f->offset; }
void il2cpp_field_static_get_value(FieldInfo* f, void* out) {
    std::memcpy(out, f->staticAddr, sizeof(void*));
}
const MethodInfo* il2cpp_class_get_method_from_name(Il2CppClass* k, const char* name, int argc) {
    for (MethodInfo* m : k->methods)
        if (!std::strcmp(m->name, name) && (argc < 0 || m->argc == argc)) return m;
    return nullptr;
}
Il2CppObject* il2cpp_runtime_invoke(const MethodInfo* m, void* obj, void** params,
                                    Il2CppException** exc) {
    *exc = nullptr;
    if (m == &g_mGetMyPlayer) {
        g_box.value = g_myPlayer;
        return &g_box.header;
    }
    if (m == &g_mNewNPC) {  // como o invoker real: chama o endereço (já com hook)
        g_box.value = g_newNpcEntry(static_cast<Il2CppObject*>(params[0]), *static_cast<int32_t*>(params[1]),
                                    *static_cast<int32_t*>(params[2]), *static_cast<int32_t*>(params[3]),
                                    *static_cast<int32_t*>(params[4]), *static_cast<float*>(params[5]),
                                    *static_cast<float*>(params[6]), *static_cast<float*>(params[7]),
                                    *static_cast<float*>(params[8]), *static_cast<int32_t*>(params[9]), m);
        return &g_box.header;
    }
    if (m == &g_mHeal) {
        MockPlayerHeal(static_cast<Il2CppObject*>(obj), *static_cast<int32_t*>(params[0]), m);
        return nullptr;
    }
    std::abort();
}
void* il2cpp_object_unbox(Il2CppObject* o) { return o + 1; }
uint32_t il2cpp_array_length(Il2CppArray* a) { return static_cast<uint32_t>(a->max_length); }
Il2CppThread* il2cpp_thread_attach(Il2CppDomain*) { return t_current = &g_thread; }
Il2CppThread* il2cpp_thread_current() { return t_current; }
void il2cpp_thread_detach(Il2CppThread*) { t_current = nullptr; }
void il2cpp_format_exception(const Il2CppException*, char* msg, int size) {
    std::strncpy(msg, "mock exception", size);
}

}  // extern "C"
