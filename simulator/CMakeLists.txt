# ==============================================================================
# simulator/CMakeLists.txt — CantoMk6 桌面模拟器(顶层工程,Windows / Linux)
#
# 用法(Windows,conda elenixos 环境,含 SDL2 + MinGW 工具链):
#   conda activate elenixos
#   cmake -S simulator -B build/sim -G "MinGW Makefiles"
#   cmake --build build/sim -j
#
# 用法(Ubuntu/Debian,系统 SDL2):
#   sudo apt install build-essential cmake libsdl2-dev
#   cmake -S simulator -B build/sim
#   cmake --build build/sim -j
#
# 设计约束(不修改工程其他任何代码,全部适配在本文件 + simulator/ 内完成):
#   1. 先 add_subdirectory(third_party/lvgl) 建出 lvgl 目标
#      (根 CMakeLists.txt 只有 target_link_libraries(... lvgl),没有自己的
#       add_subdirectory —— 必须由模拟器先建,根目录随后复用同一目标)
#   2. 根目录作为子目录加入:add_subdirectory(<root> cantomk6os_build)
#      产出 cantomk6os_lib;COS_PLATFORM=Native 分支注入
#      COS_SYS_ROOT_DIR="${CMAKE_BINARY_DIR}/fs/"(即模拟器 build 目录)
#   3. simulator/ 置于 cantomk6os_lib include 路径首位 → cos_config.h 通过
#      __has_include 解析到 simulator/cos_platform_config.h(而非
#      port/esp32s3/main/ 的版本)
#   4. COS_SIMULATOR 必须以编译宏注入,不能写进 cos_platform_config.h
#      (cos_core.c 在 include cos_config.h 之前就检查该宏;
#       置 1 时 src/port/cos_sim_hw_mock.c 注册假硬件 time/battery/power/sensor)
#   5. JerryScript 选项必须在本文件以 cache 变量预设(add_subdirectory(root)
#      之前):真机由 ESP port 手动编译 jerry 源码绕过 CMake 选项,模拟器走
#      CMake 子目录则由这些 cache 变量生效。JERRY_* 经 jerry-core 的
#      target_compile_definitions(... PUBLIC) 传播给 cantomk6os_lib 与本 exe
#   6. simulator/tools/version.py 桩必须保留:jerryscript/CMakeLists.txt 以
#      ${CMAKE_SOURCE_DIR}/tools/version.py 决定版本号,模拟器为顶层工程时
#      CMAKE_SOURCE_DIR 指向 simulator/
# ==============================================================================
cmake_minimum_required(VERSION 3.12)
project(cantomk6os_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 路径 ───────────────────────────────────────────────────────
set(SIM_DIR      ${CMAKE_CURRENT_SOURCE_DIR})
set(PROJECT_ROOT ${SIM_DIR}/..)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" ABSOLUTE)
set(CONDA_PREFIX $ENV{CONDA_PREFIX})

# ── SDL2(Windows: conda elenixos;Linux: 系统 libsdl2-dev) ─────
if(WIN32)
    # conda 的 SDL2Config.cmake 在 $CONDA_PREFIX/Library/cmake,
    # 同时提供 SDL2::SDL2 目标与 SDL2_* 兼容变量。
    if(CONDA_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH "${CONDA_PREFIX}/Library")
    endif()
    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND)
        message(FATAL_ERROR
            "SDL2 not found. Run `conda activate elenixos` (该环境内含 SDL2)。")
    endif()
else()
    # Linux/macOS:优先 pkg-config,拿到统一的 IMPORTED 目标(自带 include/link
    # 旗标)。发行版自带的老式 sdl2-config.cmake 会把链接旗标塞成单个字符串
    # ("-L... -lSDL2"),直接丢给 target_link_libraries 会当成一个参数而链接失败。
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2PC QUIET IMPORTED_TARGET sdl2)
    endif()
    if(SDL2PC_FOUND)
        set(SDL2_LIBRARIES    PkgConfig::SDL2PC)
        set(SDL2_INCLUDE_DIRS "${SDL2PC_INCLUDE_DIRS}")
    else()
        find_package(SDL2 QUIET)
        if(NOT SDL2_FOUND)
            message(FATAL_ERROR
                "SDL2 not found. Ubuntu: sudo apt install libsdl2-dev")
        endif()
    endif()
endif()

message(STATUS "SDL2 libs: ${SDL2_LIBRARIES}")
message(STATUS "SDL2 includes: ${SDL2_INCLUDE_DIRS}")

# ── JerryScript 选项(必须在 add_subdirectory(root) 之前) ───────
# 与真机值(port/esp32s3/main/CMakeLists.txt)1:1 对齐:
#   JERRY_VM_HALT=1 JERRY_LINE_INFO=1 JERRY_ERROR_MESSAGES=1
#   JERRY_GLOBAL_HEAP_SIZE=512 JERRY_SYSTEM_ALLOCATOR=0
#   JERRY_EXTERNAL_CONTEXT=1 JERRY_CPOINTER_32_BIT=1
# (VM_HALT/LINE_INFO/ERROR_MESSAGES/CMDLINE* 已由根 CMakeLists 自设,
#  这里补齐根目录没设的;GLOBAL_HEAP_SIZE 按字符串传,jerry 内部转数值)
set(JERRY_EXTERNAL_CONTEXT  ON  CACHE BOOL   "jerry external context"          FORCE)
set(JERRY_SYSTEM_ALLOCATOR  OFF CACHE BOOL   "jerry system allocator"          FORCE)
set(JERRY_CPOINTER_32_BIT   ON  CACHE BOOL   "jerry 32-bit compressed pointers" FORCE)
set(JERRY_GLOBAL_HEAP_SIZE  "512" CACHE STRING "jerry global heap size (KB)"    FORCE)
# 压制 jerry 自己的 -Werror 墙(jerry 用 EXTERNAL_COMPILE_FLAGS 钩子追加,
# 必须逐项 -Wno-error=<w>;jerry 的 -W 旗标清单见 jerryscript/CMakeLists.txt)
set(EXTERNAL_COMPILE_FLAGS
    "-Wno-error=all -Wno-error=extra -Wno-error=format-nonliteral -Wno-error=init-self -Wno-error=sign-conversion -Wno-error=format-security -Wno-error=missing-declarations -Wno-error=shadow -Wno-error=strict-prototypes -Wno-error=undef -Wno-error=old-style-definition -Wno-error=logical-op"
    CACHE STRING "external compile flags for jerryscript" FORCE)

# ── LVGL(先建 lvgl 目标,根目录随后复用) ───────────────────────
# LV_BUILD_CONF_DIR 指向 simulator/ → LVGL 编译时用 simulator/lv_conf.h;
# os_desktop.cmake 对 lvgl 目标 PUBLIC 注入 LV_CONF_INCLUDE_SIMPLE +
# LV_KCONFIG_IGNORE,故无需重复定义。
set(LV_BUILD_CONF_DIR "${SIM_DIR}" CACHE PATH "lv_conf.h directory" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_ROOT}/third_party/lvgl lvgl_build)

# LVGL 的 os_desktop.cmake 用 GLOB_RECURSE 把 src/**.S(ARM NEON / Helium
# 手写汇编)一并收进 lvgl 目标。x86 MinGW 的汇编器无法处理这两个文件
# (它们无条件 #include C 头文件 → "no such instruction: typedef ...")。
# third_party/lvgl 不修改,改在本文件把这两个源标记为 header-only,
# 使其不参与汇编(其符号本就被 __ARM_FEATURE_MVE 守卫,无引用)。
file(GLOB_RECURSE LVGL_ARM_ASM_SOURCES
    "${PROJECT_ROOT}/third_party/lvgl/src/*.S")
if(LVGL_ARM_ASM_SOURCES)
    set_source_files_properties(${LVGL_ARM_ASM_SOURCES}
        TARGET_DIRECTORY lvgl
        PROPERTIES HEADER_FILE_ONLY TRUE)
endif()

# ── CantoMk6 Core Library(根目录作为子目录加入) ───────────────
set(COS_PLATFORM "Native" CACHE STRING "Platform" FORCE)
add_subdirectory(${PROJECT_ROOT} cantomk6os_build)

# simulator/ 置于 include 路径【首位】(BEFORE + SYSTEM 去警告)。必须 BEFORE:
# 真机遗留的 third_party/lvgl/lv_conf.h(ESP32-S3 + FreeRTOS)也在引用路径上,
# 若不前置,LVGL 的 #include "lv_conf.h" 会先命中它 → LV_USE_OS=FreeRTOS
# → lv_os.h 拉入 lv_freertos.h → 找不到 FreeRTOS.h 而编译失败。
# 前置后全工程解析到 simulator/lv_conf.h(单线程 LV_OS_NONE)。
# 同时 simulator/cos_platform_config.h 优先于 port/esp32s3/main/ 版本;
# esp_shim/ 提供无守卫的 esp_heap_caps.h(src/ 内仅 cos_core.c /
# cos_service_config.c / jerry_port.c 三处 include)
target_include_directories(cantomk6os_lib SYSTEM BEFORE PUBLIC
    "${SIM_DIR}"
    "${SIM_DIR}/esp_shim"
)

# COS_SIMULATOR 必须对 cantomk6os_lib 可见(PUBLIC → exe 一并继承):
#   cos_core.c            #if COS_SIMULATOR → 编入 cos_sim_hw_mock 假硬件
#   cos_port_critical_bare_metal.c → no-op 临界区(否则 x86 发 cli 特权指令崩溃)
#   cos_service_time.c    → 跳过 esp_sntp.h(真机头)
#   cos_net_wifi.c        → 走 socket 模拟后端(配 ws2_32)
target_compile_definitions(cantomk6os_lib PUBLIC COS_SIMULATOR=1)

# 与真机编译器行为对齐:ESP-IDF 的 GCC 13 把「隐式函数声明」仅当警告,
# 而 MinGW 的 GCC 16(>=14 起)默认将其视为硬错误。src/kernel/core/cos_core.c
# 依赖隐式声明调用 cos_service_time_init / cos_service_beast_mode_init
# (未显式包含其头文件,真机全程只报 warning)。src/ 不可改,故在模拟器侧
# 把这些 GCC14+ 新增的默认错误降级回警告。
target_compile_options(cantomk6os_lib PRIVATE
    -Wno-error=implicit-function-declaration
    -Wno-error=incompatible-pointer-types
    -Wno-error=int-conversion
    -Wno-error=implicit-int)

# ── 模拟器可执行 ───────────────────────────────────────────────
# main.c:SDL 窗口/输入接线(触摸/表冠/侧键/键盘)+ --xxx 冒烟测试分支
# 字体/图标 .c:与真机同款 C 字体 + 系统图标,编进 exe(真机编进 flash;
# RemixIcon.c 已由根 CMakeLists 收进 cantomk6os_lib,此处不重复)
file(GLOB SIM_ICON_SRC ${PROJECT_ROOT}/resources/images/icon/cos_icon_*.c)
file(GLOB SIM_FONT_SRC
    ${PROJECT_ROOT}/resources/font/cos_font_jbm_*.c
    ${PROJECT_ROOT}/resources/font/cos_font_han_sans_*.c
    ${PROJECT_ROOT}/resources/font/cos_font_icon.c
)

add_executable(cantomk6os_sim
    ${SIM_DIR}/main.c
    ${SIM_DIR}/esp_shim/esp_heap_caps.c
    ${SIM_DIR}/esp_shim/esp_sntp_shim.c
    ${SIM_ICON_SRC}
    ${SIM_FONT_SRC}
)

target_include_directories(cantomk6os_sim PRIVATE
    "${SIM_DIR}"
    "${SIM_DIR}/esp_shim"
    "${PROJECT_ROOT}/src"
    "${PROJECT_ROOT}/resources"
    "${PROJECT_ROOT}/third_party/lvgl"
    "${PROJECT_ROOT}/third_party/cJSON"
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(cantomk6os_sim PRIVATE
    COS_SIMULATOR=1
    LV_CONF_INCLUDE_SIMPLE=1
    LV_KCONFIG_IGNORE=1
    COS_PLATFORM="Native"
)

# 同 cantomk6os_lib:GCC14+ 默认错误降级(见上方说明)
target_compile_options(cantomk6os_sim PRIVATE
    -Wno-error=implicit-function-declaration
    -Wno-error=incompatible-pointer-types
    -Wno-error=int-conversion
    -Wno-error=implicit-int)

target_link_libraries(cantomk6os_sim PRIVATE
    cantomk6os_lib
    lvgl
    ${SDL2_LIBRARIES}
)

# Winsock2 for the real network socket backend (src/services/network/cos_net_sock_sim.c)
if(WIN32)
    target_link_libraries(cantomk6os_sim PRIVATE ws2_32)
else()
    # Linux: src/port/critical/cos_port_critical_posix.c 与
    # src/port/file_system/cos_fs_port_posix.c 用 pthread;main.c 用 libm。
    find_package(Threads REQUIRED)
    target_link_libraries(cantomk6os_sim PRIVATE Threads::Threads m)
endif()

# ── 模拟器文件系统布局(fs/ 与根注入的 COS_SYS_ROOT_DIR 一致) ──
set(SIM_FS "${CMAKE_BINARY_DIR}/fs")

add_custom_command(TARGET cantomk6os_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/res/img"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/res/font"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/config"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app/apps"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app/app_data"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/wf/faces"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/wf/wf_data"
    COMMENT "Creating simulator filesystem layout"
)

# ── 内置资源布防(与真机 flash 资源 1:1) ───────────────────────
# 系统图标 .bin → fs/.sys/res/img/(LVGL lodepng 按 magic bytes 解码,不挑扩展名)
file(GLOB RES_ICON_BINS ${PROJECT_ROOT}/resources/images/icon/bin/*.bin)
foreach(bin ${RES_ICON_BINS})
    get_filename_component(fname ${bin} NAME)
    add_custom_command(TARGET cantomk6os_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${bin}" "${SIM_FS}/.sys/res/img/${fname}"
    )
endforeach()

# 宿主系统 CJK 字体 → fs/.sys/res/font/font.ttf(TinyTTF 中文渲染)
# Windows 取 simhei/msyh;Ubuntu/Debian 取 Noto CJK / 文泉驿 / Droid 回退。
set(SIM_CJK_CANDIDATES
    "C:/Windows/Fonts/simhei.ttf"
    "C:/Windows/Fonts/msyh.ttc"
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"
    "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc"
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"
)
set(SIM_CJK_FONT "")
foreach(_f ${SIM_CJK_CANDIDATES})
    if(NOT SIM_CJK_FONT AND EXISTS "${_f}")
        set(SIM_CJK_FONT "${_f}")
    endif()
endforeach()

if(SIM_CJK_FONT)
    add_custom_command(TARGET cantomk6os_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SIM_CJK_FONT}"
            "${SIM_FS}/.sys/res/font/font.ttf"
        COMMENT "Copying CJK font ${SIM_CJK_FONT} for CJK support"
    )
else()
    message(WARNING
        "No CJK font found - Chinese characters will show as boxes. "
        "Ubuntu: sudo apt install fonts-noto-cjk")
endif()

# ── 打包 8 个官方 app 为 .eapk 并布防到模拟 SD 卡 ──────────────
# 插件管理器开机扫描 /sdcard/apps/ 自动注册 .eapk(与真机 SD 卡布防一致)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(NOT Python3_EXECUTABLE)
    set(Python3_EXECUTABLE python)
endif()

set(COS_PKG_BUILDER "${PROJECT_ROOT}/scripts/cos_pkg_builder.py")
set(SIM_SAMPLES_PY  "${PROJECT_ROOT}/scripts/cos_make_samples.py")

# helper:打包一个 app → .sys/app/<pkg>.eapk + 布防到模拟 SD 卡
# (SD 布防挂在 <name>_eapk 目标的 POST_BUILD 上:改 apps/<name>/ 下任意
#  .js/.json 会重新打包并重新布防,即使 C 源码未动也不会布防过期包)
function(sim_pack_app name)
    set(APP_SRC  "${PROJECT_ROOT}/apps/${name}")
    set(APP_EAPK "${SIM_FS}/.sys/app/com.cantomk6.${name}.eapk")
    add_custom_command(
        OUTPUT "${APP_EAPK}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/.sys/app"
        COMMAND ${Python3_EXECUTABLE} "${COS_PKG_BUILDER}"
                "${APP_SRC}" "${APP_EAPK}" --type app
        DEPENDS "${APP_SRC}/manifest.json" "${APP_SRC}/main.js" "${APP_SRC}/icon.bin" "${COS_PKG_BUILDER}"
        COMMENT "Building seed plugin package com.cantomk6.${name}.eapk"
    )
    add_custom_target(${name}_eapk DEPENDS "${APP_EAPK}")
    add_dependencies(cantomk6os_sim ${name}_eapk)
    add_custom_command(TARGET ${name}_eapk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/sdcard/apps"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${APP_EAPK}" "${SIM_FS}/sdcard/apps/com.cantomk6.${name}.eapk"
        COMMENT "Staging com.cantomk6.${name}.eapk onto simulated SD card"
    )
endfunction()

sim_pack_app(alarm)
sim_pack_app(breach)
sim_pack_app(calculator)
sim_pack_app(calendar)
sim_pack_app(globaltime)
sim_pack_app(stopwatch)
sim_pack_app(texthub)
sim_pack_app(timer)

# ── Seed sample media(Gallery/Files 类 app 首跑内容) ─────────
add_custom_command(TARGET cantomk6os_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_FS}/gallery" "${SIM_FS}/files"
    COMMAND ${Python3_EXECUTABLE} "${SIM_SAMPLES_PY}" "${SIM_FS}"
    COMMENT "Seeding sample gallery images and text files"
)
