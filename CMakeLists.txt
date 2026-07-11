# mini_tree — 平台无关中间件 (generic CMake build)
# 目录/源文件布局对齐各平台 components/mini_tree 基准

find_package(Python3 REQUIRED COMPONENTS Interpreter)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/etl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/hal/paths.cmake")

set(KCONFIG_DOT "${CMAKE_CURRENT_LIST_DIR}/.config")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KCONFIG_DOT}")

file(STRINGS "${KCONFIG_DOT}" CONFIG_OSAL_ENTRY   REGEX "^CONFIG_OSAL_[A-Z_]+=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_ENTRY REGEX "^CONFIG_SYSTEM_(C|CPP)=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_HAL_GPIO_ENTRY
     REGEX "^CONFIG_HAL_GPIO_(STM32|CH32|ESP32)=y$")

if(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_STM32=y")
    set(HAL_GPIO_SRC "hal/gpio/hal_gpio_stm32.c")
    set(HAL_PLATFORM_SRCS
        "hal/system/hal_stm32f407.c"
    )
    set(NEW_ARCH_SRCS
        "hal/spi/hal_spi_stm32.c"
        "hal/uart/hal_uart_stm32.c"
    )
elseif(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_CH32=y")
    set(HAL_GPIO_SRC "hal/gpio/hal_gpio_ch32.c")
else()
    set(HAL_GPIO_SRC "hal/gpio/hal_gpio_esp32.c")
    set(HAL_PLATFORM_SRCS "")
    set(NEW_ARCH_SRCS "")
endif()

if(NOT DEFINED HAL_PLATFORM_SRCS)
    set(HAL_PLATFORM_SRCS "")
endif()

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
        "system_cpp/src/lifecycle.cpp"
        "system_cpp/src/system_cmd.cpp"
        "system_cpp/src/system_init.cpp"
        "system_cpp/src/system_runtime.cpp"
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

set(HAL_SRCS
    ${HAL_GPIO_SRC}
    ${HAL_PLATFORM_SRCS}
    ${NEW_ARCH_SRCS}
    "hal/amp/hal_amp.c"
    "hal/hal_if_dummy.c"
)

set(DRIVER_SRCS
    "drivers/flash/w25q64_spi_drv.c"
    "vfs/gpio/vfs-gpio.c"
    "bus/spi/spi_bus.c"
    "bus/uart/uart_bus.c"
    "vfs/spi/vfs-spi.c"
    "vfs/uart/vfs-uart.c"
)

set(KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
set(KCONFIG_OUT     "${KCONFIG_GEN_DIR}/config.h")

set(GENERATED_BOARD_DIR "${CMAKE_BINARY_DIR}/generated/board/mini_tree")
set(SCRUBBER_GEN_DIR    "${CMAKE_BINARY_DIR}/generated/scrubber/mini_tree")
set(SCRUBBER_CRC_HDR    "${SCRUBBER_GEN_DIR}/system_scrubber_crc_gen.h")
set(BOARD_DTS           "${CMAKE_CURRENT_LIST_DIR}/board/dts/board.dts" CACHE FILEPATH "板级 DTS 入口 (各平台自行覆盖)")
set(BOARD_DTSI_DIR      "${CMAKE_CURRENT_LIST_DIR}/board/dtsi" CACHE PATH "板级 dtsi 目录 (各平台自行覆盖)")
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

# 厂商 HAL 头搜索路径与设备选择宏: 让 dtsi 里 #include <xxx_ll_*.h>
# 能被 cpp 解析 (dtc-lite 通用 -I / -D 驱动, 无需在 py 里写厂商发现逻辑)
# 各平台在使用时通过 -DVENDOR_INC_DIRS=... -DVENDOR_DEFINES=... 覆盖
set(VENDOR_INC_DIRS "" CACHE PATH "厂商 HAL 头搜索路径 (各平台自行覆盖)")
set(VENDOR_DEFINES "" CACHE STRING "厂商设备选择宏 (各平台自行覆盖)")

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
            "${CMAKE_CURRENT_LIST_DIR}/vfs/tim"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/adc"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/dac"
            "${CMAKE_CURRENT_LIST_DIR}/bus/spi"
            "${CMAKE_CURRENT_LIST_DIR}/bus/uart"
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
        "${CMAKE_CURRENT_LIST_DIR}/drivers/flash/w25q64_spi_drv.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/spi/vfs-spi.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/uart/vfs-uart.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio/vfs-gpio.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/tim/vfs-tim.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/adc/vfs-adc.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/dac/vfs-dac.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/spi/spi_bus.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/uart/uart_bus.c"
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
    hal/tim/hal_tim.c
    vfs/tim/vfs-tim.c
    vfs/i2c/vfs-i2c.c
    vfs/i2c/vfs-i2c.h
    vfs/can/vfs-can.h
    vfs/can/vfs-can.c
    vfs/adc/vfs-adc.h
    vfs/adc/vfs-adc.c
    vfs/rtc/vfs-rtc.h
    vfs/rtc/vfs-rtc.c
    vfs/usb/vfs-usb.c
    vfs/i2s/vfs-i2s.c
    vfs/i2s/vfs-i2s.h
    hal/adc/hal_adc.c
    hal/dac/hal_dac.c
    algorithm/buffer/double_buffer.c
    interrupt/interrupt.c
    interrupt/interrupt.h
    vfs/dac/vfs-dac.h
    vfs/dac/vfs-dac.c
    time_slice/task/xtask.h
    time_slice/task/xtask.c
)

target_include_directories(mini_tree PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/bus/include"
    "${CMAKE_CURRENT_LIST_DIR}/hal/spi"
    "${CMAKE_CURRENT_LIST_DIR}/hal/uart"
    "${CMAKE_CURRENT_LIST_DIR}/bus/spi"
    "${CMAKE_CURRENT_LIST_DIR}/bus/uart"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/spi"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
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
        "${CMAKE_CURRENT_LIST_DIR}/time_slice/task"
        "${CMAKE_CURRENT_LIST_DIR}/time_slice/time"
        "${KCONFIG_GEN_DIR}"
        "${GENERATED_BOARD_DIR}"
        "${SCRUBBER_GEN_DIR}"
)

target_compile_definitions(mini_tree PRIVATE ${OSAL_DEFINE})
if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
    target_compile_definitions(mini_tree PRIVATE CONFIG_SYSTEM_CPP)
    target_compile_options(mini_tree PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    )
endif()
# 各平台在使用时通过 -DPLATFORM_VENDOR_LIB=xxx -DPLATFORM_VENDOR_DEFINE=xxx 覆盖
if(DEFINED PLATFORM_VENDOR_LIB)
    target_link_libraries(mini_tree PUBLIC ${PLATFORM_VENDOR_LIB})
endif()
if(DEFINED PLATFORM_VENDOR_DEFINE)
    target_compile_definitions(mini_tree PRIVATE ${PLATFORM_VENDOR_DEFINE})
endif()

target_compile_options(mini_tree PRIVATE -Wno-unused-value)
mini_tree_link_etl(mini_tree)
add_custom_target(mini_tree_gen DEPENDS "${KCONFIG_OUT}" ${GEN_SRCS} ${GEN_HDRS} "${SCRUBBER_CRC_HDR}")
add_dependencies(mini_tree mini_tree_gen)
