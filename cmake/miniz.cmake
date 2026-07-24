# miniz — local lib/miniz or FetchContent (3.1.2). Link on demand.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_MINIZ_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_MINIZ_CMAKE_LOADED ON)

set(MINI_TREE_MINIZ_VERSION "3.1.2" CACHE STRING "miniz git tag")
message(STATUS "mini_tree miniz: ${MINI_TREE_MINIZ_VERSION} (local-or-fetch on link)")

function(mini_tree_link_miniz target)
    if(NOT TARGET mini_tree_miniz)
        mini_tree_dep_get(_mz_dir
            NAME miniz
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/miniz"
            MARKER "miniz.h"
            GIT_REPOSITORY https://github.com/richgel999/miniz.git
            GIT_TAG ${MINI_TREE_MINIZ_VERSION}
        )
        add_library(mini_tree_miniz INTERFACE)
        add_library(miniz::miniz ALIAS mini_tree_miniz)
        target_sources(mini_tree_miniz INTERFACE
            "${_mz_dir}/miniz.c"
            "${_mz_dir}/miniz_tdef.c"
            "${_mz_dir}/miniz_tinfl.c"
            "${_mz_dir}/miniz_zip.c"
        )
        target_include_directories(mini_tree_miniz INTERFACE "${_mz_dir}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_miniz)
endfunction()
