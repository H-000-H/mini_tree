# FatFs — local lib/FatFs or FetchContent (abbrev/fatfs R0.16). Link on demand.
# Board must provide ffconf.h (+ optional diskio.c).
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_FATFS_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_FATFS_CMAKE_LOADED ON)

set(MINI_TREE_FATFS_VERSION "R0.16" CACHE STRING "FatFs git tag")
message(STATUS "mini_tree FatFs: ${MINI_TREE_FATFS_VERSION} (local-or-fetch on link)")

function(mini_tree_link_fatfs target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_fatfs(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/ffconf.h")
        message(FATAL_ERROR "mini_tree_link_fatfs: ffconf.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_fatfs)
        mini_tree_dep_get(_ff_root
            NAME fatfs
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/FatFs"
            MARKER "source/ff.h"
            GIT_REPOSITORY https://github.com/abbrev/fatfs.git
            GIT_TAG ${MINI_TREE_FATFS_VERSION}
        )
        set(_ff_src "${_ff_root}/source")
        add_library(mini_tree_fatfs INTERFACE)
        add_library(fatfs::fatfs ALIAS mini_tree_fatfs)
        target_sources(mini_tree_fatfs INTERFACE
            "${_ff_src}/ff.c"
            "${_ff_src}/ffsystem.c"
            "${_ff_src}/ffunicode.c"
        )
        target_include_directories(mini_tree_fatfs INTERFACE "${_ff_src}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_fatfs)
    target_include_directories(${target} PUBLIC "${_port}")
    if(EXISTS "${_port}/diskio.c")
        target_sources(${target} PUBLIC "${_port}/diskio.c")
    endif()
endfunction()
