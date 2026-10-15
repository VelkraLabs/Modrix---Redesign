# tools/

## offset_diff.py

Compara o que o TLMod tem registrado (offsets, RVAs, constantes e nomes) com o dump de um APK novo
do Terraria Mobile, e diz o que precisa ser reconferido. Só usa a biblioteca padrão (Python 3.8+).

```sh
# a cada atualização do jogo: rodar o Il2CppDumper no APK novo e depois
python3 tools/offset_diff.py --dump novo/dump.cs --script novo/script.json
```

| fonte | o que dá para conferir |
|---|---|
| `dump.cs` | campos (offset, tipo, estático ou não), constantes, métodos (RVA e nº de parâmetros) |
| `script.json` | só métodos; campos aparecem como "não verificáveis" |

**O que é conferido:**

| origem no TLMod | regra |
|---|---|
| `terraria_offsets.h` `player_off`, `npc_off`, `main_static_off`, `local_user_off` | offset do campo (com herança) e tipo, comparado com a 1ª palavra do comentário |
| `terraria_offsets.h` `rva::Classe_Metodo` | RVA; desambigua overloads com `argc::` |
| `terraria_offsets.h` `argc::` | número de parâmetros (mudou = o typedef do hook precisa mudar) |
| `terraria_offsets.h` `kMaxNPCs`, `kNpcVanillaCount`, `k*Class`, `kAssembly` | const do jogo, existência da classe e da imagem |
| `bridge_adapter.cpp` (flags do Boss Checklist) | `static bool` resolvido por nome em runtime |
| `content_registry.cpp` (`DefaultRanges`) | `ItemID.Count` etc.; alerta se passar do 1º ID de mod |

As regras de mapeamento ficam no topo do script (`FIELD_GROUPS`, `METHOD_CLASSES`, `CONST_CHECKS`...).
Ao criar um namespace novo em `terraria_offsets.h`, acrescente a regra lá.

**Saída:**
- **Relatório:** por seção, com `MUDOU`/`SUMIU`/`AVISO` e o `arquivo:linha` a corrigir. `--all` lista também o que bateu.
- **`--json`:** saída para CI.
- **Código de saída:** `0` nada mudou, `1` há diferenças, `2` erro.

**Testes:** `python3 tools/test_offset_diff.py -v`. Usam um dump sintético; o dump real não fica no repositório.

## rename_prefix.py

Troca o nome provisório do projeto (hoje `TLMod`) em um comando: conteúdo dos arquivos, nomes de
arquivos e pastas (`git mv`), símbolos C/C++, macros, funções exportadas, pacote Java e
assinaturas JNI, variáveis do CMake, campo `tlmod_api` dos manifests, ids dos mods, logs e READMEs.

```sh
python3 tools/rename_prefix.py TLMod NovoNome --dry-run   # mostra o plano (arquivos, contagem, movimentos)
python3 tools/rename_prefix.py TLMod NovoNome             # aplica e confere que não sobrou nada
```

- **Grafias.** O nome é dado na grafia de tipo; `TLMod`→`NovoNome`, `tlmod`→`novonome` e
  `TLMOD`→`NOVONOME` são derivados. Qualquer outra mistura de maiúsculas e minúsculas faz o script
  parar sem mudar nada.
- **Escopo.** Só `PROJECT_PATHS` (`il2cpp_bridge`, `modapi`, `tools`, `player_fields.md`). O resto
  do repositório é de outro projeto e não é tocado.
- **Recusa** (sem mudar nada) se o nome novo já aparece no escopo, se um destino já existe ou se um
  binário contém o prefixo.
- **Depois.** Recompile em pastas de build novas: os caminhos de include e as variáveis do CMake
  (`TLMOD_ANDROID_SDK`, ...) mudam junto. Os ids dos mods mudam, então os IDs numéricos de conteúdo
  podem mudar de ordem (o loader desempata a ordem de carga pelo id).

**Testes:** `python3 tools/test_rename_prefix.py -v` (repositório git temporário).
