  # mini_tree (generic CMake build for non-ESP-IDF projects)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(mini_tree LANGUAGES C CXX ASM)
endif()

# 策略兼容：本文件可被父工程 include，也可独立构建
if(POLICY CMP0003)
    cmake_policy(SET CMP0003 NEW)
endif()

if(ESP_PLATFORM)
    # 纯架构：不硬编码某一 MCU。板级通过下列之一注入 BOARD_DTS / dtc -I/-D / 树外扫描：
    #   1) MINI_TREE_BOARD_PORT（绝对路径）
    #   2) 同级 ../board_port.cmake（本 ESP 板工程默认）
    # 注意: component_get_requirements 为 script 模式，不可 CONFIGURE_DEPENDS
    if(DEFINED MINI_TREE_BOARD_PORT AND EXISTS "${MINI_TREE_BOARD_PORT}")
        include("${MINI_TREE_BOARD_PORT}")
    elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../board_port.cmake")
        include("${CMAKE_CURRENT_LIST_DIR}/../board_port.cmake")
    endif()

    file(GLOB MINI_TREE_PRODUCT_DRV_SRC_DIRS
        LIST_DIRECTORIES true
        "${CMAKE_CURRENT_LIST_DIR}/drivers/*/src")
    file(GLOB MINI_TREE_PRODUCT_DRV_SRCS
        "${CMAKE_CURRENT_LIST_DIR}/drivers/*/src/*.c")
    if(DEFINED MINI_TREE_DTC_EXTRA_SCAN_DIRS)
        list(APPEND MINI_TREE_DTC_EXTRA_SCAN_DIRS ${MINI_TREE_PRODUCT_DRV_SRC_DIRS})
    else()
        set(MINI_TREE_DTC_EXTRA_SCAN_DIRS ${MINI_TREE_PRODUCT_DRV_SRC_DIRS})
    endif()
    if(DEFINED MINI_TREE_DTC_EXTRA_DEPENDS)
        list(APPEND MINI_TREE_DTC_EXTRA_DEPENDS ${MINI_TREE_PRODUCT_DRV_SRCS})
    else()
        set(MINI_TREE_DTC_EXTRA_DEPENDS ${MINI_TREE_PRODUCT_DRV_SRCS})
    endif()

    # 未注入时：占位 DTS + 仅 dt-bindings -I（可编过，无板级节点）
    if(NOT DEFINED BOARD_DTS OR BOARD_DTS STREQUAL "")
        set(BOARD_DTS "${CMAKE_CURRENT_LIST_DIR}/board/dts/board.dts")
    endif()
    if(NOT DEFINED MINI_TREE_DTC_EXTRA_ARGS)
        set(MINI_TREE_DTC_EXTRA_ARGS "-I${CMAKE_CURRENT_LIST_DIR}/board")
    endif()

    include("${CMAKE_CURRENT_LIST_DIR}/cmake/esp_idf.cmake")
    return()
endif()
option(MINI_TREE_LWIP "Enable lwIP TCP/IP stack (mini_tree_net port layer)" OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/dep_fetch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/etl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/lwip.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/cjson.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/littlefs.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/easyflash.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/multibutton.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/fatfs.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/mcuboot.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/lvgl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/u8g2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/flashdb.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/sfud.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/easylogger.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/nanopb.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/coremqtt.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/libmodbus.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/mbedtls.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/cmsis_dsp.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/corehttp.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/miniz.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/freemodbus.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/hal/paths.cmake")

set(KCONFIG_DOT "${CMAKE_CURRENT_LIST_DIR}/.config")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KCONFIG_DOT}")

# config.h 输出目录 (定义提前到此处, 供下方配置期 Kconfig 同步使用)
set(KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
set(KCONFIG_OUT     "${KCONFIG_GEN_DIR}/config.h")

# ── 配置期先跑一次 genconfig: 把 Kconfig 求值结果回写 .config ──
# 下面所有 file(STRINGS ${KCONFIG_DOT} ... REGEX "^CONFIG_X=y$") 只认 .config
# 里显式写着的 =y; 而 default y 与被 select 拉起的符号要经 kconfiglib 求解
# 后才会出现。.config 陈旧时 (新增了 Kconfig 项 / 用户没重跑 menuconfig)
# 两侧就会打架: config.h 说开了, CMake 却没把对应源文件编进来 ——
# 中间件产物是静态库, 不报未定义符号, 要到最后链接 ELF 才炸。
# genconfig.py 的回写幂等且内容一致时不触碰磁盘, 不会因时间戳抖动
# 反复触发 CMAKE_CONFIGURE_DEPENDS 重配; 顺带把 config.h 也提前生成好。
# (构建期的 add_custom_command 仍保留, 负责 .config / Kconfig 变动后重新适配。)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${KCONFIG_GEN_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tools/genconfig.py"
            "${CMAKE_CURRENT_LIST_DIR}/Kconfig.non_esp"
            "${KCONFIG_GEN_DIR}"
            --config "${KCONFIG_DOT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE _kconfig_sync_rc
)
if(NOT _kconfig_sync_rc EQUAL 0)
    message(FATAL_ERROR "genconfig.py 配置期同步失败 (rc=${_kconfig_sync_rc}): "
                        "请确认 Python3 与 kconfiglib 可用 (tools/_vendor 已内置)")
endif()

file(STRINGS "${KCONFIG_DOT}" MINI_OS_ENTRY REGEX "^CONFIG_OS_(MINI_OS|FREERTOS|RTTHREAD|BARE)=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_ON REGEX "^CONFIG_SYSTEM=y$")
if(CONFIG_SYSTEM_ON)
    set(MINI_TREE_SYSTEM ON)
else()
    set(MINI_TREE_SYSTEM OFF)
endif()
file(STRINGS "${KCONFIG_DOT}" CONFIG_EVENT_BUS_ON REGEX "^CONFIG_EVENT_BUS=y$")
# EVENT_BUS 依赖 SYSTEM: .config 手写 y 但 SYSTEM=n 时按 Kconfig 语义视为关
if(NOT CONFIG_EVENT_BUS_ON OR NOT MINI_TREE_SYSTEM)
    set(MINI_TREE_EVENT_BUS OFF)
else()
    set(MINI_TREE_EVENT_BUS ON)
endif()
# SystemCmd: 默认关, 仅显式 "CONFIG_SYSTEM_CMD=y" 才编入 (系统层唯一保留的 C++ 模块)
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_CMD_ON REGEX "^CONFIG_SYSTEM_CMD=y$")
if(CONFIG_SYSTEM_CMD_ON AND MINI_TREE_SYSTEM)
    set(MINI_TREE_SYSTEM_CMD ON)
else()
    set(MINI_TREE_SYSTEM_CMD OFF)
endif()

# USB 软编码：对齐 Kconfig 语义 — 显式 "CONFIG_USB=y" 才编入 (并拉 TinyUSB)，
# .config 未置位时不编入，避免缺符号时误拉网络依赖。
file(STRINGS "${KCONFIG_DOT}" CONFIG_USB_ON REGEX "^CONFIG_USB=y$")
if(CONFIG_USB_ON)
    set(MINI_TREE_USB ON)
else()
    set(MINI_TREE_USB OFF)
endif()
if(MINI_TREE_USB)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/tinyusb.cmake")
endif()

# VIRQ 软编码：对齐 Kconfig 语义 — 显式 "CONFIG_VIRQ=y" 才编入，缺失/未置位不编入。
file(STRINGS "${KCONFIG_DOT}" CONFIG_VIRQ_ON REGEX "^CONFIG_VIRQ=y$")
if(CONFIG_VIRQ_ON)
    set(MINI_TREE_VIRQ ON)
else()
    set(MINI_TREE_VIRQ OFF)
endif()

# 裸机调度器三态 (choice: XTASK_NONE / XTASK_COOP / XTASK_PREEMPT)：
#   - 显式 "CONFIG_XTASK_COOP=y"  → 协调式
#   - 显式 "CONFIG_XTASK_PREEMPT=y" → 抢占式 (已完工可编译)
#   - 均未显式 =y → 无调度器 (XTASK_NONE, 应用层自写 while 循环)
file(STRINGS "${KCONFIG_DOT}" CONFIG_XTASK_COOP_ON REGEX "^CONFIG_XTASK_COOP=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_XTASK_PREEMPT_ON REGEX "^CONFIG_XTASK_PREEMPT=y$")
if(CONFIG_XTASK_PREEMPT_ON)
    set(MINI_TREE_XTASK_MODE "PREEMPT")
    set(MINI_TREE_XTASK ON)
    set(MINI_TREE_XTASK_PREEMPT ON)
elseif(CONFIG_XTASK_COOP_ON)
    set(MINI_TREE_XTASK_MODE "COOP")
    set(MINI_TREE_XTASK ON)
    set(MINI_TREE_XTASK_PREEMPT OFF)
else()
    set(MINI_TREE_XTASK_MODE "NONE")
    set(MINI_TREE_XTASK OFF)
    set(MINI_TREE_XTASK_PREEMPT OFF)
endif()

# mini-ota (引导/OTA 库, lib/mini-ota): 显式 "CONFIG_MINI_OTA=y" 才编入 (对齐 Kconfig 语义)。
# 从 .config 桥接, 避免 Kconfig 未回流到 CMake 的 CONFIG_* 宏; 其余 OTA 参数
# (分区/FLASH/SRAM/CRC/加密) 均由 config.h 直供 boot_config.h, 不经 CMake。
file(STRINGS "${KCONFIG_DOT}" CONFIG_MINI_OTA_ON REGEX "^CONFIG_MINI_OTA=y$")
if(CONFIG_MINI_OTA_ON)
    set(MINI_TREE_OTA ON)
    # CONFIG_IMAGE_CRYPTO (int 0/1) 决定 mini-ota 是否编入 mbedtls 分支
    file(STRINGS "${KCONFIG_DOT}" CONFIG_OTA_CRYPTO_ON REGEX "^CONFIG_IMAGE_CRYPTO=1$")
    if(CONFIG_OTA_CRYPTO_ON)
        set(MINI_TREE_OTA_CRYPTO ON)
    else()
        set(MINI_TREE_OTA_CRYPTO OFF)
    endif()
else()
    set(MINI_TREE_OTA OFF)
    set(MINI_TREE_OTA_CRYPTO OFF)
endif()

# HAL 全部为 weak 空实现，板级强符号覆盖；中间件不绑定SDK
set(HAL_SRCS
    "hal/gpio/hal_gpio.c"
    "hal/spi/hal_spi.c"
    "hal/uart/hal_uart.c"
    "hal/i2c/hal_i2c.c"
    "hal/can/hal_can.c"
    "hal/rtc/hal_rtc.c"
    "hal/i2s/hal_i2s.c"
    "hal/iwdg/hal_iwdg.c"
    "hal/wwdg/hal_wwdg.c"
    "hal/adc/hal_adc.c"
    "hal/dac/hal_dac.c"
    "hal/tim/hal_tim.c"
    "hal/amp/hal_amp.c"
    "hal/storage/hal_storage.c"
    "hal/system/hal_platform_safety.c"
    "hal/system/hal_sdio.c"
    "hal/system/hal_systick.c"
    "hal/hal_if_dummy.c"
)

if(MINI_TREE_USB)
    list(APPEND HAL_SRCS "hal/usb/hal_usb.c")
endif()

if(MINI_OS_ENTRY STREQUAL "CONFIG_OS_FREERTOS=y")
    set(MINI_OS_DEFINE CONFIG_OS_FREERTOS)
elseif(MINI_OS_ENTRY STREQUAL "CONFIG_OS_RTTHREAD=y")
    set(MINI_OS_DEFINE CONFIG_OS_RTTHREAD)
elseif(MINI_OS_ENTRY STREQUAL "CONFIG_OS_MINI_OS=y")
    set(MINI_OS_DEFINE CONFIG_OS_MINI_OS)
else()
    set(MINI_OS_DEFINE CONFIG_OS_BARE)
endif()




if(MINI_TREE_SYSTEM)
    # 系统运行时恒为纯 C (system_c/); safe_state 亦为 C, 同属 system_c/
    set(SYSTEM_SRCS
        "system_c/src/system_init.c"
        "system_c/src/system_wdt.c"
        "system_c/src/task_manager.c"
        "system_c/src/safe_state.c"
    )
    # system_scrubber 的校验原语来自 mini-ota (image_verify_area, 不再自带 CRC 表),
    # 故仅在 CONFIG_SYSTEM_SCRUBBER(已依赖 CONFIG_MINI_OTA) 开启时编入;
    # 否则会因缺 mini-ota 头/符号而编译失败。
    file(STRINGS "${KCONFIG_DOT}" MINI_TREE_SCRUBBER_ON REGEX "^CONFIG_SYSTEM_SCRUBBER=y$")
    if(MINI_TREE_SCRUBBER_ON)
        list(APPEND SYSTEM_SRCS "system_c/src/system_scrubber.c")
    endif()
    # SystemCmd (命令分发器) 是系统层唯一保留的 C++ 模块, 仅 CONFIG_SYSTEM_CMD=y 时编入
    if(MINI_TREE_SYSTEM_CMD)
        list(APPEND SYSTEM_SRCS "system_cpp/src/system_cmd.cpp")
    endif()
endif()


file(GLOB _PRODUCT_DRV_SRCS_REL
    "${CMAKE_CURRENT_LIST_DIR}/drivers/*/src/*.c")
file(GLOB _PRODUCT_DRV_INC_DIRS LIST_DIRECTORIES true
    "${CMAKE_CURRENT_LIST_DIR}/drivers/*/include")
file(GLOB _PRODUCT_DRV_SRC_DIRS LIST_DIRECTORIES true
    "${CMAKE_CURRENT_LIST_DIR}/drivers/*/src")

set(GENERATED_BOARD_DIR "${CMAKE_BINARY_DIR}/generated/board/mini_tree")

# 按 DTS 节点裁剪产品驱动源: 仅编译本板 active 节点实际用到的 driver。
# dt_used_drivers.txt 由 dtc-lite 生成 (绝对路径, 每行一个), 用正斜杠归一。
# 首配时文件可能未生成, 视为空 (不裁剪), 由 CMAKE_CONFIGURE_DEPENDS 在生成后触发重配。
set(_PRODUCT_DRV_USED_TXT "${GENERATED_BOARD_DIR}/dt_used_drivers.txt")
if(EXISTS "${_PRODUCT_DRV_USED_TXT}")
    set(_USED_DRV "" )
    file(STRINGS "${_PRODUCT_DRV_USED_TXT}" _USED_DRV)
    if(_USED_DRV)
        set(_PRODUCT_DRV_SRCS_REL_SAVE ${_PRODUCT_DRV_SRCS_REL})
        set(_PRODUCT_DRV_SRCS_REL "")
        foreach(_psrc ${_PRODUCT_DRV_SRCS_REL_SAVE})
            file(TO_CMAKE_PATH "${_psrc}" _psrc_cmake)
            if(_psrc_cmake IN_LIST _USED_DRV)
                list(APPEND _PRODUCT_DRV_SRCS_REL "${_psrc}")
            endif()
        endforeach()
    else()
        # txt 存在但为空: 本板未用任何产品驱动
        set(_PRODUCT_DRV_SRCS_REL "")
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_PRODUCT_DRV_USED_TXT}")

set(DRIVER_SRCS
    ${_PRODUCT_DRV_SRCS_REL}
    "vfs/gpio/vfs-gpio.c"
    "bus/spi/spi_bus.c"
    "bus/uart/uart_bus.c"
    "bus/i2c/i2c_bus.c"
    "bus/can/can_bus.c"
    "bus/i2s/i2s_bus.c"
    "can_hook/can_hook.c"
    "vfs/spi/vfs-spi.c"
    "vfs/uart/vfs-uart.c"
    "vfs/i2c/vfs-i2c.c"
    "vfs/iwdg/vfs-iwdg.c"
    "vfs/wwdg/vfs-wwdg.c"
)

if(MINI_TREE_USB)
    list(APPEND DRIVER_SRCS "bus/usb/usb_bus.c")
    list(APPEND DRIVER_SRCS "bus/usb/usb_net_cb.c")
endif()

# KCONFIG_GEN_DIR / KCONFIG_OUT 已在文件头部 (配置期 Kconfig 同步处) 定义

set(SCRUBBER_GEN_DIR    "${CMAKE_BINARY_DIR}/generated/scrubber/mini_tree")
set(SCRUBBER_CRC_HDR    "${SCRUBBER_GEN_DIR}/system_scrubber_crc_gen.h")
set(BOARD_DTS           "${CMAKE_CURRENT_LIST_DIR}/board/dts/board.dts" CACHE FILEPATH "板级 DTS (各平台覆盖)")
set(BOARD_DTSI_DIR      "${CMAKE_CURRENT_LIST_DIR}/board/dtsi" CACHE PATH "板级 dtsi (各平台覆盖)")
set(BOARD_DT_BINDINGS_DIR "${CMAKE_CURRENT_LIST_DIR}/board/dt-bindings")
set(DTC_LITE            "${CMAKE_CURRENT_LIST_DIR}/tools/dtc-lite.py")

set(GEN_SRCS
    "${GENERATED_BOARD_DIR}/board_devtable.c"
    "${GENERATED_BOARD_DIR}/board_probe.c"
)
set(GEN_HDRS
    "${GENERATED_BOARD_DIR}/board_nodes.h"
    "${GENERATED_BOARD_DIR}/board_devtable.h"
    "${GENERATED_BOARD_DIR}/board_handles.h"
    "${GENERATED_BOARD_DIR}/dt_config_gen.h"
)

file(MAKE_DIRECTORY "${GENERATED_BOARD_DIR}")
file(MAKE_DIRECTORY "${SCRUBBER_GEN_DIR}")

add_custom_command(
    OUTPUT  "${KCONFIG_OUT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${KCONFIG_GEN_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tools/genconfig.py"
            "${CMAKE_CURRENT_LIST_DIR}/Kconfig.non_esp"
            "${KCONFIG_GEN_DIR}"
            --config "${KCONFIG_DOT}"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/Kconfig.non_esp"
        "${CMAKE_CURRENT_LIST_DIR}/Kconfig.mini_tree"
        "${CMAKE_CURRENT_LIST_DIR}/tools/genconfig.py"
        "${KCONFIG_DOT}"
    COMMENT "Generating mini_tree config.h"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    VERBATIM
)

# 厂商头路径由板级 -DVENDOR_INC_DIRS / -DVENDOR_DEFINES 注入；中间件默认空
set(VENDOR_INC_DIRS "" CACHE STRING "厂商 HAL 头搜索路径")
set(VENDOR_DEFINES "" CACHE STRING "厂商设备宏")

set(DTC_LITE_ARGS "")
foreach(d ${VENDOR_INC_DIRS})
    list(APPEND DTC_LITE_ARGS "-I${d}")
endforeach()
foreach(d ${VENDOR_DEFINES})
    list(APPEND DTC_LITE_ARGS "-D${d}")
endforeach()

# USB 裁剪：关闭时不扫 bus/usb、vfs/usb 的 DRIVER_REGISTER，也不挂其 DEPENDS
set(USB_DTC_SCAN_DIRS "")
set(USB_DTC_DEPENDS "")
set(USB_LIB_SRCS "")
set(USB_INC_DIRS "")
if(MINI_TREE_USB)
    list(APPEND USB_DTC_SCAN_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/bus/usb"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/usb")
    list(APPEND USB_DTC_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/bus/usb/usb_bus.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/usb/vfs-usb.c")
    list(APPEND USB_LIB_SRCS
        vfs/usb/vfs-usb.h
        vfs/usb/vfs-usb.c)
    list(APPEND USB_INC_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/hal/usb"
        "${CMAKE_CURRENT_LIST_DIR}/bus/usb"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/usb")
endif()

add_custom_command(
    OUTPUT  ${GEN_SRCS} ${GEN_HDRS} "${_PRODUCT_DRV_USED_TXT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_BOARD_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
            ${DTC_LITE_ARGS}
            ${_PRODUCT_DRV_SRC_DIRS}
            "${CMAKE_CURRENT_LIST_DIR}/vfs/spi"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/i2c"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/tim"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/adc"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/dac"
            "${CMAKE_CURRENT_LIST_DIR}/bus/spi"
            "${CMAKE_CURRENT_LIST_DIR}/bus/uart"
            "${CMAKE_CURRENT_LIST_DIR}/bus/i2c"
            "${CMAKE_CURRENT_LIST_DIR}/bus/can"
            ${USB_DTC_SCAN_DIRS}
            "${CMAKE_CURRENT_LIST_DIR}/bus/i2s"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/can"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/rtc"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/iwdg"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/wwdg"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/i2s"
    DEPENDS
        "${DTC_LITE}"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/dts_ast.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/grammar.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/parser.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/compiler.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/generator.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/main.py"
        "${BOARD_DTS}"
        "${BOARD_DT_BINDINGS_DIR}/spi/spi-parameter.h"
        "${BOARD_DT_BINDINGS_DIR}/uart/uart-parameter.h"
        "${BOARD_DT_BINDINGS_DIR}/tim/tim-parameter.h"
        "${BOARD_DT_BINDINGS_DIR}/gpio/gpio-parameter.h"
        ${_PRODUCT_DRV_SRCS_REL}
        "${CMAKE_CURRENT_LIST_DIR}/vfs/spi/vfs-spi.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/uart/vfs-uart.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/i2c/vfs-i2c.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio/vfs-gpio.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/tim/vfs-tim.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/adc/vfs-adc.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/dac/vfs-dac.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/spi/spi_bus.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/uart/uart_bus.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/i2c/i2c_bus.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/can/can_bus.c"
        ${USB_DTC_DEPENDS}
        "${CMAKE_CURRENT_LIST_DIR}/vfs/can/vfs-can.c"
    COMMENT "Running dtc-lite on ${BOARD_DTS}"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${SCRUBBER_CRC_HDR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_LIST_DIR}/tools/system_scrubber_crc_stub.h"
            "${SCRUBBER_CRC_HDR}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tools/system_scrubber_crc_stub.h"
    COMMENT "Generating system_scrubber_crc_gen.h stub"
    VERBATIM
)

set(CORE_SRCS
    "core/src/mini_backend_bare.c"
    "core/src/mini_backend_freertos.c"
    "core/src/mini_backend_mini_os.c"
    "core/src/mini_backend_rtthread.c"
    "core/src/mini_panic.c"
    "core/src/mini_slot.c"
    "core/src/mini_time.c"
    "core/src/status.c"
    "mini-log/src/log.c"
    "mini-log/src/crc.c"
)
if(MINI_TREE_EVENT_BUS)
    list(APPEND CORE_SRCS "core/src/event_bus.c")
endif()

# 裸机可选: mini_malloc/calloc/free 走 mini-os 内存模块 (Kconfig CONFIG_OS_BARE_MINI_OS_MEM, 默认关)。
# 只编 lib/mini-os/src/memory.c 单文件 (无调度器/port 依赖), 不链整个 mini-os 内核;
# 堆区由板级链接脚本提供 __mini_os_heap_start/__mini_os_heap_end
# (可 INCLUDE lib/mini-os/mini-os-heap.ld)。
file(STRINGS "${KCONFIG_DOT}" CONFIG_OS_BARE_MINI_OS_MEM_ON REGEX "^CONFIG_OS_BARE_MINI_OS_MEM=y$")
if(MINI_OS_DEFINE STREQUAL "CONFIG_OS_BARE" AND CONFIG_OS_BARE_MINI_OS_MEM_ON)
    list(APPEND CORE_SRCS "lib/mini-os/src/memory.c")
endif()



add_library(mini_tree STATIC
    ${HAL_SRCS}
    "board/src/board_device.c"
    "board/src/dev_lifecycle.c"
    "board/src/board_driver.c"
    "board/src/bus.c"
    "board/src/config_store.c"
    "board/src/task_config.c"
    "board/src/task_utils.c"
    ${DRIVER_SRCS}
    ${GEN_SRCS}
    ${CORE_SRCS}
    ${SYSTEM_SRCS}
    "algorithm/buffer/circle_fifo_buffer.c"
    vfs/tim/vfs-tim.c
    vfs/can/vfs-can.h
    vfs/can/vfs-can.c
    vfs/adc/vfs-adc.h
    vfs/adc/vfs-adc.c
    vfs/rtc/vfs-rtc.h
    vfs/rtc/vfs-rtc.c
    ${USB_LIB_SRCS}
    vfs/i2s/vfs-i2s.c
    vfs/i2s/vfs-i2s.h
    algorithm/buffer/double_buffer.c
    vfs/dac/vfs-dac.h
    vfs/dac/vfs-dac.c
)

# VIRQ 中断上下部 — 仅 CONFIG_VIRQ 编入
if(MINI_TREE_VIRQ)
    target_sources(mini_tree PRIVATE interrupt/interrupt.c interrupt/interrupt.h)
endif()

# mini-ota (Kconfig: CONFIG_MINI_OTA) — 引导 + OTA 库, 见 lib/mini-ota/README.md
if(MINI_TREE_OTA)
    add_subdirectory(lib/mini-ota)
endif()
# 裸机调度器三态 (Kconfig choice: XTASK_NONE / XTASK_COOP / XTASK_PREEMPT):
#   - NONE   (MINI_TREE_XTASK=OFF):  不编入, 应用层自写 while 循环
#   - COOP   (PREEMPT=OFF):          编 xtask_coop.c (协调式 round-robin)
#   - PREEMPT(PREEMPT=ON):           编 xtask_preempt.c (抢占式, 实验性)
# COOP/PREEMPT 提供同一套对外 API (见 xtask.h), 调用方无感知; OS 后端用内核调度, 不编入.
if(MINI_OS_DEFINE STREQUAL "CONFIG_OS_BARE" AND MINI_TREE_XTASK)
    target_sources(mini_tree PRIVATE time_slice/task/xtask.h)
    if(MINI_TREE_XTASK_PREEMPT)
        target_sources(mini_tree PRIVATE time_slice/task/xtask_preempt.c)
        # xtask_preempt.c 顶部在 include 任何头之前即 #ifdef CONFIG_XTASK_PREEMPT,
        # 该宏只能经 config.h (compiler_compat.h 间接包含) 进入, 但 include 在判断之后,
        # 必须在此显式 -D 注入, 否则整个文件被跳过编译成空对象 (链接缺 x_scheduler_* 符号).
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/time_slice/task/xtask_preempt.c
            PROPERTIES COMPILE_DEFINITIONS CONFIG_XTASK_PREEMPT)
    else()
        target_sources(mini_tree PRIVATE time_slice/task/xtask_coop.c)
    endif()
endif()

target_include_directories(mini_tree PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/bus/include"
    "${CMAKE_CURRENT_LIST_DIR}/hal/spi"
    "${CMAKE_CURRENT_LIST_DIR}/hal/uart"
    "${CMAKE_CURRENT_LIST_DIR}/hal/i2c"
    "${CMAKE_CURRENT_LIST_DIR}/hal/can"
    "${CMAKE_CURRENT_LIST_DIR}/hal/rtc"
    "${CMAKE_CURRENT_LIST_DIR}/hal/i2s"
    "${CMAKE_CURRENT_LIST_DIR}/hal/iwdg"
    "${CMAKE_CURRENT_LIST_DIR}/hal/wwdg"
    "${CMAKE_CURRENT_LIST_DIR}/bus/spi"
    "${CMAKE_CURRENT_LIST_DIR}/bus/uart"
    "${CMAKE_CURRENT_LIST_DIR}/bus/i2c"
    "${CMAKE_CURRENT_LIST_DIR}/bus/can"
    "${CMAKE_CURRENT_LIST_DIR}/bus/i2s"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/spi"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/i2c"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/can"
    ${USB_INC_DIRS}
    "${CMAKE_CURRENT_LIST_DIR}/can_hook"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/rtc"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/i2s"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/iwdg"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/wwdg"
)

set_source_files_properties(${GEN_SRCS} PROPERTIES GENERATED TRUE)

target_include_directories(mini_tree
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/algorithm/buffer"
        "${CMAKE_CURRENT_LIST_DIR}/mini-log/inc"
        "${CMAKE_CURRENT_LIST_DIR}/board"
        "${CMAKE_CURRENT_LIST_DIR}/board/include"
        "${CMAKE_CURRENT_LIST_DIR}/board/define/vfs"
        "${CMAKE_CURRENT_LIST_DIR}/core/include"
        ${HAL_INCLUDE_DIRS}
        "${CMAKE_CURRENT_LIST_DIR}/interrupt"
        "${CMAKE_CURRENT_LIST_DIR}/system_c/include"
        "${CMAKE_CURRENT_LIST_DIR}/system_cpp/include"
        ${_PRODUCT_DRV_INC_DIRS}
        ${_PRODUCT_DRV_SRC_DIRS}
        "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/adc"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/tim"
        "${KCONFIG_GEN_DIR}"
        "${GENERATED_BOARD_DIR}"
        "${SCRUBBER_GEN_DIR}"
)

# time_slice 头路径仅裸机 + XTASK 暴露, 避免 OS 构建误包含 xtask.h (与 FreeRTOS xTaskCreate 同名冲突)
if(MINI_OS_DEFINE STREQUAL "CONFIG_OS_BARE" AND MINI_TREE_XTASK)
    target_include_directories(mini_tree PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/time_slice/task"
    )
endif()

# mini-os 内存模块头路径 (裸机 mini_malloc/calloc/free 的转发目标, 见 CONFIG_OS_BARE_MINI_OS_MEM);
# 仅在开关开启时暴露, 避免与仓内其它 memory.h 同名冲突。
if(MINI_OS_DEFINE STREQUAL "CONFIG_OS_BARE" AND CONFIG_OS_BARE_MINI_OS_MEM_ON)
    target_include_directories(mini_tree PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/lib/mini-os/inc"
    )
endif()



target_compile_definitions(mini_tree PUBLIC ${MINI_OS_DEFINE})
# 系统层唯一的 C++ 模块是 SystemCmd (命令分发器); 编入时对其施加 no-rtti/no-exceptions
# (嵌入式禁用异常与 RTTI, 与 ETL_NO_STL 一致)。COMPILE_LANGUAGE:CXX 保证只作用于 .cpp。
if(MINI_TREE_SYSTEM_CMD)
    if(MSVC)
        target_compile_options(mini_tree PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/GR->
            $<$<COMPILE_LANGUAGE:CXX>:/EHs-c->
        )
    else()
        target_compile_options(mini_tree PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
            $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        )
    endif()
endif()

# 第三方库：FreeRTOS / RT-Thread（按 .config）
add_subdirectory(lib)
target_link_libraries(mini_tree PUBLIC lib)

if(MSVC)
    target_compile_options(mini_tree PRIVATE /wd4100 /wd4101 /wd4102)
else()
    target_compile_options(mini_tree PRIVATE -Wno-unused-value -Wno-unused-variable)
endif()
# 上层 C++ 基础：ETL 默认进 mini_tree（SYSTEM_CPP / event_bus 等依赖）
mini_tree_link_etl(mini_tree)
add_custom_target(mini_tree_gen DEPENDS "${KCONFIG_OUT}" ${GEN_SRCS} ${GEN_HDRS} "${SCRUBBER_CRC_HDR}")
add_dependencies(mini_tree mini_tree_gen)

# TinyUSB: 仅 USB 使能时链接。usb_net_cb.c / usb_bus.c 需要 tusb.h 的
# include 路径 (lib/tinyusb/src), 且 TINYUSB_OS 与 OS 后端联动 (Kconfig select)。
if(MINI_TREE_USB)
    mini_tree_link_tinyusb(mini_tree)
endif()

# lwIP 移植层 (net/) 是否编入, 由 Kconfig 总开关 CONFIG_LWIP 桥接决定。
# 仿照仓库既有 file(STRINGS KCONFIG_DOT ...) 范式, 从 .config 提取开关,
# 避免直接引用 Kconfig 未回流到 CMake 的 CONFIG_* 宏。
file(STRINGS "${KCONFIG_DOT}" MINI_TREE_LWIP_ON REGEX "^CONFIG_LWIP=y$")
if(MINI_TREE_LWIP_ON)
    add_subdirectory(net)
    add_dependencies(mini_tree_net mini_tree_gen)

    target_link_libraries(mini_tree_net PUBLIC lib)
    target_link_libraries(mini_tree PUBLIC mini_tree_net)
endif()

# 由本地扩展提供并定义 CONFIG_UI_* 符号; 随仓只提交桥接头 ui/display/display_ui_bridge.h。
file(STRINGS "${KCONFIG_DOT}" CONFIG_UI_LVGL_ON REGEX "^CONFIG_UI_LVGL=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_UI_U8G2_ON REGEX "^CONFIG_UI_U8G2=y$")
if(CONFIG_UI_LVGL_ON OR CONFIG_UI_U8G2_ON)
    add_subdirectory(ui)
    add_dependencies(mini_tree_ui mini_tree_gen)
    target_link_libraries(mini_tree PUBLIC mini_tree_ui)
endif()

if(MINI_TREE_USB)
    target_link_libraries(mini_tree PUBLIC tinyusb)
endif()

if(MINI_TREE_OTA)
    # 让 mini-ota 的 boot_config.h 直接消费本工程 Kconfig 生成的 config.h (范式同
    # lib/mini-os): config.h 与 compiler_compat.h 必须都在 include 路径上, 否则
    # boot_config.h 会退回自带默认值, 菜单里的 OTA 参数不会生效。
    add_dependencies(mini_ota mini_tree_gen)
    target_include_directories(mini_ota SYSTEM PRIVATE
        "${KCONFIG_GEN_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/core/include"
    )
    # mini-ota 的 arch/arm/cortex-m/cortex_m.S 需 MINI_BOOT_Mx 选择核差异,
    # 由 Kconfig 平台 (PLATFORM_*) 派生; -mcpu/-mthumb 由工具链文件全局注入。
    file(STRINGS "${KCONFIG_DOT}" MINI_TREE_OTA_PLATFORM REGEX "^CONFIG_PLATFORM_")
    if(MINI_TREE_OTA_PLATFORM STREQUAL "CONFIG_PLATFORM_ARM_CM7=y")
        target_compile_definitions(arch PRIVATE MINI_BOOT_M7=1)
    elseif(MINI_TREE_OTA_PLATFORM STREQUAL "CONFIG_PLATFORM_ARM_CM4F=y")
        target_compile_definitions(arch PRIVATE MINI_BOOT_M4=1)
    elseif(MINI_TREE_OTA_PLATFORM STREQUAL "CONFIG_PLATFORM_ARM_CM3=y")
        target_compile_definitions(arch PRIVATE MINI_BOOT_M3=1)
    elseif(MINI_TREE_OTA_PLATFORM STREQUAL "CONFIG_PLATFORM_ARM_CM0=y")
        target_compile_definitions(arch PRIVATE MINI_BOOT_M0=1)
    endif()
    target_link_libraries(mini_tree PUBLIC mini_ota)
endif()
