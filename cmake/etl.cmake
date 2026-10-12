# ETL — 上层 C++ 基础设施（无堆容器/字符串等）。
# 已 vendor 在 lib/etl（仅 include + cmake，无 test/docs）；缺失时 FetchContent 兖底。
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(TARGET etl::etl)
    return()
endif()

set(MINI_TREE_ETL_VERSION "20.48.1" CACHE STRING "ETL release tag")

mini_tree_dep_get(_etl_source_dir
    NAME etl
    LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/etl"
    MARKER "include/etl/vector.h"
    GIT_REPOSITORY https://github.com/ETLCPP/etl
    GIT_TAG ${MINI_TREE_ETL_VERSION}
)
add_subdirectory("${_etl_source_dir}" "${CMAKE_BINARY_DIR}/mini_tree_etl" EXCLUDE_FROM_ALL)

function(mini_tree_link_etl target)
    if(NOT TARGET etl::etl)
        message(FATAL_ERROR "mini_tree_link_etl: etl::etl target missing")
    endif()
    target_link_libraries(${target} PUBLIC etl::etl)
    target_compile_definitions(${target} PUBLIC ETL_NO_STL)
endfunction()
