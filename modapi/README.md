# Modrix Mod API

API de mods do Modrix, independente de plataforma, montada sobre a ponte IL2CPP
(`../il2cpp_bridge`). O autor do mod só vê `include/modrix/modrix_api.h` (C puro)
e, se quiser, `include/modrix/modrix.hpp` (açúcar C++ só de header). Nenhum tipo do
jogo ou do IL2CPP atravessa a fronteira: players são índices, conteúdo é chave + ID.

```
mod (C/C++) ──ModrixHostApi (C ABI)──▶ runtime ──GameAdapter──▶ BridgeGameAdapter ──▶ il2cpp_bridge ──▶ jogo
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

A biblioteca exporta `int modrix_mod_init(const ModrixHostApi*, ModrixContext*)`.
A macro `MODRIX_MOD(fn)` gera isso a partir de `bool fn(modrix::Host&)`.

### manifest.json

| campo | obrigatório | descrição |
|---|---|---|
| `modrix_api` | sim | versão da ABI (hoje `1`) |
| `id` | sim | `[a-z0-9]` separados por `.` `_` `-`, até 64 (ex.: `modrix.example_item`) |
| `name` | sim | nome exibido |
| `version` | sim | semver `MAJOR.MINOR.PATCH[-pre]` |
| `authors`, `description` | não | metadados |
| `dependencies` | não | `{"id": "faixa"}`; faixas `*`, `1.2.3`, `^1.2`, `~1.2.3`, `>=1 <2` |
| `optional_dependencies` | não | idem; ausente é ok, presente precisa casar e carrega antes |
| `content` | não | o que o mod adiciona: `items`, `npcs`, `projectiles`, `tiles`, `walls`, `buffs` → listas de nomes `[a-z0-9_]` |
| `entry` | sim | plataforma → caminho relativo da biblioteca (`android-arm64`, `linux-x86_64`, `windows-x86_64`, `macos-arm64`…) |

`content` é um contrato. Registrar algo que não foi declarado é recusado, e algo
declarado que não foi registrado gera aviso. Assim a interface do Modrix mostra o
que um mod adiciona sem executar o código dele.

### Ciclo de carga

1. Lê `mods/*/manifest.json`. Manifests inválidos e ids duplicados são rejeitados.
2. Resolve dependências: ausentes, versões incompatíveis, ciclos e `modrix_api` diferente
   geram rejeição, que se propaga para quem depende.
3. Ordena topologicamente, com empates resolvidos pelo id. A ordem é determinística, e por isso
   os IDs também são.
4. Carrega a biblioteca da plataforma e chama `modrix_mod_init`. Se ela retornar algo diferente de 0, o
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

## Build

```sh
# host: runtime + adaptador (contra a ponte de host) + mod de exemplo + testes
cmake -S . -B build && cmake --build build && (cd build && ctest --output-on-failure)

# Android arm64: libmodrix_runtime.so (+ libmodrix_bridge.so) e o mod
cmake -S . -B build-android -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
cmake --build build-android
```

Os mods compilados saem em `<build>/dist/mods/<mod>/`, já no formato de pasta.
Para compilar um mod fora deste repositório: `cmake -S mods/example_item -B b -DMODRIX_SDK_INCLUDE_DIR=…/modapi/include`.

No Android, o runtime procura mods em `$MODRIX_MODS_DIR`; se não houver, em
`/storage/emulated/0/Android/data/<pacote>/files/modrix/mods`; e por último em `/data/data/<pacote>/files/modrix/mods`.

## Ainda não implementado

- **Fazer o jogo usar o conteúdo registrado.** Hoje o registro atribui IDs e guarda as
  definições. Falta hookar `Item.SetDefaults`, carregar texturas e tratar nome e tooltip.
- **Persistência.** Mundos e players precisam salvar itens de mod pela chave, e não pelo ID.
- **Multiplayer.** O servidor precisa enviar a tabela chave → ID aos clientes.
- **Outros tipos.** NPC, projectile, tile, wall e buff já têm faixa de ID, mas ainda não têm
  `register_*` na API.
