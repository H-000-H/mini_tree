# ==============================================================================
# mini_tree — 通用嵌入式中间件框架
# ==============================================================================
# 用户工程通过 add_subdirectory 引入, 链接 mini_tree 接口库即可:
#
#   add_subdirectory(path/to/mini_tree)
#   target_link_libraries(my_app PRIVATE mini_tree)
#
# 使用示例:
#
#   ── C++ 版本 (system_cpp, 默认) ──
#   #include "system_init.hpp"
#   int main(void) {
#       platform_init();
#       MiniTree::System_Pre_OS_Init();     // Phase 1
#       MiniTree::System_Start_Tasks();     // Phase 2
#   #ifdef CONFIG_OSAL_NULL
#       while (1) { MiniTree::System_Loop(); }
#   #else
#       vTaskStartScheduler();
#   #endif
#   }
#
#   ── C 版本 (system_c) ──
#   #include "system_init.h"
#   int main(void) {
#       platform_init();
#       mini_tree_pre_os_init();            // Phase 1
#       mini_tree_start_tasks();            // Phase 2
#   #ifdef CONFIG_OSAL_NULL
#       while (1) { mini_tree_system_loop(); }
#   #else
#       vTaskStartScheduler();
#   #endif
#   }
#
# 配置方式 (三选一):
#   1. menuconfig (推荐) — 运行 tools/kconfig_gui.py 生成 .config
#   2. -DFREERTOS_PORT=GCC_ARM_CM3 — 跳过 Kconfig, 直接指定 FreeRTOS 后端+架构
#   3. 无配置 — 默认 FreeRTOS + POSIX (本地编译验证)
#
# 用户需提供:
#   所有后端: hal_* 符号 (硬件抽象层实现)
#   FreeRTOS: FreeRTOSConfig.h (通过用户 include 路径注入)
# ==============================================================================

cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# 1. 项目定义
# ---------------------------------------------------------------------------
project(mini_tree VERSION 1.0.0 LANGUAGES C CXX ASM)

# ---------------------------------------------------------------------------
# 1.5 跨平台工具链拦截锁
# ---------------------------------------------------------------------------
# 当配置为嵌入式目标（非 POSIX）但未指定工具链文件时，告警引导
#   -DFREERTOS_PORT 方式: 明确指定 → 必须配工具链
#   Kconfig 方式 (.config) : 从 .config 读取平台 → 也可判断是否嵌入式
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(_needs_toolchain FALSE)
    if(NOT (FREERTOS_PORT STREQUAL ""))
        set(_needs_toolchain TRUE)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.config")
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.config" _plat REGEX "^CONFIG_PLATFORM_(ARM_CM|RISCV)=y")
        if(_plat)
            set(_needs_toolchain TRUE)
        endif()
        unset(_plat)
    endif()
    if(_needs_toolchain)
        message(WARNING "【架构警报】: 正在交叉编译嵌入式目标，但未指定 CMAKE_TOOLCHAIN_FILE!\n"
                        "建议使用:\n"
                        "  cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm4f.cmake ..\n"
                        "可用的工具链文件:\n"
                        "  cmake/toolchain_arm_cm3.cmake   - ARM Cortex-M3\n"
                        "  cmake/toolchain_arm_cm4f.cmake  - ARM Cortex-M4F\n"
                        "  cmake/toolchain_arm_cm7.cmake   - ARM Cortex-M7\n"
                        "  cmake/toolchain_riscv.cmake     - RISC-V (32-bit)")
    endif()
    unset(_needs_toolchain)
endif()

# ---------------------------------------------------------------------------
# 2. 构建类型与全局编译标志
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ── 编译器通用标志 (GCC / Clang / ARMClang 均支持) ──
add_compile_options(
    $<$<CONFIG:Release>:-DNDEBUG>
)

# ── GCC / Clang 标志 (ARMClang 不支持部分调试选项) ──
if(CMAKE_C_COMPILER_ID STREQUAL "ARMClang")
    # ARMCLANG: -Og / -ggdb 不支持, 调试信息由工具链文件的 -g 提供
    add_compile_options(
        -Wall -Wextra -Werror=implicit-function-declaration
        $<$<CONFIG:Release>:-Os>
        # C++ 裸机环境: 禁用异常和 RTTI
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        -include "${CMAKE_BINARY_DIR}/generated/kconfig/config.h"
    )
else()
    # GCC / Clang (完整功能)
    add_compile_options(
        -Wall -Wextra -Werror=implicit-function-declaration
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Debug>:-ggdb>
        $<$<CONFIG:Release>:-Os>
        # C++ 裸机环境: 禁用异常和 RTTI
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        -include "${CMAKE_BINARY_DIR}/generated/kconfig/config.h"
    )
endif()

# ── C/C++ 标准 ──
# ARMClang: CMake 原生支持 C11/C20, 用 CMAKE_C_STANDARD 即可.
# GCC/Clang: 显式 -std= 标志, 回避 CMake ≤3.28 对 ARM GCC 13
#   C23 映射不全的问题 (arm-none-eabi-gcc 13 用 -std=c2x 而非 -std=c23).
#   工具链文件自带 -std=, 会覆盖本处标志 (last -std= wins), 无冲突.
if(CMAKE_C_COMPILER_ID STREQUAL "ARMClang")
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
else()
    if(CMAKE_C_COMPILER_VERSION VERSION_LESS "14.0")
        add_compile_options($<$<COMPILE_LANGUAGE:C>:-std=c17>)
    else()
        add_compile_options($<$<COMPILE_LANGUAGE:C>:-std=c17>)
    endif()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++17>)
endif()

# ── Kconfig C/C++ 标准覆盖 (CUSTOM_C_STANDARD, opt-in) ──
# 当 .config 中存在 CONFIG_CUSTOM_C_STANDARD=y 时,
# 用 Kconfig 中的 C_STANDARD / CXX_STANDARD 替换 CMake 默认值.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.config")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.config" _std_config)
    if(_std_config MATCHES "CONFIG_CUSTOM_C_STANDARD=y")
        string(REGEX MATCH "CONFIG_C_STANDARD=\"([^\"]*)\"" _dummy "${_std_config}")
        if(CMAKE_MATCH_1)
            set(CMAKE_C_STANDARD "")
            add_compile_options($<$<COMPILE_LANGUAGE:C>:-std=${CMAKE_MATCH_1}>)
        endif()
        string(REGEX MATCH "CONFIG_CXX_STANDARD=\"([^\"]*)\"" _dummy "${_std_config}")
        if(CMAKE_MATCH_1)
            set(CMAKE_CXX_STANDARD "")
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=${CMAKE_MATCH_1}>)
        endif()
    endif()
    unset(_std_config)
endif()

# ---------------------------------------------------------------------------
# 3. FreeRTOS 端口配置 (由用户工程通过 -D 指定或 Kconfig 派生)
# ---------------------------------------------------------------------------
set(FREERTOS_PORT "" CACHE STRING "FreeRTOS 架构端口 (空 = 从 Kconfig 派生)")
set(FREERTOS_HEAP "4" CACHE STRING "FreeRTOS 堆分配器 (1-5)")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/.config")

# ---------------------------------------------------------------------------
# 4. Python3 & Kconfig 代码生成
# ---------------------------------------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig")
set(KCONFIG_SRC     "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig")
set(KCONFIG_DOT     "${CMAKE_CURRENT_SOURCE_DIR}/.config")
set(KCONFIG_OUT     "${KCONFIG_GEN_DIR}/config.h")

add_custom_command(
    OUTPUT  "${KCONFIG_OUT}"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/genconfig.py"
            "${KCONFIG_SRC}" "${KCONFIG_GEN_DIR}" --config "${KCONFIG_DOT}"
    DEPENDS "${KCONFIG_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/genconfig.py"
    COMMENT "生成 config.h ← Kconfig"
    VERBATIM
)

add_custom_target(kconfig_generate
    DEPENDS "${KCONFIG_OUT}"
)

# ── 平台架构编译标志 (由 Kconfig PLATFORM_MCU 或 -DFREERTOS_PORT 决定) ──
if(FREERTOS_PORT MATCHES "^GCC_ARM_CM([0-9]+F?)$")
    set(_arm_core "${CMAKE_MATCH_1}")
elseif(FREERTOS_PORT STREQUAL "GCC_RISC_V")
    set(_riscv TRUE)
elseif(FREERTOS_PORT STREQUAL "")
    # 未设置 FREERTOS_PORT (RT-Thread 后端) → 从 .config 读取
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.config")
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.config" _kconfig_platform REGEX "^CONFIG_PLATFORM_")
        if(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM7=y")
            set(_arm_core "7")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM4F=y")
            set(_arm_core "4F")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_ARM_CM3=y")
            set(_arm_core "3")
        elseif(_kconfig_platform STREQUAL "CONFIG_PLATFORM_RISCV=y")
            set(_riscv TRUE)
        endif()
    endif()
endif()

if(NOT CMAKE_TOOLCHAIN_FILE)
    if(_arm_core STREQUAL "4F")
        add_compile_options(-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16)
    elseif(_arm_core STREQUAL "3")
        add_compile_options(-mcpu=cortex-m3 -mthumb)
    elseif(_arm_core STREQUAL "7")
        add_compile_options(-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16)
    endif()
endif()
if(_riscv)
    add_compile_options(-march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany)
endif()
# ---------------------------------------------------------------------------
# 4. Kconfig 静态断言防御 (CMake Static Assert)
# ---------------------------------------------------------------------------
# 注意：此检查在 _arm_core / _riscv 销毁前执行，以便复用平台检测结果
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.config")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.config" _config_content)

    # 防御 1: OSAL 后端冲突检测（Kconfig choice 理论上应保证互斥，此为双重保险）
    if(_config_content MATCHES "CONFIG_OSAL_FREERTOS=y" AND
       _config_content MATCHES "CONFIG_OSAL_RTTHREAD=y")
        message(FATAL_ERROR "【架构熔断】: CONFIG_OSAL_FREERTOS 与 CONFIG_OSAL_RTTHREAD 不可同时使能！请重新运行 menuconfig。")
    endif()

    # 防御 2: 已选 OSAL 后端却未指定平台架构
    if((_config_content MATCHES "CONFIG_OSAL_RTTHREAD=y" OR
        _config_content MATCHES "CONFIG_OSAL_FREERTOS=y") AND
       NOT _arm_core AND NOT _riscv)
        message(FATAL_ERROR "【架构熔断】: 已选 OSAL 后端，但未配置目标 MCU 平台！请运行 menuconfig 选择 Platform。")
    endif()

    unset(_config_content)
else()
    message(WARNING "【构建警告】: 未检测到 .config 配置文件。将依赖 -DFREERTOS_PORT 或使用默认配置。")
endif()

unset(_arm_core)
unset(_riscv)

# ---------------------------------------------------------------------------
# 5. 通用组件子目录 (不包含任何 MCU 平台代码)
# ---------------------------------------------------------------------------
add_subdirectory(algorithm)
add_subdirectory(hal_if)
add_subdirectory(osal)
add_subdirectory(core)
add_subdirectory(board)
# ── 系统后端 (Kconfig SYSTEM_CPP / SYSTEM_C) ──
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.config")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.config" _system_backend REGEX "^CONFIG_SYSTEM_")
    if(_system_backend STREQUAL "CONFIG_SYSTEM_C=y")
        add_subdirectory(system_c)
        set(MINI_TREE_SYSTEM_INC "${CMAKE_CURRENT_SOURCE_DIR}/system_c/include")
    else()
        add_subdirectory(system_cpp)
        set(MINI_TREE_SYSTEM_INC "${CMAKE_CURRENT_SOURCE_DIR}/system_cpp/include")
    endif()
    unset(_system_backend)
else()
    # 默认使用 C++ 版本
    add_subdirectory(system_cpp)
    set(MINI_TREE_SYSTEM_INC "${CMAKE_CURRENT_SOURCE_DIR}/system_cpp/include")
endif()
add_subdirectory(display_if)
add_subdirectory(lib)

# ---------------------------------------------------------------------------
# 6. mini_tree 接口库 — 用户工程通过 target_link_libraries(... mini_tree) 引入
# ---------------------------------------------------------------------------
add_library(mini_tree INTERFACE)

target_link_libraries(mini_tree INTERFACE
    algorithm
    board
    core
    display_if
    hal_if
    lib
    osal
    system
)

# 将框架头文件路径和 Kconfig 生成路径暴露给用户工程
target_include_directories(mini_tree INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/board/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/hal_if/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/osal/include>
    $<BUILD_INTERFACE:${MINI_TREE_SYSTEM_INC}>
    $<BUILD_INTERFACE:${KCONFIG_GEN_DIR}>
)

# ── 反汇编 (由 Kconfig CONFIG_BUILD_DISASM 控制) ──
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.config")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.config" _disasm REGEX "^CONFIG_BUILD_DISASM=y")
    if(_disasm STREQUAL "CONFIG_BUILD_DISASM=y")
        set(CONFIG_BUILD_DISASM TRUE)
    endif()
endif()

if(CONFIG_BUILD_DISASM)
    # ── 反汇编 lst 生成 ──
    if(CMAKE_OBJDUMP)
        set(_disasm_dir "${CMAKE_BINARY_DIR}/disasm")
        add_custom_target(disasm ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_disasm_dir}"
            COMMAND ${CMAKE_OBJDUMP} -d "$<TARGET_FILE:algorithm>" > "${_disasm_dir}/algorithm.lst" 2>&1 || true
            COMMAND ${CMAKE_OBJDUMP} -d "$<TARGET_FILE:hal_if>"     > "${_disasm_dir}/hal_if.lst"     2>&1 || true
            COMMAND ${CMAKE_OBJDUMP} -d "$<TARGET_FILE:osal>"       > "${_disasm_dir}/osal.lst"       2>&1 || true
            COMMAND ${CMAKE_OBJDUMP} -d "$<TARGET_FILE:core>"       > "${_disasm_dir}/core.lst"       2>&1 || true
            COMMAND ${CMAKE_OBJDUMP} -d "$<TARGET_FILE:board>"      > "${_disasm_dir}/board.lst"      2>&1 || true
            COMMAND ${CMAKE_OBJDUMP} -d "$<TARGET_FILE:system>"     > "${_disasm_dir}/system.lst"     2>&1 || true
            COMMENT "Generating disassembly listings..."
            DEPENDS algorithm board core hal_if osal system
        )
    endif()

    # ── 固件空间占用报告 (CONFIG_BUILD_DISASM 开启时自动启用) ──
    if(CMAKE_SIZE)
        add_custom_target(size_report ALL
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND ${CMAKE_COMMAND} -E echo "         mini_tree 组件静态空间占用报告"
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND ${CMAKE_SIZE} -B "$<TARGET_FILE:core>"   || true
            COMMAND ${CMAKE_SIZE} -B "$<TARGET_FILE:board>"  || true
            COMMAND ${CMAKE_SIZE} -B "$<TARGET_FILE:osal>"   || true
            COMMAND ${CMAKE_SIZE} -B "$<TARGET_FILE:system>" || true
            DEPENDS board core osal system
        )
    endif()
endif()
