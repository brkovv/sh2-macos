cmake_minimum_required(VERSION 3.20)
project(sh2nx C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(SH2_GENERATED_DIR "${CMAKE_SOURCE_DIR}/src/recomp/gen" CACHE PATH "Generated C for the selected executable profile")
set(SH2_EXE_PROFILE "v10-ee" CACHE STRING "Executable profile: v10-ee or experimental dc-stock")
set_property(CACHE SH2_EXE_PROFILE PROPERTY STRINGS v10-ee dc-stock)
file(GLOB GEN CONFIGURE_DEPENDS "${SH2_GENERATED_DIR}/*.c")
file(GLOB RT CONFIGURE_DEPENDS src/runtime/*.c src/win32/*.c src/d3d8/*.c src/game/*.c)
if(SH2_EXE_PROFILE STREQUAL "dc-stock")
  list(REMOVE_ITEM RT "${CMAKE_SOURCE_DIR}/src/game/ee.c")
  list(APPEND RT "${CMAKE_SOURCE_DIR}/src/game/profiles/dc_stock.c")
elseif(NOT SH2_EXE_PROFILE STREQUAL "v10-ee")
  message(FATAL_ERROR "Unknown SH2_EXE_PROFILE: ${SH2_EXE_PROFILE}")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(NOT NINTENDO_SWITCH AND SH2_EXE_PROFILE STREQUAL "v10-ee")
  set(DESKTOP_ADAPTER_DIR "${CMAKE_CURRENT_BINARY_DIR}/desktop-gen")
  set(DESKTOP_MANIFEST "${CMAKE_SOURCE_DIR}/src/game/desktop_hooks.json")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/desktop_codegen.py"
    --source "${SH2_GENERATED_DIR}" --output "${DESKTOP_ADAPTER_DIR}" --manifest "${DESKTOP_MANIFEST}"
    OUTPUT_VARIABLE DESKTOP_UNITS OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  set(DESKTOP_OUTPUTS)
  set(DESKTOP_INPUTS)
  foreach(UNIT IN LISTS DESKTOP_UNITS)
    list(REMOVE_ITEM GEN "${SH2_GENERATED_DIR}/${UNIT}")
    list(APPEND GEN "${DESKTOP_ADAPTER_DIR}/${UNIT}")
    list(APPEND DESKTOP_OUTPUTS "${DESKTOP_ADAPTER_DIR}/${UNIT}")
    list(APPEND DESKTOP_INPUTS "${SH2_GENERATED_DIR}/${UNIT}")
  endforeach()
  add_custom_command(OUTPUT ${DESKTOP_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/desktop_codegen.py"
      --source "${SH2_GENERATED_DIR}" --output "${DESKTOP_ADAPTER_DIR}" --manifest "${DESKTOP_MANIFEST}"
    DEPENDS ${DESKTOP_INPUTS} "${DESKTOP_MANIFEST}" tools/desktop_codegen.py VERBATIM)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DESKTOP_MANIFEST}")
endif()
add_executable(sh2 ${GEN} ${RT})
target_include_directories(sh2 PRIVATE "${SH2_GENERATED_DIR}")
if(SH2_EXE_PROFILE STREQUAL "dc-stock")
  target_compile_definitions(sh2 PRIVATE SH2_STOCK)
endif()
# Lifted code relies on wrapping arithmetic and type punning through guest memory.
target_compile_options(sh2 PRIVATE -fwrapv -fno-strict-aliasing)
set_source_files_properties(${GEN} PROPERTIES COMPILE_OPTIONS "-w")
# The launcher's images are built into the binary (src/d3d8/launcher.c).
set(LAUNCHER_ASSETS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/launcher_assets.h")
add_custom_command(OUTPUT "${LAUNCHER_ASSETS_HEADER}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/embed_assets.py"
          "${CMAKE_SOURCE_DIR}" "${LAUNCHER_ASSETS_HEADER}"
  DEPENDS tools/embed_assets.py assets/launcher-serif.png assets/launcher_font_metrics.h
  VERBATIM)
target_sources(sh2 PRIVATE "${LAUNCHER_ASSETS_HEADER}")
target_include_directories(sh2 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# The movies (src/win32/bink.c): FFmpeg's Bink decoders. On Fedora: dnf install ffmpeg-free-devel.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)
target_link_libraries(sh2 PRIVATE PkgConfig::FFMPEG)

if(NINTENDO_SWITCH)
  # Built with the devkitPro toolchain (tools/build-switch.sh); OpenGL comes through EGL and mesa.
  target_include_directories(sh2 PRIVATE ${DEVKITPRO}/portlibs/switch/include)
  target_link_libraries(sh2 PRIVATE SDL2 EGL glapi drm_nouveau stdc++ pthread m)
  # The release NRO: no log (log=1, nxlink), call trace or debugging hooks. -DSH2_LEAN=OFF brings them back.
  option(SH2_LEAN "Switch build without logs, traces or debugging hooks" ON)
  if(SH2_LEAN)
    target_compile_definitions(sh2 PRIVATE SH2_LEAN RECOMP_NO_ICALL_TRACE)
  endif()
  nx_generate_nacp(sh2.nacp NAME "Silent Hill 2" AUTHOR "sh2-nx" VERSION "1.02")
  nx_create_nro(sh2 OUTPUT sh2-nx.nro NACP sh2.nacp NOICON)
else()
  # GL is loaded at run time through SDL (src/d3d8/gl.c), so nothing links libGL.
  find_package(SDL2 REQUIRED)
  # Sources use <SDL2/SDL.h>; some SDL2 packages export only include/SDL2, not its parent.
  find_path(SH2_SDL2_HEADER_ROOT SDL2/SDL.h HINTS ${SDL2_INCLUDE_DIRS} PATH_SUFFIXES .. REQUIRED)
  target_include_directories(sh2 PRIVATE "${SH2_SDL2_HEADER_ROOT}")
  find_package(Threads REQUIRED)
  target_link_libraries(sh2 PRIVATE SDL2::SDL2 m Threads::Threads)
  option(SH2_MAIN_THREAD_GAME "Run guest, SDL video and GL on the main thread" ${APPLE})
  if(APPLE AND NOT SH2_MAIN_THREAD_GAME)
    message(FATAL_ERROR "macOS requires SH2_MAIN_THREAD_GAME for Cocoa/SDL")
  endif()
  if(SH2_MAIN_THREAD_GAME)
    set_property(SOURCE src/runtime/rt_core.c APPEND PROPERTY COMPILE_DEFINITIONS SH2_MAIN_THREAD_GAME)
  endif()
  if(APPLE)
    # Lifted guest calls use the native call stack. Darwin's UI must stay on the main thread.
    target_link_options(sh2 PRIVATE "LINKER:-stack_size,0x1000000")
  endif()
endif()
