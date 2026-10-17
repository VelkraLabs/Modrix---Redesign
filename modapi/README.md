# TLMod Mod API

API de mods do TLMod, independente de plataforma, montada sobre a ponte IL2CPP
(`../il2cpp_bridge`). O autor do mod só vê `include/tlmod/tlmod_api.h` (C puro)
e, se quiser, `include/tlmod/tlmod.hpp` (açúcar C++ só de header). Nenhum tipo do
jogo ou do IL2CPP atravessa a fronteira: players são índices, conteúdo é chave + ID.

```
mod (C/C++) ──TLModHostApi (C ABI)──▶ runtime ──GameAdapter──▶ BridgeGameAdapter ──▶ il2cpp_bridge ──▶ jogo
                                          │                     FakeGame (testes)
                                          └─ ContentRegistry, ModLoader, manifest/semver
```

## Contrato do mod

Um mod é uma pasta:

```
example_item/
├── manifest.json
└── lib/
    ├── android-arm64/libexample_item.so
    └── linux-x86_64/libexample_item.so
```

A biblioteca exporta `int tlmod_mod_init(const TLModHostApi*, TLModContext*)`.
A macro `TLMOD_MOD(fn)` gera isso a partir de `bool fn(tlmod::Host&)`.

### manifest.json

| campo | obrigatório | descrição |
|---|---|---|
| `tlmod_api` | sim | versão da ABI (hoje `1`) |
| `id` | sim | `[a-z0-9]` separados por `.` `_` `-`, até 64 (ex.: `tlmod.example_item`) |
| `name` | sim | nome exibido |
| `version` | sim | semver `MAJOR.MINOR.PATCH[-pre]` |
| `authors`, `description` | não | metadados |
| `dependencies` | não | `{"id": "faixa"}`; faixas `*`, `1.2.3`, `^1.2`, `~1.2.3`, `>=1 <2` |
| `optional_dependencies` | não | idem; ausente é ok, presente precisa casar e carrega antes |
| `content` | não | o que o mod adiciona: `items`, `npcs`, `projectiles`, `tiles`, `walls`, `buffs` → listas de nomes `[a-z0-9_]` |
| `entry` | sim | plataforma → caminho relativo da biblioteca (`android-arm64`, `linux-x86_64`, `windows-x86_64`, `macos-arm64`…) |

`content` é um contrato. Registrar algo que não foi declarado é recusado, e algo
declarado que não foi registrado gera aviso. Assim a interface do TLMod mostra o
que um mod adiciona sem executar o código dele.

### Ciclo de carga

1. Lê `mods/*/manifest.json`. Manifests inválidos e ids duplicados são rejeitados.
2. Resolve dependências: ausentes, versões incompatíveis, ciclos e `tlmod_api` diferente
   geram rejeição, que se propaga para quem depende.
3. Ordena topologicamente, com empates resolvidos pelo id. A ordem é determinística, e por isso
   os IDs também são.
4. Carrega a biblioteca da plataforma e chama `tlmod_mod_init`. Se ela retornar algo diferente de 0, o
   conteúdo que o mod registrou é desfeito e os dependentes são pulados.

## IDs de conteúdo

Os IDs de mods ficam numa faixa fixa acima dos originais, com folga para atualizações do jogo:

| tipo | originais (dump) | 1º ID de mod | máximo | limite |
|---|---|---|---|---|
| item | 5456 | 8192 | 32767 | short na rede |
| npc | −65..687 | 4096 | 32767 | short |
| projectile | 1022 | 4096 | 32767 | short |
| tile | 693 | 4096 | 65535 | ushort |
| wall | só em runtime | 4096 | 65535 | ushort |
| buff | 355 | 4096 | 65535 | ushort na rede |

Se o jogo informar mais IDs originais do que o 1º ID de mod, aquele tipo é desativado para
mods: o runtime prefere desativar a colidir. A chave `mod_id:nome` é estável. Use a chave
para salvar e sincronizar; o ID numérico pode mudar quando o conjunto de mods muda.

## NPCs e bosses de mod

Um NPC de mod roda dentro de uma **casca**: um NPC original (`shell_type`) que fornece
textura, hitbox, sons e, se o mod quiser, a própria AI. O jogo indexa arrays por tipo
(texturas, `npcFrameCount`, sons), então um ID ≥ 688 nunca pode chegar a ele.

| gancho | RVA | o que o runtime faz |
|---|---|---|
| `NPC.NewNPC` (pré) | `0xEE4630` | ID de mod (4096+) vira o `shell_type` |
| `NPC.NewNPC` (pós) | `0xEE4630` | registra "slot i = NPC de mod X", aplica vida/dano/defesa e chama `on_spawn` |
| `NPC.SetDefaults` (pré) | `0xDAF1BC` | desmarca o slot, que mudou de dono (morte e reuso, `Transform`, rede) |
| `NPC.AI` (pré) | `0xDCE54C` | a cada tick, se o slot é de mod, chama `on_ai`; `true` pula a AI da casca |

`NPC.AI` só é chamado por `NPC.UpdateNPC` (`0xF4A2E8`), que `Main.DoUpdateInWorld`
(`0xCBBDF0`) chama para os 200 slots a cada tick. Movimento e colisão rodam em
`UpdateNPC` depois da AI, então `on_ai` só precisa ajustar velocidade e estado.

Estado por NPC: `ai[0..3]` são do jogo, e a AI da casca usa esses campos quando roda. `local[8]` é
privado do mod, fica guardado no runtime e o jogo não toca. Veja `mods/example_boss`.

O nome exibido (`display_name`) aparece no hover, na barra de vida, no mapa e nos anúncios ("X
despertou!"), por hooks em `NPC.TypeName` e `NPC.GetTypeNetName` (ver [Texturas de mod](#texturas-de-mod)).
O bestiário não tem entrada para NPC de mod: o abate conta para a casca.

**Na API.** `spawn_npc(ctx, tipo, x, y)` cria o NPC na hora, mas só na thread do jogo (dentro de
`on_ai` ou de um evento). `spawn_npc_near_player(ctx, tipo, dx, dy)` pode ser chamado de qualquer
thread, inclusive no init: o pedido espera numa fila e o runtime cria o NPC depois de 2 s seguidos
com um mundo carregado e o jogador vivo, em (centro do jogador + `dx`, `dy`), com `dy < 0` acima.

**Para testar no aparelho:** crie um arquivo vazio `spawn_on_start` na pasta do mod do boss
(`.../tlmod/mods/example_boss/spawn_on_start`). Ao entrar num mundo, 2 s depois, o Titã de Brasa
aparece 30 blocos à direita e 15 acima do jogador, com a folha `textures/ember_titan.png`, o nome
do mod e 12000 de vida (uma vez por abertura do jogo).

Ainda falta: barra e música de boss (`boss`), sincronizar a tabela de slots e o `local[]` em
multiplayer, e confirmar no jogo que `NewNPC` aceita `source` nulo. Textura própria:
ver [Texturas de mod](#texturas-de-mod).

## Itens de mod no jogo (casca)

Um item de mod roda numa **casca**: um item original que o jogo monta e desenha. Mesma ideia dos
NPCs.

```
Item.SetDefaults(8192)        hook pré: 8192 é o Rubi Fictício -> monta a casca 38 (Lente)
  └ SetDefaults original(38)  todas as tabelas por tipo só veem 38
hook pós: netID = 8192        a identidade do item; dano, uso, pilha, valor, raridade, nome e tooltip da definição
Item.RebuildTooltip           netID de mod: o tooltip vem da definição (Lang.GetTooltip(8192) estouraria)
```

| campo do `Item` | item original | item de mod |
|---|---|---|
| `type` | o ID | a casca (`shell_type`, padrão 38): é o índice de **todas** as tabelas |
| `netID` | o ID | o ID de mod (8192+): `Clone` copia, `IsTheSameAs` compara, `netDefaults`/`Refresh` voltam ao hook |
| `_nameOverride`, `ToolTip` | nulo / `Lang` | `display_name` e `tooltip` da definição (`Item.Name` usa `_nameOverride`) |

**Por que casca e não aumentar as tabelas** (o que o tModLoader faz no PC, reescrevendo o IL do jogo):

- **Tabelas por tipo no dump.**
  - 79 arrays em `ItemID.Sets`;
  - `TextureAssets.Item` e `ItemFlame`;
  - `Lang._itemNameCache` e `_itemTooltipCache`;
  - `Item.headType/bodyType/legType/staff/claw/cachedItemSpawnsByType` e `Main.itemAnimations`;
  - os dicionários de `ContentSamples` e as tabelas do mobile (`VirtualControllerInputState`, tags de
    item do chat).

  Todas são criadas com o tamanho `ItemID.Count`.
- **A constante está embutida no código ARM64.** O IL2CPP compila `ItemID.Count` como imediato: 37
  ocorrências de `0x1550` em 31 métodos. Algumas são as alocações (`.cctor` de `ItemID.Sets`,
  `TextureAssets`, `Lang`, `Item`). Outras são guardas: `Item.SetDefaults` troca `type >= 5456` por
  ar, e também `DeserializeFrom`, `ItemSlot.TryItemSwap`, `Player.UpdateEquips`, `RefreshInfoAccs`,
  `ItemTagHandler`. Aumentar os arrays não basta: seria preciso patchear essas instruções no binário,
  e de novo a cada versão do jogo.
- **Com a casca, `type` nunca sai da faixa original**, então nenhuma tabela precisa mudar. O jogo já
  separa `type` de `netID` nas variantes antigas (netID negativo), e os leitores de `netID` são
  poucos: comparações, `netDefaults`, `Serialize` e `RebuildTooltip` (este com hook).

**Na API.** `TLModItemDef.shell_type` escolhe a casca (0 = Lente), que precisa ser um item original.
Em `damage > 0` numa casca que não se usa, o item vira arma de golpe corpo a corpo.
`give_item(ctx, tipo, quantidade)` dá um item ao jogador local: pode ser chamado de qualquer thread,
inclusive no init, e a entrega acontece no próximo frame com um mundo carregado, empilhando com o que
ele já tem. O que não couber no inventário cai no chão, sobre o jogador.

**Para testar no aparelho:** crie um arquivo vazio `give_on_start` na pasta do mod de exemplo
(`.../tlmod/mods/example_item/give_on_start`). Ao entrar num mundo vêm 3 Rubis Fictícios, com a
imagem do mod (`textures/fictional_ruby.png`) e nome, tooltip, valor (50 de prata), raridade e pilha
(999) do mod.

O que ainda não está aqui:
- receitas e loja;
- sincronizar a tabela de IDs em multiplayer;
- telas que leem o nome direto de `Lang` por `type` em vez de `Item.Name` mostrariam o nome da casca.

### Escolhendo a casca

O item de mod herda da casca tudo que a definição não sobrescreve. A definição sobrescreve só
largura, altura, pilha máxima, valor, raridade, dano e tempo de uso (e `useStyle`/`melee` quando
`damage > 0` numa casca que não se usa). O resto vem de dois lugares:

**1. `ItemID.Sets` (79 arrays estáticos indexados pelo type, no `.cctor` de `ItemID.Sets`).**

Mudam o comportamento, e o `register_item` avisa:

| conjunto | efeito no item de mod |
|---|---|
| `ItemsThatShouldNotBeInInventory` | some do inventário |
| `IsAPickup`, `NebulaPickup` | é coletado ao encostar (coração, estrela, buff) e nunca entra no inventário |
| `CommonCoin` | vira dinheiro |
| `BossBag`, `OpenableBag`, `IsFishingCrate` | botão direito abre e solta o loot da casca |
| `ShimmerTransformToItem` | no Shimmer vira outro item |
| `ExtractinatorMode` | o Extrator aceita e troca por loot |
| `CanGetPrefixes` | `false` = sem prefixo e sem reforja |
| `ToolTipDamageMultiplier` | o tooltip mostra o dano multiplicado |
| `Deprecated` | item obsoleto |
| `IsAKite`, `Yoyo`, `IsDrill`, `IsChainsaw`, `StaffMinionSlotsRequired` | uso especial (pipa, ioiô, broca, lacaio) |

Mudam o comportamento só em situações específicas (sem aviso):
- `IsAMaterial`: linha "Material" e pesquisa.
- `IsFood`, `FoodParticleColors`, `DrinkParticleColors`: partículas ao comer.
- `KillsToBanner`, `BannerStrength`: estandartes.
- `CanBePlacedOnWeaponRacks`, `WorksInVoidBag`.
- `ItemsThatCountAsBombsForDemolitionistToSpawn`.
- `CanFishInLava`, `IsLavaBait`, `GrassSeeds`, `Torches`, `WaterTorches`, `Workbenches`,
  `AlsoABuildingItem`.
- `ExoticPlantsForDyeTrade`: o Comerciante de Tintas aceita.
- `ColorfulDyeValues`, `flowerPacketInfo`, `IsCrafted*`, `ShimmerCountsAsItem`, `ItemsForStuffCannon`.
- `IgnoresEncumberingStone`, `IsLavaImmuneRegardlessOfRarity`, `ItemNoGravity`,
  `ItemSpawnDecaySpeed`, `OverflowProtectionTimeOffset`.
- `ItemsThatAllowRepeatedRightClick`, `HasAProjectileThatHasAUsabilityCheck`, `ForceConsumption`,
  `ForcesBreaksSleeping`, `SkipsInitialUseSound`.
- `UsesBetterMeleeItemLocation`, `BonusMeleeSpeedMultiplier`,
  `SummonerWeaponThatScalesWithAttackSpeed`, `gunProj`, `IsPaintScraper`, `TrapSigned`,
  `UsesCursedByPlanteraTooltip`.

Só aparência, ordem ou controle:
- `Sorting*`, `Gamepad*`, `LockOn*`;
- `SingleUseInGamepad`, `CanBeQuickusedOnGamepad`;
- `NeverAppearsAsNewInInventory`, `ItemIconPulse`, `AnimatesAsSoul`, `DrawUnsafeIndicator`,
  `TextureCopyLoad`.

**2. Os campos que o `SetDefaults` da casca preenche.** "É munição" e "é um arco" não são
`ItemID.Sets` nesta build; são campos do `Item`:

| campo | efeito no item de mod |
|---|---|
| `ammo` | é munição: armas da casca consomem o item |
| `useAmmo` + `shoot` | consome munição e dispara (arco: `useAmmo` = `AmmoID.Arrow` = 40; arma de fogo: `AmmoID.Bullet` = 97, valores do `.cctor` de `AmmoID`) |
| `accessory`, `defense`, `*Slot` | equipa como acessório ou armadura, com a defesa e o visual da casca. O efeito especial de acessório é por type em `Player.ApplyEquipFunctional` |
| `consumable`, `buffType`, `healLife`/`healMana`, `potion` | usar gasta uma unidade e aplica o efeito da casca |
| `createTile`, `createWall` | usar coloca o bloco ou a parede da casca |
| `pick`, `axe`, `hammer` | também é ferramenta |
| `mana`, `magic`, `summon`, `sentry`, `channel` | gasta mana, invoca, canaliza |
| `makeNPC`, `mountType`, `bait`, `fishingPole` | solta criatura, invoca montaria, isca, vara de pesca |

**Casca recomendada por categoria** (`RecommendedShells()` em `item_shell.h`):

| categoria | casca | por quê |
|---|---|---|
| material | 38 Lente (padrão) | sem uso, sem tile, sem tiro; já tem a linha "Material" |
| arma corpo a corpo | 24 Espada de Madeira | golpe simples, sem projétil, aceita prefixos |
| arco | 39 Arco de Madeira | consome flechas e dispara a flecha usada |
| arma de fogo | 95 Pistola de Pederneira | consome balas e dispara a bala usada |
| munição | 40 Flecha de Madeira (flecha), 97 Bala de Mosquete (bala) | `ammo` da categoria, consumível, pilha grande |
| acessório | 216 Algema | só `defense = 1`. Em `ApplyEquipFunctional`/`UpdateEquips` não há comparação com 216 nem tabela de salto que a cubra |

**Aviso no `register_item`.** O runtime lê as propriedades da casca no próprio jogo:
- `ItemID.Sets` pelo nome do campo;
- os campos de um `Item` de sonda montado com o `SetDefaults` do jogo (`TLMod_ReadItemTraits`).

Ele deduz a categoria pelo dano da definição e pelo que a casca é, e avisa no log cada propriedade
que provavelmente não combina, com a casca recomendada. Exemplo:

```
[mod.x] register_item: "mod.x:gema" na casca 58: é coletada ao encostar, como coração/estrela, e nunca entra no inventário (ItemID.Sets.IsAPickup)
[mod.x] register_item: "mod.x:gema" na casca 58: para material, a casca recomendada é 38 (Lente): ...
```

As propriedades só podem ser lidas na thread do jogo, e os mods carregam numa thread própria antes
do primeiro frame. Por isso, no aparelho os avisos saem no primeiro frame (`Main.DoUpdate`), com o
mesmo texto. Como a avaliação usa os valores reais do jogo, ela também confere as recomendações
acima no aparelho: uma casca recomendada que avise alguma coisa indica uma diferença da build.

## Texturas de mod

Item e NPC podem trazer um PNG da pasta do mod: `TLModItemDef.texture` e `TLModNpcDef.texture`
(caminho relativo, ex.: `"textures/fictional_ruby.png"`). Para NPCs, `frame_count` quadros empilhados
na vertical, todos da mesma altura, e `frame_ticks` ticks por quadro na animação automática.
`TLModNpcState.frame` fixa um quadro em `on_ai`; `-1` volta ao automático. Sem PNG, ou se o PNG
falhar, fica a textura da casca e o erro vai para o log.

### Onde as texturas ficam nesta build (confirmado no dump, não no PC)

- **Tabelas.** `Terraria.GameContent.TextureAssets.Item` e `.Npc` (estáticos `0x560` e `0x570`) são
  `Asset<Texture2D>[]` do tamanho `ItemID.Count` / `NPCID.Count`. O `Asset` (ReLogic) guarda a textura
  em `Value` (`+0x28`) e o estado em `State` (`+0x18`, 2 = carregado).
- **O desenho lê o campo direto**, sem `get_Value` nem `Main.LoadItem`: `GUIInventory.DrawItem` faz
  `ldr [static+0x560]` → `ldr [asset+0x28]`. Por isso a troca é do `Asset` inteiro, na posição da casca.
- **`Texture2D` é o do XNA da versão mobile.** `Microsoft.Xna.Framework.Graphics.Texture2D` é um
  invólucro de `UnityEngine.Texture2D` (`_unityTexture`, `+0x78`), com suporte a atlas (`PackedEntry`,
  `_textureAtlas`). O `.ctor(UnityEngine.Texture2D)` (`0xB38544`) copia largura e altura e marca
  `_textureLoaded`, sem atlas.
- **`ImageConversion.LoadImage` não existe nesta build**: o stripping do Unity removeu
  `ImageConversion`, `LoadRawTextureData` e `SetPixels32`. O runtime decodifica o PNG em C++ (zlib) e a
  ponte escreve os pixels por `Texture2D.GetWritableImageData` (privado) + `Apply`. O Unity guarda
  as linhas de baixo para cima, então a ponte inverte. Filtro `Point` (pixel art) e `Clamp`.

### Fluxo

```
LoadAll (thread do loader)   lê e decodifica cada PNG; valida altura % frame_count
Main.DoUpdate (hook, thread principal do Unity)
  └ fila: até 4 texturas por frame -> UnityEngine.Texture2D -> XNA Texture2D -> Asset<Texture2D>
    (-1 enquanto TextureAssets não carregou: tenta no próximo frame; 0 = falhou, fica a casca)
desenho de um item/NPC de mod (hooks abaixo)
  └ TextureAssets.Item[casca] (ou .Npc[casca]) = Asset do mod; desenho original; devolve o da casca
```

Item de mod = `netID` na faixa de mods; NPC de mod = slot de `Main.npc` marcado pelo runtime. A
textura é escolhida por item e por slot, então um item ou NPC original da mesma casca continua com a
textura dele. Para NPCs, durante o desenho, a ponte também troca `Main.npcFrameCount[casca]` e
`NPC.frame` (o retângulo do quadro na folha do mod) e os devolve depois.

A troca vale para `TextureAssets.Item[casca]` e também tira `Main.itemAnimations[casca]` durante o
desenho, para uma casca animada não fatiar o PNG do mod em quadros.

### Telas (todas as leituras de `TextureAssets.Item` no binário)

A lista sai das 112 leituras de `TextureAssets.Item` (estático `+0x560`) em 49 métodos, mais as
leituras de nome (`Lang.GetItemName*`, `NPC.TypeName`). Para cada tela, o hook acha o item do mesmo
jeito que o jogo, troca a textura da casca só durante a chamada e devolve no fim.

| hook | RVA | tela | de onde vem o item |
|---|---|---|---|
| `GUIInventory.DrawItem` | `0x95E6E4` | inventário, baús, bancos | `inv[slot]` |
| `GUIHotbar.DrawHotbarItem` | `0x83BD64` | barra rápida | `inv[slot]` |
| `Main.DrawItem` | `0xCFA928` | item no chão | parâmetro |
| `ItemSlot.DrawItemIcon` | `0x1632310` | ícone de `ItemSlot.Draw`: itens da loja, receitas e ingredientes da guia, resultado das receitas, duplicação da Jornada, molduras | parâmetro |
| `PlayerDrawLayers.DrawPlayer_27_HeldItem` | `0x15C0EDC` | item na mão / em uso | `drawinfo.heldItem` |
| `Main.DrawNPCDirect` | `0xBFC154` | NPCs do mundo | o NPC |
| `GUITransactionButton.Draw(Item)` | `0x12220F8` | botão de compra da loja, botão da pesquisa e outros botões com item | parâmetro |
| `GUIReforgePopup.DrawItem` | `0x9D2984` | reforja | `GUIInstance.Active.GUIReforge.get_ReforgeItem()` |
| `GUIResearch.DrawResearchItem` | `0x9D7164` | pesquisa da Jornada | `GetResearchItem()` |
| `GUICraftGuidePopup.DrawItem` | `0x15A5384` | guia de receitas | receita em foco (`createItem`) ou o item da guia |
| `GUICrafting.MaterialItemDraw` | `0x73AD78` | ingredientes da receita em foco | `Main.recipe[availableRecipe[focusRecipe]].requiredItem[i]` |
| `GUICrafting.MaterialItemTooltipDraw` | `0x73A3AC` | ingredientes no tooltip de uma receita | `CraftHover.requiredItem[i]` |
| `GUIVirtualInputController.DrawControls` | `0x9EF6D0` | controles de toque: botão de uso e botão do gancho | `inventory[selectedItem]`, `QuickGrapple_GetItemToUse()` |
| `GUIQuickActionButton.Draw(QuickActionControl)` | `0x10604EC` | botão rápido do gancho (`Grapple`, `GrappleFire`) | `QuickGrapple_GetItemToUse()` |
| `Main.DrawInterface_40_InteractItemIcon` | `0xD0F8B8` | ícone junto do cursor (item e munição) | `inventory[selectedItem]`, `Main.TryGetAmmo(item)` |
| `ItemTagHandler.ItemSnippet.UniqueDraw` | `0x97F0D8` | `[i:ID]` no chat (mede pela textura) | `_item` |

Quando um desenho mostra dois itens (item na mão e munição, item e gancho) e os dois têm o mesmo
`type`, só o primeiro troca: a textura é uma por `type`.

**Pesquisa da Jornada (`GUIResearch.CanBeResearched`, `0x9D4C10`).** A pesquisa só olha o `type`.
Um item de mod seria consumido e desbloquearia a casca para duplicação. O hook responde "não pode
ser pesquisado".
O tooltip também perguntava pelo `type`: na Jornada mostrava "Pesquise mais 25 para liberar a
Duplicação", o limite da casca. Enquanto `Main.MouseText_DrawItemTooltip_GetLinesInfo` (`0xCC90D8`)
monta o tooltip de um item de mod, `CreativeItemSacrificesCatalog.TryGetSacrificeCountCapToUnlockInfiniteItems`
(`0x8F4290`) responde "sem limite" e a linha não aparece. Fora do tooltip o catálogo segue igual.

**Nomes de item.** As telas usam `Item.Name`, `AffixName` ou `HoverName`, que leem o
`_nameOverride` do item de mod. Isso vale para hover, tooltip, loja, reforja, título da pesquisa,
título da guia e tag do chat. Fora de `Item.Name`, há 13 chamadas a `Lang.GetItemName(Value)` no
binário. A maioria usa ids constantes ou derivados do tile:
- nome do Baú (48) e da Forja do Defensor (3813), na interface do baú e ao renomear;
- placas e lápides;
- pilones no mapa;
- o item consumido por uma varinha (`tileWand`), no tooltip.

Duas mostram o nome da casca:
- **Rótulos "Qualquer X" das receitas.** Saem da casca quando ela é 9, 22, 169, 3458 ou 542 e a
  receita tem o grupo correspondente. Evite essas cascas.
- **Mensagem de morte no PvP** (`Lang.CreateDeathMessage`). "Morto por X com <item>" usa o
  `plrItemType` do assassino; só existe no multiplayer.

**Nome de NPC.** `NPC.TypeName` (`0xED9240`) é `Lang.GetNPCNameValue(netID)`, e `NPC.GetTypeNetName`
(`0xEDB0BC`) é `NetworkText.FromKey(...)`. Os dois têm hook:
- o primeiro devolve o nome do mod, usado no hover sobre o NPC, na barra de vida, no mapa e em
  `FullName`;
- o segundo devolve `NetworkText.FromLiteral(nome)`, usado em "X despertou!", "X foi derrotado" e nas
  mensagens de morte.

**Não cobertas (mostram a casca):**
- **Expositor de armas e prato de comida** (`TileDrawing.DrawSpecialTilesLegacy`, `0x8CE4A0`). É um
  laço sobre todos os tiles especiais; uma troca por chamada pegaria todos os itens da mesma casca.
  Esses tile entities também não passam pelo arquivo paralelo do save. As molduras usam
  `DrawItemIcon` e estão cobertas.
- **Botão de interagir** (`GUIQuickActionButton.CanInteract`): ícones vindos do tile (constantes) e
  o item de uma moldura.
- **`[i:ID]` em texto inline** (`ItemTagHandler.PrintInline`): mede pela casca, mas o ícone sai por
  `ItemSlot.Draw` e fica certo.
- **Medidas pela casca:** `Item.GetDrawHitbox`, `Main.GetItemDrawFrame` e `Player.GetItemDrawFrame`
  medem a textura da casca fora do desenho, então escolha uma casca de tamanho parecido com o PNG.

**Item de mod não chega lá (sem hook):**
- duplicação da Jornada (`UIDynamicItemCollection`: lista de IDs de `ContentSamples.ItemsByType`);
- `GUIShop.DrawPurchaseButton`, que lê o índice constante 3817 (moeda Medalha do Defensor); o item
  vendido vai pelo `GUITransactionButton`;
- placas e lápides (`GUINPCDialogue.DrawSign`, type vindo do tile);
- ícones fixos de controles e fios (`GUIQuickActionButton.GetIconFrame`);
- moedas (`ItemSlot.DrawMoney`/`DrawSavings`, `CustomCurrency*`);
- projéteis que usam textura de item (`DrawProjDirect`, por type de projétil);
- tela de carregamento e créditos.

### Bestiário e créditos: um NPC de mod não aparece neles

- **Bestiário.** As entradas são por `netID` (NPCs originais). O ícone (`UnlockableNPCEntryIcon`)
  cria um NPC próprio com `SetDefaults(npcNetId)`. O abate é registrado por
  `NPCKillsTracker.RegisterKill` → `NPC.GetBestiaryCreditId` =
  `ContentSamples.NpcBestiaryCreditIdsByNpcNetIds[npc.netID]`, e o `netID` de um NPC de mod é o da
  casca. Matar o boss de mod conta como matar a casca e desbloqueia a entrada dela. O mod não tem
  entrada própria.
- **Créditos.** `CreditsRollComposer` monta as cenas com NPCIDs fixos, e `NPCSegment.Draw` desenha
  esses NPCs pela sobrecarga de `DrawNPCDirect` de 4 parâmetros (`0xBCB734`).

Nenhum dos dois precisa de hook.

### Mods de exemplo

- `mods/example_item/textures/fictional_ruby.png`: 32x32, o Rubi Fictício.
- `mods/example_boss/textures/ember_titan.png`: 64x256, 4 quadros de 64x64; `frameTicks = 10`. Na
  introdução o mod fixa o quadro 0 (`npc.frame = 0`); depois a animação é automática.

Os dois são gerados por `tools/make_example_textures.py`.

## Oficina de Mods

Como a tela de mods do tModLoader: o botão **Mods** no menu principal do jogo abre a Oficina, uma tela
do próprio jogo montada com as peças da seleção de mundos (`il2cpp_bridge/src/workshop_screen.cpp`):

- **Lista.** Fundo, divisória, título "Oficina de Mods", a lista que rola com o dedo (o
  `GUIDraggableItemGrid` do jogo, com arraste e momento) e Voltar. Cada linha usa o fundo de linha de
  mundo e os textos de nome, versão, estado (colorido: ativo, desativado, erro, mudança pendente) e o
  que o mod adiciona. Em ordem: ativos, desativados e com erro, cada grupo por nome.
- **Detalhes.** Tocar numa linha abre o texto completo do mod (versão, autores, descrição, conteúdo,
  estado e, se falhou, o erro inteiro), com Voltar e o botão "Novo" da tela de mundos virado em
  **Ativar/Desativar**. O voltar do Android (Escape) volta uma página.
- Ligar ou desligar vale na próxima abertura do jogo: uma `.so` carregada não é descarregada.

A lista do jogo só aceita delegates gerenciados. Os três vêm de métodos que já existem:
`GUIWorldSelectMenu.WorldScale` (devolve 1), `GUIWorldSelectMenu.WorldOver` (grava a linha sob o dedo
numa instância própria) e `GUIItemGrid.DummyDraw` (vazio, sem chamadas diretas), que ganha um hook e
desenha a linha enquanto a Oficina desenha a lista. Cada linha marca sua região com
`ControlAnchor.SetGridItemRegion`, e os textos ancorados em `GridItem` se posicionam sozinhos, como na
seleção de mundos. Os dados vêm do runtime (`TLModWorkshopSource`, montado com `BuildWorkshopEntries`);
a configuração é thread-safe e o painel vê as mudanças pela revisão dela. Se alguma peça do jogo não
resolver, o botão Mods abre a Oficina do painel (Views Android), que também abre pela aba Mods.

**O botão** é desenhado pelo próprio jogo (`il2cpp_bridge/src/menu_hooks.cpp`): depois do
`GUIMainMenu.Draw`, o mesmo `GUITransactionButton.Draw` dos outros botões, com o layout de "Comprar o
jogo completo" (`MainMenu_Layout.PurchaseFullGame`), que só aparece na versão de teste
(`Main.TrialMode`). Essa posição fica uma linha abaixo da coluna do menu e pode não caber em telas
baixas. Por isso a ponte confere em pixels (`GUITransactionButton.GetRegion` e a região `Screen` do
`ControlAnchor`) e escolhe, em ordem: a posição do PurchaseFullGame; a coluna inteira reespaçada por
igual, com o topo no lugar, se os 7 botões couberem sem se sobrepor; ao lado do último botão da coluna.
Refaz quando o tamanho da tela muda, e o log registra a decisão e a região de cada botão. Com
Configurações aberta, o botão fica desativado, como os outros.

## Ferramentas e abas no painel

API acrescentada ao fim da `TLModHostApi` (o `tlmod.hpp` confere o `struct_size`):

| função | thread | o que faz |
|---|---|---|
| `subscribe_frame(fn)` | init | `fn` roda uma vez por frame na thread do jogo (menu e mundo) |
| `register_command(channel, fn)` | init | recebe comandos de texto da interface; responde JSON (`{"toast": ...}`) |
| `content_name(kind, id, internal)` | qualquer | nome de item ou NPC, original ou de mod: no idioma do jogo ou o interno (`Zenith`, `mod:nome`) |
| `content_ids(kind)` | qualquer | todos os IDs com nome, originais e de mod |
| `get_time` / `set_time(day, time, jump)` | jogo | `Main.dayTime`/`Main.time`; `jump` = `Main.SkipToTime`, como o poder da Jornada |
| `heal_player` | jogo | vida e mana cheias |
| `kill_hostile_npcs(radius)` | jogo | `StrikeNPCNoInteraction` (com loot) nos hostis perto do jogador |

Os nomes vêm de um catálogo que o runtime monta nos primeiros frames (256 IDs por frame, pelo
`Lang` e pelo `ItemID/NPCID.Search` do jogo); até ficar pronto, `content_name` de um ID original
devolve `-2`.

**Aba própria no painel.** Um mod que registra provedor **e** comando no canal `panel` ganha uma aba.
O provedor descreve a aba em JSON; o painel desenha e devolve os toques como comandos:

```json
{"title": "Ferramentas", "sections": [
  {"title": "Hora do dia", "note": "Agora: 12:00 (dia)",
   "rows": [{"buttons": [{"label": "Meio-dia", "command": "time noon"},
                         {"label": "Congelar hora", "command": "freeze", "active": true}]}]},
  {"title": "Dar item", "search": {"id": "item", "placeholder": "Nome ou ID", "query": "estrela"},
   "rows": [{"text": "Estrela Cadente", "detail": "ID 75 · FallenStar",
             "buttons": [{"label": "+10", "command": "give 75 10"}]}]}]}
```

O botão manda o texto de `command`; o campo de busca manda `search <id> <texto>`. Comandos chegam na
thread da UI: o que mexe no jogo vai para uma fila atendida no `subscribe_frame`. Veja
[`mods/devtools`](mods/devtools/README.md).

## Itens de mod no save

O jogo grava o ID numérico do item (`netID`) no arquivo. Um ID de mod (8192+) muda quando o
conjunto de mods muda e, gravado no arquivo, o estraga. Por isso a regra é: **nenhum ID de mod
chega ao arquivo do jogo.**

| quando | o que o runtime faz (`ItemPersistence`) |
|---|---|
| antes do save | todo item fora da faixa original sai do slot (vira ar) e é anotado: chave `mod_id:nome`, pilha, prefixo, favorito e posição |
| depois do save | os itens voltam para o lugar e a lista vai para o arquivo paralelo, `<arquivo do jogo>.tlmod.json` |
| depois do load | o arquivo paralelo é lido, cada chave vira o ID atual e o item volta para o slot |

- **Mod removido.** A chave não resolve: o item fica no arquivo paralelo como órfão
  (`"orphan": "missing_mod"`) e é regravado a cada save. Com o mod de volta, o item volta.
- **Slot ocupado.** Se o slot estiver ocupado no load (o personagem foi usado sem o mod) ou no fim
  do save (o save roda no ThreadPool e o jogo segue rodando), o item vai para um slot livre
  equivalente: o mesmo banco ou baú, depois o inventário principal (0..49). Sem lugar, fica guardado
  como órfão (`"no_space"`). Nada é sobrescrito nem descartado.
- **Save que falha.** Os itens voltam do mesmo jeito e o arquivo paralelo não muda.
- **Arquivo salvo sem ter sido carregado com o TLMod** (o jogo leu o mundo antes de os mods
  subirem): o que estava no arquivo paralelo continua lá (`"not_loaded"`).
- **Arquivo paralelo ilegível.** É movido para `.tlmod.json.invalid-<data>` e nunca é sobrescrito sem
  cópia.
- **Baús** são identificados pela posição (x, y): o `LoadChests` do jogo renumera `Main.chest`.
- **Saves na nuvem** guardam o arquivo paralelo na pasta do TLMod (`cloud_saves/player|world/`).
- **ID fora da faixa original sem dono** (nenhum mod registrou): também sai do arquivo do jogo,
  volta para a memória e não vai para o arquivo paralelo (aviso no log).

```json
{"tlmod_items": 1, "kind": "player", "items": [
  {"key": "acme.gems:ruby", "container": "inventory", "slot": 3, "stack": 5, "prefix": 7, "favorited": true},
  {"key": "acme.gems:topaz", "container": "loadout_armor", "loadout": 1, "slot": 0, "stack": 1, "prefix": 0},
  {"key": "acme.gems:ruby", "container": "chest", "x": 300, "y": 50, "slot": 39, "stack": 20, "prefix": 0,
   "orphan": "missing_mod"}
]}
```

Contêineres: `inventory` (0..57), `armor`, `dye`, `misc_equips`, `misc_dyes`, `piggy_bank`, `safe`,
`defenders_forge`, `void_vault`, `loadout_armor`, `loadout_dye`, `temporary` (0 mouse, 1 sacrifício
da Jornada, 2 Guia, 3 Reforja) e `chest`. Em `temporary` vale o que o jogo grava: no save,
`Main.mouseItem` etc.; no load, `Player._temporaryItemSlots`. Como no jogo, um item que está só em
`_temporaryItemSlots` (personagem carregado e salvo pelo menu antes de entrar num mundo) não volta
ao arquivo. Trocar o prefixo do projeto (`tools/rename_prefix.py`) muda
o sufixo do arquivo paralelo e os ids dos mods, ou seja, as chaves.

### Como o jogo salva (dump desta build)

O formato é o do PC 1.4.4. RVAs em `il2cpp_bridge/include/terraria_offsets.h`.

| método | RVA | papel |
|---|---|---|
| `Player.SavePlayer(PlayerFileData, bool, bool)` | `0x1043A9C` | chamado por `WorldGen.saveToonWhilePlayingCallBack`, `SaveAndQuitCallBack` (ThreadPool), `PlayerFileData.CreateAndSave`/`Rename`, `Netplay.InnerClientLoop` |
| `Player.InternalSavePlayerFile(PlayerFileData)` | `0x1044040` | **hook antes/depois**. Cifra com Rijndael e grava o `.plr` |
| `Player.LoadPlayer(string, bool)` | `0x1049FA8` | **hook depois**. Chamado por `Player.GetFileData` (lista do menu) |
| `WorldFile.SaveWorld(bool, bool, WorldSaveContext)` | `0x14F15E8` | único chamador de `InternalSaveWorld` |
| `WorldFile.InternalSaveWorld(bool, bool)` | `0x14F1AC8` | **hook antes/depois**. `SaveWorld_Version2` → `SaveChests` (`0x14F83CC`) |
| `WorldFile.LoadWorld(bool)` | `0x14EC048` | **hook depois**. `LoadWorld_Version2` → `LoadChests` (`0x14FD70C`) |

| onde | formato do tipo do item |
|---|---|
| `.plr`: `armor`, `dye`, `miscEquips`, `miscDyes` | `netID` int32 + `prefix` byte |
| `.plr`: `inventory[0..57]` | `netID` int32, `stack` int32, `prefix` byte, `favorited` bool |
| `.plr`: `bank`, `bank2`, `bank3` (40) / `bank4` (40) | `netID` int32, `stack` int32, `prefix` byte (+ `favorited` no `bank4`) |
| `.plr`: temporários e loadouts | `Item.Serialize` (`0xBB8988`): `netID` int32, `stack` int32, `prefix` byte |
| `.wld`: `SaveChests` | por baú `x`, `y` int32 e nome; por item `stack` int16 e, se > 0, `netID` int32 + `prefix` byte |

Um ID de mod gravado ali não volta: todo load passa por `Item.SetDefaults`, que troca `type >= 5456`
por ar (`type = netID = stack = 0`, sem erro), e inventário e bancos conferem `id >= 5456` antes. O
item some em silêncio; e, se os IDs mudarem, um ID antigo vira outro item de mod. No `.wld`,
`SaveChests` indexa `ItemID.Sets.ItemsThatShouldNotBeInInventory[type]` e estoura antes de gravar
quando `type >= 5456` (com a casca, abaixo, `type` nunca passa de 5455).

### O que ainda fica de fora

- **Tile entities do mundo**: molduras (`TEItemFrame`), suportes de arma (`TEWeaponsRack`), pratos
  (`TEFoodPlatter`), manequins (`TEDisplayDoll`) e cabideiros (`TEHatRack`) gravam o item em
  `WriteExtraData` (`0x7B3E20`, `0x7BAF28`, `0x7AF108`, `0x7AB6A0`, `0x7B0AA8`). Um item de mod
  colocado num deles ainda chegaria ao `.wld`.
- **Pesquisa da Jornada** (`CreativeUnlocksTracker.Save`, chamado pelo save do personagem) e a
  sincronização de saves do celular (`SaveSynchronisationOperation`).
- O item reinserido no load é montado pelo `Item.SetDefaults` do jogo (casca + definição) e recebe
  pilha, prefixo (`Item.Prefix`) e favorito. No save, "tirar" um item zera só o que o arquivo grava
  (`netID`, `stack`, `prefix`, `favorited`) e o devolve no mesmo objeto, sem `SetDefaults`.

## Build

```sh
# host: runtime + adaptador (contra a ponte de host) + mod de exemplo + testes
cmake -S . -B build && cmake --build build && (cd build && ctest --output-on-failure)

# Android arm64: libtlmod_runtime.so (+ libtlmod_bridge.so) e o mod
cmake -S . -B build-android -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
cmake --build build-android
```

Os mods compilados saem em `<build>/dist/mods/<mod>/`, já no formato de pasta.
Para compilar um mod fora deste repositório: `cmake -S mods/example_item -B b -DTLMOD_SDK_INCLUDE_DIR=…/modapi/include`.

No Android, o runtime procura mods em `$TLMOD_MODS_DIR`; se não houver, em
`/storage/emulated/0/Android/data/<pacote>/files/tlmod/mods`; e por último em `/data/data/<pacote>/files/tlmod/mods`.

### Bibliotecas dos mods: cópia privada antes do dlopen

O armazenamento externo (`/storage/emulated/0/...`) é montado `noexec` e fica fora dos caminhos
permitidos do namespace do linker, então o `dlopen` direto da pasta de mods falha. Antes do
`dlopen`, o runtime copia a biblioteca de cada mod para
`<Context.getCodeCacheDir()>/tlmod/<mod_id>/` (obtido pelo `Application` via JNI; se o JNI
falhar, `/data/data/<pacote>/code_cache`) e carrega a cópia (`src/library_cache.cpp`):

- **atômica**: grava `<nome>.tmp-<pid>-<n>` na mesma pasta e faz `rename` sobre o destino;
  temporários de uma cópia interrompida são apagados na vez seguinte;
- **conferida**: o SHA-256 do temporário precisa bater com o da origem (e com o que foi lido durante
  a cópia: se a origem mudar no meio, a cópia é recusada);
- **somente leitura** (`0444`), como o Android 14 exige para código carregado dinamicamente;
- **reaproveitada** quando o SHA-256 da cópia existente já bate com o da origem (só corrige a
  permissão, se alguém a mudou).

Se o `dlopen` falha, o estado do mod guarda a mensagem inteira do `dlerror()`, com o caminho da
cópia; o painel mostra o texto todo (quebra linha, dá para selecionar e copiar).

### Log em arquivo

O runtime grava `tlmod/tlmod.log` (ao lado de `mods/`), com as linhas do runtime, dos mods e da
ponte (a ponte repassa as suas por `TLMod_SetLogCallback`; as emitidas antes de o runtime subir são
reenviadas com a hora original). Cada abertura do jogo começa com uma linha
`===== TLMod: sessão iniciada ...`. Passando de 1 MiB, o arquivo vira `tlmod.log.1`. As últimas 500
linhas ficam em memória para a aba **Log** do painel. Tudo continua indo também para o logcat
(tags `TLMod` e `TLModBridge`).

## Ainda não implementado

- **Receitas e loja**, e textura própria nas telas listadas em [Texturas de mod](#texturas-de-mod).
- **Multiplayer.** O servidor precisa enviar a tabela chave → ID aos clientes.
- **Outros tipos.** NPC, projectile, tile, wall e buff já têm faixa de ID, mas ainda não têm
  `register_*` na API.
