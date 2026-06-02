# ==============================================================================
# lib/os — FreeRTOS Kernel (V11.3.0 from FreeRTOSv202604.00-LTS)
# ==============================================================================
# 使用方式:
#   cmake .. -DFREERTOS_PORT=GCC_ARM_CM4F -DFREERTOS_HEAP=4
#
# 可用端口:
#   GCC_ARM_CM3   — ARM Cortex-M3
#   GCC_ARM_CM4F  — ARM Cortex-M4F / CM7 (非 r0p1)
#   GCC_RISC_V    — RISC-V (需要 FREERTOS_RISCV_EXTENSION)
#   GCC_POSIX     — POSIX (用户编译/测试)
#
# 端口也可通过 Kconfig → PLATFORM_MCU 选择, CMake 会自动同步.
# Kconfig 默认值: PLATFORM_POSIX
# ==============================================================================

# ── 端口选择: 优先 -D 覆盖, 否则从 Kconfig .config 派生 ──
if(FREERTOS_PORT STREQUAL "")
    # 未通过 -D 指定 → 从 Kconfig .config 读取平台选择
    set(FREERTOS_PORT "GCC_POSIX")
    if(EXISTS "${CMAKE_SOURCE_DIR}/.config")
        file(STRINGS "${CMAKE_SOURCE_DIR}/.config" _kconfig_platform REGEX "^CONFIG_PLATFORM_")
        if(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM7=y")
            set(FREERTOS_PORT "GCC_ARM_CM4F")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM3=y")
            set(FREERTOS_PORT "GCC_ARM_CM3")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM4F=y")
            set(FREERTOS_PORT "GCC_ARM_CM4F")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_RISCV=y")
            set(FREERTOS_PORT "GCC_RISC_V")
        endif()
    endif()
endif()

# ── 内核源文件 ──
set(FREERTOS_KERNEL_SRCS
    "tasks.c"
    "queue.c"
    "list.c"
    "timers.c"
    "event_groups.c"
    "stream_buffer.c"
)

# ── MPU 包装 ──
list(APPEND FREERTOS_KERNEL_SRCS
    "portable/Common/mpu_wrappers_v2.c"
)

# ── 堆分配器 ──
list(APPEND FREERTOS_KERNEL_SRCS
    "portable/MemMang/heap_${FREERTOS_HEAP}.c"
)

# ── 端口源文件和包含路径 ──
set(FREERTOS_PORT_INCDIR "")
set(FREERTOS_NEED_PTHREADS OFF)

if(FREERTOS_PORT STREQUAL "GCC_ARM_CM3")
    list(APPEND FREERTOS_KERNEL_SRCS
        "portable/GCC/ARM_CM3/port.c"
    )
    set(FREERTOS_PORT_INCDIR "portable/GCC/ARM_CM3")

elseif(FREERTOS_PORT STREQUAL "GCC_ARM_CM4F")
    list(APPEND FREERTOS_KERNEL_SRCS
        "portable/GCC/ARM_CM4F/port.c"
    )
    set(FREERTOS_PORT_INCDIR "portable/GCC/ARM_CM4F")

elseif(FREERTOS_PORT STREQUAL "GCC_RISC_V")
    list(APPEND FREERTOS_KERNEL_SRCS
        "portable/GCC/RISC-V/port.c"
        "portable/GCC/RISC-V/portASM.S"
    )
    set(FREERTOS_PORT_INCDIR "portable/GCC/RISC-V")

elseif(FREERTOS_PORT STREQUAL "GCC_POSIX")
    list(APPEND FREERTOS_KERNEL_SRCS
        "portable/ThirdParty/GCC/Posix/port.c"
        "portable/ThirdParty/GCC/Posix/utils/wait_for_event.c"
    )
    set(FREERTOS_PORT_INCDIR "portable/ThirdParty/GCC/Posix")
    set(FREERTOS_NEED_PTHREADS ON)

else()
    message(FATAL_ERROR "不支持的 FREERTOS_PORT: ${FREERTOS_PORT}。"
                        "可用选项: GCC_ARM_CM3, GCC_ARM_CM4F, GCC_RISC_V, GCC_POSIX")
endif()

# ── 构建库 ──
add_library(freertos_kernel STATIC ${FREERTOS_KERNEL_SRCS})

target_include_directories(freertos_kernel
    PUBLIC
        "include"
        "${CMAKE_CURRENT_SOURCE_DIR}"                     # FreeRTOSConfig.h (用户提供)
        "${CMAKE_CURRENT_SOURCE_DIR}/${FREERTOS_PORT_INCDIR}"   # portmacro.h
    PRIVATE
        "${CMAKE_SOURCE_DIR}/board/include"     # board_config.h → dt_config_gen.h (DTS 时钟)
        "${CMAKE_BINARY_DIR}/board/generated"   # dt_config_gen.h (由 dtc-lite.py 生成)
)

# ── 依赖 DTS 代码生成 + Kconfig ──
add_dependencies(freertos_kernel board_dts_generate)
add_dependencies(freertos_kernel kconfig_generate)

# ── RISC-V 扩展定义 ──
if(FREERTOS_PORT STREQUAL "GCC_RISC_V")
    if(NOT FREERTOS_RISCV_EXTENSION)
        set(FREERTOS_RISCV_EXTENSION "RISCV_no_extensions" CACHE STRING
            "RISC-V 扩展 (见 portable/GCC/RISC-V/chip_extensions.cmake)")
    endif()
    target_compile_definitions(freertos_kernel PRIVATE
        "${FREERTOS_RISCV_EXTENSION}"
    )
endif()

# ── POSIX 端口需要 pthread 和 librt ──
if(FREERTOS_NEED_PTHREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(freertos_kernel PRIVATE Threads::Threads rt)
endif()

# ── ARM 端口对齐要求 (ARMCLANG 不支持 -mno-unaligned-access) ──
if(FREERTOS_PORT MATCHES "^GCC_ARM_" AND NOT CMAKE_C_COMPILER_ID STREQUAL "ARMClang")
    target_compile_options(freertos_kernel PRIVATE -mthumb -mno-unaligned-access)
endif()
