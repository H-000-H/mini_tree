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

# USB 软编码：.config 显式 "# CONFIG_USB is not set" 才裁剪；缺省视为启用（对齐 Kconfig default y）。
# 启用时需板级 usb_tusb_port glue（docs/usb_tusb_port.md），并自行 REQUIRE esp_tinyusb 等。
file(STRINGS "${KCONFIG_DOT}" CONFIG_USB_OFF REGEX "^# CONFIG_USB is not set$")
if(CONFIG_USB_OFF)
    set(MINI_TREE_USB OFF)
else()
    set(MINI_TREE_USB ON)
endif()

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

# HAL stub 全量编入, 但每个 stub 文件内部已做 #if defined(ESP_PLATFORM) 屏蔽:
# ESP 构建下文件编译为空 (hal_* 由板级组件提供 strong 实现, 缺失直接链接报错,
# 杜绝静默 -ENOSYS); 非 ESP 构建保留 weak stub 兜底。
# 板级需覆盖的引用集 (随 DRIVER_SRCS/SYSTEM_SRCS 变化, 缺失时链接错误会指出):
#   外设 7: gpio/spi/uart/i2c/can/tim/adc  系统 5: iwdg/storage/flash/usb/platform_safety
# 注意: hal_cpu_secondary_startup 被 CONFIG_CPU_CORES>1 引用, CPU_CORES=1 时不链接;
# 设双核 AMP 前先确认板级已实现它。
set(HAL_SRCS
    "${MINI_TREE_DIR}/hal/gpio/hal_gpio.c"
    "${MINI_TREE_DIR}/hal/spi/hal_spi.c"
    "${MINI_TREE_DIR}/hal/uart/hal_uart.c"
    "${MINI_TREE_DIR}/hal/i2c/hal_i2c.c"
    "${MINI_TREE_DIR}/hal/can/hal_can.c"
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
    "${MINI_TREE_DIR}/hal/system/hal_systick.c"
    "${MINI_TREE_DIR}/hal/hal_if_dummy.c"
)

if(MINI_TREE_USB)
    list(APPEND HAL_SRCS "${MINI_TREE_DIR}/hal/usb/hal_usb.c")
endif()

if(MINI_TREE_SYSTEM)
    if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
        set(SYSTEM_SRCS
            "${MINI_TREE_DIR}/system_cpp/src/system_init.cpp"
            "${MINI_TREE_DIR}/system_cpp/src/system_scrubber.cpp"
            "${MINI_TREE_DIR}/system_cpp/src/system_wdt.cpp"
            "${MINI_TREE_DIR}/system_cpp/src/task_manager.cpp"
            "${MINI_TREE_DIR}/system_cpp/src/safe_state.c"
        )
        if(MINI_TREE_SYSTEM_CMD)
            list(APPEND SYSTEM_SRCS "${MINI_TREE_DIR}/system_cpp/src/system_cmd.cpp")
        endif()
    else()
        set(SYSTEM_SRCS
            "${MINI_TREE_DIR}/system_c/src/system_init.c"
            "${MINI_TREE_DIR}/system_c/src/system_scrubber.c"
            "${MINI_TREE_DIR}/system_c/src/system_wdt.c"
            "${MINI_TREE_DIR}/system_c/src/task_manager.c"
            "${MINI_TREE_DIR}/system_cpp/src/safe_state.c"
        )
    endif()
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
    "${MINI_TREE_DIR}/core/src/buffer_pool.c"
    "${MINI_TREE_DIR}/core/src/production_log.c"
    "${MINI_TREE_DIR}/core/src/printf_output.c"
)
if(MINI_TREE_EVENT_BUS)
    list(APPEND CORE_SRCS "${MINI_TREE_DIR}/core/src/event_bus.c")
endif()

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

if(MINI_TREE_USB)
    list(APPEND DRIVER_SRCS
        "${MINI_TREE_DIR}/bus/usb/usb_bus.c"
        "${MINI_TREE_DIR}/vfs/usb/vfs-usb.c")
endif()

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
if(MINI_TREE_USB)
    list(APPEND _DTC_SCAN_DIRS
        "${MINI_TREE_DIR}/bus/usb"
        "${MINI_TREE_DIR}/vfs/usb")
endif()
list(REMOVE_DUPLICATES _DTC_SCAN_DIRS)

# 压缩命令行长度（Windows CreateProcess 上限 8191 字符）：
# 把落在 MINI_TREE_DIR 下的扫描目录相对化，并在 add_custom_command 里设
# WORKING_DIRECTORY=${MINI_TREE_DIR}。板级/树外目录（如 EXTRA_SCAN_DIRS）保持绝对，
# 否则相对解析会指向 MINI_TREE_DIR 下不存在的路径。
set(_DTC_SCAN_DIRS_REL "")
foreach(_d IN LISTS _DTC_SCAN_DIRS)
    if(IS_ABSOLUTE "${_d}" AND "${_d}" MATCHES "^${MINI_TREE_DIR}/")
        file(RELATIVE_PATH _rel "${MINI_TREE_DIR}" "${_d}")
        list(APPEND _DTC_SCAN_DIRS_REL "${_rel}")
    else()
        list(APPEND _DTC_SCAN_DIRS_REL "${_d}")
    endif()
endforeach()

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
if(MINI_TREE_USB)
    list(APPEND _DTC_DEPENDS
        "${MINI_TREE_DIR}/bus/usb/usb_bus.c"
        "${MINI_TREE_DIR}/vfs/usb/vfs-usb.c")
endif()
list(REMOVE_DUPLICATES _DTC_DEPENDS)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# USB 裁剪：include 目录同样按 .config 门控
set(USB_INC_DIRS "")
if(MINI_TREE_USB)
    list(APPEND USB_INC_DIRS
        "${MINI_TREE_DIR}/hal/usb"
        "${MINI_TREE_DIR}/bus/usb"
        "${MINI_TREE_DIR}/vfs/usb")
endif()

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
        "${MINI_TREE_DIR}/board/define/vfs"
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
        ${USB_INC_DIRS}
        ${_ETL_INC}
        # 生成 board_* 必须先于 ide/stubs (stubs/board_nodes.h 的 DEV_ID_COUNT=1
        # 会盖住 dtc-lite 真表). KCONFIG_GEN(config.h 转发 sdkconfig.h) 亦须在
        # ide/stubs 之前: 保证 CONFIG_* 全部来自真实 sdkconfig.h, 不再依赖手写 stub.
        "${GENERATED_BOARD_DIR}"
        "${SCRUBBER_GEN_DIR}"
        "${KCONFIG_GEN_DIR}"
        "${MINI_TREE_DIR}/ide/stubs"
    # 覆盖默认 Kconfig 发现: 组件根 Kconfig (mainmenu 入口, 非 ESP 路径用) 带 mainmenu,
    # 会被 IDF 自动收录并与 Kconfig.projbuild 重复 source Kconfig.mini_tree 导致递归.
    # ESP 路径只走 Kconfig.projbuild (orsource Kconfig.mini_tree)。
    KCONFIG ""
    REQUIRES
        freertos
        esp_driver_gpio
        esp_driver_spi
        esp_driver_uart
        esp_driver_i2c
        esp_driver_twai
)

# ESP-IDF: CONFIG_* 已由 IDF 生成于 build/config/sdkconfig.h (pragma once)。
# 这里只放转发头满足 #include "config.h": 编译器与 clangd 拿到的都是真实配置,
# 不再依赖 ide/stubs/config.h 手写镜像 (曾导致 SYS_LOG 后端宏与 .config 脱节)。
add_custom_command(
    OUTPUT  "${KCONFIG_OUT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${KCONFIG_GEN_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo "#pragma once" > "${KCONFIG_OUT}"
    COMMAND "${CMAKE_COMMAND}" -E echo "/* ESP-IDF: CONFIG_* 全部来自 sdkconfig.h */" >> "${KCONFIG_OUT}"
    COMMAND "${CMAKE_COMMAND}" -E echo "#include \"sdkconfig.h\"" >> "${KCONFIG_OUT}"
    COMMENT "Generating mini_tree config.h forwarder to sdkconfig.h (ESP-IDF)"
    VERBATIM
)

add_custom_command(
    OUTPUT  ${GEN_SRCS} ${GEN_HDRS}
    WORKING_DIRECTORY "${MINI_TREE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_BOARD_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
            ${_DTC_SCAN_DIRS_REL}
            ${DTC_LITE_ARGS}
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
