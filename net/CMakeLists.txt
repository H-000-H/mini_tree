include("${CMAKE_SOURCE_DIR}/cmake/coremqtt.cmake")

file(GLOB MINI_TREE_NET_PORT_SRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/port/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/pppif/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/tcp/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/transport_glue/*.c"
)

add_library(mini_tree_net STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_arch.c"
    ${MINI_TREE_NET_PORT_SRCS}
    port/pppif/pppif.h
    port/mqtt/mqtt_client.h
    port/mqtt/mqtt_client.c
    port/transport_glue/transport_glue.h
    port/transport_glue/transport_glue.c
    port/mqtt/core_mqtt_config.h
    port/net_error.h
)

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

# coreMQTT: 由 .config 桥接条件链接 (缺省视为启用, 对齐 Kconfig default y)
set(MINI_TREE_NET_DOTCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/../.config")
if(EXISTS "${MINI_TREE_NET_DOTCONFIG}")
    file(STRINGS "${MINI_TREE_NET_DOTCONFIG}" _net_coremqtt_off REGEX "^# CONFIG_NET_MQTT_USE_COREMQTT is not set$")
else()
    set(_net_coremqtt_off "")
endif()
if(NOT _net_coremqtt_off)
    mini_tree_link_coremqtt(mini_tree_net "${CMAKE_CURRENT_SOURCE_DIR}/port/mqtt")
endif()