# Entrega: pacote de teste do TLMod (17ª versão)

Branch só para download, sem o código. Pode ser apagada depois.

| arquivo | SHA-256 |
|---|---|
| `tlmod-teste.zip` | `bb1285298def86dbff8eff1785f5ced82b3b43697721f1eab9870bd4494c7d4e` |
| `libtlmod_bridge.so` | `a6557071e12c6d4d8d6a6e3052868bcf588f41c6fa8a7c069d4e1a869b0d89ed` |
| `libtlmod_runtime.so` | `26dd0947edbe713e9ede50f5b470e3d152a609e56283d43e62740003dd3c2a0f` |
| `libdevtools.so` | `92fdc9e524e203021e221e3e12d71422a8a75381f134ddfc926b7a64cb9e5d9c` |
| `README_TESTE.md` | `4ef80d6d4aba17f2790526f0d0269ec5819efb803b2bf2a6d36f6567337a2ae7` |

- `tlmod-teste.zip`: o pacote completo.
- `libtlmod_runtime.so` e `libdevtools.so` (vai em `tlmod/mods/devtools/lib/android-arm64/`):
  as duas que mudaram desde a 16ª versão (novo layout do overlay: "M" só com o Log, ícones do Boss
  Checklist e das Ferramentas). A ponte e o resto da pasta `tlmod/` não mudaram.
- `previa_overlay.png`: prévia do novo layout, desenhada no PC a partir dos mesmos dados do painel
  (não é uma captura do jogo).
- `libexample_item.so` (da 16ª versão) foi tirado daqui: não mudou.

Gerado a partir do commit `c43a38b` da branch `ccr-bbd92bf9-i01buk`.
