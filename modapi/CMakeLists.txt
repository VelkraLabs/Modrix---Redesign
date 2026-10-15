cmake_minimum_required(VERSION 3.18)
project(modrix_modapi C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(MODRIX_WARNINGS -Wall -Wextra -Werror)

option(MODRIX_WITH_BRIDGE "Compila o adaptador sobre a ponte IL2CPP (../il2cpp_bridge)" ON)

# --- Runtime: independente de plataforma ------------------------------------
add_library(modrix_runtime STATIC
  src/log.cpp
  src/json.cpp
  src/semver.cpp
  src/manifest.cpp
  src/content_registry.cpp
  src/dynlib.cpp
  src/mod_loader.cpp)
target_include_directories(modrix_runtime PUBLIC include PRIVATE src)
target_compile_options(modrix_runtime PRIVATE ${MODRIX_WARNINGS})
target_link_libraries(modrix_runtime PUBLIC ${CMAKE_DL_LIBS})
set_target_properties(modrix_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Overlay: lógica do painel (todas as plataformas) -------------------------
add_library(modrix_overlay STATIC overlay/src/config_store.cpp overlay/src/panel.cpp)
target_include_directories(modrix_overlay PUBLIC overlay/include PRIVATE src overlay/src)
target_compile_options(modrix_overlay PRIVATE ${MODRIX_WARNINGS})
target_link_libraries(modrix_overlay PUBLIC modrix_runtime)
set_target_properties(modrix_overlay PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Mod de exemplo ----------------------------------------------------------
# Sai em <build>/dist/mods/example_item/ já no formato de pasta de mod
# (separado da pasta de build do CMake, para não levar CMakeFiles junto).
set(MODRIX_SDK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(MODRIX_MODS_OUTPUT_DIR ${CMAKE_BINARY_DIR}/dist/mods)
add_subdirectory(mods/example_item)
add_subdirectory(mods/example_boss)
add_subdirectory(mods/boss_checklist)

# --- Adaptador sobre a ponte IL2CPP -----------------------------------------
if(MODRIX_WITH_BRIDGE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../il2cpp_bridge ${CMAKE_BINARY_DIR}/il2cpp_bridge)

  add_library(modrix_bridge_adapter STATIC adapters/bridge/bridge_adapter.cpp)
  target_include_directories(modrix_bridge_adapter PUBLIC adapters/bridge)
  target_compile_options(modrix_bridge_adapter PRIVATE ${MODRIX_WARNINGS})
  target_link_libraries(modrix_bridge_adapter PUBLIC modrix_runtime modrix_bridge)
  set_target_properties(modrix_bridge_adapter PROPERTIES POSITION_INDEPENDENT_CODE ON)

  if(ANDROID)
    # --- Overlay Android: renderizador Java (DEX embutido) + cola JNI ----------
    # Precisa de javac e do d8 do Android SDK (build-tools >= 35: o d8 34
    # quebra com classes anônimas do javac 21). Informe o SDK com
    # -DMODRIX_ANDROID_SDK=/caminho ou ANDROID_SDK_ROOT.
    enable_language(ASM)
    set(MODRIX_ANDROID_SDK "$ENV{ANDROID_SDK_ROOT}" CACHE PATH "Android SDK (platforms/ e build-tools/)")
    file(GLOB _jars "${MODRIX_ANDROID_SDK}/platforms/android-*/android.jar")
    file(GLOB _d8s "${MODRIX_ANDROID_SDK}/build-tools/*/d8")
    list(SORT _jars COMPARE NATURAL)
    list(SORT _d8s COMPARE NATURAL)
    list(POP_BACK _jars MODRIX_ANDROID_JAR)
    list(POP_BACK _d8s MODRIX_D8)
    find_package(Java 11 COMPONENTS Development)
    if(NOT MODRIX_ANDROID_JAR OR NOT MODRIX_D8 OR NOT Java_JAVAC_EXECUTABLE OR NOT Java_JAR_EXECUTABLE)
      message(FATAL_ERROR "overlay: android.jar/d8/javac não encontrados (MODRIX_ANDROID_SDK=${MODRIX_ANDROID_SDK})")
    endif()
    set(OVERLAY_JAVA ${CMAKE_CURRENT_SOURCE_DIR}/overlay/java/com/modrix/overlay/ModrixOverlay.java)
    set(OVERLAY_CLASSES ${CMAKE_CURRENT_BINARY_DIR}/overlay_classes)
    set(OVERLAY_DEX ${CMAKE_CURRENT_BINARY_DIR}/overlay_dex/classes.dex)
    add_custom_command(
      OUTPUT ${OVERLAY_DEX}
      COMMAND ${CMAKE_COMMAND} -E rm -rf ${OVERLAY_CLASSES} ${CMAKE_CURRENT_BINARY_DIR}/overlay_dex
      COMMAND ${CMAKE_COMMAND} -E make_directory ${OVERLAY_CLASSES} ${CMAKE_CURRENT_BINARY_DIR}/overlay_dex
      COMMAND ${Java_JAVAC_EXECUTABLE} --release 11 -Xlint:all -Werror -classpath ${MODRIX_ANDROID_JAR}
              -d ${OVERLAY_CLASSES} ${OVERLAY_JAVA}
      COMMAND ${Java_JAR_EXECUTABLE} cf ${CMAKE_CURRENT_BINARY_DIR}/overlay_classes.jar -C ${OVERLAY_CLASSES} .
      COMMAND ${MODRIX_D8} --min-api 24 --release --output ${CMAKE_CURRENT_BINARY_DIR}/overlay_dex
              ${CMAKE_CURRENT_BINARY_DIR}/overlay_classes.jar
      DEPENDS ${OVERLAY_JAVA}
      COMMENT "overlay: javac + d8 -> classes.dex"
      VERBATIM)
    add_custom_target(modrix_overlay_dex DEPENDS ${OVERLAY_DEX})
    set_property(SOURCE overlay/android/overlay_dex.S APPEND PROPERTY OBJECT_DEPENDS ${OVERLAY_DEX})

    add_library(modrix_overlay_android STATIC overlay/android/overlay_jni.cpp overlay/android/overlay_dex.S)
    add_dependencies(modrix_overlay_android modrix_overlay_dex)
    target_include_directories(modrix_overlay_android PUBLIC overlay/android)
    target_compile_definitions(modrix_overlay_android PRIVATE MODRIX_OVERLAY_DEX_PATH="${OVERLAY_DEX}")
    target_compile_options(modrix_overlay_android PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${MODRIX_WARNINGS}>)
    target_link_libraries(modrix_overlay_android PUBLIC modrix_overlay)
    set_target_properties(modrix_overlay_android PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # libmodrix_runtime.so: carrega a ponte (NEEDED) e os mods ao subir.
    add_library(modrix_runtime_android SHARED adapters/bridge/android_main.cpp)
    set_target_properties(modrix_runtime_android PROPERTIES OUTPUT_NAME modrix_runtime)
    target_compile_options(modrix_runtime_android PRIVATE ${MODRIX_WARNINGS})
    target_link_libraries(modrix_runtime_android PRIVATE modrix_bridge_adapter modrix_overlay_android
                          modrix_overlay modrix_runtime log)
    set(RUNTIME_MAP ${CMAKE_CURRENT_SOURCE_DIR}/adapters/bridge/runtime_exports.map)
    target_link_options(modrix_runtime_android PRIVATE
      -Wl,--version-script=${RUNTIME_MAP} -Wl,--gc-sections -Wl,-z,max-page-size=16384)
    set_target_properties(modrix_runtime_android PROPERTIES LINK_DEPENDS ${RUNTIME_MAP})
  endif()
endif()

# --- Testes (host) -----------------------------------------------------------
if(NOT ANDROID)
  enable_testing()

  add_executable(runtime_test tests/runtime_test.cpp)
  target_compile_options(runtime_test PRIVATE ${MODRIX_WARNINGS})
  find_package(Threads REQUIRED)
  target_link_libraries(runtime_test PRIVATE modrix_runtime Threads::Threads)
  add_dependencies(runtime_test example_item example_boss boss_checklist)
  target_include_directories(runtime_test PRIVATE src)  # json.h, para conferir a saída
  add_test(NAME runtime_test COMMAND runtime_test ${MODRIX_MODS_OUTPUT_DIR})

  # Lógica do painel (overlay) contra o jogo simulado e os mods reais.
  add_executable(overlay_test overlay/tests/overlay_test.cpp)
  target_compile_options(overlay_test PRIVATE ${MODRIX_WARNINGS})
  target_include_directories(overlay_test PRIVATE src)
  target_link_libraries(overlay_test PRIVATE modrix_overlay)
  add_dependencies(overlay_test example_item example_boss boss_checklist)
  add_test(NAME overlay_test COMMAND overlay_test ${MODRIX_MODS_OUTPUT_DIR})

  # View-models para a prévia em PNG (overlay/preview/render_preview.py).
  add_executable(overlay_snapshot overlay/tools/overlay_snapshot.cpp)
  target_compile_options(overlay_snapshot PRIVATE ${MODRIX_WARNINGS})
  target_link_libraries(overlay_snapshot PRIVATE modrix_overlay)

  # modrix_api.h precisa compilar como C99 puro, sem extensões.
  add_library(c_abi_check OBJECT tests/c_abi_check.c)
  set_target_properties(c_abi_check PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_include_directories(c_abi_check PRIVATE include)
  target_compile_options(c_abi_check PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
