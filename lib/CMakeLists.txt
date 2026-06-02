# ==============================================================================
# lib — 第三方 / 通用库组件
# ==============================================================================
# 根据 Kconfig OSAL_BACKEND 选择编译对应的 RTOS 内核:
#   CONFIG_OSAL_FREERTOS  → freeRTOS/   (freertos_kernel)
#   CONFIG_OSAL_RTTHREAD  → rtthread/   (rtthread_kernel)
# ==============================================================================

# ── 从 Kconfig .config 读取 OSAL 后端选择 ──
set(OSAL_BACKEND "FREERTOS")
if(EXISTS "${PROJECT_SOURCE_DIR}/.config")
    file(STRINGS "${PROJECT_SOURCE_DIR}/.config" _osal_choice REGEX "^CONFIG_OSAL_")
    if(_osal_choice STREQUAL "CONFIG_OSAL_RTTHREAD=y")
        set(OSAL_BACKEND "RTTHREAD")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_NULL=y")
        set(OSAL_BACKEND "NULL")
    endif()
endif()

# ── 条件构建 RTOS 内核 ──
if(OSAL_BACKEND STREQUAL "FREERTOS")
    add_subdirectory(freeRTOS)
endif()

if(OSAL_BACKEND STREQUAL "RTTHREAD")
    add_subdirectory(rtthread)
endif()

# ── lib 接口库 ──
add_library(lib INTERFACE)

if(OSAL_BACKEND STREQUAL "FREERTOS")
    target_link_libraries(lib INTERFACE freertos_kernel)
elseif(OSAL_BACKEND STREQUAL "RTTHREAD")
    target_link_libraries(lib INTERFACE rtthread_kernel)
elseif(OSAL_BACKEND STREQUAL "NULL")
    # 裸机模式, 无需 RTOS 内核
endif()
