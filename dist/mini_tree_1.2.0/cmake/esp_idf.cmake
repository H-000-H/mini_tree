# ESP-IDF 组件入口（由根 CMakeLists.txt 在 ESP_PLATFORM 时 include）
# 全部用 MINI_TREE_DIR 绝对路径，避免 include 后相对路径落到 cmake/ 下。
#
# 板级/芯片/树外驱动全部自动推导，无需外部注入文件:
#   B: 芯片 dtc -I/-D      ← IDF_TARGET (idf_build_get_property)
#   C: 板级 DTS           ← 约定组件 components/board_${IDF_TARGET} (dts/board.dts + dtsi/)
#   D: 树外产品驱动       ← 工程 components/*/src (DRIVER_REGISTER)
#   E: 功能开关           ← IDF CONFIG_* (sdkconfig, menuconfig 可见)
# 逃生门: MINI_TREE_DTC_EXTRA_ARGS 仍可注入非标准芯片头路径 (默认空)。

get_filename_component(MINI_TREE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
idf_build_get_property(_IDF_TARGET IDF_TARGET)
string(TOUPPER "${_IDF_TARGET}" _IDF_TARGET_UP)

# ── E: 功能软编码 — 直接读 IDF 的 CONFIG_* CMake 变量 ───────────────────
# sdkconfig.cmake 在组件 CMakeLists 处理前已 include, CONFIG_* 即配置期变量,
# 板级经 sdkconfig.defaults / menuconfig 控制, ESP 路径不再读 .config。
# SYSTEM / EVENT_BUS / SYSTEM_CMD / USB 均为 Kconfig.mini_tree 符号。

# System / EventBus: Kconfig default y; 显式关才裁剪
if(CONFIG_SYSTEM)
    set(MINI_TREE_SYSTEM ON)
else()
    set(MINI_TREE_SYSTEM OFF)
endif()
# EVENT_BUS 依赖 SYSTEM: CONFIG_EVENT_BUS=y 且 SYSTEM=y 才编入 (对齐 Kconfig 语义)
if(CONFIG_EVENT_BUS AND MINI_TREE_SYSTEM)
    set(MINI_TREE_EVENT_BUS ON)
else()
    set(MINI_TREE_EVENT_BUS OFF)
endif()
# SystemCmd: Kconfig default n; 仅 SYSTEM_CPP 后端有效
if(CONFIG_SYSTEM_CMD AND CONFIG_SYSTEM_CPP)
    set(MINI_TREE_SYSTEM_CMD ON)
else()
    set(MINI_TREE_SYSTEM_CMD OFF)
endif()

# USB: Kconfig default y; 显式关才裁剪。
# 启用时需板级 usb_tusb_port glue（docs/usb_tusb_port.md），并自行 REQUIRE esp_tinyusb 等。
if(CONFIG_USB)
    set(MINI_TREE_USB ON)
else()
    set(MINI_TREE_USB OFF)
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
    if(CONFIG_SYSTEM_CPP)
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
# ── C: 板级 DTS 自动发现 ────────────────────────────────────────────────
# 约定: 名为 board_${IDF_TARGET} 的组件提供 dts/board.dts + dtsi/。
# 组件发现期即设 COMPONENT_DIR 属性 (component.cmake), 此处查询不依赖其
# CMakeLists 执行顺序; 但组件不存在时 idf_component_get_property 会 FATAL,
# 故先用 "CMakeLists.txt 存在" 作守卫 (查文件而非目录, 防半成品目录误判)。
# 未发现板级组件 → 用中间件占位 (可编过, 无板级节点); 发现但布局不符 → FAIL loud。
set(_BOARD_NAME "board_${_IDF_TARGET}")
file(GLOB _BOARD_CAND "${CMAKE_SOURCE_DIR}/components/${_BOARD_NAME}/CMakeLists.txt")
if(_BOARD_CAND)
    idf_component_get_property(_BOARD_DIR "${_BOARD_NAME}" COMPONENT_DIR)
    if(EXISTS "${_BOARD_DIR}/dts/board.dts")
        set(BOARD_DTS      "${_BOARD_DIR}/dts/board.dts")
        set(BOARD_DTSI_DIR "${_BOARD_DIR}/dtsi")
    else()
        message(FATAL_ERROR
            "${_BOARD_NAME} 已发现但缺 dts/board.dts; 约定布局: "
            "${_BOARD_DIR}/dts/board.dts + dtsi/ (纯数据组件, 空 idf_component_register)")
    endif()
endif()
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

# ── B: dtc 参数 — dt-bindings 基础 + 跨芯片通用 IDF 头 + 芯片专属头/宏 ──
# 基础 "-I mini_tree/board" (dt-bindings 搜索路径) 是固定项, 勿把 dtsi 目录塞进来。
set(DTC_LITE_ARGS "-I${MINI_TREE_DIR}/board")
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
    # 芯片专属头 (按 IDF_TARGET 推导)。IDF 目录结构随版本变, 存在才加 (跳过不影响构建)
    foreach(_d
        "${_IDF}/components/esp_hal_gpio/${_IDF_TARGET}/include"
        "${_IDF}/components/soc/${_IDF_TARGET}/include"
    )
        if(IS_DIRECTORY "${_d}")
            list(APPEND DTC_LITE_ARGS "-I${_d}")
        endif()
    endforeach()
    # 目标宏: 让 dtsi 里的 #ifdef CONFIG_IDF_TARGET_<CHIP> 分支生效 (一个 dtsi 服务多芯片)
    list(APPEND DTC_LITE_ARGS
        "-DCONFIG_IDF_TARGET_${_IDF_TARGET_UP}=1"
        "-DIDF_TARGET_${_IDF_TARGET_UP}=1")
endif()
# 逃生门: 非标准芯片头路径等特殊注入 (默认空, 正常工程无需设置)
if(DEFINED MINI_TREE_DTC_EXTRA_ARGS)
    list(APPEND DTC_LITE_ARGS ${MINI_TREE_DTC_EXTRA_ARGS})
endif()

# 默认 DRIVER_REGISTER 扫描目录（中间件 + drivers/*/src + 工程树外驱动）
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
# ── D: 树外产品驱动自动扫描 ─────────────────────────────────────────────
# 约定: 工程 components/*/src 下的 DRIVER_REGISTER 即树外产品驱动 (如 driver_ws2812)。
# 无 DRIVER_REGISTER 的 src (如 hal_*) 只是多解析一次, 无害。
file(GLOB _OUT_DRV_SRC_DIRS LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/components/*/src")
list(APPEND _DTC_SCAN_DIRS ${_OUT_DRV_SRC_DIRS})
if(MINI_TREE_USB)
    list(APPEND _DTC_SCAN_DIRS
        "${MINI_TREE_DIR}/bus/usb"
        "${MINI_TREE_DIR}/vfs/usb")
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
# 树外驱动源进 DEPENDS: 驱动代码变更即重跑 dts (与 D 段扫描目录配套)
file(GLOB _OUT_DRV_SRCS "${CMAKE_SOURCE_DIR}/components/*/src/*.c")
list(APPEND _DTC_DEPENDS ${_OUT_DRV_SRCS})
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
if(CONFIG_SYSTEM_CPP)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_SYSTEM_CPP)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    )
endif()

if(EXISTS "${MINI_TREE_DIR}/error_symbols.ld")
    target_linker_script(${COMPONENT_LIB} INTERFACE "${MINI_TREE_DIR}/error_symbols.ld")
endif()
