// O DEX do renderizador (com.tlmod.overlay.TLModOverlay) embutido na .so:
// assim o overlay é um arquivo só. TLMOD_OVERLAY_DEX_PATH vem do CMake.
    .section .rodata.tlmod_overlay_dex, "a"
    .balign 8
    .global tlmod_overlay_dex
    .hidden tlmod_overlay_dex
tlmod_overlay_dex:
    .incbin TLMOD_OVERLAY_DEX_PATH
    .global tlmod_overlay_dex_end
    .hidden tlmod_overlay_dex_end
tlmod_overlay_dex_end:
