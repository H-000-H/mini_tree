# cJSON — 基础设施。本地 lib/cJSON 优先，缺失时 FetchContent 自动拉取。
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(TARGET mini_tree_cjson)
    return()
endif()

set(MINI_TREE_CJSON_VERSION "v1.7.19" CACHE STRING "cJSON git tag")

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

function(mini_tree_link_cjson target)
    if(NOT TARGET mini_tree_cjson)
        message(FATAL_ERROR "mini_tree_link_cjson: mini_tree_cjson missing")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_cjson)
endfunction()

# Optional cJSON_Utils
function(mini_tree_link_cjson_utils target)
    mini_tree_link_cjson(${target})
    target_sources(${target} PUBLIC "${_cjson_source_dir}/cJSON_Utils.c")
endfunction()
