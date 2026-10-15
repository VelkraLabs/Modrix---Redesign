// Teste de host: carrega a libil2cpp.so falsa, roda Modrix_Init como no jogo
// (esperando o "il2cpp_init"), lê statLife, chama Heal via runtime_invoke e
// dispara Player.Hurt pelo caminho do hook.
#include <dlfcn.h>

#include <chrono>
#include <cstdio>
#include <thread>

#include "bridge.h"

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                   \
        }                                                               \
        std::printf("ok  %s\n", #cond);                                 \
    } while (0)

namespace {
bool (*mock_patch_function)(void*, void*);
double (*mock_game_hurt_player)(void*, int32_t);

ModrixHurtEvent g_lastPost{};
int g_preCalls = 0;

// Instalador de "hook" do teste: no Android isso seria DobbyHook/ShadowHook.
bool TestInstaller(void* target, void* replacement, void** original) {
    *original = target;  // a função original continua intacta no mock
    return mock_patch_function(target, replacement);
}

// Callback de mod de exemplo: corta o dano pela metade e bloqueia golpes > 1000.
bool HalfDamage(ModrixHurtEvent* ev, bool after) {
    if (after) {
        g_lastPost = *ev;
        return true;
    }
    ++g_preCalls;
    if (ev->damage > 1000) return false;
    ev->damage /= 2;
    return true;
}
// --- NPCs: o mesmo esquema que a camada de mods usa --------------------------
// Tabela lateral: slot de Main.npc -> ID de mod (-1 = NPC original).
int32_t g_modSlot[200];
int g_setDefaultsCalls = 0;
int32_t g_lastSetDefaultsIndex = -2;

int32_t RemapSpawn(int32_t type) { return type >= 4096 ? 4 : type; }  // casca: Eye of Cthulhu
void OnSpawned(Il2CppObject*, int32_t index, int32_t requested, int32_t spawned) {
    if (index >= 0 && index < 200 && requested != spawned) g_modSlot[index] = requested;
}
void OnSetDefaults(Il2CppObject*, int32_t index, int32_t) {
    ++g_setDefaultsCalls;
    g_lastSetDefaultsIndex = index;
    if (index >= 0) g_modSlot[index] = -1;  // o slot mudou de dono
}
bool OnAi(Il2CppObject* npc, int32_t index) {
    if (index < 0 || g_modSlot[index] < 0) return false;  // NPC original: AI original
    ModrixNpcFields f{};
    Modrix_ReadNpc(npc, &f);
    f.ai[1] += 10.0f;  // "comportamento do mod"
    f.vx = -2.0f;
    Modrix_WriteNpc(npc, &f);
    return true;  // substitui a AI original
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s /caminho/libil2cpp.so\n", argv[0]);
        return 2;
    }
    // "O jogo" carrega a libil2cpp.so; o runtime ainda não foi inicializado.
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    CHECK(lib != nullptr);
    auto mock_init = reinterpret_cast<void (*)()>(dlsym(lib, "mock_il2cpp_init"));
    auto mock_local = reinterpret_cast<void* (*)()>(dlsym(lib, "mock_local_player"));
    mock_patch_function = reinterpret_cast<bool (*)(void*, void*)>(dlsym(lib, "mock_patch_function"));
    mock_game_hurt_player = reinterpret_cast<double (*)(void*, int32_t)>(dlsym(lib, "mock_game_hurt_player"));
    CHECK(mock_init && mock_local && mock_patch_function && mock_game_hurt_player);

    Modrix_SetLibraryName(argv[1]);
    Modrix_SetHookInstaller(&TestInstaller);
    Modrix_SetHurtCallback(&HalfDamage);

    // Modrix_Init tem de esperar o il2cpp_init, que acontece 300 ms depois.
    bool initOk = false;
    std::thread th([&] { initOk = Modrix_Init(5000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    mock_init();
    th.join();
    CHECK(initOk);
    CHECK(Modrix_OriginalPlayerHurt() != nullptr);

    Il2CppObject* player = Modrix_GetLocalPlayer();
    CHECK(player == mock_local());
    CHECK(Modrix_GetStatLife(player) == 400);
    CHECK(Modrix_GetPlayerIndex(player) == 0);
    CHECK(Modrix_GetPlayerByIndex(0) == player);
    CHECK(Modrix_GetPlayerByIndex(1) == nullptr);
    CHECK(Modrix_GetPlayerByIndex(255) == nullptr);
    CHECK(Modrix_WaitReady(0));

    CHECK(Modrix_Heal(player, 50));
    CHECK(Modrix_GetStatLife(player) == 450);

    // Dano 100 -> callback corta para 50 -> defesa 10 tira 5 -> 45 de dano.
    double dealt = mock_game_hurt_player(player, 100);
    CHECK(g_preCalls == 1);
    CHECK(dealt == 45.0);
    CHECK(g_lastPost.lifeBefore == 450);
    CHECK(g_lastPost.lifeAfter == 405);
    CHECK(Modrix_GetStatLife(player) == 405);

    // Golpe cancelado pelo mod: vida inalterada, retorno 0.
    CHECK(mock_game_hurt_player(player, 5000) == 0.0);
    CHECK(Modrix_GetStatLife(player) == 405);

    // ---- NPCs --------------------------------------------------------------
    auto mock_game_new_npc = reinterpret_cast<int32_t (*)(int32_t, int32_t, int32_t)>(dlsym(lib, "mock_game_new_npc"));
    auto mock_game_update_npcs = reinterpret_cast<void (*)()>(dlsym(lib, "mock_game_update_npcs"));
    auto mock_game_transform = reinterpret_cast<void (*)(int32_t, int32_t)>(dlsym(lib, "mock_game_transform"));
    auto mock_npc = reinterpret_cast<void* (*)(int32_t)>(dlsym(lib, "mock_npc"));
    CHECK(mock_game_new_npc && mock_game_update_npcs && mock_game_transform && mock_npc);
    for (int32_t& v : g_modSlot) v = -1;
    ModrixNpcCallbacks cbs{&RemapSpawn, &OnSpawned, &OnSetDefaults, &OnAi};
    Modrix_SetNpcCallbacks(&cbs);

    // Spawn original: passa direto.
    CHECK(mock_game_new_npc(100, 200, 2) == 0);
    ModrixNpcFields f{};
    CHECK(Modrix_ReadNpc(Modrix_GetNpcByIndex(0), &f) && f.type == 2 && f.x == 100.0f && g_modSlot[0] == -1);

    // Spawn de mod: 4096 vira casca 4 antes do SetDefaults original (que
    // abortaria com 4096); o slot fica marcado como NPC de mod.
    CHECK(mock_game_new_npc(300, 400, 4096) == 1);
    CHECK(Modrix_ReadNpc(Modrix_GetNpcByIndex(1), &f) && f.type == 4 && f.lifeMax == 104);
    CHECK(g_modSlot[1] == 4096);
    // Dentro de NewNPC o whoAmI ainda era 0: o índice veio da busca em Main.npc.
    CHECK(g_lastSetDefaultsIndex == 1);
    CHECK(Modrix_GetNpcByIndex(1) == mock_npc(1) && Modrix_FindNpcIndex(static_cast<Il2CppObject*>(mock_npc(1))) == 1);

    // Um tick: NPC original roda a AI original; o de mod roda a do mod.
    mock_game_update_npcs();
    Modrix_ReadNpc(Modrix_GetNpcByIndex(0), &f);
    CHECK(f.ai[0] == 1.0f && f.vx == 1.0f);
    Modrix_ReadNpc(Modrix_GetNpcByIndex(1), &f);
    CHECK(f.ai[0] == 0.0f && f.ai[1] == 10.0f && f.vx == -2.0f);

    // Transform (fase de boss, por exemplo) vira tipo original: slot desmarcado.
    mock_game_transform(1, 5);
    CHECK(g_modSlot[1] == -1);
    mock_game_update_npcs();
    Modrix_ReadNpc(Modrix_GetNpcByIndex(1), &f);
    CHECK(f.type == 5 && f.ai[0] == 1.0f);

    // Spawn pedido pela camada de mods (runtime_invoke -> NewNPC com hook).
    CHECK(Modrix_SpawnNpc(10, 20, 4097) == 2);
    CHECK(g_modSlot[2] == 4097 && Modrix_ReadNpc(Modrix_GetNpcByIndex(2), &f) && f.type == 4);

    std::printf("TODOS OS TESTES PASSARAM\n");
    return 0;
}
