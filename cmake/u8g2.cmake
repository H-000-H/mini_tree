# u8g2 — local lib/u8g2 or FetchContent (2.37.1). Built on link.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_U8G2_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_U8G2_CMAKE_LOADED ON)

set(MINI_TREE_U8G2_VERSION "2.37.1" CACHE STRING "u8g2 git tag")
message(STATUS "mini_tree u8g2: ${MINI_TREE_U8G2_VERSION} (local-or-fetch on link)")

function(mini_tree_link_u8g2 target)
    if(NOT TARGET mini_tree_u8g2)
        mini_tree_dep_get(_u8g2_dir
            NAME u8g2
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/u8g2"
            MARKER "csrc/u8g2.h"
            GIT_REPOSITORY https://github.com/olikraus/u8g2
            GIT_TAG ${MINI_TREE_U8G2_VERSION}
        )
        file(GLOB _mini_tree_u8g2_SRCS "${_u8g2_dir}/csrc/*.c")
        add_library(mini_tree_u8g2 STATIC EXCLUDE_FROM_ALL ${_mini_tree_u8g2_SRCS})
        add_library(u8g2::u8g2 ALIAS mini_tree_u8g2)
        target_include_directories(mini_tree_u8g2 PUBLIC "${_u8g2_dir}/csrc")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_u8g2)
endfunction()
