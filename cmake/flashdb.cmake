# FlashDB — vendored under lib/FlashDB (2.2.0). Needs fdb_cfg.h (+ FAL/port).
if(TARGET mini_tree_flashdb)
    return()
endif()

set(MINI_TREE_FLASHDB_VERSION "2.2.0" CACHE STRING "FlashDB release version")
set(MINI_TREE_FLASHDB_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/FlashDB" CACHE PATH "FlashDB source root")

if(NOT EXISTS "${MINI_TREE_FLASHDB_DIR}/inc/flashdb.h")
    message(STATUS "mini_tree FlashDB: not found — skip")
    return()
endif()

set(_fdb "${MINI_TREE_FLASHDB_DIR}")
set(_mini_tree_flashdb_SRCS
    ${_fdb}/src/fdb.c
    ${_fdb}/src/fdb_file.c
    ${_fdb}/src/fdb_kvdb.c
    ${_fdb}/src/fdb_tsdb.c
    ${_fdb}/src/fdb_utils.c
    ${_fdb}/port/fal/src/fal.c
    ${_fdb}/port/fal/src/fal_flash.c
    ${_fdb}/port/fal/src/fal_partition.c
)

add_library(mini_tree_flashdb INTERFACE)
add_library(flashdb::flashdb ALIAS mini_tree_flashdb)
target_sources(mini_tree_flashdb INTERFACE ${_mini_tree_flashdb_SRCS})
target_include_directories(mini_tree_flashdb INTERFACE
    "${_fdb}/inc"
    "${_fdb}/port/fal/inc"
)
message(STATUS "mini_tree FlashDB: ${MINI_TREE_FLASHDB_VERSION} @ ${_fdb}")

# mini_tree_link_flashdb(<target> <port_dir>)  — port_dir: fdb_cfg.h / fal_cfg.h / flash ports
function(mini_tree_link_flashdb target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_flashdb(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/fdb_cfg.h")
        message(FATAL_ERROR "mini_tree_link_flashdb: fdb_cfg.h not found in ${_port}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_flashdb)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
