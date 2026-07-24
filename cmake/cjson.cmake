# cJSON — 基础设施，vendored under lib/cJSON (v1.7.19). Not linked by default.
if(TARGET mini_tree_cjson)
    return()
endif()

set(MINI_TREE_CJSON_VERSION "1.7.19" CACHE STRING "cJSON release version")
set(MINI_TREE_CJSON_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/cJSON" CACHE PATH "cJSON source root")

if(NOT EXISTS "${MINI_TREE_CJSON_DIR}/cJSON.h")
    message(STATUS "mini_tree cJSON: not found under ${MINI_TREE_CJSON_DIR} — skip")
    return()
endif()

add_library(mini_tree_cjson INTERFACE)
add_library(cjson::cjson ALIAS mini_tree_cjson)
target_sources(mini_tree_cjson INTERFACE
    "${MINI_TREE_CJSON_DIR}/cJSON.c"
)
target_include_directories(mini_tree_cjson INTERFACE "${MINI_TREE_CJSON_DIR}")
message(STATUS "mini_tree cJSON: ${MINI_TREE_CJSON_VERSION} @ ${MINI_TREE_CJSON_DIR}")

function(mini_tree_link_cjson target)
    if(NOT TARGET mini_tree_cjson)
        message(FATAL_ERROR "mini_tree_link_cjson: mini_tree_cjson missing")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_cjson)
endfunction()

# Optional cJSON_Utils
function(mini_tree_link_cjson_utils target)
    mini_tree_link_cjson(${target})
    target_sources(${target} PUBLIC "${MINI_TREE_CJSON_DIR}/cJSON_Utils.c")
endfunction()
