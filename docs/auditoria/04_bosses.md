# Auditoria 04: o que falta para um boss de mod ser um boss completo

Terraria Mobile 1.4.4.9 (Unity 2021.3.56f2, IL2CPP, Android arm64), comparado com o TLMod da branch
`ccr-bbd92bf9-i01buk` (código da ponte e do runtime no commit `f917b04`; os commits seguintes só
acrescentaram as auditorias).

Auditoria só de leitura. As fontes são:

- o dump regenerado (`dump.cs`, `script.json`, `stringliteral.json`) e a desmontagem da `libil2cpp.so`;
- o código da ponte (`il2cpp_bridge/`), do runtime e da API (`modapi/`) e o mod `example_boss`;
- os READMEs e as auditorias 01 a 03.

Dump, metadata e bibliotecas ficam fora do repositório. Nenhum código foi alterado; este arquivo é o único
acréscimo.

**Módulo 4:** o que falta para um boss de mod ser um boss completo.

1. Barra de vida: como o jogo escolhe e desenha a barra, e como ela depende do tipo do NPC?
2. Música: como o jogo escolhe a música, e onde o TLMod poderia plugar uma escolha do mod?
3. Mensagens, despawn de dia e trocas de fase: o que vem da casca e o que o mod controla quando `OnAI`
   assume o controle total?
4. `NPC.NewNPC` com a origem (`IEntitySource`) nula: é seguro? Quais caminhos leem a origem?
5. Loot: como o jogo decide o loot de um NPC nesta build, e como um mod definiria o próprio loot sem
   soltar o da casca?
6. Bestiário: o que seria preciso para um boss de mod ter entrada própria?

- [Como ler](#como-ler)
- [Resumo](#resumo)
- [1. O boss de mod hoje](#1-o-boss-de-mod-hoje)
- [2. Barra de vida (pergunta 1)](#2-barra-de-vida-pergunta-1)
- [3. Música (pergunta 2)](#3-música-pergunta-2)
- [4. Mensagens, despawn e fases (pergunta 3)](#4-mensagens-despawn-e-fases-pergunta-3)
- [5. NewNPC com origem nula (pergunta 4)](#5-newnpc-com-origem-nula-pergunta-4)
- [6. Loot (pergunta 5)](#6-loot-pergunta-5)
- [7. Bestiário (pergunta 6)](#7-bestiário-pergunta-6)
- [8. Respostas diretas](#8-respostas-diretas)
- [9. Lacunas do TLMod, em ordem de risco](#9-lacunas-do-tlmod-em-ordem-de-risco)
- [10. Ações recomendadas](#10-ações-recomendadas)
- [11. Experimentos no aparelho](#11-experimentos-no-aparelho)

---

## Como ler

**Níveis de confiança**, os mesmos das auditorias anteriores:

- **confirmado no binário**: visto na desmontagem ou na metadata;
- **inferido**: decorre do que foi lido, mas um passo não foi rastreado (chamada virtual, condição em
  tempo de execução, conteúdo de tabela montada em tempo de execução);
- **só o aparelho confirma**: depende do que acontece em tempo de execução. Cada caso tem um
  experimento na [seção 11](#11-experimentos-no-aparelho).

Afirmações sobre código-fonte trazem arquivo e linha e valem como confirmadas no código.

**Casca.** O NPC original (`shell_type`) dentro do qual um NPC de mod roda. O ID de mod (4096 em diante)
nunca chega ao jogo: o hook de `NPC.NewNPC` troca o ID pela casca antes do original
(`npc_hooks.cpp:274`). No jogo, `type` e `netID` do boss de mod são os da casca. Os IDs originais vão
de 1 a 687 nesta build.

**Exemplo usado no texto.** O Titã de Brasa do `example_boss`: casca 4 (Olho de Cthulhu), 12000 de vida,
40 de dano, 14 de defesa, `boss = true`. O `OnAI` devolve `true` na introdução (2 s) e `false` depois,
então a IA do Olho roda nas fases 1 e 2 (`example_boss.cpp:49–73`).

**Campos do NPC** que aparecem nos trechos (offsets do dump):

| campo | offset | campo | offset |
|---|---|---|---|
| `active` | `0x14` | `defDefense` | `0x188` |
| `SpawnedFromStatue` | `0x78` | `life` / `lifeMax` | `0x1A0` / `0x1A4` |
| `type` | `0x154` | `alpha` | `0x1D4` |
| `ai[]` | `0x158` | `boss` | `0x1F6` |
| `timeLeft` | `0x174` | `value` | `0x200` |
| `target` | `0x178` | `netID` | `0x210` |
| `damage` / `defense` | `0x17C` / `0x180` | `townNPC` | `0x21C` |
| `defDamage` | `0x184` | `despawnEncouraged` | `0x258` |

**Esforço.** Dias de trabalho de uma pessoa que já conhece a ponte e o runtime, com build e um teste no
aparelho. Valem para um jogador. Em multiplayer, tudo que depende de saber "este slot é de mod" herda a
lacuna da auditoria 01: a tabela de slots e o `local[]` não vão pela rede.

---

## Resumo

1. **Barra de vida.** Escolhida a cada quadro: o NPC ativo mais perto do jogador, num raio de 5000 px da
   tela, cuja barra valide. A barra é escolhida pelo `netID` (26 `netID`s têm barra própria, ou nenhuma;
   os outros usam a barra comum). A barra comum só existe se `GetBossHeadTextureIndex` não devolver −1, um índice
   que sai do `type`. Ela não lê o campo `boss`. Hoje o Titã ganha a barra comum com a vida do mod e o
   ícone do Olho, que muda para o da fase 2 quando `ai[0] >= 2`. Numa casca sem ícone de boss, não há
   barra. A barra do mobile não escreve nome. Confirmado no binário.
2. **Música.** `Main.UpdateAudio` decide a cada quadro em `UpdateAudio_DecideOnNewMusic`. O primeiro NPC,
   pela ordem dos slots, que tenha categoria de música e esteja a até 5000 px da tela decide. A categoria
   sai do `type`; se o `type` não tem uma, `boss` vale "Boss 1" (`MusicID` 5). O ponto de plugue é logo
   depois dessa decisão, antes da caixa de música. Só existem as faixas 1 a 91. Confirmado no binário.
3. **Mensagens.** "X despertou!" sai de `SpawnBoss`, `SpawnOnPlayer`, `SpawnSkeletron` e `SpawnWOF`.
   `NewNPC` só anuncia o Slime Rei. O `spawn_npc` do TLMod chama `NewNPC`, então não há anúncio. "X foi
   derrotado!" sai de `DoDeathEvents` quando `boss` é verdadeiro, já com o nome do mod. Confirmado no
   binário.
4. **Despawn.** `CheckActive` reinicia `timeLeft` para 750 enquanto há um jogador por perto e, para um
   boss, só desativa depois de 750 ticks fora desse alcance. Fugir de dia é trabalho da IA de cada boss.
   Com `OnAI` sempre `true`, o boss de mod nunca foge de dia, e a API não expõe `timeLeft` nem `active`.
5. **Fases.** Com `OnAI` devolvendo `false`, a IA da casca manda em `ai[]`, dano e defesa. Na fase 2 do
   Olho ela reescreve dano (23 no Normal; 18 ou 20 no Expert) e defesa (0, −15 ou −30) a cada tick: os
   40/14 do Titã se perdem. A vida do mod também ignora Expert e Mestre, porque `ScaleStats` roda dentro
   do `SetDefaults`, antes de o runtime gravar a vida. Confirmado no binário.
6. **Origem nula.** É segura. `NewNPC` e `Item.NewItem` nunca leem o registrador da origem.
   `IEntitySource` é uma interface vazia. Em todo o binário, só dois métodos testam o tipo de uma origem,
   e o único que um spawn de mod alcança (`Projectile.FindBannerToAssociateTo`) aceita nulo. Confirmado no
   binário.
7. **Loot.** `NPC.NPCLoot` → `NPCLoot_DropItems` → `ItemDropResolver.TryDropping`, que junta as regras
   globais e as do `netID`, o da casca. Em seguida, `DoDeathEvents` decide pelo `type`. Matar o Titã solta
   o loot do Olho e grava `NPC.downedBoss1` no mundo; conquista e bestiário vão para o Olho. Cada etapa de
   `NPCLoot` tem um só chamador: dá para interceptar etapa por etapa. Confirmado no binário.
8. **Bestiário.** Tudo é por `netID`: entradas, crédito de abate, sincronização. Uma entrada própria exige
   preencher cinco dicionários de `ContentSamples` para o ID de mod, registrar a entrada e resolver o
   ícone (que chama `SetDefaults(id)` e a `DrawNPCDirect` de 4 parâmetros, sem hook). O passo mínimo é
   parar de creditar a casca.

**Esforço, por item:**

| item | caminho | esforço |
|---|---|---|
| 1. barra | aviso de casca com barra especial; ícone próprio; barra para casca sem ícone | 2 dias |
| 2. música | campo `music` e um hook depois das duas decisões de música | 0,5–1 dia |
| 3. mensagens, despawn, fases | `announce`, `spawn_boss`, `time_left`/despawn, `on_post_ai`, escala por dificuldade | 3–4 dias |
| 4. origem nula | nada no código; fechar o TODO e a frase do README | 0,1 dia |
| 5. loot | hooks nas etapas de `NPCLoot` e tabela de loot do mod | 2,5–3 dias |
| 6. bestiário | crédito próprio (passo 1); entrada própria (passo 2) | 0,5 dia + 3–5 dias |

Juntos: de 12 a 16 dias. Uma faixa de música própria, fora disso, custa mais 3 a 5 dias
([seção 3.5](#35-caminho-de-implementação)).

---

## 1. O boss de mod hoje

| aspecto | quem decide hoje | onde |
|---|---|---|
| `type` e `netID` no jogo | casca | `npc_hooks.cpp:274`: o ID de mod vira a casca antes do `NewNPC` |
| vida, dano, defesa | mod, gravados depois do `NewNPC` | `mod_loader.cpp:392–396` |
| comportamento | mod (`on_ai` → `true`) ou casca (`false`) | `npc_hooks.cpp:307–308` |
| textura e quadro | mod | hook da `DrawNPCDirect` de 6 parâmetros (`0xBFC154`) |
| nome | mod | hooks de `NPC.get_TypeName` e `NPC.GetTypeNetName` (`screen_hooks.cpp:519`, `:526`) |
| campo `boss` | casca (`SetDefaults`) | `TLModNpcDef.boss` é "reservado" (`tlmod_api.h:178`), guardado (`mod_loader.cpp:552`) e nunca escrito no NPC |
| barra, música, anúncios, despawn, loot, progresso, bestiário | casca | seções 2 a 7 |

O que o mod lê e escreve a cada tick é o `TLModNpcState`: posição, velocidade, `life`, `life_max`,
`damage`, `defense`, `target`, `ai[0..3]`, `local[8]` e `frame` (`tlmod_api.h:148–162`). A ponte grava
de volta no NPC os campos do jogo (`npc_hooks.cpp:192`), antes da IA da casca; `local[]` e `frame` ficam
no runtime. Não há na API: `timeLeft`, `active`, `boss`, `value`, som, projétil, poeira ou gore, mensagem
no chat, despawn.

Dos métodos citados nas seções 2 a 7, só seis estão em `terraria_offsets.h`, todos já hookados:
`NPC.NewNPC`, `NPC.SetDefaults`, `NPC.AI`, a `DrawNPCDirect` de 6 parâmetros, `NPC.get_TypeName` e
`NPC.GetTypeNetName`. A `DrawNPCDirect` de 4 parâmetros (`0xBCB734`) aparece só num comentário
(`terraria_offsets.h:618`).

---

## 2. Barra de vida (pergunta 1)

### 2.1 Quem escolhe o NPC

`BigProgressBarSystem.Update` (`0x97F764`) roda a cada quadro, chamado por `Main.UpdateUIStates`
(`0xCB11D4`). Quando não há barra ativa, chama `TryFindingNPCToTrack` (`0x97F824`). Quando há, revalida
a barra; se a validação falhar, a barra cai (`str xzr, [x19, #0x10]` em `0x97F814`).

`TryFindingNPCToTrack`:

```
0x97f91c: mov  w1, #0x1388              ; 5000 px
0x97f920: mov  w2, #0x1388
0x97f928: bl   Rectangle.Inflate        ; a tela aumentada 5000 px para cada lado
0x97f968: ldrb w8, [x21, #0x14]         ; npc.active
0x97f978: bl   Entity.get_Hitbox
0x97f9a8: bl   Rectangle.Intersects
0x97f9dc: bl   Entity.Distance          ; até o centro do jogador local
0x97f9e0: fcmp s10, s0                  ; mais perto que o melhor até agora?
0x97f9f4: bl   BigProgressBarSystem.TryTracking
0x97f9fc: fcsel s10, s8, s10, ne        ; só conta se a barra validou
```

Ganha o NPC mais perto cuja barra valide. Enquanto essa barra validar, nenhum outro NPC toma o lugar.
Confirmado no binário.

### 2.2 Qual barra: depende do `netID`

`TryTracking` (`0x97FAF4`):

```
0x97fbac: ldr  w1, [x8, #0x210]         ; npc.netID
0x97fbb0: ldr  x20, [x19, #0x18]        ; _bossBar: a barra comum, o padrão
0x97fbbc: bl   Dictionary<int, IBigProgressBar>.TryGetValue
0x97fbc8: csel x20, x8, x20, ne         ; achou: a barra especial
0x97fc30: blr  x8                       ; ValidateAndCollectNecessaryInfo
0x97fc3c: str  x20, [x19, #0x10]        ; vira a barra ativa
```

O dicionário é preenchido no construtor (`0x97FE60`), com 26 `netID`s:

| barra | `netID`s |
|---|---|
| Gêmeos | 125, 126 |
| Devorador de Mundos | 13, 14, 15 |
| Cérebro de Cthulhu | 266 |
| Golem | 245, 246 |
| Senhor da Lua | 396, 397, 398 |
| pilares (solar, vórtice, nebulosa, poeira estelar) | 517, 422, 507, 493 |
| navio pirata | 491, 492 |
| disco marciano | 393, 394, 395 |
| Deerclops | 668 |
| nunca válida | 68, 249, 440, 548, 549 |

Todo o resto usa a barra comum (`CommonBossBigProgressBar`). Confirmado no binário.

### 2.3 Barra comum: validação e desenho

`CommonBossBigProgressBar.ValidateAndCollectNecessaryInfo` (`0x9832F4`):

```
0x983384: bl   NPC.GetBossHeadTextureIndex
0x983388: cmn  w0, #1
0x98338c: b.eq 0x9833a8                 ; −1: barra inválida
0x983390: ldr  d0, [x20, #0x1a0]        ; life e lifeMax do próprio NPC
0x983394: str  w0, [x19, #0x20]         ; guarda o índice do ícone
```

`NPC.GetBossHeadTextureIndex` (`0xEDC338`) parte de `NPCID.Sets.BossHeadTextures[type]` e trata alguns
`type`s: 4 (Olho), 125 e 126 (Gêmeos), 262 (Plantera), 266 (Cérebro), 439 e 440 (Cultista). O caso do
Olho:

```
0xedc384: ldr  x9, [x8, #0x1b8]         ; NPCID.Sets.BossHeadTextures
0xedc38c: ldrsw x8, [x19, #0x154]       ; type
0xedc3a0: ldr  w0, [x9, #0x20]          ; BossHeadTextures[type]
0xedc3f8: cmp  w8, #4
0xedc3fc: b.eq 0xedc460
0xedc470: ldr  s0, [x8, #0x20]          ; ai[0]
0xedc474: fmov s1, #2.0
0xedc47c: csinc w0, w0, wzr, lt         ; ai[0] >= 2: ícone 1 (o Olho de boca)
```

`CommonBossBigProgressBar.Draw` (`0x9833C8`):

```
0x983438: ldr  x8, [x8, #0xd0]          ; TextureAssets.NpcHeadBoss
0x983440: ldrsw x9, [x19, #0x20]        ; o índice guardado na validação
0x9834a0: bl   Utils.Frame              ; a textura inteira (1x1 quadro)
0x9834c0: ldp  s0, s1, [x19, #0x10]     ; life, lifeMax
0x9834e0: b    GUIBossBar.Draw          ; (life, lifeMax, ícone, quadro, −1, 1)
```

`GUIBossBar.Draw` (`0x14B8B44`) desenha o painel, o ícone e a barra, e escreve só números:
`StringBuilder.Append(int)`, `Append(string)`, `Append(int)` em `0x14B923C`–`0x14B92B0`. Não há nome.

Nenhum método das barras lê `boss` (`0x1F6`). A varredura de leituras desse campo está na
[seção 4.3](#43-fases-o-que-a-casca-faz-e-o-que-o-mod-controla). Confirmado no binário.

### 2.4 O boss de mod hoje

- **Casca com ícone e sem barra especial** (o Titã, casca 4): barra comum, com a vida do mod
  (`0x983390` lê `life`/`lifeMax`, que o runtime escreveu) e o ícone da casca. Com a IA do Olho rodando,
  o ícone vira o da fase 2 quando ela muda `ai[0]`. Com `OnAI` sempre `true`, o ícone segue o que o mod
  escrever em `ai[0]`. Confirmado no binário; o desenho, só o aparelho confirma
  ([E1](#e1-barra-e-ícone)).
- **Casca sem ícone** (`BossHeadTextures[type] = −1`, por exemplo um inimigo comum): sem barra, mesmo com
  `def.boss = true`. Confirmado no binário.
- **Casca com barra especial**: a barra dela, com as regras dela. A dos Gêmeos, por exemplo, procura o
  outro gêmeo. Inferido.
- **README.** `modapi/README.md:94` diz que o nome do mod aparece "na barra de vida". No mobile, a barra de
  boss não tem nome.

### 2.5 Caminho de implementação

1. **Validar a casca no `register_npc`** (0,25 dia): com `boss = true`, avisar no log se `shell_type` está
   entre os 26 `netID`s da tabela da 2.2 ou se `BossHeadTextures[shell_type]` é −1.
2. **Ícone próprio na barra** (1 dia). Acrescentar `boss_icon` (PNG) ao `TLModNpcDef` e hookar
   `CommonBossBigProgressBar.Draw` (`0x9833C8`). O parâmetro `ref BigProgressBarInfo info` chega em `x1`,
   e o primeiro campo dele é `npcIndexToAimAt`. Se esse slot é de mod e tem ícone:
   - trocar `TextureAssets.NpcHeadBoss[índice]` pelo `Asset` do mod;
   - chamar o original;
   - devolver o `Asset` da casca.

   É a mesma técnica que a ponte já usa com `TextureAssets.Npc` no `DrawNPCDirect`. Só vale para a barra.
3. **Barra para casca sem ícone, e ícone no mapa** (1,5–2 dias):
   - estender o array `TextureAssets.NpcHeadBoss` com os ícones de mod;
   - hookar `GetBossHeadTextureIndex` (`0xEDC338`) para devolver o índice estendido quando o NPC é um boss
     de mod.

   O índice também é lido por `GUIMap.DrawMap` (6 chamadas), então o mapa passa a mostrar o ícone do mod.
   O array tem 15 leitores: as 10 barras, o mapa, `Main.DrawNPCHeadBoss` e 3 rotinas de carga. As de carga
   rodam antes do TLMod estender o array (inferido). Substitui o passo 2.
4. Escrever `npc.boss = true` não é necessário para a barra.

**Esforço:** 2 dias (passos 1 e 3).

---

## 3. Música (pergunta 2)

### 3.1 Onde o jogo decide

`Main.UpdateAudio` (`0xCA0024`) é chamado por `Main.DoUpdate` (dois pontos) e por
`XNAUnityRunner.OnApplicationPause`: só na thread principal. A ordem dentro dele:

```
0xca03c4: bl   Main.UpdateAudio_DecideOnNewMusic   ; ou 0xca03d0
0xca036c: bl   Main.UpdateAudio_DecideOnTOWMusic   ; ou 0xca03d8: trilha Otherworld (mundo "drunk", ou a troca de trilha)
0xca045c: ldr  w9, [x9, #0xe0]                     ; Main.musicBox2
0xca0488: str  w9, [x0, #0x2c]                     ; SceneMetrics.ActiveMusicBox
0xca04e8: str  w9, [x8, #0x458]                    ; caixa de música → Main.newMusic (uma linha por caixa)
```

Depois vem a troca gradual de `Main.curMusic` para `Main.newMusic`. A caixa de música sempre vence a
decisão. Confirmado no binário.

### 3.2 A regra por NPC

`UpdateAudio_DecideOnNewMusic` (`0xCA3580`) percorre `Main.npc[0..199]`:

```
0xca3884: ldrb w8, [x8, #0x14]          ; active
0xca38bc: ldr  w8, [x8, #0x154]         ; switch (type) → categoria e raio
0xca3b84: ldrb w9, [x9, #0x20]          ; Sets.BelongsToInvasionOldOnesArmy[type]
0xca3b8c: csel w25, w25, w20, eq        ; → categoria 12
0xca3b90: cbnz w25, 0xca3bc8
0xca3bc4: ldrb w25, [x8, #0x1f6]        ; sem categoria: boss → categoria 1
0xca3db0: bl   Rectangle.Intersects     ; centro do NPC ± raio (5000 px) contra a tela
0xca3db4: tbnz w0, #0, 0xca3dd4         ; o primeiro que cruza decide e encerra o laço
...
0xca42e4: cbz  w29, 0xca4444            ; categoria 1?
0xca42e8: mov  w9, #5                   ; MusicID 5, "Boss 1"
0xca42ec: str  w9, [x8, #0x458]         ; Main.newMusic
```

- **Categoria pelo `type`:** o `switch` dá categoria a uma lista de tipos: Devorador, Parede de Carne,
  invasões, os mecânicos, Abelha Rainha, Plantera, Golem, Duque Peixe-Dragão, Senhor da Lua e outros. O
  Olho (4) não está entre eles.
- **Categoria por `boss`:** só quando o `type` não deu nenhuma. Vale categoria 1, música 5.
- **Raio:** 5000 px, salvo um caso de 1600.
- **Ordem:** o primeiro NPC, pela ordem dos slots, cuja área cruza a tela.
- **Prioridade:** cada categoria tem a sua faixa, e eventos e o modo "for the worthy" mudam a escolha.

Confirmado no binário.

### 3.3 As faixas

```
0x15d64b8: mov  w1, #0x5c               ; LegacyAudioSystem: AudioTracks = new Cue[92]
0xda85e4: cmp  w8, #0x5b                ; Main.LoadMusic_InSteps: carrega as faixas 1..91 no início
0xbc47f0: bl   Cue..ctor(SoundEffect)   ; SoundBank.LoadMusicId: ContentManager.Load("…Music_" + id)
```

`Main.musicFade` também tem uma posição por faixa (o laço de `0xCA36E8` a `0xCA3768` passa pelas 92), e
`Main.musicNoCrossFade` é indexado pelo número da faixa (`0xCA42A0`). Qualquer `MusicID` de 1 a 91 toca
sem carga extra. Uma faixa própria exige:

- criar um `SoundEffect` a partir de um arquivo do mod e um `Cue` em volta dele;
- aumentar `AudioTracks`, `musicFade` e `musicNoCrossFade`.

Confirmado no binário; a faixa própria, só o aparelho confirma.

### 3.4 O boss de mod hoje

- **Casca do Olho:** `boss = true` vem da casca, então a música é "Boss 1". Confirmado no binário.
- **Casca que não é boss:** segue a música do bioma.
- **Outro boss num slot menor e perto da tela:** ele decide.

O `def.boss` do mod não muda nada disso (`mod_loader.cpp:552`).

### 3.5 Caminho de implementação

1. **Campo `music` no `TLModNpcDef`** (`MusicID` 1 a 91; 0 = o da casca). O runtime guarda a lista de
   slots de mod com música.
2. **Hook depois de `UpdateAudio_DecideOnNewMusic` (`0xCA3580`) e de `UpdateAudio_DecideOnTOWMusic`
   (`0xCA7144`).** Chamar o original. Depois, se algum slot de mod com música está ativo e o centro dele
   ± 5000 px cruza a tela, gravar `Main.newMusic` (estático `+0x458`). A caixa de música continua vencendo,
   porque é aplicada depois.

   Custo: um laço sobre os slots de mod por quadro, desprezível perto do laço de 200 NPCs do original
   (inferido). Política sugerida: o boss de mod vence a música de bioma e a de outro boss.
3. **Faixa própria** (opcional, 3–5 dias, só o aparelho confirma): os passos da 3.3, carregando o arquivo do
   mod antes do primeiro `UpdateAudio` de um mundo.

**Esforço:** 0,5–1 dia (passos 1 e 2).

---

## 4. Mensagens, despawn e fases (pergunta 3)

### 4.1 Mensagens

| mensagem | quem emite | nome usado | hoje, com o TLMod |
|---|---|---|---|
| "X despertou!" | `NPC.SpawnBoss` (`0xF3ED08`) | `get_TypeName` (`0xF3EF4C`, um jogador) ou `GetTypeNetName` (`0xF3F1C0`, servidor) | não sai: `spawn_npc` chama `NewNPC` |
| "X despertou!" | `SpawnOnPlayer` (`0xEE52F4`), `SpawnSkeletron`, `SpawnWOF` | não rastreado | não sai, pelo mesmo motivo |
| "X despertou!" dentro de `NewNPC` | só `Type == 50`, o Slime Rei (`0xEE4984`) | `GetTypeNetName` (`0xEE4A0C`) ou `get_TypeName` (`0xEE4B04`) | numa casca 50 sairia com o nome da casca (abaixo) |
| "X foi derrotado!" | `DoDeathEvents_CelebrateBossDeath` (`0xF3DD94`), só com `boss` | `GetTypeNetName` (`0xF3E128`) ou `get_TypeName` (`0xF3E1DC`) | sai com o nome do mod se a casca é boss |

Um jogador ou servidor: `Main.netMode` (estático `+0x81C`).

O texto é `Announcement.HasAwoken` (`0x2B354A8`), usado nesses cinco métodos e em nenhum outro. A derrota
usa `Announcement.HasBeenDefeated_Single` (`0x2B354B8`).

**Por que a casca 50 anunciaria com o nome da casca.** Dentro de `NewNPC` o anúncio roda antes de o
original voltar, e o runtime só marca o slot como de mod no pós (`npc_hooks.cpp:281`). O hook de nome
ainda não reconhece o slot.

**`SpawnBoss` com um ID de mod funciona** (inferido; só o aparelho confirma):

```
0xf3ee74: mov  w3, w19                  ; Type como veio: o hook troca pela casca
0xf3ee78: bl   NPC.NewNPC
0xf3ef38: ldr  w8, [x0, #0x174]
0xf3ef3c: str  w21, [x0, #0x178]        ; target = targetPlayerIndex
0xf3ef40: add  w8, w8, w8, lsl #2
0xf3ef44: lsl  w8, w8, #2               ; timeLeft *= 20
0xf3ef4c: bl   NPC.get_TypeName         ; o slot já está marcado: nome do mod
```

O `Type` só é comparado com constantes (`0x7F`, `0x7D`, `0x32`, `0x52`, `0x13C`, `0x18E`, `0x226`,
`0x227`, `0x296`); nenhum array é indexado por ele. Um ID de mod cai no caminho comum do anúncio.

A derrota só é anunciada se `boss` for verdadeiro:

```
0xf3c96c: ldrb w8, [x19, #0x1f6]        ; boss
0xf3c970: cbz  w8, 0xf3c9e4
0xf3c978: bl   NPC.DoDeathEvents_DropBossPotionsAndHearts
0xf3c980: bl   NPC.DoDeathEvents_CelebrateBossDeath
```

Os dois métodos de nome são hookados pela ponte. Confirmado no binário.

### 4.2 Despawn

`NPC.CheckActive` (`0xF368DC`), chamado só por `NPC.UpdateNPC` (`0xF4B478`), a cada tick:

```
0xf36964: cbz  w8, 0xf36974             ; townNPC: nunca some
0xf36c64: ldr  w8, [x8, #0x74]          ; NPC.activeTime = 750
0xf36c68: strb wzr, [x19, #0x258]       ; despawnEncouraged = false
0xf36c6c: str  w8, [x19, #0x174]        ; jogador por perto: timeLeft = 750
0xf36cd4: ldrb w9, [x19, #0x1f6]        ; boss
0xf36ce4: orr  w23, w23, w9             ; boss conta como "jogador por perto"
0xf36f30: sub  w8, w8, #1               ; timeLeft--
0xf36f3c: and  w9, w23, w9              ; fica se (por perto ou boss) e timeLeft > 0
0xf36f88: strb wzr, [x19, #0x14]        ; senão: active = false (e SendData 23 no servidor)
```

O valor 750 vem do construtor estático de `NPC` (`0xF5E76C`, constante em `0x22A9D88`).

- **Perto de um jogador:** `timeLeft` volta a 750 a cada tick, e nada some por `CheckActive`.
- **Longe:** um boss dura 750 ticks (12,5 s). `SpawnBoss` multiplica esse tempo por 20.
- **Fugir de dia ou com o alvo morto** é trabalho da IA de cada boss. Ela se afasta e chama
  `NPC.EncourageDespawn` (`0xEDA87C`: `timeLeft = min(timeLeft, n)` e `despawnEncouraged = true`), por
  exemplo em `0xDEF30C`, dentro de `NPC.AI`.

Confirmado no binário. Não rastreei, no `switch` de 118 mil instruções do `NPC.AI`, o trecho do Olho que
foge de dia (inferido; [E3](#e3-despawn-de-dia)).

**Boss de mod com `OnAI` sempre `true`:**

- não foge de dia nem quando o jogador morre;
- some só depois de 12,5 s fora do alcance;
- a API não tem como apressar isso: `TLModNpcState` não tem `timeLeft` nem `active`, e não há função de
  despawn;
- o mod já sabe se é dia (`get_time`, `tlmod_api.h:348`).

Com `false`, a fuga é a da casca.

### 4.3 Fases: o que a casca faz e o que o mod controla

**`OnAI` devolve `true`.** A IA da casca não roda. Não há fases dela, sons de ataque, servos, fuga ou
mudança de dano. Continuam rodando, porque estão em `UpdateNPC` ou em outros métodos que leem o `type` da
casca:

- movimento e colisão (`UpdateNPC`, depois da IA);
- `CheckActive`;
- o efeito de golpe e o de morte da casca (`HitEffect`), que soltam sangue e gore da casca. Inferido: não
  rastreei `HitEffect`.

O mod controla o que o `TLModNpcState` tem (seção 1). Não tem API para som, projétil, poeira, gore, chat
ou despawn.

**`OnAI` devolve `false`.** A IA da casca roda depois do `OnAI` (`npc_hooks.cpp:307–308`) e usa `ai[]`
para as fases dela. Na fase 2 do Olho, ela reescreve dano e defesa a cada tick:

```
0xdee458: str  wzr, [x26, #0x180]       ; defense = 0
0xdee478: bl   Main.get_expertMode
0xdee484: fmov s0, #18.0                ; Expert: dano 18 ou 20
0xdee4a0: mov  w8, #-0xf                ; Expert: defesa −15 ou −30
0xdee4ac: str  w8, [x26, #0x180]
0xdee4b0: fmov s0, #23.0                ; Normal: dano 23
0xdee4c0: bl   NPC.GetAttackDamage_LerpBetweenFinalValues
0xdee4c4: str  w0, [x26, #0x17c]        ; damage
0xdee4d4: bl   NPC.GetAttackDamage_ScaledByStrength
0xdee4dc: str  w0, [x26, #0x17c]
```

Os números 23, 18 e 20 e as defesas 0, −15 e −30 são os da fase 2 do Olho.

O runtime lê o estado, o mod o altera e o runtime o escreve de volta antes da IA da casca
(`mod_loader.cpp:406–417`). Na fase 2 do Olho, os 40/14 do Titã viram 23/0 e o mod não consegue
reaplicá-los. Outras IAs fazem `damage = defDamage` (`0xDD3A88`, `0xDD4308`), e o runtime não grava
`defDamage` nem `defDefense`: essas IAs voltariam ao dano da casca. Confirmado no binário; o efeito no
Titã, só o aparelho confirma ([E2](#e2-dano-e-defesa-na-fase-2)).

**Escala por dificuldade.** `NPC.ScaleStats` (`0xEDD4D8`), a escala de Expert, Mestre e multiplayer, roda
dentro de `SetDefaults` (`0xDB4610`), que roda dentro de `NewNPC`. O runtime grava `def->lifeMax` depois
do `NewNPC` (`mod_loader.cpp:392`). O Titã tem 12000 de vida em qualquer modo, e o dano e a defesa do mod
também não escalam. Confirmado no binário.

**O campo `boss` é lido por cerca de 30 métodos do jogo.** Os relevantes aqui:

- `DoDeathEvents`, `NPCLoot`, `CountKillForAchievements`;
- `CheckActive`, `UpdateNPC`, `AnyDanger`, `Wiring.Teleport`;
- as duas decisões de música;
- `BestiaryEntry.Enemy` e `IsNPCValidForBestiaryKillCredit`;
- as condições de loot `LegacyHack_IsABoss`, `IsBossAndExpert` e `IsBossAndNotExpert`;
- `Main.UpdateTime`, `Player.GetRespawnTime`, `LanternNight.BossIsActive`.

É a chave de "comportamento de boss" fora da barra. Gravá-lo numa casca que não é boss liga tudo isso de
uma vez. Confirmado no binário: varredura de `ldrb wN, [xM, #0x1f6]`. Só `NPC.boss` tem esse offset no
dump; descartei os acertos fora de `Terraria` e os que são índices constantes de array
(`Item.SetDefaults5`, `WorldGen.CheckUnderwaterPlant`, `CreativePowers`).

### 4.4 Caminho de implementação

1. **`announce(ctx, texto, r, g, b)`** (0,5 dia). Num jogador, `Main.NewText` (`0xC9FCF4`). No servidor,
   `ChatHelper.BroadcastChatMessage(NetworkText.FromLiteral(texto), cor, -1)` (`0x765498`); a ponte já
   resolve `NetworkText.FromLiteral` para o hook de nome. Só na thread do jogo.
2. **`spawn_boss(ctx, tipo, x, y)`** (0,5 dia). Chama `NPC.SpawnBoss(x, y, tipo, jogador)`, que anuncia
   "X despertou!" com o nome do mod, multiplica `timeLeft` por 20 e sincroniza. `SpawnBoss` não devolve o
   índice: a ponte o pega do pós-`NewNPC`. Só o aparelho confirma ([E4](#e4-anúncios)).
3. **Despawn** (0,5–1 dia):
   - `time_left` no `TLModNpcState` (`+0x174`), lido e escrito como os outros campos;
   - `despawn_npc(ctx, índice)`: `active = false`, `life = 0` e `NetMessage.SendData(23)` no servidor,
     como o fim de `CheckActive`.

   O exemplo passa a mostrar a fuga de dia com `get_time`.
4. **`on_post_ai`** (0,5 dia). Callback opcional chamado depois do original no hook de `NPC.AI`. O mod
   reaplica dano e defesa, ou lê as fases da casca. Documentar que, com `false`, `ai[]`, `damage` e
   `defense` são da casca.
5. **Escala por dificuldade** (1 dia):
   - no pré-`NewNPC`, guardar numa variável da thread "o próximo `SetDefaults` é do mod X";
   - hookar `ScaleStats` (`0xEDD4D8`): antes do original, pôr `lifeMax`, `damage` e `defense` do mod no NPC,
     para o jogo escalar como faz com os originais;
   - tirar a gravação de `mod_loader.cpp:392–394`.

   Só o aparelho confirma os três modos.
6. **Fazer `def.boss` valer** (0,25 dia). Escrever `boss` no pós-`NewNPC` quando `def.boss` diverge da
   casca, ciente da lista acima.

**Esforço:** 3–4 dias.

---

## 5. NewNPC com origem nula (pergunta 4)

### 5.1 `NewNPC` não lê a origem

`NPC.NewNPC(IEntitySource source, int X, int Y, int Type, …)` (`0xEE4630`). A origem chega em `x0`:

```
0xee4668: mov  w20, w5                  ; Target
0xee466c: mov  v8.16b, v3.16b           ; ai3..ai0 (v8..v11)
0xee467c: mov  w19, w4                  ; Start
0xee4680: mov  w21, w3                  ; Type
0xee4684: mov  w23, w2                  ; Y
0xee4688: mov  w22, w1                  ; X        (x0 não é guardado)
0xee468c: tbnz w8, #0, 0xee46ec
0xee4690: adrp x0, …                    ; caminho de inicialização: x0 sobrescrito
0xee46ec: ldr  x0, [x26]                ; caminho normal: x0 sobrescrito
```

Os dois caminhos sobrescrevem `x0` antes de qualquer leitura. Confirmado no binário.

### 5.2 Quem lê uma origem no jogo

`IEntitySource` é uma interface vazia (`public interface IEntitySource {}` no dump): não há método virtual
que leia uma origem. Ler uma origem só é possível testando o tipo dela.

As 203 referências ao `TypeInfo` das 29 classes `EntitySource_*`, em 92 métodos, foram verificadas. Quase
todas criam origens. Só dois métodos testam o tipo de uma origem (`typeHierarchyDepth`, `+0x12C`):

| método | tipo testado | quem chama | nula |
|---|---|---|---|
| `Projectile.FindBannerToAssociateTo` (`0x1109B9C`) | `EntitySource_Parent` | só `Projectile.NewProjectile` | `cbz x20` em `0x1109BF0`: retorna |
| `Player.SpawnMinionOnCursor` (`0x103BE6C`) | `EntitySource_ItemUse` | só `Player.ItemCheck_Shoot`, com origens criadas ali | não se aplica |

Os outros testes de tipo nesses 92 métodos são sobre tile entities (`TEWeaponsRack`, `TEHatRack`,
`TEItemFrame`, `TEFoodPlatter`) e renderizadores. Nenhum código referencia os `TypeInfo` de
`AEntitySource_Tile` e `AEntitySource_OnHit`. Confirmado no binário.

### 5.3 `Item.NewItem` e `Projectile.NewProjectile`

- **`Item.NewItem`:** a sobrecarga principal (`0xBA8CDC`) sobrescreve `x0` em `0xBA8D30` (inicialização)
  ou em `0xBA8DEC` (caminho normal), sem lê-lo. As duas sobrecargas com `Vector2` (`0xBB7AA0`,
  `0xBB7BA4`) também. A ponte já passa nula ao soltar itens perto do jogador (`tools.cpp:153`).
  Confirmado no binário.
- **`Projectile.NewProjectile`:** a origem só serve para associar um estandarte ao projétil. Nula = sem
  estandarte.

### 5.4 Conclusão e caminho

É seguro. Fechar o TODO de `npc_hooks.cpp:212` e a frase "confirmar no jogo que `NewNPC` aceita `source`
nulo" de `modapi/README.md:108–110`. Se um dia um mod criar projéteis, nula também serve.

**Esforço:** 0,1 dia (só documentação).

---

## 6. Loot (pergunta 5)

### 6.1 A ordem em `NPC.NPCLoot`

`NPC.NPCLoot` (`0xF38944`) é chamado por `checkDead` (dois pontos), `DropEoWLoot` e o simulador de loot:

```
0xf389f8: bl   Main.get_NetClientOnly             ; no cliente, não roda
0xf38aa4: bl   Player.FindClosest
0xf38ac4: bl   NPC.CountKillForAchievements
0xf38acc: bl   NPC.GetWereThereAnyInteractions    ; se algum jogador acertou:
0xf38b1c: bl   NPCKillsTracker.RegisterKill       ;   crédito no bestiário
0xf38b24: bl   NPC.CountKillForBannersAndDropThem ;   estandartes
0xf38b54: ldrb w8, [x19, #0x78]                   ; SpawnedFromStatue: regras de estátua
0xf38c6c: bl   NPC.DoDeathEvents_BeforeLoot
0xf38c78: bl   NPC.NPCLoot_DropItems
0xf38c84: bl   NPC.DoDeathEvents
0xf38c94: bl   NPC.NPCLoot_DropMoney
0xf38ca0: bl   NPC.NPCLoot_DropHeals
```

Cada uma dessas etapas privadas tem um só chamador, o próprio `NPCLoot`: `DropItems`, `DoDeathEvents`,
`DropMoney`, `DropHeals`, `BeforeLoot` e `CountKillForAchievements`. `RegisterKill` e
`CountKillForBannersAndDropThem` têm outros chamadores. Confirmado no binário.

### 6.2 As regras: pelo `netID`

`NPCLoot_DropItems` (`0xF3B438`) monta um `DropAttemptInfo` (`npc`, `player`, `rng`,
`IsInSimulation = false`, `IsExpertMode`, `IsMasterMode`) e chama `Main.ItemDropSolver.TryDropping`
(`0x7E558C`):

```
0x7e55e0: ldr  w1, [x8, #0x210]         ; info.npc.netID
0x7e55e4: mov  w2, #1                   ; includeGlobalDrops = true
0x7e55ec: bl   ItemDropDatabase.GetRulesForNPCID
0x7e5634: bl   ItemDropResolver.ResolveRule        ; para cada regra
```

`GetRulesForNPCID` (`0x7D853C`) devolve uma lista nova com `_globalEntries` e
`_entriesByNpcNetId[netID]`. Também é usado por `BestiaryDatabase.ExtractDropsForNPC`: o bestiário lista
os drops pelas mesmas regras. Confirmado no binário.

As regras do Olho vêm de `ItemDropDatabase.RegisterBoss_EOC` (`0x7E1284`), parte da montagem do banco:

- a bolsa do boss (`ItemDropRule.BossBag(3319)`);
- três drops do Mestre: 4924 (relíquia), 3763 e 4798 (mascote, para todos os jogadores);
- por condição: máscara (2112), binóculos (1299), minério e sementes do carmesim (880, 2171) ou da
  corrupção (56, 59) e flechas profanas (47).

Confirmado no binário. Quais condições valem em cada modo, inferido.

### 6.3 O que a casca faz quando o boss de mod morre

`DoDeathEvents` (`0xF3B52C`) é um `switch` pelo `type`:

```
0xf3b724: ldr  w8, [x19, #0x154]        ; switch (type)
0xf3bba8: cmp  w8, #4
0xf3bbc8: mov  w1, #0xd                 ; evento 13
0xf3bbcc: add  x0, x8, #0x8e            ; ref NPC.downedBoss1
0xf3bbd0: b    0xf3c968                 ; NPC.SetEventFlagCleared
```

`SetEventFlagCleared` (`0xF3A6C4`) marca a flag e, na primeira vez, chama
`OnGameEventClearedForTheFirstTime(13)`. Depois do `switch`:

- se `boss`: poções e corações (`DropBossPotionsAndHearts`, poção escolhida pelo `type`) e "X foi
  derrotado!";
- no servidor: `SendData(7)`, os dados do mundo.

Matar o Titã de Brasa, então:

- solta o loot do Olho (regras do `netID` 4) e as regras globais;
- grava `NPC.downedBoss1 = true`, que vai para o `.wld` (auditoria 01) e aparece no Boss Checklist como
  "Olho derrotado" (`TLMOD_DOWNED_EYE_OF_CTHULHU` lê esse campo, `bridge_adapter.cpp:69`);
- conta a conquista e o abate do Olho (`CountKillForAchievements` e `RegisterKill` pelo `type` e
  `netID`). Não segui as conquistas além disso;
- solta as moedas do `value` da casca, que o runtime não muda.

Confirmado no binário.

### 6.4 Caminho de implementação

1. **Loot do mod no lugar do da casca** (1–1,5 dia):
   - hookar `NPC.NPCLoot_DropItems` (`0xF3B438`); para um slot de mod, não chamar o original;
   - soltar a tabela do mod: um novo `TLModLootEntry{item, min, max, chance_den, modo}` no `TLModNpcDef`, ou
     um callback `on_loot(npc, player)`;
   - criar os itens com `Item.NewItem` no hitbox do NPC; origem nula serve ([seção 5](#5-newnpc-com-origem-nula-pergunta-4));
   - para itens por jogador (bolsa do Expert), usar `NPC.DropItemInstanced(Vector2, Vector2, int, int,
     bool)` (`0xF40DA0`).

   Alternativa que também alimenta o bestiário: registrar regras reais com
   `ItemDropDatabase.RegisterToNPC(idDeMod, regra)` (`0x7D86DC`) e, no hook, resolvê-las com
   `GetRulesForNPCID(idDeMod)`. Mais trabalho: criar objetos de regra pela ponte.
2. **Efeitos de morte** (1 dia). Hookar `NPC.DoDeathEvents` (`0xF3B52C`); para um slot de mod, não chamar
   o original e fazer só a parte genérica:
   - se `def.boss`, chamar `DoDeathEvents_DropBossPotionsAndHearts` (`0xF3D9CC`) e
     `DoDeathEvents_CelebrateBossDeath` (`0xF3DD94`) direto;
   - marcar "derrotado" para o boss do mod nos dados de mundo do TLMod (o sidecar da auditoria 01), com uma
     consulta na API ao lado de `progress`.
3. **Moedas** (0,25 dia). `value` no `TLModNpcDef`, aplicado antes de `ScaleStats` como na
   [seção 4.4](#44-caminho-de-implementação), passo 5. `NPCLoot_DropMoney` fica como está.
4. **Estandartes** (0,25 dia). Em `CountKillForBannersAndDropThem` (`0xF3AB1C`), pular os slots de mod
   quando a casca tem estandarte. O crédito do bestiário (`RegisterKill`) fica no passo 1 da
   [seção 7.3](#73-caminho-de-implementação).

Os hooks rodam onde `NPCLoot` roda: num jogador ou no servidor, nunca no cliente (`0xF389F8`). No
servidor, a tabela de slots de mod só está certa se o spawn aconteceu nele (inferido; ver a auditoria 01
sobre a rede).

**Esforço:** 2,5–3 dias.

---

## 7. Bestiário (pergunta 6)

### 7.1 Como o jogo monta e usa o bestiário

**Banco.** `BestiaryDatabase` guarda `_entries`, uma `List<BestiaryEntry>`, e `_byNpcId`, um
`Dictionary<int, BestiaryEntry>` pelo `netID`. `Register` (`0x8466FC`) acrescenta a entrada à lista e,
para cada elemento de `Info` que seja um `NPCNetIdBestiaryInfoElement`, grava `_byNpcId[NetId]`
(`0x846818`–`0x846854`). `BestiaryDatabaseNPCsPopulator.Populate` (`0x813160`) monta o banco, chamado
por `Main.Initialize_AlmostEverything` (`0xC85F30`).

**Entrada de inimigo.** `BestiaryEntry.Enemy(npcNetId)` (`0x850650`):

```
0x8507d0: bl   Dictionary<int, NPC>.get_Item       ; ContentSamples.NpcsByNetId[npcNetId]
0x85095c: bl   Dictionary<int, int>.get_Item       ; ContentSamples.NpcBestiaryRarityStars
0x850b6c: ldrb w8, [x21, #0x1f6]                   ; boss → desbloqueio rápido
0x850c3c: bl   NPC.GetBestiaryCreditId
0x850c74: bl   CommonEnemyUICollectionInfoProvider.GetKillCountNeeded
0x850dbc: bl   UnlockableNPCEntryIcon..ctor(npcNetId)
```

`get_Item` lança `KeyNotFoundException` para uma chave ausente.

**Tela do mobile.** `GUIBestiary.RefreshEntries` (`0x14B14C8`) lê `Main.BestiaryDB` (estático `+0x6B8`),
filtra e ordena. `NPCDraw` (`0x14B0568`) desenha cada entrada por chamadas de interface, isto é, pelo
`IEntryIcon` da entrada. Inferido: as chamadas de interface não foram resolvidas.

**Ícone.** `UnlockableNPCEntryIcon` cria um NPC próprio com `SetDefaults(npcNetId)`
(`modapi/README.md:414–415`) e o desenha pela `DrawNPCDirect` de 4 parâmetros (`bl 0xBCB734` em
`0x852E40`, dentro de `UnlockableNPCEntryIcon.Draw`). Essa sobrecarga só tem dois chamadores, o ícone e
`NPCSegment.Draw` dos créditos, e a ponte não a hooka (auditoria 03).

**Crédito de abate.** `NPCKillsTracker.RegisterKill` (`0x84F7B8`):

```
0x84f840: bl   NPC.GetBestiaryCreditId
0x84f8ec: bl   Dictionary<string, int>.set_Item    ; contagem, com trava e teto
0x84f950: ldr  w0, [x19, #0x210]                   ; servidor: manda o netID
0x84f960: bl   NetBestiaryModule.SerializeKillCount
```

`NPC.GetBestiaryCreditId` (`0xF5E3AC`) devolve `NpcBestiaryCreditIdsByNpcNetIds[netID]` por `get_Item`:
lança exceção para um `netID` desconhecido.

**Persistência.** `NPCKillsTracker.Save` (`0x84E140`) grava a contagem e os pares (texto, número).
`Load` (`0x84E87C`) lê de volta qualquer chave com `set_Item`. Uma chave de mod sobrevive no `.wld` e,
sem o mod, fica lá sem efeito.

**Nome.** `Lang.GetNPCName(id)` (`0x11F52FC`) só consulta o cache para ids de 1 a 687. Acima disso devolve
`LocalizedText.Empty`, sem exceção:

```
0x11f533c: sub  w8, w19, #1
0x11f5340: cmp  w8, #0x2ae              ; 1..687
0x11f5344: b.hi 0x11f5380               ; positivo fora da faixa: LocalizedText.Empty
```

Tudo confirmado no binário, salvo onde dito.

### 7.2 O que impede uma entrada própria hoje

- **O `netID` do boss de mod é o da casca.** O abate credita a entrada da casca e a desbloqueia
  (`modapi/README.md:414–419`).
- **Não há chave para o ID de mod** em `ContentSamples.NpcsByNetId`, `NpcBestiaryCreditIdsByNpcNetIds`,
  `NpcNetIdsByPersistentIds`, `NpcBestiaryRarityStars` e `NpcBestiarySortingId`. `Enemy(idDeMod)` lança
  exceção.
- **O ícone faria `SetDefaults(idDeMod)` num NPC fora de `Main.npc`.** O hook de `SetDefaults` só desmarca
  slots (`npc_hooks.cpp:294`), e o jogo indexa arrays pelo tipo: exceção (inferido). O desenho sai pela
  `DrawNPCDirect` de 4 parâmetros: textura da casca.
- **Em multiplayer,** a contagem vai pelo `netID` (`SerializeKillCount`).

### 7.3 Caminho de implementação

**Passo 1: crédito próprio, sem entrada** (0,5 dia). Hookar `NPCKillsTracker.RegisterKill` (`0x84F7B8`).
Se o NPC está num slot de mod, ler a chave `tlmod:<mod>/<npc>` com `GetKillCount(string)` (`0x84FA38`),
gravar o valor mais 1 com `SetKillCountDirectly` (`0x84FAA8`) e não chamar o original. A casca deixa de ser
desbloqueada pelo boss de mod, e a contagem do mod fica salva no `.wld` (7.1).

**Passo 2: entrada própria** (3–5 dias), depois de `Main.BestiaryDB` montado (ordem de inicialização da
auditoria 02):

1. Criar um NPC de amostra: `SetDefaults(casca)` mais os números do mod. Pôr em
   `ContentSamples.NpcsByNetId[idDeMod]`.
2. Preencher os outros quatro dicionários para o ID de mod. A chave de crédito é a do passo 1.
3. Chamar `BestiaryEntry.Enemy(idDeMod)` e `Main.BestiaryDB.Register` (`0x8466FC`). Isso dá entrada com
   contador de abates, desbloqueio rápido de boss e lista de drops.
4. Ícone: no hook de `SetDefaults`, trocar o ID de mod pela casca quando o NPC não está em `Main.npc`, e
   marcar o objeto como "amostra do mod X". Hookar a `DrawNPCDirect` de 4 parâmetros (`0xBCB734`) para
   trocar a textura, como na de 6.
5. Nome e texto: o hook de `get_TypeName` reconhecer a amostra. O texto de sabor (chave
   `Bestiary_FlavorText.npc_<nome>`) fica vazio sem uma entrada em `Language` (inferido).
6. Drops: só aparecem se o loot do mod for de regras reais (alternativa da
   [seção 6.4](#64-caminho-de-implementação), passo 1).

Multiplayer fica de fora: a contagem vai pelo `netID`.

**Esforço:** 0,5 dia (passo 1) + 3–5 dias (passo 2). Risco alto: os passos 3 e 4 tocam objetos
gerenciados criados pela ponte e uma tela inteira que não foi exercitada com IDs novos. Só o aparelho
confirma ([E6](#e6-bestiário)).

---

## 8. Respostas diretas

**1. Barra de vida.** A cada quadro, `BigProgressBarSystem.Update` (`0x97F764`) procura o NPC ativo mais
perto do jogador num raio de 5000 px da tela. O tipo de barra sai do `netID` (`TryTracking`, `0x97FAF4`):
26 `netID`s têm barra própria, ou nenhuma; o resto usa a comum. A comum só existe se `GetBossHeadTextureIndex`
(`0xEDC338`) não devolver −1, e esse índice sai do `type` (`BossHeadTextures[type]` e casos por tipo: o
do Olho olha `ai[0]`). Ela desenha o ícone `TextureAssets.NpcHeadBoss[índice]` e a vida do próprio NPC com
`GUIBossBar.Draw`, sem nome. Não depende de `boss`. Hoje o boss de mod tem a barra da casca: com a vida
do mod, com o ícone da casca, ou nenhuma se a casca não tem ícone.

**2. Música.** `Main.UpdateAudio` chama `UpdateAudio_DecideOnNewMusic` (`0xCA3580`) a cada quadro. O
primeiro NPC (pela ordem dos slots) com categoria e a até 5000 px da tela decide. A categoria vem do
`switch` pelo `type`, do set de invasão do Exército do Ancião e, por último, de `boss`, que dá a faixa 5.
Depois, a caixa de música sobrepõe. O TLMod pode plugar logo depois de `DecideOnNewMusic` e de
`DecideOnTOWMusic`, gravando `Main.newMusic` (`+0x458`) com uma faixa de 1 a 91 escolhida pelo mod.

**3. Mensagens, despawn e fases.**

- *Vem da casca:* o anúncio de despertar (só por `SpawnBoss` e afins; o `spawn_npc` não anuncia), o de
  derrota (só se a casca é boss), poções e corações, `boss` e tudo que ele liga. Com `OnAI = false`,
  também as fases, a fuga de dia e o dano e a defesa da fase 2.
- *Com `OnAI = true` em todo tick, o mod controla* posição, velocidade, vida, dano, defesa, alvo, `ai[]`,
  `local[]` e quadro.
- *Perde:* fuga de dia, sons, projéteis, servos e mensagens, porque a API não os oferece. O boss só some
  12,5 s depois de sair do alcance de todos (`CheckActive`).
- *Em qualquer caso:* a vida do mod ignora Expert e Mestre.

**4. Origem nula.** É seguro. `NewNPC` (`0xEE4630`) e as três sobrecargas de `Item.NewItem` sobrescrevem
`x0` sem lê-lo. `IEntitySource` não tem métodos. Só `Projectile.FindBannerToAssociateTo` (via
`NewProjectile`, aceita nulo em `0x1109BF0`) e `Player.SpawnMinionOnCursor` (só com origens de
`ItemCheck_Shoot`) testam o tipo de uma origem.

**5. Loot.** `NPCLoot` (`0xF38944`) chama, em ordem, `DoDeathEvents_BeforeLoot`, `NPCLoot_DropItems`,
`DoDeathEvents`, `NPCLoot_DropMoney` e `NPCLoot_DropHeals`. `DropItems` resolve as regras globais e as do
`netID` (`GetRulesForNPCID`, `0x7D853C`). `DoDeathEvents` aplica os efeitos do `type`: para a casca 4,
`NPC.downedBoss1`. Para soltar só o loot do mod: hook em `NPCLoot_DropItems` que pula o original nos slots
de mod e solta a tabela do mod. Para não herdar o progresso da casca: hook em `DoDeathEvents` que faz só
poções, corações e anúncio. Cada etapa tem um só chamador.

**6. Bestiário.** Uma entrada própria precisa de:

- chaves para o ID de mod nos cinco dicionários de `ContentSamples`;
- uma `BestiaryEntry` registrada em `Main.BestiaryDB`;
- o ícone resolvido: `SetDefaults(idDeMod)` fora de `Main.npc` e a `DrawNPCDirect` de 4 parâmetros;
- o crédito de abate desviado da casca para uma chave própria, que o `.wld` já guarda.

O passo mínimo, desviar o crédito, custa meio dia. A entrada completa, de 3 a 5 dias.

---

## 9. Lacunas do TLMod, em ordem de risco

1. <a id="lacuna-1"></a>**A morte do boss de mod aplica a progressão da casca** (alto). Com a casca 4,
   matar o Titã grava `NPC.downedBoss1` no mundo e dispara o evento de primeira vez 13 (`0xF3BBA8`).
   Também credita a conquista e o bestiário do Olho e solta o loot do Olho. É persistente: vai para o
   `.wld` e não sai quando o mod é removido. Nenhum hook do TLMod passa por `NPCLoot` ou `DoDeathEvents`.
   Ações 1 e 2.
2. <a id="lacuna-2"></a>**Os números do mod não valem quando a IA da casca roda** (alto para o
   `example_boss`). Na fase 2 do Olho, dano e defesa voltam a 23/0 (Normal) a cada tick (`0xDEE458`). Em
   nenhum modo a vida, o dano ou a defesa do mod escalam (`mod_loader.cpp:392` roda depois de
   `ScaleStats`). O exemplo, que delega as fases à casca, já está nesse caso. Ação 3.
3. <a id="lacuna-3"></a>**Sem controle de despawn** (médio). Com `OnAI` sempre `true`, o boss nunca foge de
   dia, e a API não expõe `timeLeft`, `active` ou um despawn. Ação 4.
4. <a id="lacuna-4"></a>**`def.boss` não tem efeito** (médio). Está "reservado" (`tlmod_api.h:178`) e nunca
   é escrito no NPC (`mod_loader.cpp:552`). Numa casca que não é boss, faltam música, anúncio de derrota,
   poções, desbloqueio rápido e a regra de despawn de boss. Numa casca boss, vem tudo da casca. Ação 5.
5. <a id="lacuna-5"></a>**Barra com o ícone da casca, ou nenhuma** (médio). Uma casca sem ícone não tem
   barra, e as 26 cascas com barra especial trazem a lógica delas. Ação 5.
6. <a id="lacuna-6"></a>**Música fixa** (baixo): "Boss 1" quando a casca é boss, nenhuma quando não é.
   Ação 6.
7. <a id="lacuna-7"></a>**Sem anúncio de despertar** (baixo). O `spawn_npc` usa `NewNPC`. Ação 7.
8. <a id="lacuna-8"></a>**Bestiário** (baixo). O boss de mod não tem entrada e desbloqueia a da casca.
   Ação 8.
9. <a id="lacuna-9"></a>**Documentação** (baixo):
   - `modapi/README.md:94`: a barra de boss do mobile não mostra nome;
   - `modapi/README.md:108–110` e `npc_hooks.cpp:212`: a origem nula já está respondida.

   Ação 9.

---

## 10. Ações recomendadas

Em ordem. O esforço de cada uma vem das seções 2 a 7.

1. **Loot do mod e fim do loot da casca** (1,5 dia): hook em `NPCLoot_DropItems`, tabela de loot e
   `value` no `TLModNpcDef` ([6.4](#64-caminho-de-implementação), passos 1 e 3). [Lacuna 1](#lacuna-1).
2. **Morte sem a progressão da casca** (2 dias):
   - hook em `DoDeathEvents` com a parte genérica e o "derrotado" do mod no sidecar;
   - hook em `RegisterKill` para o crédito próprio;
   - pular os estandartes ([6.4](#64-caminho-de-implementação), passos 2 e 4; [7.3](#73-caminho-de-implementação), passo 1).

   [Lacunas 1](#lacuna-1) e [8](#lacuna-8).
3. **Números do mod em todas as fases e dificuldades** (1,5 dia): `on_post_ai` e a escala no `ScaleStats`
   ([4.4](#44-caminho-de-implementação), passos 4 e 5). Atualizar o `example_boss`: ou assume as fases
   (`true`), ou reaplica os números no `on_post_ai`. [Lacuna 2](#lacuna-2).
4. **Despawn** (0,5–1 dia): `time_left` e `despawn_npc`, com a fuga de dia no exemplo
   ([4.4](#44-caminho-de-implementação), passo 3). [Lacuna 3](#lacuna-3).
5. **`boss` e barra** (2–2,5 dias): escrever `boss` ([4.4](#44-caminho-de-implementação), passo 6),
   validar a casca e dar barra e ícone próprios ([2.5](#25-caminho-de-implementação), passos 1 e 3).
   [Lacunas 4](#lacuna-4) e [5](#lacuna-5).
6. **Música** (0,5–1 dia): campo `music` e o hook depois das duas decisões
   ([3.5](#35-caminho-de-implementação)). [Lacuna 6](#lacuna-6).
7. **Anúncios** (1 dia): `announce` e `spawn_boss` ([4.4](#44-caminho-de-implementação), passos 1 e 2).
   [Lacuna 7](#lacuna-7).
8. **Entrada própria no bestiário** (3–5 dias): só depois das ações 1 a 7
   ([7.3](#73-caminho-de-implementação), passo 2). [Lacuna 8](#lacuna-8).
9. **Documentação** (0,25 dia): corrigir o README e fechar o TODO da origem nula. [Lacuna 9](#lacuna-9).

As ações 1 a 7 somam cerca de 10 dias e cobrem um boss completo em um jogador, salvo o bestiário.

---

## 11. Experimentos no aparelho

Os experimentos usam:

- o `example_boss` como está, invocado pelas Ferramentas ("Invocar", na busca de NPCs de mod) ou por
  `spawn_on_start`;
- as Ferramentas: hora, "Invencível", "Curar";
- o Boss Checklist;
- o `tlmod.log` (painel → Log).

O E2 e o E5 pedem uma build de teste do mod ou da ponte, com uma linha de log a mais. Use um mundo novo,
de noite (Ferramentas → "Meia-noite").

### E1: barra e ícone

1. Invocar o Titã. Olhar a barra de boss durante a introdução (2 s parado) e depois.
2. Baixar a vida abaixo de 50% (com "Invencível" ligado) até o Olho entrar na fase 2.
3. Para o caso sem ícone, uma build de teste do `example_boss` com `shell_type` de um inimigo comum (por
   exemplo 1, o slime azul).
4. **Esperado:**
   - barra com o ícone do Olho e números de 12000 desde a introdução;
   - o ícone vira o da boca quando a fase 2 começa;
   - nenhum nome na barra;
   - na build com o slime, nenhuma barra.

### E2: dano e defesa na fase 2

1. Build de teste do `example_boss`: no `OnAI`, uma vez por segundo, `host.Info` com `npc.damage`,
   `npc.defense` e `npc.ai[0]`.
2. Lutar até a fase 2, em Normal e depois em Expert.
3. **Esperado:**
   - fase 1: 40 e 14;
   - fase 2 em Normal: 23 e 0;
   - fase 2 em Expert: 18 ou 20 e −15 ou −30;
   - `life_max` 12000 nos dois modos.

   Confirma a [lacuna 2](#lacuna-2).

### E3: despawn de dia

1. Invocar o Titã e, já na fase 1, usar Ferramentas → "Amanhecer".
2. Repetir com uma build de teste cujo `OnAI` devolva sempre `true`.
3. **Esperado:**
   - com a IA do Olho, o Titã sobe e some em segundos;
   - com `true`, ele fica parado de dia;
   - nessa build, depois de o jogador se afastar bastante (algumas telas), some uns 12 s depois.

### E4: anúncios

1. Invocar o Titã e matá-lo.
2. **Esperado:** nenhum "despertou!" na invocação e "Titã de Brasa foi derrotado!" na morte.
3. Opcional, com uma build de teste da ponte que chame `NPC.SpawnBoss` com o ID de mod: "Titã de Brasa
   despertou!".

### E5: loot e progresso

1. Num mundo novo, conferir no Boss Checklist que o Olho está pendente.
2. Matar o Titã em combate normal, sem "Matar inimigos": essa ferramenta usa `StrikeNPCNoInteraction` e
   pula o crédito do bestiário.
3. **Esperado:**
   - o loot do Olho no chão (minério da corrupção ou do carmesim, flechas) e, em Expert, a bolsa do Olho;
   - poções e corações;
   - o Boss Checklist marca o Olho como derrotado, também depois de sair e voltar ao mundo.

   Confirma a [lacuna 1](#lacuna-1).
4. Para ver a ordem das etapas, uma build de teste da ponte pode registrar a entrada em
   `NPCLoot_DropItems` e em `DoDeathEvents` com o `type`.

### E6: bestiário

1. Depois do E5, abrir o bestiário pelo inventário.
2. **Esperado:** a entrada do Olho desbloqueada e com 1 abate, e nenhuma entrada do Titã.
3. Depois da ação 2, repetir: a entrada do Olho segue bloqueada. A contagem `tlmod:example_boss/ember_titan`
   sobrevive a sair e voltar ao mundo. Para isso, uma build de teste registra `GetKillCount(chave)` ao
   entrar no mundo.
