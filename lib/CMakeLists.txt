# ==============================================================================
# lib — 基础设施第三方（进 git）+ OSAL 条件内核
# ==============================================================================
# 保留 vendor：
#   freeRTOS / rtthread（按 Kconfig）· tinyusb · lwIP · cJSON · etl
# 其余积木默认 FetchContent，见 cmake/*.cmake（本地 lib/<Name> 可覆盖）
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

# ── 基础设施源码树状态 ──
foreach(_infra IN ITEMS freeRTOS rtthread tinyusb lwip cJSON etl)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${_infra}")
        message(STATUS "infra ${_infra}: present under lib/${_infra}")
    else()
        message(STATUS "infra ${_infra}: MISSING under lib/${_infra}")
    endif()
endforeach()
message(STATUS "Optional bricks: FetchContent on mini_tree_link_* (local lib/<Name> override OK)")

add_library(lib INTERFACE)
if(OSAL_BACKEND STREQUAL "FREERTOS")
    target_link_libraries(lib INTERFACE freertos_kernel)
elseif(OSAL_BACKEND STREQUAL "RTTHREAD")
    target_link_libraries(lib INTERFACE rtthread_kernel)
endif()
