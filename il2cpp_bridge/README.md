# Ponte IL2CPP do Modrix (Terraria Mobile)

Esqueleto em C++ que fala com o runtime IL2CPP do jogo através dos exports da
`libil2cpp.so` (`il2cpp_domain_get`, `il2cpp_class_from_name`,
`il2cpp_field_get_offset`, `il2cpp_runtime_invoke` …), resolvidos via
`dlopen(RTLD_NOLOAD)` + `dlsym`.

Alvo: Unity 2021.3.56f2, arm64-v8a. Offsets e RVAs em `include/terraria_offsets.h`.

## O que faz

1. Espera a `libil2cpp.so` estar mapeada e o `il2cpp_init` ter rodado (`il2cpp_get_corlib() != NULL`).
2. Anexa a thread ao IL2CPP, acha `Assembly-CSharp.dll` e resolve `Terraria.Player` e `Terraria.Main`.
3. Resolve o offset de `Player.statLife` pela API e o compara com o dump (`0x704`).
4. Resolve `Player.Hurt` (8 parâmetros), compara `methodPointer` com `base + 0xFA15B8` e instala o hook
   com o instalador que você fornecer (Dobby, ShadowHook…).
5. A cada `Player.Hurt`, `Modrix_Hooked_PlayerHurt` chama o callback do mod antes e depois do original.

## Build

Android (gera `libmodrix_bridge.so`):

```sh
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
cmake --build build-android
```

Teste de host, contra uma `libil2cpp.so` falsa (`test/mock_il2cpp.cpp`):

```sh
cmake -S . -B build-host && cmake --build build-host && (cd build-host && ctest --output-on-failure)
```

No teste de host aparece um aviso de que `Player.Hurt` não bate com base+RVA. Isso é esperado,
porque o mock não tem o layout do binário real.

## O que falta para rodar no jogo

- **Engine de inline hook**: configure `Modrix_SetHookInstaller` (veja o exemplo comentado em `src/entry.cpp`).
- **Forma de carregar a `.so` no processo**: `System.loadLibrary` injetado no smali, `wrap.sh`, ou um loader.
- **Instante da inicialização**: o polling de `il2cpp_get_corlib` é uma heurística. Hookar `il2cpp_init`
  é mais preciso.
