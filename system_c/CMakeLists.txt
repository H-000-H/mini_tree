# ==============================================================================
# system_c — 系统运行时 (C 版本)
# ==============================================================================
# 与 system_cpp 功能完全一致, 纯 C 实现.
# 通过 Kconfig SYSTEM_BACKEND 选择编译哪一个.
# ==============================================================================

add_library(system STATIC
    "${CMAKE_SOURCE_DIR}/system_cpp/src/safe_state.c"
    "src/system_init.c"
    "src/system_scrubber.c"
    "src/system_wdt.c"
    "src/task_manager.c"
)

target_include_directories(system
    PUBLIC
        "include"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/system_cpp/include"
        "${CMAKE_SOURCE_DIR}/hal_if/include"
)

target_link_libraries(system
    PUBLIC
        core
    PRIVATE
        board
)

add_dependencies(system kconfig_generate)
