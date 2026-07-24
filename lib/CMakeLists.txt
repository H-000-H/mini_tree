# ==============================================================================
# lib — 第三方 / 通用库组件
# ==============================================================================
# RTOS 内核按 Kconfig 条件编译（与裸机隔离）:
#   CONFIG_OSAL_FREERTOS  → freeRTOS/   (freertos_kernel)
#   CONFIG_OSAL_RTTHREAD  → rtthread/   (rtthread_kernel)
#   CONFIG_OSAL_NULL      → 不编内核
#
# TinyUSB: 可移植协议栈（不含 DCD；板级自行加 CFG_TUSB_MCU / dcd）
# 可选第三方（lwIP/cJSON/.../FlashDB/SFUD/...）见 cmake/*.cmake，不默认链进固件
# ==============================================================================

get_filename_component(MINI_TREE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MINI_TREE_CONFIG "${MINI_TREE_ROOT}/.config")

if(NOT DEFINED MINI_TREE_KCONFIG_GEN_DIR)
    set(MINI_TREE_KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
endif()
if(NOT DEFINED MINI_TREE_BOARD_GEN_DIR)
    set(MINI_TREE_BOARD_GEN_DIR "${CMAKE_BINARY_DIR}/generated/board/mini_tree")
endif()

# 本板 monolithic 构建用 mini_tree_gen；为子目录 RTOS cmake 提供兼容别名
if(NOT TARGET kconfig_generate)
    if(DEFINED KCONFIG_OUT)
        add_custom_target(kconfig_generate DEPENDS "${KCONFIG_OUT}")
    else()
        add_custom_target(kconfig_generate)
    endif()
endif()
if(NOT TARGET board_dts_generate)
    if(DEFINED GEN_SRCS)
        add_custom_target(board_dts_generate DEPENDS ${GEN_SRCS} ${GEN_HDRS})
    else()
        add_custom_target(board_dts_generate)
    endif()
endif()

# ── OSAL 后端（读 mini_tree/.config）──
set(OSAL_BACKEND "NULL")
if(EXISTS "${MINI_TREE_CONFIG}")
    file(STRINGS "${MINI_TREE_CONFIG}" _osal_choice REGEX "^CONFIG_OSAL_(FREERTOS|RTTHREAD|NULL)=y$")
    if(_osal_choice STREQUAL "CONFIG_OSAL_FREERTOS=y")
        set(OSAL_BACKEND "FREERTOS")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_RTTHREAD=y")
        set(OSAL_BACKEND "RTTHREAD")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_NULL=y")
        set(OSAL_BACKEND "NULL")
    endif()
endif()

# FreeRTOS 默认：本板 Cortex-M4F + heap_4（可 -D 覆盖）
if(NOT DEFINED FREERTOS_PORT OR FREERTOS_PORT STREQUAL "")
    set(FREERTOS_PORT "GCC_ARM_CM4F")
endif()
if(NOT DEFINED FREERTOS_HEAP OR FREERTOS_HEAP STREQUAL "")
    set(FREERTOS_HEAP "4")
endif()

if(OSAL_BACKEND STREQUAL "FREERTOS")
    add_subdirectory(freeRTOS)
endif()
if(OSAL_BACKEND STREQUAL "RTTHREAD")
    add_subdirectory(rtthread)
endif()

# ── TinyUSB（通用核心，不绑 MCU；不默认链进固件）──
set(TINYUSB_ROOT "${CMAKE_CURRENT_LIST_DIR}/tinyusb")
set(TINYUSB_SRC_DIR "${TINYUSB_ROOT}/src")
if(EXISTS "${TINYUSB_SRC_DIR}/tusb.c")
    include("${TINYUSB_SRC_DIR}/CMakeLists.txt")
    tinyusb_sources_get(TINYUSB_CORE_SRCS)
    add_library(tinyusb INTERFACE)
    target_sources(tinyusb INTERFACE ${TINYUSB_CORE_SRCS})
    target_include_directories(tinyusb INTERFACE "${TINYUSB_SRC_DIR}")
else()
    message(STATUS "TinyUSB not present under lib/tinyusb — skip")
endif()

# ── lwIP（协议栈源码树；目标由 cmake/lwip.cmake 创建，不默认链进固件）──
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/lwip/src/include/lwip/init.h")
    message(STATUS "lwIP present under lib/lwip (link via cmake/lwip.cmake)")
else()
    message(STATUS "lwIP not present under lib/lwip — skip")
endif()

# ── 其它可选第三方（目标由对应 cmake/*.cmake 创建，不默认链进固件）──
foreach(_lib_name IN ITEMS
        cJSON littlefs EasyFlash MultiButton FatFs mcuboot lvgl u8g2
        FlashDB SFUD EasyLogger nanopb coreMQTT libmodbus mbedtls CMSIS-DSP)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${_lib_name}")
        message(STATUS "${_lib_name} present under lib/${_lib_name}")
    endif()
endforeach()

add_library(lib INTERFACE)
if(OSAL_BACKEND STREQUAL "FREERTOS")
    target_link_libraries(lib INTERFACE freertos_kernel)
elseif(OSAL_BACKEND STREQUAL "RTTHREAD")
    target_link_libraries(lib INTERFACE rtthread_kernel)
endif()
