# ==============================================================================
# osal — 操作系统抽象层 (OS Abstraction Layer)
# ==============================================================================
# 根据 Kconfig OSAL_BACKEND 选择编译对应的 RTOS 封装:
#   CONFIG_OSAL_MINI_OS   → osal_mini_os.c   + link mini_os_kernel (自研 RTOS)
#   CONFIG_OSAL_FREERTOS  → osal_freertos.c  + link freertos_kernel
#   CONFIG_OSAL_RTTHREAD  → osal_rtthread.c  + link rtthread_kernel
# ==============================================================================

# ── 从 mini_tree/.config 读取 OSAL 后端 (默认裸机, 与板级 Kconfig 一致) ──
set(OSAL_BACKEND "NULL")
set(_MINI_TREE_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../.config")
if(EXISTS "${_MINI_TREE_CONFIG}")
    file(STRINGS "${_MINI_TREE_CONFIG}" _osal_choice REGEX "^CONFIG_OSAL_(MINI_OS|FREERTOS|RTTHREAD|THREADX|UCOS_III|UCOS_II|NULL)=y$")
    if(_osal_choice STREQUAL "CONFIG_OSAL_MINI_OS=y")
        set(OSAL_BACKEND "MINI_OS")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_FREERTOS=y")
        set(OSAL_BACKEND "FREERTOS")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_RTTHREAD=y")
        set(OSAL_BACKEND "RTTHREAD")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_THREADX=y")
        set(OSAL_BACKEND "THREADX")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_UCOS_III=y")
        set(OSAL_BACKEND "UCOS_III")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_UCOS_II=y")
        set(OSAL_BACKEND "UCOS_II")
    elseif(_osal_choice STREQUAL "CONFIG_OSAL_NULL=y")
        set(OSAL_BACKEND "NULL")
    endif()
endif()

# ── 条件编译 ──
if(OSAL_BACKEND STREQUAL "MINI_OS")
    add_library(osal STATIC "src/osal_mini_os.c")

    target_link_libraries(osal
        PUBLIC
            mini_os_kernel
    )

elseif(OSAL_BACKEND STREQUAL "FREERTOS")
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

elseif(OSAL_BACKEND STREQUAL "THREADX")
    add_library(osal STATIC "src/osal_threadx.c")

    target_link_libraries(osal
        PUBLIC
            threadx_kernel
    )

elseif(OSAL_BACKEND STREQUAL "UCOS_III")
    add_library(osal STATIC "src/osal_ucos_iii.c")

    target_link_libraries(osal
        PUBLIC
            ucos_iii_kernel
    )

elseif(OSAL_BACKEND STREQUAL "UCOS_II")
    add_library(osal STATIC "src/osal_ucos_ii.c")

    target_link_libraries(osal
        PUBLIC
            ucos_ii_kernel
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

# ── 裸机可选: osal_malloc/osal_calloc/osal_free 走 mini-os 内存模块 (默认走 libc 堆) ──
# 只编 lib/mini-os/src/memory.c 单文件 (无调度器/port 依赖), 不链整个内核;
# 缺省视为关闭 (对齐 Kconfig default n), 仅显式 =y 启用。
set(_MINI_OS_DIR "${PROJECT_SOURCE_DIR}/lib/mini-os")
file(STRINGS "${_MINI_TREE_CONFIG}" _null_mini_os_mem REGEX "^CONFIG_OSAL_NULL_MINI_OS_MEM=y$")
if(OSAL_BACKEND STREQUAL "NULL" AND _null_mini_os_mem)
    add_library(mini_os_mem STATIC "${_MINI_OS_DIR}/src/memory.c")
    target_include_directories(mini_os_mem
        PUBLIC
            "${_MINI_OS_DIR}/inc"
    )
    add_dependencies(mini_os_mem kconfig_generate)
    target_link_libraries(osal
        PUBLIC
            mini_os_mem
    )
    message(STATUS "osal: bare-metal osal_malloc/osal_calloc/osal_free -> mini-os memory module (CONFIG_OSAL_NULL_MINI_OS_MEM); "
                   "board linker script must provide __mini_os_heap_start/__mini_os_heap_end (INCLUDE mini-os-heap.ld)")
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
