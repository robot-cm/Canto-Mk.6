# ElenixOS ESP32-S3 板级移植工程
# 目标:Seeed Studio XIAO ESP32-S3 + 1.28" Round Touch Display
#
# 用法(ESP-IDF v5.3+):
#   cd port/esp32s3
#   idf.py set-target esp32s3
#   idf.py menuconfig
#   idf.py build
#   idf.py -p /dev/ttyACM0 flash monitor
#
# 依据:AGENTS.md 第1-2节硬件 + 子报告1引脚表 + 子报告2方案A

# 把第三方 LVGL 作为 idf 组件引入(LVGL 原生支持 ESP_PLATFORM,
# 通过 env_support/cmake/esp.cmake 的 idf_component_register 注册)
# 路径:port/esp32s3/../../third_party/lvgl → ElenixOS-fork/third_party/lvgl
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/lvgl"
)
# microlink(Tailscale/WireGuard 客户端组件, 由 Settings VPN 页驱动)
# 目录存在时才引入(仓库中已移除,保留此判断以便恢复目录后自动启用)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../microlink/components")
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/../../microlink/components"
        # wireguard_lwip 是 microlink 的内嵌子组件(PRIV_REQUIRES), 需显式加入
        "${CMAKE_CURRENT_SOURCE_DIR}/../../microlink/components/microlink/components"
    )
endif()

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(elenixos_esp32s3)

# ── USB MSC App:TinyUSB 的 tusb_config.h 定位 ────────────────
# espressif/tinyusb 私有源文件用 #include "tusb_config.h" 局部查找,只走
# tinyusb 自身 PRIV include + 其 REQUIRES 的组件 PUBLIC include。我们的
# tusb_config.h 在 main 组件内,而 main 不能被加进 tinyusb 的 REQUIRES
# (那在 managed_components/ ,会被重下载覆盖)。
# 官方推荐做法(见 espressif__tinyusb/README.md "Using standalone TinyUSB"):
# 直接给 tinyusb 库目标追加 PRIVATE include 目录,指向 tusb_config.h 所在处。
# 必须在 project() 之后、tinyusb 组件库已注册时执行。
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/main")

# ════════════════════════════════════════════════════════════════
# 关键:lv_conf.h 在 port/esp32s3/main/ 下,LVGL 组件(EXTRA_COMPONENT_DIRS)
# 看不到 main/ 的 include 路径。用 include_directories 推到全局。
# 同时 LV_CONF_INCLUDE_SIMPLE 也必须在全局定义,否则 LVGL 组件
# 使用默认 include 策略(需 LV_CONF_PATH 而非搜索头文件)。
# ════════════════════════════════════════════════════════════════
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/main")
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1)