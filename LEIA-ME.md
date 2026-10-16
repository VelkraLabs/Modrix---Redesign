# Entrega: pacote de teste do TLMod (6ª versão)

Branch só para download, sem o código. Pode ser apagada depois.

| arquivo | SHA-256 |
|---|---|
| `tlmod-teste.zip` | `efb5117b5e66a4d34cb43fcf59ea22f4f6821642e05b40110973bf84366b9a0c` |
| `libtlmod_bridge.so` | `a27e45f5f0bdccfdb4cb378e9f3be4699a5623878b07ec32f00f29ae96eb4396` |
| `libtlmod_runtime.so` | `a3c3b686b34b76d5ba8bd7660bffed4b98b5114a555f141472a47819d9c443f5` |
| `libdevtools.so` | `931a7f67bec83deb9fa80e256b0ac61fca0194e7e95c75bce815f34d5c9ec8a8` |
| `README_TESTE.md` | `d09051cbb6682381e792a08555b70822f31192c9b8369bfe7cd666bc9792f512` |

- `tlmod-teste.zip`: o pacote completo.
- Soltas, as 3 `.so` que mudaram desde a 5ª versão (o que não couber no inventário cai no chão):
  - `libtlmod_bridge.so` e `libtlmod_runtime.so`: vão no carregador;
  - `libdevtools.so`: vai em `tlmod/mods/devtools/lib/android-arm64/`.

Gerado a partir do commit `51b5403` da branch `ccr-bbd92bf9-i01buk`.
