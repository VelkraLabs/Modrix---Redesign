# Auditoria 08: camada de script para mods

Terraria Mobile 1.4.4.9 (Unity 2021.3.56f2, IL2CPP, Android arm64), comparado com o TLMod da branch
`ccr-bbd92bf9-i01buk` (código da ponte e do runtime no commit `f917b04`; os commits seguintes só
acrescentaram auditorias e arquivos do jogo).

Auditoria só de leitura, com benchmarks descartáveis fora do repositório. As fontes são:

- o código do runtime e da API (`modapi/`), da ponte (`il2cpp_bridge/`) e os READMEs;
- o código da ShadowHook 2.0.1, que a ponte compila (`il2cpp_bridge/CMakeLists.txt:24–26`);
- as auditorias 01 (persistência), 03 (hooks) e 09 (falhas e segurança);
- o código-fonte de oito motores de script e de WebAssembly, compilados aqui para o host e para arm64
  ([seção 1](#1-método)).

Nenhum código foi alterado; este arquivo é o único acréscimo. Fontes dos motores, benchmarks, gerador de
ligações e binários ficam fora do repositório.

"Licença", aqui, é a licença de software de cada motor. A verificação de licença do jogo, a proteção contra
alteração, a assinatura e o empacotamento do APK e o anticheat estão fora do escopo.

**Módulo 8:** camada de script para mods.

1. Lua (interpretado, e LuaJIT se for viável), QuickJS e um runtime de WebAssembly: tamanho na `.so`,
   custo de uma chamada de gancho, memória, licença, maturidade no Android arm64 e facilidade para quem
   escreve mods.
2. Um JIT precisa de memória executável. Um app comum pode ter isso no Android atual? Se não puder, quanto
   custa ficar só no interpretador?
3. Num benchmark no host, quanto custa chamar um gancho de script muitas vezes por frame (a IA de 200
   NPCs)? Cabe nos 60 FPS de um celular intermediário?
4. O que um script consegue ou não fazer (arquivos, rede, memória)? Como dar acesso só à API do TLMod e a
   uma pasta própria do mod?
5. Como expor a API em C sem escrever cada ligação à mão? Como ficam eventos, dados salvos e telas?
6. Onde ficam os scripts, como editar e recarregar sem fechar o jogo, como mostrar os erros?
7. Mods de script e mods nativos podem conviver e se chamar?

A entrega é a recomendação, os números do benchmark e um plano de protótipo.

- [Como ler](#como-ler)
- [Resumo](#resumo)
- [1. Método](#1-método)
- [2. Os motores (pergunta 1)](#2-os-motores-pergunta-1)
- [3. JIT no aparelho (pergunta 2)](#3-jit-no-aparelho-pergunta-2)
- [4. Desempenho (pergunta 3)](#4-desempenho-pergunta-3)
- [5. Segurança (pergunta 4)](#5-segurança-pergunta-4)
- [6. API (pergunta 5)](#6-api-pergunta-5)
- [7. Fluxo no celular (pergunta 6)](#7-fluxo-no-celular-pergunta-6)
- [8. Convivência com os mods nativos (pergunta 7)](#8-convivência-com-os-mods-nativos-pergunta-7)
- [9. O TLMod hoje](#9-o-tlmod-hoje)
- [10. Recomendação](#10-recomendação)
- [11. Plano de protótipo](#11-plano-de-protótipo)
- [12. Experimentos no aparelho](#12-experimentos-no-aparelho)
- [13. Respostas diretas](#13-respostas-diretas)
- [14. Lacunas do TLMod, em ordem de risco](#14-lacunas-do-tlmod-em-ordem-de-risco)
- [15. Ações recomendadas](#15-ações-recomendadas)

---

## Como ler

**Níveis de confiança**, os mesmos das auditorias anteriores, com o sentido ajustado a este módulo:

- **confirmado no binário**: medido nos binários compilados aqui (host x86-64 e `.so` arm64), ou lido no
  código-fonte (TLMod, motores, ShadowHook), com arquivo e linha;
- **inferido**: decorre do que foi medido ou lido, mas um passo não foi verificado. O caso mais comum é a
  conversão dos tempos do host para o celular;
- **só o aparelho confirma**: depende do aparelho. Cada caso tem um experimento na
  [seção 12](#12-experimentos-no-aparelho).

**Termos:**

- **VM:** um estado do motor (`lua_State`, `JSRuntime` com `JSContext`, instância wasm);
- **gancho:** função do script que o TLMod chama, como o `on_ai` de um NPC;
- **fronteira:** a passagem entre C e script, nos dois sentidos;
- **proxy:** objeto do script em que cada leitura ou escrita de campo chama uma função C sobre a struct;
- **tabela copiada:** o host copia todos os campos da struct para uma tabela antes da chamada e lê de volta
  depois;
- **visão:** a struct em memória, lida direto pelo script com checagem de faixa (`buffer` do Luau,
  `Float32Array` do JavaScript, memória linear do wasm);
- **JIT** gera código de máquina durante o jogo; **AOT** gera antes, no PC (`wamrc`).

**Threads**, as das auditorias 03 e 07, mais uma:

- **P:** a principal do Unity, onde rodam `subscribe_frame`, `on_ai` e os eventos (`tlmod_api.h:165`, `:241`);
- **L:** a carga do runtime (`RuntimeMain`, `modapi/adapters/bridge/android_main.cpp:190`), onde roda o
  `tlmod_mod_init`;
- **U:** a thread de UI do Android, onde chegam os provedores e os comandos do painel
  (`tlmod_api.h:226–237`).

**Fator do celular.** Os tempos do host viram estimativas para um celular intermediário multiplicados por
3 (inferido; [1.4](#14-do-host-para-o-celular)).

---

## Resumo

1. **Recomendação: Luau,** interpretado, com uma VM por mod, o isolamento do próprio motor e a API gerada a
   partir do `tlmod_api.h`.
   - Entre as linguagens de script avaliadas, é a única feita para rodar código de terceiros (o wasm também
     é, mas o mod precisa ser compilado no PC). No modo isolado, bibliotecas e globais ficam somente leitura,
     o script não carrega bytecode e uma interrupção barata limita a CPU. Confirmado no binário
     ([seção 5](#5-segurança-pergunta-4)).
   - É o mais rápido das linguagens de script sem JIT. A IA de um NPC custa 0,23 µs no host com a struct
     como `buffer`, e 0,44 µs como proxy.
   - Custa 821 KiB na `.so` arm64 (fora a libc++, que o runtime já tem) e uns 200 KiB de RSS por VM.
   - **Alternativa:** Lua 5.4 ou 5.5, se tamanho e memória pesarem mais. Ocupam 221–236 KiB e 84–86 KiB por
     VM, mas o isolamento é montado à mão e não há visão de memória: a IA vai por proxy, a 0,49–0,51 µs.
   - **Depois, para mods compilados:** WebAssembly com o WAMR. Tem o melhor isolamento e, com AOT, o menor
     custo (0,11 µs por NPC), mas o mod precisa ser compilado no PC.
   - **Ficam fora da primeira versão:** QuickJS (o mais lento e o mais pesado) e LuaJIT (o JIT não ajuda nos
     ganchos e atrapalha o limite de CPU).
2. **Números.** A tabela mede a IA de 200 NPCs com o `TLModNpcState` real. Os tempos do host são a mediana
   de 5 rodadas; os do celular, o host multiplicado por 3.

   | motor | melhor forma segura | host (µs por frame) | celular (µs; % de 16,7 ms) | RSS por VM | `.so` arm64 (-O2) | licença |
   |---|---|---:|---|---:|---:|---|
   | C nativo (referência) | struct | 8,8 | 26; 0,16% | — | — | — |
   | WAMR AOT | memória linear | 22,0 | 66; 0,40% | 44 KiB | 127 KiB | Apache 2.0 com exceção LLVM |
   | wasm3 | memória linear | 26,5 | 80; 0,48% | 190 KiB | 227 KiB | MIT |
   | WAMR, interpretador rápido | memória linear | 44,9 | 135; 0,81% | 30 KiB | 142 KiB | Apache 2.0 com exceção LLVM |
   | **Luau** | **buffer** | **45,2** | **136; 0,81%** | **200 KiB** | **821 KiB** | **MIT** |
   | LuaJIT | proxy | 76,1 | 228; 1,37% | 92 KiB | 556 KiB | MIT |
   | Luau | proxy | 88,3 | 265; 1,59% | 200 KiB | 821 KiB | MIT |
   | Lua 5.5 | proxy | 97,6 | 293; 1,76% | 86 KiB | 236 KiB | MIT |
   | Lua 5.4 | proxy | 102,0 | 306; 1,84% | 84 KiB | 221 KiB | MIT |
   | QuickJS | visão | 103,1 | 309; 1,86% | 338 KiB | 872 KiB | MIT |
   | quickjs-ng | visão | 152,2 | 457; 2,74% | 356 KiB | 945 KiB | MIT |

   - Todas as linhas chegam ao mesmo estado final que o C (checksum dos 200 NPCs).
   - RSS por VM é a memória residente de cada VM a mais, já com o script e os 200 objetos.
3. **Cabe nos 60 FPS.** Com Luau, 200 ganchos de IA por frame custam 0,8% a 1,6% do frame no celular
   (inferido). O orçamento sugerido para todos os scripts é 1 ms por frame, onde cabem de 750 a 1.500 ganchos
   de IA. O que não cabe é ligar um gancho de script a cada entidade do jogo, como os 1.000 projéteis.
4. **JIT é permitido, mas quase não ajuda.**
   - A ShadowHook que o TLMod usa já cria memória executável (`sh_trampo.c:99`), e os ganchos funcionam no
     aparelho.
   - Nos ganchos, o tempo está na fronteira com o C: o LuaJIT com e sem JIT dá os mesmos 76,1 µs. Só em
     cálculo puro o JIT vale 2 a 2,5 vezes.
   - Com JIT, o limite de CPU do LuaJIT por gancho de contagem deixa de funcionar: 4 de 4 laços infinitos
     não pararam.
5. **Segurança.** Sem as bibliotecas de sistema, o script só enxerga a API. Memória, CPU e pilha têm limite,
   testado em cada motor. Arquivos, só por uma API presa à pasta de dados do mod; rede, nenhuma. Não é uma
   barreira de processo, mas é muito mais que um mod nativo, que pode tudo (auditoria 09).
6. **API gerada.** Um gerador de 574 linhas lê a AST do `tlmod_api.h` e produz as 25 ligações da
   `TLModHostApi`: 768 linhas de C, com 6 trampolins de callback e 4 structs. Só 4 anotações foram escritas
   à mão. O código gerado compilou contra o cabeçalho real e passou num teste com host falso. Confirmado no
   binário.
7. **Fluxo no celular.** Os scripts ficam na pasta do mod, e recarregar troca a VM do mod sem fechar o jogo.
   Para isso o runtime precisa desfazer o que um mod assinou, e ainda não faz: o `FrameSub` nem guarda o dono
   (`mod_loader.cpp:73–76`). Os erros vão para o `tlmod.log` e para a Oficina, com arquivo e linha.
8. **Convivência.** Um mod de script é um mod como os outros no `ModLoader`, que já aceita mods embutidos
   (`AddBuiltin`, `mod_loader.cpp:998`). Os dois tipos se encontram pelas chaves de conteúdo e pelos eventos.
   Chamar funções um do outro pede uma API nova.

**Plano de protótipo** (detalhe na [seção 11](#11-plano-de-protótipo)):

| passo | o quê | esforço |
|---|---|---|
| P0 | host de scripts como mod embutido: Luau, uma VM por mod, API gerada, isolamento e log | 3–5 dias |
| P1 | IA de NPC pela visão `buffer` e o comando `script bench` nas Ferramentas | 2–3 dias |
| P2 | recarregar: desfazer o que o mod assinou, vigia de arquivos, botão na Oficina, erros com contagem | 3–4 dias |
| P3 | dados salvos (`tlmod.data`, no arquivo paralelo) e arquivos na pasta de dados do mod | 2–4 dias |
| P4 | edição no celular: pasta de desenvolvimento escolhida pelo seletor do sistema | 2–3 dias |

Esforço em dias de uma pessoa que conhece o runtime, com build e teste no aparelho (inferido).

---

## 1. Método

### 1.1 Motores

| motor | versão | origem | licença |
|---|---|---|---|
| Lua 5.4 | 5.4.9 | lua.org | MIT (fim do `lua.h`) |
| Lua 5.5 | 5.5.1 | lua.org | MIT |
| LuaJIT | branch `v2.1`, commit `dd3b243` (2026-10-18) | github.com/LuaJIT/LuaJIT | MIT (`COPYRIGHT`) |
| Luau | 0.743, commit `9d7748a` (2026-10-16) | github.com/luau-lang/luau | MIT (`LICENSE.txt`) |
| QuickJS | 2026-06-04 | bellard.org/quickjs | MIT (`LICENSE`) |
| quickjs-ng | 0.17.0, commit `6d46d07` | github.com/quickjs-ng/quickjs | MIT |
| wasm3 | 0.9.2 em desenvolvimento, commit `28ecb9a` (2026-09-29) | github.com/wasm3/wasm3 | MIT |
| WAMR | 2.4.5, commit `25bd7eb` | github.com/bytecodealliance/wasm-micro-runtime | Apache 2.0 com exceções LLVM |

- **LuaJIT:** medido com JIT e só com o interpretador (`luaJIT_setmode`).
- **Luau:** com e sem o gerador de código nativo (`Luau.CodeGen`).
- **WAMR:** com os interpretadores clássico e rápido, e em AOT. O módulo foi compilado pelo `wamrc` 2.4.5
  oficial.

### 1.2 Compilação

- **Host:** clang 18.1.3 com `-O2 -DNDEBUG -fPIC`.
  - Ficam de fora as bibliotecas de sistema dos motores: `quickjs-libc`, WASI e `io`/`os` do Lua.
  - Máquina: Xeon da família Cascade Lake (família 6, modelo 85), 2,8 GHz, numa VM com 4 vCPUs.
- **arm64:** NDK r27c (clang 18.0.3) e `android-24`, como o TLMod.
  - Flags: `-ffunction-sections -fdata-sections -fvisibility=hidden` com `--gc-sections`, sem símbolos.
  - Cada motor vai numa `.so` com uma sonda que faz o que um host do TLMod faria: cria a VM, abre só as
    bibliotecas seguras, registra uma função C e uma classe, carrega texto, chama uma função e fecha. O
    linker descarta o que a sonda não usa.
  - A `.so` vazia tem 4 KiB.
  - Luau é C++. A sonda liga a `libc++_shared`, para não contar a libc++ que o runtime do TLMod já liga
    estaticamente.

### 1.3 Benchmark

- **Jogo falso.**
  - São 200 NPCs de mod com o `TLModNpcState` do próprio `tlmod_api.h`.
  - A cada frame, para cada NPC, o host copia o estado para a struct, chama o gancho e copia de volta, como
    o `OnNpcAi` do runtime (`mod_loader.cpp:406–418`). Depois aplica a velocidade.
- **A IA,** igual em C, Lua, JavaScript e wasm:
  - pede a posição do jogador-alvo (1 chamada à API; o host calcula seno e cosseno);
  - persegue com aceleração e velocidade máxima;
  - conta um temporizador em `ai[0]`, soma 1 a `local[0]` a cada 120 ticks e escolhe o quadro;
  - ao todo, uns 15 acessos a campos.
- **Variantes:**
  - **vazio:** só a chamada, com o índice do NPC;
  - **proxy:** cada campo passa por metamétodo ou getter em C;
  - **tabela** (no Lua) **ou objeto** (no JavaScript): todos os campos copiados antes e depois;
  - **visão:** `buffer` no Luau, `Float32Array` com `Int32Array` no QuickJS, memória linear no wasm;
  - **aloca:** a IA com proxy, criando uma tabela por chamada, o erro comum que alimenta o GC;
  - **ffi:** só no LuaJIT, a struct lida pela FFI. Fica como referência, porque a FFI não pode ser dada a
    mods ([5.1](#51-o-que-o-script-alcança)).
- **Validação.** Um checksum do estado final dos 200 NPCs. As 48 combinações de motor e variante deram o
  mesmo valor que o C. Confirmado no binário.
- **Rodadas.**
  - Cada combinação roda 300 frames de aquecimento e 3.000 medidos, em 5 processos presos num núcleo.
  - Valem a mediana do tempo por frame (a mediana das 5 rodadas), o p99 e o máximo.
  - O máximo inclui ruído da VM: até o C chega a 74–96 µs.
- **Outras medidas:**
  - 2 milhões de chamadas do script a uma função C vazia;
  - cálculo puro (`x = x + i % 7`, 50 milhões de voltas);
  - memória com 1 e com 10 VMs;
  - tempo de criar a VM e carregar o script (200 vezes) e de compilar um script de 3.500 linhas;
  - o custo de deixar o limite de CPU ligado;
  - os testes de isolamento.

### 1.4 Do host para o celular

- **O fator 3 é inferido.**
- O host roda a 2,8 GHz nominais, com turbo, num núcleo da geração Skylake. Um intermediário típico tem
  núcleos grandes Cortex-A76 ou A78 a 2,2–2,6 GHz.
- Pela frequência e pelo trabalho por ciclo, a diferença por núcleo fica entre 1,5 e 2,5 vezes. O 3 cobre o
  aquecimento do aparelho e a thread do jogo dividindo o núcleo.
- Num núcleo pequeno (Cortex-A55) seriam 6 vezes ou mais.
- O [E1](#e1) mede no aparelho.

---

## 2. Os motores (pergunta 1)

### 2.1 Comparação

Tempos no host. "IA" é o custo por NPC na melhor forma segura de cada motor.

| motor | `.so` arm64: -Os / -O2 / gzip | gancho vazio | IA | chamada à API | RSS / malloc por VM | VM + script | 3.500 linhas | licença |
|---|---|---:|---|---:|---|---:|---:|---|
| Lua 5.4 | 188 / 221 / 98 KiB | 30 ns | 510 ns (proxy) | 19,5 ns | 84 / 84 KiB | 109 µs | 1,7 ms | MIT |
| Lua 5.5 | 201 / 236 / 106 KiB | 30 ns | 488 ns (proxy) | 19,5 ns | 86 / 86 KiB | 74 µs | 1,9 ms | MIT |
| LuaJIT com JIT e FFI | 477 / 556 / 283 KiB | 22 ns | 380 ns (proxy) | 14,1 ns | 92 KiB / ¹ | 141 µs | 1,5 ms | MIT |
| LuaJIT só com interpretador ⁵ | 225 / 262 / 130 KiB | 21 ns | 381 ns (proxy) | 14,8 ns | igual | igual | igual | MIT |
| Luau | 777 / 821 / 335 KiB | 39 ns | 226 ns (buffer); 442 ns (proxy) | 22,8 ns | 200 / 439 KiB ² | 164 µs | 5,6 ms | MIT |
| Luau com codegen | 1.253 / 1.342 / 558 KiB | 39 ns | 119 ns (buffer) | 21,4 ns | 262 / 444 KiB | — | — | MIT |
| QuickJS | 683 / 872 / 409 KiB | 25 ns | 515 ns (visão); 706 ns (proxy) | 47,9 ns | 338 / 338 KiB | 245 µs | 4,9 ms | MIT |
| quickjs-ng | 741 / 945 / 450 KiB | 34 ns | 761 ns (visão) | 49,5 ns | 356 / 355 KiB | 279 µs | 4,4 ms | MIT |
| wasm3 | 209 / 227 / 89 KiB | 41 ns | 132 ns | 9,0 ns | 190 / 178 KiB | 31 µs | ³ | MIT |
| WAMR clássico | 118 / 131 / 58 KiB | 49 ns | 411 ns | 42,5 ns | 31 / 23 KiB | 16 µs | ³ | Apache 2.0 + LLVM |
| WAMR rápido | 130 / 142 / 65 KiB | 50 ns | 224 ns | 32,9 ns | 30 / 22 KiB | 26 µs | ³ | Apache 2.0 + LLVM |
| WAMR AOT | 120 / 127 / 53 KiB ⁴ | 31 ns | 110 ns | 15,9 ns | 44 / 20 KiB | 87 µs | ³ | Apache 2.0 + LLVM |
| C nativo | — | 7 ns | 44 ns | — | — | — | — | — |

1. O LuaJIT tem alocador próprio sobre `mmap`, que o malloc não vê. O heap do GC tinha 75 KiB.
2. O Luau aloca por páginas de 16 e 32 KiB, uma por classe de tamanho (`VM/src/lmem.cpp:147–148`). Por
   isso reserva 439 KiB e toca 200 KiB (inferido).
3. O wasm não compila no aparelho: o módulo chega pronto. O da IA tem 905 bytes; em AOT para arm64,
   1.492 bytes.
4. Só o carregador de AOT. Com o interpretador rápido junto, 217 KiB.
5. O tamanho é o do build sem JIT e sem FFI; os tempos, os do LuaJIT completo com o JIT desligado
   (`luaJIT_setmode`). O interpretador é o mesmo nos dois.

Para comparar, sem símbolos: o runtime do TLMod tem 982 KiB, a ponte 461 KiB, a `libil2cpp.so` 46,2 MiB e
a `libunity.so` 11,9 MiB. Confirmado no binário.

### 2.2 Por motor

**Lua 5.4 e 5.5.**

- **Tamanho e memória:** os menores de verdade entre as linguagens de script: 221–236 KiB e 84–86 KiB por
  VM. Compilam 3.500 linhas em 1,7–1,9 ms.
- **Desempenho:** sem visão de memória, a IA vai por proxy (0,49–0,51 µs) ou por tabela copiada
  (0,79–0,82 µs).
- **Isolamento à mão.**
  - O `luaL_openlibs` abre `io`, `os.execute`, `package` (o `package.loadlib` carrega qualquer `.so`) e
    `debug`.
  - O `load` aceita bytecode, que o Lua não valida (testado: `load(string.dump(f))` devolveu uma função).
  - O limite de CPU por gancho de contagem custa 41–43% quando fica armado o tempo todo
    ([5.2](#52-limites-e-custo)).
- **Maturidade:** C puro; compilou sem mudança com o NDK. A 5.4 é a mais usada; a 5.5 é a linha nova.
- **Para quem escreve mods:** a linguagem de script mais comum em mods de jogos. O campo `local` da struct é
  palavra reservada e precisa de outro nome.

**LuaJIT.**

- **Desempenho:** sem FFI, dá o mesmo com e sem JIT (76,1 µs por frame). Cada campo passa por um
  metamétodo em C, e o JIT não compila essas chamadas. Com FFI cai para 13,9 µs, mas a FFI alcança
  qualquer endereço do processo.
- **Isolamento.**
  - Com JIT, o limite de CPU por gancho de contagem não alcança o código compilado
    ([3.3](#33-jit-e-limite-de-cpu)).
  - O build com `LUAJIT_ENABLE_CHECKHOOK` resolve, mas só se o gancho for armado de fora quando o orçamento
    estoura.
- **Maturidade:** arm64 e Android documentados (`doc/install.html:353`). Não há versões numeradas: a
  branch `v2.1` é contínua. O código gerado no aparelho precisa de memória executável perto do
  interpretador (só o aparelho confirma, [E2](#e2)).
- **Para quem escreve mods:** Lua 5.1, sem inteiros.

**Luau.**

- **Isolamento de fábrica:** o `luaL_sandbox` deixa bibliotecas e globais somente leitura, e não existem
  `load` nem `string.dump`. A interrupção custa 2–5%.
- **Desempenho.**
  - O `buffer` entrega a struct inteira em 100 bytes copiados: 0,23 µs por NPC, metade do proxy.
  - As chamadas rápidas de `math` e `buffer` só valem no modo isolado. Sem o `luaL_sandbox`, a mesma IA com
    `buffer` levou 0,45 µs (medido).
  - O gerador nativo, opcional, baixa para 0,12 µs.
- **Custo:**
  - `.so` mais pesada que a do Lua (821 KiB);
  - 200 KiB de RSS por VM, com 439 KiB reservados;
  - compila 3,3 vezes mais devagar que o Lua 5.4, ainda em milissegundos.
- **Maturidade:** mantido pela Roblox, que o usa em produção, inclusive no Android (inferido). Tem gerador
  nativo para arm64 (`CodeGen/src/CodeGenA64.cpp`). Compilou com o NDK sem mudanças.
- **Para quem escreve mods:** a sintaxe do Lua, com tipos opcionais. O verificador de tipos roda no PC e no
  editor, sem custo no jogo.

**QuickJS e quickjs-ng.**

- **Para quem escreve mods:** JavaScript, a linguagem mais conhecida da lista.
- **Desempenho:** os mais lentos.
  - QuickJS: 0,52 µs por NPC com visão e 0,71 µs com proxy.
  - quickjs-ng: 0,76–0,94 µs.
  - Em cálculo puro, 18 vezes o C.
- **Memória e tamanho:** os maiores, tanto por VM (338–356 KiB) quanto na `.so` (872–945 KiB).
- **Isolamento:** sem a `quickjs-libc` não há arquivos nem rede. Memória e CPU têm limite nativo, e a
  interrupção não custou nada no teste.
- **Maturidade:** o Bellard lança por data (2026-06-04); o quickjs-ng é a bifurcação ativa (0.17.0).
- **Ligação escrita à mão erra fácil.** Com `argc` 1, o `JS_NewTypedArray` lê `argv[1]` e `argv[2]` além
  do fim do array. No teste, isso deu deslocamento tirado do lixo da pilha, memória corrompida e checksum
  errado, até passarmos os três argumentos ([6.2](#62-o-gerador)).

**wasm3.**

- Interpretador pequeno (227 KiB) e o mais rápido dos interpretadores: 0,13 µs por NPC. A struct vai
  inteira para a memória linear, e o código do mod é C compilado.
- Tem gás e limite de memória (`m3_SetResourceLimit`, `source/wasm3.h:322`). O gás custou 5%.
- **Maturidade:** o README declara "minimal maintenance phase", embora a branch principal tenha commits em
  2026 (0.9.2 em desenvolvimento). A API mudou nesta versão: `m3_GetMemory` passou a receber o módulo
  (`wasm3.h:336`).

**WAMR.**

- **Tamanho e desempenho:** o menor (127–142 KiB) e o que menos usa memória por instância (30–44 KiB).
  Em AOT fica no nível do C: 0,11 µs por NPC e 0,6 ns por volta no cálculo puro.
- **Sinais e pilha.** A checagem de limites por hardware vem ligada no Android
  (`core/shared/platform/android/platform_internal.h:75`). Com ela, o WAMR:
  - instala tratadores de SIGSEGV e SIGBUS e encadeia os anteriores (`posix_thread.c:609–637`);
  - toca a pilha inteira da thread ao iniciar (`posix_thread.c:524–557`: +8,3 MiB de RSS no host).

  O processo já tem os tratadores do Unity e da ShadowHook (auditoria 09), então o build para o TLMod
  desliga isso (`WAMR_DISABLE_HW_BOUND_CHECK`). A `.so` fica com 139 KiB.
- **O AOT carrega código executável:** depende da mesma permissão de um JIT
  ([seção 3](#3-jit-no-aparelho-pergunta-2)).
- **Para quem escreve mods:** a maior barreira de entrada. O mod é escrito em C, Rust ou AssemblyScript e
  compilado no PC. Em troca, é um caminho isolado para quem hoje escreve mod nativo.

---

## 3. JIT no aparelho (pergunta 2)

### 3.1 É permitido

- **Sim, para um app comum.**
  - Um JIT precisa criar memória executável anônima, ou tornar executável uma região que acabou de gravar.
    No SELinux do Android isso é a permissão `execmem`, que o domínio dos apps comuns tem (inferido).
  - **O TLMod já depende dela.** A ShadowHook 2.0.1 (commit `854c775`):
    - cria os trampolins com `mmap(..., PROT_READ | PROT_WRITE | PROT_EXEC, ...)`
      (`common/sh_trampo.c:99`);
    - deixa o código do jogo gravável e executável antes de escrever o desvio
      (`arch/arm64/sh_inst.c:160`). Confirmado no binário (código).
  - Os ganchos do TLMod funcionam no aparelho, logo o processo do jogo tem essa permissão lá.
- **Exceções** (inferido):
  - ROMs endurecidas, que deixam o usuário bloquear código dinâmico por app. Nelas, nem os ganchos do TLMod
    funcionam;
  - o LuaJIT no arm64 precisa alocar o código gerado perto do interpretador. Se não conseguir, desliga o JIT
    (só o aparelho confirma, [E2](#e2)).
- **Regras de loja** sobre baixar código executável tratam de distribuição: fora do escopo.

### 3.2 O custo de ficar no interpretador

**Nos ganchos** (IA de 200 NPCs, µs por frame no host):

| motor | só interpretador | com JIT ou AOT | ganho |
|---|---:|---:|---:|
| LuaJIT, proxy | 76,1 | 76,1 | 1,0× |
| LuaJIT, FFI (não pode ir para mods) | 222,0 | 13,9 | 16× |
| Luau, buffer | 45,2 | 23,8 (codegen) | 1,9× |
| Luau, proxy | 88,3 | 87,4 (codegen) | 1,0× |
| WAMR | 44,9 (rápido); 82,3 (clássico) | 22,0 (AOT) | 2,0× sobre o rápido |
| wasm3 | 26,5 | — | — |

**Em cálculo puro** (ns por volta; o C faz 1,47):

| motor | interpretador | JIT ou AOT |
|---|---:|---:|
| LuaJIT | 6,47 (4,4×) | 2,93 (2,0×) |
| Luau | 7,48 (5,1×) | 3,47 (2,4×) |
| WAMR | 8,16 rápido (5,6×); 18,5 clássico (12,6×) | 0,59 AOT (0,4×) ¹ |
| wasm3 | 5,56 (3,8×) | — |
| Lua 5.4 / 5.5 | 9,29 / 9,78 (6,3× / 6,7×) | — |
| QuickJS / quickjs-ng | 26,3 / 27,2 (18×) | — |

1. O AOT passa pelo LLVM com `-O3` e as extensões da CPU; o C de referência foi `-O2` genérico.

- **Nos ganchos,** fora a FFI, que não vai para mods, ficar no interpretador custa no máximo 2 vezes, e nada
  com proxy: o tempo está nas idas e voltas ao C.
- **Em cálculo pesado** (procurar caminho, gerar estrutura), custa de 2 a 2,5 vezes no Luau e no LuaJIT.
  Confirmado no binário.
- **Recomendação:** interpretador por padrão. O codegen do Luau entra depois, como opção por função, depois
  de testado no aparelho ([E2](#e2)).

### 3.3 JIT e limite de CPU

- **LuaJIT com JIT.** O gancho de contagem não roda dentro do código compilado. Quatro laços infinitos (vazio,
  com conta, com tabela e com conta estável de ponto flutuante) não pararam; o alarme de 3 s encerrou o
  processo. Confirmado no binário.
- **LuaJIT só com o interpretador:** os mesmos laços param.
- **Build com `LUAJIT_ENABLE_CHECKHOOK`:** param. O custo depende de quando o gancho é armado:
  - desarmado, nenhum: 13,9 µs com FFI e 75,5 µs com proxy;
  - armado o tempo todo, o código compilado volta ao interpretador a cada volta: 272 µs e 117,7 µs.
- **O desenho que funciona:** um vigia arma o gancho (`lua_sethook` com contagem 1) só quando o orçamento
  estoura. É o que o próprio LuaJIT descreve (`src/lj_record.c:2956–2969`), e o que o `lua.c` faz no
  Ctrl-C (`lua.c:76–80`).
- **Luau, QuickJS e wasm** checam a interrupção dentro do próprio motor e não têm esse problema.

---

## 4. Desempenho (pergunta 3)

### 4.1 A IA de 200 NPCs

Formas seguras, da mais rápida para a mais lenta. A coluna "por ms" é quantos ganchos de IA cabem em 1 ms
no celular.

| motor e forma | host: µs por frame | ns por NPC | p99 | máximo | celular: µs por frame | % do frame | p99 no celular | por ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| C nativo | 8,8 | 44 | 30 | 96 | 26 | 0,16% | 90 | 7.578 |
| WAMR AOT | 22,0 | 110 | 48 | 117 | 66 | 0,40% | 143 | 3.029 |
| Luau com codegen, buffer | 23,8 | 119 | 49 | 105 | 71 | 0,43% | 146 | 2.802 |
| wasm3 | 26,5 | 132 | 56 | 113 | 79 | 0,48% | 169 | 2.518 |
| WAMR rápido | 44,9 | 224 | 78 | 156 | 135 | 0,81% | 234 | 1.486 |
| **Luau, buffer** | **45,2** | **226** | **92** | **196** | **136** | **0,81%** | **275** | **1.475** |
| LuaJIT, proxy | 76,1 | 380 | 120–184 | 366–923 | 228 | 1,37% | 359–552 | 877 |
| WAMR clássico | 82,3 | 411 | 139 | 234 | 247 | 1,48% | 417 | 810 |
| Luau, proxy | 88,3 | 442 | 162 | 821 | 265 | 1,59% | 486 | 755 |
| Lua 5.5, proxy | 97,6 | 488 | 161 | 793 | 293 | 1,76% | 483 | 683 |
| Lua 5.4, proxy | 102,0 | 510 | 153 | 480 | 306 | 1,84% | 460 | 654 |
| QuickJS, visão | 103,1 | 515 | 225 | 351 | 309 | 1,86% | 676 | 647 |
| QuickJS, proxy | 141,2 | 706 | 179 | 326 | 424 | 2,54% | 538 | 472 |
| quickjs-ng, visão | 152,2 | 761 | 301 | 1.318 | 457 | 2,74% | 902 | 438 |
| quickjs-ng, proxy | 188,6 | 943 | 296 | 548 | 566 | 3,40% | 889 | 353 |

O LuaJIT aparece com intervalo porque JIT e interpretador deram a mesma mediana, mas caudas diferentes.

**Formas a evitar:**

| motor | tabela ou objeto copiado | aloca uma tabela por chamada (mediana / p99 / máximo) |
|---|---:|---|
| Lua 5.4 | 158,4 | 128,9 / 293 / 635 |
| Lua 5.5 | 162,9 | 129,0 / 305 / 1.523 |
| LuaJIT | 140,6 (JIT); 154,3 (interpretador) | 88,7 / 161 / 880 (JIT) |
| Luau | 184,2 | 104,9 / 268 / 1.278 |
| QuickJS | 180,7 | 171,6 / 303 / 456 |
| quickjs-ng | 204,7 | 220,4 / 303 / 456 |

Valores em µs por frame no host.

### 4.2 Onde vai o tempo

- **A chamada em si é barata:** 21–50 ns por gancho vazio (7 ns em C). Duzentos ganchos vazios custam
  4–10 µs por frame.
- **Cada ida do script ao C custa 9–50 ns:** wasm3 9, LuaJIT 14, Lua 19,5, Luau 23, WAMR rápido 33,
  QuickJS 48–50.
- **O proxy multiplica as idas.** A IA faz uns 15 acessos a campos e 1 chamada à API; com proxy, cada
  acesso é uma ida ao C. É isso que soma os 0,4–0,9 µs.
- **A visão troca as idas por leituras na memória,** com checagem de faixa: metade do custo no Luau, 27% a
  menos no QuickJS.
- **A tabela copiada é a pior:** 25 escritas e 21 leituras por chamada, mesmo quando o script usa 5 campos.
- **Alocar no gancho pesa no GC:** a mediana sobe até 26% e o p99 dobra (Lua 5.4: de 153 para 293 µs).
  Os máximos chegam a 1,3–1,8 ms no host.

### 4.3 Cabe nos 60 FPS

- **Orçamento.** O frame tem 16,7 ms, e o jogo usa a maior parte. O sugerido para todos os scripts juntos é
  1 ms por frame (6%), com o p99 abaixo de 2 ms.
- **200 IAs em Luau cabem:** 136 µs com `buffer` e 265 µs com proxy, ou seja, 0,8–1,6% do frame, com p99
  de 275–486 µs. Inferido; o [E1](#e1) mede.
- **Quantos ganchos cabem em 1 ms no celular:** cerca de 1.475 (Luau com buffer), 755 (Luau com proxy),
  654 (Lua 5.4 com proxy), 472 (QuickJS com proxy) e 2.518 (wasm3).
- **O que não cabe:** um gancho de script em cada entidade do jogo. Mil projéteis por proxy em Luau custam
  1,3 ms, o orçamento inteiro. O runtime já segue a regra certa: só NPCs de mod com `on_ai` pagam a chamada
  (`mod_loader.cpp:405–413`). Para o resto, operações em lote.
- **Regras para o mod, que o host pode medir e avisar:**
  - não alocar em ganchos por tick;
  - guardar estado em `ai` e `local`;
  - usar a visão nos ganchos quentes.

### 4.4 Memória e carga

- **Memória com 10 mods de script** (RSS):
  - Lua 5.4: 0,8 MiB;
  - Luau: 2,0 MiB, com 4,4 MiB reservados;
  - QuickJS: 3,4 MiB;
  - WAMR: 0,3–0,4 MiB, mais a memória linear de cada módulo (64 KiB reservados por instância no teste).
- **A primeira VM** custa mais, porque traz as páginas de código do motor: de 452 KiB de RSS no LuaJIT a
  1.216 KiB no quickjs-ng, e 172–196 KiB no WAMR.
- **WAMR com a checagem por hardware:** soma 8,3 MiB ao iniciar, a pilha inteira tocada
  ([2.2](#22-por-motor)).
- **Carga:** criar a VM e carregar o script leva 74–279 µs. Compilar 3.500 linhas (79 KiB) leva
  1,5–5,6 ms. Recarregar um mod leva milissegundos, mesmo multiplicado por 3 (até 17 ms).

### 4.5 Limites do método

- O host é um x86 numa VM, não um celular. O fator 3 é inferido.
- A IA do teste é pequena. Numa IA real com mais campos, o custo do proxy cresce na mesma proporção; o da
  visão quase não muda.
- A cópia de e para a struct do jogo custa o mesmo em todas as linhas, inclusive no C, e está incluída.
- O máximo por frame mistura GC com ruído da VM do host.

---

## 5. Segurança (pergunta 4)

### 5.1 O que o script alcança

Testado em cada motor, com a configuração isolada do host: só as bibliotecas sem efeito fora da VM, teto de
16 MiB de memória e orçamento de 200 ms de CPU. Confirmado no binário.

| teste | Lua 5.4 | LuaJIT | Luau | QuickJS e quickjs-ng | wasm3 e WAMR |
|---|---|---|---|---|---|
| `io`, `os.execute`, `package`, `debug` na configuração padrão | presentes | presentes (inferido) | só `debug`, limitado | nenhuma (sem `quickjs-libc`) | só os imports dados |
| idem, na configuração isolada | ausentes | ausentes | ausentes | `std`, `os`, `require`, `fetch` ausentes | só os imports dados |
| carregar bytecode feito pelo script | aceito na padrão; recusado na isolada | recusado na isolada | impossível | — | — |
| memória acima do teto | "not enough memory" | "not enough memory" | "not enough memory" | exceção de memória | `memory.grow` devolve -1 |
| acesso fora da memória | — | — | — | — | trap |
| laço infinito | parou (gancho) | **não parou** com JIT; parou sem JIT | parou (interrupção) | parou ("interrupted") | parou com gás ou contagem; sem limite, trava |
| recursão sem fim | erro de memória (teto) | "stack overflow" | "stack overflow" | estouro de pilha | trap (a pilha fica na memória linear) |
| mudar a metatabela das strings | permitido (só na VM do mod) | permitido (inferido) | recusado (somente leitura) | — | — |

### 5.2 Limites e custo

| motor | memória | CPU | custo do limite de CPU | pilha |
|---|---|---|---|---|
| Lua 5.4 e 5.5 | alocador com teto (`lua_newstate`) | gancho de contagem | +41–43% armado sempre; perto de zero com vigia (inferido) | erro do Lua |
| LuaJIT | alocador com teto (aceito no modo GC64) | `CHECKHOOK` mais vigia | zero desarmado | "stack overflow" |
| Luau | alocador com teto | interrupção (`lua_callbacks(L)->interrupt`) | +2–5% | "stack overflow" |
| QuickJS | `JS_SetMemoryLimit` | `JS_SetInterruptHandler` | 0% | `JS_SetMaxStackSize` |
| wasm3 | páginas máximas e `c_m3Limit_MemoryBytes` | gás (`c_m3Limit_GasUnits`) | +5% | trap |
| WAMR | páginas máximas | contagem de instruções (`WAMR_BUILD_INSTRUCTION_METERING`, interpretador) | não medido | trap |

**O vigia do Lua.** O `lua.c` arma um gancho dentro do tratador de sinal (`lua.c:76–80`). O host pode fazer
igual: cada chamada ao script recebe um prazo, e só quando ele passa o vigia arma o gancho. O gancho dispara
o erro. Sem estouro, não há custo.

### 5.3 Arquivos

- O script não vê caminhos do sistema. Só existe `tlmod.fs`, com caminhos relativos a
  `tlmod/data/<id do mod>/`.
- A implementação, no runtime:
  - recusa caminho absoluto, `..`, barra invertida e NUL;
  - abre cada parte com `openat` a partir da pasta do mod, com `O_NOFOLLOW`, sem seguir links;
  - tem cota: tamanho total e número de arquivos por mod.
- A pasta do mod, onde estão o código e as texturas, é só leitura para o script. As texturas continuam pela
  API de conteúdo.
- O I/O roda no runtime: arquivos pequenos na hora, grandes em segundo plano.

### 5.4 Rede

Nenhuma na primeira versão. O APK tem a permissão `INTERNET` (auditoria 09), então um mod nativo pode abrir
sockets; um script só poderia se a API desse.

### 5.5 Memória do processo

- **O script não recebe ponteiros:** só índices, chaves e visões com checagem de faixa.
- **Também não guarda objetos do IL2CPP.** O Boehm do jogo não varre a memória das VMs, e o desenho do
  `tlmod_api.h` já evita esses objetos (`tlmod_api.h:4–7`).
- **Risco que sobra:** um defeito no motor ou numa ligação. O erro do QuickJS achado aqui
  ([2.2](#22-por-motor)) mostra como uma ligação à mão corrompe memória. Por isso as ligações são geradas e
  testadas.
- **Não é barreira de processo.** Mesmo assim, é a única forma de isolar algo: para um mod nativo não há
  nada (auditoria 09, 6.6).

### 5.6 Só a API do TLMod

- **A tabela `tlmod` gerada é a única porta.** A VM abre só a base, sem `dofile` nem `loadfile`, e `math`,
  `string`, `table`, `buffer`, `utf8` e `coroutine`.
- **Uma VM por mod:** um mod não vê os globais de outro.
- **Permissões no manifest** (`"permissions": ["fs", "panel", "spawn"]`), mostradas na instalação
  (auditoria 09, 6.6). As ligações geradas conferem a permissão antes de chamar o runtime.

---

## 6. API (pergunta 5)

### 6.1 Como o cabeçalho se traduz

A `TLModHostApi` (`tlmod_api.h:265–362`) tem 26 funções. O gerador classificou cada uma pelos tipos dos
parâmetros:

| padrão | quantas | funções | o que a ligação faz |
|---|---:|---|---|
| direta | 15 | `mod_id`, `mod_version`, `mod_dir`, `log`, `find_content`, `local_player`, `player_life`, `spawn_npc`, `in_world`, `progress`, `give_item`, `spawn_npc_near_player`, `set_time`, `heal_player`, `kill_hostile_npcs` | confere e converte os argumentos; os enums aceitam o nome (`"item"`) ou o número |
| parâmetros de saída | 1 | `get_time` | devolve vários valores: `ok, dia, hora` |
| texto de saída | 1 | `content_name` | usa um buffer de 256 bytes; se o nome for maior, chama de novo com o tamanho devolvido |
| lista de saída | 1 | `content_ids` | chama com `NULL` para saber quantos, depois preenche uma tabela |
| struct | 3 | `register_item`, `register_npc`, `register_recipe` | a tabela vira struct com `struct_size` preenchido; os arrays com contagem e os callbacks da struct também |
| callback | 4 | `subscribe_player_hurt`, `subscribe_frame`, `register_command`, `register_provider` | guarda a função do script; um trampolim em C a chama com tratamento de erro |
| interna | 1 | `buffer_append` | não aparece: o callback devolve o texto, e o trampolim o grava |

### 6.2 O gerador

**O que ele é.** Um gerador descartável de 574 linhas de Python, fora do repositório.

- Lê a AST que o `clang -Xclang -ast-dump=json` produz do `tlmod_api.h`.
- Escreve 768 linhas de C para Lua 5.4:
  - 25 ligações;
  - 6 trampolins: frame, comando, provedor, golpe, `on_ai` e `on_spawn`;
  - 4 conversores de tabela para struct: item, receita, ingrediente e NPC;
  - as tabelas dos enums.

**O que precisa ser dito à mão: 4 anotações.** São o retorno de cada callback booleano quando o script não
devolve nada:

- `player_hurt` não cancela o golpe;
- em `on_ai`, a IA da casca roda;
- provedor e comando são aceitos.

**As convenções que ele lê do próprio C:**

- `ctx` como primeiro parâmetro (fica implícito);
- `char*` seguido de `size_t` (texto de saída);
- `T*` seguido de `max` (lista);
- `X*` com um campo `X_count` (array);
- ponteiro de função seguido de `user` (callback);
- `struct_size` (versão).

**O que mais ele resolve:**

- a palavra reservada: o campo `local` vira `local_`;
- o runtime mais antigo: antes de cada chamada, confere o `struct_size`, e uma função que não existe dá
  "tlmod.register_recipe: indisponível nesta versão do runtime";
- a thread U: cada trampolim trava a VM do mod.

**O teste:** um host falso implementa a `TLModHostApi`, e um script chama tudo. Passou, confirmado no
binário:

- argumento de tipo errado vira erro do Lua, com a posição do argumento;
- enum inválido também;
- `content_name` com nome de 282 bytes volta inteiro;
- `content_ids` volta como tabela;
- a receita chega com 2 ingredientes e 2 estações;
- o `on_ai` do NPC muda `vx`, `ai`, `local` e `frame` na struct;
- o comando e o provedor devolvem JSON pelo `buffer_append`;
- o `player_hurt` reduz o dano pela metade;
- um erro no gancho de frame vai para o log com arquivo, linha e pilha, e o jogo segue.

**A lição do QuickJS:** é na ligação escrita à mão que a memória se corrompe. Gerada uma vez e testada, a
correção vale para todas as funções.

### 6.3 Um gerador, vários motores

- **Lua 5.4, 5.5 e Luau:** a mesma API C, com diferenças pequenas. O Luau pede o nome em
  `lua_pushcfunction` e usa `lua_ref`.
- **QuickJS:** listas de funções e de getters.
- **wasm:** a mesma tabela vira imports.
  - As structs com ponteiro mudam de layout no wasm32 (`const char*` tem 4 bytes), e o host traduz cada
    ponteiro com checagem de faixa.
  - O `TLModNpcState` não tem ponteiros e tem o mesmo layout: o teste copiou a struct direto para a memória
    linear.
- **Structs quentes** (`TLModNpcState`): o gerador pode emitir também a visão, com os deslocamentos tirados
  do `offsetof`, e o proxy. O gerador descartável passa a struct como tabela copiada; a visão e o proxy do
  benchmark foram escritos à mão.
- **Onde fica:** em `tools/`, rodando no build. Uma mudança no `tlmod_api.h` regenera as ligações, e um
  teste como o da 6.2 roda no CI.

### 6.4 Eventos no script

Como um mod de NPC fica em Luau, com os nomes que o gerador produz:

```lua
-- mods/acme.rubi/main.luau
local slime = tlmod.register_npc{
  name = "slime_rubi", display_name = "Slime de Rubi", shell_type = 1, life_max = 120,
  on_ai = function(npc)
    npc.vx = 2.5
    npc.ai[1] += 1
    return true          -- o mod assume o tick; nil deixaria a IA da casca rodar
  end,
}

tlmod.subscribe_player_hurt(function(ev, after)
  if not after and ev.damage > 50 then ev.damage = 50 end
end)

tlmod.subscribe_frame(function()
  -- uma vez por frame, na thread do jogo
end)
```

Um prelúdio em Luau pode dar nomes mais curtos (`tlmod.on("frame", fn)`) sem mudar o gerador.

### 6.5 Dados salvos

- **Hoje não existe API de dados para mods.** O que o runtime salva à parte são os itens de mod, num
  arquivo paralelo ao do jogo (`<arquivo do jogo>.tlmod.json`, `item_persistence.h`).
- **Proposta:** `data_get` e `data_set` na `TLModHostApi`, com um valor JSON por chave e três escopos (mundo,
  personagem e global). O valor fica guardado pelo host e vai para o mesmo arquivo paralelo, numa seção
  `mod_data` por id de mod.
- **Nenhum script roda durante o save.** O save pode rodar fora da P (threads S e R, auditoria 01). O host
  guarda os valores já em JSON e os grava sem chamar a VM.
- **No script:** `tlmod.data.world.boss_vencido = true`, por um proxy que converte na escrita.
- **Teto por mod:** por exemplo, 256 KiB por arquivo.

### 6.6 Telas

- **O painel já é declarativo.** O provedor devolve JSON com seções, linhas e botões, e os toques voltam
  como comandos (`modapi/README.md:482–520`).
- **No script, o provedor devolve uma tabela.** O host a converte em JSON, e o runtime já valida o JSON antes
  de mostrar (`mod_loader.cpp:946–951`).
- **Atualização:** o painel pede o conteúdo a cada 0,5 s com o modal aberto (`overlay/README.md:111–117`).

```lua
tlmod.register_provider("panel", function()
  return {
    title = "Rubi", icon = "gem",
    sections = {{ title = "Slimes", note = "Chama um slime perto do jogador",
                  rows = {{ buttons = {{ label = "Chamar um", command = "spawn" }} }} }},
  }
end)
tlmod.register_command("panel", function(cmd)
  if cmd == "spawn" then tlmod.spawn_npc_near_player(slime, 0, -40) end
  return { toast = "Slime chamado" }
end)
```

### 6.7 Threads

- **Onde cada coisa roda:** `on_ai`, frame e eventos na P; `init` na L; provedor e comando na U
  (`tlmod_api.h:226–237`).
- **Uma VM só aceita uma thread por vez,** e o trampolim gerado trava a VM do mod. A U espera no máximo um
  gancho do mod, e o painel pede 2 vezes por segundo (inferido; o [E4](#e4) mede a contenção).
- **Melhor ainda: o host enfileira** o que mexe no jogo e atende no frame, como o README já pede aos mods
  nativos (`modapi/README.md:518–519`). Para scripts, o host faz isso sozinho, e o comando responde no frame
  seguinte.

---

## 7. Fluxo no celular (pergunta 6)

### 7.1 Onde ficam

```
tlmod/                            Android/data/<pacote>/files/tlmod (android_main.cpp:169–176)
├── tlmod.log
├── mods/
│   └── acme.rubi/
│       ├── manifest.json         "script": "main.luau" no lugar de "entry", e "permissions"
│       ├── main.luau
│       ├── ia/*.luau             require relativo à pasta do mod
│       └── textures/*.png
└── data/
    └── acme.rubi/                única pasta onde o script grava (tlmod.fs)
```

O manifest continua o mesmo, com `content` declarado: a Oficina mostra o que o mod adiciona sem rodar o
script, como já faz com os nativos.

### 7.2 Editar

- **Onde está o problema.** A pasta fica em `Android/data/<pacote>/files/tlmod`. Desde o Android 11, outros
  apps não leem nem gravam ali (inferido). E o jogo não pede permissão de armazenamento: o manifest só tem
  `INTERNET` e a de licença, com `targetSdkVersion` 35 (auditoria 09).
- **Caminhos:**
  1. **Pasta de desenvolvimento.** O usuário escolhe uma pasta em Documentos pelo seletor do sistema
     (`ACTION_OPEN_DOCUMENT_TREE`, com permissão persistente). Qualquer editor grava lá (Acode, Markor,
     Termux), e o TLMod copia o que mudou para `mods/<id>/`.
  2. **Do PC:** `adb push` para a pasta do jogo (inferido que funcione na maioria dos aparelhos).
  3. **Editor simples no painel,** com o `EditText` que o painel já usa no campo de busca, para ajustes
     rápidos.
- **Recomendado:** o caminho 1 como fluxo principal, o 2 para quem tem PC, o 3 depois.

### 7.3 Recarregar

- **A unidade é o mod:** uma VM por mod.
- **A sequência, na P, entre dois frames:**
  1. desfazer o que o mod assinou: frame, eventos, comandos, provedores e callbacks de NPC;
  2. fechar a VM (`lua_close`);
  3. criar a VM nova e rodar o `init`.
- **O conteúdo não recarrega.** Itens, NPCs e receitas recebem IDs na carga, e o jogo já os usa. Se o
  conteúdo mudar, a Oficina pede para reiniciar.
- **O estado:** o que estiver em `tlmod.data` sobrevive; o resto recomeça. Um `on_reload(antigo)` opcional
  passa estado entre as VMs.
- **O disparo:**
  - um vigia confere a data dos arquivos a cada 1 s (barato para poucos arquivos);
  - o botão "Recarregar" na Oficina;
  - o comando `reload <mod>` nas Ferramentas.
- **O que falta no runtime.** Hoje ele não desfaz nada:
  - o `FrameSub` não guarda o mod (`mod_loader.cpp:73–76`);
  - as bibliotecas nunca fecham (`:59`);
  - ligar ou desligar só vale na próxima abertura (`README.md:459`).

  Para scripts, fechar a VM não tem problema; falta só o registro por dono.

### 7.4 Erros

- **Toda chamada ao script passa por `pcall` com traceback.** Arquivo, linha e pilha vão para o `tlmod.log`
  com o id do mod, como no teste do gerador: "teste.lua:33: bug proposital no frame 4", seguido da pilha.
- **O que o painel já tem:**
  - o Log ("M"), com as últimas 500 linhas e os erros em vermelho (`modapi/README.md:643–650`,
    `overlay/README.md:118–120`);
  - a Oficina, com o erro inteiro de um mod que falhou na carga (`README.md:450–451`).
- **O que falta:**
  - um estado "erro de script", com contador e o último erro (arquivo e linha);
  - um aviso no primeiro erro.
- **Disjuntor:** alguns erros seguidos no mesmo gancho desligam o gancho até recarregar. Erros iguais viram
  uma linha só no log, com a contagem ("repetiu 120 vezes").
- **Orçamento por chamada:** um gancho que passa do tempo (por exemplo, 5 ms) é interrompido e conta como
  erro.

---

## 8. Convivência com os mods nativos (pergunta 7)

- **Convivem, sim.** O `ModLoader` já carrega mods sem biblioteca (`AddBuiltin`, `mod_loader.cpp:998`, e
  `rec.builtinInit`, `:1144`). O host de scripts pode ser um mod embutido, e cada mod de script um
  `TLModContext` como os outros. Isso garante:
  - a mesma ordem de carga e as mesmas dependências do manifest, nos dois sentidos;
  - o mesmo registro de conteúdo;
  - os eventos na mesma ordem para todos (`tlmod_api.h:19`).
- **Hoje já se encontram** pelas chaves de conteúdo (`find_content("mod:nome")`) e pelos eventos do jogo.
- **Para um chamar o outro, falta API:**
  - **eventos de mod:** `emit(nome, json)` e `subscribe(nome, fn)`, iguais para nativo e script. É o mais
    simples e o mais seguro;
  - **serviços:** um mod exporta funções com assinatura declarada, e o host faz a ponte com as regras do
    gerador.
  - Nunca passam ponteiros de um lado para o outro.
- **Ordem:** eventos primeiro; serviços quando aparecer o uso.

---

## 9. O TLMod hoje

| item | estado | evidência |
|---|---|---|
| API em C puro, versionada por `struct_size` | pronta para gerar ligações | `tlmod_api.h:32–34`, `:265–362` |
| Mods embutidos, sem `.so` | existem | `mod_loader.cpp:998`, `:1144` |
| Carga dos mods | thread L, antes do primeiro frame | `android_main.cpp:190–223` |
| Threads dos callbacks | P (frame, `on_ai`, eventos) e U (provedor, comando) | `tlmod_api.h:165`, `:226–242` |
| Serialização | um mutex por provedor e por comando, não por mod | `mod_loader.cpp:913`, `:942` |
| Desfazer o que um mod assinou | não existe | `FrameSub` sem dono (`mod_loader.cpp:73–76`); bibliotecas nunca fechadas (`:59`) |
| Dados salvos para mods | só itens de mod, no arquivo paralelo | `item_persistence.h` |
| Log em arquivo e no painel | existe | `modapi/README.md:643–650` |
| Erro de carga na Oficina | existe | `modapi/README.md:450–451` |
| Isolamento de mods | nenhum: todo mod é nativo | auditoria 09, 6.6 |
| Pasta de mods | `Android/data/<pacote>/files/tlmod/mods` | `android_main.cpp:169–176` |
| Camada de script | não existe | nenhuma referência a Lua, JavaScript ou wasm em `modapi/` e `il2cpp_bridge/` |

---

## 10. Recomendação

**Luau, interpretado, uma VM por mod.** Pelos números e pelos testes:

1. **Isolamento de fábrica.** Somente leitura, sem bytecode do script e interrupção a 2–5%. No Lua 5.4 a
   mesma coisa se monta à mão, e o limite de CPU custa 41% ou exige um vigia. No LuaJIT, exige um build
   especial.
2. **O mais rápido sem JIT.** Com a visão `buffer`, 0,23 µs por NPC; os outros interpretadores de script
   ficam em 0,49–0,94 µs.
3. **Custo aceitável:** 821 KiB na `.so` e 200 KiB por VM. O Lua 5.4 economiza uns 600 KiB na `.so` e uns
   115 KiB por VM.
4. **Para quem escreve mods:** sintaxe do Lua, tipos opcionais e o motor de um ecossistema grande de
   criadores.

**Com o Luau:**

- interpretador por padrão; o codegen fica para depois do [E2](#e2);
- API gerada do `tlmod_api.h`, com a visão `buffer` nos ganchos quentes;
- limites de memória, CPU e pilha em toda VM;
- host enfileirando o que chega pela U.

**Plano B: Lua 5.4.** Vale se o tamanho da `.so` ou a memória de muitos mods pesarem mais. O isolamento
segue a [seção 5](#5-segurança-pergunta-4), com vigia em vez de gancho armado.

**Depois: mods compilados em WebAssembly (WAMR).** Quem hoje escreve mod nativo ganha um caminho isolado e
rápido:

- AOT feito no PC;
- sem a checagem de limites por hardware;
- a mesma API pela tabela de imports.

**Não fazer:**

- dar a FFI do LuaJIT a mods;
- deixar o `luaL_openlibs` do Lua padrão;
- carregar bytecode vindo de mods;
- escrever ligações à mão;
- ligar ganchos de script em todas as entidades do jogo.

---

## 11. Plano de protótipo

**P0. Host de scripts como mod embutido** (3–5 dias).

- Luau estático no runtime, com uma VM por mod de script e `"script"` no manifest.
- Gerador em `tools/` produzindo as ligações Luau da `TLModHostApi`; trampolins com trava, `pcall` e
  traceback.
- Isolamento:
  - `luaL_sandbox` e uma thread por script;
  - alocador com teto de 16 MiB;
  - interrupção com orçamento por chamada (5 ms) e por frame.
- **Pronto quando:** o `example_boss` reescrito em Luau se comporta como o nativo, e um erro proposital
  aparece no log com arquivo e linha.

**P1. IA e medição** (2–3 dias).

- Visão `buffer` do `TLModNpcState`, com o proxy como opção mais legível.
- Comando `script bench` nas Ferramentas: roda no aparelho o benchmark deste relatório e grava no
  `tlmod.log`.
- **Pronto quando:** o [E1](#e1) confirma ou corrige o fator 3.

**P2. Recarregar e erros** (3–4 dias).

- Registro por dono no `ModLoader`: frame, eventos, comandos, provedores e callbacks de NPC.
- Recarga na P, entre frames.
- Vigia de arquivos, botão na Oficina e comando `reload`.
- Contador de erros, disjuntor e agrupamento no log.
- **Pronto quando:** o [E5](#e5) passa.

**P3. Dados e arquivos** (2–4 dias).

- `data_get` e `data_set` na API C, com a seção `mod_data` no arquivo paralelo.
- `tlmod.fs` preso a `tlmod/data/<id>/`, com cota.

**P4. Edição no celular** (2–3 dias).

- Pasta de desenvolvimento escolhida pelo seletor do sistema, copiada para `mods/<id>/` quando muda.
- **Pronto quando:** o [E6](#e6) passa.

**Depois:**

- eventos entre mods;
- permissões no manifest, mostradas na instalação;
- o codegen do Luau, opcional por função;
- a trilha wasm, com WAMR AOT.

---

## 12. Experimentos no aparelho

Todos usam as Ferramentas do TLMod e o `tlmod.log`.

### E1: benchmark no aparelho

<a id="e1"></a>

- **Pergunta:** o fator 3 entre o host e o celular vale ([1.4](#14-do-host-para-o-celular))?
- **Como:** o comando `script bench` (P1) roda a IA de 200 NPCs, nas formas `buffer` e proxy, por 3.000
  frames fora do jogo. Grava no log a mediana, o p99 e o máximo. Rodar no aparelho de teste e num
  intermediário.
- **Esperado:** de 100 a 140 µs por frame com `buffer` e de 200 a 300 com proxy. Fora disso, refazer a
  [4.3](#43-cabe-nos-60-fps) com o fator medido.

### E2: código nativo no aparelho

<a id="e2"></a>

- **Pergunta:** o gerador de código nativo do Luau funciona no processo do jogo, e quanto ganha?
- **Como:** o `script bench` com `luau_codegen_supported()`, `luau_codegen_create` e `luau_codegen_compile`.
  Gravar no log se compilou e o tempo.
- **Esperado:** compila (o processo já cria memória executável para a ShadowHook) e ganha uns 2 vezes no
  `buffer`.
- **Se o LuaJIT voltar à mesa:** o mesmo teste com `jit.status()` e um laço quente, para ver se o código
  gerado consegue memória perto do interpretador.

### E3: memória por VM

<a id="e3"></a>

- **Como:** o comando `script mem` cria 10 VMs com o script de teste e grava o `VmRSS` de
  `/proc/self/status` antes e depois.
- **Esperado:** uns 200 KiB por VM de Luau.

### E4: threads

<a id="e4"></a>

- **Pergunta:** o painel trava o jogo, ou o contrário?
- **Como:** um mod de script com `on_ai` em 200 NPCs e um provedor que monta um JSON grande. Abrir o modal e
  tocar sem parar por 1 minuto, com o log registrando quanto cada lado esperou pela trava.
- **Esperado:** espera abaixo de 1 ms; nenhum frame perdido por causa da trava.

### E5: recarga

<a id="e5"></a>

- **Como:** editar o `main.luau`, salvar e ver o efeito em até 1 s. Repetir 50 vezes, registrando o
  `VmRSS`.
- **Esperado:** efeito visível a cada vez; RSS estável, sem crescer a cada recarga.

### E6: edição pela pasta de desenvolvimento

<a id="e6"></a>

- **Como:** num Android 13 ou mais novo, escolher uma pasta em Documentos pelo seletor, editar o script num
  editor qualquer e conferir no log a cópia e a recarga.
- **Esperado:** funciona sem permissão de armazenamento.

---

## 13. Respostas diretas

1. **Motores.** Os números estão na [2.1](#21-comparação).
   - **Lua 5.4/5.5:** pequenos (221–236 KiB; 84–86 KiB por VM), MIT e maduros, mas com isolamento à mão;
     0,49–0,51 µs por NPC.
   - **LuaJIT:** 556 KiB, MIT; o JIT não ajuda nos ganchos (0,38 µs com e sem) e atrapalha o limite de CPU.
     Viável no Android arm64, mas não recomendado.
   - **Luau:** 821 KiB, MIT, isolamento de fábrica; 0,23 µs com `buffer`.
   - **QuickJS:** 872–945 KiB, MIT, o mais lento (0,52–0,94 µs) e o mais pesado por VM (338–356 KiB). A
     linguagem é a mais conhecida.
   - **wasm3:** 227 KiB, MIT, 0,13 µs, em manutenção mínima.
   - **WAMR:** 127–142 KiB, Apache 2.0, 0,11–0,22 µs; o mod é compilado no PC.
2. **JIT.** É permitido: a ShadowHook do TLMod já cria memória executável, e os ganchos funcionam no
   aparelho. Ficar no interpretador custa até 2 vezes nos ganchos de mod (nada com proxy) e 2–2,5 vezes em
   cálculo puro. Recomendação: interpretador.
3. **Desempenho.** A IA de 200 NPCs custa de 45 a 102 µs por frame no host com Luau ou Lua, ou 136–306 µs no
   celular (0,8–1,8% do frame). Cabe nos 60 FPS com folga; em 1 ms cabem 650–1.500 ganchos de IA. Ligar
   script em todas as entidades do jogo não cabe.
4. **Segurança.**
   - **O que o script alcança:** na configuração isolada, nem arquivos, nem rede, nem memória fora da VM;
     memória, CPU e pilha com limite.
   - **Arquivos:** só por `tlmod.fs`, presa a `tlmod/data/<id>/`, com `openat` sem seguir links e cota.
   - **O que sobra de risco:** defeitos no motor ou nas ligações. Por isso as ligações são geradas e
     testadas.
5. **API.** Um gerador lê o `tlmod_api.h` pela AST do clang e produz as 25 ligações, com 4 anotações à mão;
   o código gerado compilou e passou no teste.
   - **Eventos:** funções do script passadas a `subscribe_*`.
   - **Dados:** `data_get` e `data_set` novos, gravados pelo host no arquivo paralelo.
   - **Telas:** o JSON do painel, devolvido como tabela.
6. **Fluxo.**
   - **Onde:** `tlmod/mods/<id>/main.luau`, e os dados em `tlmod/data/<id>/`.
   - **Edição:** por uma pasta em Documentos escolhida pelo seletor do sistema, já que outros apps não
     alcançam a pasta do jogo.
   - **Recarga:** troca a VM do mod entre frames; o runtime precisa de registro por dono.
   - **Erros:** traceback no `tlmod.log`, mais o estado e o último erro na Oficina.
7. **Convivência.** Sim: o host de scripts é um mod embutido no mesmo `ModLoader`, com a mesma ordem,
   conteúdo e eventos. Para se chamarem, eventos de mod primeiro e serviços depois, sem ponteiros.

---

## 14. Lacunas do TLMod, em ordem de risco

1. <a id="lacuna-1"></a>**Nenhum mod é isolado** (alto). Todo mod é uma `.so` com as permissões do jogo
   (auditoria 09). Sem camada de script, não há onde impor limites. Ações 1 e 2.
2. <a id="lacuna-2"></a>**Não há como desfazer o que um mod registrou** (alto para scripts). O `FrameSub`
   não guarda o dono, nada é removido depois do `init`, e ligar ou desligar só vale na próxima abertura.
   Sem isso não há recarga. Ação 4.
3. <a id="lacuna-3"></a>**Callbacks em duas threads sem serialização por mod** (médio). Frame e `on_ai` na P,
   provedor e comando na U, cada um com o próprio mutex. Uma VM de script precisa de uma trava por mod ou de
   uma fila. Ação 2.
4. <a id="lacuna-4"></a>**Sem API de dados salvos para mods** (médio). Só os itens de mod vão para o arquivo
   paralelo, e um mod de script não tem onde guardar estado. Ação 5.
5. <a id="lacuna-5"></a>**A pasta de mods é inacessível para editores no Android 11 e mais novos** (médio).
   Ação 6.
6. <a id="lacuna-6"></a>**Sem permissões declaradas no manifest** (médio-baixo). Ação 7.
7. <a id="lacuna-7"></a>**O conteúdo só se registra no `init`** (baixo). A recarga não muda itens nem NPCs;
   basta documentar e avisar na Oficina.
8. <a id="lacuna-8"></a>**O campo `local` do `TLModNpcState` é palavra reservada em Lua** (baixo). O gerador
   renomeia para `local_`; a documentação da API para scripts precisa dizer isso.

---

## 15. Ações recomendadas

1. **Escolher Luau** e incluí-lo no runtime. Plano B: Lua 5.4 ([seção 10](#10-recomendação)).
   [Lacuna 1](#lacuna-1).
2. **P0, o host de scripts** (3–5 dias):
   - mod embutido via `AddBuiltin`;
   - uma VM por mod;
   - isolamento da [5.6](#56-só-a-api-do-tlmod);
   - trava por mod e fila para o que chega pela U.

   [Lacunas 1](#lacuna-1) e [3](#lacuna-3).
3. **Gerador de ligações em `tools/`,** a partir da AST do `tlmod_api.h`, com teste no CI. As únicas
   anotações são os retornos padrão dos callbacks ([6.2](#62-o-gerador)).
4. **Registro por dono no `ModLoader`** (`FrameSub` e os outros com o `TLModContext`) e recarga na P (P2).
   [Lacuna 2](#lacuna-2).
5. **`data_get` e `data_set`** no arquivo paralelo, e `tlmod.fs` com cota (P3). [Lacuna 4](#lacuna-4).
6. **Pasta de desenvolvimento** pelo seletor do sistema (P4). [Lacuna 5](#lacuna-5).
7. **Permissões no manifest,** conferidas pelas ligações e mostradas na instalação (auditoria 09, 6.6).
   [Lacuna 6](#lacuna-6).
8. **Rodar o [E1](#e1) e o [E2](#e2)** antes de decidir sobre o codegen do Luau.
9. **Trilha wasm (WAMR AOT)** para mods compilados, depois das ações 1 a 7, com a checagem de limites por
   hardware desligada.
