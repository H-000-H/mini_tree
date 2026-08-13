# ==============================================================================
# core — 核心服务 (事件总线、日志、关键数据)
# ==============================================================================
# 系统核心服务，依赖 osal。引用 system 头文件 (safe_state, wdt) 通过 PRIVATE
# 路径解决循环依赖，链接时由根 CMakeLists 统一处理。
# ==============================================================================

# ── OSAL 后端选择 (默认裸机; 读 mini_tree/.config) ──
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

add_library(core STATIC
    "src/event_bus.cpp"
    "src/production_log.c"
    "src/buffer_pool.c"
)

target_include_directories(core
    PUBLIC
        "include"
        "${KCONFIG_GEN_DIR}"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/system_cpp/include"
        "${PROJECT_SOURCE_DIR}/hal_bus/include"
        "${PROJECT_SOURCE_DIR}/hal_inst/include"
        "${PROJECT_SOURCE_DIR}/hal_if/include"
)

# core ↔ system 存在循环符号依赖（event_bus ↔ safe_state/wdt），
# CMake 自动为 STATIC 库循环使用 --start-group/--end-group
target_link_libraries(core
    PUBLIC
        osal
        system
)

add_dependencies(core kconfig_generate)
