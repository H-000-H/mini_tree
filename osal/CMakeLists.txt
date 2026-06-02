# ==============================================================================
# osal — 操作系统抽象层 (OS Abstraction Layer)
# ==============================================================================
# 根据 Kconfig OSAL_BACKEND 选择编译对应的 RTOS 封装:
#   CONFIG_OSAL_FREERTOS  → osal_freertos.c  + link freertos_kernel
#   CONFIG_OSAL_RTTHREAD  → osal_rtthread.c  + link rtthread_kernel
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

# ── 条件编译 ──
if(OSAL_BACKEND STREQUAL "FREERTOS")
    add_library(osal STATIC "src/osal_freertos.c")

    target_link_libraries(osal
        PUBLIC
            freertos_kernel
    )

elseif(OSAL_BACKEND STREQUAL "RTTHREAD")
    add_library(osal STATIC "src/osal_rtthread.c")

    target_link_libraries(osal
        PUBLIC
            rtthread_kernel
    )

elseif(OSAL_BACKEND STREQUAL "NULL")
    add_library(osal STATIC "src/osal_null.c")

else()
    message(FATAL_ERROR "Unsupported OSAL_BACKEND: ${OSAL_BACKEND}")
endif()

target_include_directories(osal
    PUBLIC
        "include"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/board/include"
        "${PROJECT_SOURCE_DIR}/core/include"
        "${CMAKE_BINARY_DIR}/generated/kconfig"
        "${PROJECT_BINARY_DIR}/board/generated"
)

add_dependencies(osal kconfig_generate)
