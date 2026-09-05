include("${CMAKE_SOURCE_DIR}/cmake/coremqtt.cmake")

file(GLOB MINI_TREE_NET_PORT_SRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/port/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/pppif/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/tcp/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/transport_glue/*.c"
)

# coreMQTT 门控 (fail-closed): 只有 .config 里显式 CONFIG_NET_MQTT_USE_COREMQTT=y
# 才编入 MQTT 包装层并链接 coreMQTT。键缺失 / .config 不存在 / 读取失败一律视为
# 关闭 —— 离线环境不会意外触发 FetchContent 网络拉取 (SSH 地址离线无法 clone)。
# 旧实现是 fail-open (只找 "is not set" 的关闭证据, 找不到就当开启), 已废弃。
set(MINI_TREE_NET_DOTCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/../.config")
set(_net_mqtt_on "")
if(EXISTS "${MINI_TREE_NET_DOTCONFIG}")
    file(STRINGS "${MINI_TREE_NET_DOTCONFIG}" _net_mqtt_y REGEX "^CONFIG_NET_MQTT_USE_COREMQTT=y$")
    set(_net_mqtt_on "${_net_mqtt_y}")
endif()

add_library(mini_tree_net STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_arch.c"
    ${MINI_TREE_NET_PORT_SRCS}
    port/pppif/pppif.h
    port/transport_glue/transport_glue.h
    port/transport_glue/transport_glue.c
    port/net_error.h
)

if(_net_mqtt_on)
    # mqtt_client.h 顶部无条件 #include "core_mqtt.h" —— 包装层源文件必须随开关
    # 一同编入, 否则关闭开关后仍编译 mqtt_client.c 会因 coreMQTT 头缺失而构建失败
    # (修复: Kconfig "关闭后 MQTT 客户端不参与链接" 由此真正生效)。
    # transport_glue 无 coreMQTT 头依赖, 保持无条件编入 (未引用部分由 gc-sections 回收)。
    target_sources(mini_tree_net PRIVATE
        port/mqtt/mqtt_client.h
        port/mqtt/mqtt_client.c
        port/mqtt/core_mqtt_config.h
    )
endif()

target_include_directories(mini_tree_net PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/port"
    "${CMAKE_SOURCE_DIR}/lib/lwip/src/include"
    "${CMAKE_SOURCE_DIR}/osal/include"
    "${CMAKE_SOURCE_DIR}/core/include"
    "${CMAKE_SOURCE_DIR}/algorithm/buffer"
    "${CMAKE_SOURCE_DIR}/board/include"
    "${CMAKE_SOURCE_DIR}/hal/gpio"
    "${CMAKE_SOURCE_DIR}/system_c/include"
    "${CMAKE_SOURCE_DIR}/time_slice/task"
    "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree"
    "${CMAKE_BINARY_DIR}/generated/board/mini_tree"
)
mini_tree_link_lwip(mini_tree_net "${CMAKE_CURRENT_SOURCE_DIR}")

if(_net_mqtt_on)
    mini_tree_link_coremqtt(mini_tree_net "${CMAKE_CURRENT_SOURCE_DIR}/port/mqtt")
endif()

# 本地扩展 (不入库): 存在 local.cmake 时加载, 承载本地新增模块的源文件与链接
include("${CMAKE_CURRENT_SOURCE_DIR}/local.cmake" OPTIONAL)
