cmake_minimum_required(VERSION 3.18)
project(tlmod_il2cpp_bridge C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)

set(BRIDGE_SOURCES src/log.cpp src/il2cpp_api.cpp src/bridge.cpp src/npc_hooks.cpp src/world_state.cpp src/save_hooks.cpp
  src/item_hooks.cpp src/item_traits.cpp src/draw_hooks.cpp
  src/screen_hooks.cpp)
set(BRIDGE_WARNINGS -Wall -Wextra -Werror)

if(ANDROID)
  # Build para o jogo: libtlmod_bridge.so, com o ShadowHook embutido.
  # cmake -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  #       -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 ..
  enable_language(ASM)

  # --- ShadowHook (bytedance/android-inline-hook, MIT) -----------------------
  # Baixado no configure. Para build offline, aponte para um checkout local:
  #   -DFETCHCONTENT_SOURCE_DIR_SHADOWHOOK=/caminho/android-inline-hook
  include(FetchContent)
  FetchContent_Declare(shadowhook
    GIT_REPOSITORY https://github.com/bytedance/android-inline-hook.git
    GIT_TAG v2.0.1  # 854c775c2c3676e57a0f383597ebf420b5204161
    GIT_SHALLOW TRUE)
  # A raiz do repositório não tem CMakeLists.txt (é um projeto Gradle), então
  # isto só baixa o código; o target é montado abaixo.
  FetchContent_MakeAvailable(shadowhook)
  set(SH_DIR ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp)

  if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(SH_ARCH arm64)
  elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(SH_ARCH arm)
  else()
    message(FATAL_ERROR "ShadowHook só suporta arm64-v8a e armeabi-v7a (ABI: ${ANDROID_ABI})")
  endif()

  # Mesmas fontes e flags do CMakeLists.txt do próprio ShadowHook, mas como
  # biblioteca estática. sh_jni.c fica de fora: o JNI_OnLoad dele registraria
  # natives de com.bytedance.shadowhook.ShadowHook, classe que o jogo não tem.
  file(GLOB SH_SOURCES
    ${SH_DIR}/*.c
    ${SH_DIR}/arch/${SH_ARCH}/*.c
    ${SH_DIR}/arch/${SH_ARCH}/*.S
    ${SH_DIR}/common/*.c
    ${SH_DIR}/third_party/*/*.c)
  list(REMOVE_ITEM SH_SOURCES ${SH_DIR}/sh_jni.c)

  add_library(shadowhook_static STATIC ${SH_SOURCES})
  set_target_properties(shadowhook_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_features(shadowhook_static PRIVATE c_std_11)
  # -Weverything -Werror como no upstream. O upstream compila com o NDK r23
  # (clang 12); o clang 18 do NDK r27 liga diagnósticos novos de estilo sob
  # -Weverything que o código não segue. Desligamos só esses quatro:
  #   declaration-after-statement  declarações no meio do bloco (válido em C11)
  #   unsafe-buffer-usage          aritmética de ponteiro (estilo C++ "hardened")
  #   reserved-identifier          __res/__r0 das macros de syscall do lss
  #   unused-macros                SH_LINKER_ABI_STR em sh_linker.c
  target_compile_options(shadowhook_static PRIVATE
    -Weverything -Werror -Os -ffunction-sections -fdata-sections
    -Wno-declaration-after-statement -Wno-unsafe-buffer-usage
    -Wno-reserved-identifier -Wno-unused-macros)
  target_include_directories(shadowhook_static
    PUBLIC ${SH_DIR}/include
    PRIVATE ${SH_DIR} ${SH_DIR}/arch/${SH_ARCH} ${SH_DIR}/common
            ${SH_DIR}/third_party/xdl ${SH_DIR}/third_party/bsd ${SH_DIR}/third_party/lss)

  # O shadowhook_init (monitor do linker) faz dlopen("libshadowhook_nothing.so"),
  # uma biblioteca auxiliar que o AAR oficial instala ao lado da libshadowhook.so.
  # Aqui ela é compilada como no upstream, embutida na ponte e gravada numa pasta
  # privada do app na inicialização (src/hook_shadowhook.cpp). Sem mexer no código
  # do ShadowHook, dois nomes são redirecionados só onde importa:
  #   sh_linker.c: dlopen          -> tlmod_sh_dlopen (abre a cópia pelo caminho absoluto)
  #   shadowhook.c: sh_linker_init -> tlmod_sh_linker_init (se o monitor falhar, os hooks
  #                                   por endereço, que são os da ponte, seguem funcionando)
  set_source_files_properties(${SH_DIR}/sh_linker.c PROPERTIES COMPILE_DEFINITIONS "dlopen=tlmod_sh_dlopen")
  set_source_files_properties(${SH_DIR}/shadowhook.c PROPERTIES
    COMPILE_DEFINITIONS "sh_linker_init=tlmod_sh_linker_init")

  add_library(shadowhook_nothing SHARED ${SH_DIR}/nothing/sh_nothing.c)
  target_compile_options(shadowhook_nothing PRIVATE -Oz -Weverything -Werror -fno-ident -fno-unwind-tables
    -fno-asynchronous-unwind-tables)
  set_target_properties(shadowhook_nothing PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  target_link_options(shadowhook_nothing PRIVATE -Wl,-z,max-page-size=16384 -Wl,--strip-all -Wl,--as-needed
    -Wl,--hash-style=sysv -Wl,--build-id=none -Wl,-nostdlib -nostartfiles -nodefaultlibs)

  # --- Ponte -------------------------------------------------------------------
  add_library(tlmod_bridge SHARED ${BRIDGE_SOURCES} src/hook_shadowhook.cpp src/entry.cpp
    src/sh_nothing_blob.S)
  add_dependencies(tlmod_bridge shadowhook_nothing)
  set(SH_NOTHING_SO ${CMAKE_CURRENT_BINARY_DIR}/libshadowhook_nothing.so)
  set_property(SOURCE src/sh_nothing_blob.S APPEND PROPERTY OBJECT_DEPENDS ${SH_NOTHING_SO})
  target_compile_definitions(tlmod_bridge PRIVATE TLMOD_SH_NOTHING_PATH="${SH_NOTHING_SO}")
  target_include_directories(tlmod_bridge PUBLIC include)
  target_compile_options(tlmod_bridge PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${BRIDGE_WARNINGS}>
    -ffunction-sections -fdata-sections)
  target_link_libraries(tlmod_bridge PRIVATE shadowhook_static dl log)
  # Exporta só TLMod_* (nada do ShadowHook vaza), descarta código morto e
  # alinha a 16 KB, exigido por aparelhos Android 15+ com páginas de 16 KB.
  set(EXPORTS_MAP ${CMAKE_CURRENT_SOURCE_DIR}/src/exports.map)
  target_link_options(tlmod_bridge PRIVATE
    -Wl,--version-script=${EXPORTS_MAP} -Wl,--gc-sections -Wl,--exclude-libs,ALL
    -Wl,-z,max-page-size=16384)
  set_target_properties(tlmod_bridge PROPERTIES LINK_DEPENDS ${EXPORTS_MAP})
else()
  # Build de host: só para validar a lógica contra uma libil2cpp.so falsa.
  # O ShadowHook só existe para ARM/Android; aqui o teste usa um instalador falso.
  add_library(tlmod_bridge SHARED ${BRIDGE_SOURCES})
  target_include_directories(tlmod_bridge PUBLIC include)
  target_compile_options(tlmod_bridge PRIVATE ${BRIDGE_WARNINGS})
  target_link_libraries(tlmod_bridge PRIVATE dl pthread)

  add_library(il2cpp_mock SHARED test/mock_il2cpp.cpp)
  set_target_properties(il2cpp_mock PROPERTIES OUTPUT_NAME il2cpp CXX_VISIBILITY_PRESET default)
  target_compile_options(il2cpp_mock PRIVATE ${BRIDGE_WARNINGS})

  add_executable(host_test test/host_test.cpp)
  target_compile_options(host_test PRIVATE ${BRIDGE_WARNINGS})
  target_link_libraries(host_test PRIVATE tlmod_bridge dl pthread)
  add_dependencies(host_test il2cpp_mock)

  enable_testing()
  add_test(NAME host_test COMMAND host_test $<TARGET_FILE:il2cpp_mock>)
endif()
