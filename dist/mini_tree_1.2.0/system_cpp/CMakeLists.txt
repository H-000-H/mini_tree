# ==============================================================================
# system — 系统运行时 (看门狗、安全状态、任务管理)
# ==============================================================================
# 注意: system 组件部分源文件直接引用 FreeRTOS API.
# 当使用 RT-Thread OSAL 后端时, 这些文件仅编译通过但运行时不可用.
# 完整的多后端支持需要将这些文件重构为 OSAL 调用.
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

add_library(system STATIC
    "src/safe_state.c"
    "src/system_init.cpp"
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
