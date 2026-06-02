# ==============================================================================
# lib/rtthread — RT-Thread Kernel (纯内核, 无设备框架)
# ==============================================================================
# 构建 RT-Thread 纯内核静态库, 由 OSAL 层包装后对外暴露.
#
# 架构端口由 Kconfig PLATFORM_MCU 自动选择:
#   CONFIG_PLATFORM_ARM_CM4F → cortex-m4
#   CONFIG_PLATFORM_ARM_CM3  → cortex-m3
#   CONFIG_PLATFORM_ARM_CM7  → cortex-m7
#   CONFIG_PLATFORM_RISCV    → risc-v (common)
#
# rtconfig.h 默认使用同目录下的版本; 用户工程可在自己的 include
# 路径中放置 rtconfig.h 来覆盖 (同 FreeRTOSConfig.h 的注入模式).
# ==============================================================================

# ── 从 Kconfig 选择架构端口 ──
set(RTTHREAD_PORT "arm/cortex-m4")
if(EXISTS "${PROJECT_SOURCE_DIR}/.config")
    file(STRINGS "${PROJECT_SOURCE_DIR}/.config" _kconfig_platform REGEX "^CONFIG_PLATFORM_")
    if(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM7=y")
        set(RTTHREAD_PORT "arm/cortex-m7")
    elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM3=y")
        set(RTTHREAD_PORT "arm/cortex-m3")
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
if(RTTHREAD_PORT MATCHES "^arm/")
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
elseif(RTTHREAD_PORT STREQUAL "risc-v/common")
    list(APPEND RTTHREAD_KERNEL_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/context_gcc.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/cpuport.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/interrupt_gcc.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/trap_common.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/risc-v/common/atomic_riscv.c"
    )
endif()

# ── 构建库 ──
add_library(rtthread_kernel STATIC ${RTTHREAD_KERNEL_SRCS})

target_include_directories(rtthread_kernel
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}"               # rtconfig.h (默认, PUBLIC 供 osal 层间接包含)
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libcpu/${RTTHREAD_PORT}"
        "${PROJECT_SOURCE_DIR}/board/include"
        "${CMAKE_BINARY_DIR}/generated/kconfig"
)

# ── 内部符号可见性 ──
target_compile_definitions(rtthread_kernel PRIVATE
    __RT_KERNEL_SOURCE__
    RT_KLIBC_USING_LIBC_VSNPRINTF
    RT_KLIBC_USING_LIBC_VSSCANF
)

add_dependencies(rtthread_kernel kconfig_generate)
