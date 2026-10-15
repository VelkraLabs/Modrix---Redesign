# Overlay do Modrix (Fase 5)

Painel de mods dentro do jogo: botão flutuante "M", aba **Mods** (estado e
liga/desliga) e aba **Boss Checklist** (JSON do mod `modrix.boss_checklist`).

## Técnica: Views Android sobre a Activity do jogo

O painel é feito de Views Android comuns, adicionadas à janela da Activity do
Unity (`UnityPlayer.currentActivity`) com `addContentView`. As classes Java vêm
num DEX embutido na `.so` e carregado com `InMemoryDexClassLoader`; a ligação
com o C++ é feita por JNI. Não precisa de permissão de sobreposição.

- **Independente do renderizador.** O Android compõe as Views por cima da
  SurfaceView do Unity. Por isso não importa se o jogo usa OpenGL ES ou Vulkan;
  pelos arquivos que temos não dá para saber qual dos dois esta build usa.
- **Acentos.** O `TextView` desenha UTF-8 nativamente. O JNI converte o texto
  para UTF-16 de verdade, não para o "UTF-8 modificado" do JNI.
- **Toque.** A raiz (`FrameLayout`) não é clicável, então toques fora do
  painel seguem para o jogo. O painel é `clickable` e o botão consome os
  próprios toques, então nada que cai no painel vaza para o jogo. Não há hook
  de entrada.

## Separação lógica × desenho

| Camada | Onde | Testada no host |
|---|---|---|
| Estado, dados e ações (`PanelController`) | `src/panel.cpp` | sim (`tests/overlay_test.cpp`) |
| Configuração persistente (`ConfigStore`) | `src/config_store.cpp` | sim |
| Desenho (Views) | `java/.../ModrixOverlay.java` | não: só no aparelho |
| Cola JNI e carga do DEX | `android/overlay_jni.cpp` | não: só no aparelho |
| Prévia em PNG | `preview/render_preview.py` | gera imagem |

O `PanelController` entrega um view-model em JSON (`SnapshotJson()`) que já
traz textos, cores e medidas em dp. O Java só desenha esse JSON e devolve
ações (`toggle_open`, `close`, `tab`, `toggle_mod`). A prévia lê o mesmo JSON.

## Liga/desliga

A escolha é salva em `modrix_config.json`, na mesma pasta que contém `mods/`:

```json
{"schema":1,"disabled_mods":["modrix.example_item"]}
```

A gravação é atômica (`.tmp` + `rename`). A mudança vale a partir da próxima
abertura do jogo: `ModLoader::SetDisabled` é aplicado antes de `LoadAll`, e
nenhum mod é descarregado com o jogo rodando.

## Build

O build Android precisa de `javac` (11 ou mais novo) e de um Android SDK com
`platforms/android-*/android.jar` e `build-tools` 35 ou mais novo (o d8 34
quebra com classes anônimas do javac 21). Informe o SDK assim:

```sh
cmake -S modapi -B build-android -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DMODRIX_ANDROID_SDK=$ANDROID_SDK_ROOT
```

Para gerar a prévia no host:

```sh
build-host/overlay_snapshot modapi/mods/boss_checklist/sample_checklist.json out/
python3 modapi/overlay/preview/render_preview.py out/snapshot_checklist.json out/snapshot_mods.json preview.png
```
