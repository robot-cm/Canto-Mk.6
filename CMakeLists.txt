cmake_minimum_required(VERSION 3.12)
project(ElenixOS C)

# Define a compile-time constant for the ElenixOS system root directory
# On MCU (ESP-IDF): fs/ lives in the project root (source dir)
# On Native simulator: fs/ lives in the build directory
if(EOS_IS_WASM)
    add_compile_definitions(EOS_SYS_ROOT_DIR="/")
elseif(EOS_PLATFORM STREQUAL "Native")
    # Simulator: point to build directory fs/ (created by simulator/CMakeLists.txt)
    add_compile_definitions(EOS_SYS_ROOT_DIR="${CMAKE_BINARY_DIR}/fs/")
else()
    # MCU target: point to project root fs/
    add_compile_definitions(EOS_SYS_ROOT_DIR="${CMAKE_SOURCE_DIR}/fs/")
endif()

# ───────────────────────────────────────────────────────────────
# Build mode: Dev (default) vs Release
#   - Release: set via `cmake -DEOS_BUILD_RELEASE=1`, or
#              `CMAKE_BUILD_TYPE=Release` (aligned with JerryScript logic).
#   - Release disables all EOS_LOG_* output (via EOS_LOG_DISABLE) and
#     skips starting the interactive USB-Serial shell task.
#   - Shell core (eos_shell_init / eos_shell_exec) is ALWAYS kept so the
#     system can still issue commands internally (rescue/debug entry per
#     AGENTS.md §9). Only the blocking USB-JTAG listener task is dropped.
# ───────────────────────────────────────────────────────────────
if(EOS_BUILD_RELEASE OR CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(EOS_BUILD_RELEASE=1 EOS_LOG_DISABLE=1)
    message(STATUS "[ElenixOS] Build mode: RELEASE (logs off, shell listener off)")
else()
    message(STATUS "[ElenixOS] Build mode: DEV (logs on, shell listener on)")
endif()

# Recursively gather all source files
file(GLOB_RECURSE ELENIXOS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c
)
# Only include RemixIcon.c (icon font used as fallback in TTF mode)
# Chinese bitmap fonts (source_han_sans_*.c) excluded since using TTF
file(GLOB ELENIXOS_RES_FONT_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/font/RemixIcon.c
)
list(APPEND ELENIXOS_SRC ${ELENIXOS_RES_FONT_SRC})

# Automatically collect all directories that contain header files
file(GLOB_RECURSE HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.h
)

set(ELENIXOS_INC_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/resources")
foreach(h ${HEADER_FILES})
    get_filename_component(dir ${h} DIRECTORY)
    list(APPEND ELENIXOS_INC_DIRS ${dir})
endforeach()
list(REMOVE_DUPLICATES ELENIXOS_INC_DIRS)

# Define library target
add_library(elenixos_lib STATIC ${ELENIXOS_SRC})

target_include_directories(elenixos_lib
    PUBLIC
    ${ELENIXOS_INC_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jerryscript/jerry-core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jerryscript/jerry-ext/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
)

# Link against necessary libraries

# cJSON
add_library(cjson STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON/cJSON.c
)
target_include_directories(cjson PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
)
target_link_libraries(elenixos_lib PRIVATE cjson)

# JerryScript
add_compile_options(-Wno-gnu-folding-constant)
set(JERRY_VM_HALT ON CACHE INTERNAL "Enable VM halt feature")
set(JERRY_LINE_INFO ON CACHE INTERNAL "Enable line info feature")
set(JERRY_ERROR_MESSAGES ON CACHE INTERNAL "Enable error message strings")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(JERRY_MEM_STATS ON CACHE INTERNAL "Enable memory stats")
else()
    set(JERRY_MEM_STATS OFF CACHE INTERNAL "Enable memory stats")
endif()
set(JERRY_CMDLINE OFF CACHE BOOL "Build jerry command line tool?" FORCE)
set(JERRY_CMDLINE_TEST OFF CACHE BOOL "Build jerry test command line tool?" FORCE)
set(JERRY_CMDLINE_SNAPSHOT OFF CACHE BOOL "Build jerry snapshot command line tool?" FORCE)
set(JERRY_LIBFUZZER OFF CACHE BOOL "Build jerry with libfuzzer support?" FORCE)
set(UNITTESTS OFF CACHE BOOL "Build unit tests?" FORCE)
set(DOCTESTS OFF CACHE BOOL "Build doc tests?" FORCE)
set(ENABLE_AMALGAM OFF CACHE BOOL "Enable amalgamated build?" FORCE)
set(JERRY_PORT ON CACHE BOOL "Build default jerry port implementation?" FORCE)

if(EOS_IS_WASM OR EOS_PLATFORM STREQUAL "Native")
    add_compile_options(-Wno-enum-enum-conversion -Wno-unterminated-string-initialization)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/jerryscript)
target_link_libraries(elenixos_lib PRIVATE jerry-core jerry-ext jerry-port)

# LVGL
target_link_libraries(elenixos_lib PRIVATE lvgl)
