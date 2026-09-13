# ==============================================================================
# system — 系统运行时 (看门狗、安全状态、任务管理)
# ==============================================================================
# 注意: system 组件部分源文件直接引用 FreeRTOS API.
# 当使用 RT-Thread 后端时, 这些文件仅编译通过但运行时不可用.
# 完整的多后端支持需要将这些文件重构为统一接口调用.
# ==============================================================================

# ── OS 后端选择 (默认裸机; 读 mini_tree/.config) ──
set(OS_BACKEND "BARE")
set(_MINI_TREE_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../.config")
if(EXISTS "${_MINI_TREE_CONFIG}")
    file(STRINGS "${_MINI_TREE_CONFIG}" _os_choice REGEX "^CONFIG_OS_(MINI_OS|FREERTOS|RTTHREAD|BARE)=y$")
    if(_os_choice STREQUAL "CONFIG_OS_FREERTOS=y")
        set(OS_BACKEND "FREERTOS")
    elseif(_os_choice STREQUAL "CONFIG_OS_RTTHREAD=y")
        set(OS_BACKEND "RTTHREAD")
    elseif(_os_choice STREQUAL "CONFIG_OS_BARE=y")
        set(OS_BACKEND "BARE")
    endif()
endif()

# 系统运行时为纯 C, 源码位于 ../system_c/src; system_cpp/ 现仅保留可选的命令模块
add_library(system STATIC
    "../system_c/src/system_init.c"
    "../system_c/src/system_wdt.c"
    "../system_c/src/task_manager.c"
    "../system_c/src/safe_state.c"
)

# system_scrubber 的校验原语来自 mini-ota (image_verify_area, 不再自带 CRC 表),
# 故仅在 CONFIG_SYSTEM_SCRUBBER(已依赖 CONFIG_MINI_OTA) 开启时编入。
if(EXISTS "${_MINI_TREE_CONFIG}")
    file(STRINGS "${_MINI_TREE_CONFIG}" _scrubber_choice REGEX "^CONFIG_SYSTEM_SCRUBBER=y$")
    if(_scrubber_choice)
        target_sources(system PRIVATE "../system_c/src/system_scrubber.c")
    endif()
endif()

# SystemCmd (命令分发器) 是系统层唯一保留的 C++ 模块, 仅 CONFIG_SYSTEM_CMD=y 时编入
if(EXISTS "${_MINI_TREE_CONFIG}")
    file(STRINGS "${_MINI_TREE_CONFIG}" _sys_cmd_choice REGEX "^CONFIG_SYSTEM_CMD=y$")
    if(_sys_cmd_choice)
        target_sources(system PRIVATE "src/system_cmd.cpp")
    endif()
endif()

target_include_directories(system
    PUBLIC
        "include"
        "${PROJECT_SOURCE_DIR}/system_c/include"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/hal_bus/include"
        "${PROJECT_SOURCE_DIR}/hal_inst/include"
        "${PROJECT_SOURCE_DIR}/hal_if/include"
)

target_link_libraries(system
    PUBLIC
        core
    PRIVATE
        board
)

add_dependencies(system kconfig_generate)
