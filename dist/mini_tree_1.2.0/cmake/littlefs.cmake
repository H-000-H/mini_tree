# littlefs — local lib/littlefs or FetchContent (v2.11.3). Link on demand.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_LITTLEFS_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_LITTLEFS_CMAKE_LOADED ON)

set(MINI_TREE_LITTLEFS_VERSION "v2.11.3" CACHE STRING "littlefs git tag")
message(STATUS "mini_tree littlefs: ${MINI_TREE_LITTLEFS_VERSION} (local-or-fetch on link)")

function(mini_tree_link_littlefs target)
    if(NOT TARGET mini_tree_littlefs)
        mini_tree_dep_get(_lfs_dir
            NAME littlefs
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/littlefs"
            MARKER "lfs.h"
            GIT_REPOSITORY https://github.com/littlefs-project/littlefs.git
            GIT_TAG ${MINI_TREE_LITTLEFS_VERSION}
        )
        add_library(mini_tree_littlefs INTERFACE)
        add_library(littlefs::littlefs ALIAS mini_tree_littlefs)
        target_sources(mini_tree_littlefs INTERFACE
            "${_lfs_dir}/lfs.c"
            "${_lfs_dir}/lfs_util.c"
        )
        target_include_directories(mini_tree_littlefs INTERFACE "${_lfs_dir}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_littlefs)
endfunction()
