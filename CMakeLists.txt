# mini_tree (generic CMake build for non-ESP-IDF projects)

if(ESP_PLATFORM)
    # 纯架构：不硬编码某一 MCU。板级通过下列之一注入 BOARD_DTS / dtc -I/-D / 树外扫描：
    #   1) MINI_TREE_BOARD_PORT（绝对路径）
    #   2) 同级 ../board_port.cmake（本 ESP 板工程默认）
    # 注意: component_get_requirements 为 script 模式，不可用 CONFIGURE_DEPENDS
    if(DEFINED MINI_TREE_BOARD_PORT AND EXISTS "${MINI_TREE_BOARD_PORT}")
        include("${MINI_TREE_BOARD_PORT}")
    elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../board_port.cmake")
        include("${CMAKE_CURRENT_LIST_DIR}/../board_port.cmake")
    endif()

    # 产品驱动在 mini_tree/drivers/*/（中间件内，与 MCU 无关）
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

file(STRINGS "${KCONFIG_DOT}" CONFIG_OSAL_ENTRY   REGEX "^CONFIG_OSAL_(FREERTOS|RTTHREAD|THREADX|UCOS_III|UCOS_II|NULL)=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_ENTRY REGEX "^CONFIG_SYSTEM_(C|CPP)=y$")

# System / EventBus 软编码：.config 显式 "# CONFIG_SYSTEM is not set" 才裁剪；缺省视为启用（对齐 Kconfig default y）
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_OFF REGEX "^# CONFIG_SYSTEM is not set$")
if(CONFIG_SYSTEM_OFF)
    set(MINI_TREE_SYSTEM OFF)
else()
    set(MINI_TREE_SYSTEM ON)
endif()
file(STRINGS "${KCONFIG_DOT}" CONFIG_EVENT_BUS_OFF REGEX "^# CONFIG_EVENT_BUS is not set$")
# EVENT_BUS 依赖 SYSTEM: .config 手写 y 但 SYSTEM=n 时按 Kconfig 语义视为关
if(CONFIG_EVENT_BUS_OFF OR NOT MINI_TREE_SYSTEM)
    set(MINI_TREE_EVENT_BUS OFF)
else()
    set(MINI_TREE_EVENT_BUS ON)
endif()
# SystemCmd: 默认关, 仅显式 "CONFIG_SYSTEM_CMD=y" 才编入
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_CMD_ON REGEX "^CONFIG_SYSTEM_CMD=y$")
# SYSTEM_CMD 仅 SYSTEM_CPP 后端有效
if(CONFIG_SYSTEM_CMD_ON AND CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
    set(MINI_TREE_SYSTEM_CMD ON)
else()
    set(MINI_TREE_SYSTEM_CMD OFF)
endif()

# USB 软编码：.config 显式 "# CONFIG_USB is not set" 才裁剪；缺省视为启用（对齐 Kconfig default y）
file(STRINGS "${KCONFIG_DOT}" CONFIG_USB_OFF REGEX "^# CONFIG_USB is not set$")
if(CONFIG_USB_OFF)
    set(MINI_TREE_USB OFF)
else()
    set(MINI_TREE_USB ON)
endif()
if(MINI_TREE_USB)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/tinyusb.cmake")
endif()

# VIRQ 软编码：.config 显式 "# CONFIG_VIRQ is not set" 才裁剪；缺省视为启用（对齐 Kconfig default y）
file(STRINGS "${KCONFIG_DOT}" CONFIG_VIRQ_OFF REGEX "^# CONFIG_VIRQ is not set$")
if(CONFIG_VIRQ_OFF)
    set(MINI_TREE_VIRQ OFF)
else()
    set(MINI_TREE_VIRQ ON)
endif()

# 裸机调度器三态 (choice: XTASK_NONE / XTASK_COOP / XTASK_PREEMPT)：
#   - 显式 "CONFIG_XTASK_COOP=y"  → 协调式
#   - 显式 "CONFIG_XTASK_PREEMPT=y" → 抢占式 (实验性)
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

# HAL 全部为 weak 空实现，板级强符号覆盖；中间件不绑任何厂商 SDK
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
    "hal/storage/hal_flash.c"
    "hal/storage/hal_storage.c"
    "hal/system/hal_platform_safety.c"
    "hal/system/hal_sdio.c"
    "hal/system/hal_systick.c"
    "hal/hal_if_dummy.c"
)

if(MINI_TREE_USB)
    list(APPEND HAL_SRCS "hal/usb/hal_usb.c")
endif()

if(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_FREERTOS=y")
    set(OSAL_SRCS "osal/src/osal_freertos.c")
    set(OSAL_DEFINE CONFIG_OSAL_FREERTOS)
elseif(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_RTTHREAD=y")
    set(OSAL_SRCS "osal/src/osal_rtthread.c")
    set(OSAL_DEFINE CONFIG_OSAL_RTTHREAD)
elseif(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_THREADX=y")
    set(OSAL_SRCS "osal/src/osal_threadx.c")
    set(OSAL_DEFINE CONFIG_OSAL_THREADX)
elseif(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_UCOS_III=y")
    set(OSAL_SRCS "osal/src/osal_ucos_iii.c")
    set(OSAL_DEFINE CONFIG_OSAL_UCOS_III)
elseif(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_UCOS_II=y")
    set(OSAL_SRCS "osal/src/osal_ucos_ii.c")
    set(OSAL_DEFINE CONFIG_OSAL_UCOS_II)
else()
    set(OSAL_SRCS "osal/src/osal_null.c")
    set(OSAL_DEFINE CONFIG_OSAL_NULL)
endif()

# C++ 任务创建封装 (osal_task.cpp) — 软编码: .config 显式 "# CONFIG_OSAL_NULL_TASK_CPP is not set" 才裁剪;
# 缺省视为启用 (对齐 Kconfig default y)。依赖 xtask 原生 API, 需 CONFIG_XTASK。
file(STRINGS "${KCONFIG_DOT}" CONFIG_OSAL_NULL_TASK_CPP_OFF REGEX "^# CONFIG_OSAL_NULL_TASK_CPP is not set$")
if(MINI_TREE_XTASK AND NOT CONFIG_OSAL_NULL_TASK_CPP_OFF)
    list(APPEND OSAL_SRCS "osal/src/osal_task.cpp")
endif()

if(MINI_TREE_SYSTEM)
    if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
        set(SYSTEM_SRCS
            "system_cpp/src/system_init.cpp"
            "system_cpp/src/system_scrubber.cpp"
            "system_cpp/src/system_wdt.cpp"
            "system_cpp/src/task_manager.cpp"
            "system_cpp/src/safe_state.c"
        )
        if(MINI_TREE_SYSTEM_CMD)
            list(APPEND SYSTEM_SRCS "system_cpp/src/system_cmd.cpp")
        endif()
    else()
        set(SYSTEM_SRCS
            "system_c/src/system_init.c"
            "system_c/src/system_scrubber.c"
            "system_c/src/system_wdt.c"
            "system_c/src/task_manager.c"
            "system_cpp/src/safe_state.c"
        )
    endif()
endif()


file(GLOB _PRODUCT_DRV_SRCS_REL
    "${CMAKE_CURRENT_LIST_DIR}/drivers/*/src/*.c")
file(GLOB _PRODUCT_DRV_INC_DIRS LIST_DIRECTORIES true
    "${CMAKE_CURRENT_LIST_DIR}/drivers/*/include")
file(GLOB _PRODUCT_DRV_SRC_DIRS LIST_DIRECTORIES true
    "${CMAKE_CURRENT_LIST_DIR}/drivers/*/src")

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
endif()

set(KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
set(KCONFIG_OUT     "${KCONFIG_GEN_DIR}/config.h")

set(GENERATED_BOARD_DIR "${CMAKE_BINARY_DIR}/generated/board/mini_tree")
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
            "${CMAKE_CURRENT_LIST_DIR}/Kconfig"
            "${KCONFIG_GEN_DIR}"
            --config "${KCONFIG_DOT}"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/Kconfig"
        "${CMAKE_CURRENT_LIST_DIR}/tools/genconfig.py"
        "${KCONFIG_DOT}"
    COMMENT "Generating mini_tree config.h"
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
    OUTPUT  ${GEN_SRCS} ${GEN_HDRS}
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
    "core/src/buffer_pool.c"
    "core/src/production_log.c"
    "core/src/printf_output.c"
)
if(MINI_TREE_EVENT_BUS)
    list(APPEND CORE_SRCS "core/src/event_bus.c")
endif()

add_library(mini_tree STATIC
    ${OSAL_SRCS}
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

# 裸机调度器三态 (Kconfig choice: XTASK_NONE / XTASK_COOP / XTASK_PREEMPT):
#   - NONE   (MINI_TREE_XTASK=OFF):  不编入, 应用层自写 while 循环
#   - COOP   (PREEMPT=OFF):          编 xtask_coop.c (协调式 round-robin)
#   - PREEMPT(PREEMPT=ON):           编 xtask_preempt.c (抢占式, 实验性)
# COOP/PREEMPT 提供同一套对外 API (见 xtask.h), 调用方无感知; OS 后端用内核调度, 不编入.
if(OSAL_DEFINE STREQUAL "CONFIG_OSAL_NULL" AND MINI_TREE_XTASK)
    target_sources(mini_tree PRIVATE time_slice/task/xtask.h)
    if(MINI_TREE_XTASK_PREEMPT)
        target_sources(mini_tree PRIVATE time_slice/task/xtask_preempt.c)
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
        "${CMAKE_CURRENT_LIST_DIR}/board"
        "${CMAKE_CURRENT_LIST_DIR}/board/include"
        "${CMAKE_CURRENT_LIST_DIR}/board/define/vfs"
        "${CMAKE_CURRENT_LIST_DIR}/core/include"
        ${HAL_INCLUDE_DIRS}
        "${CMAKE_CURRENT_LIST_DIR}/interrupt"
        "${CMAKE_CURRENT_LIST_DIR}/osal/include"
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
if(OSAL_DEFINE STREQUAL "CONFIG_OSAL_NULL" AND MINI_TREE_XTASK)
    target_include_directories(mini_tree PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/time_slice/task"
        "${CMAKE_CURRENT_LIST_DIR}/time_slice/time"
    )
endif()

target_compile_definitions(mini_tree PUBLIC ${OSAL_DEFINE})
if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
    target_compile_definitions(mini_tree PRIVATE CONFIG_SYSTEM_CPP)
    target_compile_options(mini_tree PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    )
endif()

# 第三方库：FreeRTOS / RT-Thread（按 .config）
add_subdirectory(lib)
target_link_libraries(mini_tree PUBLIC lib)

target_compile_options(mini_tree PRIVATE -Wno-unused-value)
# 上层 C++ 基础：ETL 默认进 mini_tree（SYSTEM_CPP / event_bus 等依赖）
mini_tree_link_etl(mini_tree)
add_custom_target(mini_tree_gen DEPENDS "${KCONFIG_OUT}" ${GEN_SRCS} ${GEN_HDRS} "${SCRUBBER_CRC_HDR}")
add_dependencies(mini_tree mini_tree_gen)
