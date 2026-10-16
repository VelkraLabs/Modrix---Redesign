// libshadowhook_nothing.so embutida na ponte (ver CMakeLists.txt e
// src/hook_shadowhook.cpp). TLMOD_SH_NOTHING_PATH vem do CMake.
    .section .rodata.tlmod_sh_nothing, "a"
    .balign 8
    .global tlmod_sh_nothing
    .hidden tlmod_sh_nothing
tlmod_sh_nothing:
    .incbin TLMOD_SH_NOTHING_PATH
    .global tlmod_sh_nothing_end
    .hidden tlmod_sh_nothing_end
tlmod_sh_nothing_end:
