# ==============================================================================
# osal — 操作系统抽象层 (OS Abstraction Layer)
# ==============================================================================
# 根据 Kconfig OSAL_BACKEND 选择编译对应的 RTOS 封装:
#   CONFIG_OSAL_FREERTOS  → osal_freertos.c  + link freertos_kernel
#   CONFIG_OSAL_RTTHREAD  → osal_rtthread.c  + link rtthread_kernel
# ==============================================================================

# ── 从 mini_tree/.config 读取 OSAL 后端 (默认裸机, 与板级 Kconfig 一致) ──
set(OSAL_BACKEND "NULL")
set(_MINI_TREE_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../.config")
if(EXISTS "${_MINI_TREE_CONFIG}")
    file(STRINGS "${_MINI_TREE_CONFIG}" _osal_choice REGEX "^CONFIG_OSAL_(FREERTOS|RTTHREAD|NULL)=y$")
    if(_osal_choice STREQUAL "CONFIG_OSAL_FREERTOS=y")
        set(OSAL_BACKEND "FREERTOS")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_RTTHREAD=y")
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

# C++ 任务创建封装（etl::optional）— 软编码: .config 显式 "# CONFIG_OSAL_NULL_TASK_CPP is not set" 才裁剪;
# 缺省视为启用 (对齐 Kconfig default y)
file(STRINGS "${_MINI_TREE_CONFIG}" _task_cpp_off REGEX "^# CONFIG_OSAL_NULL_TASK_CPP is not set$")
if(NOT _task_cpp_off)
    target_sources(osal
        PRIVATE
            "src/osal_task.cpp"
    )
endif()

target_include_directories(osal
    PUBLIC
        "include"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/board/include"
        "${PROJECT_SOURCE_DIR}/core/include"
        "${CMAKE_BINARY_DIR}/generated/kconfig"
        "${CMAKE_BINARY_DIR}/generated/board/mini_tree"
)

add_dependencies(osal kconfig_generate)
