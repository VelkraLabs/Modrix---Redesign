# Auditoria 02: ordem de inicialização (itens com ID próprio)

Terraria Mobile 1.4.4.9 (Unity 2021.3.56f2, IL2CPP, Android arm64), comparado com o TLMod da branch
`ccr-bbd92bf9-i01buk` (código no commit `f917b04`) e com o estudo `docs/estudo_extensao_tabelas.md`.

Auditoria só de leitura. As fontes são:

- o dump regenerado (`dump.cs`, `script.json`, `il2cpp.h`);
- a `global-metadata.dat` (versão 31);
- a desmontagem da `libil2cpp.so`;
- o `tlmod.log` de uma abertura real (Samsung SM-A075M, Android 16, sessão de 17/10 às 15:17:16).

Dump, metadata e biblioteca ficam fora do repositório. Nenhum código foi alterado; este arquivo é o único
acréscimo.

**Módulo 2:** ordem de inicialização, pensando nos itens com ID próprio (sem casca). Primeira entrega
em vista: um item de teste com ID 5456.

- [Como ler](#como-ler)
- [Resumo](#resumo)
- [1. Como o IL2CPP desta build dispara um `.cctor`](#1-como-o-il2cpp-desta-build-dispara-um-cctor)
- [2. Linha do tempo da inicialização](#2-linha-do-tempo-da-inicialização)
- [3. Tabelas de item: quem cria e quando](#3-tabelas-de-item-quem-cria-e-quando)
- [4. O ponto de hook](#4-o-ponto-de-hook)
- [5. As 44 ocorrências do estudo](#5-as-44-ocorrências-do-estudo)
- [6. Experimentos no aparelho](#6-experimentos-no-aparelho)
- [7. Respostas diretas](#7-respostas-diretas)
- [8. Veredito para a primeira entrega (item 5456)](#8-veredito-para-a-primeira-entrega-item-5456)
- [9. Lacunas, em ordem de risco](#9-lacunas-em-ordem-de-risco)
- [10. Ações recomendadas](#10-ações-recomendadas)

---

## Como ler

**Níveis de confiança**, os mesmos da auditoria 01:

- **confirmado no binário**: visto na desmontagem ou na metadata;
- **inferido**: decorre do código lido, mas um passo não foi rastreado (chamada virtual, composição da
  cena, tempo);
- **só o aparelho confirma**: depende do instante em que as coisas acontecem. Cada caso tem um
  experimento na [seção 6](#6-experimentos-no-aparelho).

Afirmações sobre código-fonte trazem arquivo e linha e valem como confirmadas no código. Isso vale para o
TLMod e para o ShadowHook 2.0.1, a versão que a ponte compila.

**Endereços e convenções** iguais aos da [auditoria 01](01_persistencia.md#como-ler). Campos do
`Il2CppClass` nesta build (pelo `il2cpp.h` do dump, conferidos no código):

| campo | deslocamento | uso aqui |
|---|---|---|
| `static_fields` | `+0xB8` | campos estáticos |
| `cctor_started` | `+0xDC` | o `.cctor` começou |
| `cctor_finished` | `+0xE0` | diferente de zero: `.cctor` terminou (ou a classe não tem) |
| `cctor_thread` | `+0xE8` | thread que está rodando o `.cctor` |
| `flags` | `+0x114` | atributos do tipo (beforefieldinit é `0x00100000`) |
| `vtable` | `+0x138` | slot k em `+0x138 + 16·k` |

**Fases** usadas no texto:

| fase | o que é | onde começa |
|---|---|---|
| **A** | nativo: as bibliotecas carregam e o `il2cpp_init` roda | antes de qualquer código do jogo |
| **B** | primeiro quadro: `XNAUnityRunner.Awake` | o primeiro código gerenciado do Terraria |
| **C** | `XNAUnityRunner.Start` → `Main.Initialize` | logo depois do `Awake` |
| **D** | quadros de carregamento: `Main.DrawSplash`, `LoadContent_Deferred`, `Initialize_AlmostEverything` | tela de carregamento |
| **E** | menu e jogo | depois do carregamento |
| **X** | código morto: nada chama | nunca |

**Fora do escopo** (só registrado): como a `libtlmod` entra no processo (alteração e empacotamento do
APK) e a parte Java do Unity. Daqui só uso a ordem observada no `tlmod.log`.

---

## Resumo

1. **`Program.LaunchGame` não roda no celular.**
   - Não tem chamador direto. Nenhum `MethodInfo` dele é usado (então não vira delegate) e não há
     string com o nome dele.
   - O corpo é o caminho do PC e do servidor dedicado: termina em `Main.DedServ` e `Game.Run`.
   - O `Main` do jogo nasce no `XNAUnityRunner.Awake` (`new Main()` em `0xAE0C30`).
   - O ponto de hook proposto no estudo **não existe** nesta build.
2. **Os `.cctor` das tabelas de item rodam no primeiro quadro.**
   - **No `Awake`:** `Main`, `Item`, `ItemID.Sets` (77 sets e mais 3 da mesma fábrica), `Player` e
     `VirtualControllerInputState`.
   - **No `Start`** (dentro do `ContentSamples.Initialize`): `TextureAssets`, `Lang`, `ContentSamples` e
     `ItemVariants`.
   - **Depois:** `PrefixLegacy.ItemSets` só no primeiro sorteio de prefixo; `MiscFallback` só na tela de
     filtros.
3. **Como o `.cctor` dispara nesta build.** O IL2CPP roda o `.cctor` (`Runtime::ClassInit`, `0x624F80`)
   em três situações:
   - no primeiro acesso a campo estático;
   - em **todo** `new` (`Object::New` termina em `ClassInit`);
   - em `il2cpp_runtime_invoke` de método estático.

   Não há inicialização antecipada: o `ForceStaticInitializers` do PC está morto aqui, porque o
   `StartForceLoad` só liga `LoadedEverything`.
4. **A ponte de hoje chega tarde, ou no limite.**
   - Ela espera o fim do `Runtime::Init` e mais 200 ms; o primeiro hook entra 1,26 s depois de a
     sessão começar.
   - O `Awake` e o `Start` rodam na thread principal do Unity, sem esperar a ponte.
   - Se o `Awake` vem antes ou depois dos hooks, só o aparelho confirma
     ([E1](#e1-mínimo-flags-dos-cctor-e-o-awake)).
5. **O ponto seguro é nativo: o `il2cpp_init`** (exportado em `0x696800`).
   - Nesta build, o `il2cpp_init_utf16` (`0x69682C`) chama o `Runtime::Init` direto, sem passar pelo
     `il2cpp_init`. Qual dos dois o Unity chama está na `libunity.so`, fora do material, então o hook
     vai nos dois.
   - Nesse instante nenhum código gerenciado rodou: as constantes podem ser corrigidas ali.
   - Os hooks gerenciados podem ser instalados na mesma chamada, logo depois do original, ainda na
     thread do Unity e antes da primeira cena.
   - Um hook pendente por símbolo é viável. O ShadowHook o instala quando o linker carrega a
     `libil2cpp.so`, antes dos construtores dela e antes de o Unity poder chamar o `il2cpp_init`. No
     log, a `libtlmod` já estava no processo pelo menos uns 230 ms antes de a `libil2cpp.so` aparecer.
6. **As 44 ocorrências do estudo.**
   - **Completude:** a lista está completa. Uma varredura nova (formas partidas, palavras em `.rodata` e
     `.data`, campos estáticos) não achou nada fora dela.
   - **Código morto:** 5 nunca rodam (`IdLookup`, os dois laços das categorias de controle e as duas
     ALLOC do simulador de loot).
   - **Por fase:** 4 no `Awake`, 8 no `Start`, 2 no carregamento, 25 depois e 5 nunca.
7. **Veredito para o item 5456: viável**, com a correção no `il2cpp_init` e hooks instalados ali. Com
   hook em `LaunchGame` não funciona, porque ele nunca é chamado.

---

## 1. Como o IL2CPP desta build dispara um `.cctor`

### 1.1 Quem chama `Runtime::ClassInit`

Todos os caminhos terminam em `Runtime::ClassInit` (`0x624F80`). Ele confere `cctor_finished` e sai se
já estiver ligado. Confirmado no binário.

```
; Runtime::ClassInit 0x624F80
0x624f98: ldr    w8, [x22, #0xe0]!          ; klass->cctor_finished
0x624f9c: cbz    w8, #0x624fb8              ; zero: roda o .cctor (com trava e thread dona)
0x624fa0: ...                               ; diferente de zero: retorna
```

| gatilho | evidência | roda o `.cctor`? | confiança |
|---|---|---|---|
| acesso a campo estático (código gerado) | antes do acesso: `ldr w8, [x0, #0xe0]`; `cbnz w8, …`; `bl 0x642c14`. O ajudante `0x642C14` é `b 0x624F80` | sim, no primeiro acesso | confirmado no binário |
| `new T()` | `0x642C28` → `Object::New` (`0x659230` → `0x65952C`), que termina em `mov x0, x19`; `bl 0x624f80` (`0x6595F0`–`0x6595F4`), sem condição | **sempre**, mesmo sem acesso a campo | confirmado no binário |
| `il2cpp_object_new` (exportado, `0x69732C`) | `bl 0x659230`: o mesmo `Object::New` | sim | confirmado no binário |
| `il2cpp_runtime_invoke` (exportado, `0x697374` → `0x626CB8`) | `ldrb w8, [x22, #0x4c]`; `tbz w8, #4` (método estático), depois `klass+0xE0` e `bl 0x624f80` (`0x626CDC`–`0x626CF4`) | só em método **estático** | confirmado no binário |
| `il2cpp_runtime_class_init` (exportado, `0x697378`) | `b 0x624f80` | sim, à força | confirmado no binário |
| `il2cpp_field_static_get_value` (exportado, `0x69713C` → `0x6253C4`) | nenhuma chamada a `0x624F80` em dois níveis | **não**: lê o campo como está (zero/nulo antes do `.cctor`) | confirmado no binário |
| `InitializeRuntimeMetadata` no começo dos métodos (`0x642B38` → `0x6340E4`) | só resolve ponteiros de metadata | não | confirmado no binário |

Exemplo de acesso a campo estático, em `XNAWorldCameraSettings.ApplyCamera`:

```
0xae0c80: ldr    x21, [x21, #0xf10]         ; GOT -> Terraria.Main_TypeInfo (nome exato pela RELA)
0xae0cb8: ldr    x0, [x21]
0xae0cbc: ldr    w8, [x0, #0xe0]            ; cctor_finished de Main
0xae0cc0: cbnz   w8, #0xae0cc8
0xae0cc4: bl     #0x642c14                  ; ClassInit(Main): roda Main..cctor
0xae0ccc: bl     #0xc69bb0                  ; Main.get_gameMenu
```

### 1.2 beforefieldinit

Lido da `global-metadata.dat` (v31, 8413 tipos de 88 bytes): `flags & 0x00100000` é beforefieldinit e o
bit 3 do `bitfield` é `has_cctor`. Confirmado na metadata.

| classe (índice do tipo) | beforefieldinit | tem `.cctor` |
|---|---|---|
| `Terraria.Main` (1017) | sim | sim |
| `Terraria.Item` (1001) | sim | sim |
| `Terraria.ID.ItemID.Sets` (1485) | sim | sim |
| `Terraria.Player` (1046) | sim | sim |
| `Terraria.NPC` (1022) | sim | sim |
| `Terraria.GameContent.TextureAssets` (2036) | sim | sim |
| `Terraria.Lang` (1079) | sim | sim |
| `Terraria.ID.ContentSamples` (1472) | sim | sim |
| `Terraria.GameContent.Items.ItemVariants` (2150) | **não** (construtor estático explícito) | sim |
| `PrefixLegacy.ItemSets` (2085) | sim | sim |
| `VirtualControllerInputState` (385) | sim | sim |
| `XNAUnityRunner` (776), `Terraria.Program` (1101) | sim | sim |

O que muda na prática:

- **Classe beforefieldinit:** o IL2CPP põe a checagem só antes de acesso a campo estático. Chamar um
  método estático dela não roda o `.cctor`, mas `new` roda, pelo `Object::New`.
- **`ItemVariants` (semântica precisa):** a checagem aparece também antes de chamada a método estático.
  Exemplo em `Item.SetDefaults`: `ClassInit(ItemVariants)` em `0xBAD190`, logo antes de
  `ItemVariants.HasVariant` em `0xBAD1A4`.
- **Em nenhum caso o IL2CPP roda um `.cctor` antes do primeiro gatilho.** Não existe inicialização
  antecipada.

### 1.3 O que no PC roda todos os `.cctor` está morto no celular

- No PC, `Program.StartForceLoad` cria uma thread que chama
  `ForceLoadAssembly(..., initializeStaticMembers: true)` → `ForceStaticInitializers` →
  `RuntimeHelpers.RunClassConstructor` em todo tipo do assembly.
- **No celular:**
  - `StartForceLoad` (`0x12AD008`) só grava `Program.LoadedEverything = 1` (`0x12AD054`). Não cria
    thread.
  - `ForceLoadThread` (`0x12AD064`) não tem chamador e não vira delegate.
  - `RunClassConstructor` (`0x1DD2D84`) só é chamado por `ForceStaticInitializers` (morto) e por
    `System.ComponentModel`.
- Confirmado no binário.

**Consequência.** Os `.cctor` que podem rodar antes de um ponto qualquer são exatamente os das classes
tocadas (campo estático, `new` ou `runtime_invoke` de estático) pelo código que roda antes desse ponto.

---

## 2. Linha do tempo da inicialização

Os horários vêm do `tlmod.log` (sessão das 15:17:16,043). As fases B e C não aparecem no log; o
instante delas só o aparelho confirma.

### Fase A: bibliotecas e `il2cpp_init`

| horário | evento | fonte |
|---|---|---|
| 16,043 | `libtlmod_bridge.so` e `libtlmod_runtime.so` carregadas; os construtores sobem as threads da ponte e do runtime ("sessão iniciada") | log; `entry.cpp:10`, `android_main.cpp:264` |
| 16,378 | a ponte vê a `libil2cpp.so` mapeada | log |
| ≈16,88 | `il2cpp_get_corlib()` fica não nulo (começo do `Runtime::Init`) | inferido do log (17,224 − 0,200 − 0,140) |
| ≈17,02 | marca de fim do `Runtime::Init` (byte `0x2BFE998`) | inferido; `terraria_offsets.h:508` |
| 17,224 | "il2cpp_init concluído (140 ms depois do corlib)", impresso depois de mais 200 ms de espera | log; `bridge.cpp:249` |
| 17,302 → 17,365 | a ponte instala os hooks (`Player.Hurt` primeiro, `Item.SetDefaults` em 17,321) | log |
| 17,562 | 4 mods carregados (na thread do runtime) | log |

O `Runtime::Init` só roda `.cctor` do corlib/System. Inferido: é código do runtime, sem Terraria.

### Fase B: `XNAUnityRunner.Awake` (`0xAE0B1C`)

Antes ou junto da cena, o Unity roda os dois únicos `[RuntimeInitializeOnLoadMethod]` do dump:

- `NativeLeakDetection.Initialize` (`0x220F5B8`);
- `ScriptableRuntimeReflectionSystemSettings.ScriptingDirtyReflectionSystemInstance` (`0x2236F4C`).

Os dois são do UnityEngine e não tocam Terraria. Os outros MonoBehaviours do jogo com
`Awake`/`OnEnable`/`Start` também não tocam as classes das tabelas:

- `DrPlatformEditorData` (`0x7173C4`);
- `ScreenshotTool` (`0xADF7CC`);
- `GUIControllerHardwareProfileControlsType` (`0xAF4FD4`);
- `InControl.InControlManager.OnEnable` (`0x1BAFB04`, 317 métodos percorridos).

Confirmado no binário; que estejam na cena, inferido.

Ordem real dentro do `Awake`. Os caminhos foram conferidos sem desvio condicional (só os de
`NullReferenceException`):

```
XNAUnityRunner.Awake 0xAE0B1C
├─ 0xAE0B8C DrPlatform.Setup                          (nenhuma classe de tabela)
├─ 0xAE0BAC LayoutDefinition.LoadReleaseLayout        (nenhuma classe de tabela)
├─ 0xAE0BEC UserManagement.SetupPrimary 0xAA7D08
│   └─ 0xAA7D64 SwitchPrimaryUser 0xAA7D68
│       (o laço sobre usuários existentes sai em 0xAA7FE4: na primeira vez a lista está vazia)
│       └─ 0xAA824C new LocalUser → LocalUser..ctor 0xAA2610
│           ├─ 0xAA27F4 new LocalUserGameState → ..ctor 0xAA2EF4
│           │   ├─ 0xAA339C new BigProgressBarSystem → .cctor 0x980224
│           │   │   └─ 0x980328 new EaterOfWorldsProgressBar → 0x983764 new NPC   ► NPC..cctor
│           │   ├─ 0xAA342C ClassInit(Main)                                        ► Main..cctor 0xD7336C
│           │   │   ├─ 0xD747F4 new Item                                           ► Item..cctor 0xBB8CF4
│           │   │   │   ├─ 0xBB8DC0 ClassInit(ItemID.Sets)                         ► ItemID.Sets..cctor
│           │   │   │   │   (fábrica 0x153C614: 77 sets)
│           │   │   │   ├─ 0xBB8E38 Factory.CreateIntSet: Item.cachedItemSpawnsByType
│           │   │   │   └─ 0xBB8EF4 / 0xBB8F0C ALLOC Item.staff / Item.claw
│           │   │   ├─ 0xD748E0 new Player                                         ► Player..cctor 0x105455C
│           │   │   │   └─ 0x1054910 Factory.CreateBoolSet: Player.ItemUsesRightFire
│           │   │   ├─ 0xD76B68 ALLOC Main.itemAnimations
│           │   │   └─ 0xD76E2C Main.TrialMode = 0
│           │   └─ 0xAA3480 new Item                   (Item..cctor já rodou)
│           ├─ 0xAA2A0C new GUIInstance → ..ctor 0x95AAAC (≈ 100 telas)
│           │   ├─ 0x95B070 new GUIVirtualInputController → .cctor 0x9F170C: new Item 0x9F174C
│           │   └─ 0x95BEA0 new VirtualControllerInputState                        ► .cctor 0x9FD274
│           │       └─ 0x9FD32C/0x9FD338 Factory.CreateIntSet: ItemCategories
│           └─ SetupState só se UserIndex (+0x78) ≠ 0 (0xAA2DB4–0xAA2DB8): não para o usuário 0
├─ 0xAE0C04 XNAWorldCameraSettings.ApplyCamera: ClassInit(Main) em 0xAE0CC4 (já rodou)
├─ 0xAE0C30 new Main + 0xAE0C40 Main..ctor 0xC8040C   (só campos, Game..ctor, GraphicsDeviceManager)
└─ 0xAE0C58 Game.UnityAwake → Main.UpdateTrialMode 0xC7CC30
    (lê e zera TrialMode em 0xC7CC88; com 0, não chama SwitchPrimaryUser nem SetupState)
```

Tudo confirmado no binário, menos uma ressalva: o primeiro disparo de `Main` é `0xAA342C`, a não ser que
algo ainda mais cedo no `LocalUser..ctor` também o toque. Nenhum dos membros criados antes tocou
`Main`, `Item`, `Player` ou `ItemID.Sets`:

- `SkyManager`, `FilterManager`, `OverlayManager`, `PersistentUserData`;
- `CreativePowerManager`, `ItemShopSellbackHelper`, `LockOnHelperStaticData`,
  `TileObjectPreviewData`.

Para a pergunta que importa ("roda no `Awake`?"), a resposta não muda.

### Fase C: `XNAUnityRunner.Start` (`0xAE1160`)

```
XNAUnityRunner.Start 0xAE1160 → 0xAE11E0 Game.UnityStart 0xB041D4
├─ 0xB0423C Main.UpdateTrialMode, 0xB04248 SocialAPI.Initialize(0)
├─ 0xB0427C Main.OnEnginePreload += Program.StartForceLoad   (só liga LoadedEverything)
└─ 0xB0428C Game.initialiseCallBack 0xB04040
    ├─ 0xB040B8 TextureAtlasDB.LoadTextureDBs  (atlas por nome de arquivo, não por tipo)
    └─ 0xB040C8 vslot 7 (+0x1A8) = Initialize → Main.Initialize 0xC85468
        ├─ 0xC85878 ClassInit(ContentSamples)                                       ► ContentSamples..cctor
        ├─ 0xC85880 ContentSamples.Initialize 0x152B1A0
        │   ├─ laço de NPCs: 0x152B3C8 NPC.SetDefaults 0xDAF1BC
        │   │   └─ se !Main.dedServ (0xDB4154): ClassInit(TextureAssets) 0xDB4170     ► TextureAssets..cctor
        │   │       (ALLOC 0x7A2DD4/0x7A2DEC: TextureAssets.Item / ItemFlame)
        │   └─ laço de itens (LOOP 0x152B4D4): 0x152B4FC Item.SetDefaults 0xBAD034
        │       ├─ 0xBAD174 BOUND (type ≥ 5456 → 0)
        │       ├─ 0xBAD190 ClassInit(ItemVariants)                                  ► ItemVariants..cctor (ALLOC 0x7CC914)
        │       ├─ 0xBADBAC Item.RebuildTooltip 0xBAAFE8 → 0xBAB034 ClassInit(Lang)  ► Lang..cctor
        │       │   (ALLOC 0x120646C/0x12064FC: _itemNameCache / _itemTooltipCache)
        │       └─ 0xBADBB4 BOUND (tipo−1 > 5454)
        ├─ 0xC85A60 if (Main.dedServ) Initialize_AlmostEverything   (no cliente, não)
        ├─ 0xC85A84 ClientInitialize 0xC86B2C
        │   ├─ 0xC86C90 LanguageManager, 0xC86E24 Main.LoadSettings (SetLanguage)
        │   └─ 0xC870C4 LoadPlayers só se Main.autoJoin (0xC870B0)
        └─ 0xC85A90 Game.Initialize 0xB04F14 → br vslot 8 (+0x1B8) → Main.LoadContent 0xC9EC24
```

A ordem dentro do `Start` está confirmada no binário. Que o primeiro `NPC.SetDefaults` e o primeiro
`Item.SetDefaults` sejam os do `ContentSamples.Initialize` é inferido: nenhum outro achado no `Awake`,
numa busca limitada a dois níveis a partir das telas do `GUIInstance`.

### Fase D: quadros de carregamento

| o quê | onde | evidência |
|---|---|---|
| carga adiada: `Lang.InitializeLegacyLocalization` (os nomes), `AssetInitializer.Load`, texturas | `Main.<LoadContent_Deferred>d__1863.MoveNext` `0xDA7D8C` (`0xDA7F6C`, `0xDA8288`, `0xDA82A0`) | confirmado no binário |
| `Initialize_AlmostEverything` (`Initialize_Items` LOOP `0xC8E034`, `Recipe.SetupRecipes` `0xC86534`) | chamado por `Main.DrawSplash` `0xD25A34` (`0xD25D34`, `0xD25E90`) | confirmado no binário; no log, receitas em **31,634** |
| `PostContentLoadInitialize` → `RebuildItemCreativeSortingIDsAfterRecipesAreSetUp` (`0xC9F31C`) → `SetCreativeMenuOrder` (LOOP `0x1530DE8`) | `Main.DrawSplash` (`0xD25D48`, `0xD25EA4`) | confirmado no binário |
| menu principal | — | log: 31,759 |

No log, o frame handler do TLMod criou as texturas dos mods em 20,950. Isso só diz quando as texturas
do jogo ficaram prontas, não quando foi o primeiro quadro (`ProcessTextures` adia enquanto o jogo
recusa, `mod_loader.cpp:206`).

### Fase E: menu e jogo

- `PrefixLegacy.ItemSets..cctor`: no primeiro sorteio de prefixo. O único acesso é
  `Item.GetRollablePrefixes` (`0xBAAC70`), chamado por `RollAPrefix`/`CanRollPrefix`.
- `ItemFilters.MiscFallback`: nos filtros de criação e duplicação (`GUIItemFilter.Matches` `0x9642E8`) e
  na interface da Jornada do PC.
- Arquivos `.plr`: lidos quando o jogador abre a lista (log: 44,212).

---

## 3. Tabelas de item: quem cria e quando

Na coluna "antes de `LaunchGame`?", "—" quer dizer que a pergunta não se aplica: `LaunchGame` nunca roda.

| tabela | tamanho | criada em (RVA) | primeiro disparo | fase | antes de `LaunchGame`? | confiança |
|---|---|---|---|---|---|---|
| `ItemID.Sets.*` (77) | 5456 | `ItemID.Sets..cctor`, fábrica `0x153C614` | `Item..cctor` `0xBB8DC0` ← `Main..cctor` `0xD747F4` ← `LocalUserGameState..ctor` `0xAA342C` ← `Awake` | **B** | — | confirmado no binário |
| `Item.cachedItemSpawnsByType` (fábrica de `ItemID.Sets`) | 5456 | `Item..cctor` `0xBB8E38` | junto com `Item..cctor` | **B** | — | confirmado no binário |
| `Item.staff`, `Item.claw` | 5456 | `Item..cctor` `0xBB8EF4`, `0xBB8F0C` | idem | **B** | — | confirmado no binário |
| `Player.ItemUsesRightFire` (fábrica) | 5456 | `Player..cctor` `0x1054910` | `Main..cctor` `0xD748E0` (`new Player`) | **B** | — | confirmado no binário |
| `Main.itemAnimations` | 5456 | `Main..cctor` `0xD76B68` | `LocalUserGameState..ctor` `0xAA342C` | **B** | — | confirmado no binário |
| `VirtualControllerInputState.ItemCategories` (**mobile**, fábrica) | 5456 | `VirtualControllerInputState..cctor` `0x9FD32C`/`0x9FD338` | `GUIInstance..ctor` `0x95BEA0` ← `LocalUser..ctor` `0xAA2A0C` | **B** | — | confirmado no binário |
| `VirtualControllerInputState.IdLookup` (**mobile**) | 5456 | `SetupLookups` `0x9FB66C` | ninguém: só `SaveDefaultCategories`, que não tem chamador | **X** | nunca | confirmado no binário |
| `ContentSamples.*` (dicionários) | sem tamanho | `ContentSamples..cctor` `0x152E6C4` | `Main.Initialize` `0xC85878` | **C** | — | confirmado no binário |
| `ContentSamples.ItemsByType` (amostras) | laço até 5456 | `ContentSamples.Initialize` (LOOP `0x152B4D4`) | `Main.Initialize` `0xC85880` | **C** | — | confirmado no binário |
| `ContentSamples.ItemCreativeSortingId` | laço até 5455 | `SetCreativeMenuOrder` (LOOP `0x1530DE8`) | `PostContentLoadInitialize` `0xC9F31C` ← `DrawSplash` | **D** | — | confirmado no binário |
| `TextureAssets.Item`, `ItemFlame` | 5456 | `TextureAssets..cctor` `0x7A2DD4`, `0x7A2DEC` | primeiro `NPC.SetDefaults` (`0xDB4170`) ← `ContentSamples.Initialize` `0x152B3C8` | **C** | — | confirmado no binário (caminho); inferido (que seja o primeiro) |
| `ItemVariants._variants` | 5456 | `ItemVariants..cctor` `0x7CC914` | primeiro `Item.SetDefaults` (`0xBAD190`) ← `ContentSamples.Initialize` `0x152B4FC` | **C** | — | idem |
| `Lang._itemNameCache`, `_itemTooltipCache` | 5456 | `Lang..cctor` `0x120646C`, `0x12064FC` | `Item.RebuildTooltip` `0xBAB034` ← primeiro `Item.SetDefaults` | **C** (alocação); **D** (os nomes: `InitializeLegacyLocalization`, `0xDA7F6C`) | — | idem |
| `PrefixLegacy.ItemSets.*` (6) | 5456 | `PrefixLegacy.ItemSets..cctor`, fábrica `0x7C23A4` | `Item.GetRollablePrefixes` `0xBAAC70` (único acesso) | **E** | — | confirmado no binário |
| `ItemFilters.MiscFallback._fitsFilterByItemType` | 5456 | `MiscFallback..ctor` `0x8FB04C` | `GUIItemFilter.Matches` `0x9642E8` | **E** | — | confirmado no binário |
| `LootSimulationItemCounter` (2) | 5456 | `..ctor` `0x7C9C0C`, `0x7C9C1C` | `LootSimulator.Run` `0x7C93B0`, sem chamador nem delegate | **X** | nunca | confirmado no binário |

Comparando com a seção 6 do estudo:

- **"`LaunchGame` → `Lang.InitializeLegacyLocalization` → cria `Main`":** vale para o PC e para o
  servidor dedicado, não para o celular.
- **"os construtores estáticos rodam dentro do caminho de `Main.Initialize`":** só vale para
  `ContentSamples`, `TextureAssets`, `ItemVariants` e `Lang`. `ItemID.Sets`, `Item`, `Player`, `Main` e o
  `VirtualControllerInputState` rodam antes, no `Awake`.

---

## 4. O ponto de hook

| candidato | instante | o que já rodou | garantia | veredito |
|---|---|---|---|---|
| `Program.LaunchGame` (`0x12ADA7C`) | nunca | — | — | **inútil**: nunca é chamado no celular |
| hooks da ponte de hoje | fim do `Runtime::Init` + 200 ms + resolução (no log, 17,30–17,37) | desconhecido: o `Awake` corre em paralelo na thread do Unity | nenhuma: é corrida | **não serve** para corrigir constantes |
| pré-`XNAUnityRunner.Awake` (`0xAE0B1C`) | primeiro código gerenciado do Terraria | só UnityEngine e corlib | **só se o hook for instalado antes do `Awake`**: com a ponte de hoje, não há garantia | bom ponto **gerenciado**, mas depende de quem o instala |
| **`il2cpp_init` (exportado, `0x696800`), com o `il2cpp_init_utf16` (`0x69682C`)** | antes de qualquer código gerenciado (fase A), na thread do Unity | nada do Terraria | **total**: dentro da chamada, nada gerenciado roda; depois do original, o Unity só volta a rodar código quando o hook retorna | **recomendado** |
| assim que a `libil2cpp.so` aparece mapeada | fase A, na thread da ponte | nada | corrida com o `il2cpp_init` (no log, uns 500 ms entre o mapeamento e o corlib) | reserva, se o hook no `il2cpp_init` não pegar |

**Por que o `il2cpp_init`.**

- **As correções não dependem do runtime.** São 4 bytes no `.text` da `libil2cpp.so`: só precisam da
  biblioteca mapeada e de ninguém executando aquelas páginas. Antes do `il2cpp_init`, ninguém executa.
- **A quantidade de itens de mod pode ser conhecida ali.**
  - O `register_item` recusa item fora do `content.items` do manifest (`mod_loader.cpp:455`).
  - Então a soma dos `content.items` é um teto conhecido só com JSON, sem IL2CPP e sem carregar
    biblioteca de mod.
  - Hoje, porém, os manifests só são lidos no `LoadAll`, depois de a ponte ficar pronta
    (`android_main.cpp:200` e `223`; no log, 17,56). A leitura teria de vir para antes do `il2cpp_init`.
- **Os hooks gerenciados podem entrar sem corrida.** Logo depois de chamar o `il2cpp_init` original, o
  `Runtime::Init` terminou e ainda estamos na thread do Unity. `Awake`, `ContentSamples.Initialize`,
  `Item.SetDefaults` e os demais podem ser instalados ali, antes de qualquer cena.
- **O hook é viável.**
  - **Como o ShadowHook faz.** O ShadowHook 2.0.1 aceita hook por nome de símbolo numa biblioteca ainda
    não carregada; o hook fica pendente.
    - Ele o instala no `soinfo::call_constructors` da biblioteca, antes dos construtores dela
      (`sh_task.c:355`, `sh_linker.c:436`–`460`), dentro do `dlopen` que o Unity fez.
    - O Unity só chama o `il2cpp_init` depois do carregamento. Inferido: ele precisa do endereço
      exportado.
  - **Monitor do linker.** Isso depende do monitor do linker, que o log mostra funcionando neste
    aparelho ("monitor do linker ok", 17,297).
  - **Folga.** A `libtlmod` entra antes da `libil2cpp.so`. A sessão começa em 16,043, e a ponte, que
    consulta a cada 100 ms (`bridge.cpp:74`), vê a `libil2cpp.so` em 16,378: a folga é de pelo menos
    uns 230 ms.
  - **`shadowhook_init`.** Hoje ele só roda no primeiro hook, depois do `il2cpp_init` (`entry.cpp:11`);
    no log, levou uns 55 ms com o monitor (17,243 → 17,297).
    - Movido para o começo da thread da ponte, cabe na folga deste aparelho.
    - No construtor não pode, pelo lock do linker (`hook_shadowhook.cpp:120`).
    - Nos outros aparelhos, só o aparelho confirma ([E2](#e2-o-hook-no-il2cpp_init-chega-a-tempo)).
- **São dois nomes.** O `Runtime::Init` (`0x6258AC`) tem três chamadores diretos nesta build:
  - o `il2cpp_init` (`0x69681C`);
  - o `il2cpp_init_utf16` (`0x696868`), que não passa pelo `il2cpp_init`: converte o nome, repete o
    `setlocale` e chama o `Runtime::Init` direto;
  - o `DllGetActivationFactory` (`0x6840D0`), a ativação WinRT do Windows, que no Android ninguém chama
    (inferido).

  ```
  ; il2cpp_init 0x696800
  0x696814: bl     setlocale
  0x69681c: bl     #0x6258ac                  ; Runtime::Init
  ; il2cpp_init_utf16 0x69682C
  0x696840: bl     #0x69b250                  ; UTF-16 -> UTF-8
  0x696858: bl     setlocale
  0x696868: bl     #0x6258ac                  ; Runtime::Init, sem passar pelo il2cpp_init
  ```

  Confirmado no binário. Qual dos dois o Unity chama está na `libunity.so`, que não faz parte do
  material: só o aparelho confirma. Por isso o hook pendente vai nos dois, e o
  [E2](#e2-o-hook-no-il2cpp_init-chega-a-tempo) registra qual disparou.
- **O próprio repositório já aponta isso.**
  - O comentário em `bridge.cpp:225` diz "Alternativa mais precisa: hookar il2cpp_init".
  - O `il2cpp_bridge/README.md` (linhas 148–150) ressalva que o `il2cpp_init` "pode já estar rodando
    quando a ponte começa". Isso vale se a `libtlmod` carregar depois da `libil2cpp.so`; com o
    carregamento atual, o log mostra a ordem contrária.
- **Se o hook chegar tarde**, por exemplo com `il2cpp_get_corlib()` já não nulo ao instalar, dá para
  detectar. Aí vale a regra do estudo: falhar fechado e voltar à casca, ou realocar (seção 5.2 do
  estudo).

**Onde preencher o conteúdo**, sempre depois de quem cria a tabela:

| conteúdo | depois de | ponto sugerido |
|---|---|---|
| valores em `ItemID.Sets`, `Item.staff/claw`, `cachedItemSpawnsByType`, `ItemUsesRightFire`, `ItemCategories` | fase B | pós-`XNAUnityRunner.Awake` (antes do `Start`, que lê os Sets no `SetDefaults`) |
| a amostra do item em `ContentSamples` e os nomes persistentes | `ContentSamples.Initialize` | o hook em `Item.SetDefaults` monta a amostra; os dicionários de nome persistente, no pós-`ContentSamples.Initialize` |
| nome e tooltip (`Lang`) | `Lang.InitializeLegacyLocalization` (D) e a troca de idioma | pós-`InitializeLegacyLocalization` e no evento de idioma |
| textura (`TextureAssets.Item[5456]`) | carga de assets (D) | pós-`AssetInitializer.Load`, ou os hooks de desenho que já existem |
| ordem da Jornada, receitas | `PostContentLoadInitialize`, `SetupRecipes` | os hooks de receita que já existem |

**Regra para o TLMod.** Antes da correção das constantes, a ponte não pode chamar método estático do jogo
por `il2cpp_runtime_invoke` nem criar objeto do jogo: os dois disparam `.cctor` (seção 1). Pode resolver
classes e ler campos com `il2cpp_field_static_get_value`, que não dispara.

A ponte de hoje já segue essa regra no init:

- os `Resolve*` e `Install*` chamados pelo `TLMod_Init` (`bridge.cpp:263`–`284`) só procuram classes,
  métodos e campos e leem campos estáticos;
- nenhum deles chama `runtime_invoke` ou `il2cpp_object_new`;
- o `Item` de sonda do `TLMod_ReadItemTraits` só é criado na thread do jogo e depois de `ItemID.Sets`
  existir (`item_traits.cpp:110`–`112`).

---

## 5. As 44 ocorrências do estudo

### 5.1 Varredura nova

| forma procurada | encontradas | resultado |
|---|---|---|
| `mov wN, #0x1550` (5456) | 37 | as 37 do estudo |
| `mov wN, #0x154F` (5455) | 9 | 3 de contagem (2 BOUND do `ItemTagHandler`, LOOP do `SetCreativeMenuOrder`) e 6 IDs, todos já no estudo |
| `mov wN, #0x154E` (5454) | 11 | 4 BOUND e 7 IDs, todos no estudo |
| `mov wN, #0x154D` (5453) e `mov wN, #-5453` | 7 e 4 | IDs, todos no estudo |
| `mov #0x2AA0` (2N), `mov #0x1558` (N+8) | 1 e 1 | o blob do `VirtualControllerInputState..cctor` e `System.Enum.ToUInt64`, como o estudo diz |
| **forma partida**: `add/sub … #1, lsl #12` seguido de `cmp/add … #0x550` (não procurada pelo estudo) | 2 | as duas em `Main.DrawNPCDirect` (`0xBF944C`, `0xC2819C`): `sp + 0x1550`, endereço de pilha, não contagem |
| palavra `0x1550` em `.rodata`/`.data` (constante vetorial ou em par, não procurada pelo estudo) | 4 + 1 | nenhuma lida por código (`adrp` + `ldr`/`ldp` de 32, 64 ou 128 bits); as do `.rodata` estão no meio de sequências consecutivas (tabelas de caracteres) |
| campo estático com a contagem | 0 | `ItemID.Count` é `const short Count = 5456`; não há `maxItemTypes` nesta build |
| `cmp`/`add` com imediato 12 bits (com ou sem `lsl #12`) | 0 | 5456 não cabe, e `0x1550` não é múltiplo de 4096 |

**A lista do estudo está completa.** Confirmado no binário, para as formas acima. Um limite escrito de
outro jeito (comparação com registrador carregado de um objeto, por exemplo) não aparece em nenhuma
varredura por constante; o alarme do estudo continua valendo.

### 5.2 Em que fase cada uma roda

Por grupo de fase:

| fase | ocorrências | quantas |
|---|---|---|
| **B** (`Awake`) | ALLOC `0xBB8EF4`, `0xBB8F0C` (`Item.staff/claw`), `0xD76B68` (`Main.itemAnimations`); FACTORY `0x153C614` (`ItemID.Sets` e mais 3 externos) | 4 |
| **C** (`Start`) | ALLOC `0x7A2DD4`, `0x7A2DEC` (`TextureAssets`), `0x7CC914` (`ItemVariants`), `0x120646C`, `0x12064FC` (`Lang`); BOUND `0xBAD174`, `0xBADBB4` (`Item.SetDefaults`); LOOP `0x152B4D4` (`ContentSamples.Initialize`) | 8 |
| **D** (carregamento) | LOOP `0xC8E034` (`Main.Initialize_Items`), `0x1530DE8` (`SetCreativeMenuOrder`) | 2 |
| **E** (menu e jogo) | FACTORY `0x7C23A4` (`PrefixLegacy.ItemSets`); ALLOC `0x8FB04C` e LOOP `0x8FB07C` (`MiscFallback`); BOUND `0xBB8A6C`, `LoadTutorialPlayer` ×4, `LoadPlayer` ×4, `Lang.GetItemName`, `CommonCode` ×3, `ItemTagHandler` ×2, `ControlsTouchTagHandler` ×4, `Player.RefreshInfoAccs`, `Player.UpdateEquips`, `ItemSlot.GetItemLight` | 25 |
| **X** (morto) | ALLOC `0x9FB66C` (`IdLookup`), `0x7C9C0C`, `0x7C9C1C` (`LootSimulationItemCounter`); LOOP `0x9FB9D0`, `0x9FBE34` (categorias de controle) | 5 |

Contra cada ponto:

- **Antes do `il2cpp_init`:** nenhuma roda. Confirmado: nenhuma está em código do runtime.
- **Antes de um pré-hook no `Awake`:** nenhuma roda. Inferido: depende de nada mais na cena tocar essas
  classes (seção 2).
- **Antes dos hooks da ponte de hoje:** podem rodar as 12 das fases B e C. Só o aparelho confirma.
- **Antes de `LaunchGame`:** todas, já que ele nunca é chamado.

Correções à coluna "Quando" do estudo:

| ocorrência | estudo | esta auditoria |
|---|---|---|
| `0x9FB66C`, `0x9FB9D0`, `0x9FBE34` (`VirtualControllerInputState`) | "antes" / "antes/depois" | **nunca** (código morto) |
| `0x7C9C0C`, `0x7C9C1C` (simulador de loot) | "depois" | **nunca** |
| `0x7C23A4` (`PrefixLegacy.ItemSets`) | "antes" | **E**: só no primeiro sorteio de prefixo |
| `0x7A2DD4`, `0x7A2DEC`, `0x120646C`, `0x12064FC`, `0x7CC914` | "antes" (sem fase) | **C**: dentro do `ContentSamples.Initialize`, no `Start` |
| `0x153C614`, `0xBB8EF4`, `0xBB8F0C`, `0xD76B68` | "antes" | **B**: dentro do `Awake`, antes de qualquer `Main.Initialize` |

---

## 6. Experimentos no aparelho

Os dois experimentos precisam de uma build de teste da ponte. Isso é fora desta auditoria, que não altera
código. Os resultados saem no `tlmod.log` (painel → Log) e no logcat (`TLModBridge`).

### E1 (mínimo): flags dos `.cctor` e o `Awake`

1. Uma função de log, `LogCctor(tag)`, que para cada classe chama `il2cpp_class_from_name` e lê
   `cctor_started` (`+0xDC`) e `cctor_finished` (`+0xE0`). As classes são `Main`, `Item`,
   `ItemID.Sets`, `Player`, `NPC`, `VirtualControllerInputState`, `ContentSamples`, `TextureAssets`,
   `Lang`, `ItemVariants` e `PrefixLegacy.ItemSets`. Essas leituras não disparam o `.cctor` (seção 1).
   Uma linha por chamada, por exemplo `cctor[pós-init] Main=0/0 Item=0/0 … tid=…`.
2. Chamar `LogCctor` em três pontos:
   - logo depois de "il2cpp_init concluído";
   - logo depois do último hook instalado;
   - em hooks só de log: pré e pós `XNAUnityRunner.Awake` (`0xAE0B1C`), pré e pós
     `XNAUnityRunner.Start` (`0xAE1160`), pré e pós `ContentSamples.Initialize` (`0x152B1A0`) e no
     primeiro `Main.DoUpdate`.
3. Abrir o jogo até o menu.
4. **Esperado por esta auditoria:**
   - pré-`Awake`: tudo `0/0`;
   - pós-`Awake`: `Main`, `Item`, `ItemID.Sets`, `Player`, `NPC` e `VirtualControllerInputState` em
     `1/1`; os demais em `0/0`;
   - pré-`ContentSamples.Initialize`: `ContentSamples` em `1/1`;
   - pós-`ContentSamples.Initialize`: `TextureAssets`, `Lang` e `ItemVariants` em `1/1`;
   - no menu: `PrefixLegacy.ItemSets` ainda `0/0`.
5. **Leitura do resultado:**
   - **as linhas do hook do `Awake` não aparecem**, e a primeira linha já mostra `Main=1/1`: a ponte
     chegou depois do `Awake` (corrida perdida). Corrigir constantes só no `il2cpp_init`;
   - **as linhas aparecem**: nesta abertura e neste aparelho a ponte chegou a tempo. Repita algumas
     vezes: não é garantia.

### E2: o hook no `il2cpp_init` chega a tempo

1. Na build de teste, chamar o `shadowhook_init` logo no começo da thread da ponte e registrar ali dois
   hooks pendentes por símbolo: `libil2cpp.so!il2cpp_init` e `libil2cpp.so!il2cpp_init_utf16`.
   Registrar no log o horário do registro.
2. No hook, registrar o horário, a thread e `il2cpp_get_corlib()`, que deve ser nulo na entrada. Depois
   chamar o original e chamar `LogCctor("pós-il2cpp_init")`.
3. **Esperado:** um dos dois hooks roda uma vez, na thread principal do Unity, antes de qualquer linha
   de E1, com tudo `0/0`. O log diz qual dos dois foi.
4. **Se não rodar** (a biblioteca carregou antes do registro), o log mostra isso, e vale a reserva da
   seção 4.

### E3: `PrefixLegacy.ItemSets` só no jogo

1. Com E1 ativo, entrar num mundo e pedir uma arma pelas Ferramentas (Dar item). Ou fabricar ou reforjar
   uma arma, que sorteia prefixo.
2. **Esperado:** `PrefixLegacy.ItemSets` passa a `1/1` no primeiro sorteio, não antes.

---

## 7. Respostas diretas

### Pergunta 1. Quais `.cctor` podem rodar antes de `Program.LaunchGame`?

**`LaunchGame` não roda no celular.** Não tem chamador, não é usado como delegate e não aparece como
string. O corpo é o caminho do PC e do servidor dedicado (`Main.DedServ`, `Game.Run`). Então, a rigor,
**todos** os `.cctor` do jogo rodam sem ele.

A pergunta útil é o que roda antes de cada ponto real. Os gatilhos, nesta build (seção 1):

- acesso a campo estático;
- todo `new`;
- `runtime_invoke` de método estático;
- `il2cpp_runtime_class_init`.

Beforefieldinit só muda onde o compilador põe a checagem. Não existe inicialização antecipada, e o
`ForceStaticInitializers` do PC está morto.

| antes de… | `.cctor` |
|---|---|
| o primeiro código do Terraria (`Awake`) | só corlib/System (`Runtime::Init`) e UnityEngine (os dois `RuntimeInitializeOnLoadMethod`); **nenhum** do Terraria |
| o fim do `Awake` | `LocalUser`, `UserManagement`, `LayoutDefinition`, `XNAUnityRunner` e cerca de 100 telas `GUI*`; **`NPC`, `Main`, `Item`, `ItemID.Sets`, `Player`, `VirtualControllerInputState`** |
| o fim do `Start` | os anteriores e **`ContentSamples`, `TextureAssets`, `ItemVariants`, `Lang`** |
| o menu | os anteriores e as classes da carga adiada (`LoadContent_Deferred`, `Initialize_AlmostEverything`) |
| o jogo | `PrefixLegacy.ItemSets` (sorteio de prefixo), `MiscFallback` (filtros) |

### Pergunta 2. Cada tabela é criada antes ou depois de `LaunchGame`, e por qual caminho?

Nenhuma depende de `LaunchGame`. O caminho e a fase de cada uma estão na
[tabela da seção 3](#3-tabelas-de-item-quem-cria-e-quando). Em resumo:

- **No `Awake`:** `ItemID.Sets` (e os três sets externos da mesma fábrica), `Item.staff/claw`,
  `Main.itemAnimations` e a tabela do mobile `ItemCategories`.
- **No `Start`:** `TextureAssets`, `Lang` (os nomes, só na carga adiada), `ContentSamples` e
  `ItemVariants`.
- **No jogo:** `PrefixLegacy.ItemSets`.
- **Nunca:** a tabela do mobile `IdLookup` e o simulador de loot.

### Pergunta 3. O hook em `Program.LaunchGame` é o melhor ponto?

**Não: ele nunca dispara.** O ponto mais seguro é o `il2cpp_init` (`0x696800`), junto com o
`il2cpp_init_utf16` (`0x69682C`), que chega ao `Runtime::Init` sem passar por ele (seção 4):

1. ler os manifests;
2. atribuir os IDs;
3. corrigir as constantes;
4. chamar o original;
5. instalar ali mesmo os hooks gerenciados (pós-`Awake`, `ContentSamples.Initialize`, `Item.SetDefaults`
   e os demais).

Tudo isso na thread do Unity, antes da primeira cena. Um pré-hook no `XNAUnityRunner.Awake` seria o
melhor ponto **gerenciado**, mas só se instalado a tempo. A ponte de hoje não garante isso.

### Pergunta 4. As 44 ocorrências continuam completas? Alguma roda antes do ponto proposto?

**Completas.** A varredura nova (seção 5.1) não achou forma nova.

- **Código morto:** 5 nunca rodam.
- **Antes do `il2cpp_init`:** nenhuma roda.
- **Antes do ponto do estudo (`LaunchGame`):** todas, porque ele não roda.
- **Antes dos hooks da ponte de hoje:** podem rodar as 12 das fases B e C. É corrida, e
  [E1](#e1-mínimo-flags-dos-cctor-e-o-awake) mede.

### Pergunta 5. Qual é o experimento mínimo que confirma a ordem?

[E1](#e1-mínimo-flags-dos-cctor-e-o-awake):

1. ler `cctor_started`/`cctor_finished` (`+0xDC`/`+0xE0`) das 11 classes logo depois de "il2cpp_init
   concluído";
2. ler de novo em hooks só de log no pré e pós `XNAUnityRunner.Awake`, pré e pós `Start` e pré e pós
   `ContentSamples.Initialize`;
3. registrar tudo no `tlmod.log`.

Se o hook do `Awake` não registrar nada e a primeira leitura já mostrar `Main=1/1`, a ponte perdeu a
corrida. [E2](#e2-o-hook-no-il2cpp_init-chega-a-tempo) confirma que o hook no `il2cpp_init` chega antes
de tudo.

---

## 8. Veredito para a primeira entrega (item 5456)

**Viável, desde que a correção aconteça no `il2cpp_init`** (ou no `il2cpp_init_utf16`, o que o Unity
chamar). Com hook em `Program.LaunchGame`, não: o hook nunca dispararia. Mesmo trocando o ponto para algo
instalado pela ponte de hoje, as tabelas das fases B e C provavelmente já existiriam com 5456 posições.
Aí cairíamos no caminho mais arriscado da realocação.

Para um único item de teste (contagem nova = 5457), o mínimo é:

1. **No hook do `il2cpp_init`/`il2cpp_init_utf16`** (antes do original): somar 1 à constante das
   ocorrências que precisam crescer (`0x1550` → `0x1551`, salvo onde indicado).
   - ALLOC `0xBB8EF4`, `0xBB8F0C`, `0xD76B68`, `0x7A2DD4`, `0x7A2DEC`, `0x7CC914`, `0x120646C`,
     `0x12064FC`.
   - FACTORY `0x153C614`, `0x7C23A4`.
   - BOUND do `Item.SetDefaults`: `0xBAD174` (5456 → 5457) e `0xBADBB4` (5454 → 5455), para o jogo
     montar o tipo.
   - LOOP `0x152B4D4` e `0xC8E034` (5456 → 5457) e `0x1530DE8` (5455 → 5456), para a amostra, as
     animações e a ordem da Jornada incluírem o item.

   Ficam como estão:

   - os BOUND de `LoadPlayer`/`LoadTutorialPlayer` e de `Item.DeserializeFrom`: o arquivo paralelo cuida
     do save (auditoria 01);
   - as 5 em código morto.

   Os demais BOUND de interface (tags de texto, `Lang.GetItemName`, drops, equipamentos, luz do slot)
   entram conforme cada tela for testada. Cada uma fica a 4 bytes de distância (estudo, seção 5).
2. **Logo depois do original, na mesma chamada:** instalar os hooks que já existem e mais o
   pós-`XNAUnityRunner.Awake`, para preencher as tabelas da fase B antes do `Start`.
3. **Conteúdo:**
   - `Item.SetDefaults(5456)` montado pelo hook, como a casca faz hoje, mas com `type` = 5456;
   - os Sets do item no pós-`Awake`;
   - nome e tooltip no pós-`Lang.InitializeLegacyLocalization`;
   - textura em `TextureAssets.Item[5456]` depois da carga de assets, ou pelos hooks de desenho atuais;
   - nome persistente nos dicionários do `ContentSamples` (a pesquisa da Jornada grava por nome).
4. **Faixa de IDs:** o registro de conteúdo começa os itens em 8192 (`content_registry.cpp:25`). Para o
   teste, o primeiro ID de item passaria a ser 5456.
5. **Antes de tudo, E1 e E2** no aparelho, para confirmar as flags e que o hook do `il2cpp_init` roda.

Riscos que continuam, do estudo e da auditoria 01:

- tile entities e rede gravam o número cru;
- o arquivo paralelo precisa tirar o item 5456 do `.plr`/`.wld` como faz com a casca;
- uma atualização do jogo muda a tabela de assinaturas: falhar fechado e voltar à casca.

---

## 9. Lacunas, em ordem de risco

| # | lacuna | consequência | confiança |
|---|---|---|---|
| 1 | **O ponto de hook do estudo não existe** (`LaunchGame` não roda no celular) | um plano feito em cima dele nunca corrige nada; as tabelas ficam com 5456 e o item vira ar | confirmado no binário |
| 2 | **A ponte instala os hooks em corrida com o `Awake`** (espera o fim do `Runtime::Init` e mais 200 ms) | as 4 ocorrências da fase B e as 8 da C podem rodar antes de qualquer hook | confirmado no binário (ordem); só o aparelho confirma (tempo) |
| 3 | **O estudo erra fases e o caminho de carga** (seção 6 do estudo) | a ordem de implementação confia em `Main.Initialize` como barreira, mas `ItemID.Sets`, `Item` e `Main` nascem no `Awake` | confirmado no binário |
| 4 | **O conteúdo tem pontos diferentes da alocação:** `Lang` aloca no `Start` e preenche na carga adiada (D); texturas na carga de assets (D) | preencher cedo demais é sobrescrito; tarde demais deixa telas sem nome ou textura | confirmado no binário |
| 5 | **Chamadas do TLMod podem disparar `.cctor` fora de ordem:** `runtime_invoke` de estático e `il2cpp_object_new` rodam `ClassInit` | código novo que consulte o jogo antes da correção cria as tabelas com 5456 na thread do TLMod; o init de hoje da ponte não faz isso (seção 4) | confirmado no binário (mecanismo) e no código (init de hoje) |
| 6 | **Tempo das fases B e C não medido** | não se sabe quanta folga existe entre o `il2cpp_init` e o `Awake` neste e em outros aparelhos | só o aparelho confirma |
| 7 | **Não se sabe qual export o Unity chama** (`il2cpp_init` ou `il2cpp_init_utf16`; a `libunity.so` não está no material) | um hook só no `il2cpp_init` pode nunca disparar; com hook nos dois, o risco some | confirmado no binário (os dois caminhos); só o aparelho confirma (qual) |

---

## 10. Ações recomendadas

1. **Rodar E1 e E2** numa build de teste da ponte, antes de qualquer implementação, e registrar o
   resultado no estudo.
2. **Trocar o ponto de partida do plano** (estudo, seções 6, 9, 10 e 11) de `Program.LaunchGame` para
   `il2cpp_init`:
   - hook pendente por símbolo no `il2cpp_init` e no `il2cpp_init_utf16`, registrado logo no começo da
     thread da ponte, com o `shadowhook_init` movido para lá;
   - dentro dele: ler os manifests (hoje lidos só no `LoadAll`), atribuir os IDs, corrigir as
     constantes, chamar o original e instalar os hooks gerenciados;
   - se chegar tarde (`il2cpp_get_corlib()` não nulo ao instalar): falhar fechado e voltar à casca.
3. **Atualizar a coluna "Quando" do estudo** com as fases desta auditoria (seção 5.2):
   - 5 ocorrências em código morto saem da tabela de assinaturas;
   - `PrefixLegacy.ItemSets` vai para depois;
   - `Lang`, `TextureAssets` e `ItemVariants` vão para o `Start`.
4. **Pontos de preenchimento** (seção 4):
   - pós-`XNAUnityRunner.Awake` para os Sets e os arrays da fase B;
   - pós-`ContentSamples.Initialize` para os dicionários de nome persistente;
   - pós-`Lang.InitializeLegacyLocalization` (e troca de idioma) para nome e tooltip;
   - depois da carga de assets para a textura.
5. **Regra no runtime**: nada de `runtime_invoke` de estático nem `new` de objeto do jogo antes da
   correção. Os mods só consultam o jogo depois do primeiro quadro, como o catálogo de nomes já faz.
6. **Reserva**: manter a realocação (estudo, seção 5.2) para a build em que o hook no `il2cpp_init`
   chegar tarde. `ItemID.Sets` e as outras tabelas estáticas não têm cópia da referência (estudo,
   seção 4.1), então trocar o campo basta. As fábricas precisam do `_size` corrigido.
7. **Para os NPCs, no futuro**: o `NPC..cctor` também roda no `Awake` (pela barra de progresso de chefe,
   seção 2). A mesma correção no `il2cpp_init` vale para eles.
