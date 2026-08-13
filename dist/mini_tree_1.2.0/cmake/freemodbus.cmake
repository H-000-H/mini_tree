# FreeModbus — local lib/FreeModbus or FetchContent (1.6.0). On link.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_FREEMODBUS_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_FREEMODBUS_CMAKE_LOADED ON)

set(MINI_TREE_FREEMODBUS_VERSION "1.6.0" CACHE STRING "FreeModbus git tag")
message(STATUS "mini_tree FreeModbus: ${MINI_TREE_FREEMODBUS_VERSION} (local-or-fetch on link)")

function(mini_tree_link_freemodbus target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_freemodbus(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/mbport.h")
        message(FATAL_ERROR "mini_tree_link_freemodbus: mbport.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_freemodbus)
        mini_tree_dep_get(_fm_root
            NAME freemodbus
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/FreeModbus"
            MARKER "modbus/mb.c"
            GIT_REPOSITORY https://github.com/cwalter-at/freemodbus.git
            GIT_TAG ${MINI_TREE_FREEMODBUS_VERSION}
        )
        set(_mb "${_fm_root}/modbus")
        add_library(mini_tree_freemodbus INTERFACE)
        add_library(freemodbus::freemodbus ALIAS mini_tree_freemodbus)
        target_sources(mini_tree_freemodbus INTERFACE
            ${_mb}/mb.c
            ${_mb}/rtu/mbcrc.c
            ${_mb}/rtu/mbrtu.c
            ${_mb}/functions/mbfunccoils.c
            ${_mb}/functions/mbfuncdiag.c
            ${_mb}/functions/mbfuncdisc.c
            ${_mb}/functions/mbfuncholding.c
            ${_mb}/functions/mbfuncinput.c
            ${_mb}/functions/mbfuncother.c
            ${_mb}/functions/mbutils.c
        )
        target_include_directories(mini_tree_freemodbus INTERFACE
            "${_mb}/include"
            "${_mb}/rtu"
        )
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_freemodbus)
    target_include_directories(${target} PUBLIC "${_port}")
    file(GLOB _port_srcs "${_port}/*.c")
    if(_port_srcs)
        target_sources(${target} PUBLIC ${_port_srcs})
    endif()
endfunction()
