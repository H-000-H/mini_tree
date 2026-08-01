# ==============================================================================
# lib — OSAL 条件内核（仅保留 RTOS）
# ==============================================================================
# 保留 vendor：freeRTOS / rtthread（按 Kconfig 选择）
# 其余基础设施（tinyusb / lwIP / cJSON / ETL）均通过 cmake/*.cmake FetchContent 按需拉取
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

# ── RTOS 源码树状态 ──
foreach(_rtos IN ITEMS freeRTOS rtthread)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${_rtos}")
        message(STATUS "RTOS ${_rtos}: present under lib/${_rtos}")
    else()
        message(STATUS "RTOS ${_rtos}: MISSING under lib/${_rtos}")
    endif()
endforeach()
message(STATUS "Infra bricks (tinyusb/lwIP/cJSON/ETL): FetchContent via cmake/*.cmake")

add_library(lib INTERFACE)
if(OSAL_BACKEND STREQUAL "FREERTOS")
    target_link_libraries(lib INTERFACE freertos_kernel)
elseif(OSAL_BACKEND STREQUAL "RTTHREAD")
    target_link_libraries(lib INTERFACE rtthread_kernel)
endif()
