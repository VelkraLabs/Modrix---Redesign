// O DEX do renderizador (com.modrix.overlay.ModrixOverlay) embutido na .so:
// assim o overlay é um arquivo só. MODRIX_OVERLAY_DEX_PATH vem do CMake.
    .section .rodata.modrix_overlay_dex, "a"
    .balign 8
    .global modrix_overlay_dex
    .hidden modrix_overlay_dex
modrix_overlay_dex:
    .incbin MODRIX_OVERLAY_DEX_PATH
    .global modrix_overlay_dex_end
    .hidden modrix_overlay_dex_end
modrix_overlay_dex_end:
