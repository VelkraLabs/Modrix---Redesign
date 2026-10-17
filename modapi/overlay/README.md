# Overlay do TLMod (Fase 5)

Overlay do TLMod dentro do jogo, com uma superfície aberta por vez:

- **Botão "M"** (flutuante): painel com o **Log** (as últimas 150
  linhas do log do TLMod, com o caminho do `tlmod.log`).
- **Ícones abaixo do "M"**, só com um mundo carregado:
  - **Boss Checklist** (prancheta; JSON do mod `tlmod.boss_checklist`): uma
    **gaveta** que desliza da esquerda para a direita, na altura toda da tela;
  - um por mod que registra o canal `panel` (provedor e comando), como as
    **Ferramentas** do `tlmod.devtools` (chave inglesa): um **modal** no centro.
  O ícone de um mod vem do campo `"icon"` do JSON dele (`checklist`, `tools`,
  `star`, `gem`, `sword`, `book`); sem ele, a inicial do título. Tocar no
  ícone de novo, no X ou no fundo escurecido fecha. Ao sair do mundo, os
  ícones somem e a gaveta ou o modal fecham.

Os ícones são desenhados com `Canvas` (`LauncherView`), sem imagens.

**Botões desenhados pelo jogo.** Quando a ponte consegue
(`il2cpp_bridge/src/hud_buttons.cpp`, `TLMod_HudAvailable`), o "M" e os ícones
são desenhados pelo próprio Terraria, junto dos controles de toque: o botão
quadrado do jogo com o ícone de um item. O Log usa o Livro, o Boss Checklist o
Olho de Aparência Suspeita e as Ferramentas a Chave Inglesa Multicolorida (ver
`kLauncherIconItems`; um mod escolhe o seu com `"icon_item"` no JSON do
painel). Por isso as telas do jogo (pausa, Configurações, Bestiário, mapa)
ficam por cima, e o toque passa pelo sistema do jogo. Com o inventário aberto,
os botões somem. O painel publica o retrato dos botões (`PanelController::Hud`,
com mutex), e a ponte devolve os toques (`HudTap`), atendidos no `Tick`. Com os
botões do jogo, as Views do overlay só aparecem no modo de edição, e Concluir
grava a posição de todos os botões. As telas abertas por eles (o Log, a gaveta
e o modal) continuam Views Android.

**Posição, tamanho e opacidade dos botões.** Ficam em **Configurações > Mods**,
uma aba a mais na tela de Configurações do próprio jogo
(`il2cpp_bridge/src/settings_tab.cpp`), com o ícone da Chave Inglesa.
As linhas usam as entradas do jogo, e o que elas fazem vem de `ModSettingsTab`
(`settings_tab.h`):

- **Posição dos botões de mods: Editar** fecha as Configurações e abre o
  **modo de edição**. O "M" e todos os ícones aparecem, mesmo fora de um
  mundo, com anel tracejado, sobre um fundo escurecido e com uma barra no
  topo (**Padrão** e **Concluir**). Arrastar move um botão; ao soltar, o
  centro dele vai para a configuração.
- **Tamanho** (50% a 150%) e **Opacidade** (30% a 100%), em barras de 10 em
  10%. A opacidade vale fora do modo de edição.
- **Padrão** (o botão da tela, com a aba aberta) volta tudo ao normal.

Fora do modo de edição, os botões só respondem ao toque e não se arrastam.
Um ícone sem posição salva fica na coluna abaixo do "M" e acompanha o "M"
quando ele é movido.

**Oficina de Mods (painel).** Tela grande para gerenciar os mods. O botão
**Mods** do menu principal abre a Oficina nativa, feita com as telas do próprio
jogo (`il2cpp_bridge/src/workshop_screen.cpp`); esta só substitui aquela se
alguma peça do jogo não resolver. Mostra os mods em seções pelo estado desta
sessão (Ativos, Desativados, Com erro), com versão, autores, descrição, o que
cada um adiciona e o interruptor de liga/desliga. "Ver o erro" abre a mensagem
inteira de um mod que falhou. A mudança vale na próxima abertura do jogo.

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
| Desenho (Views) | `java/.../TLModOverlay.java` | não: só no aparelho |
| Cola JNI e carga do DEX | `android/overlay_jni.cpp` | não: só no aparelho |
| Prévia em PNG | `preview/render_preview.py` | gera imagem |

O `PanelController` entrega um view-model em JSON (`SnapshotJson()`) que já
traz textos, cores e medidas em dp: `surface` (`none`, `log`, `checklist` ou
`mod:<id>`), `launchers` (id, rótulo, ícone, estilo `drawer`/`modal`, ativo) e
`launchers_visible` e `layout` (modo de edição, `scale_pct`, `opacity_pct`
e as posições salvas). O Java só desenha esse JSON e devolve ações
(`toggle_open`, `open`, `close`, `toggle_mod`, `mod_command`, `workshop_open`,
`workshop_close`, `workshop_expand`, `layout_edit`, `layout_move`,
`layout_reset`, `layout_done`). A prévia lê o mesmo JSON.

- **Modo de edição.** A aba Mods roda na thread do jogo: chama
  `PanelController::RequestLayoutEdit()` (atômico), e o modo abre no próximo
  `Tick`. As posições são o centro do botão em décimos de milésimo da largura e
  da altura da tela (`ButtonPlacement`). Assim, a mesma posição relativa vale
  em qualquer resolução e são refeitas quando o tamanho da tela muda.

- **Oficina de Mods.** O botão do menu roda na thread do jogo: chama
  `PanelController::RequestWorkshop()` (atômico), e a Oficina abre no próximo
  `Tick`, na thread de UI. Abrir fecha o painel e esconde o botão flutuante; o
  fundo escurecido consome os toques, então o menu do jogo fica inerte embaixo.
  A posição do botão Mods no menu está em `il2cpp_bridge/src/menu_hooks.cpp`.

- **Erro de carga.** O `detail` de um mod que falhou é a mensagem inteira (com
  o `dlerror()` completo). O Java quebra a linha em vez de pôr reticências, e o
  texto é selecionável para copiar.
- **Modal de mod.** O conteúdo é o JSON do canal `panel` do mod, repassado como
  está (seções com título, nota, campo de busca e linhas de botões; ver
  `panel.h`). Atualiza a cada 0,5 s com o modal aberto e logo depois de cada
  toque. Botões quebram em linhas conforme a largura. O campo de busca é o
  mesmo `EditText` entre desenhos, e a aba não é redesenhada enquanto ele
  tem foco, para não perder o texto nem fechar o teclado. A resposta do mod
  (`toast`) aparece no topo por 4 s.
- **Log (o "M").** Atualiza a cada 1 s com o painel aberto. As linhas vão
  num `TextView` só (monoespaçado, vermelho para erro, amarelo para aviso,
  selecionável) e a rolagem acompanha o fim enquanto ela estiver no fim.

## Liga/desliga

A escolha é salva em `tlmod_config.json`, na mesma pasta que contém `mods/`:

```json
{"schema":1,"disabled_mods":["tlmod.example_item"],
 "buttons":{"scale":100,"opacity":100,"positions":{"menu":[9300,2600]}}}
```

`buttons` guarda o tamanho e a opacidade, em %, e só as posições dos botões
que foram movidos (`menu`, `checklist` ou `mod:<id>`). Um valor inválido nessa
parte é ignorado sem erro, porque só afeta a aparência. Essa parte vale na hora.

A gravação é atômica (`.tmp` + `rename`). A mudança vale a partir da próxima
abertura do jogo: `ModLoader::SetDisabled` é aplicado antes de `LoadAll`, e
nenhum mod é descarregado com o jogo rodando.

## Build

O build Android precisa de `javac` (11 ou mais novo) e de um Android SDK com
`platforms/android-*/android.jar` e `build-tools` 35 ou mais novo (o d8 34
quebra com classes anônimas do javac 21). Informe o SDK assim:

```sh
cmake -S modapi -B build-android -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DTLMOD_ANDROID_SDK=$ANDROID_SDK_ROOT
```

Para gerar a prévia no host:

```sh
build-host/overlay_snapshot modapi/mods/boss_checklist/sample_checklist.json out/ modapi/mods/devtools/sample_panel.json
python3 modapi/overlay/preview/render_preview.py out/snapshot_closed.json out/snapshot_checklist.json \
    out/snapshot_log.json out/snapshot_tools.json preview.png
python3 modapi/overlay/preview/render_preview.py out/snapshot_edit.json out/snapshot_moved.json botoes.png
```
