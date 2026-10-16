# TLMod: pacote de teste no aparelho

Pacote de teste do TLMod no Terraria Mobile (**4ª versão**). Ele traz a ponte e o runtime
compilados para Android arm64 (release, sem símbolos) e a pasta `tlmod/` pronta, com quatro mods.

Não traz nenhum arquivo do jogo nem do dump.

## O que mudou desde o pacote anterior

A 3ª versão funcionou no aparelho (Samsung SM-A075M, Android 16): 31 hooks instalados, os 3 mods
carregados, os rubis entregues e o Titã invocado. Esta versão acrescenta um mod para testar mais
rápido:

- **`tlmod.devtools`, a aba Ferramentas no painel:**
  - **Hora do dia:** Amanhecer, Meio-dia, Anoitecer e Meia-noite, mais **Congelar hora**.
  - **Jogador:** **Curar** (vida e mana), **Invencível** e **Matar inimigos** próximos (com loot).
  - **Invocar NPC ou boss:** busca por nome ou ID, botões para 16 bosses e os NPCs de mod.
  - **Dar item:** busca por nome (com ou sem acento, ou o nome interno em inglês, como `Zenith`)
    ou por ID, com +1, +10 e +100. Sem busca, lista os itens de mod.
- É um mod separado: liga e desliga na aba Mods como os outros. Para jogar sem ele, desligue ou
  apague a pasta `tlmod/mods/devtools`.
- O painel ficou um pouco mais largo (400 dp) para caber a quarta aba.

Arquivos do pacote que mudaram:

| arquivo | antes | agora |
|---|---|---|
| `lib/arm64-v8a/libtlmod_bridge.so` | 389336 bytes, `3b926f4793a7d0ee…` | 392896 bytes, `5d970bf878748390…` |
| `lib/arm64-v8a/libtlmod_runtime.so` | 860144 bytes, `8b1a4fbbf4e5b29c…` | 891632 bytes, `fd0b69e26678aaef…` |
| `tlmod/mods/boss_checklist/lib/android-arm64/libboss_checklist.so` | 233520 bytes, `0a4c5092953f7910…` | 233520 bytes, `a22ec19635ff1ea5…` |
| `tlmod/mods/devtools/lib/android-arm64/libdevtools.so` | (novo) | 490824 bytes, `03049ce0171c3785…` |
| `tlmod/mods/devtools/manifest.json` | (novo) | 701 bytes, `74cb60b82025d083…` |
| `tlmod/mods/example_boss/lib/android-arm64/libexample_boss.so` | 225664 bytes, `d00db2b66ba43853…` | 225664 bytes, `e07ca2d00d92d86d…` |
| `tlmod/mods/example_item/lib/android-arm64/libexample_item.so` | 224944 bytes, `2e569a44f298c360…` | 224944 bytes, `e77a6be77d5620e0…` |
| `README_TESTE.md`, `SHA256SUMS.txt` | | atualizados |

Iguais ao pacote anterior: manifestos e texturas dos mods de exemplo, os marcadores e o
`tlmod_config.json`.

**Para atualizar o aparelho:** troque as duas `.so` no seu carregador e copie a pasta `tlmod/`
de novo. Ela agora tem `mods/devtools`, e as `.so` dos outros mods foram recompiladas.
Se você desligou algum mod no painel, guarde o seu `tlmod_config.json` antes de copiar a pasta.

## Conteúdo

```
tlmod-teste/
├── README_TESTE.md                 este arquivo
├── SHA256SUMS.txt                  SHA-256 de todos os arquivos (sha256sum -c SHA256SUMS.txt)
├── lib/arm64-v8a/
│   ├── libtlmod_bridge.so          ponte IL2CPP (hooks no libil2cpp.so)
│   └── libtlmod_runtime.so         runtime de mods + painel (depende da ponte)
└── tlmod/                          vai para o celular exatamente assim
    ├── tlmod_config.json           configuração padrão: nenhum mod desligado
    └── mods/
        ├── example_item/           Rubi Fictício (item de mod)
        │   └── give_on_start       marcador: 3 rubis ao entrar num mundo
        ├── example_boss/           Titã de Brasa (boss de mod)
        │   └── spawn_on_start      marcador: invoca o boss ao entrar num mundo
        ├── boss_checklist/         lista de bosses derrotados (aba do painel)
        └── devtools/               aba Ferramentas: hora, itens, invocar, curar (só para testes)
```

## Versão do jogo

As duas `.so` foram compiladas e conferidas contra esta build. Em outra build, a ponte acusa a
diferença no logcat. Ela resolve tudo pelo nome e pode funcionar, mas sem garantia.

| | |
|---|---|
| Terraria | v1.4.4.9 (`Main.versionNumber`; `curRelease` 279), versão mobile |
| Unity | 2021.3.56f2 (IL2CPP, metadata v31) |
| `libil2cpp.so` (arm64-v8a) | 48.408.592 bytes, SHA-256 `3c45ab44d50e54b3769d906655b2b6e637df39e6a400447a57996d32e05fc228` |
| `global-metadata.dat` | 7.961.524 bytes, SHA-256 `957c596a09795d6a8cf1d8e0f07d44375cf5301c2d14d63ea75c64a600c55f2a` |

Para conferir o seu jogo, extraia esses dois arquivos do APK (`lib/arm64-v8a/libil2cpp.so` e
`assets/bin/Data/Managed/Metadata/global-metadata.dat`) e rode `sha256sum`.

Build: código do commit `5a623b3` (branch `ccr-bbd92bf9-i01buk`), NDK r27c, `android-24`,
`-O3`, `llvm-strip --strip-unneeded`, páginas alinhadas a 16 KB.

## Instalação

### 1. A pasta `tlmod/`

Copie a pasta `tlmod/` inteira para dentro da pasta de arquivos do app do Terraria:

```
/storage/emulated/0/Android/data/<pacote>/files/tlmod/
```

`<pacote>` é o nome do pacote do Terraria. Na versão da Play Store ele costuma ser
`com.and.games505.TerrariaPaid`; confira em Configurações › Apps › Terraria, ou com
`adb shell pm list packages | grep -i terraria`. O caminho final fica assim:

```
/storage/emulated/0/Android/data/<pacote>/files/tlmod/tlmod_config.json
/storage/emulated/0/Android/data/<pacote>/files/tlmod/mods/example_item/...
```

- Abra o jogo uma vez antes, para que a pasta `files/` exista.
- No Android 11 ou mais novo, a maioria dos gerenciadores de arquivos não consegue gravar em
  `Android/data`. Pelo computador funciona:
  `adb push tlmod /storage/emulated/0/Android/data/<pacote>/files/`
- O runtime procura nesta ordem: a variável `$TLMOD_MODS_DIR`, depois
  `.../Android/data/<pacote>/files/tlmod/mods` (usada **só se a pasta existir**) e por último
  `/data/data/<pacote>/files/tlmod/mods`. O `tlmod_config.json` fica sempre ao lado de `mods/`.
- O runtime cria sozinho `tlmod/cloud_saves/`, para os itens de mod de saves na nuvem, e
  `tlmod/tlmod.log` (passando de 1 MiB, vira `tlmod.log.1`). Para trazer o log ao computador:
  `adb pull /storage/emulated/0/Android/data/<pacote>/files/tlmod/tlmod.log`
- As `.so` dos mods são copiadas para a pasta privada do app (`code_cache/tlmod/<mod_id>/`, em geral
  `/data/user/0/<pacote>/code_cache/tlmod/...`) e carregadas de lá. Você não precisa fazer nada: a
  cópia é refeita sozinha quando a `.so` do mod muda. O Android pode apagar o `code_cache` (por
  exemplo, ao atualizar o app), e nesse caso ela é refeita na abertura seguinte.

### 2. As `.so`

O pacote **não traz o carregador**: as duas bibliotecas precisam ser carregadas dentro do processo
do Terraria por um método à sua escolha, como `System.loadLibrary` num APK reempacotado, um loader
de injeção ou `wrap.sh`. Regras:

- Carregue `libtlmod_runtime.so`. Ela depende de `libtlmod_bridge.so` (`NEEDED`), então as duas
  precisam estar na mesma pasta de bibliotecas do app. Se o carregador usar `dlopen` com caminho
  absoluto, carregue primeiro `libtlmod_bridge.so` e depois `libtlmod_runtime.so`.
- O melhor é carregar cedo, antes ou logo depois de `libil2cpp.so`. A ponte espera o IL2CPP subir
  sozinha (até 120 s).
- Por `System.loadLibrary`, o `JNI_OnLoad` do runtime guarda a VM Java. Por outros caminhos, o
  painel pede a VM à ART.

## Primeiro teste: o que você deve ver

Use um **personagem e um mundo de teste**. O boss usa o Olho de Cthulhu como casca: matá-lo conta
como derrotar o Olho nesse mundo e dá o loot do Olho. Teste **à noite**, porque de dia a IA do Olho
foge depois da introdução.

Acompanhe pelo logcat: `adb logcat -s TLMod TLModBridge`.

1. **Ao abrir o jogo**
   - `TLModBridge` (no `tlmod.log`, como `[ponte]`): a `libil2cpp.so` encontrada,
     `Terraria.Player=...`, `Android <versão> (API <n>), <fabricante> <modelo>`,
     `ShadowHook: biblioteca auxiliar em .../code_cache/tlmod/_ponte/libshadowhook_nothing.so`,
     `ShadowHook: monitor do linker ok` e, para cada hook, `... confere com o dump` seguido de
     `ShadowHook ...: hook em 0x... -> 0x...` e `hook em ... instalado`. **Nenhuma** linha
     `shadowhook_init falhou` ou `falha ao instalar hook`.
   - `TLMod`: `pasta de mods: .../files/tlmod/mods`,
     `cópias das bibliotecas dos mods: /data/user/0/<pacote>/code_cache/tlmod`, uma linha
     `biblioteca copiada para .../code_cache/tlmod/<mod_id>/lib....so (SHA-256 ...)` por mod (nas
     aberturas seguintes, `cópia reaproveitada: ...`) e depois `4 de 4 mods carregados`. Logo depois,
     `catálogo de nomes pronto: 5456 itens, 688 NPCs`.
   - `Rubi Fictício registrado com ID ...`, `give_on_start: 3 Rubis Fictícios ao entrar num mundo`,
     `Titã de Brasa registrado com ID ...` e `spawn_on_start: Titã de Brasa ao entrar num mundo`.
2. **Botão do painel.** Um botão redondo com **"M"** no canto superior esquerdo, um pouco abaixo
   do topo. Ele aparece no menu e no mundo, e pode ser arrastado. O toque abre o painel com quatro abas:
   - **Mods**: os 4 mods como **Carregado**, cada um com um botão de liga/desliga. Mudanças valem
     na próxima abertura do jogo e ficam gravadas em `tlmod/tlmod_config.json`. Se algum falhar,
     o erro aparece inteiro embaixo do nome, em vermelho.
   - **Boss Checklist**: bosses, minibosses e eventos com status. No menu tudo aparece como
     desconhecido; dentro do mundo aparecem derrotado ou pendente, mais o próximo boss pendente.
   - **Log**: o caminho do `tlmod.log` e as últimas 150 linhas (erros em vermelho, avisos em
     amarelo), atualizadas a cada segundo e acompanhando o fim. Inclui as linhas da ponte
     (`[ponte]`) desde o início do jogo.
   - **Ferramentas**: a hora atual do mundo e os botões de hora, jogador, invocar e dar item (veja a
     seção abaixo).
3. **Os 3 rubis.** No primeiro frame dentro do mundo chegam **3 Rubis Fictícios** ao
   inventário (empilhados num slot). Eles têm o ícone vermelho do mod, o nome "Rubi Fictício" e o
   tooltip "Não existe no Terraria. Existe no TLMod.", com raridade verde e valor de 50 de prata.
   No logcat: `give_item: 3x tlmod.example_item:fictional_ruby entregue`.
4. **O boss.** **2 segundos** depois de o mundo carregar, o **Titã de Brasa** aparece cerca de 30
   blocos à direita e 15 acima do jogador, com a imagem do mod (4 quadros) e 12000 de vida. Nos
   primeiros 2 s ele fica parado; depois passa a se mover com a IA do Olho de Cthulhu. Ao passar o
   dedo ou mirar nele, o nome é "Titã de Brasa". No logcat:
   `spawn_npc_near_player: tlmod.example_boss:ember_titan no slot N em (x, y)`.

Os dois marcadores valem **uma vez por abertura do jogo**: sair do mundo e entrar de novo não repete,
fechar e abrir o jogo repete. Para parar, apague `give_on_start` e `spawn_on_start`, ou desligue
o mod no painel.

## Testes rápidos com a aba Ferramentas

Com a aba, quase toda a checklist abaixo pode ser feita sem esperar o jogo:

- **Noite para o Titã:** toque em Meia-noite e depois em Congelar hora.
- **Boss:** em Invocar, toque em Titã de Brasa (ou num dos 16 bosses). Ele nasce perto de você.
- **Loot e morte:** toque em Matar inimigos, ou em Invencível para lutar sem morrer.
- **Itens:** busque `rubi` (o item de mod) ou qualquer item do jogo, como `zenite`, `estrela` ou `4956`,
  e toque em +1/+10/+100. Os rubis também aparecem na lista sem busca.
- **Loja:** dê um rubi e venda num NPC (deve valer 50 de prata).

Os marcadores `give_on_start` e `spawn_on_start` continuam no pacote; com a aba, você pode apagá-los.

## O que só dá para confirmar no aparelho

O runtime, o painel e os mods passam nos testes de host contra um jogo simulado. Os pontos abaixo
dependem do jogo real.

**Carga e ponte**
- [ ] O log não tem `shadowhook_init falhou` nem `falha ao instalar hook`; tem `ShadowHook: monitor do
      linker ok` (se aparecer `monitor do linker indisponível`, os hooks devem funcionar mesmo assim:
      me mande o `tlmod.log` e o `adb logcat -s shadowhook_tag`)
- [ ] O carregador escolhido põe as duas `.so` no processo, e o log mostra `4 de 4 mods carregados`
- [ ] Todos os hooks aparecem como `instalado`, sem aviso de RVA ou offset divergente
- [ ] O jogo abre e chega ao menu sem travar nem fechar

**Aba Ferramentas (tlmod.devtools)**
- [ ] A aba aparece com o nome Ferramentas, e as 4 abas cabem numa linha
- [ ] Hora: os 4 botões mudam a hora, e a nota "Agora: ..." acompanha; Congelar segura a hora
- [ ] Busca: o teclado abre no campo, o texto não some enquanto você digita, e Buscar (ou o botão de
      busca do teclado) mostra os resultados; nomes com acento acham sem acento
- [ ] Dar item: +1/+10/+100 entregam no inventário (itens do jogo e o Rubi Fictício)
- [ ] Invocar: os bosses e o Titã nascem perto do jogador; Os Gêmeos chegam os dois
- [ ] Invencível impede o dano; Curar enche vida e mana; Matar inimigos mata os hostis próximos
      (com loot), sem matar NPCs da cidade
- [ ] A resposta (ex.: "10x Lente no inventário") aparece no topo da aba e some depois de alguns segundos
- [ ] Desligar o devtools na aba Mods tira a aba Ferramentas na abertura seguinte

**Cópia das bibliotecas e log**
- [ ] Os 4 mods aparecem como Carregado; o log mostra `biblioteca copiada para .../code_cache/tlmod/...`
- [ ] Na segunda abertura do jogo (sem reinstalar), o log mostra `cópia reaproveitada: ...` para os 4
- [ ] Trocar a `.so` de um mod por outra versão gera uma cópia nova na abertura seguinte
- [ ] `getCodeCacheDir` funciona (nenhum aviso `Context.getCodeCacheDir indisponível` no log)
- [ ] `tlmod/tlmod.log` é criado, começa cada abertura com `===== TLMod: sessão iniciada`, e tem as
      linhas `[ponte]` do início (hooks instalados) e as do runtime e dos mods
- [ ] Se um mod falhar, o erro aparece inteiro no painel e no `tlmod.log`, sem reticências

**Painel**
- [ ] O botão "M" aparece por cima do jogo (OpenGL ES ou Vulkan) e pode ser arrastado
- [ ] Toques fora do painel continuam indo para o jogo; toques no painel não vazam
- [ ] A aba Mods mostra os 4 mods carregados; desligar um grava `tlmod_config.json` e vale na
      abertura seguinte
- [ ] A aba Log rola, acompanha linhas novas e deixa selecionar e copiar o texto
- [ ] Os acentos aparecem certos (Titã, Fictício)

**Boss Checklist**
- [ ] Fora do mundo tudo aparece como desconhecido
- [ ] Num mundo, o status bate com o progresso real (comparar com um mundo conhecido)
- [ ] Depois de derrotar um boss, a lista atualiza ao reabrir a aba

**Rubi Fictício**
- [ ] Os 3 rubis chegam ao entrar no mundo, empilhados, com ícone, nome e tooltip do mod
- [ ] O ícone aparece também no cursor, no lixo, em baús, no hotbar e no item caído no chão
- [ ] O valor de venda numa loja é 50 de prata
- [ ] Salvar, sair e entrar de novo mantém os rubis, e aparece `<personagem>.plr.tlmod.json` ao lado do save
- [ ] O personagem abre no jogo **sem** o TLMod sem corromper; os rubis não aparecem lá e voltam
      quando o TLMod é usado de novo
- [ ] Na Jornada, a pesquisa recusa o item de mod
- [ ] Saves na nuvem: os itens de mod voltam (arquivo em `tlmod/cloud_saves/`)

**Titã de Brasa**
- [ ] Aparece 2 s depois de entrar no mundo, perto do jogador (à direita e acima)
- [ ] `NPC.NewNPC` aceita a origem nula e o boss nasce sem exceção no logcat
- [ ] A textura do mod troca a do Olho, com os 4 quadros animando (fica no quadro 0 nos primeiros 2 s)
- [ ] O nome "Titã de Brasa" aparece no hover, na barra de vida e no mapa
- [ ] A vida é 12000 e o dano e a defesa batem com o mod (40/14)
- [ ] Depois da introdução, a IA do Olho assume sem travar o jogo
- [ ] A barra de vida de boss e a música aparecem ou não (dependem da casca; `boss` do mod ainda
      não é aplicado)
- [ ] A morte, o loot (do Olho) e o despawn de dia não deixam o slot marcado (sem erro no logcat)

**Geral**
- [ ] Sair do mundo, entrar em outro e fechar o jogo funcionam sem fechamento inesperado
- [ ] Desempenho igual ao jogo sem mods
- [ ] Multiplayer **não** é suportado neste teste (a tabela de IDs não é sincronizada)

## SHA-256 dos arquivos

Também em `SHA256SUMS.txt`, que inclui este README:

| arquivo | bytes | SHA-256 |
|---|---|---|
| `lib/arm64-v8a/libtlmod_bridge.so` | 392896 | `5d970bf878748390ea76ae11eb268e5edf0197762381b90603eb147579a61a96` |
| `lib/arm64-v8a/libtlmod_runtime.so` | 891632 | `fd0b69e26678aaefbeb5bdccc21b951b2af57eb2295a80808575fba116b0347d` |
| `tlmod/mods/boss_checklist/lib/android-arm64/libboss_checklist.so` | 233520 | `a22ec19635ff1ea53a42c3c5186ecd111815235dacffd7b28696389ea19bd51a` |
| `tlmod/mods/boss_checklist/manifest.json` | 633 | `ece3d334283bb47e30eab683150c54d512fba0177b71cac4043c3a6bd08c7fb5` |
| `tlmod/mods/devtools/lib/android-arm64/libdevtools.so` | 490824 | `03049ce0171c378553eeaf0035e0f105a8affaaa51a65ba0bccdfdd1d2a8cd12` |
| `tlmod/mods/devtools/manifest.json` | 701 | `74cb60b82025d08314dab060767a8ced69c566cfc87a7e3fab06a2a546c0cf2e` |
| `tlmod/mods/example_boss/lib/android-arm64/libexample_boss.so` | 225664 | `e07ca2d00d92d86d52d0c5ff3a47da6e856a7fb7136c7b94afd0d94bf3b57862` |
| `tlmod/mods/example_boss/manifest.json` | 668 | `9264b2a34f56647e5a3cc9fd9f69362cd423c2c6df66d4ba086e537ff34ef38a` |
| `tlmod/mods/example_boss/spawn_on_start` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `tlmod/mods/example_boss/textures/ember_titan.png` | 5801 | `fa4b43f509b4c8e428027936e478b12121cd43db544fc0c2ed932870b15237f9` |
| `tlmod/mods/example_item/give_on_start` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `tlmod/mods/example_item/lib/android-arm64/libexample_item.so` | 224944 | `e77a6be77d5620e02053ce6eaad39bbe7d4da16c4bc71eb332be08c226f50612` |
| `tlmod/mods/example_item/manifest.json` | 614 | `aa787324e10e518b5d3c43848b9fbf1fa349c6f73525ccc51d44eff0ff74375c` |
| `tlmod/mods/example_item/textures/fictional_ruby.png` | 239 | `99abb2c88f7654e329e8e17812ee93a85a91e4810fa0aa8eb4807385b20a4c2a` |
| `tlmod/tlmod_config.json` | 32 | `fe7957a8a201d48e5f07a84daeb118524251c51bbf2a5d699b6f7ce8d1c5fe8a` |
