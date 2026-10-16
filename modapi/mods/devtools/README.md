# Ferramentas de teste (`tlmod.devtools`)

Mod para testar outros mods no aparelho, no estilo dos menus de trapaça: põe a aba **Ferramentas** no
painel do TLMod. Instale só para testar; ele liga e desliga pelo painel como qualquer mod.

| seção | o que faz |
|---|---|
| Hora do dia | Amanhecer (4:30), Meio-dia, Anoitecer (19:30), Meia-noite pelo `Main.SkipToTime` (o mesmo do poder de hora da Jornada); **Congelar hora** segura a hora a cada frame |
| Jogador | **Curar** (vida e mana cheias), **Invencível** (cancela todo golpe no jogador local), **Matar inimigos** (hostis a até ~150 blocos, com loot; pula NPCs da cidade, amigáveis, bichinhos e partes invulneráveis) |
| Invocar NPC ou boss | busca por nome (no idioma do jogo, sem acento ou o nome interno em inglês) ou ID; sem busca, lista os NPCs de mod e botões para 16 bosses. Nasce 30 blocos à direita e 15 acima do jogador |
| Dar item | busca igual; sem busca, lista os itens de mod. **+1 / +10 / +100** vão para o inventário |

- Nada do mundo é alterado além do que você pede, mas matar um boss conta como derrotado e
  congelar/mudar a hora mexe nos eventos. Use um mundo de teste.
- Invocar respeita os 2 s de carência ao entrar num mundo (como o `spawn_on_start`).
- A busca mostra até 30 resultados: exatos primeiro, depois os que começam com o texto, depois os que contêm.
- `sample_panel.json` é um exemplo do JSON da aba, usado pela prévia (`overlay_snapshot`).

Comandos (texto, mandados pelo painel): `time dawn|noon|dusk|midnight`, `freeze`, `heal`, `god`, `kill`,
`give <id> <n>`, `spawn <id> [<id>...]`, `search item|npc <texto>`.
