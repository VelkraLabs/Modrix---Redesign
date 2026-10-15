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
    # libmodrix_runtime.so: carrega a ponte (NEEDED) e os mods ao subir.
    add_library(modrix_runtime_android SHARED adapters/bridge/android_main.cpp)
    set_target_properties(modrix_runtime_android PROPERTIES OUTPUT_NAME modrix_runtime)
    target_compile_options(modrix_runtime_android PRIVATE ${MODRIX_WARNINGS})
    target_link_libraries(modrix_runtime_android PRIVATE modrix_bridge_adapter modrix_runtime log)
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

  # modrix_api.h precisa compilar como C99 puro, sem extensões.
  add_library(c_abi_check OBJECT tests/c_abi_check.c)
  set_target_properties(c_abi_check PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_include_directories(c_abi_check PRIVATE include)
  target_compile_options(c_abi_check PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
