# Mod autocontido: só precisa do header da API (tlmod_api.h / tlmod.hpp).
# Fora do repositório: -DTLMOD_SDK_INCLUDE_DIR=/caminho/modapi/include
cmake_minimum_required(VERSION 3.18)
project(example_boss CXX)

if(NOT DEFINED TLMOD_SDK_INCLUDE_DIR)
  set(TLMOD_SDK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
endif()
if(NOT DEFINED TLMOD_MODS_OUTPUT_DIR)
  set(TLMOD_MODS_OUTPUT_DIR ${CMAKE_BINARY_DIR}/dist/mods)
endif()

# Chave de plataforma, igual a CurrentPlatformKey() do runtime.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _cpu)
if(_cpu MATCHES "^(aarch64|arm64)$")
  set(_cpu arm64)
elseif(_cpu MATCHES "^(x86_64|amd64)$")
  set(_cpu x86_64)
endif()
if(ANDROID)
  set(_os android)
elseif(WIN32)
  set(_os windows)
elseif(APPLE)
  set(_os macos)
else()
  set(_os linux)
endif()
set(TLMOD_PLATFORM "${_os}-${_cpu}")

set(MOD_OUT ${TLMOD_MODS_OUTPUT_DIR}/example_boss)

add_library(example_boss SHARED example_boss.cpp)
target_include_directories(example_boss PRIVATE ${TLMOD_SDK_INCLUDE_DIR})
target_compile_features(example_boss PRIVATE cxx_std_17)
target_compile_options(example_boss PRIVATE -Wall -Wextra -Werror)
set_target_properties(example_boss PROPERTIES
  CXX_VISIBILITY_PRESET hidden            # só tlmod_mod_init é exportado
  LIBRARY_OUTPUT_DIRECTORY ${MOD_OUT}/lib/${TLMOD_PLATFORM}
  RUNTIME_OUTPUT_DIRECTORY ${MOD_OUT}/lib/${TLMOD_PLATFORM})

# ELF (Android/Linux): exporta só tlmod_mod_init. Sem isso a libc++ estática
# do NDK sai inteira na tabela de símbolos dinâmicos do mod.
if(CMAKE_SYSTEM_NAME MATCHES "Android|Linux")
  set(_map ${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
  target_link_options(example_boss PRIVATE -Wl,--version-script=${_map} -Wl,--gc-sections)
  set_target_properties(example_boss PROPERTIES LINK_DEPENDS ${_map})
endif()
if(ANDROID)
  target_link_options(example_boss PRIVATE -Wl,-z,max-page-size=16384)
endif()

# A pasta de saída já é o mod pronto: manifest.json + lib/<plataforma>/.
configure_file(manifest.json ${MOD_OUT}/manifest.json COPYONLY)
