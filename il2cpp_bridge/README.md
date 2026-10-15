# Ponte IL2CPP do TLMod (Terraria Mobile)

Esqueleto em C++ que fala com o runtime IL2CPP do jogo através dos exports da
`libil2cpp.so` (`il2cpp_domain_get`, `il2cpp_class_from_name`,
`il2cpp_field_get_offset`, `il2cpp_runtime_invoke` …), resolvidos via
`dlopen(RTLD_NOLOAD)` + `dlsym`.

Alvo: Unity 2021.3.56f2, arm64-v8a. Offsets e RVAs em `include/terraria_offsets.h`.

## O que faz

1. Espera a `libil2cpp.so` estar mapeada e o `il2cpp_init` ter rodado (`il2cpp_get_corlib() != NULL`).
2. Anexa a thread ao IL2CPP, acha `Assembly-CSharp.dll` e resolve `Terraria.Player` e `Terraria.Main`.
3. Resolve o offset de `Player.statLife` pela API e o compara com o dump (`0x704`).
4. Resolve `Player.Hurt` (8 parâmetros), compara `methodPointer` com `base + 0xFA15B8` e instala o
   inline hook com o [ShadowHook](https://github.com/bytedance/android-inline-hook) v2.0.1 (MIT),
   embutido na `.so`, em modo multi (`src/hook_shadowhook.cpp`).
5. A cada `Player.Hurt`, `TLMod_Hooked_PlayerHurt` chama o callback do mod antes e depois do original.
6. Resolve `Terraria.NPC` e instala hooks em `NPC.NewNPC` (`0xEE4630`), `NPC.SetDefaults` (`0xDAF1BC`)
   e `NPC.AI` (`0xDCE54C`), expostos via `TLMod_SetNpcCallbacks` (`src/npc_hooks.cpp`). Se algo de NPC
   não resolver, só esses hooks ficam desligados.
7. Instala hooks no save/load de personagem e mundo (`Player.InternalSavePlayerFile`, `Player.LoadPlayer`,
   `WorldFile.InternalSaveWorld`, `WorldFile.LoadWorld`) e expõe os slots de item que o jogo grava
   (`TLMod_SetSaveCallback`, `TLMod_ItemSlot`, `src/save_hooks.cpp`). O save original roda via
   `il2cpp_runtime_invoke`, que devolve a exceção gerenciada em vez de desenrolá-la pelos nossos
   frames; o "depois" roda sempre e a exceção é relançada com `il2cpp_raise_exception`.

## Build

Android (gera `libtlmod_bridge.so`, com o ShadowHook embutido). O configure baixa o ShadowHook via git;
para build offline, acrescente `-DFETCHCONTENT_SOURCE_DIR_SHADOWHOOK=/caminho/android-inline-hook`:

```sh
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
cmake --build build-android
```

Teste de host, contra uma `libil2cpp.so` falsa (`test/mock_il2cpp.cpp`). O ShadowHook só roda em ARM/Android,
então no host o teste usa um instalador falso que desvia a chamada do "jogo" para o handler:

```sh
cmake -S . -B build-host && cmake --build build-host && (cd build-host && ctest --output-on-failure)
```

No teste de host aparece um aviso de que `Player.Hurt` não bate com base+RVA. Isso é esperado,
porque o mock não tem o layout do binário real.

## O que falta para rodar no jogo

- **Forma de carregar a `.so` no processo**: `System.loadLibrary` injetado no smali, `wrap.sh`, ou um loader.
- **Instante da inicialização**: o polling de `il2cpp_get_corlib` é uma heurística. Hookar `il2cpp_init`
  é mais preciso.

## Notas sobre o ShadowHook

- `shadowhook_init` roda na thread de inicialização, na primeira instalação, e nunca no construtor da `.so`
  (ele mexe no linker, e o construtor roda com o lock do linker preso).
- `sh_jni.c` fica fora da build: o `JNI_OnLoad` dele tentaria registrar natives de uma classe Java que o jogo não tem.
- O endereço da original fica num slot global (`g_origHurtSlot`), lido atomicamente a cada chamada. No modo multi,
  o ShadowHook reescreve esse slot quando outro hook no mesmo alvo é removido.
- O upstream compila com o NDK r23. Com o r27, quatro grupos novos do `-Weverything` são desligados só para o
  ShadowHook (ver comentário no `CMakeLists.txt`); o resto do `-Weverything -Werror` dele continua valendo.
- A `.so` exporta apenas `TLMod_*` (`src/exports.map`) e é alinhada a 16 KB (Android 15+).
