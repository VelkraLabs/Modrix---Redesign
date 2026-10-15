# Estudo: posições próprias para itens e NPCs de mod (sem casca)

Pergunta: dá para abandonar a casca e dar a cada item/NPC de mod um ID próprio
acima de `ItemID.Count` / `NPCID.Count`, como o tModLoader faz no PC, **sem
alterar o APK** — recriando as tabelas maiores em tempo de execução e corrigindo
os limites na memória, do mesmo jeito que os hooks já são instalados?

Este documento é só investigação. Nada foi implementado.

Binário analisado: `libil2cpp.so` arm64 do Terraria Mobile (Unity 2021.3.56f2,
metadata v31), o mesmo dos offsets do `il2cpp_bridge`. Endereços são RVAs.

- `ItemID.Count = 5456` (`const short`), últimos itens reais 5453–5455.
- `NPCID.Count = 688` (`const short`), últimos NPCs reais 685–687
  (BoundTownSlimeOld/Purple/Yellow); `NPCID.NegativeIDCount = -66`.

---

## 1. Método

1. Varredura de todas as instruções da seção de código cujo imediato é a
   contagem ou uma forma derivada: MOVZ/MOVN (com `hw`), ADD/SUB/ADDS/SUBS
   imediato (`cmp`/`cmn`/`add`/`sub`, com `lsl #12`). Valores procurados:
   - itens: 5453–5464, 5487, 5488, 10912, 10936, 10942, 10944, 21824–21856,
     43648–43680 (N−3…N+8, N+32, 2N, 4N, 8N com e sem +0x20 do cabeçalho do array);
   - NPCs: 685–696, 719, 720, 1376, 1400, 1406, 1408, 2752–2784, 5504–5536.
2. Classificação automática olhando a instrução seguinte (`b.cond`, `cset`,
   destino do `bl`, registrador redefinido) e revisão manual de cada caso que
   não era claramente endereço/pilha.
3. Para cada alocação, o destino do array (`str x0` seguinte, com o slot do
   TypeInfo resolvido para nome de classe/campo).
4. Para os arrays criados por `SetFactory.Create*`: qual fábrica e qual campo.
5. Para cada tabela estática, busca de *escapes*: todo lugar onde a referência
   lida do campo é gravada em outro campo (estático ou de objeto) ou passada
   adiante; também se o mesmo array é guardado em objeto no construtor.
6. Leitura da desmontagem de save/rede (`Player.SavePlayer/LoadPlayer`,
   `WorldFile.SaveWorldHeader/LoadHeader`, `NetMessage.SendData`,
   `MessageBuffer.ProcessData`).

As ferramentas (scripts Python sobre Capstone/pyelftools e o dump do
Il2CppDumper) ficaram fora do repositório, junto com o dump.

### Contagem lida de campo em vez de constante

`ItemID.Count` e `NPCID.Count` são `const`: o compilador C# copia o valor para
cada uso, então **não existe campo estático com a contagem** para trocar. Os
lugares que leem a contagem "de um campo" são indiretos:

- `SetFactory._size` (campo de instância, +0x10) das fábricas guardadas em
  `ItemID.Sets.Factory`, `NPCID.Sets.Factory` e `PrefixLegacy.ItemSets.Factory`.
  `CreateBoolSet/IntSet/FloatSet/UshortSet/CustomSet<T>` leem `_size` e alocam.
  Todos os `Create*` dessas três fábricas rodam dentro de construtores
  estáticos (lista na seção 3).
- Laços e testes escritos como `i < array.Length` (por exemplo
  `TextureAssets.Item.Length`, `ItemID.Sets.X.Length`, `Utils.IndexInRange`):
  esses se adaptam sozinhos a um array maior e não precisam de correção.

---

## 2. Ocorrências classificadas — itens (5456 e derivados)

Classes: **ALLOC** (tamanho de `new T[n]`), **FACTORY** (tamanho de
`SetFactory`), **BOUND** (verificação de limite de tipo), **LOOP** (limite de
laço sobre todos os tipos), **ID** (ID específico 5453–5455, não é contagem),
**OUTRO**.

"Quando": **antes** = roda antes do ponto em que hoje os mods terminam de
carregar (construtores estáticos e `Main.Initialize`); **depois** = gameplay ou
telas abertas pelo jogador. Ver seção 6 sobre a ordem real de carga.

Todas as constantes de item são `mov wN, #imm16` (MOVZ, 4 bytes). Nenhuma está
num `cmp #imm`, porque 5456 não cabe no imediato de 12 bits.

### 2.1 ALLOC (12)

| RVA | Método | Instrução | Tabela criada | Quando |
|---|---|---|---|---|
| 0x7a2dd4 | TextureAssets..cctor | `mov w1,#0x1550` | `TextureAssets.Item` (Asset&lt;Texture2D&gt;[]) | antes |
| 0x7a2dec | TextureAssets..cctor | `mov w1,#0x1550` | `TextureAssets.ItemFlame` | antes |
| 0x7c9c0c | LootSimulationItemCounter..ctor | `mov w1,#0x1550` | `_itemCountsObtained` (instância) | depois (simulador de loot) |
| 0x7c9c1c | LootSimulationItemCounter..ctor | `mov w1,#0x1550` | `_itemCountsObtainedExpert` (instância) | depois |
| 0x7cc914 | ItemVariants..cctor | `mov w1,#0x1550` | `ItemVariants._variants` | antes |
| 0x8fb04c | ItemFilters.MiscFallback..ctor | `mov w1,#0x1550` | `_fitsFilterByItemType` (instância) | depois (filtros da Jornada) |
| 0x9fb66c | VirtualControllerInputState.SetupLookups | `mov w1,#0x1550` | `VirtualControllerInputState.IdLookup` (string[], **mobile**) | antes |
| 0xbb8ef4 | Item..cctor | `mov w1,#0x1550` | `Item.staff` | antes |
| 0xbb8f0c | Item..cctor | `mov w1,#0x1550` | `Item.claw` | antes |
| 0xd76b68 | Main..cctor | `mov w1,#0x1550` | `Main.itemAnimations` | antes |
| 0x120646c | Lang..cctor | `mov w1,#0x1550` | `Lang._itemNameCache` | antes |
| 0x12064fc | Lang..cctor | `mov w1,#0x1550` | `Lang._itemTooltipCache` | antes |

### 2.2 FACTORY (2)

| RVA | Método | Instrução | Arrays criados | Quando |
|---|---|---|---|---|
| 0x153c614 | ItemID.Sets..cctor | `mov w1,#0x1550` → `SetFactory..ctor` | 77 em `ItemID.Sets` (46 bool, 20 int, 2 float, 9 custom) + 3 fora dela com a mesma fábrica: `Player.ItemUsesRightFire` (Player..cctor), `Item.cachedItemSpawnsByType` (Item..cctor), `VirtualControllerInputState.ItemCategories` (**mobile**) | antes |
| 0x7c23a4 | PrefixLegacy.ItemSets..cctor | `mov w1,#0x1550` → `SetFactory..ctor` | 6 bool (`SwordsHammersAxesPicks`, `SpearsMacesChainsawsDrillsPunchCannon`, `GunsBows`, `MagicAndSummon`, `BoomerangsChakrams`, `ItemsThatCanHaveLegendary2`) | antes |

### 2.3 BOUND (24)

| RVA | Método | Instrução / desvio | Efeito | Quando |
|---|---|---|---|---|
| 0xbad174 | Item.SetDefaults | `mov w9,#0x1550` … `b.lt` | tipo ≥ 5456 não monta o item | antes e depois |
| 0xbadbb4 | Item.SetDefaults | `mov w10,#0x154e` … `b.hi` (`tipo−1 > 5454`) | idem, segundo teste | antes e depois |
| 0xbb8a6c | Item.DeserializeFrom | `mov w9,#0x1550` … `b.lt` | item recebido/lido fora da faixa vira ar | depois |
| 0x1047988, 0x1047abc, 0x1047c54, 0x1047df8 | Player.LoadTutorialPlayer | `mov wN,#0x1550` … `b.ge` | inventário/equipamentos/banco com tipo ≥ 5456 viram ar | depois |
| 0x104b5c8, 0x104b6fc, 0x104b894, 0x104ba38 | Player.LoadPlayer | `mov wN,#0x1550` … `b.ge` | idem no `.plr` | depois |
| 0x11f54fc | Lang.GetItemName | `mov w9,#0x154e` … `b.lo` | nome vazio fora da faixa | depois |
| 0x7cec14 | CommonCode.DropItemFromNPC | `mov w9,#0x154e` … `b.hi` | não dropa | depois |
| 0x7cf0a4 | CommonCode.DropItemLocalPerClientAndSetNPCMoneyTo0 | `mov w8,#0x1550` … `cset lt` | idem | depois |
| 0x7cf670 | CommonCode.DropItemForEachInteractingPlayerOnThePlayer | `mov w9,#0x154e` … `b.hi` | idem | depois |
| 0x97e2dc | ItemTagHandler.PrintInline | `mov w27,#0x154f` … `b.le` | tag `[i:ID]` no chat | depois |
| 0x97e868 | ItemTagHandler.MeasureInline | `mov w25,#0x154f` … `b.le` | idem | depois |
| 0xdc9400, 0xdc9548 | ControlsTouchTagHandler.MeasureInline | `mov w25,#0x1550` … `b.le` | tags de ícone nos textos de controle (**mobile**) | depois |
| 0xdc9724, 0xdc9864 | ControlsTouchTagHandler.PrintInline | `mov w27/w28,#0x1550` … `b.le` | idem (**mobile**) | depois |
| 0xfa5c9c | Player.RefreshInfoAccs | `mov w23,#0x1550` … `b.hi` | acessórios informativos | depois |
| 0xfa6df4 | Player.UpdateEquips | `mov w26,#0x1550` … `b.hi` | equipamentos | depois |
| 0x1635be0 | ItemSlot.GetItemLight | `mov w8,#0x1550` … `b.hi` | cor/luz do slot | depois |

### 2.4 LOOP (6)

| RVA | Método | Instrução | Laço | Quando |
|---|---|---|---|---|
| 0xc8e034 | Main.Initialize_Items | `mov w24,#0x1550` | percorre todos os tipos (texturas/animações) | antes |
| 0x152b4d4 | ContentSamples.Initialize | `mov w22,#0x1550` | cria um `Item` de amostra por tipo em `ItemsByType` | antes |
| 0x1530de8 | ContentSamples.CreativeHelper.SetCreativeMenuOrder | `mov w28,#0x154f` (`≤ 5455`) | ordem do menu da Jornada | antes |
| 0x9fb9d0 | VirtualControllerInputState.SaveCodeArrayCategories | `mov w22,#0x1550` | grava categorias por item na config de controles (**mobile**) | antes/depois |
| 0x9fbe34 | VirtualControllerInputState.SaveDefaultCategories | `mov w25,#0x1550` | idem, padrão (**mobile**) | antes/depois |
| 0x8fb07c | ItemFilters.MiscFallback..ctor | `mov w26,#0x1550` (`b.eq` = fim) | preenche `_fitsFilterByItemType` | depois |

### 2.5 IDs específicos (não mexer) e outros

- 5453/5454/5455 como ID de item (livros/variações): `GUIPageOptions.CanBeOpened`
  (0xa8703c, 0xa87048, 0xa87068), `Player.ItemCheck_ManageRightClickFeatures`
  (0xfe265c, 0xfe2668, 0xfe2674, 0xfe26b8, 0xfe274c, 0xfe2754),
  `ItemSlot.TryItemSwap` (0x162c55c, 0x162c568, 0x162c574, 0x162c5a4,
  0x162c668, 0x162c6e8), `Recipe.SetupRecipeGroups` (0x12b35ac, 0x12b36b0),
  `ContentSamples.FillResearchItemOverrides` (0x152c944, 0x152c970, 0x152c99c).
- `mov wN,#-5453` (MOVN) = `tipo − 5453` para testar a faixa 5453–5455:
  `GUIVirtualInputController.AddInteractBanner` 0x9e2148,
  `GUIPageOptions.DrawEquipButton` 0xa87658, `Item.checkMat` 0xbab270,
  `Lang.ToopltipProcessor` 0x11f7400.
- 10912 em `VirtualControllerInputState..cctor` 0x9fd2f4: tamanho de um blob de
  pares inicializado por `InitializeArray`, não é indexado por tipo.
- 5464 em `System.Enum.ToUInt64`: coincidência, sem relação.
- Nenhuma forma derivada por *strength reduction* (N+8, N+32, 2N, 4N…) apareceu
  para itens: os laços guardam 5456 num registrador e comparam o índice.

---

## 3. Ocorrências classificadas — NPCs (688 e derivados)

Diferente dos itens, 688 cabe no imediato de 12 bits, então os limites estão
em `cmp wN,#0x2af` (687, `b.hi`/`b.gt`) e há formas derivadas criadas pelo
compilador em laços (`cmp xN,#0x2b8` = 696 e `cmp xN,#0x2d0` = 720).
Das 280 ocorrências brutas, 101 são endereço/pilha e o resto se distribui assim:

### 3.1 ALLOC (14)

| RVA | Método | Instrução | Tabela | Quando |
|---|---|---|---|---|
| 0x7a2e04 | TextureAssets..cctor | `mov w1,#0x2b0` | `TextureAssets.Npc` | antes |
| 0xd73bc0 | Main..cctor | `mov w1,#0x2b0` | `Main.townNPCCanSpawn` | antes |
| 0xd742a4 | Main..cctor | `mov w1,#0x2b0` | `Main.slimeRainNPC` | antes |
| 0xd76ab0 | Main..cctor | `mov w1,#0x2b0` | `Main.npcCatchable` | antes |
| 0xd76ccc | Main..cctor | `mov w1,#0x2b0` + `RuntimeHelpers.InitializeArray` | `Main.npcFrameCount` (copiado de um blob de 688 ints) | antes |
| 0xf5e6f8 | NPC..cctor | `mov w1,#0x2b0` | `NPC.killCount` | antes |
| 0xf5e79c | NPC..cctor | `mov w1,#0x2b0` | `NPC.npcsFoundForCheckActive` | antes |
| 0xf5e890 | NPC..cctor | `mov w1,#0x2b0` | `NPC.ShimmeredTownNPCs` | antes |
| 0x120649c | Lang..cctor | `mov w1,#0x2b0` | `Lang._npcNameCache` | antes |
| 0x920b68 | EmoteBubble..cctor | `mov w1,#0x2b0` | `EmoteBubble.CountNPCs` | antes |
| 0x1042d88 | Player..ctor | `mov w1,#0x2b0` | `Player.npcTypeNoAggro` (**instância**, um por jogador) | antes e depois |
| 0x7a6c50 | TownRoomManager..ctor | `mov w1,#0x2b0` | `_hasRoom` (instância) | depois (mundo) |
| 0x79d4e4 | ShopHelper.ProcessMood | `mov w1,#0x2b0` | array local por chamada | depois |
| 0x12fefd4 | WorldGen.CheckAchievement_RealEstateAndTownSlimes | `mov w1,#0x2b0` | array local por chamada | depois |

### 3.2 FACTORY (1)

| RVA | Método | Arrays | Quando |
|---|---|---|---|
| 0x155d468 | NPCID.Sets..cctor | 55 (37 bool, 16 int, `CustomSet<Color>`, `CustomSet<float>`) | antes |

### 3.3 BOUND (19)

| RVA | Método | Instrução | Efeito | Quando |
|---|---|---|---|---|
| 0xdb45ac | NPC.SetDefaults | `cmp w8,#0x2af; b.hi` | tipo ≥ 688 não monta o NPC | antes e depois |
| 0xd822f0 | MessageBuffer.ProcessData | `cmp w8,#0x2af; b.hi` | **rede**: rejeita NPC com tipo ≥ 688 | depois |
| 0xd8e3c4 | MessageBuffer.ProcessData | `cmp w8,#0x2af; b.hi` | **rede**: idem (outro pacote) | depois |
| 0xd9ffa8 | NetMessage.SendData | `cmp w8,#0x2af; b.hi` | **rede**: não envia | depois |
| 0xda0e24 | NetMessage.SendData | `cmp w20,#0x2af; b.hi` | **rede**: idem | depois |
| 0x14fbadc | WorldFile.LoadHeader | `cmp x27,#0x2af; b.ls` | **save**: kill counts além de 687 são lidos e descartados | depois |
| 0xac2b18 | WorldFile_OldConsole.LoadHeader | `cmp x27,#0x2af` | idem (conversor de mundo antigo) | depois |
| 0xad0840 | WorldFile_OldSwitch.LoadHeader | `cmp x27,#0x2af` | idem | depois |
| 0x11f5340 | Lang.GetNPCName | `cmp w8,#0x2ae; b.hi` (`tipo−1 > 686`) | nome vazio | depois |
| 0xbab460 | Item.NPCtoBanner | `cmp w8,#0x2ae; b.hi` | estandarte | depois |
| 0xbb136c | Item.GetPickedUpByMonsters_Special | `cmp w8,#0x2af; b.hi` | — | depois |
| 0xcd2148 | Main.DrawNPCs | `cmp w9,#0x2af; b.gt` | desenho | depois |
| 0xe42c28 | NPC.AI_007_TownEntities | `cmp w8,#0x2af; b.hi` | IA de NPC da cidade | depois |
| 0xeda6fc | NPC.UpdateFoundActiveNPCs | `cmp w8,#0x2af; b.hi` | bestiário/achados | depois |
| 0xee12ec | NPC.TransformCopperSlime | `cmp w8,#0x2af; b.hi` | — | depois |
| 0xf38a04 | NPC.NPCLoot | `cmp w8,#0x2af; b.gt` | loot/kill count | depois |
| 0xf42490 | NPC.ReleaseNPC | `cmp w23,#0x2af; b.hi` | soltar criatura capturada | depois |
| 0xf515c4 | NPC.SubAI_HandleTemporaryCatchableNPCPlayerInvulnerability | `cmp w8,#0x2af; b.hi` | — | depois |
| 0x12ff094 | WorldGen.CheckAchievement_RealEstateAndTownSlimes | `cmp w8,#0x2af; b.gt` | conquista | depois |

### 3.4 LOOP (12, 8 deles com forma derivada)

| RVA | Método | Instrução | Quando |
|---|---|---|---|
| 0x152b494 | ContentSamples.Initialize | `cmp w19,#0x2b0` | antes |
| 0x152cb04 | ContentSamples.FillNpcRarities | `cmp w20,#0x2b0` | antes |
| 0xc891ac | Main.Initialize_TileAndNPCData1 | `cmp x21,#0x2d0` (**720**) | antes |
| 0x846ab4 | BestiaryDatabase.Merge | `cmp w21,#0x2b0` | antes |
| 0x7c94a8 | LootSimulator.Run | `cmp w22,#0x2b0` | depois |
| 0x919168, 0x91938c | EmoteBubble.ProbeTownNPCs | `cmp x26/x24,#0x2b8` (**696**) | depois |
| 0xd6fe60 | Main.UpdateTime_SpawnTownNPCs | `cmp x19,#0x2d0` (**720**) | depois |
| 0xf3a4fc | NPC.ResetKillCount | `cmp x20,#0x2b8` (**696**) | depois |
| 0x12fc680 | WorldGen.IsThereASpawnablePrioritizedTownNPC | `cmp x22,#0x2d0` (**720**) | depois |
| 0x12fca78 | WorldGen.IsThereASpawnablePrioritizedTownNPC_Old | `cmp x25,#0x2d0` (**720**) | depois |
| 0x14f7314 | WorldFile.SaveWorldHeader | `cmp x20,#0x2b8` (**696**) | depois |

Mais um uso de formato: `WorldFile.SaveWorldHeader` 0x14f72b0 grava
`(short)688` como quantidade de kill counts (ver seção 5).

### 3.5 Não são contagem de NPC

- **Índice constante em array de tiles** (`TileID.Count = 693`): verificações
  `cmp wN,#0x2ad/#0x2ae/#0x2af/#0x2b1; b.ls` em `Main.Initialize_TileAndNPCData1`
  (12) e `…Data2` (12), `MapHelper.Initialize` (5), `Main.DrawNPCDirect` (10),
  `TileDrawing.DrawTiles`, `Main.DrawProjDirect`, `Projectile.Colliding`,
  `Projectile.GetAlpha`, `Projectile.HandleMovement`. São a checagem de limite
  que o IL2CPP põe antes de `array[685]` etc.; não dependem de `NPCID.Count`.
- **ID específico** (39 `b.eq/b.ne`, 9 faixas de `switch`): 685–687 como NPC
  (`NPC.UpdateNPC` 0xf4b4dc, `NPC.GetAlpha` 0xf577a0, `NPC.HittableForOnHitRewards`,
  `GUIQuickActionButton.CanInteract`, `NPC.SpawnNPC`/`AnyNPCs`,
  `BestiaryDatabase.FindEntryByNPCID`, dicionários de `NPCID.Sets`), e 685–689
  como ID de item, gore ou projétil (`Recipe.SetupRecipes`, `Item.SetDefaults4/5`,
  `NPC.HitEffect` → `Gore.NewGore`, `NPC.AI_110_Betsy` → `NewProjectile`,
  `ItemConversion_OldMobile`). `NPCID.Sets..cctor` 0x156c6f8 grava 687 num array (ID).

---

## 4. Tabelas indexadas pelo tipo

### 4.1 Onde as referências ficam

Para as 165 tabelas estáticas rastreadas (as ALLOC estáticas acima, os
arrays de `ItemID.Sets`, `NPCID.Sets` e `PrefixLegacy.ItemSets` e os
dicionários do ContentSamples), **nenhuma referência ao array é
gravada em outro campo estático ou de objeto**: todo uso relê o campo estático
e indexa ali mesmo. O único "escape" detectado (`NPC.FindFrame` 0xe6eb44,
`Main.npcFrameCount`) é falso positivo: o registrador foi redefinido por
`mov x8,#imm` antes do `str`. Passagens como argumento são só para
`Utils.IndexInRange`, `Enumerable.Select`, `Player.FindItem` — uso imediato,
sem guardar.

Consequência: **para as tabelas estáticas, trocar o campo pelo array maior
basta**, desde que a troca aconteça num ponto em que nenhum método esteja no
meio de um laço segurando a referência antiga num registrador (thread do jogo,
fora de update/draw).

Não basta trocar campo nas tabelas de **instância** e **locais**, que são
criadas a partir da constante a cada construção:

| Tabela | Dono | Por que trocar o campo não basta |
|---|---|---|
| `Player.npcTypeNoAggro` (688) | cada `Player` | criado em todo `new Player()` (inclusive clones e jogadores da rede) |
| `LootSimulationItemCounter._itemCountsObtained[Expert]` (5456) | instância | criado a cada simulação |
| `ItemFilters.MiscFallback._fitsFilterByItemType` (5456) | instância | criado quando o filtro da Jornada é montado |
| `TownRoomManager._hasRoom` (688) | instância | um por mundo |
| locais em `ShopHelper.ProcessMood`, `WorldGen.CheckAchievement_RealEstateAndTownSlimes` (688) | pilha | a cada chamada |
| arrays das fábricas | `SetFactory._size` | qualquer `Create*` futuro usa `_size` |

Para essas, a constante no código precisa ser corrigida (ou o construtor
interceptado).

### 4.2 Tabelas de itens

| Tabela | Tamanho | Quem aloca | Observação |
|---|---|---|---|
| `TextureAssets.Item`, `ItemFlame` | 5456 | TextureAssets..cctor | precisa de objetos `Asset<Texture2D>` para os mods |
| `Main.itemAnimations` | 5456 | Main..cctor | `DrawAnimation` ou null |
| `Item.staff`, `Item.claw` | 5456 | Item..cctor | bool |
| `ItemVariants._variants` | 5456 | ItemVariants..cctor | listas de variantes |
| `Lang._itemNameCache`, `_itemTooltipCache` | 5456 | Lang..cctor | `LocalizedText` / `ItemTooltip` |
| `ItemID.Sets.*` (77) + `Player.ItemUsesRightFire`, `Item.cachedItemSpawnsByType`, `VirtualControllerInputState.ItemCategories` | 5456 | `ItemID.Sets.Factory` | `_size` = 5456 |
| `PrefixLegacy.ItemSets.*` (6) | 5456 | própria fábrica | define quais prefixos o item aceita |
| `VirtualControllerInputState.IdLookup` (**mobile**) | 5456 | SetupLookups | nomes internos usados na config de controles |
| `VirtualControllerInputState.ItemCategories` (**mobile**) | 5456 | fábrica | categoria de cada item para os botões de toque; **é gravada na config** pelos laços `SaveCodeArrayCategories/SaveDefaultCategories` |
| `LootSimulationItemCounter` (2) | 5456 | instância | só no simulador de loot |
| `MiscFallback._fitsFilterByItemType` | 5456 | instância | filtro "diversos" da Jornada |
| `ContentSamples.ItemsByType` | dicionário | ContentSamples.Initialize | 57 leituras em 35 métodos; laço até 5456 |
| `ContentSamples.ItemPersistentIdsByNetIds` / `ItemNetIdsByPersistentIds` | dicionários | Initialize (via `ItemID.Search`) | nome persistente ↔ ID; a pesquisa da Jornada é salva por nome |
| `ContentSamples.ItemCreativeSortingId` | dicionário | SetCreativeMenuOrder | laço até 5455 |

### 4.3 Tabelas de NPCs

| Tabela | Tamanho | Quem aloca | Observação |
|---|---|---|---|
| `TextureAssets.Npc` | 688 | TextureAssets..cctor | 672 leituras em 22 métodos |
| `Main.npcFrameCount` | 688 | Main..cctor via `InitializeArray` | valores vêm de um blob fixo de 688 ints |
| `Main.townNPCCanSpawn`, `slimeRainNPC`, `npcCatchable` | 688 | Main..cctor | — |
| `NPC.killCount` | 688 | NPC..cctor | **salvo no cabeçalho do mundo** |
| `NPC.npcsFoundForCheckActive`, `ShimmeredTownNPCs` | 688 | NPC..cctor | — |
| `Lang._npcNameCache` | 688 | Lang..cctor | — |
| `EmoteBubble.CountNPCs` | 688 | EmoteBubble..cctor | — |
| `NPCID.Sets.*` (55) | 688 | `NPCID.Sets.Factory` | + dicionários próprios de `NPCID.Sets` (offsets do bestiário etc.), por chave |
| `Player.npcTypeNoAggro` | 688 | `Player..ctor` | instância |
| `TownRoomManager._hasRoom` | 688 | instância | — |
| `ContentSamples.NpcsByNetId`, `NpcBestiaryCreditIdsByNpcNetIds` | dicionários | ContentSamples.Initialize | laço de −65 até 687 |
| `BestiaryDatabase` | entradas por NPC | populador + `Merge` | `FindEntryByNPCID` devolve nada para tipo desconhecido |

Os dicionários do ContentSamples não têm tamanho: basta inserir as chaves dos
mods. O problema neles é **quem cria a amostra**: o laço chama `SetDefaults` em
cada tipo, e `SetDefaults` recusa tipos acima do limite (BOUND 0xbad174/0xbadbb4
e 0xdb45ac).

---

## 5. Codificação das constantes e correção na memória

### 5.1 Formas encontradas

| Forma | Exemplo | Faixa do imediato | Corrigível sem mudar tamanho? |
|---|---|---|---|
| MOVZ 32 bits | `mov w1,#0x1550` (bytes `01 AA 82 52`) | 0–65535 | **sim**: reescrever o campo imm16 (bits 5–20) |
| MOVZ derivado (N−1, N−2) | `mov w9,#0x154e` antes de `tipo−1 > N−2` | 0–65535 | sim, mas é preciso saber que é N−2 e não o item 5454 |
| MOVN (−5453) | `mov w9,#-0x154d` | — | não é contagem; não mexer |
| CMP imediato | `cmp w8,#0x2af` | 0–4095 | **sim** enquanto N−1 ≤ 4095 (até 4096 NPCs) |
| CMP derivado (laço) | `cmp x20,#0x2b8` (N+8), `cmp x21,#0x2d0` (N+32) | 0–4095 | sim, somando o mesmo deslocamento |

Todas as instruções relevantes têm 4 bytes e o valor novo cabe no mesmo campo:
**nenhum ponto exige trocar instrução, inserir código ou trampolim**. A escrita
é a mesma que o ShadowHook já faz para instalar hooks: `mprotect` da página para
escrita, gravar 4 bytes, `__builtin___clear_cache`, restaurar proteção.

Limites práticos dos valores novos:

- itens: imm16 permite até 65535, mas o tipo vai como `short` na rede e em tile
  entities → teto real **32767**;
- NPCs: `cmp` imm12 → teto **4095** (N−1 ≤ 4095, e N+32 ≤ 4095 nos laços
  derivados → N ≤ 4063). Folga enorme para mods.

### 5.2 Corrigir o código ou realocar?

Há dois caminhos, e o certo é combinar os dois:

1. **Corrigir a constante antes do construtor estático rodar.** Como os
   `.cctor` do IL2CPP são preguiçosos, se o MOVZ de `TextureAssets..cctor`,
   `ItemID.Sets..cctor` etc. for corrigido antes da primeira vez que a classe é
   tocada, o próprio jogo aloca as tabelas no tamanho novo, e as fábricas ficam
   com `_size` certo. Não há cópia nem troca de referência. Exige saber quantos
   itens/NPCs de mod existem nesse momento (ver 6.1) e confirmar no aparelho que
   o `.cctor` ainda não rodou (`Il2CppClass` tem o flag `cctor_finished`).
2. **Realocar depois.** Para classes cujo `.cctor` já tenha rodado, alocar o
   array maior com a API do IL2CPP (`il2cpp_array_new`), copiar e trocar o
   campo; como não há escapes (4.1), basta. Para as fábricas, corrigir também
   `_size`.

Exceção: `Main.npcFrameCount` é preenchido por `RuntimeHelpers.InitializeArray`
a partir de um blob de 688 ints. Aumentar a constante faz o runtime copiar mais
bytes do que o blob tem (ou lançar exceção, depende da versão do libil2cpp).
Essa tabela deve ser **realocada depois** do `.cctor`, nunca pela constante.

### 5.3 Um buscador por padrão aguentaria uma versão futura?

**Itens: sim, com boa confiança.**

- O valor de `ItemID.Count` pode ser lido em tempo de execução da metadata
  (valor padrão do campo `const`) — não precisa estar fixo no código.
- Todos os pontos são `mov wN,#Count` (ou Count−1/Count−2) dentro de métodos
  conhecidos por nome. Resolver o método pela API do IL2CPP
  (`il2cpp_class_get_method_from_name`), delimitar o corpo pelo próximo ponteiro
  de método, e procurar MOVZ com o valor dentro do corpo.
- Esperar uma quantidade exata por método (tabela de assinaturas: método →
  {valor relativo, quantidade, tipo de desvio seguinte}) e **recusar tudo** se
  qualquer método divergir (falha fechada → volta para casca).
- Pontos frágeis: Count−1/Count−2 colidem com IDs reais (5454, 5455 são itens);
  distinguir pelo desvio seguinte (`b.hi/b.lo/b.le` = limite; `b.eq/b.ne` = ID).
  Inlining novo pode mover um limite para outro método — a contagem esperada
  por método detecta isso, mas não diz onde ele foi parar.
- Varredura global de `mov #Count` serve como alarme: hoje são 37 MOVZ com 5456,
  todos catalogados. Um número diferente numa versão nova significa revisar.

**NPCs: confiança média.**

- 687 (`cmp #0x2af`) é ao mesmo tempo o limite e o ID de um NPC real
  (BoundTownSlimeYellow); só a condição do desvio separa os dois (`b.hi`/`b.gt`
  versus `b.eq`/`b.ne`), e há casos em que o compilador inverte a condição.
- As formas derivadas (N+8, N+32) dependem de decisões do otimizador e mudam com
  qualquer alteração no laço.
- Muitos `cmp #0x2ad/#0x2ae/#0x2af` com `b.ls` são índices de tiles; se
  `TileID.Count` ou `NPCID.Count` mudarem, a sobreposição muda também.

---

## 6. Ordem de carga

Caminho de inicialização no binário:

- `Program.LaunchGame` (0x12ADA7C) → `Lang.InitializeLegacyLocalization`
  (dispara `Lang..cctor`) → cria `Main`.
- `Main.Initialize` (0xC85468) → `ContentSamples.Initialize`.
- `Main.Initialize_AlmostEverything` → `Initialize_Items`,
  `Initialize_TileAndNPCData1/2`, carga de texturas.
- `TextureAssets`, `ItemID.Sets`, `NPCID.Sets`, `Item`, `NPC`, `Main`:
  construtores estáticos disparados no primeiro acesso, todos dentro desse
  caminho.

Hoje os mods carregam numa thread em segundo plano (`RuntimeMain` em
`android_main.cpp`) sem sincronizar com `LaunchGame`; isso é suficiente para a
casca, mas não para tabelas maiores.

### 6.1 O que muda

Para extensão de tabelas, a quantidade de itens/NPCs de mod tem de ser
conhecida **antes** de `Program.LaunchGame`. A forma natural é um hook em
`Program.LaunchGame` que bloqueia até os manifests dos mods serem lidos (sem
rodar código de mod), atribui os IDs (5456, 5457, …), corrige as constantes e só
então chama o original. O conteúdo (stats, texturas) pode continuar sendo
preenchido depois, na thread do jogo.

---

## 7. Formatos fora da memória

| Onde | Campo | Tipo | Limite / comportamento |
|---|---|---|---|
| Rede (pacotes de item: slot, chão, baú, loja) | `netID` | `short` | até 32767; `Item.DeserializeFrom` (BOUND 0xbb8a6c) vira ar acima do limite |
| Rede (NPC) | `type` | `short` | `ProcessData` (0xd822f0, 0xd8e3c4) e `SendData` (0xd9ffa8, 0xda0e24) rejeitam ≥ 688 |
| `.plr` (inventário, armadura, banco, cofres) | `netID` | `int32` | cabe; `LoadPlayer` (4 BOUND) troca ≥ 5456 por ar ao carregar |
| `.wld` baús | `netID` | `int32` | cabe; passa por `SetDefaults` (limite) |
| `.wld` NPCs da cidade / salvos | `netID` | `int32` | cabe; passa por `NPC.SetDefaults` (limite) |
| `.wld` tile entities (moldura, rack, manequim, cabideiro, prato) | tipo do item | `short` | até 32767 |
| `.wld` cabeçalho, kill counts | quantidade + `int32` cada | `short` = 688 fixo no `SaveWorldHeader`; o `LoadHeader` lê a quantidade do arquivo e **descarta** o que passar de 687 | compatível nos dois sentidos se a quantidade gravada continuar 688 |
| Pesquisa da Jornada (`.plr`) | nome persistente | string | por nome, não por ID — mods precisam de nome persistente estável |
| Config de controles (**mobile**) | categorias por item | laço até 5456 | se o laço for ampliado, a config passa a ter mais entradas; arquivo lido pelo jogo sem mods precisa ser testado |

Ponto central: **um ID numérico de mod gravado no `.plr`/`.wld` não é estável**
(muda se a lista de mods mudar) e o jogo sem a ponte transforma esses itens em
ar ou em outro item. O tModLoader resolve isso gravando mods num arquivo à parte
(`.tplr`/`.twld`). Aqui vale o mesmo: manter o arquivo lateral de persistência
que já existe e gravar o arquivo vanilla **sem** os itens/NPCs de mod
(retirá-los antes do save e devolvê-los depois), exatamente como a casca faz
hoje. Os limites de `LoadPlayer` e `LoadHeader` podem ficar como estão.

Multijogador só funciona com todos os participantes usando a ponte e a mesma
lista de mods (mesma atribuição de IDs); um cliente vanilla vê ar no lugar dos
itens e rejeita os NPCs.

---

## 8. Veredito

### Itens: **viável**

- Todas as 44 ocorrências que importam (12 ALLOC, 2 FACTORY, 24 BOUND, 6 LOOP)
  são `mov wN,#imm16` de 4 bytes, corrigíveis no lugar com a mesma técnica dos
  hooks, e cabem valores até 32767 (limite da rede).
- Nenhuma referência de tabela estática escapa do campo: trocar o campo basta
  quando a correção da constante não puder ser feita antes do `.cctor`.
- Os 24 usos de 5453–5455 são IDs específicos e ficam intocados se os mods
  começarem em 5456.
- O que dá trabalho não é o binário, é o conteúdo: preencher `ItemID.Sets`,
  `ContentSamples`, `Lang`, `TextureAssets.Item` (objetos `Asset<Texture2D>`),
  `ItemVariants`, as tabelas do mobile, e montar o item em `SetDefaults` (hook)
  para tipos ≥ 5456, que o jogo não sabe montar.

### NPCs: **viável tecnicamente, não recomendado agora**

- Os pontos também são corrigíveis em 4 bytes (`cmp` imm12, MOVZ), mas são mais
  numerosos e menos regulares: 14 ALLOC, 55 sets, 19 limites (4 na rede, 3 no
  load do mundo), 12 laços — 8 deles em forma derivada (N+8, N+32) — e 687 é
  também um ID real.
- `Main.npcFrameCount` exige realocação (blob de `InitializeArray`);
  `Player.npcTypeNoAggro` e `TownRoomManager` exigem corrigir construtores.
- Fora das tabelas, NPC depende de muito mais lógica por tipo: IA, `FindFrame`,
  desenho (`DrawNPCDirect` lê `TextureAssets.Npc` 672 vezes), bestiário
  (`BestiaryDatabase` sem entrada), estandartes (`Item.NPCtoBanner`), spawn de
  NPC da cidade, kill counts no mundo. Cada uma é uma frente de testes.
- A casca de NPC funciona bem para a maioria dos casos (IA de um aiStyle
  existente, textura/nome já trocados pelos hooks). O ganho de tirar a casca é
  menor que o risco.

Recomendação: fazer itens primeiro; reavaliar NPCs depois que a infraestrutura
(atribuição de IDs, correção de constantes, persistência lateral) estiver
provada com itens.

---

## 9. Esforço estimado

| Etapa | Itens | NPCs (depois dos itens) |
|---|---|---|
| Hook em `Program.LaunchGame` + leitura antecipada de manifests + atribuição de IDs | 2–3 dias | reaproveita |
| Tabela de assinaturas, buscador por método, correção de constantes, falha fechada | 3–4 dias | 3–4 dias (formas derivadas) |
| Realocação de tabelas já criadas, `_size` das fábricas | 1–2 dias | 2 dias (`npcFrameCount`) |
| Preencher conteúdo: `SetDefaults` próprio, Sets, Lang, ContentSamples, ItemVariants, Jornada, tabelas do mobile | 5–7 dias | 7–10 dias (Sets, bestiário, frames, estandartes, spawn) |
| Texturas como `Asset<Texture2D>` em `TextureAssets.Item/Npc` (dispensa os hooks de desenho por casca) | 2–3 dias | 2 dias |
| Save: retirar/devolver itens de mod nos arquivos vanilla, migração dos saves com casca | 2–3 dias | 3–4 dias (NPCs salvos, kill counts) |
| Rede: conferir pacotes com IDs ≥ Count, ponte nos dois lados | 2 dias | 3 dias (4 limites de NPC) |
| Testes no jogo simulado + aparelho | 4–5 dias | 5–7 dias |
| **Total** | **≈ 4–6 semanas** | **≈ +5–7 semanas** |

---

## 10. Principais riscos

1. **Ordem de carga.** Se algum `.cctor` rodar antes do hook em
   `Program.LaunchGame` (por exemplo por um MonoBehaviour do Unity), a constante
   corrigida não tem efeito e a tabela precisa ser realocada. Confirmar no
   aparelho lendo `cctor_finished` de cada classe na hora do hook.
2. **Limite esquecido.** Um ponto não catalogado (forma derivada nova,
   comparação com registrador carregado de outro jeito) deixa o item de mod como
   ar numa tela ou causa `IndexOutOfRangeException`. Mitigação: varredura global
   como alarme, testes de cada tela já mapeada no estudo das cascas.
3. **Atualização do jogo.** Mudança de `Count`, inlining diferente ou otimizador
   novo quebram a tabela de assinaturas. Mitigação: falha fechada e volta
   automática para a casca, nunca correção parcial.
4. **Saves.** Gravar IDs de mod em arquivos vanilla corrompe o progresso quando
   a lista de mods muda ou o jogo roda sem a ponte. Mitigação: manter a
   persistência lateral e retirar mods antes do save (como hoje).
5. **Multijogador.** Clientes sem a ponte ou com outra lista de mods recebem IDs
   desconhecidos; NPCs são rejeitados por validação. Mitigação: só permitir
   salas com a mesma lista (hash da lista no handshake).
6. **Config de controles do mobile.** `SaveCodeArrayCategories` grava uma
   entrada por item; ampliar o laço muda o arquivo. Mitigação: manter esses dois
   laços em 5456 e tratar as categorias de mods à parte.
7. **Escrita em código.** Mesmo mecanismo dos hooks, mas são mais páginas
   alteradas; uma correção errada (4 bytes no lugar errado) quebra o jogo de
   forma silenciosa. Mitigação: conferir os 4 bytes originais antes de gravar.
8. **Objetos gerenciados criados pela ponte** (`Asset<Texture2D>`,
   `LocalizedText`, `DrawAnimation`) precisam ser mantidos vivos pelo GC (gravados
   nas tabelas, que já são raízes estáticas).

---

## 11. Ordem recomendada de implementação (itens)

1. Hook em `Program.LaunchGame`: ler manifests, atribuir IDs a partir de 5456,
   registrar a tabela ID ↔ nome persistente do mod.
2. Tabela de assinaturas com as 44 ocorrências de itens, buscador por método,
   conferência dos bytes originais, correção das constantes e falha fechada
   (volta para casca). Deixar de fora `LoadPlayer`/`LoadTutorialPlayer` e os
   laços da config de controles.
3. Realocação para tabelas cujo `.cctor` já tiver rodado, e `_size` das três
   fábricas.
4. Hook em `Item.SetDefaults` para tipos de mod (montar o item a partir da
   definição do mod, como o tModLoader faz em `ItemLoader.SetDefaults`).
5. Preencher `ItemID.Sets`, `PrefixLegacy.ItemSets`, `Lang._itemNameCache`/
   `_itemTooltipCache`, `ItemVariants`, `ItemID.Search`, `ContentSamples`
   (amostras, nomes persistentes, ordem da Jornada), tabelas do mobile
   (`IdLookup`, `ItemCategories`).
6. Texturas: `Asset<Texture2D>` próprios em `TextureAssets.Item`; a partir daí os
   hooks de desenho por casca deixam de ser necessários para itens.
7. Save: retirar itens de mod do `.plr`/`.wld` antes de gravar e devolver
   depois, com o arquivo lateral; migrar saves que hoje usam casca.
8. Rede: verificar os pacotes de item com IDs ≥ 5456 e exigir a mesma lista de
   mods nos dois lados.
9. Testes no jogo simulado e no aparelho; manter a casca como modo de
   compatibilidade.
10. Só então reavaliar NPCs com a mesma infraestrutura.
