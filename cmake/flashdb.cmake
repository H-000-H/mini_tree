# FlashDB — local lib/FlashDB or FetchContent (2.2.0). On link.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_FLASHDB_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_FLASHDB_CMAKE_LOADED ON)

set(MINI_TREE_FLASHDB_VERSION "2.2.0" CACHE STRING "FlashDB git tag")
message(STATUS "mini_tree FlashDB: ${MINI_TREE_FLASHDB_VERSION} (local-or-fetch on link)")

function(mini_tree_link_flashdb target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_flashdb(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/fdb_cfg.h")
        message(FATAL_ERROR "mini_tree_link_flashdb: fdb_cfg.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_flashdb)
        mini_tree_dep_get(_fdb
            NAME flashdb
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/FlashDB"
            MARKER "inc/flashdb.h"
            GIT_REPOSITORY https://github.com/armink/FlashDB.git
            GIT_TAG ${MINI_TREE_FLASHDB_VERSION}
        )
        add_library(mini_tree_flashdb INTERFACE)
        add_library(flashdb::flashdb ALIAS mini_tree_flashdb)
        target_sources(mini_tree_flashdb INTERFACE
            ${_fdb}/src/fdb.c
            ${_fdb}/src/fdb_file.c
            ${_fdb}/src/fdb_kvdb.c
            ${_fdb}/src/fdb_tsdb.c
            ${_fdb}/src/fdb_utils.c
            ${_fdb}/port/fal/src/fal.c
            ${_fdb}/port/fal/src/fal_flash.c
            ${_fdb}/port/fal/src/fal_partition.c
        )
        target_include_directories(mini_tree_flashdb INTERFACE
            "${_fdb}/inc"
            "${_fdb}/port/fal/inc"
        )
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_flashdb)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
