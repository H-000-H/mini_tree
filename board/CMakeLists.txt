# ==============================================================================
# board — 板级支持 (设备树约定、驱动注册、配置存储)
# ==============================================================================
# 设备树布局 (Linux 写法; dts / dtsi 分目录):
#   board/dts/board.dts     — 中间件默认入口 (平台用 BOARD_DTS 覆盖)
#   board/dtsi/             — SoC/外设片段 (平台覆盖 BOARD_DTSI_DIR)
#   board/dt-bindings/      — #include <dt-bindings/...> 通用参数
#   tools/dtc-lite.py       — 编译期生成 board_* / dt_config_gen.h
#
# 注意: 根 CMakeLists.txt 已统一跑 dtc-lite；本文件供独立编 board 库时使用。
# ==============================================================================

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(BOARD_DTS             "${CMAKE_CURRENT_SOURCE_DIR}/dts/board.dts" CACHE FILEPATH "板级 DTS (各平台覆盖)")
set(BOARD_DTSI_DIR        "${CMAKE_CURRENT_SOURCE_DIR}/dtsi" CACHE PATH "板级 dtsi (各平台覆盖)")
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
    "${GENERATED_BOARD_DIR}/dt_config_gen.h"
)

# 平台 dtsi 可选: 存在则加入 DEPENDS, 不存在也不挡中间件默认 board.dts
file(GLOB BOARD_DTSI_FILES CONFIGURE_DEPENDS "${BOARD_DTSI_DIR}/*.dtsi")

add_custom_command(
    OUTPUT  ${GENERATED_BOARD_SRCS} ${GENERATED_BOARD_HDRS}
    COMMAND ${Python3_EXECUTABLE}
            "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/spi"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/gpio"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/uart"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/i2c"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/tim"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/adc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/dac"
            "${CMAKE_CURRENT_SOURCE_DIR}/../bus/spi"
            "${CMAKE_CURRENT_SOURCE_DIR}/../bus/uart"
            "${CMAKE_CURRENT_SOURCE_DIR}/../bus/i2c"
            "${CMAKE_CURRENT_SOURCE_DIR}/../bus/can"
            "${CMAKE_CURRENT_SOURCE_DIR}/../bus/usb"
            "${CMAKE_CURRENT_SOURCE_DIR}/../bus/i2s"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/can"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/usb"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/rtc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/iwdg"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/wwdg"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/i2s"
            "${CMAKE_CURRENT_SOURCE_DIR}/../drivers/flash"
    DEPENDS
        "${BOARD_DTS}"
        ${BOARD_DTSI_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/dt-bindings/spi/spi-parameter.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/dt-bindings/uart/uart-parameter.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/dt-bindings/tim/tim-parameter.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/dt-bindings/gpio/gpio-parameter.h"
        "${DTC_LITE}"
    COMMENT "Running dtc-lite on ${BOARD_DTS}"
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
)

add_dependencies(board board_dts_generate)
if(TARGET kconfig_generate)
    add_dependencies(board kconfig_generate)
endif()
set_source_files_properties(${GENERATED_BOARD_SRCS} PROPERTIES GENERATED TRUE)
