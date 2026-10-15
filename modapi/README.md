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

Ainda falta: nome exibido, barra e música de boss (`boss`), textura própria, sincronizar a tabela de
slots e o `local[]` em multiplayer, e confirmar no jogo que `NewNPC` aceita `source` nulo.

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
  - 77 arrays em `ItemID.Sets`;
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
ele já tem.

**Para testar no aparelho:** crie um arquivo vazio `give_on_start` na pasta do mod de exemplo
(`.../tlmod/mods/example_item/give_on_start`). Ao entrar num mundo vêm 3 Rubis Fictícios, com o
desenho da Lente, mas com nome, tooltip, valor (50 de prata), raridade e pilha (999) do mod.

O que ainda não está aqui:
- textura própria (o jogo desenha a casca, `TextureAssets.Item[type]`);
- receitas e loja;
- sincronizar a tabela de IDs em multiplayer;
- telas que leem o nome direto de `Lang` por `type` em vez de `Item.Name` mostrariam o nome da casca.

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

## Ainda não implementado

- **Textura própria de item**, receitas e loja (o item de mod usa a textura da casca).
- **Multiplayer.** O servidor precisa enviar a tabela chave → ID aos clientes.
- **Outros tipos.** NPC, projectile, tile, wall e buff já têm faixa de ID, mas ainda não têm
  `register_*` na API.
