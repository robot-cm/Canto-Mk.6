cmake_minimum_required(VERSION 3.12)
project(ElenixOS_Simulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Paths ──────────────────────────────────────────────────────
set(SIM_DIR    ${CMAKE_CURRENT_SOURCE_DIR})
set(PROJECT_ROOT ${SIM_DIR}/..)
set(CONDA_PREFIX $ENV{CONDA_PREFIX})

# ── SDL2 (from conda environment) ─────────────────────────────
if(CONDA_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${CONDA_PREFIX}/Library")
endif()
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    # Fallback: manual SDL2 discovery for conda layout
    set(SDL2_INCLUDE_DIR "${CONDA_PREFIX}/Library/include/SDL2")
    set(SDL2_LIBRARY "${CONDA_PREFIX}/Library/lib/SDL2.lib")
    set(SDL2_DLL "${CONDA_PREFIX}/Library/bin/SDL2.dll")
    add_library(SDL2::SDL2 SHARED IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${SDL2_DLL}"
        IMPORTED_IMPLIB "${SDL2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
    )
    set(SDL2_LIBRARIES SDL2::SDL2)
    set(SDL2_INCLUDE_DIRS "${SDL2_INCLUDE_DIR}")
endif()

# ── LVGL ──────────────────────────────────────────────────────
# Tell LVGL where to find lv_conf.h
set(LV_BUILD_CONF_DIR "${SIM_DIR}" CACHE PATH "lv_conf.h directory" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(${PROJECT_ROOT}/third_party/lvgl lvgl_build)

# ── ElenixOS Core Library ─────────────────────────────────────
set(EOS_PLATFORM "Native" CACHE STRING "Platform" FORCE)
add_subdirectory(${PROJECT_ROOT} elenixos_build)

# Add platform config to the elenixos_lib include path so
# eos_config.h picks up eos_platform_config.h
target_include_directories(elenixos_lib PUBLIC "${SIM_DIR}")

# ── Simulator Executable ──────────────────────────────────────
add_executable(elenixos_sim ${SIM_DIR}/main.c)

target_include_directories(elenixos_sim PRIVATE
    "${SIM_DIR}"
    "${PROJECT_ROOT}/src"
    "${PROJECT_ROOT}/third_party/lvgl"
    "${PROJECT_ROOT}/third_party/jerryscript/jerry-core/include"
    "${PROJECT_ROOT}/third_party/jerryscript/jerry-ext/include"
    "${PROJECT_ROOT}/third_party/cJSON"
)

target_compile_definitions(elenixos_sim PRIVATE
    LV_CONF_INCLUDE_SIMPLE=1
    EOS_PLATFORM="Native"
)

target_link_libraries(elenixos_sim PRIVATE
    elenixos_lib
    lvgl
    ${SDL2_LIBRARIES}
)

# Winsock2 for the real network socket backend (eos_net_sock_sim.c).
if(WIN32)
    target_link_libraries(elenixos_sim PRIVATE ws2_32)
endif()

# Copy SDL2.dll next to the executable so it can be found at runtime
if(SDL2_DLL AND EXISTS "${SDL2_DLL}")
    add_custom_command(TARGET elenixos_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL2_DLL}"
            "$<TARGET_FILE_DIR:elenixos_sim>"
    )
endif()

# ── Filesystem for simulator ──────────────────────────────────
# Create the expected directory layout so EOS can find resources at runtime
set(SIM_FS "${CMAKE_CURRENT_BINARY_DIR}/fs")

add_custom_command(TARGET elenixos_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/res/img"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/res/font"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/config"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app/apps"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app/app_data"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/wf/faces"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/wf/wf_data"
    COMMENT "Creating simulator filesystem layout"
)

# Copy built-in image resources as .bin (LVGL lodepng decoder reads by magic bytes, not extension)
file(GLOB RES_IMAGES "${PROJECT_ROOT}/resources/images/*.png")
foreach(img ${RES_IMAGES})
    get_filename_component(fname ${img} NAME_WE)
    add_custom_command(TARGET elenixos_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${img}" "${SIM_FS}/.sys/res/img/${fname}.bin"
    )
endforeach()

# Copy Windows system font for TTF rendering
if(EXISTS "C:/Windows/Fonts/simhei.ttf")
    add_custom_command(TARGET elenixos_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/Windows/Fonts/simhei.ttf"
            "${SIM_FS}/.sys/res/font/font.ttf"
        COMMENT "Copying simhei.ttf for CJK font support"
    )
elseif(EXISTS "C:/Windows/Fonts/msyh.ttc")
    add_custom_command(TARGET elenixos_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/Windows/Fonts/msyh.ttc"
            "${SIM_FS}/.sys/res/font/font.ttf"
        COMMENT "Copying msyh.ttc as fallback for CJK font support"
    )
else()
    message(WARNING "No CJK font found in C:/Windows/Fonts - Chinese characters will show as boxes")
endif()

# ── Seed plugin package (for plugin-chain smoke test) ──────────────────────
# Build apps/pmdemo into a real .eapk and stage it into the FS so the
# --shell-test smoke test can exercise the full chain:
#   scan .eapk -> parse manifest -> register with the Plugin Manager ->
#   Launcher lists it -> launch via SPM -> JerryScript runs main.js ->
#   Back releases every resource it created.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(NOT Python3_EXECUTABLE)
    set(Python3_EXECUTABLE python)
endif()

set(PMDEMO_SRC      "${PROJECT_ROOT}/apps/pmdemo")
set(PMDEMO_EAPK     "${SIM_FS}/.sys/app/com.elenix.pmdemo.eapk")
set(EOS_PKG_BUILDER "${PROJECT_ROOT}/scripts/eos_pkg_builder.py")

add_custom_command(
    OUTPUT "${PMDEMO_EAPK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
    COMMAND ${Python3_EXECUTABLE} "${EOS_PKG_BUILDER}"
            "${PMDEMO_SRC}" "${PMDEMO_EAPK}" --type app
    DEPENDS "${PMDEMO_SRC}/manifest.json" "${PMDEMO_SRC}/main.js" "${PMDEMO_SRC}/icon.bin" "${EOS_PKG_BUILDER}"
    COMMENT "Building seed plugin package com.elenix.pmdemo.eapk"
)
add_custom_target(pmdemo_eapk DEPENDS "${PMDEMO_EAPK}")
add_dependencies(elenixos_sim pmdemo_eapk)

# Build apps/clock (时光集 页1 全屏表盘) into a real .eapk and stage it.
set(CLOCK_SRC      "${PROJECT_ROOT}/apps/clock")
set(CLOCK_EAPK     "${SIM_FS}/.sys/app/com.elenix.clock.eapk")
add_custom_command(
    OUTPUT "${CLOCK_EAPK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
    COMMAND ${Python3_EXECUTABLE} "${EOS_PKG_BUILDER}"
            "${CLOCK_SRC}" "${CLOCK_EAPK}" --type app
    DEPENDS "${CLOCK_SRC}/manifest.json" "${CLOCK_SRC}/main.js" "${CLOCK_SRC}/icon.bin" "${EOS_PKG_BUILDER}"
    COMMENT "Building seed plugin package com.elenix.clock.eapk"
)
add_custom_target(clock_eapk DEPENDS "${CLOCK_EAPK}")
add_dependencies(elenixos_sim clock_eapk)

# Build apps/timer (秒表, 计时器.html 移植) into a real .eapk and stage it.
set(TIMER_SRC      "${PROJECT_ROOT}/apps/timer")
set(TIMER_EAPK     "${SIM_FS}/.sys/app/com.elenix.timer.eapk")
add_custom_command(
    OUTPUT "${TIMER_EAPK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
    COMMAND ${Python3_EXECUTABLE} "${EOS_PKG_BUILDER}"
            "${TIMER_SRC}" "${TIMER_EAPK}" --type app
    DEPENDS "${TIMER_SRC}/manifest.json" "${TIMER_SRC}/main.js" "${TIMER_SRC}/icon.bin" "${EOS_PKG_BUILDER}"
    COMMENT "Building seed plugin package com.elenix.timer.eapk"
)
add_custom_target(timer_eapk DEPENDS "${TIMER_EAPK}")
add_dependencies(elenixos_sim timer_eapk)

# Build apps/globaltime (环球时间: 点阵地球 + 城市时区) into a real .eapk and stage it.
set(GLOBALTIME_SRC      "${PROJECT_ROOT}/apps/globaltime")
set(GLOBALTIME_EAPK     "${SIM_FS}/.sys/app/com.elenix.globaltime.eapk")
add_custom_command(
    OUTPUT "${GLOBALTIME_EAPK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
    COMMAND ${Python3_EXECUTABLE} "${EOS_PKG_BUILDER}"
            "${GLOBALTIME_SRC}" "${GLOBALTIME_EAPK}" --type app
    DEPENDS "${GLOBALTIME_SRC}/manifest.json" "${GLOBALTIME_SRC}/main.js" "${GLOBALTIME_SRC}/icon.bin" "${EOS_PKG_BUILDER}"
    COMMENT "Building seed plugin package com.elenix.globaltime.eapk"
)
add_custom_target(globaltime_eapk DEPENDS "${GLOBALTIME_EAPK}")
add_dependencies(elenixos_sim globaltime_eapk)

# Build apps/alarm (闹钟, 闹钟.html 移植) into a real .eapk and stage it.
set(ALARM_SRC       "${PROJECT_ROOT}/apps/alarm")
set(ALARM_EAPK      "${SIM_FS}/.sys/app/com.elenix.alarm.eapk")
add_custom_command(
    OUTPUT "${ALARM_EAPK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
    COMMAND ${Python3_EXECUTABLE} "${EOS_PKG_BUILDER}"
            "${ALARM_SRC}" "${ALARM_EAPK}" --type app
    DEPENDS "${ALARM_SRC}/manifest.json" "${ALARM_SRC}/main.js" "${ALARM_SRC}/icon.bin" "${EOS_PKG_BUILDER}"
    COMMENT "Building seed plugin package com.elenix.alarm.eapk"
)
add_custom_target(alarm_eapk DEPENDS "${ALARM_EAPK}")
add_dependencies(elenixos_sim alarm_eapk)

# Build apps/album (相册 P1 探针, id=com.elenix.album 与 FM 回传写死 id 一致) into an .eapk.
set(ALBUM_SRC       "${PROJECT_ROOT}/apps/album")
set(ALBUM_EAPK      "${SIM_FS}/.sys/app/com.elenix.album.eapk")
add_custom_command(
    OUTPUT "${ALBUM_EAPK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
    COMMAND ${Python3_EXECUTABLE} "${EOS_PKG_BUILDER}"
            "${ALBUM_SRC}" "${ALBUM_EAPK}" --type app
    DEPENDS "${ALBUM_SRC}/manifest.json" "${ALBUM_SRC}/main.js" "${ALBUM_SRC}/icon.bin" "${EOS_PKG_BUILDER}"
    COMMENT "Building seed plugin package com.elenix.album.eapk"
)
add_custom_target(album_eapk DEPENDS "${ALBUM_EAPK}")
add_dependencies(elenixos_sim album_eapk)

# Build apps/notes (笔记: 系统键盘输入 + SD 卡保存) into an .eapk.
set(NOTES_SRC       "${PROJECT_ROOT}/apps/notes")
set(NOTES_EAPK      "${SIM_FS}/.sys/app/com.elenix.notes.eapk")
add_custom_command(
    OUTPUT "${NOTES_EAPK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
    COMMAND ${Python3_EXECUTABLE} "${EOS_PKG_BUILDER}"
            "${NOTES_SRC}" "${NOTES_EAPK}" --type app
    DEPENDS "${NOTES_SRC}/manifest.json" "${NOTES_SRC}/main.js" "${NOTES_SRC}/icon.bin" "${EOS_PKG_BUILDER}"
    COMMENT "Building seed plugin package com.elenix.notes.eapk"
)
add_custom_target(notes_eapk DEPENDS "${NOTES_EAPK}")
add_dependencies(elenixos_sim notes_eapk)

# Build apps/draw (画图: eos.draw 平台层 + 48 色调色板 + 保存 .edrw) into an .eapk.
set(DRAW_SRC       "${PROJECT_ROOT}/apps/draw")
set(DRAW_EAPK      "${SIM_FS}/.sys/app/com.elenix.draw.eapk")
add_custom_command(
    OUTPUT "${DRAW_EAPK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
    COMMAND ${Python3_EXECUTABLE} "${EOS_PKG_BUILDER}"
            "${DRAW_SRC}" "${DRAW_EAPK}" --type app
    DEPENDS "${DRAW_SRC}/manifest.json" "${DRAW_SRC}/main.js" "${DRAW_SRC}/icon.bin" "${EOS_PKG_BUILDER}"
    COMMENT "Building seed plugin package com.elenix.draw.eapk"
)
add_custom_target(draw_eapk DEPENDS "${DRAW_EAPK}")
add_dependencies(elenixos_sim draw_eapk)

# Build apps/breach (入侵协议: Cyberpunk2077 Breach Protocol 复刻) into an .eapk.
set(BREACH_SRC       "${PROJECT_ROOT}/apps/breach")
set(BREACH_EAPK      "${SIM_FS}/.sys/app/com.elenix.breach.eapk")
add_custom_command(
    OUTPUT "${BREACH_EAPK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
    COMMAND ${Python3_EXECUTABLE} "${EOS_PKG_BUILDER}"
            "${BREACH_SRC}" "${BREACH_EAPK}" --type app
    DEPENDS "${BREACH_SRC}/manifest.json" "${BREACH_SRC}/main.js" "${BREACH_SRC}/icon.bin" "${EOS_PKG_BUILDER}"
    COMMENT "Building seed plugin package com.elenix.breach.eapk"
)
add_custom_target(breach_eapk DEPENDS "${BREACH_EAPK}")
add_dependencies(elenixos_sim breach_eapk)

# ── Seed the simulated SD card with plugin packages ─────────────────────────
# The Plugin Manager boots by scanning EOS_SD_APPS_DIR (/sdcard/apps/) for
# .eapk/.ewpk packages and registering any that are not already installed.
# Stage the built demo packages onto the simulated SD so the boot auto-scan
# (and the manual `plugin scan` shell command) has something to discover.
#
# NOTE: these copies hang off the *_eapk targets (not elenixos_sim POST_BUILD)
# so that editing a .js/.json under apps/ re-packs AND re-stages the SD card
# even when the C sources are untouched (otherwise the SD copy goes stale).
add_custom_command(TARGET pmdemo_eapk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PMDEMO_EAPK}" "${SIM_FS}/sdcard/apps/com.elenix.pmdemo.eapk"
    COMMENT "Staging com.elenix.pmdemo.eapk onto simulated SD card"
)
add_custom_command(TARGET clock_eapk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CLOCK_EAPK}" "${SIM_FS}/sdcard/apps/com.elenix.clock.eapk"
    COMMENT "Staging com.elenix.clock.eapk onto simulated SD card"
)
add_custom_command(TARGET timer_eapk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TIMER_EAPK}" "${SIM_FS}/sdcard/apps/com.elenix.timer.eapk"
    COMMENT "Staging com.elenix.timer.eapk onto simulated SD card"
)
add_custom_command(TARGET globaltime_eapk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GLOBALTIME_EAPK}" "${SIM_FS}/sdcard/apps/com.elenix.globaltime.eapk"
    COMMENT "Staging com.elenix.globaltime.eapk onto simulated SD card"
)
add_custom_command(TARGET alarm_eapk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ALARM_EAPK}" "${SIM_FS}/sdcard/apps/com.elenix.alarm.eapk"
    COMMENT "Staging com.elenix.alarm.eapk onto simulated SD card"
)
add_custom_command(TARGET album_eapk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ALBUM_EAPK}" "${SIM_FS}/sdcard/apps/com.elenix.album.eapk"
    COMMENT "Staging com.elenix.album.eapk onto simulated SD card"
)
add_custom_command(TARGET notes_eapk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NOTES_EAPK}" "${SIM_FS}/sdcard/apps/com.elenix.notes.eapk"
    COMMENT "Staging com.elenix.notes.eapk onto simulated SD card"
)
add_custom_command(TARGET draw_eapk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DRAW_EAPK}" "${SIM_FS}/sdcard/apps/com.elenix.draw.eapk"
    COMMENT "Staging com.elenix.draw.eapk onto simulated SD card"
)
add_custom_command(TARGET breach_eapk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BREACH_EAPK}" "${SIM_FS}/sdcard/apps/com.elenix.breach.eapk"
    COMMENT "Staging com.elenix.breach.eapk onto simulated SD card"
)

# ── Seed sample media (Gallery + Files apps) ─────────────────
# Generate a few PNGs under <fs>/gallery and text files under <fs>/files so
# the native Gallery and Files apps have content on first run. Idempotent.
set(SIM_SAMPLES_PY "${PROJECT_ROOT}/scripts/eos_make_samples.py")
add_custom_command(TARGET elenixos_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/gallery" "${SIM_FS}/files"
    COMMAND ${Python3_EXECUTABLE} "${SIM_SAMPLES_PY}" "${SIM_FS}"
    COMMENT "Seeding sample gallery images and text files"
)
