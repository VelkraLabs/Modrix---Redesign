# Auditoria 09: recuperação de falhas e confiança nos mods

Terraria Mobile 1.4.4.9 (Unity 2021.3.56f2, IL2CPP, Android arm64), comparado com o TLMod da branch
`ccr-bbd92bf9-i01buk` (código da ponte e do runtime no commit `f917b04`; os commits seguintes só
acrescentaram as auditorias e arquivos do jogo: as bibliotecas em `054be54` e o manifest em `4960d19`).

Auditoria só de leitura. As fontes são:

- a desmontagem da `libil2cpp.so` e o dump (`dump.cs`, `script.json`);
- a desmontagem da `libunity.so`, só nos tratadores de sinal. Ela chegou no commit `054be54`, junto com a
  `libmain.so`, e é do Unity 2021.3.56f2 (`667925324b48`), a mesma versão do jogo;
- o `AndroidManifest.xml` do APK (commit `4960d19`), decodificado do XML binário: pacote
  `com.and.games505.TerrariaPaid`, versão 1.4.4.9.8 (código 301423);
- o código da ponte (`il2cpp_bridge/`), do runtime, da API e do painel (`modapi/`);
- o código do ShadowHook 2.0.1, que a ponte compila;
- as bibliotecas do TLMod compiladas para arm64 (`libtlmod_bridge.so`, `libtlmod_runtime.so` e os mods de
  exemplo), só para conferir tabelas de desenrolamento e dependências, e as do pacote de teste (branch
  `entrega/tlmod-teste`), para as versões da libc que elas exigem;
- as auditorias 01 a 04.

O relatório do Módulo 8 não está no repositório (`docs/auditoria/` tem de 01 a 04); a relação com ele parte
da premissa do enunciado, de que scripts podem ser isolados.

Dump e metadata ficam fora do repositório. Nenhum código foi alterado; este arquivo é o único acréscimo.

Fora do escopo: licença, proteção contra alteração e checagens de integridade do jogo, assinatura e
empacotamento do APK e anticheat. Isso inclui itens que chegaram com os arquivos do jogo e não foram
analisados:

- a `libstub.so`, que traz cadeias da biblioteca de licença do Google Play e de um protetor de APK;
- a `libpairipcore.so`;
- os componentes `com.pairip.*` do manifest: a classe `Application`, uma `Activity` e um provedor de
  licença.

A seção 6 trata só do pacote de mod.

**Módulo 9:** recuperação de falhas e confiança nos mods.

1. Modo seguro: detectar que a última abertura travou ou fechou durante a carga dos mods ou durante o jogo,
   apontar o mod suspeito, desligá-lo sozinho na abertura seguinte e mostrar o motivo na Oficina.
2. Sinais: quais sinais o IL2CPP e o Unity tratam, e se o TLMod pode instalar ou encadear um tratador de
   SIGSEGV sem quebrar isso.
3. Exceções: uma exceção do jogo ou uma exceção C++ de um mod pode atravessar a fronteira da API e
   derrubar o processo? Onde o TLMod já protege isso?
4. Travamento: como detectar um mod que prende a thread do jogo, com um sinal de vida por frame.
5. Integridade e origem: um pacote de mod com manifest, SHA-256 de cada arquivo e assinatura opcional,
   verificado antes da cópia para a pasta privada, e os limites disso.
6. Experimentos no aparelho, com um mod de teste que falha de propósito.

- [Como ler](#como-ler)
- [Resumo](#resumo)
- [1. O que o TLMod faz hoje quando algo falha](#1-o-que-o-tlmod-faz-hoje-quando-algo-falha)
- [2. Modo seguro (pergunta 1)](#2-modo-seguro-pergunta-1)
- [3. Sinais do sistema (pergunta 2)](#3-sinais-do-sistema-pergunta-2)
- [4. Exceções na fronteira da API (pergunta 3)](#4-exceções-na-fronteira-da-api-pergunta-3)
- [5. Travamento (pergunta 4)](#5-travamento-pergunta-4)
- [6. Integridade e origem (pergunta 5)](#6-integridade-e-origem-pergunta-5)
- [7. Experimentos no aparelho (pergunta 6)](#7-experimentos-no-aparelho-pergunta-6)
- [8. Riscos do desenho proposto](#8-riscos-do-desenho-proposto)
- [9. Respostas diretas](#9-respostas-diretas)
- [10. Lacunas do TLMod, em ordem de risco](#10-lacunas-do-tlmod-em-ordem-de-risco)
- [11. Ações recomendadas e ordem de implementação](#11-ações-recomendadas-e-ordem-de-implementação)

---

## Como ler

**Níveis de confiança**, os mesmos das auditorias anteriores:

- **confirmado no binário**: visto na desmontagem ou na metadata;
- **inferido**: decorre do que foi lido, mas um passo não foi rastreado. Aqui isso inclui o comportamento do
  Android, da ART e da bionic, que não estão no material;
- **só o aparelho confirma**: depende do que acontece em tempo de execução. Cada caso tem um experimento na
  [seção 7](#7-experimentos-no-aparelho-pergunta-6).

Afirmações sobre código-fonte trazem arquivo e linha e valem como confirmadas no código.

**Threads** que importam aqui:

| sigla | thread | o que roda |
|---|---|---|
| **P** | principal do Unity | `Main.DoUpdate`, os hooks do jogo e os callbacks de frame, `on_ai`, `on_spawn` e `Hurt` dos mods |
| **UI** | de UI do Android (o `Looper` principal) | o painel (`Tick` a cada 100 ms), provedores e comandos dos mods (auditoria 03) |
| **L** | de carga do runtime (`RuntimeMain`, `android_main.cpp:190`) | lê a configuração, copia as bibliotecas e chama o `tlmod_mod_init` de cada mod |
| **B** | de init da ponte (`TLMod_StartInitThread`, `entry.cpp`) | instala os hooks |

**Vocabulário do desenho:**

- **diário:** uma página de 4 KiB, mapeada em memória, onde o TLMod marca a fase da abertura e qual mod está
  sendo chamado;
- **marcador:** a parte do diário de uma thread: qual mod e qual chamada estão em andamento;
- **suspender:** desligar um mod automaticamente, com motivo;
- **vigia:** a thread nativa que confere o sinal de vida.

**Esforço:** dias de trabalho de uma pessoa que conhece a ponte e o runtime, com build e teste no aparelho.

---

## Resumo

1. **Hoje um mod que derruba o jogo no init cria um laço sem saída.** O runtime carrega o mod de novo a
   cada abertura, poucos segundos depois de o jogo começar. Nada registra o que estava rodando quando o
   processo morreu. A saída é apagar a pasta do mod, que fica em `Android/data`; no Android 11+, o
   gerenciador de arquivos do sistema não abre essa pasta (inferido).
2. **Modo seguro proposto.** Um diário de uma página, mapeado com `mmap` no armazenamento interno, guarda a
   fase da abertura e, por thread, o mod em chamada. Marcar custa duas escritas na memória, sem syscall. Se o
   processo morre, o kernel mantém a página. Antes do `LoadAll`, o runtime lê o diário da abertura anterior
   e suspende o mod culpado. A Oficina mostra o motivo.
3. **Sinais.** Confirmado no binário:
   - a `libil2cpp.so` não trata SIGSEGV;
   - o `NullReferenceException` vem de 44.549 testes explícitos no código gerado;
   - o coletor de lixo usa SIGPWR e SIGXCPU para parar e retomar threads;
   - um trecho do `System.Console` instala SIGCONT, SIGINT e SIGWINCH;
   - SIGSEGV e SIGBUS só são tratados com a variável de depuração `GC_LOOP_ON_ABORT`;
   - a `libunity.so` trata SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGPIPE e SIGSTKFLT. Grava um
     relatório da queda e encadeia para o tratador anterior. A opção de boot
     `androidChainedSignalHandlerBehavior` muda isso: com `disabled`, nada é instalado; com `legacy`, o
     tratador pode pular para fora do sinal com `longjmp` ou parar a thread para sempre.

   O TLMod já tem um tratador de SIGSEGV e SIGBUS: o `bytesig` do ShadowHook. Ele é instalado no primeiro
   hook, fica na frente da cadeia e encadeia para o anterior.
4. **Encadear outro tratador de SIGSEGV** não quebra o IL2CPP nem o Unity, desde que chame o anterior. Pela
   `libsigchain` da ART, o anterior é o tratador do Unity, que grava o relatório e chama o da bionic
   (inferido). Mas o modo seguro não precisa disso: o diário diz qual mod estava ativo, e o Android 11+ diz
   por que o processo morreu (`ApplicationExitInfo`), com o tombstone a partir do 12 (inferido). Se um dia
   for preciso, instalar pelo `sigaction` normal (atrás da ART), só gravar no diário e sempre encadear.
5. **Exceções dos mods.** Cada biblioteca do processo traz a própria cópia do libc++abi e do desenrolador:
   a `libil2cpp.so`, a ponte, o runtime e cada mod, que usam `c++_static`. O adaptador da ponte
   (`bridge_adapter.cpp`, compilado no runtime) captura exceções em 8 pontos: três de item, dois de
   textura, nome de NPC, frame e saves. Não captura no `init` do mod, nos hooks de NPC (inclusive `on_ai`),
   no `Hurt`, nem em provedores e comandos. Uma exceção C++ de mod nesses caminhos derruba o
   processo (inferido no mecanismo; a falta de captura está confirmada). O SDK (`tlmod.hpp`) não captura em
   nenhum trampolim.
6. **Exceções do jogo.** O que o TLMod chama por `il2cpp_runtime_invoke` já devolve a exceção. Nos hooks
   que chamam o original direto, a exceção atravessa o frame do TLMod, mas o estado não volta: o contador de
   recursão do `Hurt` e as trocas de textura ficam presos. Os hooks de save já fazem o certo: chamam o
   original por `runtime_invoke` e relançam com `il2cpp_raise_exception`.
7. **Travamento.** O hook de `Main.DoUpdate` vira o sinal de vida, com um contador por frame. Um vigia
   nativo, sem IL2CPP e sem travas, confere a cada 500 ms. Com o jogo em primeiro plano, contador parado há
   10 s e a thread do jogo dentro de um mod, o mod é o suspeito. O vigia grava no diário, o que vale mesmo
   que o Android mate o app, e pede ao painel uma caixa "Esperar / Fechar e desligar o mod".
8. **Pacote.** Um `.tlmod` é um zip com:
   - `manifest.json`, que passa a listar cada arquivo com SHA-256 e tamanho;
   - `signature.json` opcional (Ed25519 sobre os bytes do manifest).

   O pacote é verificado inteiro numa pasta de preparo interna e só depois vai para `files/tlmod/mods/`. A
   confiança é por id de mod, fixada na primeira instalação. O TLMod já tem SHA-256 e cópia atômica da
   `.so` (`library_cache.cpp`), mas sem hash esperado para comparar.

   Limite: um mod nativo pode tudo que o jogo pode. A assinatura prova quem assinou, não que o código é
   seguro. Só scripts (Módulo 8) podem ser isolados.
9. **Carregador.** O modo seguro só começa quando a ponte roda. Antes disso, quem instala o TLMod põe um
   `System.loadLibrary` no APK, e uma falha ali fecha o jogo a cada abertura. Há uma falha certa: as `.so`
   do pacote de teste exigem `__write_chk` e `__fwrite_chk`, da versão `LIBC_N` da libc (Android 7), e o
   manifest aceita Android 6 (`minSdkVersion` 23). Confirmado no binário. Proposta: um carregador de
   referência, com `try/catch`, teste de versão e uma chave para abrir sem o TLMod (2.5).

**Ordem de implementação** (detalhe na [seção 11](#11-ações-recomendadas-e-ordem-de-implementação)):

| passo | o quê | esforço |
|---|---|---|
| 1 | SDK e runtime capturam exceção em toda chamada a mod | 1 dia |
| 2 | diário, suspensão automática e motivo na Oficina | 2–3 dias |
| 3 | modo seguro do carregador, da ponte e geral | 1–1,5 dia |
| 4 | vigia de travamento, com ciclo de vida da Activity | 1–1,5 dia |
| 5 | estado dos hooks quando o jogo lança (`Hurt`, trocas de textura) | 0,5–1 dia |
| 6 | motivo da morte do processo pelo Android (API 30+) | 0,5–1 dia |
| 7 | pacote `.tlmod` verificado e instalação pela Oficina | 3–5 dias |
| 8 | assinatura Ed25519 e confiança por mod | 1–2 dias |
| 9 | opcional: tratador mínimo de SIGSEGV | 1 dia, mais testes |

Juntos, de 10 a 16 dias sem o passo 9, que é opcional; com ele, de 11 a 17, mais os testes.

---

## 1. O que o TLMod faz hoje quando algo falha

**A abertura, em ordem:**

1. **Carregador, fora do TLMod.** O pacote de teste não traz o carregador (`entrega/README_TESTE.md:115–117`):
   quem instala põe um `System.loadLibrary` no APK, um loader ou um `wrap.sh`. A `Activity` de entrada é a
   `com.unity3d.player.UnityPlayerActivity`, e o app tem um processo só (o manifest não declara
   `android:process`).
2. **Ponte (B).** O construtor da `libtlmod_bridge.so` dispara a thread de init (`entry.cpp:10–17`), que
   instala os hooks. A primeira instalação chama `shadowhook_init(SHADOWHOOK_MODE_SHARED, true)`
   (`hook_shadowhook.cpp:134`), e cada hook usa `SHADOWHOOK_HOOK_WITH_MULTI_MODE` (`:181`).
3. **Runtime (L).** O construtor da `libtlmod_runtime.so` cria a thread `RuntimeMain`
   (`android_main.cpp:264–267`), que:
   - espera a ponte (até 120 s) e se anexa ao IL2CPP (`:200–204`);
   - lê `tlmod_config.json` e a lista `disabled_mods` (`:211–214`);
   - procura os mods em `Android/data/<pacote>/files/tlmod/mods`, ou no armazenamento interno (`:169–176`);
   - chama o `LoadAll` (`:223`);
   - só depois instala o painel, as duas Oficinas e o botão "Mods" (`:231–258`).
4. **`LoadAll`, por mod** (`mod_loader.cpp`):
   - pula os desligados;
   - copia a `.so` para `code_cache/tlmod/<mod>/`, conferindo o SHA-256 (`library_cache.cpp`);
   - faz `dlopen(RTLD_NOW | RTLD_LOCAL)` (`dynlib.cpp`), que roda os construtores estáticos do mod;
   - chama o `tlmod_mod_init` sem `try/catch` (`:1209`).
5. **Jogo (P).** A cada frame, o hook de `Main.DoUpdate` (`world_state.cpp:103`) chama o frame do runtime,
   que chama o de cada mod. Os hooks de NPC chamam `on_ai` e `on_spawn`, e o de `Player.Hurt` chama os
   ouvintes de dano. Esses dois o construtor do `ModLoader` liga antes do `LoadAll`
   (`mod_loader.cpp:869–877`); o resto, só no fim dele.
6. **UI.** O painel faz um `Tick` a cada 100 ms (`TLModOverlay.java:66`, `:165–175`). Provedores e
   comandos dos mods rodam nessa thread.

**O que acontece em cada falha:**

| falha | onde | hoje |
|---|---|---|
| a `.so` do TLMod não carrega: falta um arquivo, ou o aparelho tem Android 6 | carregador | `UnsatisfiedLinkError` no Java; se o carregador não a captura, o jogo fecha a cada abertura, e nada do TLMod roda para ajudar |
| manifest inválido, dependência, versão de API | descoberta | `Rejected`, com motivo na Oficina |
| `dlopen` falha | carga | `Failed`, com o texto do `dlerror()` |
| `init` devolve diferente de 0 | carga | `Failed`, registros do mod desfeitos |
| sinal (SIGSEGV etc.) num construtor estático ou no `init` | carga (L) | o processo morre; na próxima abertura, o mod carrega de novo |
| exceção C++ no `init` | carga (L) | sai de `RuntimeMain` sem captura: `std::terminate` e o processo morre (inferido) |
| sinal num callback | jogo (P ou UI) | o Unity grava o relatório dele e repassa à bionic; o processo morre, e nada registra qual mod estava ativo. No modo `legacy` do Unity, a thread pode ficar parada em vez de morrer (3.3) |
| laço infinito num callback | jogo (P) | o jogo congela; nada detecta |
| laço infinito no `init` | carga (L) | L fica presa; o jogo segue sem o que o `LoadAll` liga no fim (itens, receitas, desenho, frame e saves, `mod_loader.cpp:1240–1272`) e sem painel nem Oficina; pela interface, não há como desligar o mod |
| o usuário desliga um mod | Oficina | entra em `disabled_mods` (`config_store.cpp:76–82`) e vale na próxima abertura |

**Três pontos para o desenho:**

- Um mod desligado aparece como "Desativado", sem motivo. As duas Oficinas só mostram o erro dos estados de
  falha: a desenhada pelo jogo em `BuildWorkshopEntries` (`panel.cpp:319`), a do painel ao montar as linhas
  (`panel.cpp:556`).
- O `tlmod.log` sobrevive a uma queda: cada linha leva `fflush` (`modapi/src/log.cpp:71–73`), e cada
  abertura começa com "sessão iniciada … (pid N)" (`:145`). Mas não registra qual mod estava em chamada.
- A configuração é gravada num arquivo temporário e renomeada (`config_store.cpp:117–153`), sem `fsync`.
  Isso basta para queda do processo; falta de energia pode perder a última mudança (inferido).

---

## 2. Modo seguro (pergunta 1)

### 2.1 Por que um diário, e não um arquivo por etapa

O arquivo-sentinela do enunciado (criado antes da etapa, apagado depois) serve para as etapas grossas: a
ponte, a carga de cada mod. Não serve para os callbacks: são centenas por frame, e criar e apagar arquivos
custa syscalls e escrita no sistema de arquivos. Pior: um sentinela que ficou para trás não distingue
"caiu" de "o Android matou o app em segundo plano", o que acontece sem aviso quando falta memória.

**O desenho:**

- Um arquivo de 4 KiB, `files/tlmod/diario.bin` no armazenamento interno, mapeado com
  `mmap(MAP_SHARED)` no começo da abertura.
- Escrever é um store na memória. Se o processo morre, por sinal, `abort` ou `kill`, a página continua no
  cache do kernel e vai para o disco depois. Só falta de energia ou pane do kernel perdem a escrita; um
  `msync` nas mudanças de fase cobre isso (inferido: semântica do Linux).
- Armazenamento interno, não o externo: no Android 11+ o externo é FUSE, e `mmap` compartilhado sobre ele
  não é confiável (inferido).
- O mesmo diário faz o papel dos sentinelas de etapa: o campo de fase troca de valor, em vez de um arquivo
  aparecer e sumir.

### 2.2 O que o diário guarda

| campo | quem escreve | quando |
|---|---|---|
| assinatura, versão, `pid`, hora da abertura | L | no início |
| fase: 1 ponte, 2 carga, 3 jogo | B, L, P | a cada mudança, com `msync` |
| em primeiro plano | UI | `onResume` e `onPause` da Activity (seção 5.2) |
| contador de frames | P | a cada `Main.DoUpdate` |
| marcador por thread (P, UI, L e uma reserva): `seq`, índice do mod, tipo de chamada | quem chama o mod | antes e depois de cada chamada |
| travamento: mod, tipo de chamada, hora | o vigia | ao detectar (seção 5) |
| mods carregados: id e SHA-256 da biblioteca, por índice | L | depois da carga de cada mod |

**O marcador:**

- Na entrada, grava o índice do mod e o tipo de chamada, e depois soma 1 a `seq` com ordem *release*. Na
  saída, soma 1 de novo. `seq` ímpar = dentro de uma chamada.
- Não lê o relógio a cada chamada: o vigia compara `seq` entre duas amostras.
- Tipos de chamada:
  - `dlopen`, que roda os construtores estáticos;
  - `init`;
  - frame, `on_spawn`, `on_ai` e `Hurt`;
  - provedor e comando.
- Custo: duas escritas na memória por chamada. No pior caso do `on_ai`, 200 NPCs de mod a 60 ticks, são
  12 mil chamadas por segundo, coisa de microssegundos por frame (inferido; o experimento E3 da auditoria 03
  mede).

### 2.3 Quem é o culpado: as regras na abertura seguinte

O runtime lê o diário anterior antes de criar o novo:

| fase anterior | em primeiro plano | marcador ativo | travamento registrado | decisão |
|---|---|---|---|---|
| qualquer | não | qualquer | não | nada: o Android encerrou o app em segundo plano |
| 1 (ponte) | sim | — | — | conta uma queda da ponte; com duas seguidas, a ponte abre sem hooks ([2.5](#25-modo-seguro-do-próprio-tlmod)) |
| 2 (carga) | sim | mod X em `dlopen` ou `init` | — | suspende X: "o jogo fechou enquanto este mod carregava" |
| qualquer | qualquer | — | mod X | suspende X: "travou o jogo" ou "não terminou de carregar" |
| 3 (jogo) | sim | mod X em P ou UI | não | conta uma queda para X; suspende na segunda em 24 h, ou já na primeira se o Android confirmar a biblioteca de X ([2.6](#26-o-motivo-da-morte-pelo-android)) |
| 3 (jogo) | sim | nenhum | não | conta uma queda sem mod; com duas seguidas nos primeiros 60 s, modo seguro geral ([2.5](#25-modo-seguro-do-próprio-tlmod)) |

**Por que uma queda só basta na carga e não no jogo.** Na carga, a thread L só roda código do mod. No jogo,
o diário não diz qual thread caiu: um mod no meio de um callback em P não prova que foi ele, se a queda foi
em outra thread. Por isso o jogo pede duas quedas ou a confirmação do Android.

**Versão.** A suspensão guarda o SHA-256 da biblioteca. Uma versão nova do mod libera a suspensão sozinha:
a correção do autor ganha uma chance.

### 2.4 Na abertura seguinte: suspender e mostrar

**Onde.** Em `RuntimeMain`, entre `config.Load` e `loader.SetDisabled` (`android_main.cpp:211–214`):

1. ler o diário anterior;
2. decidir pela tabela da 2.3;
3. gravar as suspensões na configuração;
4. criar o diário novo.

**Configuração.** Um campo novo em `tlmod_config.json`:

```json
"suspended_mods": {
  "tlmod.falhas": {"motivo": "init", "quando": 1760784120000, "versao": "1.2.0",
                   "sha256": "9f2c…", "quedas": 1}
}
```

- `ConfigStore::Disabled()` devolve a união de `disabled_mods` com `suspended_mods`.
- `SetEnabled(id, true)` também apaga a suspensão.
- Gravar com `fsync` antes do `rename`, que hoje falta (seção 1).

**Carga.** O `LoadAll` já transforma um mod desligado em `Disabled` (`mod_loader.cpp:1120–1125`). Para um
suspenso, `rec.error` recebe o motivo em vez de "desativado nas configurações".

**Oficina.**

- Mostrar o motivo também para mods desligados: hoje as duas Oficinas só mostram o erro dos estados de
  falha (`panel.cpp:319` e `:556`).
- Estado "Suspenso", e nos detalhes, por exemplo: *"Desligado pelo modo seguro em 18/10 às 10:42: o jogo
  fechou enquanto este mod carregava (versão 1.2.0). Toque em Ativar para tentar de novo."*
- As duas recebem a `ConfigStore`; uma consulta nova, `Suspension(id)`, devolve o motivo e a data. A
  Oficina desenhada pelo jogo monta as linhas em `BuildWorkshopEntries` (`panel.cpp:272`), e a do painel em
  outro laço (`panel.cpp:548–570`): a mudança vai nos dois lugares.

**Aviso.** Uma linha no `tlmod.log` e um aviso único no painel: "O modo seguro desligou X. Veja a Oficina."

**Dependências.** Quem depende de X fica `Skipped` ("dependência X está desativada"), pela lógica que já
existe.

### 2.5 Modo seguro do próprio TLMod

- **O carregador.** É o único passo que roda antes do diário, então a proteção dele fica no próprio
  carregador (abaixo).
- **A ponte.** Se ela cair ao instalar os hooks (fase 1), o runtime nunca chega à decisão da 2.4. Então a
  própria thread B lê o diário, antes do primeiro hook. Com duas mortes seguidas na fase 1 em primeiro
  plano, ela abre sem instalar hooks. O runtime avisa "TLMod desligado nesta abertura" e oferece "Tentar de
  novo".
- **Modo seguro geral.** Duas mortes seguidas sem mod ativo, nos primeiros 60 s depois da carga: a abertura
  seguinte suspende todos os mods, com um aviso. O usuário religa um por um.
- **Chave manual.** Se existir `tlmod/modo_seguro` na pasta do TLMod (criado pelo computador), abrir com
  todos os mods desligados.

**Carregador de referência**, para o README de instalação, no começo de `UnityPlayerActivity.onCreate`:

```java
try {
    File desligar = new File(getExternalFilesDir(null), "tlmod/desligar");
    if (Build.VERSION.SDK_INT >= 24 && !desligar.exists()) System.loadLibrary("tlmod_runtime");
} catch (Throwable t) {
    Toast.makeText(this, "TLMod não carregou: " + t, Toast.LENGTH_LONG).show();
}
```

- O `try/catch (Throwable)` transforma uma `.so` que falta ou não liga num aviso, e o jogo abre sem o
  TLMod.
- O teste de versão evita a falha certa no Android 6: as `.so` do pacote de teste importam
  `__write_chk@LIBC_N` (ponte) e `__fwrite_chk@LIBC_N` (runtime), e `LIBC_N` é a libc do Android 7.
  Confirmado no binário. A alternativa é compilar para a API 23, a do jogo; não foi verificado se algo mais
  exige a 24.
- A chave `tlmod/desligar`, criada pelo computador, cobre o que o `try/catch` não pega: uma queda nativa
  durante a carga das `.so`.

### 2.6 O motivo da morte pelo Android

No Android 11+ (API 30), o painel pode perguntar ao sistema por que o processo anterior morreu
(`ActivityManager.getHistoricalProcessExitReasons`):

- `REASON_CRASH_NATIVE`, `REASON_CRASH` e `REASON_ANR` confirmam uma queda;
- `REASON_LOW_MEMORY`, `REASON_USER_REQUESTED` e `REASON_SIGNALED` indicam que o processo foi encerrado de
  fora.

A partir do Android 12 (API 31), uma queda nativa traz o tombstone (`getTraceInputStream`). Se algum frame
estiver em `code_cache/tlmod/<mod>/`, aquele mod é o culpado já na primeira queda.

**O relatório do próprio Unity.** No modo padrão, o Unity grava um relatório da queda antes de repassar o
sinal (3.3), em qualquer versão do Android, e o caminho sai no log (E10). Se ele trouxer os endereços da
pilha, o runtime pode procurar ali uma biblioteca de mod também no Android 7 a 10. O formato não foi lido
nesta auditoria: só o aparelho confirma.

Isso fortalece a tabela da 2.3; não substitui o diário, porque aparelhos com Android 7 a 10 não têm a API.
O TLMod compila para a API 24 (`-DANDROID_PLATFORM=android-24` em `modapi/README.md:614` e `--min-api 24`
em `modapi/CMakeLists.txt:85`); o jogo aceita a 23, mas o TLMod não carrega nela (2.5). O app tem um
processo só, então a lista do Android cobre o jogo inteiro. Inferido: comportamento da plataforma; o E11
confirma.

**Esforço:** 2–3 dias (diário, regras, suspensão, Oficina) + 1–1,5 dia (2.5) + 0,5–1 dia (2.6).

---

## 3. Sinais do sistema (pergunta 2)

### 3.1 O que a `libil2cpp.so` instala

**Funções importadas** que tratam de sinal: `sigaction`, `signal`, `sigsuspend`, `pthread_kill`,
`pthread_sigmask`, `sigemptyset`, `sigfillset`, `sigaddset`, `sigdelset`, `setjmp`, `abort`. Também importa
`dl_iterate_phdr`, que o desenrolador usa. Confirmado no binário (`llvm-readelf -r`).

| sinal | quem instala | onde | quando | para quê |
|---|---|---|---|---|
| SIGPWR (30) | coletor Boehm, "parar o mundo" | `0x6A9C54`, tratador `0x6A96B0`, `SA_RESTART \| SA_SIGINFO` | na inicialização do coletor | suspender cada thread registrada (`pthread_kill` em `0x6A98CC`) |
| SIGXCPU (24) | idem | `0x6A9C80`, tratador `0x6A9828`, `SA_RESTART` | idem | retomá-las (`pthread_kill` em `0x6A9B30`; a thread espera em `sigsuspend`, `0x6A97D8`) |
| SIGCONT (18), SIGINT (2), SIGWINCH (28) | configuração de terminal do `System.Console`: a função lê `COLUMNS` e `LINES` | `0x64AE64`, `0x64AE90`, `0x64AEBC` | só se o jogo preparar o console (inferido: não é o caso no Android) | terminal |
| SIGSEGV (11), SIGBUS (7) | coletor Boehm, depuração | `signal()` em `0x6A7FA8` e `0x6A7FBC`, tratador `0x6AA37C` | só se a variável de ambiente `GC_LOOP_ON_ABORT` existir | deixar o processo num laço para um depurador |

Todos menos o último passam por um invólucro de `sigaction` (`0x69C94C`). Para sinais abaixo de 64, ele
registra no kernel um trampolim único (`0x69CA00`) e guarda o tratador de verdade numa tabela por sinal
(`0x2BFFCB0`).

**O coletor:**

```
0x6a9bd4: mov  w8, #0x1e                 ; sinal de suspensão = SIGPWR (se não configurado)
0x6a9bec: mov  w9, #0x18                 ; sinal de retomada = SIGXCPU
0x6a9c20: movk w8, #0x1000, lsl #16      ; sa_flags = SA_RESTART | SA_SIGINFO
0x6a9c2c: bl   sigfillset                ; bloqueia tudo durante o tratador...
0x6a9c38: bl   0x6a95ec                  ; ...menos SIGINT, SIGQUIT, SIGABRT e SIGTERM
0x6a9c44: add  x8, x8, #0x6b0            ; tratador de suspensão 0x6a96b0
0x6a9c54: bl   0x69c94c                  ; sigaction(SIGPWR, ...)
0x6a9c68: add  x9, x9, #0x828            ; tratador de retomada 0x6a9828
0x6a9c80: bl   0x69c94c                  ; sigaction(SIGXCPU, ...)
```

**O SIGSEGV de depuração:**

```
0x6a7f8c: add  x0, x0, #0xf2e            ; "GC_LOOP_ON_ABORT"
0x6a7f90: bl   getenv
0x6a7f94: cbz  x0, 0x6a7fd0              ; sem a variável: não instala nada
0x6a7fa0: mov  w0, #0xb                  ; SIGSEGV
0x6a7fa8: bl   signal
0x6a7fb4: mov  w0, #7                    ; SIGBUS
0x6a7fbc: bl   signal
```

Confirmado no binário.

### 3.2 `NullReferenceException` sem sinal

O código gerado testa o ponteiro e chama uma função que lança a exceção. Exemplo em
`NPC.GetBossHeadTextureIndex` (auditoria 04):

```
0xedc384: ldr  x9, [x8, #0x1b8]          ; NPCID.Sets.BossHeadTextures
0xedc388: cbz  x9, 0xedc504              ; nulo?
0xedc504: bl   0x642c38                  ; lança NullReferenceException
```

- `0x642C38` → `0x68480C`, que monta `System.NullReferenceException` (cadeias `"System"` em `0x24FB51E` e
  `"NullReferenceException"` em `0x24F5500`) e lança.
- No código gerado há 44.549 `bl 0x642C38` e 12.353 `bl 0x642C40` (`IndexOutOfRangeException`).

Confirmado no binário. Transformar um acesso nulo em exceção por sinal é técnica do JIT do Mono, não desta
build (inferido quanto ao Mono).

### 3.3 O que a `libunity.so` instala

**Funções importadas** que tratam de sinal: `sigaction`, `sigaltstack`, `sigemptyset`, `raise`, `setjmp` e
`longjmp`. Não importa `signal`, `sigprocmask` nem `pthread_sigmask`.

**O instalador** (`0x2C7DBC`) recebe o tratador `0x2B35CC` de `0x2B3504`, a função que cria o estado de
relatório de queda de uma thread. Ela tem dezenas de chamadores, então a instalação deve acontecer cedo
(inferido). Um contador (`0xB5AF50`) faz o registro no kernel acontecer uma vez só. O instalador:

1. Lê a opção `androidChainedSignalHandlerBehavior` (cadeia em `0x8980E3`) da configuração de boot do
   Unity. Os modos são três:
   - sem a opção, modo 0, o padrão;
   - `legacy`, modo 1;
   - `disabled`, modo 2: nada é instalado.
2. Percorre 7 sinais da tabela em `0x898FAC`: SIGILL (4), SIGABRT (6), SIGBUS (7), SIGFPE (8), SIGSEGV (11),
   SIGPIPE (13) e SIGSTKFLT (16). Para cada um:
   - lê a ação atual e, se for `SIG_IGN`, deixa como está;
   - senão, guarda a ação anterior numa tabela (`0xB5AEE0 + 32 × sinal`);
   - instala a sua com `SA_SIGINFO | SA_ONSTACK | SA_RESTART` e máscara vazia.

```
0x2c7f64: mov  w8, #4
0x2c7f70: movk w8, #0x1800, lsl #16      ; sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART
0x2c7f7c: ldr  w19, [x23, x22]           ; próximo sinal da tabela 0x898fac
0x2c7f8c: bl   sigaction                 ; sigaction(sinal, NULL, &anterior)
0x2c7f94: cmp  x8, #1                    ; anterior == SIG_IGN?
0x2c7f98: b.eq 0x2c7fbc                  ;   então pula o sinal
0x2c7fb4: stur q1, [x8, #-0x68]          ; guarda a anterior na tabela
0x2c7fb8: bl   sigaction                 ; sigaction(sinal, &nova, NULL)
0x2c7fc0: cmp  x22, #0x1c                ; 7 sinais de 4 bytes
```

**O tratador** (`0x2B35CC`):

1. Se a thread já está num sinal (campo `0x108` do estado da thread), só encaminha.
2. Chama um gancho global, se houver (`0xB58CC8`).
3. Aloca memória (`calloc` e um `malloc` de `0x19001` bytes) e grava um relatório da queda com `open` e
   `close`, num arquivo `…/tombstone_NN`. O log ganha "Tombstone written to: …", com a etiqueta `CRASH`.
4. Lê o modo (`0x2C7D80`). No modo 0, encaminha (`0x2C80E8`): chama o `sa_sigaction` ou o `sa_handler`
   guardado, sempre com (sinal, info, contexto), e registra "Forwarding signal %i". Se o anterior era
   `SIG_DFL`, só registra "No handler for signal %i" e retorna.
5. No modo 1 (`legacy`):
   - se a thread marcou um ponto de retorno (o primeiro byte do estado, com um `jmp_buf` logo depois), faz
     `longjmp` para lá, para fora do tratador (`0x2B39F4`);
   - senão, fora da thread que carregou a `libunity.so` (gravada por um construtor estático, `0x86D88`), a
     thread dorme para sempre, em pedaços de 10.000 s (`0x2B39B4`);
   - na thread que carregou a biblioteca, encaminha.

**Outros detalhes:**

- O encaminhamento trava um mutex (`0xB5B100`). Com o `malloc` e o `open` do passo 3, o tratador não é
  seguro dentro de um sinal: uma queda dentro do `malloc` pode virar travamento (inferido).
- Antes da tabela, o encaminhador olha um ponteiro de "fallback" (`0xB5AF58`). Nesta build, nada o grava além
  dos dois `memset` que zeram a estrutura (`0x890A0`, `0x2C807C`).
- Quando a última thread libera seu estado, o contador zera e o Unity devolve as ações que tinha guardado
  (`0x2B39F8` → `0x2C7FFC`).
- O Unity também cria uma pilha alternativa de sinal de 256 KiB para cada estado de thread (`mmap` de
  `0x40000` e `sigaltstack`, `0x2B3FD8`–`0x2B4004`).

Confirmado no binário. O modo que o Terraria usa está no `boot.config` do APK, que não está no material; o
[E10](#e10-a-cadeia-de-sinais) diz como conferir.

### 3.4 A cadeia de tratadores num app Android

| camada | sinais | como entra | fonte |
|---|---|---|---|
| bionic (o linker) | SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV, SIGSTKFLT, SIGSYS, SIGTRAP | instalado no início do processo; gera o tombstone e o motivo de saída | inferido |
| ART (`libsigchain`) | intercepta `sigaction`, `signal` e `sigprocmask` de todas as bibliotecas | a ART fica com SIGSEGV para os próprios testes (nulo implícito e estouro de pilha no código Java); o `sigaction(SIGSEGV)` de um app vira a "ação do usuário", chamada depois da ART | inferido |
| ShadowHook, dentro da ponte do TLMod | SIGSEGV, SIGBUS | `bytesig_init` no `shadowhook_init` (`shadowhook.c:84–85`), no primeiro hook (`hook_shadowhook.cpp:134`) | confirmado no código |
| Unity (`libunity.so`) | SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGPIPE, SIGSTKFLT | `sigaction` comum, que a `libsigchain` intercepta; grava o relatório e chama o anterior que guardou (3.3) | confirmado no binário; a passagem pela `libsigchain` é inferida |
| o resto do TLMod | nenhum | não há `sigaction` nem `signal` no código da ponte, do runtime ou do painel | confirmado no código |

O manifest não declara relator de quedas de terceiros, como o Firebase Crashlytics. As bibliotecas de
licença e de proteção ficaram fora do escopo: se elas instalarem tratadores, o E10 mostra.

**Como o `bytesig` se encaixa:**

- Pega `sigaction64` direto da libc (`dlopen("libc.so")` + `dlsym`, `bytesig.c`, no construtor). Assim passa
  por fora da `libsigchain` e vira o tratador do kernel, na frente de todos.
- Guarda o anterior em `prev_action`.
- Para uma falha fora das regiões que ele protege (`SH_SIG_TRY`), o `bytesig_handler` chama o anterior, na
  forma certa (`SA_SIGINFO` ou não; nada se for `SIG_DFL` ou `SIG_IGN`).
- Flags: `SA_SIGINFO | SA_ONSTACK | SA_RESTART | SA_EXPOSE_TAGBITS`, com todos os sinais bloqueados.

**Ordem esperada** para um SIGSEGV num mod, com o Unity no modo 0:

1. kernel;
2. `bytesig` (TLMod);
3. `libsigchain`;
4. tratadores especiais da ART;
5. ação do usuário: o tratador do Unity, que grava o relatório;
6. o anterior que o Unity guardou: o da bionic, que gera o tombstone.

Inferido; o E10 mostra a ordem real. A `libsigchain` guarda uma ação do usuário por sinal. Um `sigaction`
novo, do TLMod ou de outro, toma o lugar do Unity e recebe o dele como anterior: só não perde o relatório
quem o chama.

### 3.5 Encadear um tratador de SIGSEGV é seguro?

**Não quebra o IL2CPP**, porque ele não usa SIGSEGV (3.1, 3.2). Confirmado no binário.

**Não quebra o Unity**, se chamar o anterior: o Unity grava o relatório e encaminha, sem depender de ser o
primeiro (3.3). Confirmado no binário; a ordem real, no E10.

**Cuidados que o binário impõe:**

- Não mexer em SIGPWR e SIGXCPU, nem bloqueá-los numa thread registrada no coletor. A thread L se registra
  (`TLMod_AttachCurrentThread`, `android_main.cpp:204`). O coletor manda SIGPWR para cada thread registrada
  (`0x6A98CC`) e espera a confirmação; com o sinal bloqueado, a próxima coleta trava (inferido do código do
  coletor).
- O tratador de suspensão bloqueia quase tudo (3.1). Um tratador do TLMod nunca pode esperar algo que uma
  thread suspensa segura.

**Condições para um tratador seguro:**

1. Instalar pelo `sigaction` normal, que passa pela `libsigchain` e fica atrás dos testes da ART. Pela libc
   direto, como o `bytesig`, ele ficaria na frente da ART e precisaria repassar a ela até as falhas do
   código Java.
2. `SA_SIGINFO | SA_ONSTACK`. A bionic dá uma pilha alternativa de sinal a cada thread (inferido).
3. Só trabalho seguro dentro de um sinal: copiar para o diário o PC e o endereço da falha e qual mod contém
   o PC. Para achar o mod, usar uma tabela [início, fim) por mod, montada na carga com `dl_iterate_phdr`.
   Nada de `dladdr`, `malloc`, log ou trava.
4. Sempre encadear para o anterior, que será o do Unity (3.4): `sa_sigaction` se `SA_SIGINFO`, senão
   `sa_handler`. Se o anterior for `SIG_DFL`, restaurar o padrão e retornar, para a falha se repetir e o
   processo morrer.
5. Instalar tarde (depois do primeiro frame) e conferir de tempos em tempos se continua lá. Se alguém o
   trocar, perde-se só o diagnóstico.

**Riscos:**

- o Unity devolver as ações que guardou quando o contador dele zera (3.3). Se o TLMod instalou depois, o
  tratador do TLMod some; a conferência da condição 5 pega isso;
- o jogo usar o modo `legacy`: fora da thread que carregou a `libunity.so`, o Unity não encaminha e deixa a
  thread parada para sempre (3.3). O tratador do TLMod, que vem antes, registra a queda, mas o processo não
  morre;
- dois tratadores que encadeiam um para o outro (recursão infinita);
- mudança de comportamento entre versões do Android.

**Recomendação:** o modo seguro não precisa de tratador. O diário já diz qual mod estava ativo, e o Android
11+ diz se foi queda (2.6). Um tratador só ajudaria a apontar a biblioteca no Android 7 a 10, e o relatório
do Unity talvez já resolva isso (2.6). Se não resolver, fazer como acima, como último passo e opcional.

**Alternativa rejeitada:** "salvar" o jogo de um SIGSEGV de mod com `sigsetjmp`/`siglongjmp`, como o
`bytesig` faz nas leituras do ShadowHook. Pular para fora do meio de um callback deixa travas presas (do
`malloc`, do IL2CPP, do jogo), estado do jogo pela metade e destrutores C++ sem rodar. A queda vira
travamento ou corrupção mais tarde (inferido).

**Esforço** do tratador opcional: 1 dia, mais testes em várias versões do Android.

---

## 4. Exceções na fronteira da API (pergunta 3)

### 4.1 Uma cópia do desenrolador por biblioteca

- **`libil2cpp.so`.** Traz o próprio libunwind e o próprio libc++abi, e os exporta: `_Unwind_Find_FDE` em
  `0x70DB2C`, `_Unwind_ForcedUnwind` em `0x70C0B4`, `__cxxabiv1::__getExceptionClass` em `0x6F5C48`.
  Importa `dl_iterate_phdr` para achar as tabelas das outras bibliotecas. Confirmado no binário.
- **TLMod.** A `libtlmod_bridge.so`, a `libtlmod_runtime.so` e os mods têm `.eh_frame`, `.eh_frame_hdr` e
  `.gcc_except_table`, e nenhuma depende de `libc++_shared.so`: usam `c++_static`. Cada uma tem o próprio
  `__gxx_personality_v0` e o próprio `_Unwind_RaiseException`, como símbolos locais: nenhuma exporta nem
  importa essas funções, então cada biblioteca usa só a própria cópia. Confirmado nas bibliotecas
  compiladas.

**Consequência.** Uma exceção lançada numa biblioteca e desenrolada pelas outras mistura as cópias: o
desenrolador de quem lançou chama a rotina de personalidade da outra biblioteca, que usa a própria cópia
das funções `_Unwind_*` sobre um contexto criado pela primeira. Funciona quando as cópias são compatíveis
(mesma família do libunwind do LLVM), mas não é garantido. O NDK recomenda não deixar exceções cruzarem
bibliotecas que usam o runtime estático (inferido).

A ponte já sabe disso: o comentário em `save_hooks.cpp:205–214` explica por que os hooks de save não têm
objetos com destrutor no frame.

### 4.2 Exceções do jogo

| caminho | exemplos | hoje |
|---|---|---|
| `il2cpp_runtime_invoke` | `TLMod_SpawnNpc` (`npc_hooks.cpp:219–221`), soltar item (`tools.cpp:161–163`), a maior parte da API | a exceção volta em `exc` e vai para o log: seguro |
| hooks de save | `Player.InternalSavePlayerFile`, `WorldFile.InternalSaveWorld` | chamam o original por `runtime_invoke`, limpam o estado, avisam "depois" com `ok = false` e relançam com `il2cpp_raise_exception` (`save_hooks.cpp:215–235`, `:438–445`): o modelo certo |
| hooks que chamam o original direto | `NPC.AI`, `DrawNPCDirect`, `Player.Hurt`, desenho de itens | a exceção atravessa o frame do TLMod pelo CFI (só há dados planos no frame) e volta ao jogo; o que o hook faria depois do original não acontece |

**Estado que fica preso:**

- **`Hurt`.** `++t_hurtDepth`, original, `--t_hurtDepth` (`bridge.cpp:384–402`). Se o original ou um
  ouvinte de mod lançar, o contador fica em 1. Daí em diante, os golpes daquela thread não chegam mais aos
  mods, até fechar o jogo; o "Invencível" das Ferramentas para de funcionar.
- **Desenho.** `BeginItem`, original, `EndItem` (`draw_hooks.cpp:471–514`) e `BeginNpc`, original,
  `EndNpc` (`:523–525`): a textura do mod fica no lugar da casca.
- **Tooltip.** `t_modItemTooltip` (`screen_hooks.cpp:425–429`) segue o mesmo padrão, mas é atribuído, não
  somado: fica preso só até o próximo tooltip, que o refaz (o comentário em `:415–417` já prevê isso).

Confirmado no código. Que o jogo lance nesses métodos é raro (inferido).

### 4.3 Exceções dos mods

| ponto de entrada | caminho | captura hoje | se o mod lançar |
|---|---|---|---|
| construtores estáticos (`dlopen`) e `tlmod_mod_init` | L: `LoadAll` → mod (`mod_loader.cpp:1209`) | nenhuma | sai de `RuntimeMain`: `std::terminate`, o processo morre, e o mod carrega de novo na próxima abertura (inferido) |
| frame | P: `DoUpdate` → `OnFrame` → mods (`mod_loader.cpp:285–292`) | um `catch (...)` para tudo (`bridge_adapter.cpp:227–231`) | o jogo segue, mas os mods depois dele no laço não rodam naquele frame, em todo frame, e nada vai para o log |
| `on_spawn`, `on_ai` | P: hooks de NPC → runtime → mod (`bridge_adapter.cpp:41–56`; `mod_loader.cpp:395`, `:415`) | nenhuma | atravessa o runtime, a ponte e o código do jogo; nenhum `catch` do IL2CPP aceita o tipo: `std::terminate` (inferido) |
| `Hurt` | P: hook → runtime → mods (`bridge_adapter.cpp:21`; `mod_loader.cpp:130–136`) | nenhuma | idem, e o `t_hurtDepth` fica preso |
| provedores e comandos | UI: JNI → runtime → mod (`mod_loader.cpp:914`, `:943`) | nenhuma | atravessa a JNI até a ART: `std::terminate` (inferido) |
| o SDK (`tlmod.hpp:236–275` e a macro `TLMOD_MOD` em `:292–298`) | dentro do mod | nenhuma | — |

**O que já é protegido** (`bridge_adapter.cpp:149–205`, `:227–231`, `:376–403`): troca de ID de item,
`SetDefaults` e tooltip de item, texturas de item e de NPC, nome de NPC, frame e saves. Fora dos testes,
ponte, runtime, painel e cola JNI não têm nenhum outro `catch`. Desses oito pontos, só o do frame chama
código de mod; os outros chamam só o runtime.

**Resposta curta:** sim. Uma exceção C++ de mod no `init`, num callback de NPC, no `Hurt` ou num
provedor/comando derruba o processo hoje. Uma exceção do jogo dentro de um hook não derruba, mas pode deixar
estado preso.

### 4.4 Caminho de implementação

1. **SDK primeiro.** Cada trampolim e a `TLMOD_MOD` ganham `try { … } catch (...) { … }`, convertendo para o
   contrato em C:

   | entrada | se o mod lançar, devolve |
   |---|---|
   | `init` | código 2, "exceção" |
   | `on_ai` | `false`: a IA da casca roda |
   | provedor e comando | `false` |
   | frame e `on_spawn` | nada |

   Isso é compilado dentro do mod, então a captura usa a cópia do próprio mod e nenhuma exceção cruza
   bibliotecas. A mensagem (`what()`, se for `std::exception`) vai para `api->log`.
2. **Runtime, segunda linha.** `try/catch (...)` em volta de cada chamada a mod: `init`, frame por mod (não
   um para todos), `on_spawn`, `on_ai`, `Hurt`, provedor e comando. Com um contador por mod: depois de 10
   exceções numa sessão, os callbacks daquele mod saem até fechar o jogo, com uma linha no log e "Erro" na
   Oficina. Isso cobre mods feitos com um SDK antigo ou em C.
3. **Hooks.** Seguir o modelo dos saves onde o estado precisa voltar:
   - o `Hurt` chama o original por `runtime_invoke` e relança (golpes são raros; o custo não pesa);
   - as trocas de textura ganham uma varredura no começo de cada `DoUpdate`, que desfaz qualquer troca que
     ficou para trás. O desenho é caminho quente, então nada de `runtime_invoke` ali.

**Esforço:** 1 dia (1 e 2) + 0,5–1 dia (3).

---

## 5. Travamento (pergunta 4)

### 5.1 O sinal de vida

`TLMod_Hooked_MainDoUpdate` (`world_state.cpp:103`) roda uma vez por frame em P, antes dos callbacks de frame
dos mods. Ali entra um contador de frames no diário (um store *relaxed*). O sinal não depende do painel nem
de mod algum.

### 5.2 O vigia

**A thread:**

- nativa, criada pelo runtime antes do `LoadAll`, para cobrir também o `init`;
- não se anexa ao IL2CPP: o coletor não a para, e ela nunca toca objeto gerenciado;
- não usa trava do TLMod, só lê atômicos do diário;
- acorda a cada 500 ms.

**As regras:**

| o que parou | condição | conclusão | ação |
|---|---|---|---|
| frames (P) | em primeiro plano e contador parado há 10 s; marcador de P ímpar e com o mesmo `seq` desde o começo da parada | o mod do marcador prende a thread do jogo | grava o travamento no diário e pede a caixa ao painel |
| frames (P) | igual, mas marcador de P par (fora de mod) | o jogo ou o TLMod parou | uma linha no log; não culpa mod |
| carga (L) | marcador de L ímpar e parado há 30 s | o `init` de um mod não termina | grava no diário e no log; o jogo segue |
| UI | marcador da UI ímpar e parado há 5 s | um provedor ou comando não termina | grava no diário; o Android tende a mostrar "não responde" (inferido) |

**Na carga não há caixa:** o painel só sobe depois do `LoadAll` (`android_main.cpp:231–258`). O registro no
diário basta: a abertura seguinte suspende o mod.

**Queda que vira travamento.** O tratador do Unity pode transformar uma queda em travamento (3.3):

- ele usa `malloc` e trava dentro do sinal, e uma queda dentro do `malloc` pode prendê-lo (inferido);
- no modo `legacy`, uma queda fora da thread que carregou a `libunity.so` deixa a thread dormindo para
  sempre. Isso vale para P, se P não for essa thread (inferido).

Nos dois casos o processo não morre, e o marcador da thread fica ímpar. O vigia vê um travamento e culpa o
mod do marcador, que é o que se quer.

**A caixa:**

- O vigia não desenha nada. Ele liga uma flag atômica, que o `Tick` do painel (UI, a cada 100 ms,
  `TLModOverlay.java:165–175`) lê.
- O painel mostra: *"O mod X está travando o jogo há 10 s. [Esperar] [Fechar o jogo e desligar o mod]"*.
- **Fechar** suspende X na configuração, com o motivo, e encerra o processo (`Process.killProcess`).
  **Esperar** pergunta de novo em 20 s.

**Se a UI também estiver presa**, a caixa não aparece. Isso acontece, por exemplo, se o frame do mod segura
uma trava que o provedor dele precisa: o `tlmod.devtools` usa o mesmo `mutex_` no `OnFrame` e no `Handle`
(`devtools.cpp:128`, `:187`).
O diário já tem o registro: quando o usuário ou o Android fechar o app, a abertura seguinte suspende X.

**Ciclo de vida.** O painel não acompanha `onPause` e `onResume` hoje (`TLModOverlay.java` não registra
callbacks de ciclo de vida). Com o app em segundo plano o Unity para de chamar `DoUpdate`, e o vigia veria
uma parada a cada pausa. Solução: `Application.ActivityLifecycleCallbacks` grava "em primeiro plano" no
diário (API 14+). O manifest acrescenta três detalhes:

- o app tem duas `Activity`: a `UnityPlayerActivity`, de entrada, e uma de licença (fora do escopo), que
  pode ficar na frente do jogo. O vigia olha só a `UnityPlayerActivity`;
- a `UnityPlayerActivity` trata sozinha as mudanças de configuração (`configChanges` = `0x40003FFF`):
  girar a tela não a recria nem pausa;
- o painel sobe com a `Activity` já rodando, e os callbacks perdem o primeiro `onResume`. Ao registrá-los,
  já marcar "em primeiro plano".

**Limites de tempo:**

- 10 s para P. Depois do primeiro frame, nenhuma operação legítima prende a thread principal tanto tempo:
  carga e geração de mundo e saves rodam nas threads C, G e S (auditoria 03).
- O vigia só se arma depois do primeiro frame.

### 5.3 O que não fazer

- **Interromper a thread presa.** Não há jeito seguro de parar um laço nativo em outra thread. Sinal mais
  `siglongjmp` deixa travas e estado corrompidos (3.5).
- **`alarm` ou `SIGALRM` na thread do jogo.** Mesmo problema, e a thread pode estar registrada no coletor.
- **Conferir só no `Tick` do painel.** Se a UI travar junto, nada fica registrado.

**Esforço:** 1–1,5 dia (vigia, flag, caixa, ciclo de vida).

---

## 6. Integridade e origem (pergunta 5)

### 6.1 O que existe hoje

- **Onde os mods ficam.** Em `Android/data/<pacote>/files/tlmod/mods/<id>/` (`android_main.cpp:169–176`):
  manifest, texturas e biblioteca. O usuário copia pelo computador ou por um gerenciador de arquivos.
- **Só a biblioteca vai para a pasta privada.** `LibraryCache::Prepare` (`library_cache.cpp`):
  1. calcula o SHA-256 da origem;
  2. copia para um temporário com `fsync`;
  3. recalcula na cópia e compara;
  4. marca como somente leitura;
  5. renomeia atomicamente para `code_cache/tlmod/<id>/`.

  É uma boa base: o hash e a cópia atômica já existem. Falta um hash esperado para comparar, e quem
  assinou.
- **O resto é lido do armazenamento externo** a cada abertura: manifest e texturas (`DiscoverDirectory`,
  `LoadTextures` em `mod_loader.cpp`).
- **O manifest não lista arquivos nem hashes.** Os campos são tlmod_api, id, name, version, description,
  authors, dependencies, content, provides e entry (`manifest.cpp:124–226`).

### 6.2 O formato `.tlmod`

Um zip, com entradas armazenadas ou com deflate; o runtime já liga a zlib por causa dos PNGs.

**`manifest.json`, obrigatório.** O formato atual, mais um objeto `package`:

```json
"package": {
  "format": 1,
  "files": {
    "lib/android-arm64/libexemplo.so": {"sha256": "9f2c…", "size": 182344},
    "textures/ember_titan.png": {"sha256": "51ab…", "size": 2048}
  }
}
```

**`signature.json`, opcional:**

```json
{"format": 1, "alg": "ed25519", "public_key": "<base64, 32 bytes>", "signature": "<base64, 64 bytes>"}
```

- A assinatura cobre os bytes exatos do `manifest.json`, sem canonizar o JSON. Como o manifest lista o hash
  de todos os outros arquivos, assinar o manifest cobre o pacote inteiro.
- Impressão digital mostrada ao usuário: os 16 primeiros dígitos hexadecimais do SHA-256 da chave pública,
  em grupos (`A1B2-C3D4-E5F6-0718`).
- A verificação Ed25519 vem de uma biblioteca pequena e revisada compilada no runtime (por exemplo, a
  Monocypher), sem depender da versão do Android.

### 6.3 A instalação verificada

1. **Escolher.** Na Oficina, "Instalar mod" abre o seletor do sistema (`ACTION_OPEN_DOCUMENT`, sem
   permissão de armazenamento). O arquivo é copiado para `files/tlmod/entrada/`, no armazenamento interno.
   É o caminho que sobra: o manifest só pede `INTERNET` e a permissão de licença, e o `targetSdkVersion` é
   35, então o jogo não lê a pasta Download por conta própria (inferido).
2. **Ler o diretório do zip e recusar:**
   - caminhos absolutos ou com `..`, `\` ou NUL;
   - links simbólicos e entradas repetidas;
   - entradas cifradas;
   - mais de 512 entradas;
   - tamanho total descompactado acima de um teto (por exemplo, 128 MiB), ou compressão acima de 100:1
     (bomba de zip).
3. **Extrair** para `files/tlmod/preparo/<id>-<aleatório>/`, calculando o SHA-256 enquanto grava. Todo
   arquivo precisa estar em `package.files`, com o mesmo hash e o mesmo tamanho. Faltou ou sobrou: recusa.
4. **Validar o manifest** com o `LoadManifest` atual: a versão `tlmod_api` e se o `entry` da plataforma
   aponta para um arquivo da lista.
5. **Assinatura**, se houver, e a política de confiança (6.4).
6. **Resumo** para o usuário confirmar: id, versão, autores, conteúdo, estado da assinatura, e "código
   nativo: roda com as mesmas permissões do jogo".
7. **Instalar.** Renomeação atômica para `files/tlmod/mods/<id>/`, guardando a versão anterior para um passo
   de volta. Registro em `instalados.json`: id, versão, SHA-256 do manifest, impressão digital, data.
8. **A cada abertura**, `LibraryCache::Prepare` já calcula o SHA-256 da biblioteca: comparar com
   `package.files`. Se divergir, não carregar, e a Oficina mostra "arquivo alterado depois da instalação".

As pastas soltas no armazenamento externo continuam carregando só num modo de desenvolvimento, marcadas
"sem verificação" na Oficina.

### 6.4 Confiança

- **Na primeira instalação de um mod assinado,** a chave fica fixada para aquele id (`chaves.json`,
  armazenamento interno). É confiança no primeiro uso.
- **Uma atualização** precisa vir com a mesma chave. Com outra chave, a instalação para com "assinado por
  outra chave", e só segue com uma confirmação explícita. De assinado para sem assinatura: aviso.
- **Troca de chave pelo autor:** a chave nova vai assinada pela antiga (um campo `rotation`) e passa sem
  confirmação extra.
- **Sem assinatura:** permitido, marcado "Sem assinatura".
- **Revogação** precisaria de um servidor; fica de fora por ora.

### 6.5 Os limites, sem rodeio

- **Um mod nativo é código dentro do processo do jogo,** com o mesmo usuário Linux e as mesmas permissões.
  Ele lê e grava os saves, a configuração do TLMod, as chaves confiáveis e o diário. Usa a rede, se o jogo
  pode. Pode alterar o TLMod na memória e desligar as verificações dele. Nada que o TLMod faça depois de
  carregar o mod resiste a um mod mal-intencionado.
- **Não há como isolar:** o `seccomp` vale para o processo inteiro e não se desfaz (quebraria o jogo, o
  Unity e a ART); um processo separado não alcança a memória do jogo para os hooks; o SELinux não é
  configurável por um app (inferido).
- **A assinatura prova quem assinou** e que os arquivos não mudaram depois. Não prova que o código é seguro.
  Uma chave roubada assina um mod malicioso, e a confiança no primeiro uso o aceita.
- **O que o pacote protege de fato:**
  - download corrompido e alteração acidental;
  - troca dos arquivos por outro app, nos Androids antigos com acesso amplo ao armazenamento;
  - substituição de um mod confiável por um de outra pessoa.

### 6.6 Relação com o Módulo 8

- **Scripts** rodam num interpretador controlado pelo TLMod e só fazem o que a API oferece: sem memória
  crua, arquivos ou rede, salvo o que a API der. Ali o TLMod pode declarar e impor permissões ("lê o
  inventário", "cria itens") e mostrá-las na instalação. A assinatura passa a tratar de autoria e de
  atualizações, não de segurança.
- **Proposta:** duas classes na Oficina, "Script (isolado)" e "Nativo (acesso total)". A instalação de um
  nativo pede confirmação explícita. Preferir script sempre que a API cobrir o que o mod faz.

**Esforço:** 3–5 dias (formato, verificação, instalação pelo seletor, pasta interna) + 1–2 dias (Ed25519,
confiança, troca de chave).

---

## 7. Experimentos no aparelho (pergunta 6)

No APK enviado, `<pacote>` é `com.and.games505.TerrariaPaid`.

**O mod de teste.** `tlmod.falhas`, só para build de teste.

- No `init`, lê `modo.txt` da própria pasta.
- Para os modos de jogo, tem um painel com botões (um canal de comando), para escolher a falha depois de
  entrar no mundo.

| modo | o que faz | onde |
|---|---|---|
| `ctor:segv` | escreve num ponteiro nulo num construtor estático | `dlopen` (L) |
| `init:segv` | idem, no `tlmod_mod_init` | L |
| `init:throw` | `throw std::runtime_error` no `init` | L |
| `init:hang` | `for (;;) {}` no `init` | L |
| `frame:segv:N` | ponteiro nulo no frame, N segundos depois de entrar no mundo | P |
| `frame:hang:N` | laço infinito no frame, depois de N segundos | P |
| `ai:throw` | invoca um NPC cujo `on_ai` lança | P |
| `command:throw` | um botão do painel cujo comando lança | UI |
| `abort` | `abort()` no frame | P |

**Em cada experimento:**

- anotar no `tlmod.log` a linha "sessão iniciada … (pid N)";
- reabrir o jogo;
- conferir a linha do modo seguro no log e o estado e os detalhes do mod na Oficina.

O resultado "com o desenho" vale depois dos passos da seção 11; o "hoje" documenta o comportamento atual.

### E1: queda no `init`

1. `modo.txt` = `init:segv`. Abrir o jogo.
2. **Hoje:** o jogo fecha em poucos segundos, a cada abertura.
3. **Com o desenho:** o jogo fecha uma vez. Na abertura seguinte:
   - o log tem "modo seguro: tlmod.falhas suspenso (o jogo fechou enquanto este mod carregava)";
   - a Oficina mostra "Suspenso" com o motivo;
   - os outros mods carregam.
4. Tocar em "Ativar": o laço volta uma vez e o mod é suspenso de novo.

### E2: queda num construtor estático

`ctor:segv`. Igual ao E1, com o motivo apontando a abertura da biblioteca. Confirma que o marcador cobre o
`dlopen`.

### E3: exceção no `init`

`init:throw`.

- **Hoje:** o processo morre por `std::terminate` (SIGABRT; o logcat mostra "terminating with uncaught
  exception").
- **Com o passo 1:** nada cai. A Oficina mostra "Erro: exceção em tlmod_mod_init: …", e os outros mods
  carregam.

### E4: `init` que não termina

`init:hang`.

- **Com o desenho:** o jogo abre e o menu funciona. Depois de 30 s, o log tem "tlmod.falhas não terminou o
  init". Fechar o jogo normalmente e reabrir: o mod está suspenso.
- **Hoje:** o jogo abre sem painel, sem Oficina e sem o que o `LoadAll` liga no fim (itens, receitas,
  texturas, frame e saves), também para os mods que já tinham carregado. Só os ouvintes de dano e de NPC
  deles funcionam, porque o construtor do `ModLoader` os liga antes. Pela interface, não há como desligar o
  mod.

### E5: queda durante o jogo

`frame:segv:10`, entrando num mundo.

- **Primeira queda:** reabrir mostra "uma queda atribuída a tlmod.falhas", e o mod continua ligado. Num
  Android 12+, se o tombstone confirmar a biblioteca, ele já sai suspenso.
- **Segunda queda:** suspenso.

### E6: travamento durante o jogo

`frame:hang:10`.

1. Depois de uns 10 s parado, aparece a caixa "O mod tlmod.falhas está travando o jogo".
2. "Esperar": a caixa volta 20 s depois.
3. "Fechar o jogo e desligar o mod": o jogo fecha; na abertura seguinte o mod está suspenso, com o motivo
   "travou o jogo".
4. Repetir apertando Home durante o travamento, sem tocar na caixa, e encerrar o app pelas Configurações do
   Android: na reabertura, o mod também está suspenso, porque o travamento ficou no diário.

### E7: exceção no `on_ai`

`ai:throw` e invocar o NPC.

- **Hoje:** o processo morre.
- **Com o passo 1:** o NPC usa a IA da casca e o log conta as exceções. Depois de 10, o `on_ai` do mod é
  desligado até fechar o jogo, e a Oficina mostra "Erro".

### E8: exceção num comando

`command:throw` e tocar no botão.

- **Hoje:** o processo morre.
- **Com o passo 1:** um aviso "erro no mod" e nada mais.

### E9: falsos positivos

Nenhum mod pode ser suspenso em nenhum destes:

1. Jogar 1 min, apertar Home, encerrar o app em segundo plano (`adb shell am kill <pacote>` ou outro app
   pesado) e reabrir.
2. Sair do jogo pelo menu e reabrir.
3. Build de teste da ponte com um comando que derruba o processo fora de qualquer chamada a mod. Na
   primeira vez, nada. Na segunda seguida, o modo seguro geral abre sem mods e avisa.

### E10: a cadeia de sinais

**Antes, sem aparelho:** o APK é um zip. Abrir `assets/bin/Data/boot.config` e procurar
`androidChainedSignalHandlerBehavior`. Sem a linha, o Unity está no modo padrão (3.3).

Build de teste da ponte. Registrar no log, em três momentos (início da thread B, primeiro frame, 60 s
depois), para SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL, SIGPIPE, SIGSTKFLT, SIGTRAP, SIGPWR, SIGXCPU,
SIGCONT, SIGINT e SIGWINCH:

- o tratador visto pelo `sigaction` normal, a visão da `libsigchain`;
- o tratador visto pelo `sigaction64` da libc, pego com `dlsym`, a visão do kernel;
- `dladdr` de cada um: biblioteca e símbolo;
- as linhas `SigCgt` e `SigBlk` de `/proc/self/status`.

**Esperado:**

- SIGPWR e SIGXCPU no trampolim da `libil2cpp.so` (deslocamento `0x69CA00`);
- SIGSEGV e SIGBUS no `bytesig` da `libtlmod_bridge.so` depois do primeiro hook, com o anterior na
  `libsigchain`;
- na visão da `libsigchain`, SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV e SIGSTKFLT no tratador do Unity
  (`libunity.so`, deslocamento `0x2B35CC`), a não ser que o `boot.config` diga `disabled`. SIGPIPE também,
  se não vinha ignorado;
- SIGCONT, SIGINT e SIGWINCH sem tratador do IL2CPP.

Depois de uma queda proposital (E1 ou E5), o logcat deve ter a linha `CRASH` "Tombstone written to: …", com
o caminho do relatório do Unity (2.6).

### E11: motivo da morte pelo Android

Num aparelho com Android 11+, logo depois do E1, do E5 e do E9.1, registrar
`getHistoricalProcessExitReasons(pacote, 0, 5)`.

**Esperado:**

- `REASON_CRASH_NATIVE` no E1 e no E5;
- `REASON_SIGNALED` ou `REASON_OTHER` no E9.1 (o código exato varia com o jeito de encerrar);
- no Android 12+, o tombstone do E5 tem um frame em `code_cache/tlmod/tlmod.falhas/`.

### E12: carregador

Num APK de teste com o carregador de referência (2.5):

1. Tirar a `libtlmod_bridge.so` do APK e abrir o jogo. **Esperado:** o aviso "TLMod não carregou", e o
   jogo abre sem o TLMod. Com um carregador sem `try/catch`, o jogo fecha.
2. Recolocar a biblioteca, criar `Android/data/<pacote>/files/tlmod/desligar` e abrir. **Esperado:** o
   jogo abre sem o TLMod e sem aviso.
3. Se houver um aparelho com Android 6: **esperado**, o jogo abre sem o TLMod. Sem o teste de versão, o
   aviso mostraria o símbolo `__write_chk` ou `__fwrite_chk`.

---

## 8. Riscos do desenho proposto

1. **Culpar o mod errado.**
   - Uma queda em outra thread enquanto um mod está em chamada em P aponta para ele. Mitigação: no jogo,
     duas quedas ou a confirmação do Android (2.3).
   - Durante a carga, o jogo já roda em P: uma queda do próprio jogo nesse momento culparia o mod em
     `init`. É raro, o usuário religa com um toque, e no Android 12+ o tombstone desfaz a dúvida.
2. **Não culpar ninguém.** Um mod que estraga dados do jogo e o faz cair depois, fora de qualquer chamada
   ao mod, não aparece no marcador. Mitigação: o modo seguro geral (2.5) e a lista de "quedas sem mod" no
   log.
3. **Custo no caminho quente.** Duas escritas por chamada de `on_ai`. Medir com o E3 da auditoria 03; se
   pesar, marcar por NPC e por tick, não por chamada.
4. **Vigia com falso alarme.** Telas de carga lentas em aparelhos fracos, ou pausas que o ciclo de vida não
   pegou. Mitigação: armar depois do primeiro frame, 10 s, e só culpar quando houver mod em chamada.
5. **Um mod mal-intencionado desfaz tudo.** Pode apagar o diário, editar a configuração ou as chaves
   confiáveis. Isso é inerente ao código nativo (6.5).
6. **Mudança de hábito.** Com os mods no armazenamento interno, o usuário não "arrasta para a pasta" mais;
   instala pela Oficina. Mitigação: o seletor do sistema e o modo de desenvolvimento para as pastas soltas.
7. **Superfície de ataque nova.** Ler zip e PNG de terceiros. Mitigação: os limites da 6.3 e testes de host
   com arquivos malformados (a infraestrutura de testes de host já existe).
8. **Chave perdida pelo autor.** Sem a chave antiga, não há como atualizar sem confirmação manual. Mitigação:
   a troca de chave assinada (6.4) e uma mensagem clara.
9. **Comportamento da plataforma.** `libsigchain`, `ApplicationExitInfo` e o `mmap` mudam entre versões do
   Android. Mitigação: o diário funciona em qualquer versão; o resto só acrescenta.
10. **Queda que não termina o processo.** No modo `legacy` do Unity, ou se o tratador dele travar dentro do
    `malloc`, a queda vira travamento (3.3). Mitigação: o vigia (5.2). Sem mod em chamada, nada é suspenso,
    mas o log registra a parada.

---

## 9. Respostas diretas

**1. Modo seguro.**

- **Detectar:** um diário de uma página mapeado em memória no armazenamento interno, com a fase da abertura
  (ponte, carga, jogo), se o app está em primeiro plano, um contador de frames e, por thread, qual mod está
  em chamada. Os sentinelas de etapa viram mudanças de campo no diário; as chamadas a mod, um contador `seq`
  por thread. Duas escritas por chamada, sem syscall.
- **Apontar:** na abertura seguinte, antes do `LoadAll`, o runtime aplica a tabela da 2.3:
  - morte em segundo plano não conta;
  - morte na carga suspende o mod em `dlopen` ou `init`;
  - travamento registrado pelo vigia suspende o mod;
  - morte no jogo suspende na segunda queda, ou na primeira se o Android 12+ confirmar pela biblioteca.
- **Mostrar:** a suspensão vai para `suspended_mods` em `tlmod_config.json`, e a Oficina mostra "Suspenso"
  com o motivo.
- **O próprio TLMod:** duas quedas seguidas da ponte deixam a abertura seguinte sem hooks.
- **Antes do TLMod:** o carregador precisa de `try/catch`, teste de versão e uma chave para não carregar.
  O jogo aceita Android 6, e as `.so` do TLMod exigem o 7.

**2. Sinais.**

- **A `libil2cpp.so`:** trata SIGPWR e SIGXCPU para o coletor de lixo e SIGCONT, SIGINT e SIGWINCH para o
  console. SIGSEGV e SIGBUS só com `GC_LOOP_ON_ABORT`. O `NullReferenceException` vem de testes explícitos
  (44.549), não de sinal. Confirmado no binário.
- **O Unity:** a `libunity.so` trata SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGPIPE e SIGSTKFLT, com
  `SA_SIGINFO | SA_ONSTACK | SA_RESTART`. Grava um relatório e encaminha para o tratador anterior, que
  guardou. A opção `androidChainedSignalHandlerBehavior` desliga tudo (`disabled`) ou muda para `legacy`,
  que pode usar `longjmp` ou parar a thread para sempre. Confirmado no binário; o modo do jogo está no
  `boot.config` do APK (E10).
- **O TLMod:** já tem um tratador de SIGSEGV e SIGBUS na frente da cadeia, o `bytesig` do ShadowHook, que
  encadeia para o anterior.
- **Encadear mais um** não quebra o IL2CPP nem o Unity, com estas condições:
  - pelo `sigaction` normal (atrás da ART), com `SA_SIGINFO | SA_ONSTACK`;
  - só escritas no diário;
  - sempre encadeando: o anterior será o do Unity, e é ele que grava o relatório;
  - sem tocar em SIGPWR e SIGXCPU;
  - conferindo de tempos em tempos se continua instalado, porque o Unity pode devolver as ações antigas.
- **Alternativa recomendada:** o diário mais o motivo de saída do Android 11+ (com o tombstone no 12+), sem
  tratador novo.

**3. Exceções.**

- **Exceção de mod:** derruba o processo quando sai do `init`, de `on_spawn` ou `on_ai`, do `Hurt` ou de um
  provedor ou comando; nenhum desses caminhos captura.
- **Onde já há proteção:** frame (um `catch` para todos os mods), callbacks de item, textura e nome, e
  saves.
- **Exceção do jogo:** chamada por `runtime_invoke` volta como valor; dentro de hooks atravessa o TLMod, mas
  deixa preso o contador do `Hurt` e as trocas de textura. Os saves já fazem o certo.
- **Correção:** `catch (...)` no SDK (dentro do mod) e no runtime (por chamada, com contador), e o modelo
  dos saves nos hooks com estado.

**4. Travamento.** O hook de `Main.DoUpdate` soma um frame no diário. Um vigia nativo, sem IL2CPP e sem
travas, confere a cada 500 ms:

- com o app em primeiro plano, frames parados há 10 s e o marcador de P dentro de um mod: grava o
  travamento no diário e pede ao painel a caixa "Esperar / Fechar e desligar";
- no `init`, o limite é 30 s; num provedor ou comando, 5 s.

Ninguém interrompe a thread. O registro no diário garante a suspensão mesmo que a caixa não apareça.

**5. Integridade e origem.**

- **Formato:** um `.tlmod` é um zip com `manifest.json`, que lista cada arquivo com SHA-256 e tamanho, e
  `signature.json` opcional (Ed25519 sobre os bytes do manifest).
- **Verificação:** completa (caminhos, limites, hashes, sobras e faltas) numa pasta de preparo interna,
  antes de ir para `files/tlmod/mods/`. Confiança por id no primeiro uso, com troca de chave assinada.
- **O que já existe:** o SHA-256 e a cópia atômica da biblioteca, sem hash esperado.
- **Limite:** um mod nativo pode tudo que o jogo pode e não há como isolá-lo; a assinatura só prova quem
  fez.
- **Módulo 8:** só scripts num interpretador podem ter permissões impostas.

**6. Experimentos.** O mod `tlmod.falhas` com nove modos de falha e os experimentos E1 a E12 da seção 7:

- queda e exceção no `init` e no construtor;
- `init` que não termina;
- queda e travamento no jogo;
- exceção no `on_ai` e num comando;
- falsos positivos;
- a cadeia de sinais e o motivo de saída do Android;
- o carregador.

---

## 10. Lacunas do TLMod, em ordem de risco

1. <a id="lacuna-1"></a>**Laço de queda sem saída** (alto). Um mod que derruba o jogo no `dlopen` ou no
   `init` volta a carregar a cada abertura, antes de o usuário conseguir abrir a Oficina (`LoadAll` sem
   registro de fase, `mod_loader.cpp:1107–1273`). A pasta do mod fica em `Android/data`, que o Android 11+
   esconde do gerenciador de arquivos do sistema (inferido). Ações 2 e 3.
2. <a id="lacuna-2"></a>**Exceções de mod sem captura** (alto). `init` (`mod_loader.cpp:1209`), `on_spawn` e
   `on_ai` (`:395`, `:415`), `Hurt` (`:130–136`), provedores e comandos (`:914`, `:943`), e nenhum
   trampolim do SDK (`tlmod.hpp:236–298`). Uma exceção nesses caminhos derruba o processo. Ação 1.
3. <a id="lacuna-3"></a>**Travamento não detectado** (médio). Um laço num callback congela o jogo sem
   registro. No `init`, deixa o jogo sem os hooks do fim do `LoadAll` e sem painel nem Oficina
   (`android_main.cpp:231–258`), sem jeito de desligar o mod pela interface. Uma queda também pode virar
   travamento, pelo tratador do Unity (3.3). Ação 4.
4. <a id="lacuna-4"></a>**Carga do TLMod sem rede de proteção** (médio). O TLMod não traz carregador
   (`entrega/README_TESTE.md:115–117`), e uma falha no `System.loadLibrary` acontece antes de qualquer
   código dele. No Android 6, que o jogo aceita (`minSdkVersion` 23), ela é certa: as `.so` do pacote de
   teste exigem `__write_chk` e `__fwrite_chk` (`LIBC_N`, Android 7). Com um carregador sem `try/catch`, o
   jogo não abre. Ação 3.
5. <a id="lacuna-5"></a>**Pacote sem integridade nem origem** (médio). O manifest não lista arquivos nem
   hashes. Textura e manifest são lidos do armazenamento externo a cada abertura. O SHA-256 da biblioteca é
   calculado, mas não comparado com nada. Não há assinatura. Ações 7 e 8.
6. <a id="lacuna-6"></a>**Estado preso quando o jogo lança dentro de um hook** (médio-baixo). O
   `t_hurtDepth` (`bridge.cpp:384–402`) e as trocas de textura (`draw_hooks.cpp:471–525`). Ação 5.
7. <a id="lacuna-7"></a>**Um `catch` para todos os frames** (médio-baixo). Um mod que lança no frame impede os
   mods seguintes no laço de rodar, em todo frame, e nada vai para o log (`bridge_adapter.cpp:227–231`,
   `mod_loader.cpp:285–292`). Ação 1.
8. <a id="lacuna-8"></a>**Desligado sem motivo na Oficina** (baixo). As duas Oficinas só mostram o erro dos
   estados de falha (`panel.cpp:319` e `:556`). Ação 2.
9. <a id="lacuna-9"></a>**Configuração sem `fsync`** (baixo). `ConfigStore::Save` renomeia sem `fsync`
   (`config_store.cpp:117–153`): falta de energia logo depois de mudar a configuração pode perdê-la. Ação 2.

---

## 11. Ações recomendadas e ordem de implementação

Em ordem. O que vem primeiro evita as quedas mais prováveis; o modo seguro vem logo depois, para as que
sobrarem.

1. **Capturar exceções em toda chamada a mod** (1 dia): `catch (...)` nos trampolins e na `TLMOD_MOD` do SDK,
   e no runtime por chamada e por mod, com contador e desligamento na sessão
   ([4.4](#44-caminho-de-implementação), passos 1 e 2). [Lacunas 2](#lacuna-2) e [7](#lacuna-7).
2. **Diário e suspensão automática** (2–3 dias):
   - o diário mapeado;
   - os marcadores em toda chamada a mod, inclusive o `dlopen`;
   - a fase e o contador de frames;
   - as regras da 2.3 em `RuntimeMain`;
   - `suspended_mods` com `fsync`;
   - o motivo na Oficina e o aviso no painel.

   [Lacunas 1](#lacuna-1), [8](#lacuna-8) e [9](#lacuna-9).
3. **Modo seguro do carregador, da ponte e geral** (1–1,5 dia): o carregador de referência no README de
   instalação, a thread B lendo o diário antes dos hooks, o modo seguro geral e as chaves manuais (2.5).
   Decidir entre exigir Android 7 e compilar para a API 23. [Lacunas 1](#lacuna-1) e [4](#lacuna-4).
4. **Vigia de travamento** (1–1,5 dia): thread nativa, regras da 5.2, caixa no painel e
   `ActivityLifecycleCallbacks`. [Lacuna 3](#lacuna-3).
5. **Estado dos hooks** (0,5–1 dia): `Hurt` pelo modelo dos saves e varredura das trocas de textura no
   `DoUpdate` ([4.4](#44-caminho-de-implementação), passo 3). [Lacuna 6](#lacuna-6).
6. **Motivo de saída do Android** (0,5–1 dia): `getHistoricalProcessExitReasons` no painel (API 30+) e o
   tombstone no 12+, alimentando as regras da 2.3.
7. **Pacote `.tlmod`** (3–5 dias): formato, verificação completa na pasta de preparo, instalação pelo
   seletor do sistema, mods no armazenamento interno, comparação do hash a cada abertura e modo de
   desenvolvimento para as pastas soltas (6.2, 6.3). [Lacuna 5](#lacuna-5).
8. **Assinatura e confiança** (1–2 dias): Ed25519, chave fixada por id, troca de chave assinada, e "Nativo
   (acesso total)" na Oficina (6.4, 6.6). [Lacuna 5](#lacuna-5).
9. **Opcional: tratador mínimo de SIGSEGV** (1 dia, mais testes): só se o Android 7 a 10 precisar apontar a
   biblioteca; com as condições da 3.5.

Depois de cada passo, o experimento correspondente da seção 7:

| passos | experimentos |
|---|---|
| 1 | E3, E7, E8 |
| 2 e 3 | E1, E2, E5, E9, E12 |
| 4 | E4, E6 |
| 6 | E11 |
| antes de decidir o passo 9 | E10 |
