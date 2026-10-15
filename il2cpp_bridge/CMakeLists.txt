cmake_minimum_required(VERSION 3.18)
project(modrix_il2cpp_bridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
add_compile_options(-Wall -Wextra -Werror)

set(BRIDGE_SOURCES src/il2cpp_api.cpp src/bridge.cpp)

if(ANDROID)
  # Build para o jogo: libmodrix_bridge.so (arm64-v8a).
  # cmake -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  #       -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 ..
  add_library(modrix_bridge SHARED ${BRIDGE_SOURCES} src/entry.cpp)
  target_include_directories(modrix_bridge PRIVATE include)
  target_link_libraries(modrix_bridge PRIVATE dl log)
else()
  # Build de host: só para validar a lógica contra uma libil2cpp.so falsa.
  add_library(modrix_bridge SHARED ${BRIDGE_SOURCES})
  target_include_directories(modrix_bridge PUBLIC include)
  target_link_libraries(modrix_bridge PRIVATE dl pthread)

  add_library(il2cpp_mock SHARED test/mock_il2cpp.cpp)
  set_target_properties(il2cpp_mock PROPERTIES OUTPUT_NAME il2cpp CXX_VISIBILITY_PRESET default)

  add_executable(host_test test/host_test.cpp)
  target_link_libraries(host_test PRIVATE modrix_bridge dl pthread)
  add_dependencies(host_test il2cpp_mock)

  enable_testing()
  add_test(NAME host_test COMMAND host_test $<TARGET_FILE:il2cpp_mock>)
endif()
