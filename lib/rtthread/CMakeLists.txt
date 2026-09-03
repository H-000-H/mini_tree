# ==============================================================================
# lib/rtthread — RT-Thread Kernel (纯内核, 无设备框架)
# ==============================================================================
# 构建 RT-Thread 纯内核静态库, 由 OSAL 层包装后对外暴露.
#
# 架构端口由 Kconfig PLATFORM_MCU 自动选择:
#   CONFIG_PLATFORM_ARM_CM4F → cortex-m4
#   CONFIG_PLATFORM_ARM_CM3  → cortex-m3
#   CONFIG_PLATFORM_ARM_CM0  → cortex-m0
#   CONFIG_PLATFORM_ARM_CM7  → cortex-m7
#   CONFIG_PLATFORM_RISCV    → risc-v (common)
#
# rtconfig.h 默认使用同目录下的版本; 用户工程可在自己的 include
# 路径中放置 rtconfig.h 来覆盖 (同 FreeRTOSConfig.h 的注入模式).
# ==============================================================================

# ── 从 Kconfig 选择架构端口 ──
if(NOT DEFINED MINI_TREE_ROOT)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.config")
        set(MINI_TREE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
    elseif(EXISTS "${PROJECT_SOURCE_DIR}/.config")
        set(MINI_TREE_ROOT "${PROJECT_SOURCE_DIR}")
    else()
        get_filename_component(MINI_TREE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    endif()
endif()

if(NOT DEFINED MINI_TREE_KCONFIG_GEN_DIR)
    set(MINI_TREE_KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
    if(NOT EXISTS "${MINI_TREE_KCONFIG_GEN_DIR}")
        set(MINI_TREE_KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig")
    endif()
endif()

set(_MINI_TREE_KCONFIG "${MINI_TREE_ROOT}/.config")
set(RTTHREAD_PORT "arm/cortex-m4")
if(EXISTS "${_MINI_TREE_KCONFIG}")
    file(STRINGS "${_MINI_TREE_KCONFIG}" _kconfig_platform REGEX "^CONFIG_PLATFORM_")
    if(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM7=y")
        set(RTTHREAD_PORT "arm/cortex-m7")
    elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM3=y")
        set(RTTHREAD_PORT "arm/cortex-m3")
    elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM0=y")
        set(RTTHREAD_PORT "arm/cortex-m0")
    elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM4F=y")
        set(RTTHREAD_PORT "arm/cortex-m4")
    elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_RISCV=y")
        set(RTTHREAD_PORT "risc-v/common")
    endif()
endif()

# ── 内核源文件 ──
set(RTTHREAD_KERNEL_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu_up.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/defunct.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/idle.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/irq.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/klibc/kerrno.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/klibc/kstdio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/klibc/kstring.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kservice.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/object.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scheduler_comm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scheduler_up.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thread.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/timer.c"
)

# ── 架构端口源文件 ──
# atomic_arm.c 使用 LDREX/STREX (ARMv7-M+), M0/M0+ 由 rtconfig.h 关闭
# RT_USING_HW_ATOMIC 退回 rtatomic.h 内的软件实现, 因此不编入.
if(RTTHREAD_PORT MATCHES "^arm/" AND NOT RTTHREAD_PORT STREQUAL "arm/cortex-m0")
    list(APPEND RTTHREAD_KERNEL_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/arm/common/atomic_arm.c"
    )
endif()

# cortex-m4 和 cortex-m7 共享同一个 port (都有 FPU)
if(RTTHREAD_PORT STREQUAL "arm/cortex-m4" OR RTTHREAD_PORT STREQUAL "arm/cortex-m7")
    list(APPEND RTTHREAD_KERNEL_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/arm/cortex-m4/context_gcc.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/arm/cortex-m4/cpuport.c"
    )
elseif(RTTHREAD_PORT STREQUAL "arm/cortex-m3")
    list(APPEND RTTHREAD_KERNEL_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/arm/cortex-m3/context_gcc.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/arm/cortex-m3/cpuport.c"
    )
elseif(RTTHREAD_PORT STREQUAL "arm/cortex-m0")
    list(APPEND RTTHREAD_KERNEL_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/arm/cortex-m0/context_gcc.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/arm/cortex-m0/cpuport.c"
    )
elseif(RTTHREAD_PORT STREQUAL "risc-v/common")
    list(APPEND RTTHREAD_KERNEL_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/context_gcc.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/cpuport.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/interrupt_gcc.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/trap_common.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/atomic_riscv.c"
    )
endif()

# ── 启用汇编语言 ──
# 架构端口的上下文切换/中断开关 (rt_hw_context_switch*, rt_hw_interrupt_disable/enable,
# PendSV_Handler) 都在 libcpu/*/context_gcc.S 里。根 project() 只声明 LANGUAGES C CXX,
# 且仅在独立构建时执行 (ESP-IDF 组件构建走父工程 project, 此处不跑); mini-os 靠自身
# project(... C ASM) 激活 ASM, rtthread 若不激活, .S 会被 CMake 静默丢弃 ->
# 静态库阶段无报错, 链接真实固件时才缺这些 port 符号。故在此自启用 ASM
enable_language(ASM)

# ── 构建库 ──
add_library(rtthread_kernel STATIC ${RTTHREAD_KERNEL_SRCS})

target_include_directories(rtthread_kernel
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}"               # rtconfig.h (默认, PUBLIC 供 osal 层间接包含)
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/${RTTHREAD_PORT}"
        "${MINI_TREE_ROOT}/board/include"
        "${MINI_TREE_KCONFIG_GEN_DIR}"
)

# ── 内部符号可见性 ──
target_compile_definitions(rtthread_kernel PRIVATE
    __RT_KERNEL_SOURCE__
    RT_KLIBC_USING_LIBC_VSNPRINTF
    RT_KLIBC_USING_LIBC_VSSCANF
)

# ── 事件集 (Kconfig CONFIG_RTTHREAD_EVENT, 默认关闭) ──
# rtconfig.h 靠本宏定义 RT_USING_EVENT; RT-Thread 内核源文件看不到
# kconfig 生成的 config.h, 所以开关必须在 CMake 层注入。
# PUBLIC 传播: osal 层与内核必须看到同一份 rtconfig.h 展开结果
# (rt_event 开关会改变 IPC 对象集合, 全工程必须一致)。
# 关闭时 src/ipc.c 内的 rt_event_* 代码段整体不编译, 无额外 flash/ram 开销。
# CONFIG_OSAL_EVENT 是兜底触发条件: OSAL_EVENT 会 select RTTHREAD_EVENT,
# 而根 CMakeLists 已在配置期跑 genconfig.py 把 Kconfig 求值结果 (含 select
# 出来的符号) 回写 .config, 所以正常路径下 CONFIG_RTTHREAD_EVENT=y 本来就在。
# 额外认 CONFIG_OSAL_EVENT 只为覆盖绕过回写的情况 (单独构建 rtthread_kernel、
# 手改 .config 后尚未重配)。漏判的后果是 ipc.c 里的 rt_event_* 整体不编译,
# 而 osal_rtthread.c 已经在调它们 -> 静态库阶段不报错, 链接 ELF 时才缺符号
# (而且 rtconfig.h 展开不一致会连 struct rt_thread 布局都对不上)。
set(RTTHREAD_EVENT OFF)
if(EXISTS "${_MINI_TREE_KCONFIG}")
    file(STRINGS "${_MINI_TREE_KCONFIG}" _rtthread_event REGEX "^(CONFIG_RTTHREAD_EVENT|CONFIG_OSAL_EVENT)=y$")
    if(_rtthread_event)
        set(RTTHREAD_EVENT ON)
    endif()
endif()
if(RTTHREAD_EVENT)
    target_compile_definitions(rtthread_kernel PUBLIC
        MINI_TREE_RTTHREAD_EVENT=1
    )
    message(STATUS "mini_tree RT-Thread: event set ON (CONFIG_RTTHREAD_EVENT / CONFIG_OSAL_EVENT)")
else()
    message(STATUS "mini_tree RT-Thread: event set OFF (RT_USING_EVENT undefined)")
endif()

add_dependencies(rtthread_kernel kconfig_generate)
