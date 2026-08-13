# LVGL — local lib/lvgl or FetchContent (v9.5.0). Built on mini_tree_link_lvgl().
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_LVGL_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_LVGL_CMAKE_LOADED ON)

set(MINI_TREE_LVGL_VERSION "v9.5.0" CACHE STRING "LVGL git tag")
message(STATUS "mini_tree LVGL: ${MINI_TREE_LVGL_VERSION} (local-or-fetch on link)")

function(mini_tree_link_lvgl target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_lvgl(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/lv_conf.h")
        message(FATAL_ERROR "mini_tree_link_lvgl: lv_conf.h not found in ${_port}")
    endif()

    if(NOT TARGET lvgl)
        mini_tree_dep_get(_lvgl_dir
            NAME lvgl
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/lvgl"
            MARKER "lvgl.h"
            GIT_REPOSITORY https://github.com/lvgl/lvgl
            GIT_TAG ${MINI_TREE_LVGL_VERSION}
        )
        set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
        set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)
        set(LV_BUILD_CONF_DIR "${_port}" CACHE PATH "Directory containing lv_conf.h" FORCE)
        add_subdirectory("${_lvgl_dir}" "${CMAKE_BINARY_DIR}/mini_tree_lvgl" EXCLUDE_FROM_ALL)
    endif()

    target_link_libraries(${target} PUBLIC lvgl)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
