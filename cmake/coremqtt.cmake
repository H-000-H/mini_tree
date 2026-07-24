# coreMQTT — local lib/coreMQTT or FetchContent (v5.0.2). Needs core_mqtt_config.h.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_COREMQTT_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_COREMQTT_CMAKE_LOADED ON)

set(MINI_TREE_COREMQTT_VERSION "v5.0.2" CACHE STRING "coreMQTT git tag")
message(STATUS "mini_tree coreMQTT: ${MINI_TREE_COREMQTT_VERSION} (local-or-fetch on link)")

function(mini_tree_link_coremqtt target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_coremqtt(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/core_mqtt_config.h")
        message(FATAL_ERROR "mini_tree_link_coremqtt: core_mqtt_config.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_coremqtt)
        mini_tree_dep_get(_mqtt_root
            NAME coremqtt
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/coreMQTT"
            MARKER "source/include/core_mqtt.h"
            GIT_REPOSITORY https://github.com/FreeRTOS/coreMQTT.git
            GIT_TAG ${MINI_TREE_COREMQTT_VERSION}
        )
        set(_mqtt_src "${_mqtt_root}/source")
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
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_coremqtt)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
