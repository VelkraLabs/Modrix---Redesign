# Entrega: pacote de teste do TLMod (3ª versão)

Branch só para download, sem o código. Pode ser apagada depois.

| arquivo | SHA-256 |
|---|---|
| `tlmod-teste.zip` | `e6792d20b65e0fb46a3445572140553cdc4124c68cac95f9611a4e6dc6173d14` |
| `libtlmod_bridge.so` | `3b926f4793a7d0eeba5ab7ee3bba3b4055a7324cee3eacc2cc9256df6483063f` |
| `libtlmod_runtime.so` | `8b1a4fbbf4e5b29c1c893df54f55c49af5d37d5eb9252820094ee72519e45470` |
| `README_TESTE.md` | `dac8b4f2f64aa58aa9bed8bdcd4fcf4021568f41789a47763ff6d5f2804c7620` |

- `tlmod-teste.zip`: o pacote completo (as `.so`, a pasta `tlmod/`, o README_TESTE.md e o SHA256SUMS.txt).
- `libtlmod_bridge.so`: a única que mudou desde a 2ª versão (correção do `shadowhook_init`). Basta
  trocar essa no aparelho.
- `libtlmod_runtime.so`: igual à da 2ª versão; fica aqui por conveniência.

Gerado a partir do commit `b071b1d` da branch `ccr-bbd92bf9-i01buk`.
