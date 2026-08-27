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

# ════════════════════════════════════════════════════════════════
# 关键:lv_conf.h 在 port/esp32s3/main/ 下,LVGL 组件(EXTRA_COMPONENT_DIRS)
# 看不到 main/ 的 include 路径。用 include_directories 推到全局。
# 同时 LV_CONF_INCLUDE_SIMPLE 也必须在全局定义,否则 LVGL 组件
# 使用默认 include 策略(需 LV_CONF_PATH 而非搜索头文件)。
# ════════════════════════════════════════════════════════════════
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/main")
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1)