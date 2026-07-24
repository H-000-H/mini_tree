# mini_tree_dep_get — local lib/<name> first, else FetchContent download.
# Optional bricks use this; infrastructure (OS/USB/lwIP/cJSON/ETL) stays vendored.
include_guard(GLOBAL)
include(FetchContent)

# mini_tree_dep_get(<out_source_dir>
#   NAME <id>
#   LOCAL_DIR <path>
#   MARKER <relative-path-under-LOCAL_DIR-or-fetched-root>
#   GIT_REPOSITORY <url>
#   GIT_TAG <tag-or-commit>
#   [GIT_SUBMODULES_RECURSE]
# )
function(mini_tree_dep_get out_source_dir)
    set(options GIT_SUBMODULES_RECURSE)
    set(oneValueArgs NAME LOCAL_DIR MARKER GIT_REPOSITORY GIT_TAG)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT ARG_NAME OR NOT ARG_LOCAL_DIR OR NOT ARG_MARKER OR NOT ARG_GIT_REPOSITORY OR NOT ARG_GIT_TAG)
        message(FATAL_ERROR "mini_tree_dep_get: NAME LOCAL_DIR MARKER GIT_REPOSITORY GIT_TAG required")
    endif()

    if(EXISTS "${ARG_LOCAL_DIR}/${ARG_MARKER}")
        message(STATUS "mini_tree ${ARG_NAME}: local ${ARG_LOCAL_DIR}")
        set(${out_source_dir} "${ARG_LOCAL_DIR}" PARENT_SCOPE)
        return()
    endif()

    set(_fc_name "mini_tree_fc_${ARG_NAME}")
    string(TOLOWER "${_fc_name}" _fc_name_lower)

    set(_declare_args
        GIT_REPOSITORY "${ARG_GIT_REPOSITORY}"
        GIT_TAG        "${ARG_GIT_TAG}"
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    if(ARG_GIT_SUBMODULES_RECURSE)
        list(APPEND _declare_args GIT_SUBMODULES_RECURSE TRUE)
    endif()

    FetchContent_Declare(${_fc_name} ${_declare_args})
    FetchContent_GetProperties(${_fc_name})
    if(NOT ${_fc_name_lower}_POPULATED)
        message(STATUS "mini_tree ${ARG_NAME}: FetchContent ${ARG_GIT_REPOSITORY} @ ${ARG_GIT_TAG}")
        FetchContent_Populate(${_fc_name})
    endif()

    set(_src "${${_fc_name_lower}_SOURCE_DIR}")
    if(NOT EXISTS "${_src}/${ARG_MARKER}")
        message(FATAL_ERROR "mini_tree ${ARG_NAME}: marker '${ARG_MARKER}' missing after fetch at ${_src}")
    endif()
    set(${out_source_dir} "${_src}" PARENT_SCOPE)
endfunction()
