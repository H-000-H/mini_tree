# LVGL — vendored under lib/lvgl (v9.5.0).
# Not built until mini_tree_link_lvgl(); board must provide lv_conf.h.
if(DEFINED MINI_TREE_LVGL_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_LVGL_CMAKE_LOADED ON)

set(MINI_TREE_LVGL_VERSION "9.5.0" CACHE STRING "LVGL release version")
set(MINI_TREE_LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/lvgl" CACHE PATH "LVGL source root")

if(NOT EXISTS "${MINI_TREE_LVGL_DIR}/lvgl.h")
    message(STATUS "mini_tree LVGL: not found under ${MINI_TREE_LVGL_DIR} — skip")
    return()
endif()

message(STATUS "mini_tree LVGL: ${MINI_TREE_LVGL_VERSION} @ ${MINI_TREE_LVGL_DIR} (link via mini_tree_link_lvgl)")

# mini_tree_link_lvgl(<target> <port_dir>)
# port_dir must contain lv_conf.h (display/indev flush callbacks stay in app/port).
function(mini_tree_link_lvgl target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_lvgl(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/lv_conf.h")
        message(FATAL_ERROR "mini_tree_link_lvgl: lv_conf.h not found in ${_port}")
    endif()

    if(NOT TARGET lvgl)
        set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
        set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)
        set(LV_BUILD_CONF_DIR "${_port}" CACHE PATH "Directory containing lv_conf.h" FORCE)
        add_subdirectory("${MINI_TREE_LVGL_DIR}" "${CMAKE_BINARY_DIR}/mini_tree_lvgl" EXCLUDE_FROM_ALL)
    endif()

    target_link_libraries(${target} PUBLIC lvgl)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
