# ==============================================================================
# hal_if — 硬件抽象接口层 (Hardware Abstraction Layer Interface)
# ==============================================================================
# 定义 HAL 接口头文件，不含具体硬件实现。
# 各平台通过 soc_port_* 组件提供实现。
# ==============================================================================

add_library(hal_if STATIC
    "src/hal_if_dummy.c"
)

target_include_directories(hal_if
    PUBLIC
        "include"
)

add_dependencies(hal_if kconfig_generate)
