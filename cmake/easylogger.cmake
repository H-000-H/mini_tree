# EasyLogger — local lib/EasyLogger or FetchContent (2.2.0). On link.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_EASYLOGGER_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_EASYLOGGER_CMAKE_LOADED ON)

set(MINI_TREE_EASYLOGGER_VERSION "2.2.0" CACHE STRING "EasyLogger git tag")
message(STATUS "mini_tree EasyLogger: ${MINI_TREE_EASYLOGGER_VERSION} (local-or-fetch on link)")

function(mini_tree_link_easylogger target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_easylogger(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/elog_cfg.h")
        message(FATAL_ERROR "mini_tree_link_easylogger: elog_cfg.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_easylogger)
        mini_tree_dep_get(_elog_root
            NAME easylogger
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/EasyLogger"
            MARKER "easylogger/inc/elog.h"
            GIT_REPOSITORY https://github.com/armink/EasyLogger.git
            GIT_TAG ${MINI_TREE_EASYLOGGER_VERSION}
        )
        set(_elog "${_elog_root}/easylogger")
        add_library(mini_tree_easylogger INTERFACE)
        add_library(easylogger::easylogger ALIAS mini_tree_easylogger)
        target_sources(mini_tree_easylogger INTERFACE
            "${_elog}/src/elog.c"
            "${_elog}/src/elog_async.c"
            "${_elog}/src/elog_buf.c"
            "${_elog}/src/elog_utils.c"
        )
        target_include_directories(mini_tree_easylogger INTERFACE "${_elog}/inc")
        set(MINI_TREE_EASYLOGGER_DIR "${_elog_root}" CACHE PATH "" FORCE)
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_easylogger)
    target_include_directories(${target} PUBLIC "${_port}")
    if(EXISTS "${_port}/elog_port.c")
        target_sources(${target} PUBLIC "${_port}/elog_port.c")
    elseif(EXISTS "${MINI_TREE_EASYLOGGER_DIR}/easylogger/port/elog_port.c")
        target_sources(${target} PUBLIC "${MINI_TREE_EASYLOGGER_DIR}/easylogger/port/elog_port.c")
    endif()
endfunction()
