# Auditoria 03: segurança dos hooks que o TLMod já instala

Terraria Mobile 1.4.4.9 (Unity 2021.3.56f2, IL2CPP, Android arm64), comparado com o TLMod da branch
`ccr-bbd92bf9-i01buk` (código da ponte e do runtime no commit `f917b04`).

Auditoria só de leitura. As fontes são:

- o dump regenerado (`dump.cs`, `script.json`) e a desmontagem da `libil2cpp.so`;
- o código da ponte (`il2cpp_bridge/`) e do runtime (`modapi/`);
- a `libtlmod_bridge.so` arm64 compilada desse mesmo código (17/10, 19:30), usada só para contar as
  instruções dos handlers;
- o código do ShadowHook 2.0.1, a versão que a ponte compila;
- o `tlmod.log` da abertura de 17/10 às 15:17:16, com os 46 hooks instalados.

Dump, metadata e bibliotecas ficam fora do repositório. Nenhum código foi alterado; este arquivo é o único
acréscimo.

**Módulo 3:** para cada hook que o TLMod instala:

- quais threads chamam o método;
- se o handler faz algo inseguro para essa thread;
- se há reentrância;
- quanto custa nos caminhos quentes;
- se alguma sobrecarga ou cópia contorna o hook.

- [Como ler](#como-ler)
- [Resumo](#resumo)
- [1. As threads do jogo](#1-as-threads-do-jogo)
- [2. Tabela por hook](#2-tabela-por-hook)
- [3. Respostas diretas](#3-respostas-diretas)
- [4. Problemas, em ordem de gravidade](#4-problemas-em-ordem-de-gravidade)
- [5. Experimentos no aparelho](#5-experimentos-no-aparelho)
- [6. Ações recomendadas](#6-ações-recomendadas)

---

## Como ler

**Níveis de confiança**, os mesmos das auditorias 01 e 02:

- **confirmado no binário**: visto na desmontagem ou na metadata;
- **inferido**: decorre do que foi lido, mas um passo não foi rastreado (chamada virtual, condição em
  tempo de execução);
- **só o aparelho confirma**: depende do que acontece em tempo de execução. Cada caso tem um
  experimento na [seção 5](#5-experimentos-no-aparelho).

Afirmações sobre código-fonte trazem arquivo e linha e valem como confirmadas no código.

**Threads.** A P é a do Unity. As outras são criadas pelo `ThreadManager` do jogo (`ManagedThread` →
`Thread.Start`, ver a auditoria 01, seção 5.1):

| sigla | thread | entrada |
|---|---|---|
| **P** | principal do Unity: atualização (`Main.Update` → `DoUpdate`) e desenho (`Main.DoDraw`, telas `GUI*`) | `XNAUnityRunner` |
| **G** | geração de mundo | `WorldGen.worldGenCallback` (`0x1305BE4`) |
| **C** | carregar mundo | `WorldGen.playWorldCallBack` (`0x130D624`), `serverLoadWorldCallBack` (`0x130F0FC`), `WorldFile.renameWorldCallBack` (`0x14F5864`) |
| **S** | save com o jogo andando ou saindo | `saveToonWhilePlayingCallBack` (`0x130ED54`: autosave do personagem e morte), `saveAndPlayCallBack` (`0x130EBDC`: autosave do mundo), `SaveAndQuitCallBack` (`0x130C8CC`: salvar e sair, também pela queda de rede) |
| **R** | rede do cliente | `Netplay.TcpClientLoop` (`0x11D2654`) → `InnerClientLoop`: só o save quando a conexão cai |
| **Q** | sair do multiplayer | `GUINetPlayStatusMenu.QuitCallBack` (`0xA50A44`) |
| **H** | conversão do modo difícil, com o jogo andando | `WorldGen.smCallBack` (`0x1353F94`) |

**Custo.** As instruções foram contadas na `libtlmod_bridge.so` compilada, com `llvm-objdump`. O
"caminho rápido" é o que o handler faz quando o item ou NPC é original, o caso comum. Os tempos são
estimativas (inferido).

**Limite da busca.** O fecho de chamadas parte de cada raiz de thread e segue só chamadas diretas
(`bl`/`b`). Delegates e chamadas virtuais ficam de fora. Onde importam (etapas da geração de mundo, tile
entities), sigo à parte. Cada caminho citado foi conferido na desmontagem. Os caminhos que a busca achou
mas que não acontecem estão na [seção 1.3](#13-caminhos-que-a-busca-achou-e-não-acontecem).

---

## Resumo

1. **Threads.** Dos 46 hooks, 34 só rodam na thread principal (P). Os outros 12 também rodam em
   threads do jogo:
   - `Item.SetDefaults` e `Item.RebuildTooltip`: geração (G), carga (C), saves (S, pelo próprio TLMod)
     e saída do multiplayer (Q);
   - `NPC.SetDefaults` e `NPC.NewNPC`: geração (o Guia) e carga;
   - os 4 de save e load: quase sempre fora da P;
   - nomes de NPC, `Player.Hurt` e o catálogo da pesquisa: fora da P só em caminhos improváveis.

   Nenhum hook roda no ThreadPool, nas threads de render ou em threads Java. Das threads de rede, só a
   do cliente (R) chega a um hook: o save do personagem quando a conexão cai.
2. **Estado por thread.** Nesta build, `Main.myPlayer`, `Main.mouseItem` e outros vêm de
   `LocalUserGameState.Instance`, que é `[ThreadStatic]`. Toda thread do jogo que chega a um hook chama
   `LocalUser.LoadThreadstate` antes. Os handlers só leem esse estado nessas threads: correto.
3. **Unity e alocação.** Nenhum handler chama API do Unity fora da P: a textura só é criada no
   `DoUpdate`. Toda alocação gerenciada feita por handler acontece numa thread do jogo, que já está
   anexada ao IL2CPP.
4. **O ponto fraco é a API de mods.**
   - Sete funções que mexem no jogo rodam na thread de quem chama, sem conferir qual é.
   - Comandos e provedores dos mods rodam na thread de UI do Android, que não está anexada ao IL2CPP.
   - Os mods do repositório respeitam o contrato; um mod de terceiros pode causar corrida ou queda.
5. **Exceções.**
   - Os callbacks de NPC e de dano não têm `try/catch` em nenhuma camada: uma exceção C++ de um mod
     atravessa `NPC.UpdateNPC` ou `Player.Update`.
   - As trocas de textura dos 16 handlers de desenho e de tela não são desfeitas se o desenho original
     lançar.
6. **Reentrância** está tratada onde existe:
   - `Player.Hurt`, que chama a si mesmo;
   - os saves, cujo original volta pelo hook;
   - o `SetDefaults` chamado de dentro dos hooks de save, de receita e de frame;
   - o botão Mods, que passa pelo próprio hook de botão.

   Não há trava presa nem recursão infinita.
7. **Caminhos quentes.**
   - `NPC.AI` chega a 12 000 chamadas por segundo; o desenho de item e NPC, a centenas por quadro; o
     `SetDefaults`, a rajadas.
   - O caminho rápido custa de 30 a 130 instruções.
   - Os hooks do HUD fazem `runtime_invoke` com retorno encaixotado: alocações a cada quadro.
   - Os botões de mods provavelmente são desenhados duas vezes por quadro.
8. **Contornos.**
   - `Main.DrawNPCDirect` tem uma segunda cópia completa, de 4 argumentos, usada no bestiário e nos
     créditos.
   - `Player.KillMe` mata sem passar por `Hurt` (afogamento, dano contínuo, língua da Parede de Carne):
     o "Invencível" das Ferramentas não protege contra isso.
   - As outras sobrecargas e cópias passam pelos hooks.

---

## 1. As threads do jogo

### 1.1 Quem cria thread

Chamadas a `ThreadManager.Run`/`RunForUser`/`RunForUserCritical`/`RunCritical`, `Thread.Start`,
`ThreadPool.QueueUserWorkItem`/`UnsafeQueueUserWorkItem`, `Task.Run`, `TaskFactory.StartNew` e
`FastParallel.For`. O alvo do delegate é o `MethodInfo` lido logo antes da chamada.

| raiz | criada em | hooks alcançados | confiança |
|---|---|---|---|
| G `worldGenCallback` | `CreateNewWorld` `0x130C42C` (`RunForUserCritical`) | `Item.SetDefaults`/`RebuildTooltip`, `NPC.NewNPC`/`SetDefaults`, `WorldFile.InternalSaveWorld` | confirmado no binário |
| C `playWorldCallBack`, `serverLoadWorldCallBack`, `renameWorldCallBack` | `playWorld` `0x130EBC0`, `serverLoadWorld` `0x130F7BC`, `RenameWorldUsingLoadAndSave` `0x14F5848` | `WorldFile.LoadWorld`, `Item.SetDefaults`/`RebuildTooltip`, `NPC.SetDefaults`, `WorldFile.InternalSaveWorld` | confirmado no binário |
| S autosave, morte, salvar e sair | `saveToonWhilePlaying` `0x130F0F4`, `saveAndPlay` `0x130ED4C`, `SaveAndQuit` `0x130D2DC`, `UserManagement.HandleNetworkLost` `0xAA7C38` | `Player.InternalSavePlayerFile`, `WorldFile.InternalSaveWorld`; `Item.SetDefaults` pelo TLMod | confirmado no binário |
| R `TcpClientLoop` | `Netplay.StartTcpClient` `0x11D7A44` | `Player.InternalSavePlayerFile` (`InnerClientLoop` `0x11D2208`) | confirmado no binário |
| Q `QuitCallBack` | `GUINetPlayStatusMenu.Quit` `0xA5099C` | `Player.LoadPlayer`, `Item.SetDefaults` | confirmado no binário |
| H `smCallBack` | `WorldGen.StartHardmode` `0x1353D30` | `NPC.NewNPC`/`SetDefaults`, `Item.SetDefaults`, nomes de NPC, `Player.Hurt`: as chamadas existem, a condição não foi verificada | inferido |
| render (até 3) `RenderThreadInstance.DrawThread` `0xAB6700` | `RenderThreadInstance..ctor` `0xAB53D8` (`RunCritical`) | nenhum ([seção 1.3](#13-caminhos-que-a-busca-achou-e-não-acontecem)) | confirmado no binário |
| rede: Telepathy (5 threads), `TcpSocket.ListenLoop`, `Netplay.ServerLoop` e `BroadcastThread`; descoberta por Wi-Fi (`ServerAutoFinder`, `SaveSynchronisation*`) | `Thread.Start`, `ThreadManager.Run` | nenhum | inferido (sem chamadas virtuais) |
| `CacheFileManager` (2), `AsyncActionDispatcher` (carregador de assets), `Main.RunServerCallBack`, `joinLocalWorldCallBack` | idem | nenhum | inferido |
| ThreadPool | no jogo, só `WorldGen.RenameWorld` (`0x130D3A8`) | — | confirmado no binário: `RenameWorld` não tem chamador nem outra referência ao `MethodInfo` |
| Java | o único `AndroidJavaProxy` do jogo é `AndroidImmersive.Listener` | nenhum | inferido |

`FastParallel.For` não tem chamador. `Task.Run` só aparece em código do `System`.

**Correção à auditoria 01.** A seção 5.1 de lá lista `WorldGen.RenameWorld` → ThreadPool como caminho
ativo. O método não tem chamador: a renomeação no celular usa `WorldFile.RenameWorldUsingLoadAndSave` →
`renameWorldCallBack`, numa thread do `ThreadManager`. O jogo não usa o ThreadPool nesses caminhos.

### 1.2 Estado por thread (`[ThreadStatic]`)

Campos `[ThreadStatic]` do jogo no dump:

- `LocalUserGameState.Instance`: estado do jogador local;
- `FileManager.Instance`;
- `Main.rand`;
- `WorldGen._genRand` e `_genRandSeed`.

Vários "estáticos" do `Main` são getters sobre `LocalUserGameState.Instance`. Exemplo, o `Main.myPlayer`
que a ponte usa para achar o jogador local:

```
; Main.get_myPlayer 0xC68B64
0xc68b94: ldr    x0, [x20]                  ; TypeInfo LocalUserGameState
0xc68b98: bl     #0x642b50                  ; dados [ThreadStatic] desta thread
0xc68b9c: ldr    x8, [x0]                   ; LocalUserGameState.Instance
0xc68ba0: cbz    x8, #0xc68bb4              ; nulo nesta thread:
0xc68ba4: ldr    w0, [x8, #0x30c]           ;   (senão) myPlayer
0xc68bb4: bl     #0x642c38                  ;   NullReferenceException
```

`Main.get_mouseItem` (`0xC68290`) é igual, com `+0x2B8`. Confirmado no binário.

`LocalUser.LoadThreadstate` (`0xAA486C`) grava `Instance = this.GameState` (`+0xB0`) e
`FileManager.Instance = this.fileManager` (`+0x68`). Ela tem 21 chamadas em 20 métodos. Entre eles
estão, logo no começo, todas as raízes que chegam a hooks:

- `worldGenCallback` (`0x1305C84`);
- `playWorldCallBack` (`0x130D780`), `serverLoadWorldCallBack` (`0x130F1F4`) e
  `renameWorldCallBack` (`0x14F58F8`);
- `saveToonWhilePlayingCallBack` (`0x130EDEC`), `saveAndPlayCallBack` (`0x130EC44`) e
  `SaveAndQuitCallBack` (`0x130C9DC`);
- `TcpClientLoop` (`0x11D2758`), `QuitCallBack` (`0xA50AE4`) e `smCallBack` (`0x135410C`).

Confirmado no binário. A única callback que não a chama é o `WorldGen.RenameWorldCallBack` morto.

**Consequência.** Nas threads que chegam a hooks, o estado do jogador existe, e é o mesmo objeto da P.
Fora delas, o getter lança `NullReferenceException`. A ponte já sabe disso:

- `TLMod_IsGameMenu` lê `Instance` por `il2cpp_field_static_get_value`, que resolve o campo por thread
  (`world_state.cpp:1`–`7`, `:73`);
- `TLMod_ReadItemTraits` só roda na thread do jogo (`item_traits.cpp:110`).

### 1.3 Caminhos que a busca achou e não acontecem

- **Thread de render → `Item.SetDefaults`.** O caminho seria `DrawThread` → `TileDrawing.DrawTiles` →
  `DrawTiles_EmitParticles` → `ParticleOrchestrator.RequestParticleSpawn` → `Spawn_ItemTransfer` →
  `ItemTransferParticle.Prepare` → `SetDefaults`. Mas o pedido é de outra partícula:

  ```
  0x8ba888: mov    w0, #1                     ; clientOnly
  0x8ba88c: mov    w1, #0x1e                  ; ParticleOrchestraType.PooFly (30)
  0x8ba8a0: bl     #0x899ab8                  ; RequestParticleSpawn
  ```

  O `SetDefaults` só existe no caso `ItemTransfer` (23). As tarefas da thread de render são tiles,
  paredes, luz, fundos e o scanner de tiles (enum `RenderThreadInstance.DrawTask`). Molduras, expositores
  e pratos são desenhados na P: `TileDrawing.DrawSpecialTilesLegacy` só é chamado por `TileDrawing.Draw`,
  e este por `DoDraw_Tiles_Solid`/`NonSolid`, `RenderTiles` e `DrawCapture`. Confirmado no binário.
- **Salvar e sair → `Item.SetDefaults`.** `ClearPlayerTempInfo` chama `Item.TurnToAir(false)`
  (`0x1046768`: `mov w1, wzr`), que só chama `SetDefaults` com `true`. O mesmo vale para
  `CreativeUI.Reset` na geração (`0x8F756C`). Confirmado no binário.
- **Chamadas fantasmas.** `b 0x122ECB8` (`SetupRecipes`) dentro de `GUIAmmo.Draw` (`0x122DCB8`) e
  `b 0xBFC154` (`DrawNPCDirect`) dentro da outra `DrawNPCDirect` (`0xBFB154`) vêm logo depois de
  `bl 0x642c40`, que lança exceção e não volta. Nenhuma das duas é chamada de verdade.

### 1.4 Os 12 hooks que rodam fora da P

✓ = confirmado no binário; "TLMod" = só quando o próprio TLMod chama o jogo nessa thread; "em tese" =
a chamada existe, mas o caminho é improvável.

| hook | P | G | C | S | R | Q | H |
|---|---|---|---|---|---|---|---|
| `Item.SetDefaults` | ✓ | ✓ | ✓ | TLMod | — | ✓ | inferido |
| `Item.RebuildTooltip` | ✓ | ✓ | ✓ | TLMod | — | ✓ | inferido |
| `NPC.SetDefaults` | ✓ | ✓ | ✓ | — | — | — | inferido |
| `NPC.NewNPC` | ✓ | ✓ | em tese | — | — | — | inferido |
| `NPC.get_TypeName` | ✓ | em tese | em tese | — | — | — | em tese |
| `NPC.GetTypeNetName` | ✓ | em tese | em tese | — | — | — | em tese |
| `Player.Hurt` | ✓ | em tese | em tese | — | — | — | em tese |
| `CreativeItemSacrificesCatalog.TryGetSacrificeCountCapToUnlockInfiniteItems` | ✓ | em tese | em tese | — | — | — | em tese |
| `Player.InternalSavePlayerFile` | ✓ (menu) | — | — | ✓ | ✓ | — | — |
| `Player.LoadPlayer` | ✓ (menu) | — | — | — | — | ✓ | — |
| `WorldFile.InternalSaveWorld` | — | ✓ | ✓ | ✓ | — | — | — |
| `WorldFile.LoadWorld` | — | — | ✓ | — | — | — | — |

Os outros 34 hooks só rodam na P.

---

## 2. Tabela por hook

Os 46 hooks do log, na ordem dos arquivos da ponte. Os handlers ficam em `il2cpp_bridge/src/`.

### 2.1 Jogador e NPCs

#### `Player.Hurt` (`0xFA15B8`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_PlayerHurt`, `bridge.cpp:373` |
| threads | **P**: 21 chamadas em 16 métodos (`Player.Update` ×4, `UpdateBuffs`, `ApplyTouchDamage`, `Update_NPCCollision`, `WOFTongue`, `MessageBuffer.ProcessData`…). Em tese G, C e H, por `KillTile` → `NewProjectile` → `Projectile.Kill` → `Damage` → `Hurt` |
| o que faz | callback do runtime antes e depois do original; os mods podem mudar o dano ou cancelar o golpe |
| riscos | nenhum na P. O callback das Ferramentas lê `local_player` (`[ThreadStatic]`), que existe em todas essas threads. Sem `try/catch` no caminho até o mod (`bridge_adapter.cpp:21`, `mod_loader.cpp:130`): [problema 2](#problema-2) |
| reentrância | `Hurt` chama a si mesmo (`0xFA26C0`); `t_hurtDepth` (`thread_local`) pula o callback aninhado. Se o original lançar, `t_hurtDepth` fica em 1 e os callbacks de dano param naquela thread ([problema 3](#problema-3)) |
| custo | chamada rara (golpes) |
| contornos | `Player.KillMe` mata sem `Hurt` em 6 métodos: `CheckDrowning`, `UpdateLifeRegen` (×3), `WOFTongue`, `BordersMovement`, `ItemCheck_UseTeleportRod` e `ProcessData` |
| confiança | confirmado no binário (chamadores) e no código (handler) |

#### `NPC.SetDefaults` (`0xDAF1BC`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_NpcSetDefaults`, `npc_hooks.cpp:289` |
| threads | **P**: 69 chamadas em 37 métodos (spawn, `Transform`, rede, amostras do `ContentSamples`). **G**: dentro do `NewNPC` da etapa do Guia. **C**: `WorldFile.LoadNPCs` (`0x14FE348`, `0x14FE5D4`). **H**: inferido |
| o que faz | antes do original, `on_set_defaults(índice)`: o runtime desmarca o slot (`npcSlots[i] = {}`, `mod_loader.cpp:401`) |
| riscos | G, C e H escrevem `npcSlots` sem trava enquanto a P pode ler ([problema 5](#problema-5)). Callback sem `try/catch` ([problema 2](#problema-2)) |
| reentrância | `SetDefaults` chama `SetDefaultsFromNetId` (`0xDAF550`), que volta a `SetDefaults` (`0xEDC68C`, `0xEDCA3C`, `0xEDCB3C`). O callback roda de novo e só desmarca outra vez |
| custo | por spawn ou transformação; 688 no início (amostras). `IndexOf` faz um `il2cpp_field_static_get_value`; se o NPC não estiver em `Main.npc` (amostras), varre 201 ponteiros. Handler de 130 instruções |
| contornos | nenhum: `SetDefaultsKeepPlayerInteraction` (`Transform`, vermes) e `SetDefaultsFromNetId` chamam `0xDAF1BC` |
| confiança | confirmado no binário e no código |

#### `NPC.AI` (`0xDCE54C`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_NpcAI`, `npc_hooks.cpp:303` |
| threads | só **P**. Único chamador: `NPC.UpdateNPC` (`0xF4A7C8`) |
| o que faz | `on_ai(índice)` antes; se o mod devolver verdadeiro, a AI original não roda |
| riscos | o código do mod é chamado sem `try/catch` em nenhuma camada (`tlmod.hpp:273`, `mod_loader.cpp:406`, `bridge_adapter.cpp:53`): uma exceção C++ atravessa `NPC.UpdateNPC` ([problema 2](#problema-2)) |
| reentrância | nenhuma |
| custo | **o hook mais quente**: até 200 NPCs × 60/s = 12 000 chamadas/s. Caminho rápido: `IndexOf` (um `il2cpp_field_static_get_value`), `std::function`, `ValidSlot` e o teste do tipo. Handler de 121 instruções; estimativa de 50 a 100 ns por chamada, ~1 ms por segundo de jogo |
| contornos | nenhum |
| confiança | confirmado no binário (chamador) e no código; tempo inferido |

#### `NPC.NewNPC` (`0xEE4630`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_NpcNewNPC`, `npc_hooks.cpp:266` |
| threads | **P**: 283 chamadas em 62 métodos (spawn natural, invocações, itens, estátuas). **G**: etapa `WorldGen.<>c.<GenerateWorld>b__277_79` (`0x1475550` e mais 11 chamadas, inclusive o Guia, tipo 22). **H**: inferido (`Convert` → `KillTile` → `ShakeTree` → `NewNPC`) |
| o que faz | `remap_spawn_type` (ID de mod → casca) antes; `on_spawned` depois, que marca o slot e chama `on_spawn` do mod |
| riscos | na G e na H, `on_spawned` roda fora da P; com tipo original, `OnNpcSpawned` sai na primeira linha (`mod_loader.cpp:376`). `on_spawn` do mod sem `try/catch` ([problema 2](#problema-2)) |
| reentrância | dentro do original rodam os hooks de `NPC.SetDefaults` e, para chefes, de `GetTypeNetName`. Um mod pode chamar `spawn_npc` dentro do `on_spawn`: os índices são outros e `npcSlots` tem tamanho fixo (`mod_loader.cpp:871`) |
| custo | por spawn; handler de 123 instruções |
| contornos | NPCs que chegam pela rede no cliente e os do arquivo do mundo (`LoadNPCs` → `SetDefaults`) não passam por `NewNPC` (já dito no comentário do handler e na auditoria 01) |
| confiança | confirmado no binário; H inferido |

#### `NPC.get_TypeName` (`0xED9240`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_NpcGetTypeName`, `screen_hooks.cpp:519` |
| threads | **P**: 13 chamadas em 11 métodos (`get_FullName`, `get_GivenOrTypeName`, `GetFullNetName`, `PopupText.NewText`, `WorldGen.MoveTownNPC`, ordenação alfabética do bestiário…). Em tese G e H, por `NewNPC` |
| o que faz | `ModNpcName`: nome do NPC de mod pelo slot. A string gerenciada é criada uma vez por texto e presa por GC handle, num cache sob mutex |
| riscos | nenhum: mutex próprio e `il2cpp_string_new` numa thread do jogo |
| reentrância | nenhuma |
| custo | 29 instruções + `npc_name`. Na ordenação do bestiário, os NPCs de amostra não estão em `Main.npc`, e cada chamada varre 201 ponteiros: rajada pequena |
| contornos | nenhuma cópia inline: o corpo é `Lang.GetNPCNameValue(netID)` (`0xED9278` lê `+0x210`), e os outros 9 chamadores de `GetNPCNameValue` passam um id (bestiário, buff, acessórios), não um NPC |
| confiança | confirmado no binário e no código |

#### `NPC.GetTypeNetName` (`0xEDB0BC`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_NpcGetTypeNetName`, `screen_hooks.cpp:526` |
| threads | **P**: 9 chamadas em 7 métodos (`SpawnOnPlayer`, `SpawnBoss`, `SpawnWOF`, `NewNPC`, `DoDeathEvents_CelebrateBossDeath`, `GetGivenOrTypeNetName`, `GetFullNetName`). Em tese G, C e H |
| o que faz | `ModNpcName` e `NetworkText.FromLiteral` por `runtime_invoke` |
| riscos | nenhum nas threads do jogo |
| reentrância | nenhuma |
| custo | raro (anúncios e mortes) |
| contornos | `CountKillForBannersAndDropThem` e `GetInvasionWaveText` usam `Lang.GetNPCName(id)` direto: a mensagem de estandarte de um NPC de mod sai com o nome da casca |
| confiança | confirmado no binário e no código |

### 2.2 Item

#### `Item.SetDefaults(int, bool, ItemVariant)` (`0xBAD034`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_ItemSetDefaults`, `item_hooks.cpp:277` |
| threads | **P**. **G**: `WorldGen.AddBuriedChest` (122 chamadas a `SetDefaults(int)`), `FinishDrunkGen`… **C**: `WorldFile.LoadChests` → `Item.netDefaults` (`0x14FD8F4`, `0x14FD944`); tile entities por chamada virtual. **Q**: `LoadPlayer` → itens. **S**, C, Q e P também pelo TLMod: `TLMod_SetItemDefaults` ao devolver itens depois do save e ao reinserir no load (`bridge_adapter.cpp:314`). **H**: inferido |
| o que faz | `remap_type` (ID de mod → casca) antes; depois, `netID` = ID de mod e `on_defaults` (atributos, nome, tooltip) |
| riscos | nenhum. O que o runtime lê é imutável depois do `LoadAll` (o registro; os hooks de item só são publicados no fim dele, `mod_loader.cpp:1246`) ou fica sob `g_cacheMutex` (strings e tooltips). Cria objetos gerenciados (`il2cpp_string_new`, `LocalizedText`, `ItemTooltip`) só em threads do jogo. Callbacks com `try/catch` (`bridge_adapter.cpp:150`, `:157`) |
| reentrância | o original chama o hook de `RebuildTooltip` (`0xBADBAC`) com o `netID` ainda da casca; o tooltip do mod entra depois. O TLMod chama `SetDefaults` por `runtime_invoke` (passa pelo hook) de dentro dos hooks de save, de receita e do `DoUpdate`; nenhum handler o chama de dentro do próprio `SetDefaults` |
| custo | em rajadas: 5 455 no início (amostras); um por slot de baú ao carregar o mundo; telas de criação. Handler de 58 instruções + `remap_type` (`std::function` e teste de faixa, `content_registry.cpp:92`): dezenas de ns, contra microssegundos do corpo do `SetDefaults` |
| contornos | nenhum. As três entradas terminam em `0xBAD034`: `SetDefaults(int)` (`0xBA8CD0`, 7 181 chamadas em 106 métodos), `netDefaults` (`0xBAB378`, 140 em 42) e a própria (17 em 16). `SetDefaults1` a `5` só são chamados de dentro dela |
| confiança | confirmado no binário e no código |

#### `Item.RebuildTooltip` (`0xBAAFE8`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_ItemRebuildTooltip`, `item_hooks.cpp:291` |
| threads | as do `SetDefaults`, que o chama, e **P** (`GUICrafting.CraftButtonOver`, `GUIItemDuplication.CraftItemOver`) |
| o que faz | com `netID` de mod, põe o tooltip do mod em vez de `Lang.GetTooltip(netID)`, que estouraria o índice |
| riscos | os mesmos do `SetDefaults`: nenhum |
| reentrância | nenhuma |
| custo | 77 instruções; para item original, lê o `netID` e chama o original |
| contornos | nenhum: `Lang.GetTooltip` (`0x11F5414`) só é chamado por `RebuildTooltip` (`0xBAB040`) |
| confiança | confirmado no binário e no código |

### 2.3 Frame e receitas

#### `Main.DoUpdate` (`0xCADDB8`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_MainDoUpdate`, `world_state.cpp:103` |
| threads | só **P** (`Main.Update`, `0xCADD1C`) |
| o que faz | grava a thread do jogo (`OnGameThread`, `world_state.cpp:57`), aplica receitas pendentes e chama o frame do runtime: 30 leituras estáticas de progresso, `IsGameMenu` e o frame handler. Este faz a avaliação de casca, cria até 4 texturas, monta o catálogo de nomes, atende a fila de `give_item` e de spawn e chama os mods |
| riscos | nenhum: a API do Unity (criação de textura) só é chamada aqui, na P. Frame dos mods com `try/catch` (`bridge_adapter.cpp:227`) |
| reentrância | o frame chama o jogo, e o jogo passa por outros hooks, todos na P: `GiveItem` → `SetDefaults`, `NewItem`; `SpawnNpc` → `NewNPC`; Ferramentas → `StrikeNPCNoInteraction` → loot → `NewItem` |
| custo | 60/s; handler de 37 instruções; o frame inteiro, alguns µs em regime. `ProcessNearSpawns` chama `LocalPlayerCenter` a cada quadro num mundo (`mod_loader.cpp:294`): um `runtime_invoke` de `get_myPlayer` com retorno encaixotado. No início, o catálogo faz até 512 `runtime_invoke` por quadro durante ~24 quadros (`kCatalogPerFrame`, `mod_loader.cpp:97`) |
| contornos | nenhum |
| confiança | confirmado no binário e no código; tempos inferidos |

#### `Recipe.SetupRecipes` (`0x122ECB8`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_RecipeSetupRecipes`, `recipe_hooks.cpp:244` |
| threads | só **P**: `Main.Initialize_AlmostEverything` (`0xC86534`), chamado pelo `DrawSplash`. O `b` em `GUIAmmo.Draw` é artefato ([seção 1.3](#13-caminhos-que-a-busca-achou-e-não-acontecem)) |
| o que faz | depois das receitas originais, as de mod: `SetDefaults` por `runtime_invoke` (hook de item), `AddRecipe` e `CreateRequiredItemQuickLookups`, sob `g_mutex` |
| riscos | segura `g_mutex` enquanto roda código do jogo; nada lá dentro tenta o mesmo mutex |
| reentrância | hooks de item dentro deste, na mesma thread |
| custo | uma vez |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

### 2.4 Saves

#### `Player.InternalSavePlayerFile` (`0x1044040`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_PlayerInternalSavePlayerFile`, `save_hooks.cpp:433` |
| threads | **P** no menu (`PlayerFileData.CreateAndSave`, `Rename`, `GUIPlayerSelectMenu.CloseNameEditAndSave`), **S** (`saveToonWhilePlayingCallBack`, `SaveAndQuitCallBack`) e **R** (`InnerClientLoop`). Único chamador: `SavePlayer` |
| o que faz | antes do save, tira os itens de mod; depois, devolve e grava o arquivo paralelo |
| riscos | na S o jogo segue na P e vê os slots vazios durante o save (auditoria 01, lacuna 2). Lê `Main.mouseItem`, `guideItem` e `reforgeItem` por `runtime_invoke` (`save_hooks.cpp:173`): são `[ThreadStatic]`, e toda thread de save chama `LoadThreadstate` antes, então leem o estado certo, o mesmo objeto da P |
| reentrância | o original roda por `runtime_invoke` e entra de novo no hook; `t_inPlayerSave` (`thread_local`, `save_hooks.cpp:202`) repassa direto. A exceção do save é capturada pelo `runtime_invoke`, avisada no "depois" e relançada com `il2cpp_raise_exception`. A "vez" de 30 s é tomada no "antes" e devolvida no "depois", também em falha |
| custo | raro |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

#### `Player.LoadPlayer` (`0x1049FA8`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_PlayerLoadPlayer`, `save_hooks.cpp:449` |
| threads | **P** (`Main.LoadPlayers` → `GetFileData`, lista de personagens) e **Q** (`QuitCallBack` → `RefreshPlayers` → `LoadPlayers` → `GetFileData` → `LoadPlayer`) |
| o que faz | depois do original, reinsere os itens de mod, com `SetDefaults` por `runtime_invoke` |
| riscos | nenhum: a Q tem o estado (`LoadThreadstate` em `0xA50AE4`) |
| reentrância | hook de item dentro deste |
| custo | um por personagem listado |
| contornos | nenhum: `Player_OldConsole`, `Player_OldSwitch` e `Player_OldMobile.LoadPlayer` só são chamados de dentro dele |
| confiança | confirmado no binário e no código |

#### `WorldFile.InternalSaveWorld` (`0x14F1AC8`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_WorldFileInternalSaveWorld`, `save_hooks.cpp:460` |
| threads | **S** (`saveAndPlayCallBack`, `SaveAndQuitCallBack`), **G** (fim do `worldGenCallback`, `0x1305DA0`) e **C** (`LoadWorld` → `SaveWorld`, `0x14EC5D0`, depois de gerar um mundo que faltava; `renameWorldCallBack`). Nunca na P. Único chamador: `SaveWorld` |
| o que faz | o mesmo do personagem, para os baús |
| riscos | os da auditoria 01: baús vazios durante o save, com o jogo andando |
| reentrância | `t_inWorldSave` |
| custo | raro |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

#### `WorldFile.LoadWorld` (`0x14EC048`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_WorldFileLoadWorld`, `save_hooks.cpp:477` |
| threads | só **C**: `playWorldCallBack` (4 chamadas), `serverLoadWorldCallBack` (4) e `renameWorldCallBack` |
| o que faz | depois do original, reinsere os itens de mod nos baús |
| riscos | nenhum |
| reentrância | hooks de item e, dentro do original, `InternalSaveWorld` (conversão de versão) |
| custo | um por mundo carregado |
| contornos | nenhum: os formatos antigos (`WorldFile_OldConsole`, `OldSwitch`, `OldMobile.LoadSaveData`) são lidos de dentro dele |
| confiança | confirmado no binário e no código |

### 2.5 Desenho de item e NPC

Os seis seguem o mesmo padrão: trocar a textura da casca, chamar o original e destrocar
(`drawx::BeginItem`/`EndItem`, `draw_hooks.cpp:153`–`173`). A troca mexe em tabelas globais:

- para item, `TextureAssets.Item[casca]` e `Main.itemAnimations[casca]`;
- para NPC, `TextureAssets.Npc[casca]`, `Main.npcFrameCount[casca]` e `npc.frame`.

Todos rodam só na P. As leituras diretas dessas tabelas estão todas em código de desenho da P: o desenho
de tiles nas threads de render não desenha item nem NPC (seção 1.3). Nenhum deles desfaz a troca se o
original lançar ([problema 3](#problema-3)).

#### `GUIInventory.DrawItem` (`0x95E6E4`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_GUIInventoryDrawItem`, `draw_hooks.cpp:465` |
| threads | **P** (`GUIInventory.ItemDraw` ×2) |
| custo | um por slot desenhado (inventário, baús, bancos). Caminho rápido: ~40 instruções antes do original (handler + saída do `BeginItem`) |
| reentrância | o original pode chamar `ItemSlot.DrawItemIcon`, também hookado: troca aninhada, desfeita na ordem certa |
| contornos | nenhum conhecido |
| confiança | confirmado no binário e no código |

#### `GUIHotbar.DrawHotbarItem` (`0x83BD64`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_GUIHotbarDrawHotbarItem`, `draw_hooks.cpp:476` |
| threads | **P** (`GUIHotbar.ItemDraw`) |
| custo | 10 por quadro, ~40 instruções cada |
| reentrância | como o anterior |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

#### `Main.DrawItem` (`0xCFA928`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_MainDrawItem`, `draw_hooks.cpp:487` |
| threads | **P** (`Main.DrawItems`) |
| custo | um por item no chão por quadro (até 400). O caminho rápido compilado tem 30 instruções até chamar o original, contando a chamada ao `BeginItem`, mais a saída dele |
| reentrância | nenhuma |
| contornos | nenhum: `Main.DrawItem_GetBasics`, que também lê `TextureAssets.Item`, só é chamado de dentro dele |
| confiança | confirmado no binário e no código |

#### `ItemSlot.DrawItemIcon` (`0x1632310`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_ItemSlotDrawItemIcon`, `draw_hooks.cpp:496` |
| threads | **P**: `ItemSlot.Draw`, `UIItemIcon.DrawSelf`, `ItemTransferParticle.Draw` e `TileDrawing.DrawSpecialTilesLegacy` (molduras, desenhadas na P: seção 1.3) |
| custo | um por ícone; 83 instruções no total |
| reentrância | aninhado no `GUIInventory.DrawItem` |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

#### `PlayerDrawLayers.DrawPlayer_27_HeldItem` (`0x15C0EDC`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_DrawPlayerHeldItem`, `draw_hooks.cpp:508` |
| threads | **P** (`LegacyPlayerRenderer.DrawPlayer_UseNormalLayers` ×3) |
| custo | um por jogador por quadro |
| reentrância | nenhuma |
| contornos | `DrawPlayer_30_BladedGlove` (`0x15C65D8`), outra camada do mesmo desenhador, lê `TextureAssets.Item` sem hook: um item de mod com casca de luva com lâmina aparece com a textura da casca |
| confiança | confirmado no binário e no código |

#### `Main.DrawNPCDirect`, 6 argumentos (`0xBFC154`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_MainDrawNPCDirect`, `draw_hooks.cpp:517` |
| threads | **P** (`Main.DrawNPC`, chamada final em `0xCD44C8`) |
| custo | um por NPC desenhado (até 200 por quadro). Caminho rápido: `npc_texture` com índice por `whoAmI` (um `il2cpp_field_static_get_value`) e `std::function`; 266 instruções no total |
| reentrância | os `DrawNPCDirect_*` (Deerclops, Fada, Imperatriz…) são chamados de dentro dele, já com a troca feita |
| contornos | **a segunda cópia**, `DrawNPCDirect(SpriteBatch, NPC, bool, Vector2)` (`0xBCB734`, 0x30A20 bytes, 273 leituras de `TextureAssets.Npc`). É o corpo inteiro duplicado, chamado só pelo ícone do bestiário (`UnlockableNPCEntryIcon.Draw`) e pelos créditos (`NPCSegment.Draw`). Também ficam de fora o retrato na conversa (`GUINPCDialogue.DrawNPC`, `0x9B6148`) e o ícone de interação (`GUIQuickActionButton.GetMouseNPC`) |
| confiança | confirmado no binário e no código |

### 2.6 Telas

Dez dos treze usam a mesma troca da 2.5 e herdam o [problema 3](#problema-3); `CanBeResearched`,
`GetLinesInfo` e o catálogo da pesquisa não trocam textura. Todos rodam só na P.

#### `GUITransactionButton.Draw(Item)` (`0x12220F8`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_GUITransactionButtonDraw`, `screen_hooks.cpp:368` |
| threads | **P**: 244 chamadas em 90 métodos (loja, pesquisa, botões com item); `DrawWithBanner(..., Item, ...)` (`0x1221F30`) também chega nele (`0x1222008`, `0x12220C8`) |
| custo | um por botão por quadro; ~40 instruções sem item de mod |
| reentrância | o botão Mods do menu chama este método direto pelo `methodPointer` (`menu_hooks.cpp:263`): passa pelo hook com item nulo, sem efeito |
| contornos | as sobrecargas que recebem `Texture2D` pronta (`0x1223F04`, `0x1223F70`, `0x12242AC`, `DrawWithBanner` com textura) dependem de quem leu a textura; essas leituras são o assunto dos hooks de tela |
| confiança | confirmado no binário e no código |

#### `GUIReforgePopup.DrawItem` (`0x9D2984`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:382` |
| threads | **P** (`GUIReforgePopup.Draw`) |
| custo | com a reforja aberta: um `runtime_invoke` de `GUIReforge.get_ReforgeItem` por quadro |
| reentrância | nenhuma |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

#### `GUIResearch.DrawResearchItem` (`0x9D7164`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:391` |
| threads | **P** (`GUIResearch.DrawResearchBacking`) |
| custo | com a pesquisa aberta: um `runtime_invoke` de `GetResearchItem` por quadro |
| reentrância | nenhuma |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

#### `GUIResearch.CanBeResearched` (`0x9D4C10`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:403` (não troca textura) |
| threads | **P**: `GUIInventory.DrawItem`, `GUIResearch.DrawInventoryResearchBinding` e `DrawInventoryResearchButton`, `GUIResearchPage.RefreshItems` e `DrawResearch` |
| o que faz | item de mod → "não pode ser pesquisado" |
| custo | na Jornada, um por slot do inventário desenhado; 35 instruções |
| reentrância | nenhuma |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

#### `Main.MouseText_DrawItemTooltip_GetLinesInfo` (`0xCC90D8`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:418` (não troca textura) |
| threads | **P**: `MouseText_DrawItemTooltip`, `GUICraftGuidePopup.UpdateText` e `Creative.ItemFilters.BySearch.FitsFilter` |
| o que faz | liga `t_modItemTooltip` (`thread_local`) durante o tooltip de um item de mod |
| custo | um por quadro com tooltip; na busca da Jornada ou da criação, um por item filtrado. 57 instruções |
| reentrância | guarda o valor anterior e o devolve. Se o original lançar, o valor fica até o próximo tooltip, que o regrava |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

#### `CreativeItemSacrificesCatalog.TryGetSacrificeCountCapToUnlockInfiniteItems` (`0x8F4290`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_SacrificeCountCap`, `screen_hooks.cpp:432` |
| threads | **P**: 7 chamadores (`CreativeUI.ShouldDrawSacrificeArea`, `ItemsSacrificedUnlocksTracker.TryGetSacrificeNumbers`, `MiscFallback..ctor`, `GUIResearch` ×3 e o tooltip). Em tese G e C, por `Recipe.FindRecipes` → filtros da criação |
| o que faz | "sem limite" enquanto `t_modItemTooltip` estiver ligado na mesma thread |
| custo | 37 instruções; `MiscFallback..ctor` o chama uma vez por tipo de item ao montar o filtro |
| reentrância | nenhuma |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

#### `GUICraftGuidePopup.DrawItem` (`0x15A5384`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:442` |
| threads | **P** (`GUICraftGuidePopup.Draw`) |
| custo | com a guia aberta: leitura da receita em foco ou um `runtime_invoke` de `get_guideItem` por quadro |
| reentrância | nenhuma |
| contornos | `GUICraftGuidePopup.MaterialItemDraw` (`0x15A6174`) e `GUIRubbleMaker.MaterialItemDraw` (`0x9DB3C4`) são de outras classes e não leem `TextureAssets.Item`: não contornam a troca |
| confiança | confirmado no binário e no código |

#### `GUICrafting.MaterialItemDraw` (`0x73AD78`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:451` |
| threads | **P**; sem chamada direta: é chamado pela grade de itens por delegate (inferido) |
| custo | com a criação aberta, por ingrediente por quadro: dois `runtime_invoke` (`Main.get_availableRecipe` e `get_focusRecipe`, este com retorno encaixotado) |
| reentrância | nenhuma |
| contornos | nenhum |
| confiança | confirmado no código; chamador inferido |

#### `GUICrafting.MaterialItemTooltipDraw` (`0x73A3AC`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:461` |
| threads | **P**; delegate, como o anterior |
| custo | por ingrediente do tooltip; só leitura de campos |
| reentrância | nenhuma |
| contornos | nenhum |
| confiança | confirmado no código; chamador inferido |

#### `GUIVirtualInputController.DrawControls` (`0x9EF6D0`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:472` |
| threads | **P** (`GUIVirtualInputController.Draw`, `0x9EF654`) |
| o que faz | troca o item na mão e o gancho; depois desenha os botões de mods (`DrawHudButtons(false)`, `screen_hooks.cpp:479`) |
| custo | 60/s no jogo. Com texturas de mod: dois `runtime_invoke` de `get_myPlayer` (retorno encaixotado) e um de `Player.QuickGrapple_GetItemToUse`, que percorre o inventário. Os botões copiam o retrato do painel (vetor e strings) sob `hudMutex_` (`panel.h:248`, trava curta) |
| reentrância | os botões chamam `GUITransactionButton.Draw` com `Texture2D` (sobrecarga sem hook) e `Main.LoadItem` |
| contornos | nenhum; ver o [problema 4](#problema-4) (desenho duplo) |
| confiança | confirmado no binário e no código |

#### `GUIQuickActionButton.Draw(QuickActionControl)` (`0x10604EC`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:482` |
| threads | **P**: `GUIVirtualInputController.DrawControls` ×3, `GUIVirtualInputController.Draw` e `DrawMapControls`, `GUIMap.DrawMap` |
| custo | só os botões do gancho fazem o `runtime_invoke` do gancho |
| reentrância | nenhuma |
| contornos | a outra `Draw` (`0x105DB68`) só desenha um `GUIButton`; `GetIconFrame` e `CanInteract` leem ícones fixos ou do tile (documentado no README) |
| confiança | confirmado no binário e no código |

#### `Main.DrawInterface_40_InteractItemIcon` (`0xD0F8B8`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:494` |
| threads | **P** (`DrawInterface_WorldMouseOver`, `0xD1C370`, uma camada da interface) |
| o que faz | troca o item na mão e a munição; depois desenha os botões de mods de novo (`screen_hooks.cpp:504`) |
| custo | a cada quadro com cursor de mundo: `runtime_invoke` de `get_myPlayer` e de `Main.TryGetAmmo` (os dois encaixotados) e os botões |
| reentrância | nenhuma |
| contornos | nenhum; ver o [problema 4](#problema-4) |
| confiança | confirmado no binário e no código |

#### `ItemTagHandler.ItemSnippet.UniqueDraw` (`0x97F0D8`)

| campo | valor |
|---|---|
| handler | `screen_hooks.cpp:507` |
| threads | **P**; método virtual (sem chamada direta) do texto do chat |
| custo | por `[i:ID]` visível |
| reentrância | nenhuma |
| contornos | `ItemTagHandler.PrintInline` (`0x97E144`) lê `TextureAssets.Item` para medir (documentado no README) |
| confiança | confirmado no código; chamador inferido |

### 2.7 Menu

#### `GUIMainMenu.Draw` (`0xA4397C`)

| campo | valor |
|---|---|
| handler | `TLMod_Hooked_GUIMainMenuDraw`, `menu_hooks.cpp:333` |
| threads | **P** (`Main.DrawMenu`) |
| o que faz | com a Oficina aberta, a desenha no lugar do menu e não chama o original; senão chama o original, desenha o botão Mods e os botões de mods do menu |
| riscos | nenhum na P; o clique chama o runtime (`OnMenuModsButton`), que só abre a Oficina |
| reentrância | o botão Mods passa pelo hook de `GUITransactionButton.Draw(Item)` |
| custo | 60/s no menu; leitura de alguns estáticos e um desenho de botão |
| contornos | nenhum |
| confiança | confirmado no binário e no código |

### 2.8 Configurações

Os 12 hooks da aba Mods (`settings_tab.cpp:733`–`836`) rodam só na **P**, dentro do desenho das
Configurações. Todos seguem o mesmo padrão:

- nas linhas da aba Mods, devolvem o texto ou o valor guardado (`RowIndex`, `RowOf`);
- nas linhas do jogo, chamam o original.

Nenhum faz alocação por quadro: as strings são criadas uma vez e presas por GC handle. Nenhum tem
contorno: cada um tem um único chamador ou é chamado pelo desenho da página. Confirmado no binário
(chamadores) e no código.

#### `GUISettingsOverlay.SetCategory` (`0xA67B94`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:733` |
| threads | **P**: 20 chamadas em 6 métodos (`DrawCategories` ×11, `GUISettings.Draw` ×5, `GUISettingsPauseMenu.Draw`, `GUIMainMenu.Draw`…) |
| o que faz | depois do original, preenche as entradas da aba Mods |
| custo | ao trocar de aba |

#### `GUISettingsOverlay.DrawCategories` (`0xA68C68`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:742` |
| threads | **P** (`GUISettingsOverlay.Draw`) |
| o que faz | desenha a aba Mods depois das do jogo; atende o pedido de fechar |
| custo | por quadro com as Configurações abertas |

#### `GUISettingsOverlay.DrawTitle` (`0xA699E4`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:754` |
| threads | **P** (`GUISettingsOverlay.Draw`) |
| o que faz | título da aba Mods |
| custo | por quadro com as Configurações abertas |

#### `GUISettingsOverlay.ResetPageSettings` (`0xA6B3C8`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:761` |
| threads | **P** (`GUISettingsOverlay.DrawMainButtons`) |
| o que faz | "Padrão" na aba Mods chama o `reset` do runtime |
| custo | ao tocar no botão |

#### `GUIOpenUIButtonSetting.GetOption` (`0xA8BF14`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:773` |
| threads | **P** (`GUIOpenUIButtonSetting.Draw`) |
| o que faz | texto da linha |
| custo | por linha por quadro; busca da linha e leitura de handle |

#### `GUIOpenUIButtonSetting.GetButtonCaption` (`0xA8C2B4`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:782` |
| threads | **P** (`GUIOpenUIButtonSetting.Draw`) |
| o que faz | texto do botão |
| custo | por linha por quadro |

#### `GUIOpenUIButtonSetting.OpenUI` (`0xA8C434`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:791` |
| threads | **P** (`GUIOpenUIButtonSetting.Draw`) |
| o que faz | toque numa linha da aba Mods vai para o runtime |
| custo | ao tocar |

#### `GUISliderSetting.GetTitle` (`0xA93E18`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:799` |
| threads | **P** (`GUISliderSetting.Draw`) |
| o que faz | título da barra |
| custo | por barra por quadro |

#### `GUISliderSetting.GetValue` (`0xA9395C`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:808` |
| threads | **P** (`GUISliderSetting.Draw`) |
| o que faz | posição da barra (0 a 1) |
| custo | por barra por quadro |

#### `GUISliderSetting.GetMaxValue` (`0xA94220`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:817` |
| threads | **P** (`GUISliderSetting.Draw`, 2 chamadas) |
| o que faz | número de passos |
| custo | por barra por quadro |

#### `GUISliderSetting.SetValue` (`0xA942E8`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:826` |
| threads | **P** (`GUISliderSetting.Draw`) |
| o que faz | arrasto da barra vai para o runtime |
| custo | ao arrastar |

#### `GUISliderSetting.GetStringValue` (`0xA948E4`)

| campo | valor |
|---|---|
| handler | `settings_tab.cpp:836` |
| threads | **P** (`GUISliderSetting.Draw`) |
| o que faz | texto do valor |
| custo | por barra por quadro |

---

## 3. Respostas diretas

### Pergunta 1. Quais threads chamam cada método hookado?

- **Só a principal (P): 34 hooks**: desenho, telas, menu, Configurações, `NPC.AI`, `Main.DoUpdate` e
  `Recipe.SetupRecipes`.
- **Também outras threads do jogo: 12 hooks** ([tabela 1.4](#14-os-12-hooks-que-rodam-fora-da-p)):
  - `Item.SetDefaults` e `RebuildTooltip`: geração, carga, saída do multiplayer e, pelo TLMod, saves;
  - `NPC.SetDefaults` e `NewNPC`: geração e carga;
  - saves e loads: thread de save, de rede (save ao cair), de geração, de carga e de saída;
  - nomes de NPC, `Player.Hurt` e o catálogo da pesquisa: fora da P só em tese.
- **ThreadPool**: nenhum. O único uso do jogo, `WorldGen.RenameWorld`, não tem chamador.
- **Thread de save**: as threads do `ThreadManager` (S, R), não o ThreadPool. O comentário em
  `bridge.h:365` está impreciso.
- **Thread de rede**: só a `TcpClientLoop`, e só para o save ao cair. A recepção de pacotes roda na P
  (auditoria 01), e as threads do Telepathy só enfileiram.
- **Threads de render e Java**: nenhum hook.

### Pergunta 2. Há operação insegura para a thread?

- **API do Unity fora da thread principal: não.** Texturas só no `DoUpdate`; desenho, som e botões só
  nos hooks de desenho, todos na P. Os hooks que rodam fora da P fazem só I/O em C++ e código gerenciado
  sem Unity (`SetDefaults`, `LocalizedText`, `NetworkText`).
- **Campo `[ThreadStatic]`: lido certo.** `LocalUserGameState.Instance` é lido por
  `il2cpp_field_static_get_value` (resolve por thread) ou pelos getters do jogo, e toda thread do jogo
  que chega a hook chama `LoadThreadstate` antes. O risco está fora dos hooks: as funções da API de mods
  que leem esse estado, chamadas de outra thread ([problema 1](#problema-1)).
- **Alocação gerenciada sem thread anexada: não nos hooks.** Os hooks rodam em threads do jogo, já
  anexadas. As threads do TLMod:
  - a de init se anexa e se desanexa (`bridge.cpp:260`, `:290`);
  - a do runtime se anexa (`android_main.cpp:204`) e termina sem desanexar ([problema 8](#problema-8));
  - a de UI do Android não é anexada e roda os comandos e provedores dos mods (`overlay_jni.cpp:107`).
    Os mods do repositório só enfileiram ali; nada impede um mod de chamar o jogo
    ([problema 1](#problema-1)).

### Pergunta 3. Há reentrância?

Sim. Todos os casos encontrados estão tratados:

- **`SetDefaults` dentro de `SetDefaults`.** Não pelo TLMod. O jogo chama `RebuildTooltip`
  (hookado) de dentro do `SetDefaults`, com o `netID` ainda da casca, e `NPC.SetDefaults` volta a si
  mesmo por `SetDefaultsFromNetId`. Os dois handlers aguentam.
- **`SetDefaults` e `NewItem` dentro de hooks do TLMod.** São chamados por `runtime_invoke`, e passam
  pelos hooks, a partir de:
  - save e load (devolver ou reinserir itens);
  - receitas;
  - frame: `give_item` → `GiveItem` ou `DropItemNearPlayer` → `Item.NewItem`; `kill` → loot →
    `NewItem`; spawn → `NewNPC` → `NPC.SetDefaults`.

  Nenhum lock do TLMod fica preso de um jeito que o hook interno tente tomar de novo.
- **Hook que dispara outro hook:**
  - `NewNPC` → `NPC.SetDefaults` e `GetTypeNetName`;
  - `SetDefaults` → `RebuildTooltip`;
  - `GUIInventory.DrawItem` → `ItemSlot.DrawItemIcon` (troca aninhada, desfeita na ordem certa);
  - `GUIMainMenu.Draw` → `GUITransactionButton.Draw(Item)`.
- **Save:** o original roda por `runtime_invoke` e volta pelo hook; `t_inPlayerSave`/`t_inWorldSave`
  repassam.
- **`Player.Hurt` chama `Hurt`:** `t_hurtDepth` pula o callback aninhado.

O ponto fraco é a saída por exceção: `t_hurtDepth` e as trocas de textura não voltam ao estado anterior
([problema 3](#problema-3)).

### Pergunta 4. Quais hooks estão em caminhos quentes e quanto custam?

| hook | frequência | custo do handler (caminho rápido) |
|---|---|---|
| `NPC.AI` | até 200 × 60 = 12 000/s | 121 instruções + `il2cpp_field_static_get_value` + `std::function`: ~50 a 100 ns; ~1 ms por segundo |
| `Main.DrawItem` | até 400 × 60/s | ~40 instruções |
| `Main.DrawNPCDirect` | até 200 × 60/s | índice + `std::function`: ~50 ns |
| `GUIInventory.DrawItem`, `ItemSlot.DrawItemIcon`, `GUIHotbar.DrawHotbarItem`, `GUITransactionButton.Draw(Item)` | um por slot ou botão desenhado | ~40 instruções cada |
| `GUIResearch.CanBeResearched` | um por slot na Jornada | 35 instruções |
| `Item.SetDefaults` + `RebuildTooltip` | rajadas (início, carga de mundo, criação) | dezenas de ns, contra µs do original |
| `Main.MouseText_DrawItemTooltip_GetLinesInfo` | um por quadro com tooltip; um por item na busca | 57 instruções |
| `GUIVirtualInputController.DrawControls` | 60/s | 3 `runtime_invoke` (2 encaixotados) + botões de mods: alguns µs |
| `Main.DrawInterface_40_InteractItemIcon` | 60/s com cursor | 2 `runtime_invoke` encaixotados + botões de mods |
| `GUICrafting.MaterialItemDraw` | por ingrediente, com a criação aberta | 2 `runtime_invoke` (1 encaixotado) |
| `Main.DoUpdate` | 60/s | alguns µs; no início, até 512 `runtime_invoke` por quadro durante ~24 quadros |
| `NPC.get_TypeName` | hover, barra de vida, mapa; rajada na ordenação do bestiário | 29 instruções + índice |

O desvio do ShadowHook em modo multi é um salto para o proxy, sem hub (`hook_shadowhook.cpp:177`).
Somado, o custo fixo é uma fração pequena do quadro de 16,7 ms (inferido). O que destoa:

- **alocações por quadro:** de 3 a 6 retornos encaixotados por quadro no jogo;
- **botões de mods desenhados duas vezes** ([problema 4](#problema-4)).

Medida no aparelho: [E3](#e3-custo-dos-hooks-quentes).

### Pergunta 5. Sobrecargas ou cópias contornam algum hook?

- **`Main.DrawNPCDirect`: sim.** A sobrecarga de 4 argumentos (`0xBCB734`) é uma cópia completa do
  desenho de NPC, de 0x30A20 bytes. Usam essa cópia só o bestiário (`UnlockableNPCEntryIcon.Draw`) e os
  créditos (`NPCSegment.Draw`); o mundo usa a hookada, por `Main.DrawNPC`. Hoje não há efeito, porque
  a textura de mod é escolhida pelo slot de `Main.npc`. O retrato na conversa (`GUINPCDialogue.DrawNPC`)
  também lê a textura sem hook.
- **`Player.Hurt`: sim, em comportamento.** `KillMe` é chamado direto por afogamento, dano contínuo,
  língua da Parede de Carne, borda do mundo, Vara do Teletransporte e rede.
- **`NPC.NewNPC`: NPCs da rede (cliente) e do arquivo do mundo não passam por ele.** Já documentado.
- **Nenhum contorno** nos demais:
  - `Item.SetDefaults`: três entradas, uma só implementação;
  - `RebuildTooltip`: único caminho para `Lang.GetTooltip`;
  - `NPC.SetDefaults`: variantes passam por ele;
  - `LoadPlayer` e `LoadWorld`: formatos antigos dentro deles;
  - `InternalSave*`: só por `SavePlayer`/`SaveWorld`;
  - `NPC.AI`, `DoUpdate`, `SetupRecipes`: um chamador real cada;
  - `DrawWithBanner(Item)`: chama `Draw(Item)`;
  - `NPC.TypeName`: sem cópia inline.
- **Leituras de textura fora dos hooks.** A varredura de `TextureAssets.Item` (estático `+0x560`) acha
  97 leituras em 44 métodos (o README conta 112 em 49, com outro critério). As que ficam fora dos hooks
  estão no README (`modapi/README.md`, "Telas"), mais `DrawPlayer_30_BladedGlove`.

---

## 4. Problemas, em ordem de gravidade

### Problema 1

**[alta] A API de mods deixa um mod mexer no jogo fora da thread do jogo.**

- **Funções sem conferência de thread.** Sete funções vão direto ao jogo na thread de quem chama:
  - `spawn_npc` (`mod_loader.cpp:813`);
  - `heal_player` (`:712`) e `kill_hostile_npcs` (`:714`);
  - `set_time` com salto (`:707`) e `get_time` (`:698`);
  - `local_player` (`:513`) e `player_life` (`:517`).

  A ponte tem o teste `bridge::OnGameThread` (`world_state.cpp:57`), mas só `TLMod_ReadItemTraits` o
  usa. `TLMod_SpawnNpc` e `TLMod_KillHostileNpcs` (`npc_hooks.cpp:209`, `:227`) não conferem nada.
- **Em quais threads um mod roda.**
  - Comandos e provedores rodam na thread de UI do Android (`overlay_jni.cpp:107`, `:116` →
    `PanelController::ModCommand`, `panel.cpp:604` → `ModLoader::Command`). Essa thread não está
    anexada ao IL2CPP.
  - O `init` roda na thread do runtime, anexada, mas sem o estado `[ThreadStatic]`.
- **Consequência.** `runtime_invoke` de `NPC.NewNPC`, `StrikeNPCNoInteraction` ou `Main.SkipToTime`
  numa thread não anexada e em corrida com a P. `get_myPlayer` lança `NullReferenceException` fora das
  threads do jogo; ela é capturada e a função devolve -1.
- **Contrato e uso.** O contrato está escrito (`tlmod_api.h:289` e `:343`), mas não é cobrado. Os mods do
  repositório o seguem (o Ferramentas enfileira tudo para o `OnFrame`). O risco vem de mods de terceiros.
- **Confiança.** Confirmado no código. Se o resultado é queda ou erro, só o aparelho confirma.

### Problema 2

**[média-alta] Uma exceção C++ de mod nos callbacks de NPC e de dano derruba o jogo.**

- **Quem tem proteção.** Item, desenho, save e frame têm `try/catch` no adaptador
  (`bridge_adapter.cpp:150`, `:178`, `:227`, `:379`).
- **Quem não tem.** NPC (`NpcRemap`, `NpcSpawned`, `NpcSetDefaults`, `NpcAi`,
  `bridge_adapter.cpp:41`–`55`) e dano (`OnBridgeHurt`, `:21`, e `DispatchHurt`, `mod_loader.cpp:130`).
  Os trampolins do `tlmod.hpp` (`:270`–`275`) também não.
- **Consequência.** Uma exceção no `OnAI`, `OnSpawn` ou no handler de dano de um mod em C++ sobe por
  `NPC.UpdateNPC` ou `Player.Update`. Esses métodos só capturam exceção gerenciada, e o processo cai.
- **Confiança.** Confirmado no código; a queda é inferida.

### Problema 3

**[média] Exceção no meio de um desenho deixa a troca presa.**

- **Onde.** Os 16 handlers com troca de textura (6 de desenho e 10 de tela) fazem trocar → original →
  destrocar sem limpeza no desenrolar. É uma escolha consciente ("Só PODs nos frames",
  `draw_hooks.cpp:462`), por causa das duas cópias do libc++abi (`save_hooks.cpp:209`–`214`).
- **O que fica com os valores do mod, até o fim da sessão.**
  - Item: `TextureAssets.Item[casca]` e `Main.itemAnimations[casca]`.
  - NPC: `TextureAssets.Npc[casca]`, `Main.npcFrameCount[casca]` e `npc.frame`.
- **Efeito.** Todo item ou NPC original daquela casca passa a ser desenhado com o PNG do mod.
- **O mesmo problema em outros pontos.**
  - `t_hurtDepth` fica em 1, e os callbacks de dano param na P (o "Invencível" deixa de funcionar).
  - `t_modItemTooltip`, em menor grau: o próximo tooltip o corrige.
- **Confiança.** Confirmado no código. Quantas vezes o desenho original lança, só o aparelho confirma.

### Problema 4

**[baixa-média] Os botões de mods provavelmente são desenhados duas vezes por quadro.**

- **Onde.** `DrawHudButtons(false)` é chamado nos hooks de `DrawControls` (`screen_hooks.cpp:479`) e de
  `DrawInterface_40_InteractItemIcon` (`:504`), sem trava por quadro (`hud_buttons.cpp:243`, `:410`).
  Os dois métodos rodam a cada quadro num mundo: o segundo quando há cursor de mundo.
- **Efeito.**
  - O custo dobra.
  - A opacidade escolhida se acumula: o botão fica mais opaco que o configurado.
  - O toque não duplica, porque o primeiro consome `mouseLeftRelease` e há a borda `btn.clicked`.
- **Confiança.** Confirmado no código (dois chamadores). Que os dois rodem no mesmo quadro, só o
  aparelho confirma ([E2](#e2-botões-de-mods-por-quadro)).

### Problema 5

**[baixa] Hooks de NPC fora da P mexem em `npcSlots` sem trava.**

- **Onde.** Na G, na C e na H, `OnNpcSetDefaults` e `OnNpcSpawned` escrevem `npcSlots` enquanto a P
  pode ler (desenho, AI).
- **Quando importa.**
  - Na G e na C a P está na tela de carregamento.
  - Na H o jogo anda, e a própria H mexe em `Main.npc`.
- **Efeito máximo.** Um quadro com a textura ou o estado errado.
- **Confiança.** Inferido.

### Problema 6

**[baixa] Alocações e `runtime_invoke` a cada quadro.**

- **Onde.** `get_myPlayer` em `DrawControls` (×2), `DrawInterface_40` e `ProcessNearSpawns`;
  `TryGetAmmo`; `get_focusRecipe` por ingrediente; `QuickGrapple_GetItemToUse`.
- **Efeito.** De 3 a 6 objetos encaixotados por quadro no jogo, alguns µs e pressão pequena no GC. O
  retrato do HUD também copia vetor e strings a cada quadro.
- **Confiança.** Confirmado no código; o custo é inferido.

### Problema 7

**[baixa] Caminhos que contornam hooks.**

- `KillMe` sem `Hurt`: o "Invencível" não segura afogamento nem dano contínuo.
- `DrawNPCDirect` de 4 argumentos e o retrato na conversa: textura da casca.
- `DrawPlayer_30_BladedGlove`: textura da casca.
- Mensagens de estandarte com o nome da casca.

Nenhum quebra o jogo. Confirmado no binário.

### Problema 8

**[baixa] A thread do runtime termina anexada ao IL2CPP.**

- **Onde.** `TLMod_AttachCurrentThread()` (`android_main.cpp:204`) e o `return` no fim do `RuntimeMain`
  (`:261`), sem `il2cpp_thread_detach`.
- **Efeito.** No log, o jogo seguiu normal depois disso: a sessão continuou por mais de 27 s (de 17,56 a
  44,21). O contrato do IL2CPP pede desanexar antes de sair.
- **Confiança.** Confirmado no código; o efeito, só o aparelho confirma.

### Problema 9

**[informativo] Textos imprecisos.**

- `bridge.h:365` diz que os saves rodam no ThreadPool: são threads do `ThreadManager`.
- A auditoria 01 (seção 5.1) lista `WorldGen.RenameWorld` como caminho ativo: é código morto.

---

## 5. Experimentos no aparelho

Todos usam uma build de teste da ponte, salvo o E4, que usa as Ferramentas como estão. O resultado sai no
`tlmod.log` (painel → Log).

### E1: threads de cada hook

1. Em cada handler, registrar a primeira chamada de cada par (hook, thread), com `gettid()` e o nome da
   thread (`pthread_getname_np`).
2. Roteiro:
   - criar um mundo (G);
   - entrar nele (C);
   - morrer, para o save da morte (S);
   - "Salvar e sair" (S);
   - entrar como cliente num jogo local e sair (R, Q);
   - matar a Parede de Carne (H).
3. **Esperado:** a [tabela 1.4](#14-os-12-hooks-que-rodam-fora-da-p). Nenhuma linha do ThreadPool, de
   render ou do Telepathy. Os 34 hooks da P só com o tid da P.

### E2: botões de mods por quadro

1. Contar as chamadas de `DrawHudButtons(false)` por quadro (zerar no `DoUpdate`) e registrar o máximo a
   cada segundo.
2. **Esperado:** 2 num mundo, com os controles de toque e o cursor de mundo ativos; 1 no menu. Com 2, o
   [problema 4](#problema-4) se confirma.

### E3: custo dos hooks quentes

1. Somar `clock_gettime(CLOCK_MONOTONIC)` em volta do handler, sem o original, para:
   - `NPC.AI`, `Main.DrawItem`, `DrawNPCDirect`;
   - `Item.SetDefaults`;
   - `DrawControls`, `DrawInterface_40` e `DoUpdate`.

   Registrar contagem e tempo total a cada 10 s. Outra opção: `simpleperf record` no processo do jogo.
2. Medir com 200 NPCs (evento) e ao carregar um mundo grande.
3. **Esperado:** abaixo de 1% do tempo do quadro em regime. As rajadas do `SetDefaults` na carga de
   mundo devem pesar menos que o corpo do próprio `SetDefaults`.

### E4: "Invencível" contra o que não passa por `Hurt`

1. Ferramentas → Jogador → Invencível ligado.
2. Ficar debaixo d'água até acabar o ar, ou pegar fogo e veneno.
3. **Esperado:** o golpe de inimigo não tira vida, mas o afogamento e o dano contínuo matam. É
   `KillMe` sem `Hurt`, [problema 7](#problema-7). Este experimento usa a build atual, sem mudança.

---

## 6. Ações recomendadas

1. **Cobrar a thread na API** ([problema 1](#problema-1)). Duas opções:
   - exportar `TLMod_OnGameThread()` da ponte e, no runtime, recusar com log fora da P, em `spawn_npc`,
     `heal_player`, `kill_hostile_npcs`, `set_time`, `get_time`, `local_player` e `player_life`;
   - ou enfileirar para o frame, como `give_item` já faz.

   Pôr a mesma conferência em `TLMod_SpawnNpc`, `TLMod_KillHostileNpcs`, `TLMod_SetWorldTime` e
   `TLMod_CreateTexture`.
2. **`try/catch` nos callbacks de NPC e de dano** ([problema 2](#problema-2)). No adaptador
   (`NpcRemap`, `NpcSpawned`, `NpcSetDefaults`, `NpcAi`, `OnBridgeHurt`), como já é feito para item.
   Nos trampolins do `tlmod.hpp`, `catch` e log.
3. **Troca de textura que se desfaz sozinha** ([problema 3](#problema-3)), sem destrutor no frame do
   hook:
   - registrar cada troca ativa numa pilha;
   - no começo de cada `DoUpdate`, desfazer o que tiver sobrado e avisar no log;
   - zerar `t_hurtDepth` ali também, porque fora de um `Hurt` ele deve valer 0.
4. **Desenhar os botões de mods uma vez por quadro** ([problema 4](#problema-4)): número do quadro
   gravado no `DoUpdate`, ou uma só das duas chamadas.
5. **Ler o jogador local uma vez por quadro**, no `DoUpdate`: `myPlayer` e `selectedItem` num cache, e o
   mesmo para `focusRecipe` com a criação aberta ([problema 6](#problema-6)).
6. **`il2cpp_thread_detach` no fim do `RuntimeMain`** ([problema 8](#problema-8)).
7. **Corrigir o texto** de `bridge.h:365` e anotar na auditoria 01 que o ThreadPool não é usado
   ([problema 9](#problema-9)).
8. **Decisão de produto**, depois do [E4](#e4-invencível-contra-o-que-não-passa-por-hurt): se o
   "Invencível" deve cobrir afogamento e dano contínuo, isso pede `KillMe` ou ajuste de `breath` e
   `lifeRegen` no frame.
