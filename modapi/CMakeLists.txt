cmake_minimum_required(VERSION 3.18)
project(tlmod_modapi C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(TLMOD_WARNINGS -Wall -Wextra -Werror)

option(TLMOD_WITH_BRIDGE "Compila o adaptador sobre a ponte IL2CPP (../il2cpp_bridge)" ON)

# --- Runtime: independente de plataforma ------------------------------------
add_library(tlmod_runtime STATIC
  src/log.cpp
  src/json.cpp
  src/semver.cpp
  src/manifest.cpp
  src/content_registry.cpp
  src/item_persistence.cpp
  src/png.cpp
  src/item_shell.cpp
  src/dynlib.cpp
  src/sha256.cpp
  src/library_cache.cpp
  src/mod_loader.cpp)
target_include_directories(tlmod_runtime PUBLIC include PRIVATE src)
target_compile_options(tlmod_runtime PRIVATE ${TLMOD_WARNINGS})
find_package(ZLIB REQUIRED)  # PNG das texturas de mod (no Android, a libz.so do sistema)
target_link_libraries(tlmod_runtime PUBLIC ${CMAKE_DL_LIBS} ZLIB::ZLIB)
set_target_properties(tlmod_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Overlay: lógica do painel (todas as plataformas) -------------------------
add_library(tlmod_overlay STATIC overlay/src/config_store.cpp overlay/src/panel.cpp overlay/src/settings_tab.cpp)
target_include_directories(tlmod_overlay PUBLIC overlay/include PRIVATE src overlay/src)
target_compile_options(tlmod_overlay PRIVATE ${TLMOD_WARNINGS})
target_link_libraries(tlmod_overlay PUBLIC tlmod_runtime)
set_target_properties(tlmod_overlay PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Mod de exemplo ----------------------------------------------------------
# Sai em <build>/dist/mods/example_item/ já no formato de pasta de mod
# (separado da pasta de build do CMake, para não levar CMakeFiles junto).
set(TLMOD_SDK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(TLMOD_MODS_OUTPUT_DIR ${CMAKE_BINARY_DIR}/dist/mods)
add_subdirectory(mods/example_item)
add_subdirectory(mods/example_boss)
add_subdirectory(mods/boss_checklist)
add_subdirectory(mods/devtools)

# --- Adaptador sobre a ponte IL2CPP -----------------------------------------
if(TLMOD_WITH_BRIDGE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../il2cpp_bridge ${CMAKE_BINARY_DIR}/il2cpp_bridge)

  add_library(tlmod_bridge_adapter STATIC adapters/bridge/bridge_adapter.cpp)
  target_include_directories(tlmod_bridge_adapter PUBLIC adapters/bridge)
  target_compile_options(tlmod_bridge_adapter PRIVATE ${TLMOD_WARNINGS})
  target_link_libraries(tlmod_bridge_adapter PUBLIC tlmod_runtime tlmod_bridge)
  set_target_properties(tlmod_bridge_adapter PROPERTIES POSITION_INDEPENDENT_CODE ON)

  if(ANDROID)
    # --- Overlay Android: renderizador Java (DEX embutido) + cola JNI ----------
    # Precisa de javac e do d8 do Android SDK (build-tools >= 35: o d8 34
    # quebra com classes anônimas do javac 21). Informe o SDK com
    # -DTLMOD_ANDROID_SDK=/caminho ou ANDROID_SDK_ROOT.
    enable_language(ASM)
    set(TLMOD_ANDROID_SDK "$ENV{ANDROID_SDK_ROOT}" CACHE PATH "Android SDK (platforms/ e build-tools/)")
    file(GLOB _jars "${TLMOD_ANDROID_SDK}/platforms/android-*/android.jar")
    file(GLOB _d8s "${TLMOD_ANDROID_SDK}/build-tools/*/d8")
    list(SORT _jars COMPARE NATURAL)
    list(SORT _d8s COMPARE NATURAL)
    list(POP_BACK _jars TLMOD_ANDROID_JAR)
    list(POP_BACK _d8s TLMOD_D8)
    find_package(Java 11 COMPONENTS Development)
    if(NOT TLMOD_ANDROID_JAR OR NOT TLMOD_D8 OR NOT Java_JAVAC_EXECUTABLE OR NOT Java_JAR_EXECUTABLE)
      message(FATAL_ERROR "overlay: android.jar/d8/javac não encontrados (TLMOD_ANDROID_SDK=${TLMOD_ANDROID_SDK})")
    endif()
    set(OVERLAY_JAVA ${CMAKE_CURRENT_SOURCE_DIR}/overlay/java/com/tlmod/overlay/TLModOverlay.java)
    set(OVERLAY_CLASSES ${CMAKE_CURRENT_BINARY_DIR}/overlay_classes)
    set(OVERLAY_DEX ${CMAKE_CURRENT_BINARY_DIR}/overlay_dex/classes.dex)
    add_custom_command(
      OUTPUT ${OVERLAY_DEX}
      COMMAND ${CMAKE_COMMAND} -E rm -rf ${OVERLAY_CLASSES} ${CMAKE_CURRENT_BINARY_DIR}/overlay_dex
      COMMAND ${CMAKE_COMMAND} -E make_directory ${OVERLAY_CLASSES} ${CMAKE_CURRENT_BINARY_DIR}/overlay_dex
      COMMAND ${Java_JAVAC_EXECUTABLE} --release 11 -Xlint:all -Werror -classpath ${TLMOD_ANDROID_JAR}
              -d ${OVERLAY_CLASSES} ${OVERLAY_JAVA}
      COMMAND ${Java_JAR_EXECUTABLE} cf ${CMAKE_CURRENT_BINARY_DIR}/overlay_classes.jar -C ${OVERLAY_CLASSES} .
      COMMAND ${TLMOD_D8} --min-api 24 --release --output ${CMAKE_CURRENT_BINARY_DIR}/overlay_dex
              ${CMAKE_CURRENT_BINARY_DIR}/overlay_classes.jar
      DEPENDS ${OVERLAY_JAVA}
      COMMENT "overlay: javac + d8 -> classes.dex"
      VERBATIM)
    add_custom_target(tlmod_overlay_dex DEPENDS ${OVERLAY_DEX})
    set_property(SOURCE overlay/android/overlay_dex.S APPEND PROPERTY OBJECT_DEPENDS ${OVERLAY_DEX})

    add_library(tlmod_overlay_android STATIC overlay/android/overlay_jni.cpp overlay/android/overlay_dex.S)
    add_dependencies(tlmod_overlay_android tlmod_overlay_dex)
    target_include_directories(tlmod_overlay_android PUBLIC overlay/android)
    target_compile_definitions(tlmod_overlay_android PRIVATE TLMOD_OVERLAY_DEX_PATH="${OVERLAY_DEX}")
    target_compile_options(tlmod_overlay_android PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${TLMOD_WARNINGS}>)
    target_link_libraries(tlmod_overlay_android PUBLIC tlmod_overlay)
    set_target_properties(tlmod_overlay_android PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # libtlmod_runtime.so: carrega a ponte (NEEDED) e os mods ao subir.
    add_library(tlmod_runtime_android SHARED adapters/bridge/android_main.cpp)
    set_target_properties(tlmod_runtime_android PROPERTIES OUTPUT_NAME tlmod_runtime)
    target_compile_options(tlmod_runtime_android PRIVATE ${TLMOD_WARNINGS})
    target_link_libraries(tlmod_runtime_android PRIVATE tlmod_bridge_adapter tlmod_overlay_android
                          tlmod_overlay tlmod_runtime log)
    set(RUNTIME_MAP ${CMAKE_CURRENT_SOURCE_DIR}/adapters/bridge/runtime_exports.map)
    target_link_options(tlmod_runtime_android PRIVATE
      -Wl,--version-script=${RUNTIME_MAP} -Wl,--gc-sections -Wl,-z,max-page-size=16384)
    set_target_properties(tlmod_runtime_android PROPERTIES LINK_DEPENDS ${RUNTIME_MAP})
  endif()
endif()

# --- Testes (host) -----------------------------------------------------------
if(NOT ANDROID)
  enable_testing()

  add_executable(runtime_test tests/runtime_test.cpp)
  target_compile_options(runtime_test PRIVATE ${TLMOD_WARNINGS})
  find_package(Threads REQUIRED)
  target_link_libraries(runtime_test PRIVATE tlmod_runtime Threads::Threads)
  add_dependencies(runtime_test example_item example_boss boss_checklist devtools)
  target_include_directories(runtime_test PRIVATE src)  # json.h, para conferir a saída
  add_test(NAME runtime_test COMMAND runtime_test ${TLMOD_MODS_OUTPUT_DIR})

  # Lógica do painel (overlay) contra o jogo simulado e os mods reais.
  add_executable(overlay_test overlay/tests/overlay_test.cpp)
  target_compile_options(overlay_test PRIVATE ${TLMOD_WARNINGS})
  target_include_directories(overlay_test PRIVATE src)
  target_link_libraries(overlay_test PRIVATE tlmod_overlay)
  add_dependencies(overlay_test example_item example_boss boss_checklist devtools)
  add_test(NAME overlay_test COMMAND overlay_test ${TLMOD_MODS_OUTPUT_DIR})

  # View-models para a prévia em PNG (overlay/preview/render_preview.py).
  add_executable(overlay_snapshot overlay/tools/overlay_snapshot.cpp)
  target_compile_options(overlay_snapshot PRIVATE ${TLMOD_WARNINGS})
  target_link_libraries(overlay_snapshot PRIVATE tlmod_overlay)

  # Ponta a ponta: runtime + BridgeGameAdapter + ponte contra a libil2cpp.so falsa.
  if(TLMOD_WITH_BRIDGE)
    add_executable(bridge_save_test tests/bridge_save_test.cpp)
    target_compile_options(bridge_save_test PRIVATE ${TLMOD_WARNINGS})
    target_link_libraries(bridge_save_test PRIVATE tlmod_bridge_adapter ${CMAKE_DL_LIBS} Threads::Threads)
    add_dependencies(bridge_save_test il2cpp_mock)
    target_compile_definitions(bridge_save_test PRIVATE
      TLMOD_EXAMPLE_RUBY_PNG="${CMAKE_CURRENT_SOURCE_DIR}/mods/example_item/textures/fictional_ruby.png"
      TLMOD_EXAMPLE_BOSS_PNG="${CMAKE_CURRENT_SOURCE_DIR}/mods/example_boss/textures/ember_titan.png")
    add_test(NAME bridge_save_test COMMAND bridge_save_test $<TARGET_FILE:il2cpp_mock>)
  endif()

  # tlmod_api.h precisa compilar como C99 puro, sem extensões.
  add_library(c_abi_check OBJECT tests/c_abi_check.c)
  set_target_properties(c_abi_check PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_include_directories(c_abi_check PRIVATE include)
  target_compile_options(c_abi_check PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
