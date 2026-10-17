# TLMod: pacote de teste no aparelho

Pacote de teste do TLMod no Terraria Mobile (**19ª versão**). Ele traz a ponte e o runtime
compilados para Android arm64 (release, sem símbolos) e a pasta `tlmod/` pronta, com quatro mods.

Não traz nenhum arquivo do jogo nem do dump.

## O que mudou desde o pacote anterior

**Os botões de mods agora são desenhados pelo próprio Terraria.** Antes eles eram uma camada do
Android colada por cima do jogo: ficavam na frente da pausa, das Configurações e do inventário, e
pegavam o toque antes do jogo. Agora:

- Cada botão é um **botão do jogo** (a mesma moldura quadrada das abas das Configurações) com o ícone
  de um item:
  - **Livro**: o Log (o antigo "M");
  - **Olho de Aparência Suspeita**: o Boss Checklist;
  - **Chave Inglesa Multicolorida**: as Ferramentas.
- Eles são desenhados junto dos **controles de toque**. As telas do jogo ficam **por cima** e os
  botões **somem** com elas, como os controles de toque: pausa e Configurações, Bestiário,
  Conquistas, mapa em tela cheia.
- **Com o inventário aberto, os botões não aparecem.** No jogo, um toque vale para todos os botões que
  estão embaixo do dedo. Se o inventário ficasse por cima de um botão nosso, o toque valeria para os
  dois.
- O toque passa pelo jogo (som e animação de botão do jogo). A tela que o botão abre (Log, gaveta,
  modal) continua igual por enquanto.
- No **menu principal** aparece só o do Log, e nenhum com as Configurações abertas.
- **Modo de edição** (Configurações > Mods > Editar): continua igual. Os botões aparecem para arrastar
  e, ao tocar em **Concluir**, a posição de **todos** fica salva. Os botões do jogo usam exatamente
  essas posições, e as que você já tinha salvo continuam valendo.
- **Tamanho** (50% a 150% do botão do jogo) e **Opacidade** continuam na aba Mods.

**Aba Mods:** o ícone agora é a **Chave Inglesa** (o da Caixa de Ferramentas é escuro e sumia no azul).
As barras se chamam só **Tamanho** e **Opacidade**: o jogo desenha o valor logo depois do título, e o
título comprido ficava embaixo do "100%".

**Se o jogo não conseguir desenhar os botões** (o log da ponte diz por quê), o overlay volta a
mostrar os botões antigos, por cima de tudo.

O que a 16ª (receitas), a 17ª (novo overlay) e a 18ª (aba Mods) trouxeram continua no pacote. A lista
do que conferir está mais abaixo.

Arquivos do pacote que mudaram:

| arquivo | antes | agora |
|---|---|---|
| `lib/arm64-v8a/libtlmod_bridge.so` | 461664 bytes, `3a2898df22160216…` | 470376 bytes, `2abe023313cb4e7b…` |
| `lib/arm64-v8a/libtlmod_runtime.so` | 986048 bytes, `eea8ab19b40fb5b5…` | 995712 bytes, `09602b6327dc6479…` |
| `README_TESTE.md`, `SHA256SUMS.txt` | | atualizados |

Todo o resto é idêntico, byte a byte, ao pacote anterior: as `.so` dos 4 mods, os manifestos,
as texturas, os marcadores e o `tlmod_config.json`.

**Para atualizar o aparelho:** troque as duas `.so` no seu carregador (`libtlmod_bridge.so` e
`libtlmod_runtime.so`). A pasta `tlmod/` pode ficar como está.

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
        ├── boss_checklist/         lista de bosses derrotados (gaveta do Olho)
        └── devtools/               Ferramentas (Chave Inglesa Multicolorida): hora, itens, invocar, curar (só para testes)
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

Build: código do commit `14484eb` (branch `ccr-bbd92bf9-i01buk`), NDK r27c, `android-24`,
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
2. **Botões de mods.** Um botão do jogo com o ícone de um **Livro**, no canto esquerdo, um pouco
   abaixo do topo. Ele aparece no menu e no mundo (para mudar de lugar: Configurações > Mods > Editar).
   O toque abre o **Log**: o caminho do `tlmod.log` e as últimas 150 linhas (erros em vermelho,
   avisos em amarelo), atualizadas a cada segundo e acompanhando o fim, desde o início do jogo
   (inclui as linhas da ponte, `[ponte]`). Dentro de um mundo, abaixo dele, ficam mais dois:
   - **Olho de Aparência Suspeita**: a gaveta do **Boss Checklist** (desliza da esquerda), com
     bosses, minibosses e eventos (derrotado ou pendente) e o próximo boss pendente;
   - **Chave Inglesa Multicolorida**: o modal **Ferramentas**, com a hora atual do mundo e os botões
     de hora, jogador, invocar e dar item (veja a seção abaixo).
   Os mods se ligam e desligam pela **Oficina de Mods** (botão **Mods** do menu principal).
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
o mod na Oficina de Mods.

## Testes rápidos com as Ferramentas

Com o modal Ferramentas (botão da Chave Inglesa Multicolorida), quase toda a checklist abaixo pode ser feita sem esperar o jogo:

- **Noite para o Titã:** toque em Meia-noite e depois em Congelar hora.
- **Boss:** em Invocar, toque em Titã de Brasa (ou num dos 16 bosses). Ele nasce perto de você.
- **Loot e morte:** toque em Matar inimigos, ou em Invencível para lutar sem morrer.
- **Itens:** busque `rubi` (o item de mod) ou qualquer item do jogo, como `zenite`, `estrela` ou `4956`,
  e toque em +1/+10/+100. Os rubis também aparecem na lista sem busca.
- **Receitas:** busque `pedra` (+100) e `gel` (+10) e fique perto de uma Bancada de Trabalho (busque
  `bancada` e coloque uma). O Rubi Fictício aparece na fabricação; faça 5 e o Rubi do jogo aparece também.
- **Loja:** invoque o Mercador (busque `mercador` ou `17` em Invocar), abra a loja e venda um rubi: rende cerca de 10 de prata (1/5 do valor de 50 de prata; a felicidade do NPC muda um pouco, como
  11 prata 23 cobre).

Os marcadores `give_on_start` e `spawn_on_start` continuam no pacote; com as Ferramentas, você pode apagá-los.

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

**Botões de mods desenhados pelo jogo (novo)**
- [ ] O log da ponte tem `botões de mods: desenhados pelo jogo` e, no primeiro frame,
      `botões de mods: tela ...; botão do jogo ...` e uma linha `botões de mods: <id> em (x, y) LxA` por
      botão. Se aparecer `botões pelo overlay`, os botões antigos voltam: **me mande o log**
- [ ] Os botões têm a moldura quadrada do jogo, com o Livro, o Olho e a Chave Inglesa Multicolorida
- [ ] Abrir a pausa, as Configurações, o Bestiário, as Conquistas ou o mapa em tela cheia: os botões
      somem (como os controles de toque) e o toque não chega mais neles
- [ ] Abrir o inventário: os botões somem; fechar: eles voltam
- [ ] Tocar num botão toca o som do jogo e abre o Log, a gaveta ou o modal (pode levar até 0,1 s)
- [ ] Com a tela aberta, o botão dela fica "apertado"
- [ ] Tamanho e Opacidade (aba Mods) mudam os botões do jogo
- [ ] Modo de edição: os botões (do overlay) aparecem onde estavam os do jogo; depois de mover e
      Concluir, os do jogo vão para o mesmo lugar
- [ ] No menu principal só aparece o do Log, e ele some com as Configurações abertas

**Aba Mods nas Configurações**
- [ ] O log da ponte tem `Configurações: aba Mods pronta` e, ao abrir as Configurações,
      `Configurações: tela ...; faixa das abas ...; abas: ...` e `aba Mods depois de ...` (ou `antes de`,
      ou `9 abas reespaçadas`). **Me mande essas linhas** se a aba ficar fora do lugar
- [ ] A 9ª aba aparece junto das outras, com o ícone da Chave Inglesa, e não cobre nada
- [ ] Tocar nela abre a página com o título "Mods" e três linhas: Posição (botão Editar), Tamanho e
      Opacidade (barras, sem o valor por cima do título); nenhuma aba do jogo fica marcada
- [ ] Tocar numa aba do jogo volta para ela normalmente; o Padrão das abas do jogo continua funcionando
- [ ] As barras se movem de 10 em 10% e mostram o valor; o tamanho e a opacidade dos botões mudam
      (dá para ver ao fechar as Configurações)
- [ ] Editar fecha as Configurações e abre o modo de edição; os botões se arrastam e ficam onde soltou;
      Concluir sai; fechar e abrir o jogo mantém as posições
- [ ] Padrão (na barra do modo de edição, ou o da tela com a aba Mods aberta) volta tudo ao normal
- [ ] Voltar, com a aba Mods aberta, fecha as Configurações
- [ ] Fora do modo de edição, os botões não se arrastam e o toque abre o Log, a gaveta e o modal

**Gaveta e modal**
- [ ] O Livro abre só o Log (sem abas)
- [ ] O Olho abre a gaveta do Boss Checklist deslizando da esquerda para a direita; tocar no fundo
      escurecido, no ✕ ou no Olho de novo fecha (deslizando de volta)
- [ ] A Chave Inglesa Multicolorida abre o modal Ferramentas no centro; o fundo escurecido, o ✕ ou o
      botão fecham
- [ ] Abrir um fecha o outro (só uma tela por vez); voltar ao menu fecha o que estiver aberto

**Ferramentas (tlmod.devtools)**
- [ ] Hora: os 4 botões mudam a hora, e a nota "Agora: ..." acompanha; Congelar segura a hora
- [ ] Busca: o teclado abre no campo, o texto não some enquanto você digita, e Buscar (ou o botão de
      busca do teclado) mostra os resultados; nomes com acento acham sem acento
- [ ] Dar item: +1/+10/+100 entregam no inventário (itens do jogo e o Rubi Fictício)
- [ ] Com o inventário cheio, o que não couber cai no chão em cima do personagem (o Rubi cai como
      Rubi, com ícone e nome); ao abrir espaço, o personagem pega de volta
- [ ] Invocar: os bosses e o Titã nascem perto do jogador; Os Gêmeos chegam os dois
- [ ] Invencível impede o dano; Curar enche vida e mana; Matar inimigos mata os hostis próximos
      (com loot), sem matar NPCs da cidade
- [ ] A resposta (ex.: "10x Lente entregue (o que não couber cai no chão)") aparece no topo do modal e
      some depois de alguns segundos
- [ ] Desligar o devtools na Oficina de Mods tira o botão das Ferramentas na abertura seguinte

**Receitas de mod**
- [ ] O `tlmod.log` tem `receitas: ... 2 de mods` e as duas linhas `receita tlmod.example_item: ... na posição N`
- [ ] Perto de uma Bancada, com 10 Pedras e 5 Géis, o Rubi Fictício aparece na fabricação e sai com a
      imagem, o nome e a descrição do mod; os 10 Blocos de Pedra e os 5 Géis são gastos
- [ ] Longe da Bancada, a receita some
- [ ] Com 5 Rubis Fictícios, a receita do Rubi do jogo aparece; ao fabricar, os 5 Rubis Fictícios são gastos
- [ ] Só com Lentes (sem Rubis Fictícios), a receita do Rubi **não** aparece; e os Rubis Fictícios não
      contam como Lentes nas receitas do jogo que pedem Lente (ex.: Óculos de Proteção, 2 Lentes num Altar Demoníaco)
- [ ] No Guia, colocar Pedra, Gel ou Rubi Fictício mostra as receitas de mod
- [ ] (Opcional, no Shimmer) Jogar uma Lente no Shimmer não devolve Pedra e Gel

**Abertura do jogo**
- [ ] O jogo abre normalmente várias vezes seguidas (feche e abra umas 10 vezes); se fechar sozinho,
      mande o `tlmod.log`
- [ ] O `tlmod.log` tem a linha "il2cpp_init concluído (N ms depois do corlib)" logo depois de
      "libil2cpp.so em ..."

**Oficina de Mods**
- [ ] O botão **Mods** aparece inteiro, ao lado de Configurações, e abre a Oficina com o som do menu
- [ ] As linhas ficam uma embaixo da outra, dentro do painel, acima da linha do Voltar
- [ ] Cada linha: ícone na moldura da esquerda (Rubi no "Exemplo: Rubi Fictício", Olho de Aparência
      Suspeita no "Titã de Brasa", Livro no Boss Checklist e nas "Ferramentas de teste"), nome, estado colorido, versão e
      o conteúdo ou o autor
- [ ] Tocar em qualquer ponto da linha (nome, quadros, à direita) abre os detalhes; arrastar a
      lista não abre nada
- [ ] Com mais mods do que cabem, arrastar a lista rola e para numa linha inteira
- [ ] Nos detalhes, o texto não passa por cima de Voltar/Desativar; texto longo rola ao arrastar
- [ ] Nos detalhes do "Titã de Brasa", o título fica numa linha só, com folga nas bordas do quadro:
      "Exemplo: Titã de..." (cortado no fim de uma palavra)
- [ ] Com o Titã de Brasa desativado, a linha dele continua com o Olho de Aparência Suspeita
- [ ] Desativar/Ativar muda o estado para "Será desativado"/"Será ativado" (amarelo); reabrindo o
      jogo, o mod aparece Desativado (cinza), com o Livro no lugar do ícone, e volta ao religar
- [ ] Um mod com erro (ex.: apague a `.so` de um mod) mostra "Erro" em vermelho, e os detalhes
      trazem a mensagem inteira
- [ ] Voltar (botão e o voltar do Android) volta dos detalhes para a lista e da lista para o menu
- [ ] O `tlmod.log` tem até cinco linhas "Oficina: toque em ..." (mande junto se algo falhar)

**Cópia das bibliotecas e log**
- [ ] Os 4 mods aparecem como Carregado; o log mostra `biblioteca copiada para .../code_cache/tlmod/...`
- [ ] Na segunda abertura do jogo (sem reinstalar), o log mostra `cópia reaproveitada: ...` para os 4
- [ ] Trocar a `.so` de um mod por outra versão gera uma cópia nova na abertura seguinte
- [ ] `getCodeCacheDir` funciona (nenhum aviso `Context.getCodeCacheDir indisponível` no log)
- [ ] `tlmod/tlmod.log` é criado, começa cada abertura com `===== TLMod: sessão iniciada`, e tem as
      linhas `[ponte]` do início (hooks instalados) e as do runtime e dos mods
- [ ] Se um mod falhar, o erro aparece inteiro nos detalhes da Oficina de Mods e no `tlmod.log`, sem reticências

**Painel**
- [ ] O painel do Log aparece por cima do jogo (OpenGL ES ou Vulkan)
- [ ] Toques fora do painel continuam indo para o jogo; toques no painel não vazam
- [ ] O Log rola, acompanha linhas novas e deixa selecionar e copiar o texto
- [ ] Os acentos aparecem certos (Titã, Fictício)

**Boss Checklist**
- [ ] Num mundo, o status bate com o progresso real (comparar com um mundo conhecido)
- [ ] Depois de derrotar um boss, a lista atualiza (com a gaveta aberta, em até 1 s)

**Rubi Fictício**
- [ ] Os 3 rubis chegam ao entrar no mundo, empilhados, com ícone, nome e tooltip do mod
- [ ] O ícone aparece também no cursor, no lixo, em baús, no hotbar e no item caído no chão
- [ ] Vendido numa loja, rende cerca de 10 de prata (1/5 do valor de 50 de prata, ajustado pela felicidade
      do NPC); recomprar custa cerca de 50 de prata, com o mesmo ajuste
- [ ] Salvar, sair e entrar de novo mantém os rubis, e aparece `<personagem>.plr.tlmod.json` ao lado do save
- [ ] O personagem abre no jogo **sem** o TLMod sem corromper; os rubis não aparecem lá e voltam
      quando o TLMod é usado de novo
- [ ] Na Jornada, a pesquisa recusa o item de mod, e o tooltip dele **não** mostra "Pesquise mais N para
      liberar a Duplicação" (itens do jogo continuam mostrando)
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
| `lib/arm64-v8a/libtlmod_bridge.so` | 470376 | `2abe023313cb4e7bd6a55e6f145200bbe2cd6225356c6399787a9ce6a31df243` |
| `lib/arm64-v8a/libtlmod_runtime.so` | 995712 | `09602b6327dc6479fb27f4e1a9f2d58578e3c5396043b2cb5f8ebe7fec0a223b` |
| `tlmod/mods/boss_checklist/lib/android-arm64/libboss_checklist.so` | 233520 | `a22ec19635ff1ea53a42c3c5186ecd111815235dacffd7b28696389ea19bd51a` |
| `tlmod/mods/boss_checklist/manifest.json` | 633 | `ece3d334283bb47e30eab683150c54d512fba0177b71cac4043c3a6bd08c7fb5` |
| `tlmod/mods/devtools/lib/android-arm64/libdevtools.so` | 490872 | `92fdc9e524e203021e221e3e12d71422a8a75381f134ddfc926b7a64cb9e5d9c` |
| `tlmod/mods/devtools/manifest.json` | 701 | `74cb60b82025d08314dab060767a8ced69c566cfc87a7e3fab06a2a546c0cf2e` |
| `tlmod/mods/example_boss/lib/android-arm64/libexample_boss.so` | 225664 | `e07ca2d00d92d86d52d0c5ff3a47da6e856a7fb7136c7b94afd0d94bf3b57862` |
| `tlmod/mods/example_boss/manifest.json` | 668 | `9264b2a34f56647e5a3cc9fd9f69362cd423c2c6df66d4ba086e537ff34ef38a` |
| `tlmod/mods/example_boss/spawn_on_start` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `tlmod/mods/example_boss/textures/ember_titan.png` | 5801 | `fa4b43f509b4c8e428027936e478b12121cd43db544fc0c2ed932870b15237f9` |
| `tlmod/mods/example_item/give_on_start` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `tlmod/mods/example_item/lib/android-arm64/libexample_item.so` | 226528 | `0d04150ed12ee97ea566ca261d30179dfb671c0db89e675a85f21199b393a6de` |
| `tlmod/mods/example_item/manifest.json` | 614 | `aa787324e10e518b5d3c43848b9fbf1fa349c6f73525ccc51d44eff0ff74375c` |
| `tlmod/mods/example_item/textures/fictional_ruby.png` | 239 | `99abb2c88f7654e329e8e17812ee93a85a91e4810fa0aa8eb4807385b20a4c2a` |
| `tlmod/tlmod_config.json` | 32 | `fe7957a8a201d48e5f07a84daeb118524251c51bbf2a5d699b6f7ce8d1c5fe8a` |
