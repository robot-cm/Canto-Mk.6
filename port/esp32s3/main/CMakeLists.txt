# ElenixOS ESP32-S3 main 组件
#
# 架构决策(2026-08-22 编译反馈后修正):
#   ElenixOS Core 与 script engine 强耦合(eos_core.c / apps / widgets /
#   basic_widgets 均 #include script_engine_core.h → jerryscript.h),
#   排除法行不通。改为:全量编译 src/ + JerryScript(jerry-core/ext/port),
#   仅排除模拟器专属文件。

set(EOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

# microlink(Tailscale/WireGuard 客户端组件)目录存在时才作为依赖引入
set(MICROLINK_COMPONENT_DIR "${EOS_ROOT}/microlink/components")
set(MAIN_REQUIRES
    bt
    driver
    lvgl
    spiffs
    esp_wifi
    esp_netif
    nvs_flash
    esp_event
    fatfs
    esp_driver_sdspi
    sdmmc
)
if(EXISTS "${MICROLINK_COMPONENT_DIR}")
    list(APPEND MAIN_REQUIRES microlink)
endif()

idf_component_register(
    SRCS
        "main.c"
        "eos_bt_esp32.c"
        "eos_dev_display_gc9a01.c"
        "eos_dev_touch_chsc6x.c"
        "eos_dev_rtc_bm8563.c"
        "eos_mem_align.c"
        "eos_net_wifi_esp32.c"
    INCLUDE_DIRS
        "."
        "${EOS_ROOT}/third_party/cJSON"
    REQUIRES
        ${MAIN_REQUIRES}
)

# ── ElenixOS Core 全部源文件(仅排除模拟器专属) ──────────
file(GLOB_RECURSE EOS_CORE_SRCS CONFIGURE_DEPENDS "${EOS_ROOT}/src/**/*.c")
list(FILTER EOS_CORE_SRCS EXCLUDE REGEX ".*eos_sim_hw_mock\\.c$")
list(FILTER EOS_CORE_SRCS EXCLUDE REGEX ".*eos_virtual_display\\.c$")
list(FILTER EOS_CORE_SRCS EXCLUDE REGEX ".*eos_net_sock_sim\\.c$")

# ── cJSON(源码直接编入,根 CMake 用静态库) ──────────────
set(CJSON_SRCS "${EOS_ROOT}/third_party/cJSON/cJSON.c")

# ── JerryScript(jerry-core / jerry-ext / 默认 port) ─────
#   jerry_port.c(ElenixOS 自定义)已在 EOS_CORE_SRCS 中
#   jerry-port/common + unix 提供 context/fs/date(ESP-IDF VFS/newlib 兼容)
file(GLOB_RECURSE JERRY_CORE_SRCS CONFIGURE_DEPENDS "${EOS_ROOT}/third_party/jerryscript/jerry-core/**/*.c")
file(GLOB_RECURSE JERRY_EXT_SRCS  CONFIGURE_DEPENDS "${EOS_ROOT}/third_party/jerryscript/jerry-ext/**/*.c")
# 排除 debugger 扩展(未启用,且依赖 socket)
list(FILTER JERRY_EXT_SRCS EXCLUDE REGEX ".*/jerry-ext/debugger/.*")
# 排除平台无关的测试工具
list(FILTER JERRY_EXT_SRCS EXCLUDE REGEX ".*test262\\.c$")
list(FILTER JERRY_EXT_SRCS EXCLUDE REGEX ".*repl\\.c$")
set(JERRY_PORT_SRCS
    "${EOS_ROOT}/third_party/jerryscript/jerry-port/common/jerry-port-context.c"
    "${EOS_ROOT}/third_party/jerryscript/jerry-port/common/jerry-port-fs.c"
    "${EOS_ROOT}/third_party/jerryscript/jerry-port/common/jerry-port-io.c"
    "${EOS_ROOT}/third_party/jerryscript/jerry-port/common/jerry-port-process.c"
    "${EOS_ROOT}/third_party/jerryscript/jerry-port/unix/jerry-port-unix-date.c"
    "${EOS_ROOT}/third_party/jerryscript/jerry-port/unix/jerry-port-unix-fs.c"
)

# ── 字体:内置 C 字体编译进 Flash(XIP 映射,不占 PSRAM) ──
#    EOS_FONT_C_MULTI 模式(eos_platform_config.h 强制)下,eos_font_c_multi.c 依赖
#    eos_font_jbm_{16,20,26,30}(JetBrains Mono,ASCII/拉丁/箭头,共 ~600KB)。
#    fallback 链:jbm_16 -> han_sans_16 -> eos_font_icon;jbm_20 -> han_sans_22 -> eos_font_icon;
#    jbm_26/30 -> eos_font_icon。

target_sources(${COMPONENT_LIB} PRIVATE
    ${EOS_CORE_SRCS}
    ${CJSON_SRCS}
    ${JERRY_CORE_SRCS}
    ${JERRY_EXT_SRCS}
    ${JERRY_PORT_SRCS}
    # 内置 C 字体(EOS_FONT_C_MULTI 必需)
    # 小字号:jbm_10(≤10px)/jbm_13(11..13px),fallback han_sans_16(常用字子集)
    # 中档:jbm_16(16..17px)/jbm_18(18..19px)/jbm_20(20..21px)/jbm_22(22..25px)/jbm_26(26..29px)/jbm_30(≥30px)
    "${EOS_ROOT}/resources/font/eos_font_jbm_10.c"
    "${EOS_ROOT}/resources/font/eos_font_jbm_13.c"
    "${EOS_ROOT}/resources/font/eos_font_jbm_16.c"
    "${EOS_ROOT}/resources/font/eos_font_jbm_18.c"
    "${EOS_ROOT}/resources/font/eos_font_jbm_20.c"
    "${EOS_ROOT}/resources/font/eos_font_jbm_22.c"
    "${EOS_ROOT}/resources/font/eos_font_jbm_26.c"
    "${EOS_ROOT}/resources/font/eos_font_jbm_30.c"
    # 10px 中文子集字体(jbm_10 的 fallback,GB2312 一级 3755 字,约 0.32MB flash)
    "${EOS_ROOT}/resources/font/eos_font_han_sans_10.c"
    # 13px 中文子集字体(jbm_13 的 fallback,GB2312 一级 3755 字,约 0.54MB flash)
    "${EOS_ROOT}/resources/font/eos_font_han_sans_13.c"
    # 中文子集字体(eos_font_jbm_22 的 fallback,GB2312 全量,约 1MB)
    "${EOS_ROOT}/resources/font/eos_font_han_sans_22.c"
    # 16px 中文子集字体(表盘 hint 轮播,177 字符,fallback han_sans_22 兜底)
    "${EOS_ROOT}/resources/font/eos_font_han_sans_16.c"
    # 图标字体子集(RI_*/LV_SYMBOL_* 字符,jbm/han_sans 的 fallback,约 90KB)
    "${EOS_ROOT}/resources/font/eos_font_icon.c"
    # 全量 Remix 图标字体(30px,eos_font.h 的 EOS_FONT_ICON 宏引用,app 页翻页箭头等)
    #   源码约 6MB,编译进 flash 约 700-800KB(factory 5.94MB,余量 1.87MB,可容纳)
    "${EOS_ROOT}/resources/font/RemixIcon.c"
    # Control Center 彩色图标(webp 转换的 ARGB8888 静态图)
    "${EOS_ROOT}/resources/images/icon/eos_icon_bluetooth.c"
    "${EOS_ROOT}/resources/images/icon/eos_icon_wifi.c"
    "${EOS_ROOT}/resources/images/icon/eos_icon_torch.c"
    "${EOS_ROOT}/resources/images/icon/eos_icon_powersave.c"
    "${EOS_ROOT}/resources/images/icon/eos_icon_setting.c"
    # Settings 页面图标(wireguard.webp 普通转换; pwd.webp 白色抠图生成)
    "${EOS_ROOT}/resources/images/icon/eos_icon_wireguard.c"
    "${EOS_ROOT}/resources/images/icon/eos_icon_pwd.c"
)

# ── Include 目录(复刻根 CMakeLists.txt:所有含头文件的目录) ──
file(GLOB_RECURSE EOS_HEADER_FILES "${EOS_ROOT}/src/**/*.h")
set(EOS_INC_DIRS "${EOS_ROOT}/src" "${EOS_ROOT}/resources")
foreach(h ${EOS_HEADER_FILES})
    get_filename_component(dir ${h} DIRECTORY)
    list(APPEND EOS_INC_DIRS ${dir})
endforeach()
list(REMOVE_DUPLICATES EOS_INC_DIRS)

# ── JerryScript:内部使用无前缀平铺 include(如 "lit-globals.h"),
#    必须把 jerry-core/ext/port 下所有含头文件的子目录全部加入 include 路径 ──
file(GLOB_RECURSE JERRY_HEADER_FILES
    "${EOS_ROOT}/third_party/jerryscript/jerry-core/**/*.h"
    "${EOS_ROOT}/third_party/jerryscript/jerry-ext/**/*.h"
    "${EOS_ROOT}/third_party/jerryscript/jerry-port/**/*.h"
)
set(JERRY_INC_DIRS "${EOS_ROOT}/third_party/jerryscript")
foreach(h ${JERRY_HEADER_FILES})
    get_filename_component(dir ${h} DIRECTORY)
    list(APPEND JERRY_INC_DIRS ${dir})
endforeach()
list(REMOVE_DUPLICATES JERRY_INC_DIRS)

target_include_directories(${COMPONENT_LIB} PUBLIC
    ${EOS_INC_DIRS}
    "${EOS_ROOT}/third_party"
    "${EOS_ROOT}/third_party/lvgl"
    "${EOS_ROOT}/third_party/lvgl/src"
    ${JERRY_INC_DIRS}
    # jerry-core 根目录含 config.h 模板,必须显式加入
    "${EOS_ROOT}/third_party/jerryscript/jerry-core"
    "${EOS_ROOT}/third_party/jerryscript/jerry-core/include"
    "${EOS_ROOT}/third_party/jerryscript/jerry-ext"
    "${EOS_ROOT}/third_party/jerryscript/jerry-ext/include"
    "${EOS_ROOT}/third_party/jerryscript/jerry-port"
    "${EOS_ROOT}/third_party/cJSON"
)

# ── 编译定义 ────────────────────────────────────────────
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    EOS_PLATFORM_ESP32=1
    EOSUSE_VIRTUAL_DISPLAY=0
    EOS_SIMULATOR=0
    LV_CONF_INCLUDE_SIMPLE=1
    LV_KCONFIG_IGNORE=1
    # 注意:不用 -D 定义 EOS_SYS_ROOT_DIR。
    # 由 eos_platform_config.h 定义为 "/sdcard"(真机 SD 挂载点),
    # -D 会先于 #ifndef 生效导致覆盖错误。
    # JerryScript 配置(对齐根 CMakeLists.txt)
    JERRY_VM_HALT=1
    JERRY_LINE_INFO=1
    JERRY_ERROR_MESSAGES=1
    # JERRY_LOGGING=1:恢复 jerry_assert_fail 的 ICE 现场行打印。
    # JERRY_ERROR_MSG 在 JERRY_LOGGING=0 时被编译为空,
    # 断言失败只剩 Fatal code=120,没有文件/行号/表达式,无法定位凶手。
    # 仅启用错误级 log,无额外刷屏。
    JERRY_LOGGING=1
    # JS 堆 512KB:内部堆模式(SYSTEM_ALLOCATOR=0) + 外部 context。
    # JERRY_EXTERNAL_CONTEXT=1 使 512KB 堆走 jerry_port_context_alloc
    # 一次大分配(实现中放入 PSRAM),BSS 不再占用内部 DRAM;
    # 避免 SYSTEM_ALLOCATOR=1 时小对象 malloc 碎片化内部 DMA RAM
    # (内部 DMA largest 30KB→32B,直接导致 SD/FATFS 写路径失败的根因)
    # 256KB 时多个常驻 app realm(Activity 栈保留旧程序)会瓜分殆尽,
    # 新 app 启动即 JerryScript fatal OOM(120);PSRAM 充足,放大到 512KB。
    JERRY_GLOBAL_HEAP_SIZE=512
    JERRY_SYSTEM_ALLOCATOR=0
    JERRY_EXTERNAL_CONTEXT=1
    JERRY_CPOINTER_32_BIT=1
)
# microlink 已接入 → eos_net_vpn.c 编译真机驱动路径(目录存在时才启用)
if(EXISTS "${MICROLINK_COMPONENT_DIR}")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC EOS_HAVE_MICROLINK=1)
endif()

# ── 链接:--wrap 重定向 malloc/free/realloc/calloc 到 8 字节对齐实现 ──
#   ESP-IDF malloc 仅 4 字节对齐;JerryScript JERRY_CPOINTER_32_BIT 要求
#   JMEM_ALIGNMENT(8) 对齐(eos_mem_align.c 提供 __wrap_* 实现)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=malloc"
    "-Wl,--wrap=free"
    "-Wl,--wrap=realloc"
    "-Wl,--wrap=calloc"
    # 强制提取 eos_bt_esp32.c.obj(strong 后端)。
    # GNU ld 单遍扫描 libmain.a 时,strong 定义排在引用者(eos_net_bt.c.obj)
    # 之前不会被提取,导致 weak stub 胜出、NimBLE 依赖链未被拉入。
    "-Wl,-u,eos_net_bt_backend_set_enabled"
)

# ── 警告抑制(原项目源码在 xtensa GCC 下,不改原代码) ──────
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format
    -Wno-error=format-truncation
    -Wno-error=array-bounds
    -Wno-error=stringop-overflow
    -Wno-error=maybe-uninitialized
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=enum-conversion
    -Wno-error=implicit-function-declaration
    -Wno-error=return-type
)
