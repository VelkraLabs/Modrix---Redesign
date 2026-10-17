// Hook por frame (Main.DoUpdate) e leitura de estado do mundo.
//
// Por que um hook por frame: Main.gameMenu, que diz se há um mundo carregado,
// vem de LocalUserGameState.Instance, que é [ThreadStatic]. Só a thread do
// jogo enxerga a instância; em qualquer outra thread o getter lança
// NullReferenceException. A camada de UI precisa então de uma "fotografia"
// tirada na thread do jogo, que é o que o TLModFrameCallback permite.
#include <pthread.h>

#include <atomic>
#include <cstring>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

typedef void (*MainDoUpdateFn)(Il2CppObject* self, Il2CppObject* gameTime, const MethodInfo* method);

namespace {

struct WorldResolved {
    const MethodInfo* doUpdate = nullptr;   // Main.DoUpdate(GameTime)
    FieldInfo* localUserInstance = nullptr; // [ThreadStatic] LocalUserGameState.Instance
    size_t gameMenuOffset = terraria::local_user_off::gameMenu;
};
WorldResolved w;

void* g_origDoUpdateSlot = nullptr;
std::atomic<TLModFrameCallback> g_frameCallback{nullptr};
// Thread que roda Main.DoUpdate (a principal do Unity), gravada a cada frame.
std::atomic<pthread_t> g_gameThread{};
std::atomic<bool> g_gameThreadKnown{false};

}  // namespace

namespace bridge {

bool ResolveWorldState() {
    w.doUpdate = api.il2cpp_class_get_method_from_name(MainClass(), "DoUpdate", terraria::argc::Main_DoUpdate);
    // LocalUserGameState fica no namespace global.
    Il2CppClass* lugs = api.il2cpp_class_from_name(Image(), "", terraria::kLocalUserGameStateClass);
    if (lugs) {
        w.localUserInstance = api.il2cpp_class_get_field_from_name(lugs, "Instance");
        w.gameMenuOffset = FieldOffset(lugs, "gameMenu", terraria::local_user_off::gameMenu);
    }
    if (!w.doUpdate) {
        LOGW("Main.DoUpdate não encontrado; callback por frame desativado");
        return false;
    }
    if (!w.localUserInstance) LOGW("LocalUserGameState.Instance não encontrado; TLMod_IsGameMenu devolve -1");
    return true;
}

bool OnGameThread() {
    return g_gameThreadKnown.load(std::memory_order_acquire) &&
           pthread_equal(g_gameThread.load(std::memory_order_relaxed), pthread_self());
}

void InstallWorldStateHooks() {
    InstallHook("Main.DoUpdate", w.doUpdate, terraria::rva::Main_DoUpdate,
                reinterpret_cast<void*>(&TLMod_Hooked_MainDoUpdate), &g_origDoUpdateSlot);
}

}  // namespace bridge

extern "C" {

void TLMod_SetFrameCallback(TLModFrameCallback cb) { g_frameCallback.store(cb, std::memory_order_release); }

int32_t TLMod_IsGameMenu(void) {
    if (!w.localUserInstance) return -1;
    // il2cpp_field_static_get_value resolve campos ThreadStatic para a
    // thread que chama: fora da thread do jogo, devolve nulo.
    Il2CppObject* instance = nullptr;
    api.il2cpp_field_static_get_value(w.localUserInstance, &instance);
    if (!instance) return -1;
    return FieldAt<bool>(instance, w.gameMenuOffset) ? 1 : 0;
}

TLModStaticField* TLMod_FindStaticField(const char* namespaze, const char* klass, const char* field) {
    if (!bridge::Image() || !namespaze || !klass || !field) return nullptr;
    Il2CppClass* k = api.il2cpp_class_from_name(bridge::Image(), namespaze, klass);
    FieldInfo* f = k ? api.il2cpp_class_get_field_from_name(k, field) : nullptr;
    if (!f) LOGW("campo estático %s.%s.%s não encontrado", namespaze, klass, field);
    return reinterpret_cast<TLModStaticField*>(f);
}

bool TLMod_ReadStaticBool(const TLModStaticField* field, bool* out) {
    if (!field || !out) return false;
    // bool ocupa 1 byte no IL2CPP; static_get_value copia o tamanho do tipo.
    bool value = false;
    api.il2cpp_field_static_get_value(reinterpret_cast<FieldInfo*>(const_cast<TLModStaticField*>(field)),
                                      &value);
    *out = value;
    return true;
}

// Main.DoUpdate (RVA 0xCADDB8). Primeiro o callback (estado do frame
// anterior já aplicado), depois o frame do jogo.
void TLMod_Hooked_MainDoUpdate(Il2CppObject* self, Il2CppObject* gameTime, const MethodInfo* method) {
    auto orig = reinterpret_cast<MainDoUpdateFn>(__atomic_load_n(&g_origDoUpdateSlot, __ATOMIC_ACQUIRE));
    g_gameThread.store(pthread_self(), std::memory_order_relaxed);
    g_gameThreadKnown.store(true, std::memory_order_release);
    bridge::TickRecipes();
    if (TLModFrameCallback cb = g_frameCallback.load(std::memory_order_acquire)) cb();
    if (orig) orig(self, gameTime, method);
}

}  // extern "C"
