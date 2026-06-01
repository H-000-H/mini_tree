# ==============================================================================
# board — 板级支持 (设备树、驱动注册、配置存储、VFS)
# ==============================================================================
# 从 board.dts 生成设备表代码，注册板级驱动。
# 设备树生成需要 Python + dtc-lite。
# ==============================================================================

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---- DTS 生成 ----
set(BOARD_DTS            "${CMAKE_CURRENT_SOURCE_DIR}/board.dts")
set(DTC_LITE             "${CMAKE_SOURCE_DIR}/tools/dtc-lite.py")
set(GENERATED_BOARD_DIR  "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${GENERATED_BOARD_DIR}")

set(GENERATED_BOARD_SRCS
    "${GENERATED_BOARD_DIR}/board_devtable.c"
    "${GENERATED_BOARD_DIR}/board_probe.c"
    "${GENERATED_BOARD_DIR}/board_force_link.c"
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
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    DEPENDS "${BOARD_DTS}" "${DTC_LITE}"
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
    "src/board_driver.c"
    "src/config_store.c"
    ${GENERATED_BOARD_SRCS}
)

target_include_directories(board
    PUBLIC
        "include"
        "${GENERATED_BOARD_DIR}"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
)

target_link_libraries(board
    PUBLIC
        osal
        core
        hal_if
)

add_dependencies(board board_dts_generate)
add_dependencies(board kconfig_generate)
set_source_files_properties(${GENERATED_BOARD_SRCS} PROPERTIES GENERATED TRUE)
