# ==============================================================================
# board — 板级支持 (设备树、驱动注册、配置存储、VFS)
# ==============================================================================
# 设备树布局 (Linux 写法; dts / dtsi 分目录):
#   board/dts/stm32f407zgt6.dts       — 板级入口 (#include 头 + / { } + &label)
#   board/dtsi/stm32f407.dtsi         — SoC 根 / { compatible, soc: soc { simple-bus } }
#   board/dtsi/stm32f407-spi.dtsi     — &soc { spi@0 { ... } }
#   board/dt-bindings/                — #include <dt-bindings/...>
#   board/docs/devicetree.md          — 布局、无序全解耦版说明、compatible 属性契约
#   tools/dtc-lite.py                 — 编译期生成 board_* / dt_config_gen.h
# ==============================================================================

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(BOARD_DTS             "${CMAKE_CURRENT_SOURCE_DIR}/dts/stm32f407zgt6.dts")
set(BOARD_DTSI_DIR        "${CMAKE_CURRENT_SOURCE_DIR}/dtsi")
set(DTC_LITE              "${PROJECT_SOURCE_DIR}/tools/dtc-lite.py")
set(GENERATED_BOARD_DIR   "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${GENERATED_BOARD_DIR}")

set(GENERATED_BOARD_SRCS
    "${GENERATED_BOARD_DIR}/board_devtable.c"
    "${GENERATED_BOARD_DIR}/board_probe.c"
)

set(GENERATED_BOARD_HDRS
    "${GENERATED_BOARD_DIR}/board_nodes.h"
    "${GENERATED_BOARD_DIR}/board_devtable.h"
    "${GENERATED_BOARD_DIR}/board_handles.h"
)

add_custom_command(
    OUTPUT  ${GENERATED_BOARD_SRCS} ${GENERATED_BOARD_HDRS}
    COMMAND ${Python3_EXECUTABLE}
            "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/spi"
    DEPENDS
        "${BOARD_DTS}"
        "${BOARD_DTSI_DIR}/stm32f407.dtsi"
        "${BOARD_DTSI_DIR}/stm32f407-spi.dtsi"
        "${CMAKE_CURRENT_SOURCE_DIR}/dt-bindings/spi/spi-parameter.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/spi/spi_bus.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/spi/spi_client.c"
        "${DTC_LITE}"
    VERBATIM
)

add_custom_target(board_dts_generate
    DEPENDS ${GENERATED_BOARD_SRCS}
)

# ---- 库定义 ----
add_library(board STATIC
    "src/task_config.c"
    "src/task_utils.c"
    "src/board_device.c"
    "src/bus.c"
    "src/dev_lifecycle.c"
    "src/board_driver.c"
    "src/config_store.c"
    ${GENERATED_BOARD_SRCS}
)

target_include_directories(board
    PUBLIC
        "include"
        "${GENERATED_BOARD_DIR}"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/core/include"
)

target_link_libraries(board
    PUBLIC
        osal
        core
        hal_bus
        hal_inst
        hal_if
)

add_dependencies(board board_dts_generate)
add_dependencies(board kconfig_generate)
set_source_files_properties(${GENERATED_BOARD_SRCS} PROPERTIES GENERATED TRUE)
