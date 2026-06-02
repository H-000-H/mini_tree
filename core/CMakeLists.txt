# ==============================================================================
# core — 核心服务 (事件总线、日志、关键数据)
# ==============================================================================
# 系统核心服务，依赖 osal。引用 system 头文件 (safe_state, wdt) 通过 PRIVATE
# 路径解决循环依赖，链接时由根 CMakeLists 统一处理。
# ==============================================================================

# ── OSAL 后端选择 ──
set(OSAL_BACKEND "FREERTOS")
if(EXISTS "${CMAKE_SOURCE_DIR}/.config")
    file(STRINGS "${CMAKE_SOURCE_DIR}/.config" _osal_choice REGEX "^CONFIG_OSAL_")
    if(_osal_choice STREQUAL "CONFIG_OSAL_RTTHREAD=y")
        set(OSAL_BACKEND "RTTHREAD")
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
        "${CMAKE_SOURCE_DIR}/system_cpp/include"
        "${CMAKE_SOURCE_DIR}/hal_if/include"
)

# core ↔ system 存在循环符号依赖（event_bus ↔ safe_state/wdt），
# CMake 自动为 STATIC 库循环使用 --start-group/--end-group
target_link_libraries(core
    PUBLIC
        osal
        system
)

add_dependencies(core kconfig_generate)
