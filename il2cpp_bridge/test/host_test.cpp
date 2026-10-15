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

    std::printf("TODOS OS TESTES PASSARAM\n");
    return 0;
}
