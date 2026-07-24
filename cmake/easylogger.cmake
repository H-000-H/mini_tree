# EasyLogger — lib/EasyLogger (2.2.0). Needs elog_cfg.h + elog_port.c.
if(TARGET mini_tree_easylogger)
    return()
endif()

set(MINI_TREE_EASYLOGGER_VERSION "2.2.0" CACHE STRING "EasyLogger release version")
set(MINI_TREE_EASYLOGGER_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/EasyLogger" CACHE PATH "EasyLogger root")
set(_elog "${MINI_TREE_EASYLOGGER_DIR}/easylogger")

if(NOT EXISTS "${_elog}/inc/elog.h")
    message(STATUS "mini_tree EasyLogger: not found — skip")
    return()
endif()

add_library(mini_tree_easylogger INTERFACE)
add_library(easylogger::easylogger ALIAS mini_tree_easylogger)
target_sources(mini_tree_easylogger INTERFACE
    "${_elog}/src/elog.c"
    "${_elog}/src/elog_async.c"
    "${_elog}/src/elog_buf.c"
    "${_elog}/src/elog_utils.c"
)
target_include_directories(mini_tree_easylogger INTERFACE "${_elog}/inc")
message(STATUS "mini_tree EasyLogger: ${MINI_TREE_EASYLOGGER_VERSION}")

# mini_tree_link_easylogger(<target> <port_dir>)
function(mini_tree_link_easylogger target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_easylogger(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/elog_cfg.h")
        message(FATAL_ERROR "mini_tree_link_easylogger: elog_cfg.h not found in ${_port}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_easylogger)
    target_include_directories(${target} PUBLIC "${_port}")
    if(EXISTS "${_port}/elog_port.c")
        target_sources(${target} PUBLIC "${_port}/elog_port.c")
    elseif(EXISTS "${MINI_TREE_EASYLOGGER_DIR}/easylogger/port/elog_port.c")
        # sample port — usually replaced by board
        target_sources(${target} PUBLIC "${MINI_TREE_EASYLOGGER_DIR}/easylogger/port/elog_port.c")
    endif()
endfunction()
