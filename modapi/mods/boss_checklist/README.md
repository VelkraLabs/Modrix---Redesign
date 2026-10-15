# Boss Checklist

Mod que lê quais bosses, minibosses e eventos já foram derrotados no mundo atual e expõe a lista
para a camada de interface. Ele não desenha nada; a UI consome o canal de dados.

## Como a UI consome

O mod declara `"provides": ["checklist"]` no manifest. O runtime expõe esse canal:

```cpp
std::optional<std::string> json = loader.Query("modrix.boss_checklist", "checklist", &error);
```

- O runtime valida o JSON antes de devolver. Um provedor com bug nunca entrega JSON quebrado.
- Pode ser chamado de **qualquer thread**, em geral a da UI. Chamadas concorrentes são serializadas.
- É sob demanda (pull): cada chamada lê o estado atual. Não há cache nem polling dentro do mod.

## Esquema (`schema: 1`)

| campo | tipo | significado |
|---|---|---|
| `in_world` | bool \| null | há um mundo carregado; `null` = o jogo ainda não rodou um frame |
| `hardmode` | bool \| null | Wall of Flesh derrotado (`Main.hardMode`) |
| `summary.bosses` / `.minibosses` / `.events` | `{defeated, pending, unknown, total}` | contagens por categoria |
| `next` | `{key, name, order}` \| null | próximo **boss** pendente na ordem de progressão |
| `entries[]` | `{key, name, category, order, hardmode, status}` | um item por boss/miniboss/evento, já ordenado |

- **`status`:** `"defeated"`, `"pending"` ou `"unknown"`. Fora de um mundo, tudo é `unknown`, porque as flags
  estáticas ainda guardam o último mundo jogado e não podem ser mostradas.
- **`key`:** estável, para localizar o nome. `name` é o nome canônico do jogo, em inglês.
- **Exemplo:** `sample_checklist.json` é a saída real do teste de host no cenário hardmode (Deerclops como
  `unknown`).

## De onde vem cada estado

| entrada | campo do jogo (dump) |
|---|---|
| King Slime | `NPC.downedSlimeKing` (static+0x92) |
| Eye of Cthulhu | `NPC.downedBoss1` (0x8E) |
| Eater of Worlds / Brain of Cthulhu | `NPC.downedBoss2` (0x8F), um campo só para os dois |
| Queen Bee / Skeletron / Deerclops | `downedQueenBee` 0x91 / `downedBoss3` 0x90 / `downedDeerclops` 0xA8 |
| Wall of Flesh | `Main.hardMode` (0x360); não existe `downedWallOfFlesh` |
| Queen Slime | `downedQueenSlime` 0xA7 |
| Destroyer / Twins / Skeletron Prime | `downedMechBoss1/2/3` 0xCA/0xCB/0xCC (atribuição segue o PC 1.4.4) |
| Plantera, Golem, Duke Fishron, Empress, Cultist, Moon Lord | `downedPlantBoss` 0x97, `downedGolemBoss` 0x98, `downedFishron` 0x9A, `downedEmpressOfLight` 0xA6, `downedAncientCultist` 0xA0, `downedMoonlord` 0xA1 |
| eventos e minibosses | `downedGoblins`, `downedPirates`, `downedFrost`, `downedMartians`, `downedHalloweenTree/King`, `downedChristmasTree/Santank/IceQueen`, `downedTower*` |

`in_world` vem de `LocalUserGameState.Instance.gameMenu`. `Instance` é `[ThreadStatic]`: fora da thread do jogo,
o getter `Main.get_gameMenu` lança `NullReferenceException`. Por isso a ponte fotografa todo esse estado
a cada frame, num hook em `Main.DoUpdate` (`0xCADDB8`), e a UI lê a fotografia.
