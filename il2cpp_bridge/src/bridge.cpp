#include "bridge.h"

#include <pthread.h>
#include <time.h>

#include <atomic>
#include <cstring>

#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

namespace {

// ---------------------------------------------------------------------------
// Estado resolvido em runtime
// ---------------------------------------------------------------------------
struct Resolved {
    Il2CppDomain* domain = nullptr;
    const Il2CppImage* image = nullptr;  // Assembly-CSharp.dll
    Il2CppClass* playerClass = nullptr;  // Terraria.Player
    Il2CppClass* mainClass = nullptr;    // Terraria.Main

    // Offset de Player.statLife. Começa no valor do dump (0x704) e é
    // sobrescrito pelo il2cpp_field_get_offset quando a classe é resolvida.
    size_t statLifeOffset = terraria::player_off::statLife;
    // Entity.whoAmI (índice do player em Main.player). Dump: 0x10.
    size_t whoAmIOffset = terraria::player_off::whoAmI;

    FieldInfo* mainPlayerField = nullptr;       // static Player[] Main.player
    const MethodInfo* mainGetMyPlayer = nullptr;  // static int Main.get_myPlayer()
    const MethodInfo* playerHurt = nullptr;     // Player.Hurt (8 args)
    const MethodInfo* playerHeal = nullptr;     // Player.Heal (1 arg)
};

Resolved g;
const char* g_libName = "libil2cpp.so";
TLModHookInstaller g_installer = nullptr;
std::atomic<TLModHurtCallback> g_hurtCallback{nullptr};
// Slot do endereço "original" de Player.Hurt. Precisa viver o processo
// inteiro: o ShadowHook (modo multi) grava aqui o próximo elo da cadeia de
// proxies e pode reescrevê-lo depois, quando outro hook no mesmo alvo sai.
// Por isso é lido atomicamente a cada chamada, nunca copiado.
void* g_origHurtSlot = nullptr;

PlayerHurtFn LoadOrigHurt() {
    return reinterpret_cast<PlayerHurtFn>(__atomic_load_n(&g_origHurtSlot, __ATOMIC_ACQUIRE));
}
std::atomic<bool> g_ready{false};

// Evita reentrada se o próprio callback do mod provocar outro Hurt.
thread_local int t_hurtDepth = 0;

void SleepMs(int ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

using bridge::LogException;

// Espera `pred` virar true, consultando a cada 100 ms, até timeoutMs.
template <typename Pred>
bool WaitFor(Pred pred, int& remainingMs) {
    while (!pred()) {
        if (remainingMs <= 0) return false;
        SleepMs(100);
        remainingMs -= 100;
    }
    return true;
}

const Il2CppImage* FindImage(const char* imageName) {
    size_t count = 0;
    const Il2CppAssembly** assemblies = api.il2cpp_domain_get_assemblies(g.domain, &count);
    for (size_t i = 0; i < count; ++i) {
        const Il2CppImage* img = api.il2cpp_assembly_get_image(assemblies[i]);
        const char* name = img ? api.il2cpp_image_get_name(img) : nullptr;
        if (name && std::strcmp(name, imageName) == 0) return img;
    }
    return nullptr;
}

bool ResolveTerraria() {
    g.image = FindImage(terraria::kAssembly);
    if (!g.image) return false;

    g.playerClass = api.il2cpp_class_from_name(g.image, terraria::kNamespace, terraria::kPlayerClass);
    g.mainClass = api.il2cpp_class_from_name(g.image, terraria::kNamespace, terraria::kMainClass);
    if (!g.playerClass || !g.mainClass) return false;

    // --- Player.statLife: offset resolvido pela API ------------------------
    // Esperado nesta build: 0x704 (terraria_offsets.h / player_fields.md).
    g.statLifeOffset = bridge::FieldOffset(g.playerClass, "statLife", terraria::player_off::statLife);
    // Entity.whoAmI: a busca inclui as classes-base, então funciona a partir de Player.
    g.whoAmIOffset = bridge::FieldOffset(g.playerClass, "whoAmI", terraria::player_off::whoAmI);

    // --- Main.player (static Player[]) e Main.get_myPlayer -----------------
    // Main.player fica em static_fields+0x678 nesta build; lido via
    // il2cpp_field_static_get_value para não depender do layout.
    g.mainPlayerField = api.il2cpp_class_get_field_from_name(g.mainClass, "player");
    g.mainGetMyPlayer = api.il2cpp_class_get_method_from_name(g.mainClass, "get_myPlayer",
                                                              terraria::argc::Main_get_myPlayer);

    // --- Métodos de Player --------------------------------------------------
    g.playerHurt = api.il2cpp_class_get_method_from_name(g.playerClass, "Hurt",
                                                         terraria::argc::Player_Hurt);
    g.playerHeal = api.il2cpp_class_get_method_from_name(g.playerClass, "Heal",
                                                         terraria::argc::Player_Heal);
    if (!g.playerHurt) {
        LOGE("Player.Hurt(8) não encontrado");
        return false;
    }
    return true;
}

bool InstallHurtHook() {
    return bridge::InstallHook("Player.Hurt", g.playerHurt, terraria::rva::Player_Hurt,
                               reinterpret_cast<void*>(&TLMod_Hooked_PlayerHurt), &g_origHurtSlot);
}

void* InitThreadMain(void*) {
    TLMod_Init(60000);
    return nullptr;
}

}  // namespace

// ===========================================================================
// Interno (bridge_internal.h)
// ===========================================================================
namespace bridge {

bool Ready() { return g_ready.load(); }
Il2CppDomain* Domain() { return g.domain; }
const Il2CppImage* Image() { return g.image; }
Il2CppClass* MainClass() { return g.mainClass; }
Il2CppClass* PlayerClass() { return g.playerClass; }

void LogException(const char* where, Il2CppException* exc) {
    char buf[512] = {0};
    if (api.il2cpp_format_exception) api.il2cpp_format_exception(exc, buf, sizeof(buf));
    LOGE("exceção em %s: %s", where, buf);
}

size_t FieldOffset(Il2CppClass* klass, const char* name, size_t fallback) {
    FieldInfo* f = klass ? api.il2cpp_class_get_field_from_name(klass, name) : nullptr;
    if (!f) {
        LOGW("campo %s não encontrado; usando offset do dump 0x%zx", name, fallback);
        return fallback;
    }
    size_t off = api.il2cpp_field_get_offset(f);
    if (off != fallback) LOGW("%s em 0x%zx (dump dizia 0x%zx): build diferente do dump", name, off, fallback);
    return off;
}

bool InstallHook(const char* name, const MethodInfo* method, uintptr_t dumpRva, void* replacement,
                 void** slot) {
    if (!method) {
        LOGE("%s: método não resolvido; hook não instalado", name);
        return false;
    }
    // Checagem cruzada: methodPointer deve ser base + RVA do dump. Se divergir,
    // a build é outra; seguimos com o ponteiro vindo do runtime.
    uintptr_t fromApi = reinterpret_cast<uintptr_t>(method->methodPointer);
    uintptr_t fromDump = g_base + dumpRva;
    if (fromApi != fromDump) {
        LOGW("%s: runtime=%p, base+RVA=%p (RVA 0x%lx). Usando o ponteiro do runtime.", name,
             reinterpret_cast<void*>(fromApi), reinterpret_cast<void*>(fromDump),
             static_cast<unsigned long>(dumpRva));
    } else {
        LOGI("%s confere com o dump: base+0x%lx", name, static_cast<unsigned long>(dumpRva));
    }
    if (!g_installer) {
        LOGW("nenhum TLModHookInstaller configurado; hook de %s não instalado", name);
        return false;
    }
    void* target = reinterpret_cast<void*>(method->methodPointer);
    if (!g_installer(target, replacement, slot) || !__atomic_load_n(slot, __ATOMIC_ACQUIRE)) {
        LOGE("falha ao instalar hook em %s (%p)", name, target);
        return false;
    }
    LOGI("hook em %s instalado (%p)", name, target);
    return true;
}

}  // namespace bridge

// ===========================================================================
// API pública
// ===========================================================================
extern "C" {

void TLMod_SetLibraryName(const char* name) { g_libName = name; }
void TLMod_SetHookInstaller(TLModHookInstaller installer) { g_installer = installer; }
void TLMod_SetHurtCallback(TLModHurtCallback cb) { g_hurtCallback.store(cb); }

bool TLMod_Init(int timeoutMs) {
    if (g_ready.load()) return true;
    int remaining = timeoutMs;

    // 1) A libil2cpp.so precisa estar mapeada pelo jogo.
    if (!WaitFor([] { return LoadApi(g_libName); }, remaining)) {
        LOGE("timeout esperando %s", g_libName);
        return false;
    }
    LOGI("%s em %p", g_libName, reinterpret_cast<void*>(g_base));

    // 2) O runtime precisa ter passado por il2cpp_init. il2cpp_get_corlib()
    //    só lê il2cpp_defaults.corlib, que fica nulo até Runtime::Init: é seguro
    //    consultar em loop. (Chamar il2cpp_domain_get antes disso alocaria no GC
    //    ainda não inicializado.) Alternativa mais precisa: hookar il2cpp_init.
    if (!WaitFor([] { return api.il2cpp_get_corlib() != nullptr; }, remaining)) {
        LOGE("timeout esperando il2cpp_init");
        return false;
    }
    g.domain = api.il2cpp_domain_get();

    // 3) Esta thread não é do jogo: anexá-la ao IL2CPP antes de usar a API
    //    (runtime_invoke, GC). Só desanexa se fomos nós que anexamos.
    Il2CppThread* attached = nullptr;
    if (!api.il2cpp_thread_current()) attached = api.il2cpp_thread_attach(g.domain);

    // 4) Assembly-CSharp e Terraria.Player podem aparecer um pouco depois.
    bool ok = WaitFor([] { return ResolveTerraria(); }, remaining);
    if (ok) {
        LOGI("Terraria.Player=%p, statLife @0x%zx", static_cast<void*>(g.playerClass),
             g.statLifeOffset);
        InstallHurtHook();
        // NPCs são opcionais: se algo não resolver, o resto da ponte segue.
        if (bridge::ResolveNpc()) bridge::InstallNpcHooks();
        if (bridge::ResolveWorldState()) bridge::InstallWorldStateHooks();
        if (bridge::ResolveSaves()) bridge::InstallSaveHooks();
        g_ready.store(true);
    } else {
        LOGE("timeout resolvendo Terraria.Player em %s", terraria::kAssembly);
    }

    if (attached) api.il2cpp_thread_detach(attached);
    return ok;
}

void TLMod_StartInitThread(void) {
    pthread_t th;
    if (pthread_create(&th, nullptr, &InitThreadMain, nullptr) == 0) pthread_detach(th);
}

int32_t TLMod_GetStatLife(Il2CppObject* player) {
    if (!player) return -1;
    return FieldAt<int32_t>(player, g.statLifeOffset);
}

Il2CppObject* TLMod_GetPlayerByIndex(int32_t index) {
    if (!g_ready.load() || !g.mainPlayerField || index < 0) return nullptr;
    // static Player[] Main.player
    Il2CppArray* players = nullptr;
    api.il2cpp_field_static_get_value(g.mainPlayerField, &players);
    if (!players || static_cast<uint32_t>(index) >= api.il2cpp_array_length(players)) {
        return nullptr;
    }
    return ArrayData<Il2CppObject*>(players)[index];
}

int32_t TLMod_GetPlayerIndex(Il2CppObject* player) {
    if (!player) return -1;
    return FieldAt<int32_t>(player, g.whoAmIOffset);
}

bool TLMod_WaitReady(int timeoutMs) {
    int remaining = timeoutMs;
    return WaitFor([] { return g_ready.load(); }, remaining);
}

bool TLMod_AttachCurrentThread(void) {
    if (!g_ready.load()) return false;
    if (!api.il2cpp_thread_current()) api.il2cpp_thread_attach(g.domain);
    return true;
}

Il2CppObject* TLMod_GetLocalPlayer(void) {
    if (!g_ready.load() || !g.mainGetMyPlayer) return nullptr;

    // static int Main.myPlayer { get; } -> runtime_invoke devolve int "boxed".
    Il2CppException* exc = nullptr;
    Il2CppObject* boxed = api.il2cpp_runtime_invoke(g.mainGetMyPlayer, nullptr, nullptr, &exc);
    if (exc || !boxed) {
        if (exc) LogException("Main.get_myPlayer", exc);
        return nullptr;
    }
    int32_t myPlayer = *static_cast<int32_t*>(api.il2cpp_object_unbox(boxed));
    return TLMod_GetPlayerByIndex(myPlayer);
}

bool TLMod_Heal(Il2CppObject* player, int32_t amount) {
    if (!g_ready.load() || !player || !g.playerHeal) return false;
    // Tipos-valor vão como ponteiro para o valor em params[].
    void* params[] = {&amount};
    Il2CppException* exc = nullptr;
    api.il2cpp_runtime_invoke(g.playerHeal, player, params, &exc);
    if (exc) {
        LogException("Player.Heal", exc);
        return false;
    }
    return true;
}

PlayerHurtFn TLMod_OriginalPlayerHurt(void) { return LoadOrigHurt(); }

// ---------------------------------------------------------------------------
// Handler do hook de Player.Hurt (RVA 0xFA15B8 nesta build).
// Roda na thread do jogo, dentro de Main.DoUpdateInWorld -> Player.Update /
// NPC.UpdateNPC / Projectile.Update, então já está anexada ao IL2CPP.
// ---------------------------------------------------------------------------
double TLMod_Hooked_PlayerHurt(Il2CppObject* self, Il2CppObject* damageSource, int32_t damage,
                                int32_t hitDirection, bool pvp, bool quiet, bool crit,
                                int32_t cooldownCounter, bool dodgeable, const MethodInfo* method) {
    PlayerHurtFn orig = LoadOrigHurt();
    TLModHurtCallback cb = g_hurtCallback.load();
    if (!orig) return 0.0;  // não deveria acontecer: o hook só existe com trampoline
    if (!cb || t_hurtDepth > 0) {
        return orig(self, damageSource, damage, hitDirection, pvp, quiet, crit, cooldownCounter,
                    dodgeable, method);
    }

    ++t_hurtDepth;
    TLModHurtEvent ev{};
    ev.player = self;
    ev.damage = damage;
    ev.hitDirection = hitDirection;
    ev.pvp = pvp;
    ev.crit = crit;
    ev.lifeBefore = TLMod_GetStatLife(self);

    double result = 0.0;
    if (cb(&ev, /*after=*/false)) {
        result = orig(self, damageSource, ev.damage, hitDirection, pvp, quiet, crit,
                      cooldownCounter, dodgeable, method);
        ev.result = result;
        ev.lifeAfter = TLMod_GetStatLife(self);
        cb(&ev, /*after=*/true);
    }
    // Golpe cancelado: retorna 0.0, que o jogo trata como "sem dano".
    --t_hurtDepth;
    return result;
}

}  // extern "C"
