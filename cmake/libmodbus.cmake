# libmodbus — lib/libmodbus (v3.2.0). Prefer RTOS/POSIX; needs port config.h + modbus-version.h.
if(TARGET mini_tree_libmodbus)
    return()
endif()

set(MINI_TREE_LIBMODBUS_VERSION "3.2.0" CACHE STRING "libmodbus release version")
set(MINI_TREE_LIBMODBUS_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/libmodbus" CACHE PATH "libmodbus root")
set(_mb "${MINI_TREE_LIBMODBUS_DIR}/src")

if(NOT EXISTS "${_mb}/modbus.h")
    message(STATUS "mini_tree libmodbus: not found — skip")
    return()
endif()

add_library(mini_tree_libmodbus INTERFACE)
add_library(modbus::modbus ALIAS mini_tree_libmodbus)
target_sources(mini_tree_libmodbus INTERFACE
    "${_mb}/modbus.c"
    "${_mb}/modbus-data.c"
    "${_mb}/modbus-rtu.c"
    "${_mb}/modbus-tcp.c"
)
target_include_directories(mini_tree_libmodbus INTERFACE "${_mb}")
message(STATUS "mini_tree libmodbus: ${MINI_TREE_LIBMODBUS_VERSION}")

# mini_tree_link_libmodbus(<target> <port_dir>)
# port_dir must provide config.h and modbus-version.h (from .in or hand-written).
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
    target_link_libraries(${target} PUBLIC mini_tree_libmodbus)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
