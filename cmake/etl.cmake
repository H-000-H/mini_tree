# ETL (Embedded Template Library) — header-only, no heap STL in upper layers.
if(TARGET etl::etl)
    return()
endif()

set(MINI_TREE_ETL_VERSION "20.41.1" CACHE STRING "ETL release tag")

set(_etl_local_candidates
    "${CMAKE_CURRENT_LIST_DIR}/../../../ESP32-S3/managed_components/marcel-cd__etlcpp/etl"
    "${CMAKE_CURRENT_LIST_DIR}/../../third_party/etl"
)
set(_etl_source_dir "")
foreach(_candidate IN LISTS _etl_local_candidates)
    if(EXISTS "${_candidate}/include/etl/vector.h")
        set(_etl_source_dir "${_candidate}")
        break()
    endif()
endforeach()

if(_etl_source_dir)
    message(STATUS "mini_tree ETL: local ${_etl_source_dir}")
    add_subdirectory("${_etl_source_dir}" "${CMAKE_BINARY_DIR}/mini_tree_etl" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        mini_tree_etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG        ${MINI_TREE_ETL_VERSION}
    )
    FetchContent_MakeAvailable(mini_tree_etl)
endif()

function(mini_tree_link_etl target)
    if(NOT TARGET etl::etl)
        message(FATAL_ERROR "mini_tree_link_etl: etl::etl target missing")
    endif()
    target_link_libraries(${target} PUBLIC etl::etl)
    target_compile_definitions(${target} PUBLIC ETL_NO_STL)
endfunction()
