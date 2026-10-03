cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures (use \"arm64;x86_64\" for universal)")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
project(OpenGTA VERSION 0.2.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(OG_WARN -Wall -Wextra -Wno-unused-parameter)

# The portable core: everything except host backends (no platform headers).
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/*.c src/render/*.c src/game/*.c src/front/*.c src/audio/*.c src/hud/*.c src/movie/*.c src/render/hires/*.c)
set(HOST_SRC src/vfs_host.c)
foreach(f ${HOST_SRC} src/platform_gasm.c)
  list(REMOVE_ITEM CORE_SRC "${CMAKE_SOURCE_DIR}/${f}")
endforeach()

# Platform: native (tools and headless tests) or gasm (opengta.wasm, a gasm guest):
#   cmake -S . -B build-gasm -DOPENGTA_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=<gasm>/sdk/c/cmake/gasm-toolchain.cmake [-DWASI_SDK_PREFIX=<wasi-sdk>]
# (tools/fetch-gasm-sdk.sh fetches the released SDK and wasi-sdk into .deps/)
set(OPENGTA_PLATFORM native CACHE STRING "Platform: native or gasm")
if(OPENGTA_PLATFORM STREQUAL "gasm")
  include(Gasm)
  gasm_add_game(opengta ${CORE_SRC} src/platform_gasm.c)
  target_include_directories(opengta PRIVATE src)
  target_compile_options(opengta PRIVATE ${OG_WARN} -O2)
  # Map and style loading keep large buffers on the stack: 1 MiB instead of 64 KiB.
  target_link_options(opengta PRIVATE -Wl,-z,stack-size=1048576 -Wl,--strip-debug)
  return()
endif()

add_library(og_core STATIC ${CORE_SRC})
target_include_directories(og_core PUBLIC src)
target_compile_options(og_core PRIVATE ${OG_WARN})
target_link_libraries(og_core PUBLIC m)

# Tests and tools read the game data through the file layer (./game, or OPENGTA_DATA=<dir|installer>).
enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*_test.c)
foreach(src ${TEST_SRC})
  get_filename_component(t ${src} NAME_WE)
  add_executable(${t} ${src} ${HOST_SRC})
  target_link_libraries(${t} PRIVATE og_core)
  target_compile_options(${t} PRIVATE ${OG_WARN})
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# Tools (native): skin_template, the skin author's manifest and validator (docs/skins.md).
add_executable(skin_template tools/skin-template.c ${HOST_SRC})
target_link_libraries(skin_template PRIVATE og_core)
target_compile_options(skin_template PRIVATE ${OG_WARN})
# skin_generate: a seamless flat skin drawn from scratch, the original only a reference (tools/skin-generate.c).
add_executable(skin_generate tools/skin-generate.c ${HOST_SRC})
target_link_libraries(skin_generate PRIVATE og_core)
target_compile_options(skin_generate PRIVATE ${OG_WARN})
