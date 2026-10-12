# ==============================================================================
# core — 核心服务 (事件总线、日志、关键数据)
# ==============================================================================
# 系统核心服务，.引用 system 头文件 (safe_state, wdt) 通过 PRIVATE
# 路径解决循环依赖，链接时由根 CMakeLists 统一处理。
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

add_library(core STATIC
    "src/event_bus.c"
    "src/mini_backend_bare.c"
    "src/mini_backend_freertos.c"
    "src/mini_backend_mini_os.c"
    "src/mini_backend_rtthread.c"
    "src/mini_panic.c"
    "src/mini_slot.c"
    "src/mini_time.c"
    "src/status.c"
)

target_include_directories(core
    PUBLIC
        "include"
        "${KCONFIG_GEN_DIR}"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/system_c/include"
        # 三个 OS 后端实现读 board_config.h 取池尺寸 (MINI_MUTEX_POOL_SIZE / MINI_SEM_POOL_SIZE)
        "${PROJECT_SOURCE_DIR}/board/include"
)

# core ↔ system 存在循环符号依赖（event_bus ↔ safe_state/wdt），
# CMake 自动为 STATIC 库循环使用 --start-group/--end-group
target_link_libraries(core
    PUBLIC
        system
)

add_dependencies(core kconfig_generate)
