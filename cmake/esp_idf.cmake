# ESP-IDF 组件入口（由根 CMakeLists.txt 在 ESP_PLATFORM 时 include）
# 全部用 MINI_TREE_DIR 绝对路径，避免 include 后相对路径落到 cmake/ 下。
#
# 板级可在 idf_component 之前设置（推荐经 ../board_port.cmake / MINI_TREE_BOARD_PORT）:
#   BOARD_DTS / BOARD_DTSI_DIR     — 板级设备树（默认占位）
#   MINI_TREE_DTC_EXTRA_SCAN_DIRS  — 额外 DRIVER_REGISTER 扫描目录列表
#   MINI_TREE_DTC_EXTRA_DEPENDS    — dtc-lite 额外 DEPENDS 文件列表
#   MINI_TREE_DTC_EXTRA_ARGS       — 芯片专用 -I/-D（勿把 SoC 路径写死进本文件）

get_filename_component(MINI_TREE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(KCONFIG_DOT "${MINI_TREE_DIR}/.config")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KCONFIG_DOT}")

file(STRINGS "${KCONFIG_DOT}" CONFIG_OSAL_ENTRY   REGEX "^CONFIG_OSAL_(FREERTOS|RTTHREAD|NULL)=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_ENTRY REGEX "^CONFIG_SYSTEM_(C|CPP)=y$")

include("${MINI_TREE_DIR}/hal/paths.cmake")
# paths.cmake 可能列出尚未建目录的 HAL 子路径；IDF 要求 INCLUDE_DIRS 必须存在
set(_HAL_INC_EXISTING "")
foreach(d ${HAL_INCLUDE_DIRS})
    if(IS_DIRECTORY "${d}")
        list(APPEND _HAL_INC_EXISTING "${d}")
    endif()
endforeach()
set(HAL_INCLUDE_DIRS ${_HAL_INC_EXISTING})

# ESP 强制 FreeRTOS OSAL（对接 IDF 内核）
set(OSAL_SRCS "${MINI_TREE_DIR}/osal/src/osal_freertos.c")
set(OSAL_DEFINE CONFIG_OSAL_FREERTOS)

set(HAL_SRCS
    "${MINI_TREE_DIR}/hal/gpio/hal_gpio.c"
    "${MINI_TREE_DIR}/hal/spi/hal_spi.c"
    "${MINI_TREE_DIR}/hal/uart/hal_uart.c"
    "${MINI_TREE_DIR}/hal/i2c/hal_i2c.c"
    "${MINI_TREE_DIR}/hal/can/hal_can.c"
    "${MINI_TREE_DIR}/hal/usb/hal_usb.c"
    "${MINI_TREE_DIR}/hal/rtc/hal_rtc.c"
    "${MINI_TREE_DIR}/hal/i2s/hal_i2s.c"
    "${MINI_TREE_DIR}/hal/iwdg/hal_iwdg.c"
    "${MINI_TREE_DIR}/hal/wwdg/hal_wwdg.c"
    "${MINI_TREE_DIR}/hal/adc/hal_adc.c"
    "${MINI_TREE_DIR}/hal/dac/hal_dac.c"
    "${MINI_TREE_DIR}/hal/tim/hal_tim.c"
    "${MINI_TREE_DIR}/hal/amp/hal_amp.c"
    "${MINI_TREE_DIR}/hal/storage/hal_flash.c"
    "${MINI_TREE_DIR}/hal/storage/hal_storage.c"
    "${MINI_TREE_DIR}/hal/system/hal_platform_safety.c"
    "${MINI_TREE_DIR}/hal/system/hal_sdio.c"
    "${MINI_TREE_DIR}/hal/hal_if_dummy.c"
)

if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
    set(SYSTEM_SRCS
        "${MINI_TREE_DIR}/system_cpp/src/system_cmd.cpp"
        "${MINI_TREE_DIR}/system_cpp/src/system_init.cpp"
        "${MINI_TREE_DIR}/system_cpp/src/system_scrubber.cpp"
        "${MINI_TREE_DIR}/system_cpp/src/system_wdt.cpp"
        "${MINI_TREE_DIR}/system_cpp/src/task_manager.cpp"
        "${MINI_TREE_DIR}/system_cpp/src/safe_state.c"
    )
else()
    set(SYSTEM_SRCS
        "${MINI_TREE_DIR}/system_c/src/system_init.c"
        "${MINI_TREE_DIR}/system_c/src/system_scrubber.c"
        "${MINI_TREE_DIR}/system_c/src/system_wdt.c"
        "${MINI_TREE_DIR}/system_c/src/task_manager.c"
        "${MINI_TREE_DIR}/system_cpp/src/safe_state.c"
    )
endif()

set(BOARD_SRCS
    "${MINI_TREE_DIR}/board/src/board_device.c"
    "${MINI_TREE_DIR}/board/src/dev_lifecycle.c"
    "${MINI_TREE_DIR}/board/src/board_driver.c"
    "${MINI_TREE_DIR}/board/src/bus.c"
    "${MINI_TREE_DIR}/board/src/config_store.c"
    "${MINI_TREE_DIR}/board/src/task_config.c"
    "${MINI_TREE_DIR}/board/src/task_utils.c"
)

set(CORE_SRCS
    "${MINI_TREE_DIR}/core/src/event_bus.c"
    "${MINI_TREE_DIR}/core/src/buffer_pool.c"
    "${MINI_TREE_DIR}/core/src/production_log.c"
    "${MINI_TREE_DIR}/core/src/printf_output.c"
)

# 总线/VFS + mini_tree/drivers/*/ 产品驱动（ws2812 仍在树外独立组件）
file(GLOB _PRODUCT_DRV_SRCS
    "${MINI_TREE_DIR}/drivers/*/src/*.c")
file(GLOB _PRODUCT_DRV_INC_DIRS LIST_DIRECTORIES true
    "${MINI_TREE_DIR}/drivers/*/include")
file(GLOB _PRODUCT_DRV_SRC_DIRS LIST_DIRECTORIES true
    "${MINI_TREE_DIR}/drivers/*/src")

set(DRIVER_SRCS
    ${_PRODUCT_DRV_SRCS}
    "${MINI_TREE_DIR}/vfs/gpio/vfs-gpio.c"
    "${MINI_TREE_DIR}/vfs/tim/vfs-tim.c"
    "${MINI_TREE_DIR}/vfs/adc/vfs-adc.c"
    "${MINI_TREE_DIR}/bus/spi/spi_bus.c"
    "${MINI_TREE_DIR}/bus/uart/uart_bus.c"
    "${MINI_TREE_DIR}/bus/i2c/i2c_bus.c"
    "${MINI_TREE_DIR}/bus/can/can_bus.c"
    "${MINI_TREE_DIR}/vfs/spi/vfs-spi.c"
    "${MINI_TREE_DIR}/vfs/uart/vfs-uart.c"
    "${MINI_TREE_DIR}/vfs/i2c/vfs-i2c.c"
    "${MINI_TREE_DIR}/vfs/can/vfs-can.c"
    "${MINI_TREE_DIR}/can_hook/can_hook.c"
    "${MINI_TREE_DIR}/algorithm/buffer/circle_fifo_buffer.c"
    "${MINI_TREE_DIR}/algorithm/buffer/double_buffer.c"
    "${MINI_TREE_DIR}/interrupt/interrupt.c"
)

set(GENERATED_BOARD_DIR "${CMAKE_BINARY_DIR}/generated/board/mini_tree")
set(KCONFIG_GEN_DIR     "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
set(SCRUBBER_GEN_DIR    "${CMAKE_BINARY_DIR}/generated/scrubber/mini_tree")
set(KCONFIG_OUT         "${KCONFIG_GEN_DIR}/config.h")
set(SCRUBBER_CRC_HDR    "${SCRUBBER_GEN_DIR}/system_scrubber_crc_gen.h")
# 板级可覆盖 BOARD_DTS；未设则用中间件占位
if(NOT DEFINED BOARD_DTS OR BOARD_DTS STREQUAL "")
    set(BOARD_DTS "${MINI_TREE_DIR}/board/dts/board.dts")
endif()
set(DTC_LITE            "${MINI_TREE_DIR}/tools/dtc-lite.py")

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
file(MAKE_DIRECTORY "${KCONFIG_GEN_DIR}")
file(MAKE_DIRECTORY "${SCRUBBER_GEN_DIR}")

# 仅挂跨芯片通用 IDF 头；芯片专用路径/宏由板级 MINI_TREE_DTC_EXTRA_ARGS 注入
set(DTC_LITE_ARGS "")
if(DEFINED ENV{IDF_PATH})
    set(_IDF "$ENV{IDF_PATH}")
    foreach(d
        "${_IDF}/components/esp_hal_gpio/include"
        "${_IDF}/components/esp_hal_uart/include"
        "${_IDF}/components/esp_hal_gpspi/include"
        "${_IDF}/components/esp_hal_i2c/include"
        "${_IDF}/components/soc/include"
        "${_IDF}/components/esp_common/include"
        "${_IDF}/components/esp_hw_support/include"
        "${_IDF}/components/hal/platform_port/include"
    )
        if(IS_DIRECTORY "${d}")
            list(APPEND DTC_LITE_ARGS "-I${d}")
        endif()
    endforeach()
endif()
if(DEFINED MINI_TREE_DTC_EXTRA_ARGS)
    list(APPEND DTC_LITE_ARGS ${MINI_TREE_DTC_EXTRA_ARGS})
endif()

# 默认 DRIVER_REGISTER 扫描目录（中间件 + drivers/*/src；树外用 EXTRA）
set(_DTC_SCAN_DIRS
    ${_PRODUCT_DRV_SRC_DIRS}
    "${MINI_TREE_DIR}/vfs/spi"
    "${MINI_TREE_DIR}/vfs/gpio"
    "${MINI_TREE_DIR}/vfs/tim"
    "${MINI_TREE_DIR}/vfs/adc"
    "${MINI_TREE_DIR}/vfs/uart"
    "${MINI_TREE_DIR}/vfs/i2c"
    "${MINI_TREE_DIR}/vfs/can"
    "${MINI_TREE_DIR}/bus/spi"
    "${MINI_TREE_DIR}/bus/uart"
    "${MINI_TREE_DIR}/bus/i2c"
    "${MINI_TREE_DIR}/bus/can"
)
if(DEFINED MINI_TREE_DTC_EXTRA_SCAN_DIRS)
    list(APPEND _DTC_SCAN_DIRS ${MINI_TREE_DTC_EXTRA_SCAN_DIRS})
endif()
list(REMOVE_DUPLICATES _DTC_SCAN_DIRS)

# dtc-lite DEPENDS：BOARD_DTS + 板级 dtsi（若设 BOARD_DTSI_DIR）+ 可选 SoC 片段
set(_DTC_DEPENDS
    "${DTC_LITE}"
    "${MINI_TREE_DIR}/tools/dtc_lite/generator.py"
    "${BOARD_DTS}"
)
if(DEFINED BOARD_DTSI_DIR AND IS_DIRECTORY "${BOARD_DTSI_DIR}")
    file(GLOB _BOARD_DTSI_FILES "${BOARD_DTSI_DIR}/*.dtsi")
    list(APPEND _DTC_DEPENDS ${_BOARD_DTSI_FILES})
endif()
if(DEFINED MINI_TREE_DTC_EXTRA_DEPENDS)
    list(APPEND _DTC_DEPENDS ${MINI_TREE_DTC_EXTRA_DEPENDS})
endif()
list(REMOVE_DUPLICATES _DTC_DEPENDS)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ETL：优先板级 FetchContent / managed；若本地 lib/etl 存在则加入 include（兑底）
set(_ETL_INC "")
if(IS_DIRECTORY "${MINI_TREE_DIR}/lib/etl/include")
    set(_ETL_INC "${MINI_TREE_DIR}/lib/etl/include")
endif()

idf_component_register(
    SRCS
        ${OSAL_SRCS}
        ${HAL_SRCS}
        ${BOARD_SRCS}
        ${CORE_SRCS}
        ${DRIVER_SRCS}
        ${SYSTEM_SRCS}
        ${GEN_SRCS}
    INCLUDE_DIRS
        "${MINI_TREE_DIR}"
        "${MINI_TREE_DIR}/board/include"
        "${MINI_TREE_DIR}/board"
        "${MINI_TREE_DIR}/core/include"
        "${MINI_TREE_DIR}/osal/include"
        "${MINI_TREE_DIR}/system_c/include"
        "${MINI_TREE_DIR}/system_cpp/include"
        "${MINI_TREE_DIR}/algorithm/buffer"
        "${MINI_TREE_DIR}/interrupt"
        ${_PRODUCT_DRV_INC_DIRS}
        ${_PRODUCT_DRV_SRC_DIRS}
        "${MINI_TREE_DIR}/vfs/gpio"
        "${MINI_TREE_DIR}/vfs/tim"
        "${MINI_TREE_DIR}/vfs/adc"
        "${MINI_TREE_DIR}/vfs/uart"
        "${MINI_TREE_DIR}/vfs/spi"
        "${MINI_TREE_DIR}/vfs/i2c"
        "${MINI_TREE_DIR}/vfs/can"
        "${MINI_TREE_DIR}/can_hook"
        "${MINI_TREE_DIR}/bus/spi"
        "${MINI_TREE_DIR}/bus/uart"
        "${MINI_TREE_DIR}/bus/i2c"
        "${MINI_TREE_DIR}/bus/can"
        "${MINI_TREE_DIR}/hal/spi"
        "${MINI_TREE_DIR}/hal/uart"
        "${MINI_TREE_DIR}/hal/i2c"
        "${MINI_TREE_DIR}/hal/can"
        "${MINI_TREE_DIR}/hal/tim"
        "${MINI_TREE_DIR}/hal/adc"
        ${HAL_INCLUDE_DIRS}
        ${_ETL_INC}
        # 生成 board_* 必须先于 ide/stubs (stubs/board_nodes.h 的 DEV_ID_COUNT=1
        # 会盖住 dtc-lite 真表). 但 stubs/config.h 须先于空壳 KCONFIG_GEN
        # (否则 SYS_LOG 后端宏丢失).
        "${GENERATED_BOARD_DIR}"
        "${SCRUBBER_GEN_DIR}"
        "${MINI_TREE_DIR}/ide/stubs"
        "${KCONFIG_GEN_DIR}"
    REQUIRES
        freertos
        esp_driver_gpio
        esp_driver_spi
        esp_driver_uart
        esp_driver_i2c
        esp_driver_twai
)

# ESP-IDF: CONFIG_* 已由 sdkconfig.h 注入。再生成一份完整 config.h 会 -Werror=redefined。
# 这里放空壳，满足 #include "config.h"。
add_custom_command(
    OUTPUT  "${KCONFIG_OUT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${KCONFIG_GEN_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo "/* ESP-IDF: use sdkconfig.h for CONFIG_* */" > "${KCONFIG_OUT}"
    COMMENT "Generating mini_tree config.h stub (ESP-IDF)"
    VERBATIM
)

add_custom_command(
    OUTPUT  ${GEN_SRCS} ${GEN_HDRS}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_BOARD_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
            ${DTC_LITE_ARGS}
            ${_DTC_SCAN_DIRS}
    DEPENDS ${_DTC_DEPENDS}
    COMMENT "Running dtc-lite on board.dts"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${SCRUBBER_CRC_HDR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${MINI_TREE_DIR}/tools/system_scrubber_crc_stub.h"
            "${SCRUBBER_CRC_HDR}"
    DEPENDS "${MINI_TREE_DIR}/tools/system_scrubber_crc_stub.h"
    COMMENT "Generating system_scrubber_crc_gen.h stub"
    VERBATIM
)

add_custom_target(mini_tree_kconfig_gen DEPENDS "${KCONFIG_OUT}")
add_custom_target(mini_tree_board_dts_gen DEPENDS ${GEN_SRCS} ${GEN_HDRS})
add_custom_target(mini_tree_scrubber_crc_gen DEPENDS "${SCRUBBER_CRC_HDR}")
add_dependencies(${COMPONENT_LIB}
    mini_tree_kconfig_gen mini_tree_board_dts_gen mini_tree_scrubber_crc_gen)

set_source_files_properties(${GEN_SRCS} PROPERTIES GENERATED TRUE)

target_compile_definitions(${COMPONENT_LIB} PUBLIC ${OSAL_DEFINE} ETL_NO_STL)
if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_SYSTEM_CPP)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    )
endif()

if(EXISTS "${MINI_TREE_DIR}/error_symbols.ld")
    target_linker_script(${COMPONENT_LIB} INTERFACE "${MINI_TREE_DIR}/error_symbols.ld")
endif()
