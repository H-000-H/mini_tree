# nanopb — local lib/nanopb or FetchContent (0.4.9.1). Link on demand.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_NANOPB_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_NANOPB_CMAKE_LOADED ON)

set(MINI_TREE_NANOPB_VERSION "0.4.9.1" CACHE STRING "nanopb git tag")
message(STATUS "mini_tree nanopb: ${MINI_TREE_NANOPB_VERSION} (local-or-fetch on link)")

function(mini_tree_link_nanopb target)
    if(NOT TARGET mini_tree_nanopb)
        mini_tree_dep_get(_np_dir
            NAME nanopb
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/nanopb"
            MARKER "pb.h"
            GIT_REPOSITORY https://github.com/nanopb/nanopb.git
            GIT_TAG ${MINI_TREE_NANOPB_VERSION}
        )
        add_library(mini_tree_nanopb INTERFACE)
        add_library(nanopb::nanopb ALIAS mini_tree_nanopb)
        target_sources(mini_tree_nanopb INTERFACE
            "${_np_dir}/pb_common.c"
            "${_np_dir}/pb_decode.c"
            "${_np_dir}/pb_encode.c"
        )
        target_include_directories(mini_tree_nanopb INTERFACE "${_np_dir}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_nanopb)
endfunction()
