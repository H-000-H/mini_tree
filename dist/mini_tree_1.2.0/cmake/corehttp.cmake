# coreHTTP — local lib/coreHTTP or FetchContent (v3.1.3 + llhttp submodule).
# Needs core_http_config.h in port_dir.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_COREHTTP_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_COREHTTP_CMAKE_LOADED ON)

set(MINI_TREE_COREHTTP_VERSION "v3.1.3" CACHE STRING "coreHTTP git tag")
message(STATUS "mini_tree coreHTTP: ${MINI_TREE_COREHTTP_VERSION} (local-or-fetch on link)")

function(mini_tree_link_corehttp target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_corehttp(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/core_http_config.h")
        message(FATAL_ERROR "mini_tree_link_corehttp: core_http_config.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_corehttp)
        mini_tree_dep_get(_http
            NAME corehttp
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/coreHTTP"
            MARKER "source/include/core_http_client.h"
            GIT_REPOSITORY https://github.com/FreeRTOS/coreHTTP.git
            GIT_TAG ${MINI_TREE_COREHTTP_VERSION}
            GIT_SUBMODULES_RECURSE
        )
        if(NOT EXISTS "${_http}/source/dependency/3rdparty/llhttp/src/llhttp.c")
            message(FATAL_ERROR "mini_tree_link_corehttp: llhttp missing after fetch (submodule)")
        endif()
        add_library(mini_tree_corehttp INTERFACE)
        add_library(corehttp::corehttp ALIAS mini_tree_corehttp)
        target_sources(mini_tree_corehttp INTERFACE
            "${_http}/source/core_http_client.c"
            "${_http}/source/dependency/3rdparty/llhttp/src/api.c"
            "${_http}/source/dependency/3rdparty/llhttp/src/http.c"
            "${_http}/source/dependency/3rdparty/llhttp/src/llhttp.c"
        )
        target_include_directories(mini_tree_corehttp INTERFACE
            "${_http}/source/include"
            "${_http}/source/interface"
            "${_http}/source/dependency/3rdparty/llhttp/include"
        )
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_corehttp)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
