# Auditoria 01: persistência de IDs (disco, nuvem e rede)

Terraria Mobile 1.4.4.9 (Unity 2021.3.56f2, IL2CPP, Android arm64), comparado com o TLMod no commit
`f917b04` da branch `ccr-bbd92bf9-i01buk`.

Auditoria só de leitura. As fontes são o dump regenerado (`dump.cs`, `script.json`, `il2cpp.h`) e a
desmontagem da `libil2cpp.so`, que ficam fora do repositório. Nenhum código foi alterado; este
arquivo é o único acréscimo.

**Módulo 1:** tudo que grava IDs de item, NPC ou buff em disco, na nuvem ou na rede.

- [Como ler](#como-ler)
- [Resumo](#resumo)
- [1. Tabela de todos os caminhos](#1-tabela-de-todos-os-caminhos)
- [2. Itens: evidências](#2-itens-evidências)
- [3. NPCs](#3-npcs)
- [4. Buffs, pensando no `register_buff`](#4-buffs-pensando-no-register_buff)
- [5. Threads](#5-threads)
- [6. O que o TLMod já faz](#6-o-que-o-tlmod-já-faz)
- [7. Experimentos no aparelho](#7-experimentos-no-aparelho)
- [8. Respostas diretas](#8-respostas-diretas)
- [9. Lacunas do TLMod, em ordem de risco](#9-lacunas-do-tlmod-em-ordem-de-risco)
- [10. Ações recomendadas](#10-ações-recomendadas)

---

## Como ler

**Níveis de confiança.**

- **confirmado no binário**: visto na desmontagem (uma instrução, uma chamada ou a tabela de exceções).
- **inferido**: decorre do código lido, mas um passo não foi rastreado (uma chamada virtual, o efeito
  na tela, a ordem no tempo).
- **só o aparelho confirma**: depende do tempo de execução, da interface ou do que o Unity faz com uma
  exceção. Cada caso tem um experimento na [seção 7](#7-experimentos-no-aparelho).

**Endereços.** Todos são RVAs da `libil2cpp.so`, os mesmos do `script.json` e de
`il2cpp_bridge/include/terraria_offsets.h`. Nos trechos de desmontagem:

- `this` chega em `x0`, e o `MethodInfo*` é o último argumento.
- Um array guarda o tamanho em `+0x18` e os elementos a partir de `+0x20`.
- Um campo estático fica em `[klass+0xB8]` mais o deslocamento do campo.
- `BinaryWriter` e `BinaryReader` são chamados pela vtable. Slots que aparecem aqui:
  - escrita: `Write(short)` 18 (`+0x258`), `Write(ushort)` 19, `Write(int)` 20 (`+0x278`);
  - leitura: `ReadInt16` 13 (`+0x208`), `ReadUInt16` 14, `ReadInt32` 15 (`+0x228`).
- Ajudantes do runtime:
  - `bl 0x642c40` lança `IndexOutOfRangeException`;
  - `bl 0x642c38` lança `NullReferenceException`;
  - `bl 0x642c30` relança a exceção atual.
- Os try/catch vêm da tabela de exceções (`.eh_frame_hdr` → FDE → LSDA no `.gcc_except_table`).

**Constantes.**

- `ItemID.Count = 5456` (`0x1550`), `NPCID.Count = 688` (`0x2B0`), `BuffID.Count = 355` (`0x163`).
- No `Item`: `type` em `+0xBC` (é o índice de todas as tabelas), `stack` em `+0xCC`, `netID` em
  `+0x198`.

**Fora do escopo** (só registrado, não analisado):

- a cifra Rijndael do `.plr` (`RijndaelManaged` criado em `0x10442AC`, dentro de
  `Player.InternalSavePlayerFile`);
- a cifra do arquivo de conquistas (`AchievementManager.Save`, `0x15F9650`);
- a validação do mundo antes de fazer o backup (`WorldFile.ValidateWorld`, chamada em `0x14F2000`).

### O que é um "ID de mod" hoje

- **Item: casca.** `type` é um item original (abaixo de 5456) e `netID` é o ID de mod (8192 a 32767).
  - O hook em `Item.SetDefaults` (`0xBAD034`) troca o ID de mod pela casca antes de chamar o original
    (`RemapItemType`, `modapi/src/mod_loader.cpp:157`).
  - Um `netDefaults(ID de mod)` remonta o item de mod se o ID estiver registrado. Se não estiver, o
    original recebe o número e devolve ar.
- **NPC: casca também.** O `NewNPC` recebe a casca. O ID de mod (4096 ou mais) fica só na tabela de
  slots do TLMod, na memória (`mod_loader.cpp:142`); o jogo nunca o vê.
- **Buff: ainda não há `register_buff`.** A faixa 4096–65535 está reservada
  (`modapi/src/content_registry.cpp:30`).
- **Estabilidade dos números.**
  - Os IDs são dados em sequência, na ordem de carga (`content_registry.cpp:74`).
  - A ordem é a mesma para o mesmo conjunto de mods (`mod_loader.cpp:1045`).
  - Ela muda quando um mod entra, sai ou ganha conteúdo. O README já avisa: "o ID numérico pode mudar
    quando o conjunto de mods muda" (`modapi/README.md:72`).

Por isso há dois modos de falha diferentes:

- **(A) Número de mod no arquivo ou no pacote.** O jogo grava sem reclamar. Quem carrega sem o TLMod,
  sem o mod ou com outra numeração recebe ar ou outro item.
- **(B) Índice ≥ Count numa tabela do jogo.** Lança `IndexOutOfRangeException`.
  - Com a casca, (B) não acontece com itens.
  - Com buffs, aconteceria se um `register_buff` pusesse o ID de mod em `Player.buffType`.

---

## Resumo

1. **`.plr` (itens).** Todo contêiner de item vira ar, sem aviso, ao carregar um ID ≥ 5456.
   - Alguns contêineres têm guarda explícita; nos outros, quem zera é o `SetDefaults`.
   - O TLMod tira os itens de mod antes do save e cobre todos os contêineres gravados.
   - A lixeira não é gravada.
2. **`.wld` (itens).**
   - Os baús estão cobertos pelo TLMod.
   - **Molduras, suportes de armas, pratos, manequins e cabideiros, não.** Eles gravam o `netID` cru
     (short) e carregam com `netDefaults`:
     - com o TLMod e o mesmo conjunto de mods, o item volta pelo hook;
     - sem o mod, vira ar;
     - com outro conjunto de mods, vira **outro** item de mod.
3. **Os saves só trocam o arquivo no fim.** O `.plr` e o `.wld` são montados num `MemoryStream` e só vão
   ao disco no `WriteAllBytes` final. Uma exceção no meio cancela o save e deixa o arquivo antigo
   intacto.
4. **Buffs (futuro `register_buff`).**
   - **Save:** um buff ≥ 355 em `Player.buffType` faz **todo** save do personagem lançar exceção. No
     "Salvar e sair", isso pula também o save do mundo e a volta ao menu.
   - **Load:** buffs ≥ 355 são descartados.
   - **Rede:** `AddBuff` lança exceção, o que desconecta o cliente.
5. **NPCs.**
   - Um tipo ≥ 688 no `.wld` lança exceção ao carregar. Isso vale para NPCs da cidade,
     `SavesAndLoads`, NPCs brilhados e quartos.
   - O `LoadWorld` pega a exceção e marca `loadFailed`: o mundo não abre.
   - Hoje um NPC de mod é gravado como a casca. Ele perde a identidade, e a contagem de abates, o
     estandarte e o bestiário vão para a casca.
6. **Threads.**
   - Rodam fora da thread do jogo:
     - os autosaves do personagem (a cada 5 min) e do mundo (a cada 10 min);
     - o "Salvar e sair";
     - carregar, criar e renomear mundo;
     - o save do personagem quando a rede cai.
   - Os dois autosaves rodam **com o jogo andando**, e o TLMod tira e devolve os itens de mod do
     jogador e dos baús nessa outra thread.
7. **Rede.**
   - Formatos: itens vão como `netID` short (ushort no manequim e no cabideiro), NPCs como short e
     buffs como ushort.
   - Thread: a recepção roda na thread do jogo.
   - Exceções: uma exceção ao processar uma mensagem desconecta o cliente. No anfitrião, ela sobe até
     o Unity.
8. **Nuvem e sincronização.**
   - A nuvem está desligada no mobile (`IsAvailable`, `IsRequired` e `IsConnected` devolvem `false`).
   - A **sincronização de saves por Wi-Fi** copia `.plr`, `.map` e `.wld` crus, **sem o arquivo
     paralelo** do TLMod.
9. **Pesquisa da Jornada.**
   - Ela é gravada por nome persistente (string), e só de itens originais.
   - Ela usa o `type` do item. O hook do TLMod em `GUIResearch.CanBeResearched` bloqueia a interface,
     mas `GUIResearch.ActionResearch` não confere de novo.

---

## 1. Tabela de todos os caminhos

O que cada coluna diz:

| Coluna | Conteúdo |
|---|---|
| Grava / carrega | método e RVA de cada lado |
| Formato | como o ID vai no arquivo ou no pacote |
| ID ≥ Count ao carregar | efeito de um ID fora da faixa original (5456 para itens, 688 para NPCs, 355 para buffs) |
| TLMod | se o TLMod já protege o caminho |
| Thread | onde o caminho roda (códigos abaixo) |
| Confiança | um dos três níveis de [Como ler](#como-ler) |

Códigos da coluna Thread:

- **J**: thread do jogo, a principal do Unity (`XNAUnityRunner.ExecuteFrame` → `Game.UnityUpdate`).
- **S**: thread criada para o save ou o load (ver a [seção 5](#5-threads)).
- **S/J**: depende de quem chama.

### 1.1 Personagem (`.plr` e arquivos que o save do personagem grava)

O save é `Player.InternalSavePlayerFile` (`0x1044040`) e o load é `Player.LoadPlayer`
(`0x1049FA8`), salvo onde a linha indicar outro método.

| # | Caminho | Grava / carrega | Formato | ID ≥ Count ao carregar | TLMod | Thread | Confiança |
|---|---|---|---|---|---|---|---|
| P1 | Inventário 0..57 | save e load do `.plr` | int `netID`, int `stack`, byte `prefix`, bool `favorited` | guarda `id >= 0x1550` (`0x104B5EC`, `0x104B720`): `netDefaults(0)`, ar | protege (`inventory`) | S/J | confirmado no binário |
| P2 | Armadura e acessórios (20), corantes (10) | idem | int `netID`, byte `prefix` | `netDefaults` → `SetDefaults` zera `type >= 5456` (`0xBAD170`–`0xBAD180`): ar, sem aviso | protege (`armor`, `dye`) | S/J | confirmado no binário |
| P3 | Equipamentos extras (5) e seus corantes (5) | idem | int `netID`, byte `prefix` | guarda (`0x104B8B8`, `0x104B980`; formato antigo `0x104BA64`, `0x104BB34`): ar | protege (`misc_equips`, `misc_dyes`) | S/J | confirmado no binário |
| P4 | Porquinho, cofre, forja do defensor e cofre do vazio (40 cada) | idem | int `netID`, int `stack`, byte `prefix`; o cofre do vazio tem também bool `favorited` | `SetDefaults`: ar | protege (`piggy_bank`, `safe`, `defenders_forge`, `void_vault`) | S/J | confirmado no binário |
| P5 | Equipamentos alternativos (3 × armadura 20 e corantes 10) | `EquipmentLoadout.Serialize` `0x12AC9CC` / `Deserialize` `0x12ACAD0`, que chamam `Item.Serialize` `0xBB8988` / `Item.DeserializeFrom` `0xBB89FC` | int `netID`, int `stack`, byte `prefix` | `DeserializeFrom` força ar depois do `netDefaults` (`0xBB8A68`–`0xBB8A94`) | protege (`loadout_armor`, `loadout_dye`) | S/J | confirmado no binário |
| P6 | Slots temporários (mouse, pesquisa, guia, reforja) | `Player.SaveTemporaryItemSlotContents` `0x104624C` / `LoadTemporaryItemSlotContents` `0x1046488` | máscara de bits e `Item.Serialize` | `DeserializeFrom`: ar | protege (`temporary` 0..3) | S/J | confirmado no binário |
| P7 | Lixeira (`Player.trashItem`, `+0x418`) | **não é gravada**: nenhuma referência a `+0x418` no save nem no load | — | — | nada a proteger; um item na lixeira some ao sair, como um original | — | confirmado no binário |
| P8 | Buffs do jogador | save e load do `.plr` | int `type`, int `time` por buff | save: `Main.buffNoSave[type]` com limite (`0x104505C`), **exceção e save cancelado**; load: `type >= 355` ou negativo é descartado (`0x104C35C`–`0x104C384`) | não se aplica (sem `register_buff`) | S/J | confirmado no binário |
| P9 | Pesquisa da Jornada | `ItemsSacrificedUnlocksTracker.Save` `0x8F75B4` / `Load` `0x8F77CC`, via `CreativeUnlocksTracker.Save` (`0x1045408`) | int contagem, depois pares (string ID persistente, int) | nome desconhecido é ignorado (`TryGetValue`, `0x8F7904`) | parcial: hook em `GUIResearch.CanBeResearched` (`0x9D4C10`); ver [2.4](#24-pesquisa-da-jornada) | S/J | confirmado no binário (gravação); inferido (proteção pela interface) |
| P10 | Poderes da Jornada do personagem | `CreativePowerManager.SaveToPlayer` `0x8F31E4` / `LoadToPlayer` `0x8F3438` | bool e ushort por poder; nenhum ID de item, NPC ou buff | — | — | S/J | confirmado no binário |
| P11 | Atalhos radiais e estado dos acessórios de construção | `DpadRadial.Bindings` (`0x10452E0`), `builderAccStatus` (`0x1045330`) | int: índices de slot e estados, não IDs | — | — | S/J | confirmado no binário |
| P12 | `PersistentUserData.dat` | `LocalUser.SavePersistantData` `0xAA55EC` (chamado pelo `SavePlayer` em `0x1043B24`) / `LoadPersistantData` `0xAA4954` | IDs que vêm de listas fixas do jogo | — | — | S/J | confirmado no binário |
| P13 | Conquistas | `AchievementManager.Save` `0x15F94D8` → `Save(string, bool)` `0x15F9508` (chamado pelo `SavePlayer` em `0x1043B70`) | BSON cifrado com o estado das conquistas (nomes de condição e progresso) | — | — | S/J | confirmado no binário (caminho); inferido (conteúdo) |
| P14 | Mapa (`.map`) | `Player.InternalSaveMap` `0x1043D3C` (chamado em `0x1043B98`) → `MapHelper.SaveMap` `0x1602F10` / `InternalSaveMap` `0x1604458` | tiles, paredes e líquidos; nenhum ID de item, NPC ou buff | — | — | S/J | confirmado no binário |
| P15 | `config.json` | `Main.SaveSettings` `0xC75D0C` / `LoadSettings` `0xC788C8` | nenhum ID de item | — | — | S/J | confirmado no binário |
| P16 | Categorias por item do controle virtual (CSV `Item,Category`) | `VirtualControllerInputState.SaveDefaultCategories` `0x9FBC64`, `SaveCodeArrayCategories` `0x9FB8A8` / `LoadDefaultCategories` `0x9FBF98` | — | — | — | — | inferido: nenhum chamador direto, código morto |

### 1.2 Mundo (`.wld`)

O save é `WorldFile.InternalSaveWorld` (`0x14F1AC8`), que chama `SaveWorld_Version2` (`0x14F2BA8`).
O load é `WorldFile.LoadWorld` (`0x14EC048`), que chama `LoadWorld_Version2` (`0x14F076C`).

| # | Caminho | Grava / carrega | Formato | ID ≥ Count ao carregar | TLMod | Thread | Confiança |
|---|---|---|---|---|---|---|---|
| W1 | Baús | `WorldFile.SaveChests` `0x14F83CC` / `LoadChests` `0x14FD70C` | por item: short `stack` e, se > 0, int `netID` e byte `prefix` | `netDefaults` (`0x14FD8F4`, `0x14FD944`): ar. No save, `ItemsThatShouldNotBeInInventory[type]` com limite (`0x14F8728`) lança exceção se `type >= 5456` | protege (`chest`, pela posição) | S | confirmado no binário |
| W2 | Moldura | `TEItemFrame.WriteExtraData` `0x7B3E20` / `ReadExtraData` `0x7B3E9C` | short `netID`, byte `prefix`, short `stack` | `netDefaults(short)` (`0x7B3F18`): sem o mod, ar; com o TLMod, o hook remonta o item pelo número | **não protege** | S | confirmado no binário |
| W3 | Suporte de armas | `TEWeaponsRack` `0x7BAF28` / `0x7BAFA4` | igual à moldura | igual (`0x7BB020`) | **não protege** | S | confirmado no binário |
| W4 | Prato de comida | `TEFoodPlatter` `0x7AF108` / `0x7AF184` | igual à moldura | igual (`0x7AF200`) | **não protege** | S | confirmado no binário |
| W5 | Manequim (8 itens e 8 corantes) | `TEDisplayDoll` `0x7AB6A0` / `0x7ABBE4` | 2 bytes de máscara; por item, short `netID`, byte `prefix`, short `stack` | igual (`0x7ABD0C`) | **não protege** | S | confirmado no binário |
| W6 | Cabideiro (2 itens e 2 corantes) | `TEHatRack` `0x7B0AA8` / `0x7B0CEC` | 1 byte de máscara; por item, igual ao manequim | igual (`0x7B0DF0`) | **não protege** | S | confirmado no binário |
| W7 | Outros tile entities | `TELogicSensor` `0x7B746C` / `0x7B74C4` (byte); `TETrainingDummy` `0x7BA1B4` / `0x7BA1E0` (short, índice de NPC); pilone sem dados extras | nenhum ID de item | — | — | S | confirmado no binário |
| W8 | NPCs da cidade e `NPCID.Sets.SavesAndLoads` (`+0xE8`) | `WorldFile.SaveNPCs` `0x14F8994` / `LoadNPCs` `0x14FE110` | int `netID` (`0x14F8BC8`, `0x14F8DA8`), nome, posição, casa e variante; sem itens e sem buffs | `NPC.SetDefaults(netID)` (`0x14FE348`): tipo ≥ 688 lança exceção (limite em `0xDB4190`) | não se aplica hoje: o NPC de mod é gravado como a casca | S | confirmado no binário (o limite); inferido (que esse caminho o alcança) |
| W9 | NPCs brilhados (`NPC.ShimmeredTownNPCs`) | `SaveNPCs` / `LoadNPCs` | int contagem e int por tipo | limite em `0x14FE220`: exceção | — | S | confirmado no binário |
| W10 | Contagem de abates (estandartes) | `WorldFile.SaveWorldHeader` `0x14F6284` (short 688 em `0x14F72C0`, 688 × int em `0x14F730C`) / `LoadHeader` `0x14FA1EC` | short n, depois n × int | índice > 687 é lido e descartado (`0x14FBADC`–`0x14FBAF4`) | o abate de um NPC de mod conta para a casca | S | confirmado no binário |
| W11 | Quartos dos NPCs | `TownRoomManager.Save` `0x7A6550` / `Load` `0x7A6818` | int contagem e (int tipo, int x, int y) | `_hasRoom[tipo]` com limite (`0x7A69A8`): exceção | — | S | confirmado no binário |
| W12 | Bestiário | `SaveBestiary` `0x14F94F4` → `NPCKillsTracker.Save` `0x84E140`, `NPCWasNearPlayerTracker.Save` `0x84E3C4`, `NPCWasChatWithTracker.Save` `0x84E5FC` / `Load` `0x84E87C`, `0x84E940`, `0x84E9E8` | strings (IDs persistentes) e int | não indexa tabela por tipo | o crédito vai para a casca | S | confirmado no binário (formato); inferido (um nome desconhecido é inofensivo) |
| W13 | Cabeçalho e eventos | `SaveWorldHeader` / `LoadHeader` | `invasionType` (`0x14F6E6C`), índice da missão do pescador (`0x14F7204`), índices de slot dos NPCs da festa (`0x14F764C`), minérios escolhidos (`0x14F78D0`); nenhum ID de item | — | — | S | confirmado no binário |
| W14 | Poderes da Jornada do mundo | `CreativePowerManager.SaveToWorld` `0x8F2428` / `LoadFromWorld` `0x8F2674` | estados dos poderes | — | — | S | inferido |
| W15 | Itens no chão (`Main.item`) | não são gravados: as seções do `SaveWorld_Version2` (`0x14F2C1C`–`0x14F2D3C`) são cabeçalho, tiles, baús, placas, NPCs, tile entities, placas de pressão, quartos, bestiário, poderes e rodapé | — | — | — | — | inferido |

### 1.3 Nuvem e sincronização por Wi-Fi

| # | Caminho | Grava / carrega | Formato | ID ≥ Count ao carregar | TLMod | Thread | Confiança |
|---|---|---|---|---|---|---|---|
| C1 | Nuvem | `Terraria.Social.iOS.CloudSocialModule` (o único módulo de nuvem do binário): `IsRequired` `0x149B86C`, `IsAvailable` `0x149BA5C` e `IsConnected` `0x149BA64` são `mov w0, wzr; ret`. Um save marcado como nuvem cairia no `cacheFolder` local (`Write`, `0x149BBE0`, que chama `File.WriteAllBytes`) | arquivo cru | igual ao `.plr` e ao `.wld` | o arquivo paralelo de um save na nuvem vai para `cloud_saves/` | S/J | confirmado no binário |
| C2 | Envio do personagem | `SaveSynchronisationOperation_SendFileList.SendPlayerFile` `0xAAEF84` | `.plr` e `.map` crus | igual ao `.plr` | **o arquivo paralelo não vai junto** | J | confirmado no binário |
| C3 | Prévia do personagem na lista | `WritePlayerData` `0xAAF430` (int `netID` dos itens na memória, ex. `0xAAF950`) / `SaveSynchronisationState.ReadPlayerData` `0xAB1210` (`netDefaults`, ex. `0xAB1780`) | int `netID` | `netDefaults`: ar sem o TLMod | só exibição | J | confirmado no binário |
| C4 | Envio do mundo | `SendWorldFile` `0xAAFE9C` | `.wld` cru | igual ao `.wld` | **o arquivo paralelo não vai junto** | J | confirmado no binário |
| C5 | Recebimento | `SaveSynchronisationOperation_ReceiveFile.WriteFile` `0xAAD2C4` (`WriteAllBytes`) | arquivo cru | — | — | J | confirmado no binário (chamador); inferido (despacho virtual) |

### 1.4 Rede (mensagens e NetModules)

O envio é `NetMessage.SendData` (`0xD982E0`). A recepção é `MessageBuffer.ProcessData` (`0xD783B4`).
Nas duas, a mensagem é escolhida por uma tabela de saltos:

- envio: tabela em `0x22A76E0`, base `0xD98974`;
- recepção: tabela em `0x22A7520`, base `0xD7905C`.

| # | Mensagem | Envio / recepção | Formato do ID | ID ≥ Count ao receber | Confiança |
|---|---|---|---|---|---|
| N1 | 5: slot do jogador (inventário, equipamento, bancos, lixeira, equipamentos alternativos) | caso `0xD9C4A4`; `netID` em `0xDA0314`, escrito como short em `0xDA0358` / caso `0xD7B01C`; leitura em `0xD7B1A0`–`0xD7B208` | byte jogador, short slot, short `stack`, byte `prefix`, short `netID` | `netDefaults`: ar sem o TLMod; com o TLMod, o item do mesmo número | confirmado no binário |
| N2 | 21, 90, 145 e 148: itens no chão (mesmo caso nos dois lados) | caso `0xD98974`; `netID` em `0xD98AE4`, short em `0xD98B68` / caso `0xD7905C`; `netDefaults` em `0xD79444` e `0xD79520` | short `netID`, depois de índice, posição, velocidade, `stack` e `prefix` | igual a N1 | confirmado no binário |
| N3 | 32: slot de baú | short em `0xD9DBB0` / `netDefaults` em `0xD82148` | short `netID` | igual a N1 | confirmado no binário |
| N4 | 89, 123 e 133: pôr item em moldura, suporte de armas, prato | short em `0xD9979C`, `0xD9C2A0`, `0xD9B450` / `TEItemFrame.TryPlacing` (`0xD82CEC`), `TEWeaponsRack.TryPlacing` (`0xD7FCB4`), `TEFoodPlatter.TryPlacing` (`0xD7EB3C`) | short `netID` | igual a N1 | confirmado no binário |
| N5 | 86: tile entity inteiro | `TileEntity.Write` `0x15D3C68` / `TileEntity.Read(networkSend: true)` `0x15D3D44` (chamado em `0xD82FD0`) | o `WriteExtraData` de cada tipo; manequim e cabideiro usam `WriteItem` | igual a N1 | confirmado no binário |
| N6 | 121 e 124: slot de manequim e de cabideiro | `TEDisplayDoll.WriteItem` `0x7ADD28` / `ReadItem` `0x7ADDC8`; `TEHatRack.WriteItem` `0x7B2C7C` / `ReadItem` `0x7B2D1C` | ushort `netID`, ushort `stack`, byte `prefix` | `SetDefaults(ushort)` (`0x7ADE7C`, `0x7B2DD0`): ar | confirmado no binário |
| N7 | 23: atualização de NPC | `npc.netID` lido em `0xD99CCC` / `ReadInt16` em `0xD7DBBC`; `NPC.SetDefaults(netID)` quando o tipo muda (`0xD8E180`, `0xD8E214`) | short `netID` | ≥ 688: exceção no `SetDefaults` | confirmado no binário (leitura e chamada); inferido (qual dos shorts é o `netID`) |
| N8 | 61, 71 e 130: invocar, soltar, NPC pescado | `NPC.SpawnOnPlayer(short)` `0xD82388`; `NPC.ReleaseNPC(short)` `0xD7E7B8`; `NPC.SetDefaults(short)` `0xD80F9C` | short tipo | ≥ 688: exceção | confirmado no binário (chamadas); inferido (exceção) |
| N9 | 83: contagem de abates | `W.short(tipo)` e `W.int(NPC.killCount[tipo])`, com limite no envio (`0xD9A04C`) | short tipo, int contagem | — | confirmado no binário |
| N10 | 50: buffs do jogador | `W.ushort(buffType)` em `0xD9CA98` / grava cru em `buffType[]` (`0xD87224`), sem conferir o valor | ushort por buff | o tipo inválido fica guardado; estoura quando algo indexar com ele | confirmado no binário (gravação crua); inferido (onde estoura) |
| N11 | 53: buff em NPC | `NPC.AddBuff(ushort)` em `0xD7AFE0`, que indexa `buffImmune[type]` (`0xF486DC`) | short NPC, ushort tipo, short tempo | exceção | confirmado no binário |
| N12 | 54: buffs do NPC | `W.ushort` em `0xD9B0C4` / grava cru em `0xD829DC` | ushort tipo e short tempo por buff | igual a N10 | confirmado no binário |
| N13 | 55: buff em jogador | anfitrião confere `Main.pvpBuff[type]` com limite (`0xD7BDBC`); `Player.AddBuff` (`0xD7BE64`) indexa `buffImmune[type]` (`0xF84188`) | byte jogador, ushort tipo, int tempo | exceção | confirmado no binário |
| N14 | `NetCreativeUnlocksModule` (pesquisa, servidor → cliente) | `SerializeItemSacrifice` `0x8E6EF8` / `Deserialize` `0x8E6FAC` | short item, ushort contagem | `ContentSamples.ItemPersistentIdsByNetIds[item]` com `get_Item` (`0x8E707C`): `KeyNotFoundException` | confirmado no binário |
| N15 | `NetCreativeUnlocksPlayerReportModule` (pesquisa, cliente → servidor) | `SerializeSacrificeRequest` `0x8E752C` / `Deserialize` `0x8E7600` (só lê e descarta) | byte 0, ushort item, ushort quantidade | — | confirmado no binário |
| N16 | `NetBestiaryModule` | `SerializeKillCount` `0x8E6A34`, `SerializeSight` `0x8E6B08`, `SerializeChat` `0x8E6BAC` / `Deserialize` `0x8E6C50` | byte subtipo, short `netID` do NPC, ushort contagem | `ContentSamples.NpcsByNetId[id]` com `get_Item` (`0x8E6D48`): `KeyNotFoundException` | confirmado no binário |

Para toda a rede:

- **Thread:** o envio roda na thread de quem chama `SendData`; a recepção roda na thread do jogo
  (ver [5.1](#51-quem-roda-onde)).
- **TLMod:** não há tabela chave → ID na rede. O README lista o multiplayer em "Ainda não implementado".

---

## 2. Itens: evidências

### 2.1 Carregar um ID ≥ 5456 no `.plr` dá ar, sem aviso

**Inventário** (`Player.inventory` em `+0x4A0`). Abaixo de 5456 o slot recebe `netDefaults(id)`
(`0x104B614`); do contrário, `netDefaults(0)`. Confirmado no binário.

```
0x104b5c8: mov    w27, #0x1550                ; ItemID.Count
0x104b5e0: blr    x9                          ; ReadInt32 -> netID
0x104b5ec: cmp    w1, w27
0x104b5f4: b.ge   #0x104b694                  ; >= 5456:
...
0x104b6ac: mov    w1, wzr
0x104b6b4: bl     #0xbab378                   ; Item.netDefaults(0) -> ar
```

Os equipamentos extras (`+0x408`) e os corantes extras (`+0x410`) têm a mesma guarda em `0x104B8B8` e
`0x104B980`.

**Armadura, corantes e bancos** não têm guarda. Passam por `netDefaults` → `SetDefaults`, que zera
`type` depois do `ResetStats`, sem exceção nem log. Confirmado no binário.

```
; Item.SetDefaults(int, bool, ItemVariant) 0xBAD034
0xbad168: bl     #0xbadc6c                    ; Item.ResetStats
0xbad170: ldr    w8, [x23, #0xbc]!            ; type
0xbad174: mov    w9, #0x1550
0xbad178: cmp    w8, w9
0xbad17c: b.lt   #0xbad184
0xbad180: str    wzr, [x23]                   ; type = 0: o resto do SetDefaults monta ar
```

**Equipamentos alternativos e slots temporários** usam `Item.DeserializeFrom`. Ele chama
`netDefaults` e depois zera o item à mão. Confirmado no binário.

```
; Item.DeserializeFrom 0xBB89FC
0xbb8a68: ldr    w8, [x19, #0xbc]             ; type
0xbb8a6c: mov    w9, #0x1550
0xbb8a70: cmp    w8, w9
0xbb8a74: b.lt   #0xbb8a98
0xbb8a7c: str    wzr, [x19, #0xbc]            ; type = 0
0xbb8a80: str    wzr, [x19, #0xcc]            ; stack = 0
0xbb8a88: str    wzr, [x19, #0x198]           ; netID = 0
```

Para o TLMod, isso é o que já está em `terraria_offsets.h:223` e no README ("Como o jogo salva"): um ID
de mod gravado no `.plr` não volta. O TLMod evita o problema tirando o item antes do save (modo A).

### 2.2 O save só troca o arquivo no fim

**Personagem** (`Player.InternalSavePlayerFile`, `0x1044040`). Confirmado no binário.

```
0x104428c: bl     #0x14971b8                  ; FileUtilities.Copy(path, path + ".bak")
0x10442d0: bl     #0x1df5e38                  ; new MemoryStream(2000): tudo vai para a memória
0x10454f4: bl     #0x1d43e78                  ; CryptoStream.FlushFinalBlock
0x10456b4: bl     #0x14973b4                  ; FileUtilities.WriteAllBytes: só aqui o arquivo muda
```

**Mundo** (`WorldFile.InternalSaveWorld`, `0x14F1AC8`). Confirmado no binário.

```
0x14f1ce4: bl     #0x1df5e38                  ; new MemoryStream
0x14f1d1c: bl     #0x14f2ba8                  ; SaveWorld_Version2(writer)
0x14f2000: bl     #0x14f2d48                  ; ValidateWorld (o arquivo atual, antes do .bak)
0x14f21f0: b      #0x14973b4                  ; FileUtilities.WriteAllBytes
```

No mundo, a exceção do `SaveWorld_Version2` cai num `finally` (o `using` do `BinaryWriter`). Ele libera
o writer e relança: o landing pad `0x14F23BC` guarda a exceção em `x22` e volta para `0x14F1D28`; depois
do `Dispose`, `cbnz x22, #0x14f2204` desvia para `bl 0x66c3c8` com essa exceção, que a relança.
Uma exceção no meio de qualquer um dos dois saves, portanto:

- não muda o arquivo do jogo;
- no personagem, deixa o `.bak` igual ao arquivo antigo, porque a cópia é feita antes.

**Quem pega a exceção.**

- `Player.SavePlayer` (`0x1043A9C`) pega `Exception` na chamada de `InternalSavePlayerFile`
  (`0x1043C40`), registra e relança. Confirmado no binário.

  ```
  0x1043cfc: bl     #0x2219d9c                ; UnityEngine.Debug.LogError
  0x1043d04: bl     #0x642c30                 ; relança
  ```

- `WorldFile.SaveWorld` (`0x14F15E8`) só libera o `WorldFile.IOLock` (`Monitor.Exit` no `finally`) e
  deixa a exceção subir. Confirmado no binário.

O TLMod já trata isso: `RunSave` chama o original por `il2cpp_runtime_invoke`, recebe a exceção, avisa
`ok = false` e a relança (`il2cpp_bridge/src/save_hooks.cpp:215`). O `AfterSave` devolve os itens e não
mexe no arquivo paralelo (`modapi/src/item_persistence.cpp:255`).

### 2.3 Lixeira

`Player.trashItem` (`+0x418`) não aparece no save nem no load do `.plr`: nenhuma instrução lê ou
grava `+0x418` em `InternalSavePlayerFile` ou `LoadPlayer`. Confirmado no binário.

- Um item na lixeira some ao fechar o jogo, seja original ou de mod.
- Na rede, a lixeira vai pela mensagem 5 (slot `PlayerItemSlotID.TrashItem`, comparado em
  `0xD9C698`), com `netID` short.
- Não há lacuna de persistência aqui. O TLMod não precisa cobrir a lixeira no save.

### 2.4 Pesquisa da Jornada

**No arquivo.**

- `ItemsSacrificedUnlocksTracker.Save` (`0x8F75B4`) grava int contagem, depois um par por item:
  `W.string(ID persistente)` (`0x8F7704`) e `W.int(quantidade)` (`0x8F771C`).
- O `Load` (`0x8F77CC`) lê os pares e resolve o nome com `TryGetValue` (`0x8F7904`). Um nome
  desconhecido é ignorado, sem exceção. Confirmado no binário.
- O nome vem de `ContentSamples.ItemPersistentIdsByNetIds`, que só tem itens originais. **Nenhum ID de
  mod chega ao arquivo pela pesquisa.**

**Na pesquisa em si.** O mobile pesquisa por `GUIResearch.ActionResearch` (`0x9D674C`), chamado por
`GUIResearch.DrawMainButtons` (`0x9D6720`). Ele usa o `type` do item no slot de pesquisa. Confirmado
no binário.

```
0x9d6814: CreativeItemSacrificesCatalog.TryGetSacrificeCountCapToUnlockInfiniteItems(itemId = item.type)
0x9d68f4: NetCreativeUnlocksPlayerReportModule.SerializeSacrificeRequest(itemId = item.type, ...)
0x9d6960: ItemsSacrificedUnlocksTracker.RegisterItemSacrifice(itemId = item.type, ...)
0x9d698c: Item.TurnToAir(item)
```

(Linhas do rastreio, que mostra os argumentos de cada chamada; os `bl` estão nesses endereços.)

O `ActionResearch` não chama `CanBeResearched`. Um item de mod que chegue ao slot de pesquisa é
consumido e soma pesquisa para a **casca**.

**O que o TLMod faz.**

- Responde "não pode ser pesquisado" no `GUIResearch.CanBeResearched` (`0x9D4C10`). Os chamadores dessa
  função são `GUIInventory.DrawItem`, `DrawInventoryResearchBinding`, `DrawInventoryResearchButton`,
  `GUIResearchPage.RefreshItems` e `DrawResearch`.
- Se esses pontos bastam para impedir que o item chegue ao slot é **inferido**. Só o aparelho confirma
  ([E4](#e4-pesquisa-da-jornada-com-item-de-mod)).
- O caminho do PC (`CreativeUI.SacrificeItem`, `0x8F6D7C`) também usa o `type`. Os chamadores são
  `UICreativeInfiniteItemsDisplay.SacrificeWhatYouCan` e `ItemSlot.OverrideLeftClick`; a interface do
  PC, que no mobile é provavelmente inalcançável (inferido).

### 2.5 Baús

`SaveChests` (`0x14F83CC`) indexa uma tabela pelo `type` antes de gravar. Confirmado no binário.

```
0x14f8720: ldr    x8, [x8, #0x28]             ; ItemID.Sets.ItemsThatShouldNotBeInInventory
0x14f8728: ldrsw  x9, [x26, #0xbc]            ; item.type
0x14f8730: cmp    w9, w10                     ; w10 = tamanho do array
0x14f8734: b.hs   #0x14f8818                  ; -> IndexOutOfRangeException
...
0x14f87a0: ldr    w1, [x26, #0x198]           ; item.netID
0x14f87a8: ldr    x9, [x8, #0x278]            ; Write(int)
```

- Com a casca, `type` nunca passa de 5455, e o `netID` de mod iria ao arquivo como int. O TLMod tira
  esses itens antes (`chest`, identificado pela posição x, y).
- Se um dia o TLMod abandonar a casca (`docs/estudo_extensao_tabelas.md`), este é um dos pontos que
  estouram.

### 2.6 Tile entities

Os cinco tipos que guardam item gravam o `netID` cru e carregam com `netDefaults(short)`. Confirmado no
binário.

```
; TEItemFrame.ReadExtraData 0x7B3E9C (rastreio)
0x7b3f08: R.Int16()
0x7b3f18: Item.netDefaults(new Item, type = sxth(R.Int16()))
0x7b3f2c: R.Byte()       -> Item.Prefix
0x7b3f58: R.Int16()      -> stack
```

| tipo | grava | carrega (`netDefaults`) | itens |
|---|---|---|---|
| `TEItemFrame` | `0x7B3E20` | `0x7B3F18` | 1 |
| `TEWeaponsRack` | `0x7BAF28` | `0x7BB020` | 1 |
| `TEFoodPlatter` | `0x7AF108` | `0x7AF200` | 1 |
| `TEDisplayDoll` | `0x7AB6A0` | `0x7ABD0C` e seguintes | 8 itens e 8 corantes |
| `TEHatRack` | `0x7B0AA8` | `0x7B0DF0` e seguintes | 2 itens e 2 corantes |

Na rede (mensagens 121 e 124), manequim e cabideiro usam `WriteItem` e `ReadItem`. O formato muda para
ushort `netID` e ushort `stack`, e o load vai direto por `Item.SetDefaults` (`0x7ADE7C`, `0x7B2DD0`).
Confirmado no binário.

**O que isso faz com um item de mod.** O TLMod cobre só os contêineres de `BridgeItemSlots`
(jogador e `Main.chest`). O README lista os tile entities em "O que ainda fica de fora". Então o `netID`
de mod (8192–32767) vai ao `.wld` como short. Ao carregar:

| situação | resultado | confiança |
|---|---|---|
| TLMod ativo, mesmo conjunto de mods | `netDefaults(8192)` → hook → a casca é remontada com `netID` 8192: o item volta | inferido (pelo código do hook) |
| mod desativado, removido, ou jogo sem o TLMod | `SetDefaults` original → `type >= 5456` → ar. O próximo save grava ar: **perda definitiva**, sem linha no `tlmod.log` | confirmado no binário |
| conjunto de mods mudou (um mod entrou ou saiu antes deste) | o número aponta para outro item registrado: **o item vira outro, em silêncio** | inferido (IDs sequenciais, `content_registry.cpp:74`) |

### 2.7 Itens em NPCs, no mundo e em eventos

Não há outros lugares que guardam itens:

- **NPCs** gravam só tipo, nome, posição, casa e variante (`SaveNPCs`, `0x14F8BB0`–`0x14F8CB8`). Não
  há item nem buff. Confirmado no binário.
- **Cabeçalho do mundo e eventos.** Eventos e invasões são gravados como flags e contadores. A missão
  do pescador vai como índice (`Main.anglerQuest`, `0x14F7204`). A festa grava índices de slot de NPC
  (`0x14F764C`), não tipos. Confirmado no binário.
- **Itens no chão** (`Main.item`) não são gravados (W15, inferido). Só vão pela rede (N2).

---

## 3. NPCs

### 3.1 NPCs gravados no mundo

`SaveNPCs` (`0x14F8994`) grava, nesta ordem (confirmado no binário):

1. a lista de NPCs brilhados: int contagem e um int de tipo por NPC;
2. cada NPC da cidade: `W.bool(true)`, `W.int(netID)` (`0x14F8BC8`), nome, posição, `homeless`, casa,
   máscara de bits e variante;
3. cada NPC de `NPCID.Sets.SavesAndLoads`: `W.int(netID)` (`0x14F8DA8`) e posição.

No load, três pontos indexam por tipo. Confirmado no binário.

```
; LoadNPCs 0x14FE110: NPCs brilhados
0x14fe208: ldr    x24, [x8, #0x140]           ; NPC.ShimmeredTownNPCs
0x14fe214: blr    x8                          ; ReadInt32 -> tipo
0x14fe21c: ldr    w8, [x24, #0x18]            ; tamanho (688)
0x14fe220: cmp    w0, w8
0x14fe224: b.hs   #0x14fe748                  ; -> IndexOutOfRangeException

; TownRoomManager.Load 0x7A6818: quartos
0x7a69a4: ldr    w9, [x8, #0x18]              ; _hasRoom.Length
0x7a69a8: cmp    w22, w9
0x7a69ac: b.hs   #0x7a69e0                    ; -> IndexOutOfRangeException

; NPC.SetDefaults 0xDAF1BC (chamado por LoadNPCs em 0x14FE348)
0xdb417c: ldr    x8, [x8, #0x570]             ; TextureAssets.Npc
0xdb418c: cmp    w9, w10
0xdb4190: b.hs   #0xdbc92c                    ; -> IndexOutOfRangeException
```

`WorldFile.LoadWorld` (`0x14EC048`) pega a exceção. O landing pad `0x14ED258` registra com
`Debug.LogError`, guarda `WorldFile.LastThrownLoadException` (`0x14ED2FC`) e marca
`WorldGen.loadFailed`. O mundo não abre e o menu oferece o backup.

- O caminho é confirmado no binário.
- A tela do menu é inferida.
- O TLMod lê `loadFailed` e, nesse caso, não aplica o arquivo paralelo (`save_hooks.cpp:262`,
  `bridge_adapter.cpp:395`).

### 3.2 Contagem de abates e estandartes

`SaveWorldHeader` grava `W.short(688)` (`0x14F72C0`) e 688 ints de `NPC.killCount` (`0x14F730C`). O
`LoadHeader` lê n entradas e descarta as de índice > 687. Confirmado no binário.

```
0x14fbadc: cmp    ..., 687                    ; (rastreio) índice <= 687?
0x14fbaf4: R.Int32()                          ; não: lê e descarta
0x14fbb40: NPC.killCount[i] = R.Int32()       ; sim: guarda
```

A mensagem 83 envia `short tipo, int killCount[tipo]`, com limite no envio (`0xD9A04C`).

### 3.3 Bestiário

**No mundo.** O bestiário é gravado por nome persistente (`NPCKillsTracker.Save`, `0x84E140`;
`NPCWasNearPlayerTracker.Save`, `0x84E3C4`; `NPCWasChatWithTracker.Save`, `0x84E5FC`).

**Na rede.** `NetBestiaryModule.Deserialize` (`0x8E6C50`) converte o `netID` em nome com
`ContentSamples.NpcsByNetId[id]` usando `get_Item` (`0x8E6D48`). Um ID desconhecido lança
`KeyNotFoundException`. Confirmado no binário.

### 3.4 O que isso significa para NPCs de mod

O TLMod nunca deixa o ID de mod chegar ao jogo, então nenhum desses caminhos estoura hoje. Em troca:

- **Identidade.** Um NPC de mod cuja casca seja NPC da cidade ou esteja em `SavesAndLoads` é gravado
  como a casca. Ao recarregar o mundo, volta como a casca original: a tabela de slots do TLMod é só
  memória. Inferido.
- **Crédito.** Abates, estandartes, bestiário (`NPCKillsTracker.RegisterKill`, `0x84F7B8`, por
  `GetBestiaryCreditId`) e a mensagem 83 contam para a casca. Inferido; só o aparelho confirma
  ([E7](#e7-crédito-de-npc-de-mod)).
- **Rede.** A mensagem 23 leva o `netID` da casca. O cliente vê a casca, sem a IA nem a textura do mod,
  porque não sabe que aquele slot é de mod. Inferido.

---

## 4. Buffs, pensando no `register_buff`

`Player.buffImmune` e `NPC.buffImmune` são `bool[355]`. `Main.buffNoSave` também tem 355 posições
(alocado em `0xD75CE4`). Confirmado no binário.

```
; Player..ctor 0x104242C
0x1042878: mov    w1, #0x163                  ; 355
0x104287c: bl     #0x642b80                   ; new bool[355]
0x104288c: str    x0, [x19, #0x450]           ; Player.buffImmune

; NPC..ctor 0xF45E7C
0xf4604c: mov    w1, #0x163
0xf46050: bl     #0x642b80
0xf4605c: str    x0, [x19, #0x110]            ; NPC.buffImmune
```

### 4.1 Save do personagem: um buff ≥ 355 cancela o save

```
; InternalSavePlayerFile, laço dos buffs
0x1045040: ldr    x9, [x9, #0xbf0]            ; Main.buffNoSave
0x1045050: ldrsw  x8, [x8, #0x20]             ; buffType[i]
0x1045054: ldr    w10, [x9, #0x18]            ; 355
0x1045058: cmp    w8, w10
0x104505c: b.hs   #0x1045a7c                  ; -> IndexOutOfRangeException
```

Confirmado no binário. O que acontece em seguida depende de quem salvou:

| quem salvou | efeito | confiança |
|---|---|---|
| autosave do personagem (`saveToonWhilePlayingCallBack`, `0x130ED54`) | `SavePlayer` registra e relança; `ManagedThread.ThreadExecute` (`0x71F3E0`) pega e chama `Debug.LogWarning` (`0x71F4B0`). O `.plr` não muda. **Repete a cada autosave enquanto o buff durar** | confirmado no binário |
| "Salvar e sair" (`SaveAndQuitCallBack`, `0x130C8CC`) | a chamada de `SavePlayer` (`0x130CD0C`) não tem landing pad no callback. A exceção pula `SaveWorld` (`0x130CD6C`), o fim da rede e a volta ao menu (`LocalUserQuitOnMainThread`, `0x130CEF0`; `menuMode = 0`, `0x130CFA0`). O mundo não é salvo e o jogo fica em `menuMode 10` ("Salvando") com `gameMenu = true` | confirmado no binário (o fluxo); só o aparelho confirma (a tela) |
| queda da rede (`InnerClientLoop`, `0x11D2208`) | igual ao autosave; a limpeza depois do save (`MessageBuffer.Reset`, `0x11D2248`) não roda | confirmado no binário (o fluxo) |

### 4.2 Load do personagem: buffs ≥ 355 somem

```
0x104c370: cmp    w9, #0x163                  ; tipo < 355?
0x104c374: csel   w28, w27, wzr, lt
0x104c380: cbnz   w28, #0x104c630             ; sim: mantém
0x104c384: str    wzr, [x8]                   ; não: buffType[i] = 0, e o laço encolhe
```

Confirmado no binário. Negativos também são descartados (`0x104C35C`).

### 4.3 Rede

| mensagem | o que a recepção faz com um tipo ≥ 355 | confiança |
|---|---|---|
| 50 (buffs do jogador) | grava cru em `buffType[]` (`0xD87224`); estoura depois, em quem indexar | confirmado no binário (gravação crua); inferido (onde estoura) |
| 54 (buffs do NPC) | grava cru (`0xD829DC`); idem | confirmado no binário (gravação crua); inferido (onde estoura) |
| 53 (buff em NPC) | `NPC.AddBuff` → `buffImmune[type]` (`0xF486D8`–`0xF486DC`): exceção | confirmado no binário |
| 55 (buff em jogador) | anfitrião: `Main.pvpBuff[type]` (`0xD7BDBC`); depois `Player.AddBuff` → `buffImmune[type]` (`0xF84184`–`0xF84188`): exceção | confirmado no binário |

### 4.4 O que um `register_buff` precisa respeitar

- O ID de mod **não pode** entrar em `Player.buffType`/`NPC.buffType`: quebra todo save (4.1), some no
  load (4.2) e derruba a rede (4.3).
- O caminho que já funciona para itens serve para buffs:
  - **casca de buff**: um buff original sem efeito próprio ocupa o slot e o TLMod guarda, à parte, qual
    buff de mod está ali;
  - **tirar antes do save e devolver depois**, com o arquivo paralelo do personagem.
- `AddBuff` (`Player.AddBuff` `0xF8410C`, `NPC.AddBuff` `0xF4866C`) é o ponto de entrada natural para a
  troca, como o `SetDefaults` é para itens.
- O ushort da rede (até 65535) não é o limite real. O limite real são os arrays de 355 posições.

---

## 5. Threads

### 5.1 Quem roda onde

`ThreadManager.RunForUser` (`0x71F604`), `RunForUserCritical` (`0x71F89C`) e `Run` (`0x71F4F0`) criam
um `ManagedThread`. `ManagedThread.Run` (`0x71F360`) faz `Thread.set_IsBackground(true)` e
`Thread.Start`, e `RunCritical` (`0x71F3A0`) sobe a prioridade e faz `Thread.Start`. Ou seja, **thread
nova, não ThreadPool** (confirmado no binário). Só a renomeação de mundo usa o ThreadPool.

| entrada (thread do jogo) | thread do save/load | grava / lê | jogo continua? | confiança |
|---|---|---|---|---|
| `Main.DoUpdate_AutoSave` `0xCB0BFC` → `WorldGen.saveToonWhilePlaying` `0x130F020` → `RunForUser` | `saveToonWhilePlayingCallBack` `0x130ED54` → `SavePlayer` (`0x130EF20`) | `.plr`, `.map`, conquistas, `PersistentUserData.dat` | **sim**, a cada `Main.AutoSavePlayerTime` = 300 000 ms | confirmado no binário |
| `DoUpdate_AutoSave` → `WorldGen.saveAndPlay` `0x130EC78` → `RunForUser` (mais um `saveToonWhilePlaying`) | `saveAndPlayCallBack` `0x130EBDC` → `SaveWorld` (`0x130EC60`) | `.wld` | **sim**, a cada `Main.AutoSaveWorldTime` = 600 000 ms | confirmado no binário |
| `Player.KillMe` → `saveToonWhilePlaying` (`0xFC0C10`) | igual ao autosave | `.plr` | sim | confirmado no binário |
| `WorldGen.SaveAndQuit` `0x130D1B8` → `RunForUserCritical` | `SaveAndQuitCallBack` `0x130C8CC`: `gameMenu = true` (`0x130CBEC`), depois `SavePlayer` e `SaveWorld` | `.plr`, `.wld` | não (mundo parado) | confirmado no binário |
| `Netplay.StartTcpClient` `0x11D78C8` → `ThreadManager.Run` | `TcpClientLoop` → `InnerClientLoop` `0x11D1F48`: ao cair, `gameMenu = true` e `SavePlayer` (`0x11D2208`) | `.plr` | não | confirmado no binário |
| `WorldGen.playWorld` `0x130EAB4` → `RunForUserCritical` | `playWorldCallBack` `0x130D624` → `LoadWorld` (4 chamadas) | `.wld` | não (tela de carregamento) | confirmado no binário |
| `WorldGen.serverLoadWorld` `0x130F724` → `ThreadManager.Run` | `serverLoadWorldCallBack` `0x130F0FC` → `LoadWorld` | `.wld` | não | confirmado no binário |
| `WorldGen.CreateNewWorld` `0x130C290` → `RunForUserCritical` | `worldGenCallback` → `SaveWorld` (`0x1305DA0`) | `.wld` | não | confirmado no binário |
| `WorldGen.RenameWorld` `0x130D2E4` → `ThreadPool.QueueUserWorkItem` | `RenameWorldCallBack` `0x130D3B0` → `LoadWorld` e `SaveWorld` | `.wld` | não (menu) | confirmado no binário |
| telas do menu: `Main.LoadPlayers` `0xC73C38` (chamado por `GUIMainMenu.Draw` e outras) → `Player.GetFileData` → `LoadPlayer` | thread do jogo | `.plr` | — | confirmado no binário (chamadores); inferido (que a tela roda na thread do jogo) |
| `PlayerFileData.CreateAndSave` `0x14D93E4`, `Rename` `0x14DA448`, `GUIPlayerSelectMenu.CloseNameEditAndSave` `0xA5DC88` → `SavePlayer` | thread do jogo | `.plr` | — | inferido |
| rede, recepção: `Game.UnityUpdate` `0xB04670` → `Netplay.UpdateClientNet` `0x11D38D4` → `ClientUpdateFrame` `0x11D3228` / `ClientLoopFrameUpdate` `0x11D3B14` → `ProcessData`; anfitrião: `ServerUpdateFrame` `0x11D3420` | thread do jogo | — | — | confirmado no binário |
| sincronização por Wi-Fi: `GUISaveSynchronisation.Draw` → `SaveSynchronisationManager.Update` `0xA97E6C` → `Host/Client.Update` → `ProcessServerMessage`/`ProcessClientMessage` → `ReceiveFile.WriteFile` | thread do jogo | arquivos crus | — | confirmado no binário (até `Process*Message`); inferido (despacho virtual) |

As constantes do autosave vêm do `Main..cctor`. `0xD76E90` grava os dois ints lidos de `0x22A6CE8`:
`300000` em `AutoSavePlayerTime` (`+0x1228`) e `600000` em `AutoSaveWorldTime` (`+0x122C`). Confirmado
no binário.

### 5.2 Exceções nessas threads

- `ManagedThread.ThreadExecute` (`0x71F3E0`) pega qualquer `Exception` do callback e chama
  `Debug.LogWarning` (`0x71F4B0`). A thread termina sem derrubar o jogo. Confirmado no binário.
- `saveToonWhilePlayingCallBack` e `SaveAndQuitCallBack` não têm landing pad nas chamadas de save. A
  exceção atravessa o resto do callback (ver 4.1). Confirmado no binário.
- **Rede, cliente.** `ClientLoopFrameUpdate` pega a exceção do `ProcessData` (landing pad
  `0x11D471C` → `0x11D472C`). Confirmado no binário.

  ```
  0x11d4748: ldr    x0, [x0, #0xfd8]          ; TypeInfo System.Exception
  0x11d47bc: bl     #0x22232d8                ; Debug.Log("   Client loop Exception: " + e)
  0x11d47f4: strb   w9, [x8, #0x40]           ; Netplay.Disconnect = true
  0x11d480c: bl     #0x1f05fb0                ; Netplay.disconnectEvent.Set()
  ```

- **Rede, anfitrião.** `ServerUpdateFrame` e `ClientUpdateFrame` não têm try/catch, e
  `Game.UnityUpdate` (`0xB04670`) e `XNAUnityRunner.ExecuteFrame` (`0xAE1A20`) também não.
  - A exceção sobe até o Unity, que a registra e encerra aquele `Update`. Isso é inferido: é o
    comportamento padrão do Unity.
  - A mensagem já saiu da fila (`Common.GetNextMessage`, `0x11D3500`), então não é reprocessada.
    Inferido.

### 5.3 O que o TLMod faz nessas threads

- Os ganchos de save rodam na thread que salva: `TLMod_Hooked_PlayerInternalSavePlayerFile` e
  `TLMod_Hooked_WorldFileInternalSaveWorld` (`save_hooks.cpp:433` e `:460`).
- `ItemPersistence` serializa os saves do mesmo tipo com uma "vez" de até 30 s (`kSaveTurnWait`,
  `item_persistence.cpp:25`).
- O código já sabe que o save pode rodar com o jogo andando (`item_persistence.cpp:214`). Ao devolver um
  item:
  - confere se o slot ainda está vazio;
  - se não estiver, procura outro slot equivalente;
  - sem lugar, guarda o item como órfão `no_space`.
- **O que não está coberto** (lacuna 2):
  - Durante o save, o slot fica com `stack = 0` e `netID = 0`, e a thread do jogo o vê vazio. O item
    some da mão, do equipamento ou do baú aberto pelo tempo do save. No mundo, são todos os baús com
    item de mod. Só o aparelho confirma ([E2](#e2-janela-do-autosave)).
  - Entre "o slot está vazio?" e "devolve" não há trava com a thread do jogo (checagem e escrita
    separadas).
- O README diz que o save roda "no ThreadPool" (`modapi/README.md:537` e `:573`). Pelo binário, é uma
  `Thread` nova do `ThreadManager`. O efeito é o mesmo (outra thread, jogo andando), mas o texto está
  impreciso.

---

## 6. O que o TLMod já faz

| área | o que existe | onde |
|---|---|---|
| ganchos de save e load | antes e depois de `InternalSavePlayerFile` (`0x1044040`) e `InternalSaveWorld` (`0x14F1AC8`); depois de `LoadPlayer` (`0x1049FA8`) e `LoadWorld` (`0x14EC048`, respeitando `loadFailed`) | `il2cpp_bridge/src/save_hooks.cpp`; RVAs em `terraria_offsets.h` |
| arquivo paralelo | `<arquivo>.tlmod.json` com chave `mod_id:nome`, slot, pilha, prefixo e favorito; gravado em `.tmp` e renomeado (atômico); se estiver ilegível, vai para quarentena (`.invalid-<data>`) | `modapi/src/item_persistence.cpp` |
| contêineres cobertos | inventário 0..57, armadura, corantes, equipamentos extras e seus corantes, os 4 bancos, equipamentos alternativos, 4 slots temporários, baús | `modapi/adapters/bridge/bridge_adapter.cpp` (`BridgeItemSlots`) |
| órfãos | `missing_mod`, `no_space`, `not_loaded`; regravados a cada save | `item_persistence.cpp` |
| casca de item | o ID de mod nunca chega ao `SetDefaults` original; `netDefaults` com ID de mod remonta o item | `item_hooks.cpp`, `mod_loader.cpp:157` |
| casca de NPC | o ID de mod nunca chega a `NewNPC`/`SetDefaults` | `bridge.h:108`–`121` |
| pesquisa | `CanBeResearched` responde "não"; o tooltip esconde a linha de pesquisa | `screen_hooks.cpp:403`; README |
| nuvem | o arquivo paralelo de um save na nuvem vai para `cloud_saves/` | README |
| erros | nenhuma exceção C++ sai do gancho (`OnBridgeSave`, `try/catch`) | `bridge_adapter.cpp:376` |

---

## 7. Experimentos no aparelho

As Ferramentas (`tlmod.devtools`) dão itens e NPCs de mod (Dar item, Invocar) e mudam a hora. A
Oficina de Mods liga e desliga mods.

Onde ler os resultados:

- **`tlmod.log`** (painel → Log, ou `tlmod/tlmod.log`): mostra o que o TLMod faz no save. Linhas úteis:
  - `N itens de mod reinseridos (M em outro slot), K guardados como órfãos`;
  - `... mudou de X para Y (o slot foi ocupado durante o save)`;
  - `o save falhou; arquivo paralelo mantido`.
- **logcat, tag `Unity`**: as mensagens do próprio jogo (`Debug.Log`/`LogError`/`LogWarning`) não vão
  para o `tlmod.log`. Exemplos: `Auto saving...`, `   Client loop Exception: ...` e o erro do
  `SavePlayer`. Comando: `adb logcat -s Unity TLMod TLModBridge`.

Use um mundo e um personagem de teste.

### E1. Item de mod em moldura (W2)

1. Ferramentas → Dar item → um item de mod (por exemplo, o Rubi Fictício do `tlmod.example_item`).
2. Ponha o item numa moldura. A moldura aceita qualquer item; suporte, prato, manequim e cabideiro
   filtram pela casca.
3. Salvar e sair; abrir o mundo de novo. **Esperado:** o item volta (hook, mesmo número). Não há
   linha no `tlmod.log` para ele.
4. Na Oficina, desative o mod; abra o mundo; salvar e sair. Reative o mod; abra o mundo.
   **Esperado:** a moldura está vazia, e o item não volta (perda definitiva, sem log).
5. Variante da troca de número: com dois mods de item, ponha na moldura o item do mod que carrega por
   último e desative o outro. **Esperado:** outro item, ou vazio.

### E2. Janela do autosave

1. Num mundo grande, ponha um item de mod no primeiro slot de um baú e deixe o baú aberto.
2. Espere o autosave do mundo (10 min de jogo; o logcat mostra `Auto saving...`).
3. Observe se o slot fica vazio por um instante. **Se ficar, a janela é visível.**
4. Para provocar a corrida, ponha um item original nesse slot durante a janela. **Esperado no
   `tlmod.log`:** `... mudou de chest(x,y)#0 para ... (o slot foi ocupado durante o save)`.
5. Para o personagem: use continuamente um item de mod na mão, ou vista um acessório de mod, durante o
   autosave de 5 min. Observe se o uso é interrompido ou se algum efeito pisca.

### E3. Sincronização por Wi-Fi (C2, C4)

1. Num aparelho A com o TLMod e um item de mod no inventário, envie o personagem para um aparelho B com
   o mesmo TLMod e o mesmo mod.
2. **Esperado:**
   - em B, o personagem chega sem o item de mod;
   - no `tlmod.log` de B, nenhuma linha de "reinseridos" para esse arquivo;
   - em A, o item continua (no próximo load, A registra "reinseridos").
3. Mande o personagem de B de volta para A. Observe se A reinsere em cima do `.plr` recebido os itens do
   arquivo paralelo antigo.

### E4. Pesquisa da Jornada com item de mod

1. Num personagem da Jornada, Ferramentas → Dar item → item de mod.
2. Tente pôr o item no slot de pesquisa: arrastar, tocar e o atalho do inventário.
3. **Esperado:** a interface recusa (hook em `CanBeResearched`).
4. Se aceitar, toque Pesquisar. **Se o item sumir e a casca ganhar pesquisa, a proteção da interface
   não basta.**

### E5. Buff ≥ 355 (para o `register_buff`)

As Ferramentas de hoje não põem buffs. Um comando de teste mínimo (fora desta auditoria, que não altera
código) bastaria: `buff <id> <segundos>`, que escreve `Player.buffType[0] = 4096` e `buffTime[0]`.

1. Com o buff ativo, espere 5 min. **Esperado:**
   - no `tlmod.log`: `<arquivo>.plr: o save falhou; arquivo paralelo mantido`;
   - no logcat: o `LogError` do `SavePlayer` e o `LogWarning` do `ManagedThread`.
2. Salvar e sair. **Esperado:** o jogo para na tela "Salvando" e o mundo não é salvo (horário do `.wld`
   inalterado).

### E6. Rede com outro conjunto de mods (N1–N6, N14, N16)

1. Anfitrião com o TLMod e um mod de itens; cliente sem o TLMod (ou com outro conjunto de mods).
2. O anfitrião põe um item de mod num baú; o cliente abre o baú.
3. **Esperado:**
   - cliente sem o TLMod: ar;
   - cliente com outro conjunto de mods: outro item.

### E7. Crédito de NPC de mod

1. Ferramentas → Invocar → NPC de mod; mate o NPC.
2. Abra o bestiário. **Esperado:** a entrada da casca aparece desbloqueada, com o abate.

---

## 8. Respostas diretas

### Pergunta 1. Todos os caminhos em que um tipo ou `netID` de item chega a um arquivo ou à sincronização

**`.plr`** (`InternalSavePlayerFile` `0x1044040` / `LoadPlayer` `0x1049FA8`):

- inventário 0..57 (P1);
- armadura e acessórios, corantes (P2);
- **slots de equipamento extra** e seus corantes (P3);
- porquinho, cofre, forja e cofre do vazio (P4);
- **equipamentos alternativos** (P5);
- slots temporários: mouse, pesquisa, guia e reforja (P6).

**Lixeira:** não é gravada (P7).

**Pesquisa da Jornada:** grava nomes persistentes, só de itens originais (P9). A pesquisa usa o `type`
(`ActionResearch`, `0x9D674C`).

**`.wld`:**

- baús (W1);
- **tile entities:** moldura (W2), suporte de armas (W3), prato de comida (W4), manequim (W5) e
  cabideiro (W6);
- sensor lógico, boneco de treino e pilone não guardam item (W7).

**NPC, mundo e eventos:**

- nenhum NPC guarda item no arquivo (W8);
- o cabeçalho e os eventos não têm ID de item (W13);
- itens no chão não são gravados (W15).

**Arquivos sem ID de item:** `.map` (P14), `config.json` (P15), conquistas (P13) e
`PersistentUserData.dat` (P12, listas fixas). As categorias do controle virtual são código morto (P16).

**Sincronização:**

- a nuvem está desligada (C1);
- o Wi-Fi copia `.plr`, `.map` e `.wld` crus (C2, C4) e manda `netID` int dos itens na memória para a
  prévia (C3).

**Rede** (fora do disco, mas leva IDs): mensagens 5, 21/90/145/148, 32, 86, 89, 121, 123, 124 e 133, e o
`NetCreativeUnlocksModule` (N1–N6, N14, N15).

### Pergunta 2. Para cada caminho: método, formato, o que acontece com ID ≥ 5456 e se o TLMod protege

Método, RVA e formato de cada caminho estão nas [tabelas 1.1 a 1.4](#1-tabela-de-todos-os-caminhos). Em
resumo:

- **Vira ar, sem erro e sem log.**
  - `.plr` (P1–P6), pelas guardas `0x1550` ou pelo `SetDefaults`.
  - `.wld`: baús e tile entities (W1–W6).
  - Rede: N1–N6.
  - O TLMod remonta o item só onde o número chega a `netDefaults` com o mod registrado.
- **Lança erro.**
  - No save de baú, se o **`type`** real for ≥ 5456 (`0x14F8728`). Não acontece com a casca.
  - Nos NetModules de pesquisa e bestiário (N14, N16): `KeyNotFoundException`, e o cliente desconecta.
- **"Corrompe".** O arquivo nunca fica estruturalmente corrompido: os dois saves só trocam o arquivo no
  fim (2.2). A corrupção possível é **de identidade**: num tile entity (W2–W6) ou na rede (N1–N6), o
  número de mod passa a apontar para outro item quando o conjunto de mods muda.
- **TLMod.**
  - **Protege:** P1–P6 e W1.
  - **Não protege:** os tile entities (W2–W6), a sincronização por Wi-Fi (C2, C4) e a rede (N1–N6).
  - **Protege em parte:** a pesquisa, só pela interface (P9).
  - **Nada a proteger:** P7 e P10–P16.

### Pergunta 3. NPCs e buffs

**NPCs.** O limite é 688 em todas as tabelas.

- Ao carregar o mundo, um tipo ≥ 688 lança exceção em três pontos: NPCs gravados (W8), NPCs brilhados
  (W9) e quartos (W11). O `LoadWorld` pega e marca `loadFailed`, e o mundo não abre.
- A contagem de abates (estandartes) grava 688 entradas fixas e descarta as de índice > 687 (W10).
- O bestiário grava strings (W12). Na rede, o NetModule lança `KeyNotFoundException` para ID
  desconhecido (N16).
- A mensagem 23 leva `netID` short e chama `SetDefaults` com ele (N7).
- **No TLMod hoje:**
  - nenhum desses estoura, porque o NPC de mod é gravado e enviado como a casca;
  - a identidade some ao recarregar o mundo;
  - abates, estandarte e bestiário vão para a casca (3.4).

**Buffs.** O limite é 355.

- **Save:** um buff ≥ 355 cancela **todo** save do personagem. No "Salvar e sair", deixa o mundo sem
  salvar e o jogo preso na tela de salvamento (4.1).
- **Load:** buffs ≥ 355 somem (4.2).
- **Rede:** as mensagens 53 e 55 lançam exceção (cliente desconecta). As mensagens 50 e 54 guardam o
  tipo cru e estouram depois (4.3).
- **Para o `register_buff`:** casca de buff mais o arquivo paralelo; o ID de mod nunca em `buffType`
  (4.4).

### Pergunta 4. Quais rodam fora da thread do jogo

**Fora da thread do jogo** (thread própria criada pelo `ThreadManager`, ou ThreadPool na renomeação):

- **com o jogo andando:**
  - o autosave do personagem (5 min) e o do mundo (10 min);
  - o save na morte do jogador (`KillMe`);
- **com o mundo parado:**
  - "Salvar e sair";
  - o save do personagem quando a rede cai (`InnerClientLoop`);
  - carregar mundo (`playWorld`, `serverLoadWorld`);
  - criar mundo;
  - renomear mundo.

Isso vale para todos os caminhos `.plr` (P1–P14) e `.wld` (W1–W14) que essas entradas disparam.

**Na thread do jogo:**

- carregar personagem pelo menu;
- criar e renomear personagem;
- recepção da rede (`ProcessData`, a partir de `Game.UnityUpdate`);
- sincronização por Wi-Fi (a partir de `GUISaveSynchronisation.Draw`).

O envio de rede (`SendData`) roda na thread de quem chama. Detalhes e RVAs na [seção 5](#5-threads).

---

## 9. Lacunas do TLMod, em ordem de risco

| # | lacuna | o que acontece | chance | risco | confiança |
|---|---|---|---|---|---|
| 1 | **Tile entities fora do arquivo paralelo** (W2–W6) | O `netID` de mod vai cru ao `.wld`. Com o mod fora, o item some ao carregar e o próximo save o apaga. Com outro conjunto de mods, vira outro item. Tudo em silêncio, sem linha no log. | alta: pôr item em moldura ou manequim é comum | **alto** | confirmado no binário (caminho); inferido (volta pelo hook) |
| 2 | **Tirar e devolver itens fora da thread do jogo, com o jogo andando** (autosaves, seção 5) | Durante o save, os itens de mod somem da mão, do equipamento e de todos os baús. A devolução confere e escreve sem trava com a thread do jogo. | certa a cada 5 e 10 min; a perda real exige uma corrida estreita | **alto** (efeito visível frequente); perda rara | confirmado no binário (threads e intervalos); só o aparelho confirma (efeito) |
| 3 | **`register_buff` sem casca** (P8, N10–N13) | Um buff de mod em `buffType` cancela todo save do personagem e, no "Salvar e sair", deixa o mundo sem salvar e o jogo preso. Na rede, desconecta. | só quando o recurso existir | **crítico para o recurso**; latente hoje | confirmado no binário |
| 4 | **Sincronização por Wi-Fi sem o arquivo paralelo** (C2–C5) | O personagem e o mundo chegam ao outro aparelho sem os itens de mod. Na volta, um `.plr` recebido herda o arquivo paralelo antigo do mesmo nome. | média: recurso visível no menu | **médio** | confirmado no binário (arquivos crus); só o aparelho confirma (volta) |
| 5 | **Rede sem tabela chave → ID** (N1–N16) | Cliente sem o TLMod vê ar. Cliente com outro conjunto de mods vê outro item. NetModules com ID desconhecido derrubam o cliente. | baixa: multiplayer local, já listado como não implementado | **médio** | confirmado no binário |
| 6 | **NPC de mod gravado e creditado como a casca** (W8, W10, W12, N7) | Perde a identidade ao recarregar, se a casca for salva com o mundo. Abates, estandarte e bestiário vão para a casca. | baixa hoje (cascas de chefe não são salvas) | **médio-baixo**; cresce com NPCs da cidade de mod | inferido |
| 7 | **Pesquisa protegida só pela interface** (P9, 2.4) | Se um item de mod chegar ao slot de pesquisa, `ActionResearch` o consome e credita a casca. | baixa | **baixo** | confirmado no binário (`ActionResearch` usa `type`); inferido (a interface bloqueia) |
| 8 | **Backups do jogo sem par no arquivo paralelo** (2.2) | O jogo guarda `.plr.bak` e `.wld.bak`. Se o jogador restaurar um backup, o arquivo paralelo é de outro momento: itens de mod de depois do backup reaparecem, ou falta o que só existia antes. | baixa | **baixo** | confirmado no binário (cópia `.bak`); inferido (efeito) |
| 9 | **README diz "ThreadPool"** (`modapi/README.md:537`, `:573`) | É `Thread` nova do `ThreadManager`. Só a renomeação de mundo usa o ThreadPool. | — | **baixo** (documentação) | confirmado no binário |

Não são lacunas:

- a lixeira (P7);
- `.map`, `config.json`, conquistas e `PersistentUserData.dat` (P12–P16);
- a nuvem (C1);
- a quebra estrutural de arquivo: os saves só trocam o arquivo no fim.

---

## 10. Ações recomendadas

Na ordem das lacunas.

1. **Cobrir os tile entities no arquivo paralelo do mundo.**
   - Contêineres novos: `item_frame`, `weapons_rack`, `food_platter`, `display_doll` (itens e corantes)
     e `hat_rack` (itens e corantes).
   - Identificados pela posição (x, y), como os baús. Os tile entities estão nos dicionários
     `TileEntity.ByPosition`/`ByID`.
   - Tirar antes do `InternalSaveWorld` e devolver depois, com o mesmo tratamento de órfãos.
   - Testes: no `fake_game.h`, um tile entity que grava `netID` cru e carrega por `netDefaults`; no
     aparelho, E1.
2. **Medir e reduzir a janela do autosave.**
   - Primeiro, E2: duração da janela e efeitos visíveis.
   - Se a janela for perceptível, pausar a thread do jogo enquanto os itens estão fora: uma barreira no
     frame handler do TLMod, com limite de tempo.
     - Começar pelo personagem, cujo save é curto.
     - No mundo, avaliar o custo, que cresce com o tamanho do mapa.
   - De todo modo, devolver o item só se o slot ainda for o mesmo objeto, com `type` da casca,
     `netID = 0` e `stack = 0`. Hoje basta "está vazio".
   - Corrigir o README (lacuna 9) junto.
3. **Desenhar o `register_buff` com casca** (4.4).
   - O ID de mod nunca vai a `buffType`/`buffTime`.
   - Troca em `Player.AddBuff` (`0xF8410C`) e `NPC.AddBuff` (`0xF4866C`).
   - Tirar e devolver no save do personagem, com o arquivo paralelo.
   - Na rede, só o ID da casca.
   - Teste no `fake_game.h` que reproduza o limite de `Main.buffNoSave` (como o
     `vanillaSetDefaultsOverflow` dos itens).
4. **Sincronização por Wi-Fi.**
   - Curto prazo: avisar na Oficina e no README que ela não leva os itens de mod.
   - Depois, estudar se `SaveSynchronisationOperation_SendFileList` (`0xAAEE1C`, `SendNextFile`) aceita
     um arquivo extra, `<arquivo>.tlmod.json`, e se o `ReceiveFile` o grava sem filtrar o nome.
5. **Rede, antes de liberar multiplayer com conteúdo de mod.**
   - A tabela chave → ID do servidor para o cliente, já prevista no README.
   - Ganchos em `NetCreativeUnlocksModule.Deserialize` (`0x8E6FAC`) e `NetBestiaryModule.Deserialize`
     (`0x8E6C50`) que descartem ID desconhecido em vez de deixar a `KeyNotFoundException` desconectar.
   - Até lá, documentar que multiplayer com mods de itens mostra ar ou outro item.
6. **NPCs de mod.**
   - Evitar cascas que sejam NPC da cidade ou estejam em `NPCID.Sets.SavesAndLoads` (o
     `register_npc` pode recusar).
   - Quando houver NPC da cidade de mod, um arquivo paralelo de NPCs: posição e casa → chave.
   - Documentar que abates, estandarte e bestiário vão para a casca, ou trocar o crédito em
     `NPCKillsTracker.RegisterKill` (`0x84F7B8`) e no estandarte.
7. **Pesquisa: defesa em profundidade.** Recusar em `GUIResearch.ActionResearch` (`0x9D674C`), ou em
   `ItemsSacrificedUnlocksTracker.RegisterItemSacrifice` (`0x8F7134`), quando o item do slot tiver
   `netID` de mod. Depois rodar E4.
8. **Backups.** Quando o jogo copiar `.plr` para `.plr.bak` (`0x104428C`) ou fizer o `.wld.bak`
   (`0x14F2038`), copiar também o arquivo paralelo para `<arquivo>.bak.tlmod.json`. A restauração usaria
   o par.
9. **Documentação.** Corrigir "ThreadPool" no README e acrescentar ao `terraria_offsets.h` os RVAs que
   esta auditoria usa e ele ainda não tem: tile entities, NetModules, autosave e `ActionResearch`.
