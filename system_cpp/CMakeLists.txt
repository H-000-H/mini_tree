# ==============================================================================
# system — 系统运行时 (生命周期、看门狗、安全状态、任务管理)
# ==============================================================================
# 注意: system 组件部分源文件直接引用 FreeRTOS API.
# 当使用 RT-Thread OSAL 后端时, 这些文件仅编译通过但运行时不可用.
# 完整的多后端支持需要将这些文件重构为 OSAL 调用.
# ==============================================================================

# ── OSAL 后端选择 (用于决定 FreeRTOS 兼容头文件路径) ──
set(OSAL_BACKEND "FREERTOS")
if(EXISTS "${PROJECT_SOURCE_DIR}/.config")
    file(STRINGS "${PROJECT_SOURCE_DIR}/.config" _osal_choice REGEX "^CONFIG_OSAL_")
    if(_osal_choice STREQUAL "CONFIG_OSAL_RTTHREAD=y")
        set(OSAL_BACKEND "RTTHREAD")
    endif()
endif()

add_library(system STATIC
    "src/lifecycle.cpp"
    "src/safe_state.c"
    "src/system_init.cpp"
    "src/system_runtime.cpp"
    "src/system_scrubber.cpp"
    "src/system_wdt.cpp"
    "src/task_manager.cpp"
)

target_include_directories(system
    PUBLIC
        "include"
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
