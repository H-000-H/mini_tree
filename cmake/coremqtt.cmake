# coreMQTT — FreeRTOS coreMQTT v2 API, lib/coreMQTT (v5.0.2). Needs core_mqtt_config.h.
if(TARGET mini_tree_coremqtt)
    return()
endif()

set(MINI_TREE_COREMQTT_VERSION "5.0.2" CACHE STRING "coreMQTT release version")
set(MINI_TREE_COREMQTT_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/coreMQTT" CACHE PATH "coreMQTT root")
set(_mqtt_src "${MINI_TREE_COREMQTT_DIR}/source")

if(NOT EXISTS "${_mqtt_src}/include/core_mqtt.h")
    message(STATUS "mini_tree coreMQTT: not found — skip")
    return()
endif()

add_library(mini_tree_coremqtt INTERFACE)
add_library(coremqtt::coremqtt ALIAS mini_tree_coremqtt)
target_sources(mini_tree_coremqtt INTERFACE
    "${_mqtt_src}/core_mqtt.c"
    "${_mqtt_src}/core_mqtt_state.c"
    "${_mqtt_src}/core_mqtt_serializer.c"
    "${_mqtt_src}/core_mqtt_serializer_private.c"
    "${_mqtt_src}/core_mqtt_prop_serializer.c"
    "${_mqtt_src}/core_mqtt_prop_deserializer.c"
)
target_include_directories(mini_tree_coremqtt INTERFACE
    "${_mqtt_src}/include"
    "${_mqtt_src}/interface"
)
message(STATUS "mini_tree coreMQTT: ${MINI_TREE_COREMQTT_VERSION}")

# mini_tree_link_coremqtt(<target> <port_dir>)
function(mini_tree_link_coremqtt target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_coremqtt(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/core_mqtt_config.h")
        message(FATAL_ERROR "mini_tree_link_coremqtt: core_mqtt_config.h not found in ${_port}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_coremqtt)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
