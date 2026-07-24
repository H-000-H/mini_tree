# SFUD — SPI Flash 统一驱动, lib/SFUD (1.1.0). Needs sfud_cfg.h + sfud_port.c.
if(TARGET mini_tree_sfud)
    return()
endif()

set(MINI_TREE_SFUD_VERSION "1.1.0" CACHE STRING "SFUD release version")
set(MINI_TREE_SFUD_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/SFUD" CACHE PATH "SFUD source root")
set(_sfud "${MINI_TREE_SFUD_DIR}/sfud")

if(NOT EXISTS "${_sfud}/inc/sfud.h")
    message(STATUS "mini_tree SFUD: not found — skip")
    return()
endif()

add_library(mini_tree_sfud INTERFACE)
add_library(sfud::sfud ALIAS mini_tree_sfud)
target_sources(mini_tree_sfud INTERFACE
    "${_sfud}/src/sfud.c"
    "${_sfud}/src/sfud_sfdp.c"
)
target_include_directories(mini_tree_sfud INTERFACE "${_sfud}/inc")
message(STATUS "mini_tree SFUD: ${MINI_TREE_SFUD_VERSION} @ ${MINI_TREE_SFUD_DIR}")

# mini_tree_link_sfud(<target> <port_dir>)
function(mini_tree_link_sfud target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_sfud(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/sfud_cfg.h")
        message(FATAL_ERROR "mini_tree_link_sfud: sfud_cfg.h not found in ${_port}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_sfud)
    target_include_directories(${target} PUBLIC "${_port}")
    if(EXISTS "${_port}/sfud_port.c")
        target_sources(${target} PUBLIC "${_port}/sfud_port.c")
    endif()
endfunction()
