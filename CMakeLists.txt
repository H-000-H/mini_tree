# mini_tree (generic CMake build for non-ESP-IDF projects)
# 目录/源文件布局对齐 ESP32-S3/components/mini_tree 基准

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

file(STRINGS "${KCONFIG_DOT}" CONFIG_OSAL_ENTRY   REGEX "^CONFIG_OSAL_(FREERTOS|RTTHREAD|NULL)=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_ENTRY REGEX "^CONFIG_SYSTEM_(C|CPP)=y$")

# HAL 全部为 weak 空实现，板级强符号覆盖；中间件不绑任何厂商 SDK
set(HAL_SRCS
    "hal/gpio/hal_gpio.c"
    "hal/spi/hal_spi.c"
    "hal/uart/hal_uart.c"
    "hal/i2c/hal_i2c.c"
    "hal/can/hal_can.c"
    "hal/usb/hal_usb.c"
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
    "hal/hal_if_dummy.c"
)

if(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_FREERTOS=y")
    set(OSAL_SRCS "osal/src/osal_freertos.c")
    set(OSAL_DEFINE CONFIG_OSAL_FREERTOS)
elseif(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_RTTHREAD=y")
    set(OSAL_SRCS "osal/src/osal_rtthread.c")
    set(OSAL_DEFINE CONFIG_OSAL_RTTHREAD)
else()
    set(OSAL_SRCS "osal/src/osal_null.c")
    set(OSAL_DEFINE CONFIG_OSAL_NULL)
endif()

if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
    set(SYSTEM_SRCS
        "system_cpp/src/system_cmd.cpp"
        "system_cpp/src/system_init.cpp"
        "system_cpp/src/system_scrubber.cpp"
        "system_cpp/src/system_wdt.cpp"
        "system_cpp/src/task_manager.cpp"
        "system_cpp/src/safe_state.c"
    )
else()
    set(SYSTEM_SRCS
        "system_c/src/system_init.c"
        "system_c/src/system_scrubber.c"
        "system_c/src/system_wdt.c"
        "system_c/src/task_manager.c"
        "system_cpp/src/safe_state.c"
    )
endif()


set(DRIVER_SRCS
    "drivers/flash/w25q64_spi_drv.c"
    "vfs/gpio/vfs-gpio.c"
    "bus/spi/spi_bus.c"
    "bus/uart/uart_bus.c"
    "bus/i2c/i2c_bus.c"
    "bus/can/can_bus.c"
    "bus/usb/usb_bus.c"
    "bus/i2s/i2s_bus.c"
    "can_hook/can_hook.c"
    "vfs/spi/vfs-spi.c"
    "vfs/uart/vfs-uart.c"
    "vfs/i2c/vfs-i2c.c"
    "vfs/iwdg/vfs-iwdg.c"
    "vfs/wwdg/vfs-wwdg.c"
)

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

add_custom_command(
    OUTPUT  ${GEN_SRCS} ${GEN_HDRS}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_BOARD_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
            ${DTC_LITE_ARGS}
            "${CMAKE_CURRENT_LIST_DIR}/drivers/flash"
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
            "${CMAKE_CURRENT_LIST_DIR}/bus/usb"
            "${CMAKE_CURRENT_LIST_DIR}/bus/i2s"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/can"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/usb"
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
        "${CMAKE_CURRENT_LIST_DIR}/drivers/flash/w25q64_spi_drv.c"
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
        "${CMAKE_CURRENT_LIST_DIR}/bus/usb/usb_bus.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/can/vfs-can.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/usb/vfs-usb.c"
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
    "core/src/event_bus.c"
    "core/src/buffer_pool.c"
    "core/src/production_log.c"
    "core/src/printf_output.c"
    ${SYSTEM_SRCS}
    "algorithm/buffer/circle_fifo_buffer.c"
    vfs/tim/vfs-tim.c
    vfs/can/vfs-can.h
    vfs/can/vfs-can.c
    vfs/adc/vfs-adc.h
    vfs/adc/vfs-adc.c
    vfs/rtc/vfs-rtc.h
    vfs/rtc/vfs-rtc.c
    vfs/usb/vfs-usb.h
    vfs/usb/vfs-usb.c
    vfs/i2s/vfs-i2s.c
    vfs/i2s/vfs-i2s.h
    algorithm/buffer/double_buffer.c
    interrupt/interrupt.c
    interrupt/interrupt.h
    vfs/dac/vfs-dac.h
    vfs/dac/vfs-dac.c
)

# 裸机时间片调度器 — 仅 CONFIG_OSAL_NULL; OS 后端用内核调度, 不编入
if(OSAL_DEFINE STREQUAL "CONFIG_OSAL_NULL")
    target_sources(mini_tree PRIVATE
        time_slice/task/xtask.h
        time_slice/task/xtask.c
    )
endif()

target_include_directories(mini_tree PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/bus/include"
    "${CMAKE_CURRENT_LIST_DIR}/hal/spi"
    "${CMAKE_CURRENT_LIST_DIR}/hal/uart"
    "${CMAKE_CURRENT_LIST_DIR}/hal/i2c"
    "${CMAKE_CURRENT_LIST_DIR}/hal/can"
    "${CMAKE_CURRENT_LIST_DIR}/hal/usb"
    "${CMAKE_CURRENT_LIST_DIR}/hal/rtc"
    "${CMAKE_CURRENT_LIST_DIR}/hal/i2s"
    "${CMAKE_CURRENT_LIST_DIR}/hal/iwdg"
    "${CMAKE_CURRENT_LIST_DIR}/hal/wwdg"
    "${CMAKE_CURRENT_LIST_DIR}/bus/spi"
    "${CMAKE_CURRENT_LIST_DIR}/bus/uart"
    "${CMAKE_CURRENT_LIST_DIR}/bus/i2c"
    "${CMAKE_CURRENT_LIST_DIR}/bus/can"
    "${CMAKE_CURRENT_LIST_DIR}/bus/usb"
    "${CMAKE_CURRENT_LIST_DIR}/bus/i2s"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/spi"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/i2c"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/can"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/usb"
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
        "${CMAKE_CURRENT_LIST_DIR}/core/include"
        ${HAL_INCLUDE_DIRS}
        "${CMAKE_CURRENT_LIST_DIR}/interrupt"
        "${CMAKE_CURRENT_LIST_DIR}/osal/include"
        "${CMAKE_CURRENT_LIST_DIR}/system_c/include"
        "${CMAKE_CURRENT_LIST_DIR}/system_cpp/include"
        "${CMAKE_CURRENT_LIST_DIR}/drivers/flash"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/adc"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/tim"
        "${KCONFIG_GEN_DIR}"
        "${GENERATED_BOARD_DIR}"
        "${SCRUBBER_GEN_DIR}"
)

# time_slice 头路径仅裸机暴露, 避免 OS 构建误包含 xtask.h (与 FreeRTOS xTaskCreate 同名冲突)
if(OSAL_DEFINE STREQUAL "CONFIG_OSAL_NULL")
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

# 第三方库：FreeRTOS / RT-Thread（按 .config）+ TinyUSB
add_subdirectory(lib)
target_link_libraries(mini_tree PUBLIC lib)

# TinyUSB：板级自行链接 DCD / CFG_TUSB_MCU，中间件不绑 MCU

target_compile_options(mini_tree PRIVATE -Wno-unused-value)
# 上层 C++ 基础：ETL 默认进 mini_tree（SYSTEM_CPP / event_bus 等依赖）
mini_tree_link_etl(mini_tree)
add_custom_target(mini_tree_gen DEPENDS "${KCONFIG_OUT}" ${GEN_SRCS} ${GEN_HDRS} "${SCRUBBER_CRC_HDR}")
add_dependencies(mini_tree mini_tree_gen)
