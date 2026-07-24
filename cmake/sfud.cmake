# SFUD — local lib/SFUD or FetchContent (1.1.0). On link.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_SFUD_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_SFUD_CMAKE_LOADED ON)

set(MINI_TREE_SFUD_VERSION "1.1.0" CACHE STRING "SFUD git tag")
message(STATUS "mini_tree SFUD: ${MINI_TREE_SFUD_VERSION} (local-or-fetch on link)")

function(mini_tree_link_sfud target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_sfud(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/sfud_cfg.h")
        message(FATAL_ERROR "mini_tree_link_sfud: sfud_cfg.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_sfud)
        mini_tree_dep_get(_sfud_root
            NAME sfud
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/SFUD"
            MARKER "sfud/inc/sfud.h"
            GIT_REPOSITORY https://github.com/armink/SFUD.git
            GIT_TAG ${MINI_TREE_SFUD_VERSION}
        )
        set(_sfud "${_sfud_root}/sfud")
        add_library(mini_tree_sfud INTERFACE)
        add_library(sfud::sfud ALIAS mini_tree_sfud)
        target_sources(mini_tree_sfud INTERFACE
            "${_sfud}/src/sfud.c"
            "${_sfud}/src/sfud_sfdp.c"
        )
        target_include_directories(mini_tree_sfud INTERFACE "${_sfud}/inc")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_sfud)
    target_include_directories(${target} PUBLIC "${_port}")
    if(EXISTS "${_port}/sfud_port.c")
        target_sources(${target} PUBLIC "${_port}/sfud_port.c")
    endif()
endfunction()
