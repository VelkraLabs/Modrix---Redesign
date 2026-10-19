# Auditoria 05: preparação para `register_projectile`, `register_buff` e `register_tile`

Terraria Mobile 1.4.4.9 (Unity 2021.3.56f2, IL2CPP, Android arm64), comparado com o TLMod da branch
`ccr-bbd92bf9-i01buk` (código da ponte e do runtime no commit `f917b04`; os commits seguintes só
acrescentaram auditorias e arquivos do jogo).

Auditoria só de leitura. As fontes são:

- a desmontagem da `libil2cpp.so` e o dump (`dump.cs`, `script.json`);
- o estudo de itens e NPCs (`docs/estudo_extensao_tabelas.md`), cujo método este módulo repete;
- as auditorias 01 (persistência), 02 (inicialização), 03 (hooks) e 07 (mapa do tModLoader);
- o código da ponte (`il2cpp_bridge/`), do runtime e da API (`modapi/`), o `terraria_offsets.h` e os
  READMEs.

Nenhum código foi alterado; este arquivo é o único acréscimo. Dump e scripts ficam fora do repositório.

**Módulo 5:** preparação para `register_projectile`, `register_buff` e `register_tile`. Para projéteis,
buffs e tiles:

1. Quais tabelas são indexadas pelo tipo, e com que tamanho?
2. Que constantes no código dependem da contagem de tipos?
3. Como o jogo cria, atualiza, desenha, salva e envia pela rede cada um?
4. A casca ou o ID próprio é mais adequado para cada tipo?

A entrega é uma recomendação por tipo e a ordem sugerida.

- [Como ler](#como-ler)
- [Resumo](#resumo)
- [1. Método](#1-método)
- [2. Projéteis](#2-projéteis)
- [3. Buffs](#3-buffs)
- [4. Tiles](#4-tiles)
- [5. O que vale para os três](#5-o-que-vale-para-os-três)
- [6. O TLMod hoje](#6-o-tlmod-hoje)
- [7. Recomendação por tipo e ordem sugerida](#7-recomendação-por-tipo-e-ordem-sugerida)
- [8. Experimentos no aparelho](#8-experimentos-no-aparelho)
- [9. Respostas diretas](#9-respostas-diretas)
- [10. Lacunas do TLMod, em ordem de risco](#10-lacunas-do-tlmod-em-ordem-de-risco)
- [11. Ações recomendadas](#11-ações-recomendadas)

---

## Como ler

**Níveis de confiança**, os mesmos das auditorias anteriores:

- **confirmado no binário**: visto na desmontagem ou na metadata;
- **inferido**: decorre do que foi lido, mas um passo não foi rastreado;
- **só o aparelho confirma**: depende do que acontece em tempo de execução. Cada caso tem um experimento na
  [seção 8](#8-experimentos-no-aparelho).

Afirmações sobre código-fonte trazem arquivo e linha e valem como confirmadas no código. Endereços são RVAs
da `libil2cpp.so`.

**Casca e ID próprio**, como no estudo de itens:

- **casca:** o conteúdo de mod ocupa um tipo original. O jogo só vê esse tipo; o TLMod guarda à parte qual
  conteúdo de mod está ali e aplica por cima nome, textura e comportamento. É o modelo de hoje para itens e
  NPCs;
- **ID próprio:** o conteúdo de mod ganha um tipo novo, depois da contagem original. As tabelas do jogo
  crescem e os limites no código são corrigidos na memória.

**Classes das ocorrências**, as do estudo:

- **ALLOC:** tamanho de um `new T[n]`;
- **FACTORY:** tamanho de uma `SetFactory`, que cria vários arrays de uma vez;
- **BOUND:** teste de limite do tipo;
- **LOOP:** laço sobre todos os tipos;
- **FORMATO:** a contagem gravada em arquivo.

Ficam de fora, contadas à parte:

- **IDX:** a checagem que o IL2CPP põe antes de `tabela[k]` com `k` constante. Não depende da contagem;
- **ID:** comparação com um tipo específico (`tipo == 1020`).

**Fases**, as da auditoria 02:

- **A:** `il2cpp_init`, antes de qualquer código gerenciado;
- **B:** `XNAUnityRunner.Awake`;
- **C:** `XNAUnityRunner.Start`;
- **D:** quadros de carregamento (`Initialize_AlmostEverything`, carga de texturas);
- **E:** menu e jogo.

**Threads**, as das auditorias 03 e 07:

- **P:** a principal do Unity (atualização e desenho);
- **G:** geração de mundo; **C:** carregar mundo; **S:** save com o jogo andando; **Q:** sair do
  multiplayer; **R:** rede do cliente; **H:** conversão do modo difícil; **L:** carga do runtime do TLMod;
- **T:** as threads de render de tiles do mobile ([4.3](#43-como-o-jogo-cria-atualiza-desenha-salva-e-envia)).

---

## Resumo

1. **As contagens.** Confirmado no binário (metadata):

   | tipo | constante | valor | forma |
   |---|---|---:|---|
   | projétil | `ProjectileID.Count` | 1022 | `const short` |
   | buff | `BuffID.Count` | 355 | `const int` |
   | tile | `TileID.Count` | 693 | `const ushort` |

   São `const`, como `ItemID.Count`: o compilador copia o valor em cada uso, e não há campo para trocar.
2. **Os pontos no código** que dependem da contagem, com a mesma classificação do estudo de itens:

   | tipo | ALLOC | FACTORY (arrays) | BOUND | LOOP | FORMATO | total |
   |---|---:|---:|---:|---:|---:|---:|
   | projétil | 7 | 1 (42) | 6 | 2 | 0 | **16** |
   | buff | 15 | 1 (8) | 4 | 4 | 0 | **24** |
   | tile | 57 | 1 (127) | 33 | 13 | 3 | **107** |
   | item (estudo) | 12 | 2 (86) | 24 | 6 | — | 44 |

   - Todos são instruções de 4 bytes (`mov wN, #imm16` ou `cmp wN, #imm12`), corrigíveis no lugar como no
     estudo. Confirmado no binário.
   - Nenhuma tabela desses tipos é preenchida por blob de `InitializeArray`, o caso que travava o
     `Main.npcFrameCount`. Confirmado no binário.
   - Das 241 tabelas estáticas rastreadas, só uma referência escapa do campo: `TileID.Sets.CacheSpecialDrawTree`,
     copiada para um campo do `TileDrawing`.
3. **Como cada um vive no jogo:**
   - **Projétil:** nunca é salvo. Na rede, a mensagem 27 leva o tipo como `short`, e quem recebe chama
     `SetDefaults` sem conferir a faixa. O `SetDefaults` lê `ProjectileID.Sets.TrailCacheLength[tipo]`:
     tipo ≥ 1022 lança exceção.
   - **Buff:** vai para o `.plr` como `int` e para a rede como `ushort`. A auditoria 01 mostrou que um buff
     ≥ 355 em `buffType` cancela todo save do personagem. `Lang.GetBuffName` também lança exceção.
   - **Tile:** no mobile, o mundo é um banco nativo de tiles com deduplicação (`TileData`), e o tipo é
     `ushort`. O save rápido do mobile lança exceção com tipo ≥ 693, e o mundo não é salvo. As seções de tile
     da rede também indexam `Main.tileFrameImportant[tipo]`. O desenho roda em threads de render próprias.
4. **Casca ou ID próprio:**
   - **Projétil: os dois servem.**
     - A casca já é viável, como a de NPC.
     - O ID próprio é o mais barato dos três: 16 pontos e nada a salvar.
     - Trocar uma pela outra depois não exige migrar saves.
   - **Buff: ID próprio.** Não há buff original que sirva de casca limpa. Os 14 sem caso no código de
     atualização ou são aplicados pelo próprio jogo, ou são montarias, ou são níveis de bônus de armadura.
   - **Tile: só ID próprio, e por último.**
     - A casca exigiria guardar a identidade por posição do mundo.
     - Todos os 692 tipos têm caso especial nos métodos centrais.
     - O desenho roda fora da P.
     - O ID próprio custa 107 pontos, o save do `.wld` e do `.map` e a `Main.tileMerge`, que é quadrada.
5. **O registro do TLMod precisa mudar para o ID próprio.**
   - Hoje projétil, tile e buff começam em 4096 (`content_registry.cpp:27–30`).
   - Os limites no código são `cmp` com imediato de 12 bits (até 4095), e a `tileMerge` cresce com o
     quadrado do maior tipo.
   - Por isso os tipos de mod no jogo têm de vir logo depois da contagem original: 1022, 355 e 693.

**Ordem sugerida** (detalhe na [seção 7](#7-recomendação-por-tipo-e-ordem-sugerida)):

| passo | o quê | depende de | esforço |
|---|---|---|---|
| 1 | projétil por casca (o passo 6 da auditoria 07) | nada novo | 4–6 dias |
| 2 | infraestrutura de ID próprio: correção no `il2cpp_init`, manifests lidos antes, tabela de assinaturas com falha fechada | auditoria 02 | 6–9 dias |
| 3 | projétil com ID próprio, o primeiro cliente da infraestrutura | 2 | 5,5–9 dias |
| 4 | buff com ID próprio e o arquivo paralelo do `.plr` | 2 | 9–13 dias |
| 5 | tile com ID próprio e o arquivo paralelo do `.wld` e do `.map` | 2 e 4 | 26–38 dias |

Esforço em dias de uma pessoa que conhece a ponte e o runtime, com build e teste no aparelho (inferido).

---

## 1. Método

O mesmo do estudo de itens (`docs/estudo_extensao_tabelas.md`, seção 1), com as ferramentas daquele estudo.

1. **Varredura dos imediatos.** Toda instrução da seção de código cujo imediato é a contagem ou uma forma
   derivada (MOVZ/MOVN, ADD/SUB/CMP/CMN imediato). Valores procurados, com N a contagem: de N−3 a N+8, N+31,
   N+32, 2N, 2N+24, 2N+30, 2N+32 e as faixas 4N a 4N+32 e 8N a 8N+32.
   - Projétil: 5.456 ocorrências brutas. Sem 1024, 4096 e 8192, potências de dois que aparecem em todo
     lugar, e sem aritmética de endereço e de pilha, sobram 278.
   - Buff: 1.649 brutas, 966 depois do mesmo filtro.
   - Tile: 614 brutas, 398 depois do filtro.
2. **Classificação automática** pela instrução seguinte (`b.cond`, `cset`, destino do `bl`, registrador
   redefinido), como no estudo.
3. **Revisão:**
   - **IDX:** o desvio vai para o lançador de `IndexOutOfRangeException` (`0x642C40`), ou o registrador
     comparado vem do comprimento do array (`+0x18`);
   - **ID:** o `cmp` é guarda de tabela de salto (há `br` logo depois);
   - **BOUND e LOOP:** o registrador comparado vem do tipo (`Projectile.type` em `+0x70`, `Tile.get_type`,
     elemento de `buffType`), ou há uma tabela do tipo logo depois. Cada um foi lido na desmontagem.
4. **Laços que o classificador não pega:** contagem regressiva (`mov w28, #N` … `subs w28, w28, #1`) e
   capacidade de `List`. Achou um de cada em `TileObjectData`.
5. **Tabelas e fábricas.**
   - Destino de cada ALLOC.
   - Arrays criados por cada `SetFactory`.
   - Escapes: onde a referência lida de um campo estático é gravada em outro campo. Os candidatos foram
     conferidos à mão.
6. **Ciclo de vida.** Chamadores diretos (`bl`/`b`) dos métodos de criação, atualização, desenho, save e
   rede, e leitura dos trechos que tocam o tipo. Nos pacotes de rede, as leituras e escritas vêm pelas
   entradas da tabela virtual do `BinaryReader`/`BinaryWriter` (`ReadInt16` na posição 13, `ReadByte` na
   10, `Write(short)` na 18).
7. **Casos especiais por tipo, para julgar a casca.** Em cada método central, os tipos que ganham tratamento
   próprio:
   - `cmp`/`cmn` com o tipo;
   - entradas de tabela de salto que não vão para o caso padrão;
   - faixas curtas (até 64 tipos).

   Dois critérios:
   - **com origem:** só comparações cujo registrador veio do campo de tipo. Pode deixar passar casos;
   - **estrito:** qualquer comparação com aquele número no método. Conta a mais, por isso é um limite
     superior.

**Limites.**

- Palavras de dados (`.rodata`, `.data`) não foram varridas para 355, 693 e 1022. São números comuns
  demais, e a auditoria 02 já mostrou que as tabelas de tipo não usam blobs.
- A origem do registrador é buscada para trás, em ordem linear. Num bloco alcançado por salto, a última
  definição linear pode não ser a real; os casos usados nas conclusões foram lidos à mão.

---

## 2. Projéteis

### 2.1 Constantes no código

**ALLOC (7).** Todas são `mov w1, #0x3fe` (1022) antes do `SZArrayNew` (`0x642B80`):

| RVA | método | tabela criada | fase |
|---|---|---|---|
| `0x7A2E1C` | `TextureAssets..cctor` | `TextureAssets.Projectile` (`Asset<Texture2D>[]`) | C |
| `0xD75BF4` | `Main..cctor` | `Main.projHostile` | B |
| `0xD75C24` | `Main..cctor` | `Main.projHook` | B |
| `0xD75F18` | `Main..cctor` | `Main.projFrames` | B |
| `0xD75F30` | `Main..cctor` | `Main.projPet` | B |
| `0x1206484` | `Lang..cctor` | `Lang._projectileNameCache` | C |
| `0x1042D78` | `Player..ctor` | `Player.ownedProjectileCounts` (`+0xBF8`, **um por jogador**) | B e E |

**FACTORY (1).** `ProjectileID.Sets..cctor` (`0x15751B0`), `mov w1, #0x3fe` em `0x1575498` antes do
`SetFactory..ctor`. Cria 42 arrays em `ProjectileID.Sets`: 31 `bool`, 4 `float`, 3 `int`, 2
`Nullable<bool>` e 2 de objeto (`CharacterPreviewAnimations`, `ExtendedCanHitCheckSearch`). Fora o
`.cctor`, ninguém lê a fábrica. Confirmado no binário.

**BOUND (6).** Todas na fase E:

| RVA | método | instrução | depois do teste | efeito com tipo ≥ 1022 |
|---|---|---|---|---|
| `0xD99030` | `NetMessage.SendData` (mensagem 27) | `sub w9, w8, #1; cmp w9, #0x3fc; b.hi` | `ProjectileID.Sets.NeedsUUID` | não marca o UUID ao enviar |
| `0x1109488` | `Projectile.NewProjectile` | `sub w8, w22, #1; cmp w8, #0x3fc; b.ls` | `NeedsUUID` (`0x11093A0`) | não ganha UUID |
| `0xFE4164` | `Player.ItemCheck` | `ldr w8, [x20, #0x158]` (`Item.shoot`); `cmp w8, #0x3fd; b.hi` | `ProjectileID.Sets.LightPet` (`0xFE4188`) | o pet de luz do item é ignorado |
| `0x1111F70` | `Projectile.Damage` | `cmp w9, #0x3fc; b.hi` | `StardustDragon` | sem o tratamento |
| `0x111204C` | `Projectile.Damage` | `cmp w9, #0x3fc; b.hi` | `StormTiger` | sem o tratamento |
| `0x11140DC` | `Projectile.Damage` | `cmp w8, #0x3fe; b.hi` | `CultistIsResistantTo` | sem a resistência do Cultista |

**LOOP (2).** Fase D, os dois dentro do `Initialize_AlmostEverything`:

| RVA | método | instrução | laço |
|---|---|---|---|
| `0xC86478` | `Main.Initialize_AlmostEverything` | `cmp x26, #0x41e` (N+32) | percorre `Main.projHook` e `projHostile` byte a byte, com o cabeçalho de 0x20 no índice |
| `0xC8783C` | `Main.Initialize_TileAndNPCData1` | `cmp x22, #0x3fe; b.eq` | `Main.projFrames[i] = 1` para todos os tipos |

**Fora da conta:**

- 14 IDX. Por exemplo, `ownedProjectileCounts[1020]` em `ItemCheck_CheckCanUse` (`0xF854B4`),
  `ItemCheck_OwnerOnlyCode` (`0x1025E50`) e `ItemCheck_TryDestroyingDrones` (`0x102B0D8`): o `cmp w9, #0x3fc`
  compara o comprimento do array com o índice 1020.
- Os IDs 1019 a 1021 são projéteis reais e aparecem como ID em `AI_006`, `Kill`, `HandleMovement`,
  `GetAlpha` e no desenho.

Nenhum ponto de projétil grava a contagem em arquivo.

### 2.2 Tabelas indexadas pelo tipo

| tabela | tamanho | quem aloca | leituras | observação |
|---|---:|---|---|---|
| `TextureAssets.Projectile` | 1022 | `TextureAssets..cctor` | 202 em 48 métodos | precisa de objetos `Asset<Texture2D>` para o mod |
| `Main.projFrames` | 1022 | `Main..cctor` | 99 em 36 métodos | quadros da folha; o laço em `Main.Initialize_TileAndNPCData1` (`0xC8783C`) põe 1 em todos |
| `Main.projHook` | 1022 | `Main..cctor` | 16 em 14 | gancho de agarrar |
| `Main.projPet` | 1022 | `Main..cctor` | 13 em 9 | pet |
| `Main.projHostile` | 1022 | `Main..cctor` | 2 em 2 | — |
| `Lang._projectileNameCache` | 1022 | `Lang..cctor` | 5 em 3 | nomes (mensagem de morte) |
| `ProjectileID.Sets.*` (42) | 1022 | fábrica | — | rastro, minion, sentinela, chicote, iô-iô, UUID, vento… |
| `Player.ownedProjectileCounts` | 1022 | `Player..ctor` | — | **instância**: criado a cada `new Player` |

**Escapes:** nenhum. Toda leitura relê o campo estático e indexa ali mesmo. Confirmado no binário (varredura
de escapes, revisada).

O `ContentSamples` do mobile não tem amostras de projétil: os campos dele são de NPC e item
(`ContentSamples`, `dump.cs`). Confirmado na metadata.

### 2.3 Como o jogo cria, atualiza, desenha, salva e envia

| etapa | métodos (RVA) | thread | o que importa para o mod |
|---|---|---|---|
| criar | `Projectile.NewProjectile` (`0x1108E7C`, 302 chamadas; `0x1108DA0`, 80) → `Projectile.SetDefaults` (`0x1067854`) | P; em tese G, C e H (auditoria 03) | `SetDefaults` lê **uma** tabela: `TrailCacheLength[tipo]`, com checagem do IL2CPP (`0x106795C`–`0x106796C`). Tipo ≥ 1022 lança exceção. Depois vêm tabelas de salto por faixa de tipo (`0x1067C48`–`0x1067C6C`), e no fim um salto para a IA (`b` em `0x106F668`) |
| atualizar | `Main.DoUpdateInWorld` → `Projectile.Update` (`0x1120B04`) → `AI` (`0x1070668`), `HandleMovement` (`0x1124814`) → `UpdatePosition` (`0x112CB1C`), `Damage` (`0x1110080`), `Kill` (`0x10C70A4`, 244 chamadas) | P | tipo desconhecido cai nos casos padrão dos `switch` |
| desenhar | `Main.DoDraw`/`DrawCapture` → `DrawProjectiles` (`0xCD08E4`) → `DrawProj` (`0xCD0D70`) → `DrawProjDirect` (`0xC2CCF8`) | P | `TextureAssets.Projectile[tipo]` e `Main.projFrames[tipo]` |
| salvar | **não salva** | — | as seções do `SaveWorld_Version2` não têm projéteis (auditoria 01, W15), nem o `.plr` |
| rede | mensagem 27: envio em `SendData` grava `identity` como `short` (`0xD98EBC`–`0xD98ECC`) e o tipo como `short` (`0xD98F20`–`0xD98F30`); recepção no caso `0xD819E4` do `ProcessData` | P (recepção) | o tipo cabe até 32767. Quem recebe não confere a faixa e chama `SetDefaults` se o tipo mudou (`0xD8C6B0`–`0xD8C6C8`) |

Na recepção da mensagem 27, confirmado no binário:

```
0xd81a20: ldr  x9, [x8, #0x208]   ; ReadInt16: identity
0xd81a58: bl   Utils.ReadVector2  ; posição
0xd81a74: bl   Utils.ReadVector2  ; velocidade
0xd81a94: ldp  x9, x1, [x8, #0x1d8] ; ReadByte: dono
0xd81ab8: ldr  x10, [x9, #0x208]  ; ReadInt16: tipo
```

A mensagem 29 (matar projétil) leva `identity` e dono, sem o tipo.

### 2.4 Casca ou ID próprio

**Casca.**

- **Modelo.** Um projétil original faz de recipiente: tipo, tabelas e colisão dele. O TLMod sabe quais
  slots de `Main.projectile` são de mod, como já faz com `Main.npc`. Assume a IA, troca a textura no
  `DrawProj` e recebe os eventos de acerto e morte.
- **O que vaza.** Os 19 métodos centrais têm tratamento próprio para muitos tipos:
  - **com origem:** 565 dos 1021 tipos têm algum caso. Os maiores são `Damage` (202), `DrawProjDirect`
    (172), `CanCutTiles` (125), `Kill` (79) e `HandleMovement` (77);
  - **pelo critério estrito,** só 14 tipos ficam sem caso nenhum: as boias de pesca (360 a 366, 381 e 382),
    as lanças 367 a 369, `ZephyrFish` (380) e `SoulscourgePirate` (394). Neles o comportamento vem de
    campos e do `aiStyle`, que o mod sobrescreve depois do `SetDefaults`;
  - **as flags por tipo** (42 `Sets` e 4 tabelas de `Main`) também vazam. Dos 456 tipos livres pelo
    critério com origem, 423 não recebem gravação por índice constante em `projPet` nem em `projFrames`,
    as duas tabelas que a ferramenta conseguiu ler.
  - Confirmado no binário (as contagens); inferido (o efeito de cada flag).
- **O que já existe para isso.** A casca de NPC: `on_ai`, troca de textura, `TLModNpcState`. Mesmo modelo,
  sem infraestrutura nova.

**ID próprio.**

- **No binário:** 16 pontos, a metade dos de item, e nenhum formato de arquivo.
- **Na rede:** `short`.
- **Comportamento:**
  - um tipo ≥ 1022 não tem caso especial em lugar nenhum: todo `switch` cai no padrão;
  - o mod controla tudo pelos ganchos;
  - o desenho original funciona sozinho, com a textura em `TextureAssets.Projectile[tipo]` e os quadros em
    `projFrames`.
- **O que falta montar:**
  - um gancho em `SetDefaults` para os tipos de mod, que preencha os campos;
  - as 42 `Sets`, as 4 tabelas de `Main` e os nomes;
  - um `Asset<Texture2D>` por textura.

**Veredito: os dois servem.**

- **Casca agora.** É o passo 6 da auditoria 07, sem esperar infraestrutura. A casca vem de uma lista
  conferida e passa por uma checagem das tabelas por tipo em tempo de execução, como o TLMod já faz com
  itens.
- **ID próprio depois.** Quando a infraestrutura existir, o projétil é o primeiro cliente natural: o mais
  barato e sem save.
- **Por que a troca é barata.** Como projétil não é salvo, ir da casca ao ID próprio não exige migrar nada:
  só a API precisa esconder a estratégia (o `shell_type` vira uma dica opcional).

---

## 3. Buffs

### 3.1 Constantes no código

**ALLOC (15).** Todas `mov w1, #0x163` (355):

| RVA | método | tabela | fase |
|---|---|---|---|
| `0x7A2DBC` | `TextureAssets..cctor` | `TextureAssets.Buff` | C |
| `0xD74070` | `Main..cctor` | `Main.buffAlpha` (`float`) | B |
| `0xD75C3C`, `0xD75C54`, `0xD75C6C`, `0xD75C84`, `0xD75C9C`, `0xD75CB4`, `0xD75CCC`, `0xD75CE4`, `0xD75CFC` | `Main..cctor` | `pvpBuff`, `persistentBuff`, `vanityPet`, `lightPet`, `meleeBuff`, `debuff`, `buffNoSave`, `buffNoTimeDisplay`, `buffDoubleApply` | B |
| `0x12064CC`, `0x12064E4` | `Lang..cctor` | `Lang._buffNameCache`, `_buffDescriptionCache` | C |
| `0x1042878` | `Player..ctor` | `Player.buffImmune` (`+0x450`, **um por jogador**) | B e E |
| `0xF4604C` | `NPC..ctor` | `NPC.buffImmune` (`+0x110`, **um por NPC**) | B e E |

**FACTORY (1).** `BuffID.Sets..cctor` (`0x1528238`), `mov w1, #0x163` em `0x152832C`. Cria as 8 tabelas de
`BuffID.Sets`: 7 `bool` (`IsWellFed`, `IsFedState`, `IsAnNPCWhipDebuff`, `TimeLeftDoesNotDecrease`,
`CanBeRemovedByNetMessage`, `IsAFlaskBuff`, `NurseCannotRemoveDebuff`) e `BasicMountData`. Fora o `.cctor`,
ninguém lê a fábrica.

**BOUND (4).** Fase E:

| RVA | método | instrução | depois | efeito com tipo ≥ 355 |
|---|---|---|---|---|
| `0x9B1914` | `GUINPCDialogue.SetupButtonText` (**mobile**) | `cmp w29, #0x162; b.hi` | `NurseCannotRemoveDebuff` | a enfermeira trata como removível |
| `0x9B4B7C` | `GUINPCDialogue.Option1Clicked` (**mobile**) | `cmp w22, #0x162; b.hi` | `NurseCannotRemoveDebuff` | idem, ao curar |
| `0xF48C1C` | `NPC.RequestBuffRemoval` | `cmp w19, #0x162; b.hi` | `CanBeRemovedByNetMessage` | não remove |
| `0x104C370` | `Player.LoadPlayer` | `cmp w9, #0x163; csel w28, w27, wzr, lt` | — | **save:** o buff some ao carregar o `.plr` (auditoria 01, 4.2). Deve ficar como está |

**LOOP (4):**

| RVA | método | instrução | fase |
|---|---|---|---|
| `0xDAF6A0` | `NPC.SetDefaults` | `cmp x8, #0x163; b.ne` (zera `buffImmune`) | B e E |
| `0x150C19C` | `AssetInitializer.LoadTextures` | `cmp w23, #0x162; b.gt` (texturas 1 a 354) | D |
| `0x15172E4` | `AssetInitializer.LoadTextures_InSteps` (iterador) | `cmp w23, #0x163; b.lt` | D |
| `0x158C7EC` | `NPCDebuffImmunityData.ApplyToNPC` | `cmp x22, #0x183` (N+32) | B e E |

**Fora da conta:** 131 IDX (as atribuições por ID em `Main.Initialize_TileAndNPCData1`, `NPC.SetDefaults`,
`NPC.UpdateNPC_BuffSetFlags` e afins) e as comparações com IDs de NPC, tile e projétil entre 352 e 363,
que são muitas: são números comuns.

**Formatos fora da memória** (auditoria 01): `int` tipo e `int` tempo por buff no `.plr` (P8); `ushort`
nas mensagens 50, 53, 54 e 55 (N10 a N13). Nenhum grava a contagem.

### 3.2 Tabelas indexadas pelo tipo

| tabela | tamanho | quem aloca | leituras | observação |
|---|---:|---|---|---|
| `TextureAssets.Buff` | 355 | `TextureAssets..cctor` | 13 em 7 métodos | `GUIBuffs.Draw`, `GetRegion`, `ItemDraw`, `GUIControllerBuff`, `Main.DrawBuffIcon`, carga |
| `Main.debuff` | 355 | `Main..cctor` | 8 em 8 | — |
| `Main.buffNoSave` | 355 | `Main..cctor` | 3 em 2 | lida em `Player.InternalSavePlayerFile` (`0x104505C`): é a que cancela o save |
| `Main.buffNoTimeDisplay`, `pvpBuff`, `persistentBuff`, `meleeBuff`, `buffDoubleApply` | 355 | `Main..cctor` | de 0 a 8 | — |
| `Main.vanityPet`, `lightPet` | 355 | `Main..cctor` | 23 e 25 | pets |
| `Main.buffAlpha` | 355 | `Main..cctor` | 13 em 2 | `float` |
| `Lang._buffNameCache`, `_buffDescriptionCache` | 355 | `Lang..cctor` | 2 e 2 | `Lang.GetBuffName` (`0x11F5714`) indexa sem conferir a faixa: tipo ≥ 355 lança exceção (`0x11F576C`–`0x11F5794`) |
| `BuffID.Sets.*` (8) | 355 | fábrica | — | `BasicMountData` liga buff a montaria |
| `Player.buffImmune`, `NPC.buffImmune` | 355 | construtores | — | **instância** |

**Escapes:** nenhum. Confirmado no binário.

### 3.3 Como o jogo cria, atualiza, desenha, salva e envia

| etapa | métodos (RVA) | thread | o que importa para o mod |
|---|---|---|---|
| criar | `Player.AddBuff` (`0xF8410C`, 226 chamadas) e auxiliares (`AddBuff_DetermineBuffTimeToAdd` `0xF84548` e outros); `NPC.AddBuff` (`0xF4866C`) | P | `Player.AddBuff` lê `Main.pvpBuff`, `BuffID.Sets.IsFedState` e `buffImmune[tipo]`; `NPC.AddBuff`, `Main.debuff` e `buffImmune[tipo]`. Tipo ≥ 355 lança exceção |
| atualizar | `Player.Update` → `Player.UpdateBuffs` (`0xF9BD80`, só `Update` chama; 5.228 instruções, 9 tabelas de salto); NPC: `UpdateNPC_BuffSetFlags` (`0xF4CC0C`), `UpdateNPC_BuffApplyDOTs` (`0xF4D5F4`), `UpdateNPC_BuffClearExpiredBuffs` (`0xF4D468`) | P | o efeito é um `switch` pelo tipo; tipo desconhecido não faz nada. `UpdateBuffs` lê `Main.debuff`, `TimeLeftDoesNotDecrease`, `BasicMountData` e `buffImmune` |
| remover | `Player.DelBuff` (`0xF84408`) compacta o array; `GUIBuffs.RemoveBuff` (`0x14B99D4`) no toque | P | a posição de um buff muda quando outro sai |
| desenhar | `Main.GUIBarsDrawInner` → `GUIBuffs.Draw` (`0x14BA3EC`) e `ItemDraw` (`0x14BACB4`), do mobile; `Main.DrawBuffIcon` (`0xD09294`) | P | ícone em `TextureAssets.Buff[tipo]`, nome em `Lang.GetBuffName` |
| salvar | `.plr`: `InternalSavePlayerFile` grava `int` tipo e `int` tempo, conferindo `Main.buffNoSave[tipo]` (`0x104505C`); `LoadPlayer` descarta ≥ 355 (`0x104C370`) | S, P e R (save); P e Q (load) | auditoria 01, P8 e 4.1: um tipo ≥ 355 cancela **todo** save do personagem. Buff de NPC não é salvo |
| rede | 50 e 54 gravam o `ushort` cru em `buffType`; 53 e 55 chamam `AddBuff` (auditoria 01, N10–N13) | P | `ushort` cabe; 53 e 55 lançam exceção com tipo ≥ 355 |

### 3.4 Casca ou ID próprio

**Casca: não há candidato limpo.**

- **O que tem caso no código de atualização.** Em `UpdateBuffs`, `UpdateNPC_BuffSetFlags` e
  `UpdateNPC_BuffApplyDOTs`, sem filtro de origem, e nos `AddBuff_*`, com origem: 340 dos 354 buffs têm
  algum caso. Sobram 14, e nenhum é neutro:

  | buffs | o que são | evidência |
  |---|---|---|
  | 86, 87, 89, 99 (`WaterCandle`, `Campfire`, `HeartLamp`, `BeetleMight2`) | o jogo os aplica ou procura por ID | chamadas a `AddBuff`, `FindBuffIndex`, `ClearBuff` e `RequestBuffRemoval` com essas constantes. Confirmado no binário |
  | 118, 138, 166, 167, 184 (carrinhos de mina) | dão montaria | as cinco constantes no `BuffID.Sets..cctor`, que grava a `BasicMountData` em `0x152A2B4`; `UpdateBuffs` chama `Mount.SetMount` com ela (`0xF9C418`). Confirmado no binário |
  | 96, 171, 174, 177, 180 (`BeetleEndurance2`, `SolarShield2`, `NebulaUp*2`) | níveis de bônus de armadura, postos e tirados pelo código do conjunto | inferido |

- **O que mais vaza.** Mesmo com a casca certa, vazam as flags da casca:
  - `debuff`: a enfermeira e as imunidades;
  - `buffNoSave` e `persistentBuff`;
  - `pvpBuff`;
  - e as `BuffID.Sets`.
- **Revisão de uma recomendação anterior.** A auditoria 01 (4.4) recomendou a casca de buff. Este
  levantamento mostra que ela não tem recipiente neutro.

**ID próprio: viável, com o save resolvido.**

- **No binário.** 24 pontos. Os dois arrays por instância (`Player.buffImmune` e `NPC.buffImmune`) nascem
  grandes se a constante for corrigida antes de qualquer `new Player` ou `new NPC`, o que o `il2cpp_init`
  garante (fase A; o primeiro `new NPC` é na fase B, auditoria 02).
- **Comportamento.** Um tipo ≥ 355 não tem caso no `switch`: não faz nada sozinho, e o mod age pelo gancho
  de atualização.
- **Desenho.** A barra do celular (`GUIBuffs`) desenha pelo `TextureAssets.Buff[tipo]` e pelo nome do
  `Lang`, depois de estendidos.
- **Save.**
  - Com `Main.buffNoSave` estendida, o save não lança mais exceção.
  - O tipo de mod ainda iria para o `.plr`, e o jogo sem a ponte o descartaria ao carregar.
  - Por isso o TLMod tem de tirar os buffs de mod antes do save e devolvê-los depois, com o arquivo
    paralelo, como faz com itens.

**Veredito: ID próprio.**

- O `GlobalBuff` (efeito em buffs originais) não depende disso e pode vir antes (auditoria 07, passo 12).
- Se for preciso algo antes da infraestrutura, existe o "buff virtual" da auditoria 07: o TLMod guarda tempo
  e efeito e desenha o ícone na própria HUD, fora da barra do jogo. É um paliativo.

---

## 4. Tiles

### 4.1 Constantes no código

**ALLOC (57): 55 arrays e 2 capacidades de `List`.** Todas `mov w1, #0x2b5` (693):

| RVA | método | o que cria | fase |
|---|---|---|---|
| `0x7A2EF4`, `0x7A2F6C` | `TextureAssets..cctor` | `TextureAssets.HighlightMask`, `TextureAssets.Tile` | C |
| `0x7C862C` | `TileMaterials..cctor` | `TileMaterials.MaterialsByTileId` | primeiro acesso (inferido) |
| `0xD75F64` a `0xD76AE0` (39) | `Main..cctor` | as 39 tabelas `Main.tile*` da [4.2](#42-tabelas-indexadas-pelo-tipo), inclusive o array externo de `Main.tileMerge` | B |
| `0xC71130`, `0xC711A8`, `0xC711C4` | `Main.SetupTileMerge` | `Main.tileMerge = new bool[693][]` e um `new bool[693]` **por tipo** | D |
| `0x1427DDC`, `0x1427EA0` | `WorldGen..cctor` | `WorldGen.tileCounts`, `houseTile` | primeiro acesso (inferido) |
| `0x15F36D8`, `0x15F39D0` | `MapHelper.Initialize` | `MapHelper.tileOptionCounts`, `tileLookup` | D |
| `0x15E1820` | `MapHelper.Initialize` | `Color[693][]` local: cores por opção | D |
| `0x1042B74`, `0x1042B8C` | `Player..ctor` | `Player.adjTile`, `oldAdjTile` (`+0x920`, `+0x928`, **por jogador**) | B e E |
| `0x12C15D4` | `SceneMetrics..ctor` | `SceneMetrics._tileCounts` (`+0x90`, **instância**) | inferido |
| `0x12F9EFC` | `WorldGen.ScoreRoom` | array local | E, G |
| `0x7F0234` | `DeadMansChestBiome.PlaceBoulderTrapSpot` | array local | G |
| `0x149D290` | `TileObjectData.Initialize` | `new List<TileObjectData>(693)` | D |
| `0x14CB3D0` | `TileObjectData.SetupBaseObject` | `SubTiles = new List(693)` | D |

**FACTORY (1).** `TileID.Sets..cctor` (`0x1583BAC`), `mov w1, #0x2b5` em `0x158406C`, grava a fábrica em
`TileID.Sets.Factory` (`0x1584084`). Cria 127 arrays: 108 em `TileID.Sets`, 15 em `Sets.Conversion`, 3 em
`Sets.TileCutIgnore` e 1 em `Sets.ForAdvancedCollision`. São 119 `bool`, 6 `int`, 1 `ushort`
(`GemsparkFramingTypes`) e 1 `Nullable<bool>` (`DrawTileInSolidLayer`).

**BOUND (33).** Quase todos são o mesmo padrão: `Tile.get_type`, `and w8, w0, #0xffff`, `cmp w8, #0x2b4`
(692), `b.hi`, e depois uma tabela do tipo. Ou seja, "se o tipo for conhecido, consulte a tabela". Um tile de
mod pula o comportamento, sem exceção:

| RVA | método | tabela depois do teste | efeito com tipo ≥ 693 |
|---|---|---|---|
| `0x12F2284`, `0x12F22A8` | `WorldGen.PlaceTile` | — (`cmp w23, #0x2b4` com o argumento `Type`) | **recusa pôr o tile** |
| `0x13B2D8C` | `WorldGen.PlaceObject` | — (argumento `type`) | **recusa pôr o objeto** |
| `0x12E5518`, `0x12E8128`, `0x12E81EC`, `0x12EA4F4`, `0x12EBD08` | `WorldGen.TileFrameDoWork` | `Conversion.Grass`, `Mud`, `OreMergesWithMud`, `Dirt` | sem as regras de moldura dessas famílias |
| `0x13E8EA8` | `WorldGen.KillTile_GetItemDrops` | guarda da tabela de salto de todos os tipos | **não solta item** |
| `0x13D66A0` | `WorldGen.KillWall` | `FramesOnKillWall` | — |
| `0x139D1DC`, `0x13B6F74`, `0x139C468` | `Check2x1`, `Check2x2Style`, `CheckPile` | `Conversion.Sand`, `Main.tile`, `Snow` | — |
| `0x13DD544` | `WorldGen.WouldTileReplacementWork` | `Conversion.Grass`, `tileRope` | — |
| `0x13927A8`, `0x13D630C`, `0x13C7D54` | `HasValidGroundFor*`, `BlockBelowMakesSandConvertIntoHardenedSand` | `Conversion.*`, `tileSolid` | — |
| `0x1327FD4`, `0x1328078`, `0x1328400`, `0x1328594`, `0x1328638`, `0x13288AC` | `WorldGen.Convert` | `cmp w8, #0x2b5` (693), dentro do `switch` da conversão | não converte |
| `0x13EEA94` | `WorldGen.hardUpdateWorld` | `cmp w8, #0x2b3` (691); `CanGrowCrystalShards` | — |
| `0x11C21AC` | `Collision.IsWorldPointSolid` | `cmp w8, #0x2b5`; uma `TileID.Sets` | pula a checagem |
| `0x11E2124` | `DelegateMethods.SpreadDirt` | — | — |
| `0x78057C`, `0x781BF4` | `SmartCursorHelper.Step_PumpkinSeeds`, `Step_Boulders` | `Boulders` | cursor inteligente |
| `0x7F096C`, `0x7F0D40` | `DeadMansChestBiome.FindDartTrapSpotSingle`, `IsGoodSpotsForExplosive` | `IsAContainer` | geração |
| `0x1485B70` | `WorldGen.<GenerateWorld>b__277_104` | `cmp w8, #0x2b5`; `Dirt` | geração |
| `0x117B220` | `Projectile.AI_006` | `CommonSapling` | pó de purificação |
| `0x1324D24` | `WorldGen.ValidateTypes` | `cmp w8, #0x2b5; b.hs` | sem chamador direto; só registra erro (`Debug.LogError`) |

**LOOP (13):**

| RVA | método | instrução | fase |
|---|---|---|---|
| `0xC712E0` | `Main.SetupTileMerge` | `cmp x19, #0x2d5` (N+32): calcula a `tileMerge` | D |
| `0xFFD5BC`, `0xFFD8CC` | `Player.AdjTiles` | `cmp x10, #0x2b5` e `#0x2d5` | E |
| `0x1043448` | `Player..ctor` | `cmp x10, #0x2d5` | B e E |
| `0x15E188C` | `MapHelper.Initialize` | `cmp x22, #0x2b5` | D |
| `0x149D298` | `TileObjectData.Initialize` | `mov w28, #0x2b5` … `subs w28, w28, #1` (`0x149D314`): põe 693 entradas na lista | D |
| `0x14F6224` | `WorldFile.SaveFileFormatHeader` | `cmp x22, #0x2d5`: grava os bits de `tileFrameImportant` | **save** |
| `0x1603834`, `0x16039C0` | `MapHelper.InternalSaveMapCompressed` | `cmp x20, #0x2bd` (N+8) | **save do mapa** |
| `0x1604A28`, `0x1604BDC` | `MapHelper.InternalSaveMap` | `cmp x20, #0x2bd` | **save do mapa** |
| `0x160787C`, `0x16087D4` | `MapHelper.LoadMapVersion2`, `LoadMapVersionCompressed` | `cmp x20, #0x2b5`, `cmp x29, #0x2b5` | **load do mapa** |

**FORMATO (3).**

- `WorldFile.SaveFileFormatHeader` grava 693 como quantidade de flags de "moldura importante"
  (`mov w1, #0x2b5` em `0x14F619C`, `Write(short)` virtual em seguida).
- `MapHelper.InternalSaveMapCompressed` (`0x16036F4`) e `InternalSaveMap` (`0x16048DC`) gravam 693 no
  cabeçalho do `.map`.

**Fora da conta:** 22 IDX, inclusive as checagens de índice constante do `MapHelper.Initialize`
(`cmp w8, #0x272` … em `0x15E4818`), e os IDs 690 a 692 como tile, NPC ou projétil.

### 4.2 Tabelas indexadas pelo tipo

| tabela | tamanho | quem aloca | leituras | observação |
|---|---:|---|---|---|
| `Main.tileSolid`, `tileSolidTop` | 693 | `Main..cctor` | 547 em 249 e 292 em 139 | colisão |
| `Main.tileFrame`, `tileFrameCounter` | 693 | `Main..cctor` | 206 e 180 | animação (`Main.AnimateTiles` `0xCB3430`) |
| `Main.tileCut`, `tileMoss`, `tileDungeon`, `tileNoAttach`, `tileRope`, `tileTable`, `tileLargeFrames`, `tileStone`, `tileShine2`, `tileMergeDirt`, `tileAlch`, `tileBlockLight`, `tileLighted`, `tileBrick`, `tileHammer`, `tileNoSunLight`, `tileSign`, `tileAxe`, `tileContainer`, `tileLavaDeath`, `tileOreFinderPriority`, `tileNoFail`, `tileObsidianKill`, `tileSand`, `tileWaterDeath`, `tileBlendAll`, `tileCracked`, `tileFlame`, `tileGlowMask`, `tilePile`, `tileSpelunker`, `tileBouncy`, `tileShine` | 693 | `Main..cctor` | de 0 a 53 | flags e valores por tipo |
| `Main.tileFrameImportant` | 693 | `Main..cctor` | 38 em 35 | decide se o save e a rede gravam a moldura |
| `Main.tileMerge` | 693 × 693 | `Main..cctor` (externo), `SetupTileMerge` (internos) | 3 em 2 | **quadrada**: 480 mil bools hoje |
| `TextureAssets.Tile`, `HighlightMask` | 693 | `TextureAssets..cctor` | 26 em 10; 5 em 3 | `TileDrawing.DrawTiles`, `GetTileDrawData`, `GetTileDrawTexture`, `Main.LoadTiles`, `TileObject.DrawPreview`… |
| `TileID.Sets.*` (127) | 693 | fábrica | — | — |
| `WorldGen.tileCounts`, `houseTile` | 693 | `WorldGen..cctor` | 38 em 8; 6 em 3 | o load do mundo incrementa `tileCounts[tipo]` |
| `MapHelper.tileLookup`, `tileOptionCounts` | 693 | `MapHelper.Initialize` | 15 em 7 cada | cor e opção de cada tipo no mapa |
| `TileMaterials.MaterialsByTileId` | 693 | `.cctor` | 4 em 3 | som e partículas por material |
| `TileObjectData._data` | lista de 693 | `TileObjectData.Initialize` | — | objetos de vários tiles (móveis) |
| `Player.adjTile`, `oldAdjTile`; `SceneMetrics._tileCounts` | 693 | construtores | — | **instância** |

**Escape (1), confirmado no binário.** `TileDrawing..ctor` (`0x8A7D84`, chamado no
`Initialize_AlmostEverything`) copia a referência da `TileID.Sets.CacheSpecialDrawTree` para um campo do
objeto:

```
0x8a7e58: ldr  x8, [x8, #0x68]   ; static TileID.Sets.CacheSpecialDrawTree
0x8a7e60: str  x8, [x20, #0x10]  ; TileDrawing.cacheSpecialDrawTree
```

Com a constante corrigida no `il2cpp_init`, a tabela já nasce grande e o escape não importa. Se a tabela for
realocada depois, esse campo também tem de ser trocado.

O outro candidato da varredura, `Main.tileSign` em `Player.TileInteractionsUse` (`0xFF0980`), é falso: o
bloco que grava é alcançado por salto, e ali o registrador guarda outra coisa. A leitura da tabela fica em
`0xFF06E0`, e o caminho dela não chega à gravação.

### 4.3 Como o jogo cria, atualiza, desenha, salva e envia

**O mundo do mobile é diferente do PC.** `Main.tile` é um `TileData`: um banco em memória nativa, com
deduplicação. `Tile` é só um índice (`_tileOffset`). Os campos ficam em vetores nativos estáticos:

- `TileData.TileType` (`ushort*`);
- `TileLookup` (`uint*`, posição → registro);
- `TileReferenceCount`, `TileFrameX`/`Y`, `TileWall`…

O tipo é `ushort`, então a faixa até 65535 cabe no armazenamento. Confirmado na metadata.

| etapa | métodos (RVA) | thread | o que importa para o mod |
|---|---|---|---|
| criar | `WorldGen.PlaceTile` (`0x12F204C`, 213 chamadas, quase todas da geração); `PlaceObject` (`0x13B2D2C`); `TileObject.Place` (`0x12CB28C`); `Player.PlaceThing_Tiles` (`0x1009B68`, pelo `createTile` do item); `Tile.set_type` (`0x12C54A0`) | G e P | `PlaceTile` e `PlaceObject` recusam tipo ≥ 693 (BOUND) |
| moldura e quebra | `WorldGen.TileFrame` (`0x1331164`) → `TileFrameDoWork` (`0x12E43B4`); `KillTile` (`0x1303528`, 238 chamadas) → `KillTile_GetItemDrops` (`0x13E8C68`), `KillTile_MakeTileDust` (`0x13DE1C0`) | P e G | moldura, poeira e item solto são `switch` pelo tipo |
| atualizar | `Main.DoUpdateInWorld` → `WorldGen.UpdateWorld` (`0x13F7CB8`) → `UpdateWorld_OvergroundTile` (`0x13F908C`), `UndergroundTile` (`0x13FECFC`), `GrassGrowth`; `hardUpdateWorld`; `Main.DoUpdate` → `AnimateTiles` (`0xCB3430`) | P; H na conversão | atualização aleatória por tipo |
| desenhar | `TileDrawing.Draw` (`0x8CDC08`) ← `Main.DrawCapture`, `DrawTiles`, `RenderTiles`, `RenderTiles2`, `DoDraw_Tiles_NonSolid`; `TileDrawing.DrawTiles` (`0x8AF41C`) ← `DrawTilesThreaded` e `ThreadedRenderManager.RenderThreadInstance.DrawThread` (`0xAB6700`) | **T** e P | as threads de render são `ManagedThread` (`RenderThreadInstance.ThreadHandle`, `+0x78`), criadas em `RestartThread` (`0xAB5558`) por `ThreadStart` e `ThreadManager.RunCritical`. Luz: `TileLightScanner.ApplyTileLight` (`0x755EB0`) |
| mapa | `WorldMap.Update`, `UpdateLighting`, `UpdateType` → `MapHelper.CreateMapTile` (`0x1600140`) | inferido: P | lê `MapHelper.tileLookup[tipo]` |
| salvar | `SaveWorld_Version2` → `SaveFileFormatHeader` (`0x14F60D4`) → **`SaveWorldTilesFast`** (`0x14F7C58`, do mobile; o `SaveWorldTiles` `0x14F9704` não tem chamador) | S e P | tipo em 1 ou 2 bytes (`lsr #8`); `tileFrameImportant[tipo]` com checagem: tipo ≥ 693 lança exceção e o mundo não é salvo |
| carregar | `LoadWorld_Version2` → `LoadFileFormatHeader` (`0x14F5078`: lê a quantidade e os bits) → `LoadWorldTiles` (`0x14FCB70`) | C | `importance[tipo]` (`0x14FCE7C`–`0x14FCE84`) e `WorldGen.tileCounts[tipo]` (`0x14FD2EC`…): exceção com tipo desconhecido |
| mapa salvo | `MapHelper.InternalSaveMap` (`0x1604458`) e `InternalSaveMapCompressed`: 693 e as quantidades de opção por tipo | S e P | o `.map` também tem formato por tipo (FORMATO e LOOP acima) |
| rede | 10 (seção): `CompressTileBlock_Inner` (`0xDA90E8`) / `DecompressTileBlock_Inner` (`0xDAB1F4`); 20 (quadrado): caso `0xD79948`; 17 (manipulação): `ReadInt16` do tipo → chamada a `WorldGen.PlaceTile` em `0xD80184`; 79 (objeto): `ReadInt16` → chamada a `WorldGen.PlaceObject` em `0xD842AC` | P (recepção) | 10 e 20 gravam o tipo **antes** de indexar `tileFrameImportant[tipo]` (`0xDAB4CC` → `0xDAB4EC`–`0xDAB4FC`; `0xD79FB4` → `0xD79FE0`): exceção e tile corrompido. 17 e 79 esbarram no BOUND de `PlaceTile` e `PlaceObject` |

**O save rápido do mobile**, confirmado no binário:

```
0x14f7d64: ldr  x25, [x9, #0xe38]    ; Main.tileFrameImportant, lida uma vez para o laço
0x14f7ee8: ldrh w10, [x10, x8, lsl #1] ; TileType[TileLookup[posição]]
0x14f7eec: cmp  w10, w11             ; w11 = comprimento da tabela
0x14f7ef0: b.hs #0x14f83c4           ; -> bl 0x642c40: IndexOutOfRangeException
```

### 4.4 Casca ou ID próprio

**Casca: inviável.**

- **Não há onde guardar a identidade.**
  - Num item ou NPC, a casca convive com o conteúdo de mod no mesmo objeto (`Item.netID`, o slot do NPC).
  - Um tile não tem campo livre, e o `TileData` deduplica registros iguais: a identidade teria de ir para uma
    estrutura por posição do mundo.
  - Essa estrutura seria consultada em moldura, quebra, colisão, luz, mapa e desenho.
- **Todo tipo tem caso especial.** Nos métodos centrais, pelo critério estrito, os 692 tipos têm algum caso:
  - `KillTile_MakeTileDust`: 692;
  - `KillTile_GetItemDrops`: 683;
  - `TileFrameDoWork`: 623;
  - `PlaceTile`: 541;
  - `ApplyTileLight`, `DrawTiles_EmitParticles` e `DrawTiles`: de 261 a 275.

  A casca soltaria o item da casca, faria a poeira dela e emendaria como ela.
- **O desenho roda em threads de render.** Um gancho de troca de textura por tile rodaria fora da P, no
  caminho mais quente do jogo.
- **Persistência.** O mundo salvo teria a casca. O jogo sem a ponte veria o tile da casca, o que até é seguro,
  mas o TLMod precisaria de um arquivo paralelo por posição.

**ID próprio: o caminho, e o mais caro.**

- **No binário:** 107 pontos. A `Main.tileMerge` é quadrada: com IDs a partir de 4096, seria uma matriz de
  mais de 16 milhões de bools; com IDs logo depois de 693, cresce pouco.
- **No save do `.wld`:**
  - o save rápido lança exceção com tipo ≥ 693 se a tabela não crescer;
  - e mesmo crescendo, o mundo gravado teria tipos que o jogo sem a ponte não carrega (`LoadWorldTiles`
    lança exceção).
  - Logo, o TLMod tem de tirar os tiles de mod antes do save e devolvê-los depois, com o arquivo paralelo,
    como faz com os itens dos baús.
  - O cabeçalho pode continuar gravando 693 (FORMATO), o que mantém o arquivo legível pelo jogo original.
- **No `.map`:** o mapa guarda o "lookup" por posição e a contagem 693. O tile de mod precisa virar um tipo
  original no mapa salvo, ou o formato muda. Só o aparelho confirma o que o jogo original faz com um
  `.map` assim ([E7](#e7-mapa-com-tile-de-mod)).
- **No comportamento:**
  - colocar, quebrar e soltar item exigem ganchos (`PlaceTile` e `PlaceObject` recusam, `GetItemDrops` não
    solta);
  - a moldura de um tile de mod precisa de regra própria (`TileFrameDoWork`);
  - móveis precisam de entradas em `TileObjectData`.
- **No desenho:** com `TextureAssets.Tile[tipo]` estendida, o desenho original funciona sozinho nas threads
  de render, sem gancho por tile.

**Veredito: ID próprio, por último.** Depois de a infraestrutura ser provada com projéteis e de o padrão de
save paralelo ser provado com buffs. Paredes (`WallID.Count`, que no mobile é `static readonly`) ficam para
um estudo próprio.

---

## 5. O que vale para os três

### 5.1 Onde corrigir as constantes

A auditoria 02 mostrou que o `Program.LaunchGame` do estudo nunca roda no celular e que o ponto seguro é o
`il2cpp_init`, antes de qualquer código gerenciado (fase A).

Para os três tipos, isso basta:

- **Antes de serem criadas.** Todas as tabelas nascem nas fases B a D:
  - `Main..cctor` no `Awake`;
  - `TextureAssets..cctor` e `Lang..cctor` no `Start`;
  - `SetupTileMerge`, `MapHelper.Initialize`, `TileObjectData.Initialize` e `TileDrawing..ctor` no
    `Initialize_AlmostEverything`;
  - as fábricas de `Sets`, no primeiro acesso.

  Corrigidas na fase A, nascem no tamanho novo.
- **Os arrays de instância** (`Player.ownedProjectileCounts`, `buffImmune`, `adjTile`;
  `NPC.buffImmune`; `SceneMetrics._tileCounts`) também nascem grandes, porque o primeiro `new Player` e o
  primeiro `new NPC` são da fase B.
- **A quantidade de cada tipo é conhecida antes.** O manifest já tem as listas `content.projectiles`,
  `content.buffs` e `content.tiles` (`manifest.cpp:14–15`). Falta lê-lo antes do `il2cpp_init` (auditoria
  02, ação 2).

Isso é inferido da linha do tempo da auditoria 02. Os `.cctor` novos deste módulo ainda pedem a conferência
no aparelho ([E1](#e1-flags-dos-cctor-dos-novos-tipos-no-il2cpp_init)).

### 5.2 IDs contíguos: o registro precisa mudar

O `ContentRegistry` reserva projétil, tile, parede e buff a partir de 4096 (`content_registry.cpp:27–30`).
Para a casca isso é indiferente, porque o ID de mod nunca vai para o jogo. Para o ID próprio, não funciona:

- **Projétil e buff.** Os BOUND e LOOP desses tipos são `cmp` com imediato de 12 bits. O valor novo cabe só
  até 4095; com IDs a partir de 4096, nenhum deles pode ser corrigido em 4 bytes.
- **Tile.** Além disso, a `tileMerge` cresce com o quadrado do maior tipo.
- **Saída.** Separar o ID de registro, estável e persistente por nome, do tipo no jogo, contíguo depois da
  contagem original (1022, 355 e 693). O estudo de itens já propunha começar em 5456 (estudo, 6.1).

### 5.3 Texturas e nomes

- **ID próprio.**
  - O desenho original passa a funcionar com o tipo de mod, desde que haja um `Asset<Texture2D>` em
    `TextureAssets.Projectile`, `.Buff` ou `.Tile` e um `LocalizedText` em `Lang`.
  - O TLMod de hoje troca a textura no desenho (`draw_hooks.cpp`) e não cria `Asset`.
  - Criar e manter vivos esses objetos gerenciados é trabalho novo (estudo, risco 8).
- **Casca de projétil.** Basta o modelo de hoje: troca de textura no `Main.DrawProj` (`0xCD0D70`).

### 5.4 Persistência e rede

| tipo | `.plr` | `.wld` | `.map` | rede | tipo de mod sem tabela estendida |
|---|---|---|---|---|---|
| projétil | — | — | — | `short` (27) | exceção no `SetDefaults` de quem recebe |
| buff | `int` (P8) | — | — | `ushort` (50, 53, 54, 55) | exceção no save do personagem, no `AddBuff` e no `GetBuffName` |
| tile | — | 1 ou 2 bytes, mais a moldura se `tileFrameImportant` | lookup por posição e 693 | 1 ou 2 bytes (10), `ushort` (20), `short` (17, 79) | exceção no save e no load do mundo e nas seções de rede; recusa em `PlaceTile`/`PlaceObject` |

Para buff e tile, vale o que o estudo disse para itens: gravar o arquivo do jogo **sem** o conteúdo de mod e
guardar o conteúdo de mod no arquivo paralelo, pelo nome persistente. No multiplayer, os dois lados precisam
das mesmas tabelas; o TLMod é de um jogador só (README, "Ainda não implementado").

### 5.5 O que cada tabela vira no `register_*`

Cada tabela por tipo é um campo que o `register_*` precisa aceitar, ou que o TLMod preenche com o padrão:

- **`TLModProjectileDef`:**
  - `hostile`, `hook`, `pet` e `frames` (as 4 de `Main`);
  - as 42 `ProjectileID.Sets`, das quais as mais usadas são `TrailCacheLength`/`TrailingMode`,
    `MinionShot`, `SentryShot`, `IsAWhip`, `CultistIsResistantTo` e `NeedsUUID`;
  - nome e textura;
  - na casca, `shell_type` como dica.
- **`TLModBuffDef`:**
  - `debuff`, `no_save`, `no_time_display`, `pvp`, `persistent`, `melee`, `double_apply`, `vanity_pet`,
    `light_pet` e `alpha` (as 10 de `Main`);
  - as 8 `BuffID.Sets`;
  - nome, descrição e ícone.
- **`TLModTileDef`:**
  - as 39 de `Main` (sólido, topo sólido, moldura importante, luz, bloqueia luz, corta, morre na lava…);
  - as regras de emenda (`tileMerge`, `tileMergeDirt`, `tileBlendAll`);
  - as `TileID.Sets` que importarem;
  - o objeto de vários tiles (`TileObjectData`);
  - a cor e o nome no mapa (`MapHelper`, `Lang._mapLegendCache`);
  - o material (`TileMaterials`);
  - o item solto;
  - a textura.

---

## 6. O TLMod hoje

| o quê | onde | situação |
|---|---|---|
| faixas de ID | `content_registry.cpp:27–30` (comentário em `:16–19`) | projétil 4096–32767 (`short`), tile e parede 4096–65535 (`ushort`), buff 4096–65535 (`ushort` na rede). Serve para casca; impede o ID próprio ([5.2](#52-ids-contíguos-o-registro-precisa-mudar)) |
| manifest | `manifest.cpp:14–15` | aceita `content.projectiles`, `tiles`, `walls` e `buffs`: a quantidade de cada tipo já pode vir do JSON |
| API | `tlmod_api.h:63–69` (tipos de conteúdo), `:277` (`register_item`), `:288` (`register_npc`) | não há `register_projectile`, `register_buff` nem `register_tile`. O README lista os três como não implementados (`modapi/README.md:657`); a mesma linha ainda cita NPC, que já tem `register_npc` |
| ganchos | `il2cpp_bridge/src/*.cpp` | nenhum em `Projectile.*`, `AddBuff`/`UpdateBuffs`, `PlaceTile`/`KillTile` ou `TileDrawing` (busca no código) |
| casca de item que aplica buff | `item_shell.cpp:77–79` | o runtime já avisa quando a casca de item aplica o buff dela: o mesmo tipo de aviso serviria para a casca de projétil |
| offsets | `terraria_offsets.h:111`, `:123` | só `Item.createTile` e `Item.buffType`, usados nos traços de item |
| save paralelo | `save_hooks.cpp` | só itens de mod. É o padrão que buffs e tiles vão usar |
| texturas | `draw_hooks.cpp` | troca de textura no desenho de item e NPC; não cria `Asset<Texture2D>` |

---

## 7. Recomendação por tipo e ordem sugerida

| tipo | recomendação | por quê |
|---|---|---|
| **projétil** | **casca agora, ID próprio quando houver a infraestrutura** | maior valor (22% dos itens de mod disparam projétil de mod, auditoria 07), casca viável com recipiente conferido, ID próprio barato (16 pontos, sem save) e troca sem migrar saves |
| **buff** | **ID próprio** (o `GlobalBuff` pode vir antes; "buff virtual" só como paliativo) | não há buff original neutro para casca; o ID próprio custa 24 pontos e o save paralelo do `.plr` |
| **tile** | **ID próprio, por último** | a casca é inviável; o ID próprio é o mais caro (107 pontos, `.wld`, `.map`, `tileMerge`, rede) e precisa da infraestrutura e do padrão de save já provados |

**Ordem e esforço** (inferido):

| passo | o quê | inclui | esforço |
|---|---|---|---|
| 1 | projétil por casca | `register_projectile` com a casca como dica; ganchos em `NewProjectile`, `AI`, `Damage`, `Kill` e `DrawProj` (o passo 6 da auditoria 07); checagem das tabelas da casca no registro | 4–6 dias |
| 2 | infraestrutura de ID próprio | gancho no `il2cpp_init` e leitura antecipada dos manifests (auditoria 02); tipo no jogo separado do ID de registro; tabela de assinaturas por método com falha fechada; realocação de reserva | 6–9 dias |
| 3 | projétil com ID próprio | os 16 pontos; `SetDefaults` para tipos de mod; as 42 `Sets`, as 4 de `Main` e os nomes; `Asset<Texture2D>` | 5,5–9 dias |
| 4 | buff com ID próprio | os 24 pontos, sem o `LoadPlayer`; tabelas, nomes e ícones; tirar e devolver buffs de mod no save do `.plr`; ganchos de atualização de jogador e NPC | 9–13 dias |
| 5 | tile com ID próprio | os 107 pontos, `tileMerge`, `TileObjectData`, `MapHelper` e o escape do `TileDrawing`; tirar e devolver tiles de mod no `.wld` e no `.map`; ganchos de colocar, quebrar, soltar e moldura | 26–38 dias |

**Notas:**

- **Itens.** O estudo recomendou estrear a infraestrutura com itens (44 pontos, com saves). Estreá-la com
  projéteis é menos arriscado: menos pontos e nada salvo. Itens seguem logo depois, com a mesma
  infraestrutura.
- **Passo 1 mais passo 3 não é retrabalho.** Os ganchos de comportamento da casca (IA, acerto, morte) ficam;
  muda só como o projétil é identificado.

---

## 8. Experimentos no aparelho

Com uma build de teste da ponte, as Ferramentas do TLMod e o `tlmod.log`. Os que mexem em save usam um
personagem e um mundo descartáveis.

### E1: flags dos `.cctor` dos novos tipos no `il2cpp_init`

O E1 da auditoria 02, estendido. No gancho do `il2cpp_init`, e de novo no fim do primeiro quadro, registrar
`cctor_started` (`+0xDC`) e `cctor_finished` (`+0xE0`) de `ProjectileID.Sets`, `BuffID.Sets`, `TileID.Sets`
(e as quatro subclasses), `TileMaterials`, `WorldGen`, `Main`, `TextureAssets` e `Lang`.

**Esperado:** tudo zero no `il2cpp_init`; `Main` pronto depois do `Awake`.

### E2: projétil acima da contagem sem tabela

Comando de teste `proj <tipo>` que chama `Projectile.NewProjectile` perto do jogador.

**Esperado:**

- com `proj 1021`, nada estranho;
- com `proj 1022`, uma `IndexOutOfRangeException` no `SetDefaults`, vinda de `TrailCacheLength` (por volta
  de `0x106796C`), no `logcat` e no `tlmod.log`.

Confirma que basta estender as tabelas para o tipo nascer.

### E3: casca de projétil

Projétil de teste com casca 360 (boia), 367 (lança) e 1 (flecha), com um `on_ai` que zera `aiStyle` e move o
projétil em linha reta. Deixar cada um morrer contra um bloco e contra um inimigo.

**Esperado:** nas cascas 360 e 367, nenhum efeito da casca ao morrer ou acertar; na 1, a poeira e o som de
flecha. Mostra quanto vaza da lista de candidatas.

### E4: buffs candidatos a casca

Comando de teste `buff <tipo> <segundos>` que escreve `buffType[0]` e `buffTime[0]`, com os tipos 96, 118 e
171. Registrar `buffType` e a montaria do jogador a cada segundo.

**Esperado:** o 118 monta o carrinho; 96 e 171 somem ou são trocados pelo código de armadura. Confirma que
não há casca neutra.

### E5: save do mundo com tile acima da contagem

Num mundo descartável, um comando de teste põe `type = 693` num tile perto do jogador (`Tile.set_type`).
Depois, "Salvar e sair".

**Esperado:** exceção no `SaveWorldTilesFast` (por volta de `0x14F7EF0`), o `.wld` com a mesma data de antes
e, possivelmente, o jogo preso na tela de save, como no caso do buff da auditoria 01.

### E6: threads do desenho de tiles

Gancho de teste em `TileDrawing.DrawTiles` (`0x8AF41C`) que conta chamadas por segundo e registra o `gettid()`
de cada uma.

**Esperado:** chamadas em threads diferentes da P, uma por `RenderThreadInstance`. Diz se um gancho de tile
precisa ser seguro para várias threads.

### E7: mapa com tile de mod

Depois do passo 5, com as tabelas estendidas: explorar uma área com tile de mod, salvar, abrir o mesmo
personagem e mundo numa instalação sem a ponte.

**Esperado:** saber se o `.map` carrega, se a área aparece com cor errada ou se o jogo lança exceção. Decide
entre converter o tile de mod no mapa salvo ou mudar o formato.

---

## 9. Respostas diretas

**1. Tabelas indexadas pelo tipo e tamanhos.**

- **Projétil (1022):**
  - estáticas: `TextureAssets.Projectile`, as 4 tabelas `Main.proj*`, `Lang._projectileNameCache` e as 42
    `ProjectileID.Sets`;
  - de instância: `Player.ownedProjectileCounts`.
- **Buff (355):**
  - estáticas: `TextureAssets.Buff`, as 10 de `Main` (`debuff`, `buffNoSave`…), os dois caches de `Lang` e
    as 8 `BuffID.Sets`;
  - de instância: `buffImmune` em cada `Player` e cada `NPC`.
- **Tile (693):**
  - as 39 `Main.tile*`, com a `tileMerge` de 693 × 693;
  - `TextureAssets.Tile` e `HighlightMask`;
  - as 127 `TileID.Sets`;
  - `WorldGen.tileCounts` e `houseTile`;
  - `MapHelper.tileLookup`, `tileOptionCounts` e as cores;
  - `TileMaterials`;
  - a lista de `TileObjectData`;
  - de instância: `Player.adjTile`, `oldAdjTile` e `SceneMetrics._tileCounts`.
- **Escapes:** um só, `CacheSpecialDrawTree` → `TileDrawing`.

**2. Constantes no código.** São 16 pontos para projétil, 24 para buff e 107 para tile (tabela no
[Resumo](#resumo)):

- todos `mov #imm16` ou `cmp #imm12` de 4 bytes;
- nenhum blob;
- os de `cmp` limitam o tipo novo a 4095.

Fora da conta ficam as checagens de índice constante (14, 131 e 22) e os IDs específicos.

**3. Ciclo de vida.**

- **Projétil.** Nasce em `NewProjectile` → `SetDefaults`, vive em `Projectile.Update` na P, é desenhado em
  `DrawProj` → `DrawProjDirect`. Nunca é salvo. Na rede vai como `short` (mensagem 27), sem checagem na
  recepção.
- **Buff.** Nasce em `AddBuff`, vive em `UpdateBuffs` e `UpdateNPC_Buff*`, é desenhado pela `GUIBuffs` do
  mobile. É salvo no `.plr` como `int` e vai pela rede como `ushort`.
- **Tile.** Vive no `TileData` nativo, como `ushort`. É posto por `PlaceTile`/`PlaceObject`, emoldurado por
  `TileFrameDoWork` e atualizado por `UpdateWorld`. É desenhado em threads de render. É salvo pelo
  `SaveWorldTilesFast`, com 1 ou 2 bytes e mais a moldura, e vai para o `.map`. Na rede vai em seções e
  quadrados (1 a 2 bytes, `ushort`) e em manipulações (`short`).

**4. Casca ou ID próprio.**

- **Projétil:** os dois servem; casca agora, ID próprio depois, sem migração.
- **Buff:** ID próprio; não há casca neutra.
- **Tile:** só ID próprio, e por último.

**Ordem:**

1. projétil por casca;
2. infraestrutura;
3. projétil com ID próprio;
4. buff;
5. tile.

---

## 10. Lacunas do TLMod, em ordem de risco

1. <a id="lacuna-1"></a>**As faixas do registro impedem o ID próprio** (alto).
   - Projétil, tile e buff começam em 4096 (`content_registry.cpp:27–30`).
   - Os limites no código só aceitam até 4095 em 4 bytes, e a `tileMerge` cresce com o quadrado do tipo.
   - Quem implementar o ID próprio com essas faixas vai esbarrar nisso.
   - Ação 2.
2. <a id="lacuna-2"></a>**Nenhum ponto para corrigir constantes antes do código gerenciado** (alto). Os
   manifests são lidos depois de a ponte ficar pronta, e não há gancho no `il2cpp_init` (auditoria 02).
   Todo ID próprio depende disso. Ação 3.
3. <a id="lacuna-3"></a>**Tile de mod no mundo quebra o save do mundo** (alto, quando houver tiles). O
   `SaveWorldTilesFast` lança exceção com tipo ≥ 693, e o `LoadWorldTiles` também. Não existe arquivo paralelo
   para tiles. Ação 6.
4. <a id="lacuna-4"></a>**Buff de mod em `buffType` quebra o save do personagem** (alto, quando houver
   buffs). É a auditoria 01, P8. A casca, que lá era a saída, não tem candidato neutro. Ação 5.
5. <a id="lacuna-5"></a>**Nenhum gancho de projétil, buff ou tile** (médio). Não há como receber IA, acerto,
   morte, atualização de buff, colocar ou quebrar tile. Ações 1, 5 e 6.
6. <a id="lacuna-6"></a>**Texturas só por troca no desenho** (médio). O ID próprio precisa de
   `Asset<Texture2D>` nas tabelas do jogo, e a ponte ainda não cria esses objetos. Ação 4.
7. <a id="lacuna-7"></a>**Desenho de tiles fora da P** (médio, só tiles). Qualquer gancho no desenho de tile
   roda em threads de render. Ação 6.
8. <a id="lacuna-8"></a>**Escape no `TileDrawing`** (baixo-médio, só tiles). Se uma tabela for realocada
   depois de criada, `TileDrawing.cacheSpecialDrawTree` fica com a antiga. Ação 6.
9. <a id="lacuna-9"></a>**Rede** (fora do alcance agora). A mensagem 27 não confere o tipo, e as seções de
   tile indexam `tileFrameImportant`: os dois lados precisam das mesmas tabelas. O TLMod é de um jogador só.
   Documentar.

---

## 11. Ações recomendadas

Em ordem. A [seção 7](#7-recomendação-por-tipo-e-ordem-sugerida) dá o porquê e o esforço.

1. **Projétil por casca** (4–6 dias):
   - `register_projectile` com a casca como dica opcional, para a API não depender da estratégia;
   - lista de cascas conferidas: as 14 do critério estrito primeiro, depois as do critério com origem;
   - checagem das 42 `Sets` e das 4 tabelas de `Main` da casca no registro, com aviso no log, como no
     `item_shell.cpp`;
   - ganchos em `NewProjectile`, `AI`, `Damage`, `Kill` e `DrawProj`.

   [E3](#e3-casca-de-projétil). [Lacuna 5](#lacuna-5).
2. **Separar o tipo no jogo do ID de registro** (parte da infraestrutura):
   - os tipos de mod no jogo começam em 1022, 355 e 693, contíguos;
   - o ID de registro e o nome persistente continuam para o arquivo paralelo.

   [Lacuna 1](#lacuna-1).
3. **Infraestrutura de ID próprio** (6–9 dias):
   - gancho no `il2cpp_init` e no `il2cpp_init_utf16` (auditoria 02);
   - leitura dos manifests antes, com as quantidades de `content.*`;
   - tabela de assinaturas com os pontos deste relatório (16, 24 e 107) e conferência dos 4 bytes
     originais;
   - falha fechada: volta para a casca, nunca correção parcial;
   - realocação de reserva, que troque também o `TileDrawing.cacheSpecialDrawTree`.

   [E1](#e1-flags-dos-cctor-dos-novos-tipos-no-il2cpp_init). [Lacuna 2](#lacuna-2).
4. **Projétil com ID próprio** (5,5–9 dias):
   - os 16 pontos;
   - gancho em `SetDefaults` para tipos de mod;
   - preencher as tabelas e os nomes;
   - criar `Asset<Texture2D>` em `TextureAssets.Projectile`;
   - testar com o [E2](#e2-projétil-acima-da-contagem-sem-tabela).

   [Lacuna 6](#lacuna-6).
5. **Buff com ID próprio** (9–13 dias):
   - os pontos, deixando o `LoadPlayer` como está;
   - corrigir os dois limites da `GUINPCDialogue` do mobile;
   - tabelas, nomes e ícones;
   - tirar os buffs de mod do `.plr` antes do save e devolvê-los depois, pelo arquivo paralelo;
   - ganchos de atualização de jogador e NPC.

   [E4](#e4-buffs-candidatos-a-casca). [Lacuna 4](#lacuna-4).
6. **Tile com ID próprio** (26–38 dias):
   - os 107 pontos, com `tileMerge`, `TileObjectData` e `MapHelper`;
   - tirar e devolver tiles de mod no `.wld`, mantendo o cabeçalho em 693, e decidir o `.map` com o
     [E7](#e7-mapa-com-tile-de-mod);
   - ganchos de colocar, quebrar, soltar e moldura;
   - desenho pelas tabelas, sem gancho por tile.

   [E5](#e5-save-do-mundo-com-tile-acima-da-contagem), [E6](#e6-threads-do-desenho-de-tiles).
   [Lacunas 3](#lacuna-3), [7](#lacuna-7) e [8](#lacuna-8).
7. **Corrigir o README** (minutos): tirar NPC da lista de tipos sem `register_*` (`modapi/README.md:657`).

Fora do plano por ora:

- paredes, que pedem estudo próprio (`WallID.Count` é `static readonly` no mobile);
- multiplayer ([lacuna 9](#lacuna-9)).
