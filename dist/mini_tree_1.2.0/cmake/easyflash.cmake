# EasyFlash — local lib/EasyFlash or FetchContent (master tip tag fallback: use commit-ish).
# Upstream often has no usable release tag matching tree; default GIT_TAG master with shallow.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_EASYFLASH_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_EASYFLASH_CMAKE_LOADED ON)

set(MINI_TREE_EASYFLASH_VERSION "master" CACHE STRING "EasyFlash git tag/branch")
message(STATUS "mini_tree EasyFlash: ${MINI_TREE_EASYFLASH_VERSION} (local-or-fetch on link)")

function(mini_tree_link_easyflash target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_easyflash(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/ef_cfg.h")
        message(FATAL_ERROR "mini_tree_link_easyflash: ef_cfg.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_easyflash)
        mini_tree_dep_get(_ef_root
            NAME easyflash
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/EasyFlash"
            MARKER "easyflash/inc/easyflash.h"
            GIT_REPOSITORY https://github.com/armink/EasyFlash.git
            GIT_TAG ${MINI_TREE_EASYFLASH_VERSION}
        )
        set(_ef "${_ef_root}/easyflash")
        add_library(mini_tree_easyflash INTERFACE)
        add_library(easyflash::easyflash ALIAS mini_tree_easyflash)
        target_sources(mini_tree_easyflash INTERFACE
            ${_ef}/src/easyflash.c
            ${_ef}/src/ef_env.c
            ${_ef}/src/ef_env_legacy.c
            ${_ef}/src/ef_env_legacy_wl.c
            ${_ef}/src/ef_iap.c
            ${_ef}/src/ef_log.c
            ${_ef}/src/ef_utils.c
        )
        target_include_directories(mini_tree_easyflash INTERFACE "${_ef}/inc")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_easyflash)
    target_include_directories(${target} PUBLIC "${_port}")
    if(EXISTS "${_port}/ef_port.c")
        target_sources(${target} PUBLIC "${_port}/ef_port.c")
    endif()
endfunction()
