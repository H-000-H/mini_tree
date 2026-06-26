# mini_tree — 纯中间件库（架构无关）
#
# 设计目标：仅包含架构无关的中间件代码。HAL 实现、板级 DTS、设备驱动
# 由具体项目（CH32V307 / STM32F407ZGT6 / ESP32-S3）集成时通过变量提供。
#
# 集成示例（具体项目 CMakeLists.txt）：
#   set(BOARD_DTS  "${CMAKE_CURRENT_LIST_DIR}/board/dts/xxx.dts")
#   set(HAL_SRCS   hal/gpio/hal_gpio_xxx.c hal/cpu/hal_cpu_delay_xxx.c)
#   add_subdirectory(mini_tree)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/etl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/hal/paths.cmake")

set(KCONFIG_DOT "${CMAKE_CURRENT_LIST_DIR}/.config")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KCONFIG_DOT}")

# ---- OSAL 选择 ----
file(STRINGS "${KCONFIG_DOT}" CONFIG_OSAL_ENTRY   REGEX "^CONFIG_OSAL_[A-Z_]+=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_ENTRY REGEX "^CONFIG_SYSTEM_(C|CPP)=y$")

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

# ---- system_c / system_cpp 选择 ----
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

# ---- HAL 实现 / 板级 DTS 由具体项目提供 ----
# HAL_SRCS  : 具体芯片的 HAL 实现源文件列表（hal_gpio_xxx.c 等）
# BOARD_DTS : 具体板级 DTS 文件路径（用于 dtc-lite 生成设备表）
if(NOT DEFINED HAL_SRCS)
    set(HAL_SRCS "")
endif()

# ---- kconfig 生成 ----
set(KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
set(KCONFIG_OUT     "${KCONFIG_GEN_DIR}/config.h")

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

# ---- dtc-lite 板级设备表生成（仅当具体项目提供 BOARD_DTS）----
set(GENERATED_BOARD_DIR "${CMAKE_BINARY_DIR}/generated/board/mini_tree")
set(SCRUBBER_GEN_DIR    "${CMAKE_BINARY_DIR}/generated/scrubber/mini_tree")
set(SCRUBBER_CRC_HDR    "${SCRUBBER_GEN_DIR}/system_scrubber_crc_gen.h")
set(DTC_LITE            "${CMAKE_CURRENT_LIST_DIR}/tools/dtc-lite.py")

file(MAKE_DIRECTORY "${GENERATED_BOARD_DIR}")
file(MAKE_DIRECTORY "${SCRUBBER_GEN_DIR}")

set(GEN_SRCS "")
set(GEN_HDRS "")
set(BOARD_DEVICE_SRC "")
if(BOARD_DTS)
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
    set(BOARD_DEVICE_SRC "board/src/board_device.c")
    add_custom_command(
        OUTPUT  ${GEN_SRCS} ${GEN_HDRS}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_BOARD_DIR}"
        COMMAND "${Python3_EXECUTABLE}" "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
        DEPENDS
            "${DTC_LITE}"
            "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/ast.py"
            "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/lexer.py"
            "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/parser.py"
            "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/compiler.py"
            "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/generator.py"
            "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/main.py"
            "${BOARD_DTS}"
        COMMENT "Running dtc-lite on ${BOARD_DTS}"
        VERBATIM
    )
endif()

add_custom_command(
    OUTPUT  "${SCRUBBER_CRC_HDR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_LIST_DIR}/tools/system_scrubber_crc_stub.h"
            "${SCRUBBER_CRC_HDR}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tools/system_scrubber_crc_stub.h"
    COMMENT "Generating system_scrubber_crc_gen.h stub"
    VERBATIM
)

# ---- 中间件源文件（架构无关）----
set(MIDDLEWARE_SRCS
    ${OSAL_SRCS}
    ${HAL_SRCS}
    "hal/hal_if_dummy.c"
    "hal/cpu/hal_cpu_amp.c"
    "hal/pwm/hal_pwm.c"
    ${BOARD_DEVICE_SRC}
    "board/src/dev_lifecycle.c"
    "board/src/board_driver.c"
    "board/src/bus.c"
    "board/src/config_store.c"
    "board/src/task_config.c"
    "board/src/task_utils.c"
    "bus/dma/dma_core.c"
    "bus/spi/spi_bus.c"
    "bus/uart/uart_bus.c"
    "vfs/gpio/vfs-gpio.c"
    "vfs/spi/spi_vfs.c"
    "vfs/uart/uart_vfs.c"
    "core/src/event_bus.c"
    "core/src/buffer_pool.c"
    "core/src/production_log.c"
    "core/src/printf_output.c"
    ${SYSTEM_SRCS}
    "algorithm/buffer/circle_fifo_buffer.c"
)

add_library(mini_tree STATIC ${MIDDLEWARE_SRCS} ${GEN_SRCS})

target_include_directories(mini_tree PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/bus/dma"
    "${CMAKE_CURRENT_LIST_DIR}/bus/include"
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
        "${CMAKE_CURRENT_LIST_DIR}/board/dt-bindings/gpio"
        "${CMAKE_CURRENT_LIST_DIR}/board/include"
        "${CMAKE_CURRENT_LIST_DIR}/core/include"
        ${HAL_INCLUDE_DIRS}
        "${CMAKE_CURRENT_LIST_DIR}/osal/include"
        "${CMAKE_CURRENT_LIST_DIR}/system_c/include"
        "${CMAKE_CURRENT_LIST_DIR}/system_cpp/include"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
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

mini_tree_link_etl(mini_tree)
add_custom_target(mini_tree_gen DEPENDS "${KCONFIG_OUT}" ${GEN_SRCS} ${GEN_HDRS} "${SCRUBBER_CRC_HDR}")
add_dependencies(mini_tree mini_tree_gen)
