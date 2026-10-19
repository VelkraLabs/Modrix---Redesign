# Auditoria 07: mapa entre os ganchos do tModLoader e o Terraria Mobile

Terraria Mobile 1.4.4.9 (Unity 2021.3.56f2, IL2CPP, Android arm64), comparado com o TLMod da branch
`ccr-bbd92bf9-i01buk` (código da ponte e do runtime no commit `f917b04`; os commits seguintes só
acrescentaram auditorias e arquivos do jogo).

Auditoria só de leitura. As fontes são:

- a desmontagem da `libil2cpp.so` e o dump (`dump.cs`, `script.json`);
- o código público do tModLoader, ramo `1.4.4` (commit `130d914`, de 16/10/2026), feito para a mesma versão
  do Terraria que o mobile:
  - os patches sobre o código do jogo (`patches/tModLoader/Terraria/*.cs.patch`);
  - as classes dos ganchos e os carregadores (`ModPlayer.cs`, `PlayerLoader.cs` e afins), inclusive a
    documentação XML de cada gancho;
  - o ExampleMod;
- seis mods públicos de tModLoader, só para contar o uso de cada gancho;
- o código da ponte (`il2cpp_bridge/`), do runtime e da API (`modapi/`), o `terraria_offsets.h` e os
  READMEs;
- as auditorias 01 a 04 e 09.

Nenhum código do tModLoader ou de mod foi copiado para o repositório: dele saem só nomes de ganchos, nomes de
métodos do jogo e contagens. Dump, metadata e clones ficam fora do repositório. Nenhum código foi alterado;
este arquivo é o único acréscimo.

**Módulo 7:** mapa entre os ganchos do tModLoader e o Terraria Mobile.

1. Quais são os ganchos mais usados de `ModPlayer`, `ModItem`/`GlobalItem`, `ModNPC`/`GlobalNPC`,
   `ModProjectile`/`GlobalProjectile`, `ModBuff` e `ModSystem`?
2. Para cada um:
   - o método vanilla que o tModLoader altera;
   - o equivalente no binário mobile, com RVA;
   - a thread que o chama;
   - o que o TLMod já tem;
   - a dificuldade.
3. Como a API do TLMod deve nomear e organizar esses ganchos para ficar familiar a quem vem do tModLoader?
4. Quais ganchos destravam mais mods com menos esforço?

- [Como ler](#como-ler)
- [Resumo](#resumo)
- [1. Método e amostra](#1-método-e-amostra)
- [2. Os ganchos mais usados (pergunta 1)](#2-os-ganchos-mais-usados-pergunta-1)
- [3. A matriz (pergunta 2)](#3-a-matriz-pergunta-2)
- [4. Proposta da API (pergunta 3)](#4-proposta-da-api-pergunta-3)
- [5. Ordem por valor (pergunta 4)](#5-ordem-por-valor-pergunta-4)
- [6. Experimentos no aparelho](#6-experimentos-no-aparelho)
- [7. Respostas diretas](#7-respostas-diretas)
- [8. Lacunas do TLMod, em ordem de risco](#8-lacunas-do-tlmod-em-ordem-de-risco)
- [9. Ações recomendadas e ordem de implementação](#9-ações-recomendadas-e-ordem-de-implementação)

---

## Como ler

**Níveis de confiança**, os mesmos das auditorias anteriores:

- **confirmado no binário**: visto na desmontagem ou na metadata;
- **inferido**: decorre do que foi lido, mas um passo não foi rastreado;
- **só o aparelho confirma**: depende do que acontece em tempo de execução. Cada caso tem um experimento na
  [seção 6](#6-experimentos-no-aparelho).

Afirmações sobre código-fonte trazem arquivo e linha e valem como confirmadas no código. Na matriz, o método
vanilla vem dos patches do tModLoader (confirmado no código deles); o equivalente mobile e a RVA vêm do dump,
e os chamadores, da desmontagem (confirmados no binário). O que foge disso está marcado.

**Threads**, como nas auditorias 03 e 09: **P** é a principal do Unity (atualização e desenho); **G**, a de
geração de mundo; **C**, a de carregar mundo; **S**, a de save com o jogo andando; **Q**, a de sair do
multiplayer; **R**, a de rede do cliente; **H**, a de conversão do modo difícil (`WorldGen.smCallBack`); **L**,
a de carga do runtime do TLMod.

**Colunas da matriz:**

- **uso:** fração das classes daquela base, nos seis mods da amostra, que sobrescrevem o gancho, e em quantos
  dos seis ele aparece. Quando a linha junta vários ganchos, vem uma fração para cada um, e o número de mods
  é o do primeiro. Nas tabelas que juntam `Mod*` e `Global*`, o `G` marca a fração das classes `Global*`;
- **vanilla:** o método do Terraria de PC onde o tModLoader põe a chamada do gancho;
- **mobile:** o método equivalente no binário, com RVA. "Sufixo" e "prefixo" dizem se o gancho do TLMod
  rodaria depois ou antes do original;
- **TLMod hoje:** o que já existe, com arquivo e linha.

**Dificuldade:**

- **trivial:** o TLMod já engancha o método certo, ou já tem o callback; falta expor ao mod ou ampliar;
- **simples:** um gancho novo num método do jogo, que vale igual para conteúdo original e de mod;
- **casca:** funciona no modelo atual, em que o conteúdo de mod roda dentro de um item ou NPC original, mas o
  comportamento da casca vaza: efeitos e tabelas por tipo. Fica completo só com ID próprio;
- **ID próprio:** precisa de um tipo novo, além de `ItemID.Count` (5456), `NPCID.Count` (688),
  `ProjectileID.Count` (1022) ou `BuffID.Count` (355). Caminho estudado na auditoria 02;
- **inviável hoje:** depende de algo que o TLMod não tem nem tem caminho curto.

**Esforço:** dias de trabalho de uma pessoa que conhece a ponte e o runtime, com build e teste no aparelho.

---

## Resumo

1. **Os ganchos mais usados.** Contei os overrides de 11.201 classes em seis mods públicos: Calamity,
   Starlight River, Clicker Class, Terraria Overhaul, Infernum e Spirit.
   - **Itens e projéteis dominam:** 5.015 classes de item e 4.101 de projétil, contra 691 de NPC, 563 de buff
     e 175 de jogador.
   - **No jogador:** `ResetEffects` aparece em 51% das classes `ModPlayer`, nos seis mods.
   - **Nos NPCs:** `AI` (83%), `HitEffect` (78%), `FindFrame` (61%), `PreDraw` (55%), `ModifyNPCLoot` (50%) e
     `SpawnChance` (40%).
   - **Nos projéteis:** `AI` (85%), `PreDraw` (61%), `OnKill` (38%) e `OnHitNPC` (33%).
   - **Nos itens:** fora `SetDefaults`, `AddRecipes` e `SetStaticDefaults`, os campeões são `Shoot` (16%),
     `CanUseItem` (9%) e `UpdateAccessory` (7%).
   - **Pelos campos:** 22% dos itens disparam um projétil de mod, 24% colocam um bloco, 8% são acessórios e
     6% são peças de armadura. 60% dos NPCs de mod copiam a IA de um NPC original (`AIType`), justamente o
     modelo de casca do TLMod.
2. **O mobile tem quase todos os métodos que o tModLoader altera, com o mesmo nome.** Confirmado no binário.
   As diferenças que mudam o mapa:
   - `Player.ItemCheck_Inner` não existe: o corpo dele está dentro de `Player.ItemCheck` (`0xFE39A0`);
   - `Main.DrawProj_Inner` e `DrawProj_Inner_DoDrawProj` não existem: tudo fica em `Main.DrawProj`
     (`0xCD0D70`);
   - `WorldGen.GenerateWorld_RunTasksAndFinish` não existe: as etapas rodam dentro de
     `WorldGen.GenerateWorld` (`0x1306EF4`);
   - o banco de loot não tem regras por item. As bolsas abrem por `Player.OpenBossBag(int)` (`0xF8F774`), um
     `switch` fixo;
   - a interface de conversa, loja e buffs é própria do celular (`GUINPCDialogue`, `GUIShop`, `GUIBuffs`);
   - os blocos ficam num `TileData` (`Main.tile`), não no `Tilemap` do PC.
3. **Threads.** Quase todos os ganchos rodam na P, chamados de `Main.DoUpdateInWorld` (`0xCBBDF0`) ou do
   desenho. As exceções:
   - a geração de mundo roda na G;
   - os saves, na S, na C e na P;
   - o `OnEnterWorld` roda na P, porque `Player.Spawn` é chamado por `WorldGen.FinaliseOnMainThread`.
4. **O que o TLMod já cobre.**
   - **Por inteiro:** `AddRecipes`, `OnSpawn` de NPC de mod, `ModifyHurt`/`OnHurt` (o `Hurt` com antes e
     depois) e `Load`.
   - **Em parte:**
     - `SetDefaults` de item e NPC, com poucos campos;
     - `AI` de NPC de mod (`on_ai`);
     - save e load, só dos itens de mod;
     - desenho, só troca de textura;
     - tooltip fixo;
     - frame global.
   - **O resto falta.**
   - **Um efeito contrário:** o TLMod grava vida, dano e defesa fixos no NPC de mod depois do nascimento e
     apaga a escala de Expert e Mestre (`mod_loader.cpp:392–394`, depois de `NPC.ScaleStats`, `0xEDD4D8`).
5. **A matriz** tem 152 ganchos, em 101 linhas: uma linha junta os ganchos que entram no mesmo ponto. Por
   gancho:
   - 31 triviais e 92 simples;
   - 12 dependem da casca;
   - 5 dependem de ID próprio;
   - 12 inviáveis hoje: rede, camadas de interface do PC, botões de conversa, etapas de geração de mundo e
     teclas.
6. **API proposta.** As mesmas classes e nomes do tModLoader na camada C++: `ModPlayer::ResetEffects`,
   `ModItem::UpdateAccessory`, `ModNPC::AI` e assim por diante. Embaixo, uma ABI em C com uma tabela de
   callbacks por papel (`TLModPlayerHooks`, `TLModItemHooks`, `TLModNpcHooks`...), com `struct_size` como a
   `TLModHostApi`. Os nomes em C são os mesmos, em `snake_case`. Os ganchos recebem estado em struct, que o
   runtime lê antes e grava depois, como já faz com `TLModNpcState`.
7. **O que destrava mais mods com menos esforço**, em ordem:
   1. ampliar os campos de item e corrigir a escala dos NPCs de mod;
   2. o núcleo do `ModPlayer`;
   3. os ganchos de equipamento;
   4. os de acerto;
   5. os de uso de item;
   6. projéteis por casca.

   Juntos, destravam o grosso de itens, acessórios, armaduras e armas: de 11,5 a 18,5 dias.

**Ordem de implementação** (detalhe na [seção 9](#9-ações-recomendadas-e-ordem-de-implementação)):

| passo | o quê | esforço |
|---|---|---|
| 1 | campos de item ampliados e `GlobalItem.SetDefaults`; escala de dificuldade dos NPCs de mod | 1,5–2,5 dias |
| 2 | núcleo do `ModPlayer`: `ResetEffects`, `PreUpdate`, `PostUpdateEquips`, `PostUpdate`, `UpdateDead`, dados por jogador e atributos | 2–3 dias |
| 3 | equipamento: `UpdateAccessory`, `UpdateEquip`, `IsArmorSet`/`UpdateArmorSet`, `UpdateInventory` | 1–2 dias |
| 4 | acerto: `ModifyHitNPC`/`OnHitNPC` de item, projétil e jogador; origem no `Hurt`; `PreKill`/`Kill` | 2–3 dias |
| 5 | uso de item: `CanUseItem`, `UseItem`, `HoldItem`, `AltFunctionUse`, `ModifyWeaponDamage` | 1–2 dias |
| 6 | projéteis por casca e `Shoot`/`ModifyShootStats` | 4–6 dias |
| 7 | NPC: `HitEffect`, `OnKill`, `CheckDead`, `CheckActive`, `OnHitPlayer`, `SpawnChance`, `FindFrame` | 3–4 dias |
| 8 | loot declarativo e bolsas | 2–3 dias |
| 9 | dados livres no save; eventos de mundo e de entrada | 1–2 dias |
| 10 | `Global*` em conteúdo original, com filtro de tipo | 1–2 dias |
| 11 | desenho (`PreDraw`/`PostDraw`) e tooltips dinâmicos | 4–5 dias |
| 12 | `GlobalBuff`; `ModBuff` só com ID próprio | 1 dia + o caminho da auditoria 02 |

Juntos, de 23,5 a 35,5 dias até o passo 12, sem o `ModBuff` próprio.

---

## 1. Método e amostra

### 1.1 Onde o tModLoader chama cada gancho

Os patches do ramo `1.4.4` são diffs contra o código do Terraria 1.4.4 de PC. Para cada chamada
`PlayerLoader.X(`, `ItemLoader.X(`, `NPCLoader.X(`, `ProjectileLoader.X(`, `BuffLoader.X(`, `SystemLoader.X(`
ou `CombinedHooks.X(` acrescentada por um patch, localizei o método vanilla que a contém:

- pela assinatura no mesmo bloco do patch, quando havia;
- senão, pelas assinaturas antes e depois e pelo código de contexto.

Exemplo: o `PlayerLoader.UpdateEquips(this)` entra logo depois do laço `ApplyEquipFunctional` dos slots 3 a
9, que é `Player.UpdateEquips`. A documentação XML de cada gancho confirma o momento ("called at the very end
of the `Player.Update` method", "called right after all of this player's equipment and armor sets update").

O tModLoader renomeia alguns originais e põe o carregador no lugar:

- `NPC.AI` → `VanillaAI`;
- `NPC.FindFrame` → `VanillaFindFrame`;
- `NPC.HitEffect` → `VanillaHitEffect`;
- `Projectile.AI` → `VanillaAI`.

Na matriz aparece o nome do jogo.

### 1.2 O uso de cada gancho

**ExampleMod** do mesmo ramo (561 arquivos `.cs`), mais seis mods públicos de conteúdo, só lidos e contados:

| mod | repositório | ramo, commit | arquivos `.cs` |
|---|---|---|---:|
| Calamity | `CalamityTeam/CalamityModPublic` | `1.4.4`, `1a8cebd` (08/2026) | 7.780 |
| Starlight River | `ProjectStarlight/StarlightRiver` | `master`, `d367416` (08/2026) | 1.100 |
| Clicker Class | `SamsonAllen13/ClickerClass` | `master`, `61370a2` (09/2026) | 421 |
| Terraria Overhaul | `Mirsario/TerrariaOverhaul` | `dev`, `e202f47` (08/2026) | 387 |
| Infernum | `InfernumTeam/InfernumMode` | `master`, `399342f` (10/2026) | 1.456 |
| Spirit | `GabeHasWon/SpiritMod` | `future-release-content`, `7ef2993` (09/2026) | 3.495 |

**Como contei:**

- cada classe é seguida pela herança até a base do tModLoader, então uma espada que herda de uma
  `BaseSword : ModItem` conta como `ModItem`;
- conta-se o override só se o nome for um gancho virtual daquela base;
- classes com o mesmo nome em namespaces diferentes, como um item e o bloco que ele coloca, contam
  separadas, e o override de uma classe aninhada conta só para ela;
- uma classe conta uma vez por gancho.

**Classes por base:**

| base | Calamity | Starlight | Clicker | Overhaul | Infernum | Spirit | total | ExampleMod |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `ModPlayer` | 31 | 44 | 3 | 31 | 9 | 57 | 175 | 31 |
| `ModItem` | 2.685 | 535 | 185 | 7 | 99 | 1.504 | 5.015 | 209 |
| `GlobalItem` | 15 | 18 | 2 | 57 | 7 | 9 | 108 | 17 |
| `ModNPC` | 309 | 74 | 0 | 3 | 31 | 274 | 691 | 32 |
| `GlobalNPC` | 11 | 40 | 3 | 26 | 1 | 23 | 104 | 17 |
| `ModProjectile` | 2.246 | 353 | 93 | 2 | 606 | 801 | 4.101 | 69 |
| `GlobalProjectile` | 6 | 11 | 2 | 12 | 1 | 11 | 43 | 6 |
| `ModBuff` | 312 | 58 | 20 | 3 | 17 | 153 | 563 | 27 |
| `GlobalBuff` | 1 | 0 | 1 | 1 | 1 | 1 | 5 | 1 |
| `ModSystem` | 109 | 67 | 7 | 64 | 122 | 27 | 396 | 23 |

**Limites da estimativa:**

- O Calamity pesa metade das classes. Por isso a matriz traz a fração e o número de mods (de 0 a 6), não só
  o total.
- Contar overrides subestima ganchos usados por campo. Um item que só preenche `Item.shoot` dispara projétil
  sem sobrescrever `Shoot`. A [seção 2.2](#22-o-que-os-itens-e-npcs-declaram) mede os campos.
- O ExampleMod mostra todos os ganchos de propósito; serve de conferência, não de peso.
- Seis mods grandes e conhecidos não são uma amostra aleatória. Mods pequenos tendem a usar menos ganchos e
  os mesmos do topo (inferido).

### 1.3 O lado mobile

Para cada método vanilla, procurei o mesmo nome e a mesma assinatura no `script.json`. A thread vem dos
chamadores diretos (`bl`/`b`) na `libil2cpp.so` e das raízes de thread da auditoria 03. Exemplo: `Player.Update`
(`0xF5F9A8`) só é chamado por `Main.DoUpdateInWorld` (`0xCBBDF0`), que só é chamado por `Main.DoUpdate`, na P.

---

## 2. Os ganchos mais usados (pergunta 1)

### 2.1 Por classe

Os mais usados de cada base, com a fração das classes e o número de mods (de 6):

| base | ganchos mais usados |
|---|---|
| `ModPlayer` (175) | `ResetEffects` 51% (6), `PostUpdate` 27% (6), `PreUpdate` 17% (6), `UpdateDead` 14% (5), `PostUpdateMiscEffects` 10% (4), `PostUpdateEquips` 8% (5), `ModifyHurt` 7% (4), `SaveData`/`LoadData` 7% (6), `OnEnterWorld` 7% (5), `ModifyDrawInfo` 6% (5), `OnHitNPCWithProj` 6% (4), `PreUpdateMovement` 6% (4), `OnHurt` 6% (4), `ModifyHitNPCWithProj` 5% (6) |
| `ModItem` (5.015) | `SetDefaults` 82% (6), `AddRecipes` 52% (6), `SetStaticDefaults` 42% (5), `Shoot` 16% (5), `CanUseItem` 9% (6), `UpdateAccessory` 7% (6), `PostDrawInWorld` 6% (5), `UpdateEquip` 5% (4), `ModifyTooltips` 5% (5), `HoldoutOffset` 4% (4), `UseItem` 4% (5), `HoldItem` 4% (5), `AltFunctionUse` 3% (4) |
| `GlobalItem` (108) | `SetDefaults` 24% (4), `ModifyTooltips` 22% (6), `UseItem` 18% (6), `AppliesToEntity` 15% (5), `HoldItem` 12% (2), `CanUseItem` 7% (4), `UpdateInventory` 6% (5), `PreDrawInInventory` 6% (3) |
| `ModNPC` (691) | `SetDefaults` 97% (5), `SetStaticDefaults` 92% (4), `AI` 83% (5), `HitEffect` 78% (5), `SetBestiary` 68% (4), `FindFrame` 61% (4), `PreDraw` 55% (4), `ModifyNPCLoot` 50% (5), `SpawnChance` 40% (4), `SendExtraAI`/`ReceiveExtraAI` 31% (4), `OnHitPlayer` 27% (4), `PostDraw` 24% (3), `CheckActive` 22% (4), `ApplyDifficultyAndPlayerScaling` 18% (4), `CanHitPlayer` 15% (4), `OnKill` 14% (4) |
| `GlobalNPC` (104) | `PostAI` 25% (6), `SetDefaults` 21% (5), `OnKill` 20% (6), `AppliesToEntity` 20% (4), `ResetEffects` 16% (5), `PreAI` 15% (5), `OnHitByProjectile` 13% (6), `PostDraw` 13% (4), `PreDraw` 12% (5), `AI` 11% (4), `ModifyHitByProjectile` 11% (5), `ModifyShop` 11% (4), `UpdateLifeRegen` 11% (4) |
| `ModProjectile` (4.101) | `SetDefaults` 94% (6), `AI` 85% (5), `PreDraw` 61% (5), `SetStaticDefaults` 59% (5), `OnKill` 38% (5), `OnHitNPC` 33% (5), `CanDamage` 16% (4), `Colliding` 15% (4), `GetAlpha` 15% (5), `OnHitPlayer` 13% (4), `OnTileCollide` 12% (5), `SendExtraAI` 12% (5), `ModifyHitNPC` 10% (4) |
| `GlobalProjectile` (43) | `AppliesToEntity` 37% (5), `PreAI` 26% (5), `OnHitNPC` 26% (4), `AI` 21% (4), `OnKill` 21% (4), `OnSpawn` 21% (5), `ModifyHitNPC` 16% (5) |
| `ModBuff` (563) | `SetStaticDefaults` 71% (6), `Update` 71% (6), `ModifyBuffText` 3% (4) |
| `ModSystem` (396) | `Load` 38% (6), `Unload` 26% (6), `OnModLoad` 26% (5), `PostUpdateEverything` 12% (5), `SaveWorldData`/`LoadWorldData` 8% (4), `PostSetupContent` 7% (5), `ClearWorld` 4% (5), `UpdateUI` 4% (6), `PreUpdateWorld` 4% (5), `ModifyInterfaceLayers` 3% (6), `ModifyWorldGenTasks` 2% (5) |

Ficam de fora os ganchos de infraestrutura (`Load`, `Unload`, `Initialize`, `Clone`, `IsLoadingEnabled`), menos
em `ModSystem`, onde `Load` e `Unload` são o próprio uso. Em `ModPlayer`, `Load` aparece em 17% das classes e
`Initialize` em 6%.

### 2.2 O que os itens e NPCs declaram

A mesma amostra, pelos campos atribuídos no corpo das classes:

| base | campo | fração |
|---|---|---:|
| `ModItem` | dano maior que zero (arma) | 25,8% |
| `ModItem` | `Item.shoot` com projétil de mod | 22,3% |
| `ModItem` | `createTile` (coloca bloco) | 24,1% |
| `ModItem` | `consumable` | 11,8% |
| `ModItem` | `accessory` | 8,2% |
| `ModItem` | `mana` | 8,0% |
| `ModItem` | `defense` (armadura) | 5,9% |
| `ModItem` | `buffType` | 2,7% |
| `ModNPC` | `AIType` (copia a IA de um original) | 60,3% |
| `ModNPC` | `Banner` | 37,8% |
| `ModNPC` | `aiStyle` original | 33,1% |
| `ModNPC` | `boss` | 10,1% |
| `ModProjectile` | `aiStyle` original | 9,1% |

**Leitura:**

- Projétil de mod é tão comum quanto item: 4.101 classes contra 5.015, e um item em cada quatro dispara um.
- Os blocos (24% dos itens) ficam fora da lista do enunciado, mas pesam: um `ModTile` é o próximo módulo
  natural.
- O modelo de casca serve bem aos NPCs, porque a maioria já pede emprestada a IA de um original. Serve mal
  aos projéteis: 91% têm IA própria.

### 2.3 As categorias do enunciado

| categoria | ganchos que a cobrem |
|---|---|
| IA | `ModNPC.AI`/`PreAI`/`PostAI`, `ModProjectile.AI`/`PreAI`/`PostAI`, `GlobalNPC` e `GlobalProjectile` |
| ao acertar | `ModProjectile.OnHitNPC`, `ModPlayer.OnHitNPCWithProj`/`WithItem`, `ModItem.OnHitNPC`, `GlobalNPC.OnHitByProjectile`, `ModNPC.OnHitPlayer` |
| modificar dano | `ModProjectile.ModifyHitNPC`, `ModPlayer.ModifyHitNPCWithProj`/`ModifyHurt`, `GlobalNPC.ModifyHitByProjectile`/`ModifyIncomingHit`, `ModifyWeaponDamage` |
| ao usar item | `ModItem.UseItem`, `GlobalItem.UseItem`, `HoldItem` |
| pode usar item | `ModItem.CanUseItem`, `GlobalItem.CanUseItem` |
| disparo | `ModItem.Shoot`, `ModifyShootStats`, `GlobalItem.Shoot` |
| efeito de acessório | `ModItem.UpdateAccessory`, `GlobalItem.UpdateAccessory` |
| atualizar equipamentos | `ModItem.UpdateEquip`, `IsArmorSet`/`UpdateArmorSet`, `ModPlayer.ResetEffects`/`PostUpdateEquips` |
| ao morrer | `ModNPC.OnKill`/`PreKill`/`CheckDead`, `GlobalNPC.OnKill`, `ModProjectile.OnKill`, `ModPlayer.Kill`/`PreKill` |
| loot | `ModNPC.ModifyNPCLoot`, `GlobalNPC.ModifyNPCLoot`/`ModifyGlobalLoot`, `ModItem.ModifyItemLoot`, `BossLoot` |
| desenho | `ModProjectile.PreDraw`, `ModNPC.PreDraw`/`PostDraw`/`FindFrame`, `ModItem.PostDrawInWorld`/`PreDrawInInventory`, `ModPlayer.ModifyDrawInfo` |

---

## 3. A matriz (pergunta 2)

### 3.1 `ModPlayer`

| gancho | uso | vanilla | mobile (RVA) | thread | TLMod hoje | dificuldade |
|---|---|---|---|---|---|---|
| `ResetEffects` | 51% · 6/6 | `Player.ResetEffects`, no fim | `Player.ResetEffects` `0xFBD68C` | P; também jogadores falsos do desenho ([3.7](#37-o-que-o-mobile-muda-no-mapa)) | nada | simples |
| `PostUpdate` | 27% · 6/6 | `Player.Update`, no fim | sufixo de `Player.Update` `0xF5F9A8` | P | só o frame global | simples |
| `PreUpdate` | 17% · 6/6 | `Player.Update`, depois de `UpdateMiscCounter` | prefixo de `Player.Update` `0xF5F9A8` | P | só o frame global | simples |
| `UpdateDead` | 14% · 5/6 | `Player.UpdateDead` | `Player.UpdateDead` `0xFB783C` | P | nada | simples |
| `PostUpdateMiscEffects` | 10% · 4/6 | `Player.Update`, antes de `UpdateLifeRegen` | prefixo de `Player.UpdateLifeRegen` `0xFBE3DC` (só `Update` chama) | P | nada | simples |
| `PostUpdateEquips` | 8% · 5/6 | `Player.Update`, depois dos equipamentos e conjuntos | sufixo de `Player.UpdateArmorSets` `0xFB00DC` (só `Update` chama, logo depois de `UpdateEquips`) | P | nada | simples |
| `ModifyHurt`, `OnHurt`, `FreeDodge` | 7% / 6% / 2% · 4/6 | `Player.Hurt` | `Player.Hurt` `0xFA15B8` | P | existe: `subscribe_player_hurt`, antes (muda o dano, cancela) e depois (`bridge.cpp:373`) | trivial |
| `SaveData`, `LoadData` | 7% · 6/6 | `Player.InternalSavePlayerFile`, `Player.LoadPlayer` | `0x1044040`, `0x1049FA8` | S, P, R; P, Q | os ganchos existem (`save_hooks.cpp:433`, `:449`), só para itens de mod | trivial |
| `OnEnterWorld` | 7% · 5/6 | `Player.Hooks.EnterWorld` | `Player.Hooks.EnterWorld` `0x11D8D6C`, via `Player.Spawn` `0xFB7F3C` | P | parcial: `in_world` lido no frame | trivial |
| `OnHitNPCWithProj`, `ModifyHitNPCWithProj` | 6% · 4/6; 5% · 6/6 | `Projectile.Damage` | `Projectile.Damage` `0x1110080` | P | nada | simples |
| `ModifyDrawInfo` | 6% · 5/6 | `PlayerDrawSet.BoringSetup` | `PlayerDrawSet.BoringSetup` `0x158ED74` | P | parcial: troca a textura do item na mão (`draw_hooks.cpp:508`) | simples |
| `PreUpdateMovement` | 6% · 4/6 | `Player.Update`, entre o efeito do `vortexDebuff` e a colisão de movimento | meio de `Player.Update` (`0xF6A020`); por função, prefixo de `DryCollision` `0xFD4408`, `WaterCollision` `0xFD429C`, `HoneyCollision` `0xFD4130` e `ShimmerCollision` `0xFD3DAC`, que só `Update` chama | P | nada | simples |
| `OnHitNPCWithItem`, `ModifyHitNPCWithItem` | 5% · 3/6; 3% · 4/6 | `Player.ProcessHitAgainstNPC` | `Player.ProcessHitAgainstNPC` `0x1033D14` (só `ItemCheck_MeleeHitNPCs` chama) | P | nada | simples |
| `PreKill`, `Kill` | 4% / 3,4% · 4/6 | `Player.KillMe` | `Player.KillMe` `0xFBF6BC` (9 chamadores, inclusive `Hurt`, afogamento e dano contínuo) | P | nada; a RVA já está em `terraria_offsets.h:524`, sem gancho | simples |
| `ModifyHitByNPC`, `OnHitByNPC` | 3% · 4/6 | `Player.Update_NPCCollision` | `Player.Update_NPCCollision` `0xFE1314`, que chama `Hurt` | P | parcial: o `Hurt` vê o golpe, não o NPC | trivial |
| `UpdateBadLifeRegen`, `UpdateLifeRegen` | 3% / 2% · 4/6 | meio de `Player.UpdateLifeRegen` | `Player.UpdateLifeRegen` `0xFBE3DC` | P | nada | simples |
| `ProcessTriggers` | 3% · 4/6 | entrada de teclado | não há teclas configuráveis no celular | P | botões do painel e da HUD, `register_command` | inviável hoje |
| `ModifyHitNPC`, `OnHitNPC` (qualquer origem) | 2% / 2% · 4/6 | `Player.ApplyDamageToNPC` e os dois acima | `Player.ApplyDamageToNPC` `0xFA1268` (9 chamadores); todos terminam em `NPC.StrikeNPC` `0xF476A8` | P | nada | simples |
| `ModifyWeaponDamage` | 2% · 2/6 | `Player.GetWeaponDamage` | `Player.GetWeaponDamage` `0x101CA70` | P | nada | simples |

**Notas:**

- O `ModifyHitByNPC` é "trivial" porque o `Player.Hurt` já recebe o `PlayerDeathReason`, que traz o índice
  do NPC de origem. Basta passá-lo ao evento (`TLModPlayerHurt` não tem esse campo, `tlmod_api.h:248–255`).
- O tModLoader insere `UpdateLifeRegen` no meio do método. Um prefixo só aproxima, se o jogo não zerar
  `lifeRegen` depois. Só o aparelho confirma ([E4](#e4-regeneração-por-prefixo)).
- **`PreUpdateMovement`** entra logo antes do teste de `tongued`: no mobile, `ldrb w8, [x19, #0x82b]` em
  `0xF6A020`, depois do bloco do `vortexDebuff` (`ldrb w8, [x19, #0x85c]` em `0xF69F90`). Não há chamada de
  método nesse ponto. O prefixo dos quatro métodos de colisão chega ao mesmo lugar, menos com o jogador preso
  pela língua (`tongued`). Como `DryCollision` tem três chamadas, o gancho precisa de uma marca por tick.
  Confirmado no binário.
- **`Initialize` e `Load`** ficam fora da matriz: são de infraestrutura. O bloco de dados por jogador, zerado
  na criação, e o `tlmod_mod_init` fazem esse papel ([4.1](#41-princípios)).

### 3.2 `ModItem` e `GlobalItem`

| gancho | uso | vanilla | mobile (RVA) | thread | TLMod hoje | dificuldade |
|---|---|---|---|---|---|---|
| `SetDefaults` | 82% · 6/6; G 24% | `Item.SetDefaults(int, bool, ItemVariant)`, no fim | `0xBAD034` | P, G, C, S, Q | existe para item de mod: o `TLModItemDef` é aplicado depois do original (`item_hooks.cpp:277`), mas só com nome, tooltip, textura, tamanho, pilha, valor, raridade, dano corpo a corpo e tempo de uso; `GlobalItem`: nada | trivial |
| `AddRecipes` | 52% · 6/6 | `Recipe.SetupRecipes`, no fim | `0x122ECB8` | P | existe: `register_recipe` (`recipe_hooks.cpp:244`) | trivial |
| `SetStaticDefaults` | 42% · 5/6; G 4% | tabelas por tipo, na carga | `ItemID.Sets` (5456 posições) | P | a casca define | ID próprio |
| `Shoot`, `ModifyShootStats` | 16% / 2% · 5/6; G 6% / 5% | `Player.ItemCheck_Shoot` | `0xF7051C` | P | nada: a casca dispara o projétil dela | casca |
| `CanUseItem` | 9% · 6/6; G 7% | `Player.ItemCheck_CheckCanUse` | `0xF851F0` (`ItemCheck` e os seis `Quick*`) | P | nada | simples |
| `UpdateAccessory` | 7% · 6/6; G 4% | `Player.ApplyEquipFunctional` | `0xFA94E8` (só `UpdateEquips` chama) | P | nada; os efeitos da casca também valem, porque o jogo escolhe pelo tipo | casca |
| `PostDrawInWorld`, `PreDrawInWorld` | 6% / 2% · 5/6 | `Main.DrawItem` | `0xCFA928` | P | troca de textura (`draw_hooks.cpp:487`) | simples |
| `UpdateEquip` | 5% · 4/6; G 3% | `Player.GrantArmorBenefits` | `0xFA75C8` (só `UpdateEquips` chama) | P | nada; a defesa é a da casca (o `TLModItemDef` não tem `defense`) | casca |
| `ModifyTooltips` | 5% · 5/6; G 22% | `Main.MouseText_DrawItemTooltip` | `Main.MouseText_DrawItemTooltip_GetLinesInfo` `0xCC90D8` | P | tooltip fixo; o TLMod já engancha o método (`screen_hooks.cpp:418`) | simples |
| `HoldoutOffset`, `UseStyle` | 4% / 3% · 4/6 | desenho do item na mão; `Player.ItemCheck_ApplyUseStyle` | `PlayerDrawLayers.DrawPlayer_27_HeldItem` `0x15C0EDC`; `ItemCheck_ApplyUseStyle` `0x101F058` | P | troca de textura na mão | simples |
| `UseItem` | 4% · 5/6; G 18% | `Player.ItemCheck_Inner` | `Player.ItemCheck` `0xFE39A0` (sem `ItemCheck_Inner` no mobile) | P | nada | simples |
| `HoldItem` | 4% · 5/6; G 12% | `Player.ItemCheck_Inner` | `Player.ItemCheck` `0xFE39A0` | P | nada | simples |
| `AltFunctionUse` | 3% · 4/6; G 5% | `Player.ItemCheck_ManageRightClickFeatures` | `0xFE210C` (só `Update` chama) | P | nada | simples |
| `IsArmorSet`, `UpdateArmorSet` | 2% · 4/6 | `Player.UpdateArmorSets`, no fim | `0xFB00DC` | P | nada | casca |
| `CanRightClick`, `RightClick` | 2% / 0,8% · 5/6; G 3% | `ItemSlot.RightClick` | `ItemSlot.RightClick` `0x16284C4` e `0x162C410`; `ItemSlot.TryOpenContainer` `0x162C754` | P | nada | simples |
| `PreDrawInInventory` | 2% · 4/6; G 6% | desenho do slot | `ItemSlot.DrawItemIcon` `0x1632310`, `GUIInventory.DrawItem` `0x95E6E4` | P | troca de textura (`draw_hooks.cpp:465`, `:496`) | simples |
| `OnHitNPC`, `ModifyHitNPC` (golpe do item) | 1,4% / 0,1% · 3/6; G 4% / 5% | `Player.ProcessHitAgainstNPC` | `0x1033D14` | P | nada | simples |
| `UpdateInventory` | 1,4% · 3/6; G 6,5% | `Player.UpdateEquips`, laço do inventário | `0xFA6BC8` | P | nada | simples |
| `ModifyItemLoot` | 1,2% · 5/6; G 6% | regras por item do banco de loot | não há regras por item ([3.7](#37-o-que-o-mobile-muda-no-mapa)); bolsas: `Player.OpenBossBag(int)` `0xF8F774` | P | nada | casca |
| `PostUpdate`, `Update` (item no chão) | 1,2% / 1,1% · 5/6 | `Item.UpdateItem`, `Item.MoveInWorld` | `0xBB047C`, `0xBB1B70` | P | nada | simples |
| `GetAlpha` | 1% · 5/6 | `Item.GetAlpha` | `0xBADECC` | P | nada | simples |
| `SaveData`, `LoadData` | 0,9% · 4/6 | saves do jogador e do mundo | ganchos de save do TLMod (`0x1044040`, `0x1049FA8`, `0x14F1AC8`, `0x14EC048`) | S, C, P | os itens de mod vão para o arquivo paralelo: tipo, pilha, prefixo | trivial |
| `OnPickup` | 0,6% · 4/6; G 5% | `Player.GrabItems` | `0xFF5DBC` (só `Update` chama) | P | nada | simples |
| `ModifyWeaponDamage` | 0,3% · 3/6; G 2% | `Player.GetWeaponDamage` | `0x101CA70` | P | nada | simples |

**Notas:**

- **`Shoot` é "casca"** porque o tipo do projétil vem do item original: o `TLModItemDef` não tem `shoot`. O
  gancho em si é simples. O que falta é haver projétil de mod para disparar ([3.4](#34-modprojectile-e-globalprojectile)).
- **`UpdateAccessory` e `UpdateEquip`:** o `switch` de `ApplyEquipFunctional` e de `GrantArmorBenefits` é
  pelo tipo, que é o da casca. A casca recomendada para acessório (216, `tlmod_api.h:104`) não pode ter
  efeito próprio. O TLMod já avisa no log quando a casca não combina.
- **Bolsas:** para uma bolsa de mod, o caminho é o `RightClick` soltando os itens pelo próprio TLMod. Para
  uma bolsa original, um gancho simples em `OpenBossBag`.
- **`UseItem` e `HoldItem`:** o corpo do `ItemCheck_Inner` do PC está dentro de `Player.ItemCheck`. Ele chama
  `ItemCheck_StartActualUse` (`0xFE41FC`), `ItemCheck_ApplyHoldStyle` (`0xFE4244`) e
  `ItemCheck_EmitHeldItemLight` (`0xFE4258`). Confirmado no binário.

### 3.3 `ModNPC` e `GlobalNPC`

| gancho | uso | vanilla | mobile (RVA) | thread | TLMod hoje | dificuldade |
|---|---|---|---|---|---|---|
| `SetDefaults` | 97% · 5/6; G 21% | `NPC.SetDefaults`, no fim | `0xDAF1BC` | P, G, C | existe para NPC de mod: `TLModNpcDef`. O gancho roda antes do original (`npc_hooks.cpp:289`), e os valores entram no nascimento; `GlobalNPC`: nada | trivial |
| `SetStaticDefaults` | 92% · 4/6 | tabelas por tipo, na carga | `NPCID.Sets`, `Main.npcFrameCount` (688 posições) | P | a casca define | casca |
| `AI`, `PreAI`, `PostAI` | 83% / 8% / 1% · 5/6; G 11% / 15% / 25% | `NPC.AI` (renomeado `VanillaAI`) | `NPC.AI` `0xDCE54C` (só `NPC.UpdateNPC` chama) | P | existe: `on_ai` antes, que pode substituir a IA da casca (`npc_hooks.cpp:303`) | trivial |
| `HitEffect` | 78% · 5/6; G 5% | `NPC.HitEffect` (renomeado `VanillaHitEffect`) | `0xE9BAE4` (41 chamadas: `AI`, `StrikeNPC`, rede) | P | nada | simples |
| `SetBestiary` | 68% · 4/6 | `BestiaryDatabaseNPCsPopulator` | `BestiaryDatabaseNPCsPopulator.Populate` `0x813160` | P | nada (auditoria 04) | ID próprio |
| `FindFrame` | 61% · 4/6 | `NPC.FindFrame` (renomeado `VanillaFindFrame`) | `0xE63FA8` (`UpdateNPC`; também ícones do bestiário e créditos) | P | parcial: animação automática da textura (`frame`, `frame_ticks`) | simples |
| `PreDraw`, `PostDraw` | 55% / 24% · 4/6; G 12% / 13% | `Main.DrawNPCDirect` | `Main.DrawNPCDirect` de 6 argumentos `0xBFC154`; a cópia de 4 (`0xBCB734`) é do bestiário | P | troca de textura e quadro (`draw_hooks.cpp:517`) | simples |
| `ModifyNPCLoot` | 50% · 5/6; G 8% | `ItemDropDatabase.Populate` (o tModLoader monta as regras ali) | `Populate` `0x7D8E00`, `RegisterToNPC` `0x7D86DC`, `ItemDropRule.Common` `0x7CDA10` | P (início) | nada: o NPC de mod solta o loot da casca (auditoria 04) | ID próprio |
| `SpawnChance`, `EditSpawnPool` | 40% · 4/6; G 5% | `NPC.SpawnNPC` (o tModLoader sorteia entre os candidatos) | `NPC.SpawnNPC` `0xE77CD0` (`Main.DoUpdateInWorld`) | P | parcial: `spawn_npc_near_player`, com o mod decidindo quando | simples |
| `SendExtraAI`, `ReceiveExtraAI` | 31% · 4/6 | `NetMessage.SendData` (mensagem 23) | — | P, R | nada; `local[]` não vai pela rede (`tlmod_api.h:145–146`) | inviável hoje |
| `OnHitPlayer`, `CanHitPlayer` | 27% / 15% · 4/6; G 4% | `Player.Update_NPCCollision` | `0xFE1314` | P | nada | simples |
| `CheckActive` | 22% · 4/6 | `NPC.CheckActive` | `0xF368DC` (só `UpdateNPC` chama) | P | nada | simples |
| `ApplyDifficultyAndPlayerScaling` | 18% · 4/6 | `NPC.ScaleStats` | `0xEDD4D8`, chamado dentro de `NPC.SetDefaults` (`0xDB4610`) | P | contrário: o TLMod grava vida, dano e defesa fixos depois e apaga a escala (`mod_loader.cpp:392–394`) | simples |
| `OnKill`, `PreKill` | 14% / 4,3% · 4/6; G 20% | `NPC.NPCLoot` | `0xF38944` (`checkDead`) | P | nada | simples |
| `DrawHealthBar` | 11% · 4/6 | `Main.DrawInterface_14_EntityHealthBars` | `0xD19550` | P | nada | simples |
| `CanBeHitByProjectile`, `CanBeHitByItem` | 8,5% / 3,3% · 4/6 | `Projectile.Damage` e `Player.ProcessHitAgainstNPC` (o tModLoader junta os testes em `CombinedHooks`) | `0x1110080`, `0x1033D14` | P | nada | simples |
| `GetAlpha` | 8% · 4/6 | `NPC.GetAlpha` | `0xF571CC` | P | nada | simples |
| `CheckDead` | 7% · 4/6 | `NPC.checkDead` | `0xF08F24` | P | nada | simples |
| `BossLoot` | 6% · 4/6 | `NPC.DoDeathEvents_DropBossPotionsAndHearts` | `0xF3D9CC` | P | nada | simples |
| `OnSpawn` | 5% · 4/6; G 6% | `NPC.NewNPC` | `0xEE4630` | P, G | existe para NPC de mod: `on_spawn` (`npc_hooks.cpp:266`) | trivial |
| `ModifyHitByProjectile`, `OnHitByProjectile` | 3,5% / 2% · 4/6; G 11% / 13,5% | `Projectile.Damage` | `0x1110080` | P | nada | simples |
| `ModifyHitByItem`, `OnHitByItem` | 2% / 2% · 3/6; G 6% / 7% | `Player.ProcessHitAgainstNPC` | `0x1033D14` | P | nada | simples |
| `ModifyIncomingHit` | G 8% · 4/6 | `NPC.StrikeNPC` (o tModLoader extrai `GetIncomingStrikeModifiers`) | `NPC.StrikeNPC` `0xF476A8` | P | nada | simples |
| `ResetEffects`, `UpdateLifeRegen` | G 16% / 11% · 5/6 | `NPC.UpdateNPC_BuffFlagsReset`, `UpdateNPC_BuffApplyDOTs` | `0xF4CBD8`, `0xF4D5F4` | P | nada | simples |
| `EditSpawnRate` | G 7% · 4/6 | meio do laço de `NPC.SpawnNPC` | estáticos `NPC.defaultSpawnRate` (`0x78`) e `defaultMaxSpawns` (`0x7C`) | P | nada | simples |
| `GetChat`, `ModifyShop` | 3% · 3/6; G 11% | `NPC.GetChat`, `Chest.SetupShop` | `0xF57E4C`, `0xB42618` | P | nada | simples |
| `SetChatButtons`, `OnChatButtonClicked` | 2% · 2/6 | conversa do PC (`Main.GUIChatDrawInner`) | a conversa é o `GUINPCDialogue`, só do mobile | P | nada | inviável hoje |

**Notas:**

- **`AI`:** 60% dos NPCs de mod copiam a IA de um original. O `on_ai` sobre a casca cobre esse uso.
  `PostAI` é um sufixo no mesmo gancho. Um `GlobalNPC.AI` para todos os NPCs custa até 12.000 chamadas por
  segundo (auditoria 03).
- **`ModifyNPCLoot` é "ID próprio"** para NPC de mod, porque o banco guarda regras por `netID`, e o `netID` do
  NPC de mod é o da casca. Para um NPC original ganhar um item original, é simples:
  `ItemDropRule.Common` mais `RegisterToNPC` depois do `Populate`. Para um item de mod, o TLMod precisa
  soltar pelo próprio caminho (casca e conversão): o `give_item` faz assim (`bridge_adapter.cpp:562`), e o
  `Item.NewItem` não passa por gancho que conheça IDs de mod (inferido).
- **`EditSpawnRate`:** o tModLoader ajusta a taxa por jogador, no meio do laço. No mobile só dá para
  aproximar com os estáticos de base, que valem para todos os jogadores.
- **`SetChatButtons`:** os botões são da interface do celular. Exige estudar o `GUINPCDialogue`; o mapa do
  tModLoader não ajuda.

### 3.4 `ModProjectile` e `GlobalProjectile`

| gancho | uso | vanilla | mobile (RVA) | thread | TLMod hoje | dificuldade |
|---|---|---|---|---|---|---|
| `SetDefaults` | 94% · 6/6; G 14% | `Projectile.SetDefaults`, no fim | `0x1067854` | P | nada: não há API de projétil | casca |
| `AI`, `PreAI`, `PostAI` | 85% / 5% / 1,5% · 5/6; G 21% / 26% / 14% | `Projectile.AI` (renomeado `VanillaAI`) | `0x1070668` (`Projectile.Update`; e uma vez no `SetDefaults`) | P | nada | casca |
| `PreDraw`, `PostDraw` | 61% / 4% · 5/6; G 7% | `Main.DrawProj_Inner_DoDrawProj` | `Main.DrawProj` `0xCD0D70` (sem `DrawProj_Inner` no mobile) | P | nada | simples |
| `OnKill`, `PreKill` | 38% / 0,4% · 5/6; G 21% | `Projectile.Kill` | `0x10C70A4` (244 chamadas) | P; em tese G, C, H (auditoria 03) | nada | simples |
| `OnHitNPC`, `ModifyHitNPC` | 33% / 10% · 5/6; G 26% / 16% | `Projectile.Damage` | `0x1110080` | P | nada | simples |
| `CanDamage`, `Colliding`, `CanHitNPC` | 16% / 15% / 5% · 4/6 | `Projectile.Damage`, `Projectile.Colliding` | `0x1110080`, `0x1119FDC` | P | nada | simples |
| `GetAlpha` | 15% · 5/6 | `Projectile.GetAlpha` | `0x11B9050` | P | nada | simples |
| `OnHitPlayer` | 13% · 4/6 | `Projectile.Damage`, golpe em jogador | `0x1110080`, que chama `Player.Hurt` | P | parcial: o `Hurt` vê o golpe | simples |
| `OnTileCollide`, `TileCollideStyle` | 12% / 2,4% · 5/6; G 9% | `Projectile.HandleMovement` | `0x1124814` | P | nada | simples |
| `SendExtraAI`, `ReceiveExtraAI` | 12% · 5/6 | `NetMessage.SendData` (mensagem 27) | — | P, R | nada | inviável hoje |
| `ShouldUpdatePosition` | 4% · 5/6 | `Projectile.UpdatePosition` (o tModLoader retorna antes de somar a velocidade) | `0x112CB1C` (só `HandleMovement` chama, duas vezes) | P | nada | simples |
| `OnSpawn` | 4% · 5/6; G 21% | `Projectile.NewProjectile` | `0x1108E7C` e `0x1108DA0` (as duas sobrecargas com origem) | P | nada | simples |

**Notas:**

- **Projétil por casca:** o mesmo modelo dos NPCs. Um projétil original faz de casca: tipo, tabelas e colisão
  dele. O mod troca a textura no `DrawProj` e assume a IA num `on_ai` antes do original. Como 91% dos
  projéteis de mod têm IA própria, a casca serve de recipiente, não de comportamento: escolher uma com IA
  simples (uma flecha, por exemplo).
- **`Projectile.SetDefaults` chama a IA:** ele salta para `Projectile.AI` uma vez (`b` em `0x106F668`). Um
  gancho de IA precisa aceitar a chamada antes de o projétil estar no mundo
  ([E5](#e5-projétil-por-casca)).
- **`OnTileCollide`:** o tModLoader decide no meio do método se o projétil morre. No mobile só dá para
  aproximar, comparando a velocidade antes e depois de `HandleMovement`.

### 3.5 `ModBuff` e `GlobalBuff`

| gancho | uso | vanilla | mobile (RVA) | thread | TLMod hoje | dificuldade |
|---|---|---|---|---|---|---|
| `SetStaticDefaults` | 71% · 6/6 | nome, descrição e tabelas por tipo | `Lang.GetBuffName` `0x11F5714`, `Lang.GetBuffDescription` `0x11F5798`, `Main.debuff[]` (355 posições) | P | nada | ID próprio |
| `Update` (jogador) | 71% · 6/6; `GlobalBuff` 3 de 5 | `Player.UpdateBuffs`, por buff | `0xF9BD80` (só `Update` chama) | P | nada | ID próprio |
| `Update` (NPC) | o mesmo gancho | `NPC.UpdateNPC_BuffSetFlags` | `0xF4CC0C` | P | nada | simples |
| `ModifyBuffText` | 3% · 4/6; `GlobalBuff` 2 de 5 | desenho da barra de buffs | `Lang.GetBuffName`/`GetBuffDescription`; a barra é o `GUIBuffs` (`GUIBuffs.Draw` `0x14BA3EC`) | P | nada | simples |
| `ReApply` | 2% · 1/6 | `Player.AddBuff`, `NPC.AddBuff` | `0xF8410C`, `0xF4866C` | P | nada | simples |

**Nota.** Um `ModBuff` de verdade precisa de ID acima de 354: as tabelas por tipo e a textura da barra têm
355 posições. Por isso `SetStaticDefaults` e `Update` do jogador ficam "ID próprio". Dois ganchos valem para
buffs originais sem ID próprio: `Update` no NPC e `ModifyBuffText`.

Um caminho intermediário é um "buff virtual": o TLMod guarda tempo e efeito por jogador e mostra o ícone na
própria HUD, fora da barra do jogo. Não é o modelo do tModLoader.

### 3.6 `ModSystem`

| gancho | uso | vanilla | mobile (RVA) | thread | TLMod hoje | dificuldade |
|---|---|---|---|---|---|---|
| `Load`, `OnModLoad`, `PostSetupContent` | 38% / 26% / 7% · 6/6 | carga de mods do tModLoader | `tlmod_mod_init` na thread de carga (`mod_loader.cpp:1209`) | L | existe | trivial |
| `Unload`, `OnModUnload` | 26% / 5% · 6/6 | descarga de mods do tModLoader, que recarrega mods sem fechar o jogo | — | — | nada, e não precisa: o TLMod não recarrega mods, que saem com o processo | trivial |
| `PostUpdateEverything` | 12% · 5/6 | `Main.DoUpdateInWorld`, no fim | `0xCBBDF0` (só `DoUpdate` chama) | P | parcial: o frame roda no começo do `DoUpdate`, também no menu | trivial |
| `SaveWorldData`, `LoadWorldData` | 8% · 4/6 | `WorldFile.SaveWorld`, `WorldFile.LoadWorld` | `WorldFile.InternalSaveWorld` `0x14F1AC8`, `WorldFile.LoadWorld` `0x14EC048` | S, G, C; C | os ganchos existem (`save_hooks.cpp:460`, `:477`), só para baús | trivial |
| `ClearWorld`, `OnWorldLoad`, `OnWorldUnload` | 4% cada · 4–5/6 | `WorldGen.clearWorld`, `WorldFile.LoadWorld`, `WorldGen.SaveAndQuitCallBack` | `0x1305FAC`, `0x14EC048`, `0x130C8CC` | C, G; C; S | parcial: o "depois do load" do mundo | trivial |
| `PreUpdateWorld`, `PostUpdateWorld` | 4% / 2% · 5/6 | `WorldGen.UpdateWorld` | `0x13F7CB8` (`Main.DoUpdateInWorld`) | P | parcial: frame mais `in_world` | simples |
| `UpdateUI`, `ModifyInterfaceLayers` | 4% / 2,8% · 6/6 | camadas de `Main.DrawInterface` | a interface do celular é `GUI*`; `Main.DrawInterface` `0xD0ED7C` não tem as camadas do PC | P | painel, HUD e telas do TLMod | inviável hoje |
| `PreUpdateEntities`, `PostUpdateNPCs` e afins | 3% / 2% · 4/6 | meio de `Main.DoUpdateInWorld` | prefixos e sufixos dos métodos chamados ali, como `NPC.UpdateNPC` | P | nada | simples |
| `NetSend`, `NetReceive` | 3% · 4/6 | dados do mundo pela rede | — | P, R | nada | inviável hoje |
| `ModifyWorldGenTasks` | 2% · 5/6 | `WorldGen.GenerateWorld`, antes das etapas | `WorldGenerator.GenerateWorld` `0x1494A70`; a lista é o estático `WorldGen._generator` (`0x218`) | G | nada | inviável hoje |
| `PostWorldGen` | 2% · 3/6 | `WorldGen.GenerateWorld`, depois das etapas | sufixo de `WorldGen.GenerateWorld` `0x1306EF4` | G | nada | simples |
| `AddRecipes`, `PostAddRecipes`, `AddRecipeGroups` | 1,5% / 2% / 1% · 4–5/6 | `Recipe.SetupRecipes` | `0x122ECB8` | P | existe: `register_recipe`, sem grupos | trivial |
| `PostDrawTiles` | 2,5% · 3/6 | `Main.DoDraw`, depois dos blocos | `Main.DoDraw` `0xD5CD28`, `Main.DrawTiles` `0xCCEB38` | P | nada | simples |
| `ModifySunLightColor` | 2% · 4/6 | `Main.SetBackColor` | `0xD3F160` | P | nada | simples |

**Notas:**

- **`ModifyWorldGenTasks`:** uma etapa nova é um objeto gerenciado (`PassLegacy`, `GenPass`) com um delegate
  gerenciado. Criar isso do lado nativo apontando para código de mod exige montar um delegate IL2CPP à mão.
  Daí "inviável hoje".
- **`PostWorldGen`:** o gancho é simples, mas não há API de blocos. Os blocos ficam num `TileData`
  (`Main.tile`, campo estático `0x588`), formato do celular que precisa de estudo próprio.

### 3.7 O que o mobile muda no mapa

| diferença | evidência | efeito no mapa |
|---|---|---|
| sem `Player.ItemCheck_Inner` | `ItemCheck` (`0xFE39A0`) chama `ItemCheck_StartActualUse` em `0xFE41FC` e `ItemCheck_ApplyHoldStyle` em `0xFE4244` | `UseItem` e `HoldItem` vão em `ItemCheck` |
| sem `Main.DrawProj_Inner` e `DrawProj_Inner_DoDrawProj` | ausentes do dump; `Main.DrawProj` tem 8 chamadores de desenho | `PreDraw` de projétil vai em `DrawProj` |
| sem `WorldGen.GenerateWorld_RunTasksAndFinish` | `WorldGenerator.GenerateWorld` (`0x1494A70`) tem um único chamador, `0x130C1F4`, dentro de `WorldGen.GenerateWorld` | `PostWorldGen` é sufixo de `GenerateWorld` |
| banco de loot sem regras por item | `ItemDropDatabase` só tem `_globalEntries`, `_entriesByNpcNetId` e `_npcNetIdsByType`; nenhum `RegisterToItem` | `ModifyItemLoot` vira `RightClick` ou gancho em `OpenBossBag` |
| conversa, loja e buffs do celular | `GUINPCDialogue`, `GUIShop`, `GUIBuffs` no dump | botões de conversa e camadas de interface ficam fora |
| blocos em `TileData` | `public static TileData tile; // 0x588` em `Main` | uma API de blocos é estudo à parte |

E dois cuidados que valem também no PC:

- **`Player.ResetEffects`** é chamado por `Player.Update` (`0xF64F04`) e também pelo desenho de jogadores
  falsos: `TEHatRack.Draw`, `TEDisplayDoll.Draw`, `PlayerSegment.Draw` e `Main.DrawSuperSpecialProjectiles`.
  Um gancho ali precisa ignorar quem não está em `Main.player`, ou dar a ele dados próprios
  ([E1](#e1-reseteffects-em-jogadores-falsos)).
- **`NPC.FindFrame`** também roda nos ícones do bestiário (`UnlockableNPCEntryIcon.Update`) e nos créditos.

**A ordem no `Player.Update` do mobile**, pelas chamadas (confirmado no binário):

```
0xf6073c: bl Player.UpdateMiscCounter     ; PreUpdate do tML vem logo depois
0xf60cd4: bl Player.UpdateDead            ; UpdateDead
0xf64f04: bl Player.ResetEffects          ; ResetEffects
0xf6529c: bl Player.UpdateBuffs           ; Update dos buffs
0xf65508: bl Player.UpdateEquips          ; UpdateAccessory, UpdateEquip, UpdateInventory
0xf656c8: bl Player.UpdateArmorSets       ; UpdateArmorSet; PostUpdateEquips logo depois
0xf6610c: bl Player.UpdateLifeRegen       ; PostUpdateMiscEffects logo antes
0xf67938: bl Player.GrabItems             ; OnPickup
0xf6865c: bl Player.Update_NPCCollision   ; OnHitPlayer, ModifyHitByNPC
0xf6a020: ldrb w8, [x19, #0x82b]          ; teste de tongued: PreUpdateMovement vem logo antes
```

---

## 4. Proposta da API (pergunta 3)

### 4.1 Princípios

1. **Os nomes do tModLoader.** As mesmas classes (`ModPlayer`, `ModItem`, `GlobalItem`, `ModNPC`, `GlobalNPC`,
   `ModProjectile`, `GlobalProjectile`, `ModBuff`, `GlobalBuff`, `ModSystem`) e os mesmos ganchos, com os
   parâmetros na mesma ordem. Quem vem do tModLoader acha `ModifyHitNPC(NPC target, HitModifiers&
   modifiers)` onde espera.
2. **Duas camadas.**
   - **Embaixo, uma ABI em C estável:** uma tabela de callbacks por papel, com `struct_size`, registrada uma
     vez, como a `TLModHostApi` e o `TLModNpcDef` já fazem. Os nomes em C são os mesmos em `snake_case`:
     `reset_effects`, `post_update_equips`, `modify_hit_npc`.
   - **Em cima, a camada C++ do SDK:** classes com métodos virtuais, que montam as tabelas.
3. **Estado por valor.** Os ganchos recebem structs, não ponteiros do IL2CPP. O runtime lê os campos antes do
   callback e grava os que mudaram depois, como faz hoje com `TLModNpcState` (`tlmod_api.h:148–162`). Os
   offsets ficam no `terraria_offsets.h`, longe dos mods.
4. **Dados por entidade.** O equivalente aos campos de uma subclasse de `ModPlayer` ou `GlobalNPC`: um bloco
   por jogador ou NPC, do tamanho que o mod declara. É zerado na criação e salvo, se o mod quiser, pelos
   ganchos de save que já existem.
5. **Exceções contidas.** Toda chamada a mod com `try/catch` no SDK e no runtime (auditoria 09, ação 1).
6. **Thread por gancho, na documentação.** Tudo na P, menos save e load (S, C, P) e geração (G).
7. **Sem IDs mágicos.** O `ModContent.ItemType<T>()` do tModLoader vira `host.ItemType<T>()` sobre o
   `find_content` que já existe.

### 4.2 A ABI em C

Esboço das declarações (nomes e formas, não implementação):

```c
/* Referência a uma entidade do jogo: índice em Main.player, Main.npc ou Main.projectile. */
typedef struct TLModPlayerRef { int32_t index; } TLModPlayerRef;
typedef struct TLModNpcRef    { int32_t index; } TLModNpcRef;
typedef struct TLModProjRef   { int32_t index; } TLModProjRef;
typedef struct TLModItemRef   { int32_t owner; int32_t slot; TLModId type; } TLModItemRef;

/* Espelho dos modificadores de acerto do tModLoader (NPC.HitModifiers). */
typedef struct TLModHitModifiers {
    float   source_damage_mult;  /* SourceDamage *= */
    int32_t flat_bonus;          /* FlatBonusDamage += */
    float   final_damage_mult;   /* FinalDamage *= */
    float   knockback_mult;
    int8_t  crit;                /* -1 nunca, 0 o jogo decide, 1 sempre */
    int32_t armor_penetration;
} TLModHitModifiers;

typedef struct TLModHitInfo {    /* espelho de NPC.HitInfo */
    int32_t damage; float knockback; int32_t hit_direction; bool crit; int32_t damage_class;
} TLModHitInfo;

typedef enum TLModHitSource { TLMOD_HIT_ITEM, TLMOD_HIT_PROJECTILE, TLMOD_HIT_OTHER } TLModHitSource;

typedef struct TLModPlayerHooks {
    uint32_t struct_size;
    uint32_t data_size;          /* bytes por jogador: os "campos" do ModPlayer */
    void*    user;
    void (*reset_effects)(void* user, TLModPlayerRef p, void* data);
    void (*pre_update)(void* user, TLModPlayerRef p, void* data);
    void (*post_update_equips)(void* user, TLModPlayerRef p, void* data);
    void (*post_update_misc_effects)(void* user, TLModPlayerRef p, void* data);
    void (*pre_update_movement)(void* user, TLModPlayerRef p, void* data);
    void (*post_update)(void* user, TLModPlayerRef p, void* data);
    void (*update_dead)(void* user, TLModPlayerRef p, void* data);
    void (*modify_hurt)(void* user, TLModPlayerRef p, void* data, TLModHurtModifiers* m);
    void (*on_hurt)(void* user, TLModPlayerRef p, void* data, const TLModHurtInfo* info);
    bool (*pre_kill)(void* user, TLModPlayerRef p, void* data, TLModKillInfo* k);
    void (*kill)(void* user, TLModPlayerRef p, void* data, const TLModKillInfo* k);
    void (*modify_hit_npc)(void* user, TLModPlayerRef p, void* data, TLModHitSource src,
                           TLModNpcRef target, TLModHitModifiers* m);
    void (*on_hit_npc)(void* user, TLModPlayerRef p, void* data, TLModHitSource src,
                       TLModNpcRef target, const TLModHitInfo* hit);
    void (*on_enter_world)(void* user, TLModPlayerRef p, void* data);
    void (*save_data)(void* user, TLModPlayerRef p, const void* data, TLModTag* tag);
    void (*load_data)(void* user, TLModPlayerRef p, void* data, const TLModTag* tag);
} TLModPlayerHooks;

/* Na TLModHostApi, ao fim da tabela (struct_size cresce): */
bool  (*register_player_hooks)(TLModContext* ctx, const TLModPlayerHooks* hooks);
float (*player_stat)(const TLModContext* ctx, TLModPlayerRef p, TLModPlayerStat stat);
void  (*player_stat_add)(TLModContext* ctx, TLModPlayerRef p, TLModPlayerStat stat, float delta);
```

**Atributos do jogador por enum, não por ponteiro.** `TLModPlayerStat` lista o que os mods mais mexem:

- defesa e vida e mana máximas;
- dano e crítico por classe (`meleeDamage`, `rangedDamage`, `magicDamage`, `minionDamage`);
- velocidade de ataque e de movimento;
- regeneração e custo de mana.

O 1.4.4 vanilla não tem a `DamageClass` do tModLoader: são campos separados no `Player`. O runtime mapeia
cada valor para um offset, como já faz com `statDefense` e `statLifeMax2` (`terraria_offsets.h:49–51`).

As outras tabelas seguem a mesma forma:

| tabela | registro | ganchos (em `snake_case`) |
|---|---|---|
| `TLModItemHooks` | `register_item_hooks(ctx, item_id, ...)` (`ModItem`); `register_global_item(ctx, filtro, ...)` (`GlobalItem`) | `set_defaults` (struct `TLModItemDefaults`, ver abaixo), `can_use_item`, `use_item`, `hold_item`, `shoot`, `modify_shoot_stats`, `update_accessory`, `update_equip`, `is_armor_set`, `update_armor_set`, `update_inventory`, `modify_tooltips`, `modify_hit_npc`, `on_hit_npc`, `modify_weapon_damage`, `can_right_click`, `right_click`, `alt_function_use` |
| `TLModNpcHooks` | os campos novos do `TLModNpcDef` (`ModNPC`); `register_global_npc(ctx, filtro, ...)` | `set_defaults`, `pre_ai`, `ai` (o `on_ai` de hoje), `post_ai`, `find_frame`, `hit_effect`, `pre_kill`, `on_kill`, `check_dead`, `check_active`, `modify_npc_loot` (lista declarativa), `spawn_chance`, `on_spawn` (o de hoje), `can_hit_player`, `on_hit_player`, `can_be_hit_by_projectile`, `can_be_hit_by_item`, `modify_hit_by_projectile`, `on_hit_by_projectile`, `modify_hit_by_item`, `on_hit_by_item`, `apply_difficulty_scaling`, `pre_draw`, `post_draw` |
| `TLModProjectileHooks` | `register_projectile(ctx, def)` com `shell_type`, como NPC | `set_defaults`, `pre_ai`, `ai`, `post_ai` (estado em `TLModProjState`: posição, velocidade, `ai[2]`, `local[8]`, `time_left`, `penetrate`, `frame`, `rotation`, dono), `on_hit_npc`, `modify_hit_npc`, `on_hit_player`, `pre_kill`, `on_kill`, `on_tile_collide`, `should_update_position`, `colliding`, `can_damage`, `get_alpha`, `on_spawn`, `pre_draw`, `post_draw` |
| `TLModBuffHooks` | `register_buff(ctx, def)`, só com ID próprio; `register_global_buff(ctx, ...)` | `update_player`, `update_npc`, `modify_buff_text`, `re_apply` |
| `TLModSystemHooks` | `register_system(ctx, ...)` | `post_setup_content`, `pre_update_world`, `post_update_world`, `post_update_everything`, `on_world_load`, `on_world_unload`, `clear_world`, `save_world_data`, `load_world_data`, `post_world_gen`, `post_add_recipes` |

**`TLModItemDefaults`** amplia o `TLModItemDef` com o que os mods de fato preenchem no `SetDefaults`
([2.2](#22-o-que-os-itens-e-npcs-declaram)):

- `defense`, `accessory`;
- `shoot`, `shoot_speed`, `use_ammo`, `ammo`;
- `use_style`, `use_animation`, `auto_reuse`;
- `knock_back`, `crit`, `mana`, `damage_class`;
- `consumable`, `heal_life`, `heal_mana`;
- `buff_type`, `buff_time`;
- `create_tile`, `pick`, `axe`, `hammer`.

Os campos entram no fim da struct, e o `struct_size` mantém os mods antigos.

**`TLModTag`** é o `TagCompound` do tModLoader em C: chaves de texto com valores inteiros, reais, texto e
bytes, gravados pelo runtime no arquivo paralelo que já existe (`save_hooks.cpp`).

### 4.3 A camada C++

No mesmo espírito do `tlmod.hpp` de hoje, que já tem `FrameListener`, `CommandHandler` e `NpcBehavior`
(`tlmod.hpp:59–91`):

```cpp
namespace tlmod {

class ModPlayer {
public:
    virtual ~ModPlayer() = default;
    virtual void ResetEffects() {}
    virtual void PreUpdate() {}
    virtual void PostUpdateEquips() {}
    virtual void PostUpdate() {}
    virtual void UpdateDead() {}
    virtual void ModifyHurt(HurtModifiers& modifiers) {}
    virtual void OnHurt(const HurtInfo& info) {}
    virtual bool PreKill(KillInfo& info) { return true; }
    virtual void Kill(const KillInfo& info) {}
    virtual void ModifyHitNPCWithItem(Item item, NPC target, HitModifiers& modifiers) {}
    virtual void ModifyHitNPCWithProj(Projectile proj, NPC target, HitModifiers& modifiers) {}
    virtual void OnHitNPCWithItem(Item item, NPC target, const HitInfo& hit) {}
    virtual void OnHitNPCWithProj(Projectile proj, NPC target, const HitInfo& hit) {}
    virtual void OnEnterWorld() {}
    virtual void SaveData(TagCompound& tag) const {}
    virtual void LoadData(const TagCompound& tag) {}
    Player Player;   // como o this.Player do tModLoader
};

class ModItem {
public:
    virtual ~ModItem() = default;
    virtual void SetDefaults() {}          // preenche Item.* (vira TLModItemDefaults)
    virtual void AddRecipes() {}
    virtual bool CanUseItem(Player player) { return true; }
    virtual std::optional<bool> UseItem(Player player) { return std::nullopt; }
    virtual bool Shoot(Player player, ShootContext& shot) { return true; }
    virtual void UpdateAccessory(Player player, bool hideVisual) {}
    virtual void UpdateEquip(Player player) {}
    virtual bool IsArmorSet(Item head, Item body, Item legs) { return false; }
    virtual void UpdateArmorSet(Player player) {}
    virtual void ModifyTooltips(TooltipList& tooltips) {}
    virtual void OnHitNPC(Player player, NPC target, const HitInfo& hit) {}
    Item Item;
};

class ModNPC { /* SetDefaults, AI, PreAI, PostAI, FindFrame, HitEffect, ModifyNPCLoot, OnKill,
                  CheckDead, CheckActive, SpawnChance, OnHitPlayer, PreDraw, PostDraw; NPC NPC; */ };
class GlobalItem; class GlobalNPC; class ModProjectile; class GlobalProjectile;
class ModBuff; class GlobalBuff; class ModSystem;

}  // namespace tlmod

// Registro: o tModLoader acha as classes por reflexão; aqui, o mod as lista no carregamento.
TLMOD_MOD(OnLoad)
bool OnLoad(tlmod::Host& host) {
    host.Add<VampirePlayer>();
    host.Add<BloodBlade>("blood_blade");      // a chave local do manifest
    host.Add<BloodBolt>("blood_bolt");
    return true;
}
```

**Como a camada funciona:**

- `host.Add<T>()` monta a tabela C com trampolins que chamam os virtuais. Os trampolins capturam exceções e
  convertem para o contrato em C (auditoria 09).
- O SDK cria uma instância de `ModPlayer` por jogador, sob demanda, e a guarda no bloco `data`.
- `Player`, `Item`, `NPC` e `Projectile` são alças leves. Os atributos usam o nome do tModLoader:
  `player.GetDamage(DamageClass::Melee) += 0.1f`, que grava em `meleeDamage`.
- `Player Player;` e `Item Item;` imitam o tModLoader. No código de verdade, o tipo vai qualificado
  (`tlmod::Item`), no membro e nos parâmetros: o C++ não deixa um membro mudar o sentido de um nome de tipo
  já usado na classe. O GCC recusa a forma curta; o Clang aceita.

**Equivalências**, para quem vem do tModLoader:

| tModLoader | TLMod C++ | TLMod C |
|---|---|---|
| `class X : ModPlayer` + `ResetEffects()` | `class X : public tlmod::ModPlayer` + `ResetEffects()` | `TLModPlayerHooks.reset_effects` |
| `Player.statDefense += 4` | `Player.statDefense() += 4` | `player_stat_add(ctx, p, TLMOD_STAT_DEFENSE, 4)` |
| `ModContent.ItemType<BloodBlade>()` | `host.ItemType<BloodBlade>()` | `find_content(ctx, TLMOD_CONTENT_ITEM, "mod:blood_blade")` |
| `Item.shoot = ModContent.ProjectileType<BloodBolt>()` | `Item.shoot = host.ProjectileType<BloodBolt>()` | `TLModItemDefaults.shoot` |
| `npcLoot.Add(ItemDropRule.Common(id, 3))` | `loot.Add(Common(id, 3))` | `TLModLootRule` na lista de `modify_npc_loot` |
| `tag["kills"] = kills` | `tag.Set("kills", kills)` | `tag_set_i32(tag, "kills", kills)` |

### 4.4 O que muda por causa do mobile

- **Casca enquanto não houver ID próprio.** `ModItem`, `ModNPC` e `ModProjectile` declaram uma casca
  (`shell_type`). `SetStaticDefaults` fica limitado ao que não depende de tabela por tipo.
- **Sem `ProcessTriggers`:** o celular não tem teclas configuráveis. O lugar disso é a HUD e o painel do TLMod
  (`register_command` e botões).
- **Sem `ModifyInterfaceLayers` e `UpdateUI`:** a interface é `GUI*`. O lugar é a API do painel.
- **Sem `SendExtraAI` e `NetSend`:** o TLMod é de um jogador só por enquanto (README, "Ainda não
  implementado").
- **Desenho:** um `DrawContext` mínimo (textura do mod, posição, quadro, cor, rotação, escala) no lugar do
  `SpriteBatch` cru. É o que cabe nos ganchos de desenho que o TLMod já tem.
- **Dano por campos vanilla:** `meleeDamage` e afins, não `DamageClass`. A camada C++ mantém a sintaxe do
  tModLoader por cima.

---

## 5. Ordem por valor (pergunta 4)

**Valor** é quanto conteúdo o grupo destrava: as frações das seções 2.1 e 2.2. **Esforço** é a dificuldade da
matriz mais o trabalho de API.

| ordem | grupo | destrava | evidência | dificuldade | esforço |
|---|---|---|---|---|---|
| 1 | campos de item (`SetDefaults` ampliado) e `GlobalItem.SetDefaults`; escala de dificuldade dos NPCs de mod | armas à distância e mágicas, armaduras, acessórios, consumíveis, poções; bosses com a vida certa no Expert e no Mestre | `SetDefaults` em 82% dos itens; 26% armas, 12% consumíveis, 8% mana, 8% acessórios, 6% defesa; `ApplyDifficultyAndPlayerScaling` em 18% dos NPCs | trivial | 1,5–2,5 dias |
| 2 | núcleo do `ModPlayer` com dados e atributos | o padrão básico do tModLoader: o acessório liga uma flag, o `ResetEffects` a zera, o efeito roda depois | `ResetEffects` em 51%, `PostUpdate` 27%, `PreUpdate` 17%; os três nos 6 mods | simples | 2–3 dias |
| 3 | equipamento | efeitos de acessório e de armadura | `UpdateAccessory` nos 6 mods; 8% dos itens são acessórios e 6% armaduras | casca e simples | 1–2 dias |
| 4 | acerto | efeitos ao acertar: debuffs, vampirismo, explosões | `OnHitNPC` em 33% dos projéteis; `ModifyHitNPCWithProj` nos 6 mods; `GlobalNPC.OnHitByProjectile` nos 6 | simples | 2–3 dias |
| 5 | uso de item | condições de uso, ferramentas, armas com efeito | `CanUseItem` nos 6 mods; `GlobalItem.UseItem` em 18% | simples | 1–2 dias |
| 6 | projéteis por casca e `Shoot` | um item em cada quatro | 22% dos itens disparam projétil de mod; 4.101 classes de projétil | casca | 4–6 dias |
| 7 | NPC: `HitEffect`, morte, colisão, surgimento, quadro | inimigos completos sobre a casca | `HitEffect` 78%, `SpawnChance` 40%, `OnHitPlayer` 27% | simples | 3–4 dias |
| 8 | loot | o que os inimigos e bolsas soltam | `ModifyNPCLoot` em 50% dos NPCs | ID próprio para NPC de mod; simples para original | 2–3 dias |
| 9 | dados livres no save, eventos de mundo e de entrada | progresso, contadores, estado entre sessões | `SaveData`/`LoadData` de jogador nos 6 mods; de mundo em 8% dos sistemas | trivial | 1–2 dias |
| 10 | `Global*` em conteúdo original | mods de ajuste, como o Overhaul, que é quase todo `Global*` | `GlobalNPC.OnKill` nos 6 mods | simples, com custo nos caminhos quentes | 1–2 dias |
| 11 | desenho e tooltips dinâmicos | efeitos visuais, rastros, brilho | `PreDraw` em 61% dos projéteis e 55% dos NPCs | simples, mais API de desenho | 4–5 dias |
| 12 | buffs | debuffs e buffs de mod | `ModBuff.Update` em 71% de 563 classes | ID próprio (`GlobalBuff`: simples) | 1 dia + auditoria 02 |

**Por que esta ordem:**

- **Itens e jogador antes de tudo.** Os passos 1 a 5 são baratos (de 7,5 a 12,5 dias) e cobrem o que a
  maioria dos itens precisa. O passo 1 também corrige a escala dos NPCs de mod, um erro que já afeta os
  bosses de hoje.
- **Projéteis em seguida.** Valem muito (passo 6), mas custam mais, porque não há nada de projétil hoje.
- **NPCs depois.** Já têm o essencial (`on_ai`, casca, textura). Faltam os ganchos de morte e de acerto.
- **Buffs por último.** Dependem do ID próprio da auditoria 02.

---

## 6. Experimentos no aparelho

Com uma build de teste da ponte e o `tlmod.log`. Cada experimento é curto.

### E1: ResetEffects em jogadores falsos

Gancho de teste em `Player.ResetEffects` (`0xFBD68C`) que registra o `whoAmI` e se o objeto está em
`Main.player`. Pôr um manequim e um cabideiro perto do jogador.

**Esperado:** chamadas para objetos fora de `Main.player`, vindas de `TEDisplayDoll.Draw` e `TEHatRack.Draw`.
O gancho final precisa ignorá-los ou dar a eles um bloco de dados próprio.

### E2: custo dos ganchos globais

`GlobalNPC.PreAI` e `GlobalProjectile.PreAI` vazios para todos os tipos. Medir o tempo de quadro com 200 NPCs
(Ferramentas: invocar inimigos) e com muitas flechas na tela, contra a build sem os ganchos. O E3 da
auditoria 03 tem o método.

**Esperado:** abaixo de 1 ms por segundo de jogo para cada gancho. Inferido da estimativa da auditoria 03
para o gancho de `NPC.AI`: de 50 a 100 ns por chamada.

### E3: clique direito no celular

Registrar as chamadas a `ItemSlot.RightClick` (`0x16284C4`, `0x162C410`) e `ItemSlot.TryOpenContainer`
(`0x162C754`) ao abrir uma bolsa original pelo toque.

**Esperado:** saber qual das duas o toque usa. É ali que entram `RightClick` e `CanRightClick` de mod.

### E4: regeneração por prefixo

Gancho de teste com prefixo em `Player.UpdateLifeRegen` (`0xFBE3DC`) que soma 10 a `lifeRegen`. Conferir no
painel se a vida sobe mais rápido.

**Esperado:** a vida sobe mais rápido se o jogo não zerar o campo no começo do método. Se zerar, é preciso
outro ponto.

### E5: projétil por casca

Mod de teste com um projétil de casca "flecha de madeira" e um `on_ai` que o faz girar. Disparar pela arma de
teste.

**Esperado:**
- o `on_ai` roda uma vez antes de o projétil entrar no mundo, pelo `Projectile.SetDefaults` (`b` em
  `0x106F668`);
- depois roda a cada tick;
- a textura troca no `Main.DrawProj` (`0xCD0D70`).

### E6: entrada no mundo

Registrar a thread e o momento de `Player.Hooks.EnterWorld` (`0x11D8D6C`) ao entrar num mundo.

**Esperado:** na P, depois de `WorldGen.FinaliseOnMainThread`, uma vez por entrada.

### E7: loot de NPC original pelo banco do jogo

Depois de `ItemDropDatabase.Populate` (`0x7D8E00`), registrar com `ItemDropRule.Common` (`0x7CDA10`) e
`RegisterToNPC` (`0x7D86DC`) uma gota de 100% de um item original no Zumbi (tipo 3). Matar alguns.

**Esperado:** a gota aparece. Com um item de mod, não usar a regra do jogo (ver [3.3](#33-modnpc-e-globalnpc)).

### E8: escala de dificuldade

Invocar o boss de exemplo num mundo Clássico e num Expert, e ler a vida máxima no `tlmod.log`.

**Esperado:**
- **hoje:** a mesma vida nos dois, porque `mod_loader.cpp:392` grava `life_max` fixo;
- **com a correção:** a vida do Expert maior, como a de um NPC original.

---

## 7. Respostas diretas

**1. Os ganchos mais usados.** Pela amostra de seis mods públicos e pelo ExampleMod:

- **`ModPlayer`:** `ResetEffects`, `PostUpdate`, `PreUpdate`, `UpdateDead`, `PostUpdateEquips`, os de dano
  (`ModifyHurt`, `OnHurt`), `SaveData`/`LoadData`, `OnEnterWorld`, `PreUpdateMovement` e os de acerto com
  projétil.
- **`ModItem`:** `SetDefaults`, `AddRecipes`, `SetStaticDefaults`, `Shoot`, `CanUseItem`, `UpdateAccessory`,
  `UpdateEquip`, `ModifyTooltips`, `UseItem`, `HoldItem` e `AltFunctionUse`.
- **`GlobalItem`:** `SetDefaults`, `ModifyTooltips` e `UseItem`.
- **`ModNPC`:** `SetDefaults`, `AI`, `HitEffect`, `SetBestiary`, `FindFrame`, `PreDraw`, `ModifyNPCLoot`,
  `SpawnChance`, `OnHitPlayer` e `CheckActive`.
- **`GlobalNPC`:** `PostAI`, `OnKill`, `PreAI` e os de acerto por projétil.
- **`ModProjectile`:** `SetDefaults`, `AI`, `PreDraw`, `OnKill`, `OnHitNPC`, `Colliding`, `CanDamage` e
  `OnTileCollide`.
- **`ModBuff`:** `Update` e `SetStaticDefaults`.
- **`ModSystem`:** `Load`/`Unload`, `PostUpdateEverything`, `SaveWorldData`/`LoadWorldData`, `ClearWorld`,
  `UpdateUI` e `ModifyWorldGenTasks`.

**2. O mapa.** A matriz da seção 3 dá, para 152 ganchos em 101 linhas, o método vanilla, o equivalente mobile
com RVA, a thread, o estado no TLMod e a dificuldade. Em resumo:

- o mobile tem quase todos os métodos, com o mesmo nome, e quase tudo roda na P;
- 31 ganchos são triviais e 92 simples;
- 12 dependem da casca: `Shoot`/`ModifyShootStats`, `UpdateAccessory`, `UpdateEquip`,
  `IsArmorSet`/`UpdateArmorSet`, `ModifyItemLoot`, o `SetStaticDefaults` de NPC e o `SetDefaults` e a IA
  (`AI`, `PreAI`, `PostAI`) de projétil;
- 5 dependem de ID próprio: as tabelas de item, o bestiário, o loot de NPC de mod e o nome e o `Update` de
  `ModBuff`;
- 12 são inviáveis hoje: a rede de NPC, projétil e sistema, as camadas de interface, os botões de conversa,
  as etapas de geração e as teclas.

**3. A API.**

- **Camada C++:** as mesmas classes e nomes do tModLoader (`tlmod::ModPlayer::ResetEffects`,
  `tlmod::ModItem::UpdateAccessory`, `tlmod::ModNPC::AI`).
- **ABI em C:** uma tabela de callbacks por papel, com `struct_size`, nomes iguais em `snake_case`, estado em
  struct aplicado pelo runtime, dados por entidade e `TLModTag` para o save.
- **O que muda no celular:** a casca enquanto não houver ID próprio; a HUD no lugar das teclas; o painel no
  lugar das camadas de interface; sem rede por enquanto.

**4. A ordem por valor:**

1. campos de item e escala dos NPCs de mod;
2. núcleo do `ModPlayer`;
3. equipamento;
4. acerto;
5. uso de item;
6. projéteis por casca;
7. NPC;
8. loot;
9. dados no save e eventos;
10. `Global*`;
11. desenho;
12. buffs.

Os cinco primeiros custam de 7,5 a 12,5 dias e cobrem a maior parte dos itens. O sexto, de 4 a 6, cobre um
item em cada quatro.

---

## 8. Lacunas do TLMod, em ordem de risco

1. <a id="lacuna-1"></a>**Nenhum gancho de jogador por tick** (alto). Sem `ResetEffects`, `PostUpdateEquips`,
   `PostUpdate` nem dados por jogador, o padrão mais comum do tModLoader não tem onde rodar: 51% das classes
   `ModPlayer`, nos seis mods. O `terraria_offsets.h` já tem a RVA de `Player.Update` (`:523`), sem gancho.
   Ação 2.
2. <a id="lacuna-2"></a>**Nenhum projétil de mod** (alto). 22% dos itens de mod disparam projétil próprio, e
   a amostra tem 4.101 classes de projétil. O README lista o `register_*` de projétil como não
   implementado. Ação 6.
3. <a id="lacuna-3"></a>**Campos de item curtos** (alto). O `TLModItemDef` só tem dano corpo a corpo e tempo
   de uso (`tlmod_api.h:85–108`). Sem defesa, acessório, disparo, munição, mana, consumível, cura ou buff,
   quase nenhum item fora da espada simples porta sem perder comportamento. Ação 1.
4. <a id="lacuna-4"></a>**Nenhum gancho de acerto** (médio-alto). Não há `OnHitNPC`/`ModifyHitNPC` de item,
   projétil ou jogador, nem a origem no evento de dano. Ação 4.
5. <a id="lacuna-5"></a>**NPC de mod ignora a dificuldade** (médio). O `on_spawned` grava `life`, `life_max`,
   `damage` e `defense` fixos (`mod_loader.cpp:392–394`). Isso apaga o que `NPC.ScaleStats` (`0xEDD4D8`,
   chamado em `0xDB4610` dentro do `SetDefaults`) aplicou para Expert e Mestre: um boss de mod tem a mesma
   vida no Clássico e no Mestre. Ação 1.
6. <a id="lacuna-6"></a>**Nenhum loot de mod** (médio). O NPC de mod solta o loot da casca (auditoria 04), e
   não há como declarar loot nem bolsa. Ação 8.
7. <a id="lacuna-7"></a>**Nenhum buff de mod** (médio). São 563 classes na amostra. Precisa de ID próprio
   (auditoria 02) ou de buff virtual. Ação 12.
8. <a id="lacuna-8"></a>**Save só de itens** (médio-baixo). Os ganchos de save existem, mas guardam só itens
   de mod. Não há dado livre por mod, jogador ou mundo. Ação 9.
9. <a id="lacuna-9"></a>**Desenho só por troca de textura** (baixo-médio). Sem `PreDraw`/`PostDraw`, efeitos
   visuais não portam. Ação 11.
10. <a id="lacuna-10"></a>**Sem rede** (fora do alcance agora). 31% dos NPCs e 12% dos projéteis de mod
    sincronizam estado; o TLMod é de um jogador só. Documentar.

---

## 9. Ações recomendadas e ordem de implementação

Em ordem. A [seção 5](#5-ordem-por-valor-pergunta-4) dá o porquê.

1. **Campos de item e escala de NPC** (1,5–2,5 dias):
   - `TLModItemDefaults` com os campos da [4.2](#42-a-abi-em-c), aplicados no gancho de `Item.SetDefaults` que
     já existe;
   - um callback de `GlobalItem.SetDefaults` no mesmo gancho;
   - no NPC de mod, aplicar vida, dano e defesa antes do `ScaleStats`, ou multiplicar pelo fator de
     dificuldade, em vez de sobrescrever depois.

   [Lacunas 3](#lacuna-3) e [5](#lacuna-5).
2. **Núcleo do `ModPlayer`** (2–3 dias):
   - ganchos em `Player.ResetEffects`, prefixo e sufixo de `Player.Update`, `Player.UpdateDead`, sufixo de
     `Player.UpdateArmorSets`, prefixo de `Player.UpdateLifeRegen` e dos métodos de colisão (para
     `PreUpdateMovement`);
   - bloco de dados por jogador, ignorando os jogadores falsos;
   - `player_stat` e `player_stat_add`;
   - a classe `tlmod::ModPlayer`.

   [Lacuna 1](#lacuna-1).
3. **Equipamento** (1–2 dias): `ApplyEquipFunctional`, `GrantArmorBenefits`, fim de `UpdateArmorSets` e o laço
   do inventário em `UpdateEquips`, para itens de mod e, com filtro, para originais.
4. **Acerto** (2–3 dias):
   - `ProcessHitAgainstNPC`, `Projectile.Damage`, `ApplyDamageToNPC` e `NPC.StrikeNPC`;
   - `TLModHitModifiers` e `TLModHitInfo`;
   - a origem no evento de `Hurt`;
   - `Player.KillMe`.

   [Lacuna 4](#lacuna-4).
5. **Uso de item** (1–2 dias): `ItemCheck_CheckCanUse`, `ItemCheck`, `ItemCheck_ManageRightClickFeatures` e
   `GetWeaponDamage`.
6. **Projéteis por casca** (4–6 dias):
   - `register_projectile` com `shell_type` e `TLModProjState`;
   - ganchos em `Projectile.AI`, `Damage`, `Kill`, `HandleMovement`, `UpdatePosition`, `Colliding`,
     `GetAlpha` e `NewProjectile`;
   - a textura no `Main.DrawProj`;
   - `Shoot`/`ModifyShootStats` em `ItemCheck_Shoot`.

   [Lacuna 2](#lacuna-2).
7. **NPC** (3–4 dias): `HitEffect`, `NPCLoot`, `checkDead`, `CheckActive`, `Update_NPCCollision`, sufixo de
   `SpawnNPC` com `spawn_chance`, `FindFrame` e `PostAI`.
8. **Loot** (2–3 dias):
   - lista declarativa resolvida pelo TLMod no `NPCLoot`, soltando itens de mod pelo caminho da casca;
   - regras do jogo para NPC e item originais;
   - bolsas por `RightClick` e por `OpenBossBag`.

   [Lacuna 6](#lacuna-6).
9. **Save e eventos** (1–2 dias):
   - `TLModTag` no arquivo paralelo, para jogador, mundo e item;
   - `OnEnterWorld`, `OnWorldLoad`, `OnWorldUnload` e `ClearWorld`;
   - `Pre`/`PostUpdateWorld` e `PostUpdateEverything`.

   [Lacuna 8](#lacuna-8).
10. **`Global*` em conteúdo original** (1–2 dias): filtros por tipo, medindo o custo antes de abrir os caminhos
    quentes ([E2](#e2-custo-dos-ganchos-globais)).
11. **Desenho e tooltips** (4–5 dias): `DrawContext` nos ganchos de desenho que já existem e em `DrawProj`;
    linhas dinâmicas em `MouseText_DrawItemTooltip_GetLinesInfo`. [Lacuna 9](#lacuna-9).
12. **Buffs** (1 dia, mais o caminho da auditoria 02): `GlobalBuff` primeiro; `ModBuff` quando houver ID
    próprio. [Lacuna 7](#lacuna-7).

Depois de cada passo, o experimento correspondente da seção 6:

| passos | experimentos |
|---|---|
| 1 | E8 |
| 2 | E1, E4, E6 |
| 6 | E5 |
| 8 | E7 |
| 5 e 8 | E3 |
| 10 | E2 |

Fora do plano por ora:

- a rede;
- as camadas de interface do PC e os botões de conversa;
- as etapas de geração de mundo;
- o bestiário, que pede ID próprio;
- uma API de blocos, que pede estudar o `TileData`.
