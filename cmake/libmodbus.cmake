# libmodbus — local lib/libmodbus or FetchContent (v3.1.11 closest / v3.1.10).
# Prefer RTOS/POSIX; port_dir needs config.h + modbus-version.h.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_LIBMODBUS_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_LIBMODBUS_CMAKE_LOADED ON)

# Upstream tags are v3.1.x; tree previously labeled 3.2.0 — pin a known release tag.
set(MINI_TREE_LIBMODBUS_VERSION "v3.1.10" CACHE STRING "libmodbus git tag")
message(STATUS "mini_tree libmodbus: ${MINI_TREE_LIBMODBUS_VERSION} (local-or-fetch on link)")

function(mini_tree_link_libmodbus target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_libmodbus(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/config.h")
        message(FATAL_ERROR "mini_tree_link_libmodbus: config.h not found in ${_port}")
    endif()
    if(NOT EXISTS "${_port}/modbus-version.h")
        message(FATAL_ERROR "mini_tree_link_libmodbus: modbus-version.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_libmodbus)
        mini_tree_dep_get(_mb_root
            NAME libmodbus
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/libmodbus"
            MARKER "src/modbus.h"
            GIT_REPOSITORY https://github.com/stephane/libmodbus.git
            GIT_TAG ${MINI_TREE_LIBMODBUS_VERSION}
        )
        set(_mb "${_mb_root}/src")
        add_library(mini_tree_libmodbus INTERFACE)
        add_library(modbus::modbus ALIAS mini_tree_libmodbus)
        target_sources(mini_tree_libmodbus INTERFACE
            "${_mb}/modbus.c"
            "${_mb}/modbus-data.c"
            "${_mb}/modbus-rtu.c"
            "${_mb}/modbus-tcp.c"
        )
        target_include_directories(mini_tree_libmodbus INTERFACE "${_mb}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_libmodbus)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
