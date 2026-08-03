# cJSON — 基础设施。按需加载：本地 lib/cJSON 优先，缺失时首次 mini_tree_link_cjson() 才 FetchContent 拉取。
# 不链接则不拉取。
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_CJSON_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_CJSON_CMAKE_LOADED ON)

set(MINI_TREE_CJSON_VERSION "v1.7.19" CACHE STRING "cJSON git tag")
message(STATUS "mini_tree cJSON: ${MINI_TREE_CJSON_VERSION} (local-or-fetch on link)")

function(mini_tree_link_cjson target)
    if(NOT TARGET mini_tree_cjson)
        mini_tree_dep_get(_cjson_source_dir
            NAME cjson
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/cJSON"
            MARKER "cJSON.h"
            GIT_REPOSITORY https://github.com/DaveGamble/cJSON
            GIT_TAG ${MINI_TREE_CJSON_VERSION}
        )

        add_library(mini_tree_cjson INTERFACE)
        add_library(cjson::cjson ALIAS mini_tree_cjson)
        target_sources(mini_tree_cjson INTERFACE
            "${_cjson_source_dir}/cJSON.c"
        )
        target_include_directories(mini_tree_cjson INTERFACE "${_cjson_source_dir}")
        message(STATUS "mini_tree cJSON: ${MINI_TREE_CJSON_VERSION} @ ${_cjson_source_dir}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_cjson)
endfunction()

# Optional cJSON_Utils（独立 target，避免重复携带 cJSON.c）
function(mini_tree_link_cjson_utils target)
    mini_tree_link_cjson(${target})
    if(NOT TARGET mini_tree_cjson_utils)
        mini_tree_dep_get(_cjson_utils_dir
            NAME cjson
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/cJSON"
            MARKER "cJSON.h"
            GIT_REPOSITORY https://github.com/DaveGamble/cJSON
            GIT_TAG ${MINI_TREE_CJSON_VERSION}
        )
        add_library(mini_tree_cjson_utils INTERFACE)
        target_sources(mini_tree_cjson_utils INTERFACE "${_cjson_utils_dir}/cJSON_Utils.c")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_cjson_utils)
endfunction()
