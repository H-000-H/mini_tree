# ==============================================================================
# lib/os — FreeRTOS Kernel (V11.3.0 from FreeRTOSv202604.00-LTS)
# ==============================================================================
# 使用方式:
#   cmake .. -DFREERTOS_PORT=GCC_ARM_CM4F -DFREERTOS_HEAP=4
#
# 可用端口:
#   GCC_ARM_CM0   — ARM Cortex-M0 / M0+
#   GCC_ARM_CM3   — ARM Cortex-M3
#   GCC_ARM_CM4F  — ARM Cortex-M4F / CM7 (非 r0p1)
#   GCC_RISC_V    — RISC-V (需要 FREERTOS_RISCV_EXTENSION)
#   GCC_POSIX     — POSIX (用户编译/测试)
#
# 端口也可通过 Kconfig → PLATFORM_MCU 选择, CMake 会自动同步.
# Kconfig 默认值: PLATFORM_POSIX
# ==============================================================================

# ── mini_tree/.config 路径 (供 Kconfig 派生的开关读取) ──
# 独立入口未定义该变量时回退到同级 ../.config (即 mini_tree/.config)
if(NOT DEFINED MINI_TREE_CONFIG)
    set(MINI_TREE_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../.config")
endif()

# ── 端口选择: 优先 -D 覆盖, 否则从 Kconfig .config 派生 ──
if(FREERTOS_PORT STREQUAL "")
    # 未通过 -D 指定 → 从 Kconfig .config 读取平台选择
    set(FREERTOS_PORT "GCC_POSIX")
    if(EXISTS "${MINI_TREE_CONFIG}")
        file(STRINGS "${MINI_TREE_CONFIG}" _kconfig_platform REGEX "^CONFIG_PLATFORM_")
        if(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM7=y")
            set(FREERTOS_PORT "GCC_ARM_CM4F")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM3=y")
            set(FREERTOS_PORT "GCC_ARM_CM3")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM0=y")
            set(FREERTOS_PORT "GCC_ARM_CM0")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM4F=y")
            set(FREERTOS_PORT "GCC_ARM_CM4F")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_RISCV=y")
            set(FREERTOS_PORT "GCC_RISC_V")
        endif()
    endif()
endif()

# ── 内核源文件 ──
# event_groups.c 不在此列: 事件组由 Kconfig CONFIG_FREERTOS_EVENT_GROUPS
# 单独门控 (默认关闭), 见下方条件编入块
set(FREERTOS_KERNEL_SRCS
    "tasks.c"
    "queue.c"
    "list.c"
    "timers.c"
    "stream_buffer.c"
)

# ── 事件组 (Kconfig CONFIG_FREERTOS_EVENT_GROUPS, 默认关闭) ──
# 双重隔离: 关闭时既不编入 event_groups.c, 也让 FreeRTOSConfig.h 把
# configUSE_EVENT_GROUPS 显式定为 0 (FreeRTOS.h 的兜底值是 1, 不显式
# 定义就关不掉)。宏用 PUBLIC 传播, 保证 osal 层与内核看到同一份
# FreeRTOSConfig.h 展开结果 (事件组相关类型布局必须全工程一致)。
# CONFIG_OSAL_EVENT 是兜底触发条件: OSAL_EVENT 会 select
# FREERTOS_EVENT_GROUPS, 而根 CMakeLists 已在配置期跑 genconfig.py 把 Kconfig
# 求值结果 (含 select 出来的符号) 回写 .config, 所以正常路径下
# CONFIG_FREERTOS_EVENT_GROUPS=y 本来就在。额外认 CONFIG_OSAL_EVENT 只为覆盖
# 绕过回写的情况 (单独构建 freertos_kernel、手改 .config 后尚未重配)。
# 漏判的后果是 event_groups.c 不编入内核库, 而 osal_freertos.c 已经在调
# xEventGroup* -> 静态库阶段不报错, 链接 ELF 时才缺符号。
set(FREERTOS_EVENT_GROUPS OFF)
if(EXISTS "${MINI_TREE_CONFIG}")
    file(STRINGS "${MINI_TREE_CONFIG}" _freertos_event_groups REGEX "^(CONFIG_FREERTOS_EVENT_GROUPS|CONFIG_OSAL_EVENT)=y$")
    if(_freertos_event_groups)
        set(FREERTOS_EVENT_GROUPS ON)
    endif()
endif()
if(FREERTOS_EVENT_GROUPS)
    list(APPEND FREERTOS_KERNEL_SRCS "event_groups.c")
    message(STATUS "mini_tree FreeRTOS: event groups ON (CONFIG_FREERTOS_EVENT_GROUPS / CONFIG_OSAL_EVENT)")
else()
    message(STATUS "mini_tree FreeRTOS: event groups OFF (event_groups.c not compiled)")
endif()

# ── 软件定时器开关 (CONFIG_FREERTOS_USE_TIMERS) ──
# FreeRTOSConfig.h 用 #ifdef CONFIG_FREERTOS_USE_TIMERS 决定 configUSE_TIMERS,
# 但该宏来自 kconfig 生成的 config.h, 而内核源文件 (tasks.c / timers.c /
# event_groups.c) 并不包含 config.h -> 内核侧 configUSE_TIMERS 会恒为 0。
# 平时看不出问题 (timers.c 整体被 #if configUSE_TIMERS==1 包住, 等于空编),
# 但事件组开启后 FreeRTOSConfig.h 会连带定义 INCLUDE_xTimerPendFunctionCall,
# timers.c 开头的 "#if INCLUDE_xTimerPendFunctionCall && !configUSE_TIMERS"
# 就会直接 #error。故这里同样用 -D 注入等效开关 (PUBLIC, 与事件组一致)。
# 事件组依赖守护任务, 因此 event groups 命中时定时器必须一起开。
set(FREERTOS_USE_TIMERS ${FREERTOS_EVENT_GROUPS})
if(EXISTS "${MINI_TREE_CONFIG}")
    file(STRINGS "${MINI_TREE_CONFIG}" _freertos_use_timers REGEX "^CONFIG_FREERTOS_USE_TIMERS=y$")
    if(_freertos_use_timers)
        set(FREERTOS_USE_TIMERS ON)
    endif()
endif()

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

if(FREERTOS_PORT STREQUAL "GCC_ARM_CM0")
    list(APPEND FREERTOS_KERNEL_SRCS
        "portable/GCC/ARM_CM0/port.c"
        "portable/GCC/ARM_CM0/portasm.c"
    )
    set(FREERTOS_PORT_INCDIR "portable/GCC/ARM_CM0")

elseif(FREERTOS_PORT STREQUAL "GCC_ARM_CM3")
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
                        "可用选项: GCC_ARM_CM0, GCC_ARM_CM3, GCC_ARM_CM4F, GCC_RISC_V, GCC_POSIX")
endif()

# ── 构建库 ──
add_library(freertos_kernel STATIC ${FREERTOS_KERNEL_SRCS})

target_include_directories(freertos_kernel
    PUBLIC
        "include"
        "${CMAKE_CURRENT_SOURCE_DIR}"                     # FreeRTOSConfig.h (用户提供)
        "${CMAKE_CURRENT_SOURCE_DIR}/${FREERTOS_PORT_INCDIR}"   # portmacro.h
    PRIVATE
        "${PROJECT_SOURCE_DIR}/board/include"     # board_config.h → dt_config_gen.h (DTS 时钟)
        "${PROJECT_BINARY_DIR}/board/generated"     # dt_config_gen.h (由 dtc-lite.py 生成)
)

# ── 依赖 DTS 代码生成 + Kconfig ──
add_dependencies(freertos_kernel board_dts_generate)
add_dependencies(freertos_kernel kconfig_generate)

# ── 事件组 / 软件定时器开关宏 (内核源文件看不到 config.h, 需 -D 注入) ──
if(FREERTOS_EVENT_GROUPS)
    target_compile_definitions(freertos_kernel PUBLIC
        MINI_TREE_FREERTOS_EVENT_GROUPS=1
    )
endif()
if(FREERTOS_USE_TIMERS)
    target_compile_definitions(freertos_kernel PUBLIC
        MINI_TREE_FREERTOS_USE_TIMERS=1
    )
endif()

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

# ── ARM 端口对齐要求 ──
if(FREERTOS_PORT MATCHES "^GCC_ARM_")
    target_compile_options(freertos_kernel PRIVATE -mthumb -mno-unaligned-access)
endif()
