# MultiButton — local lib/MultiButton or FetchContent (v1.1.1). Link on demand.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_MULTIBUTTON_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_MULTIBUTTON_CMAKE_LOADED ON)

set(MINI_TREE_MULTIBUTTON_VERSION "master" CACHE STRING "MultiButton git tag/branch (upstream has few release tags)")
message(STATUS "mini_tree MultiButton: ${MINI_TREE_MULTIBUTTON_VERSION} (local-or-fetch on link)")

function(mini_tree_link_multibutton target)
    if(NOT TARGET mini_tree_multibutton)
        mini_tree_dep_get(_mb_dir
            NAME multibutton
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/MultiButton"
            MARKER "multi_button.h"
            GIT_REPOSITORY https://github.com/0x1abin/MultiButton.git
            GIT_TAG ${MINI_TREE_MULTIBUTTON_VERSION}
        )
        add_library(mini_tree_multibutton INTERFACE)
        add_library(multibutton::multibutton ALIAS mini_tree_multibutton)
        target_sources(mini_tree_multibutton INTERFACE
            "${_mb_dir}/multi_button.c"
        )
        target_include_directories(mini_tree_multibutton INTERFACE "${_mb_dir}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_multibutton)
endfunction()
