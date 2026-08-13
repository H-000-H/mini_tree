# CMSIS-DSP — local lib/CMSIS-DSP or FetchContent (v1.17.1). On link.
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_CMSIS_DSP_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_CMSIS_DSP_CMAKE_LOADED ON)

set(MINI_TREE_CMSIS_DSP_VERSION "v1.17.1" CACHE STRING "CMSIS-DSP git tag")
message(STATUS "mini_tree CMSIS-DSP: ${MINI_TREE_CMSIS_DSP_VERSION} (local-or-fetch on link)")

function(mini_tree_link_cmsis_dsp target)
    if(NOT TARGET CMSISDSP)
        mini_tree_dep_get(_dsp_dir
            NAME cmsis_dsp
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/CMSIS-DSP"
            MARKER "Include/arm_math.h"
            GIT_REPOSITORY https://github.com/ARM-software/CMSIS-DSP.git
            GIT_TAG ${MINI_TREE_CMSIS_DSP_VERSION}
        )
        if(NOT DEFINED CMSISCORE AND NOT HOST)
            set(HOST ON CACHE BOOL "CMSIS-DSP host build" FORCE)
            message(STATUS "mini_tree CMSIS-DSP: CMSISCORE unset — enabling HOST=ON")
        endif()
        set(CMSISDSP_INSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory("${_dsp_dir}/Source" "${CMAKE_BINARY_DIR}/mini_tree_cmsis_dsp" EXCLUDE_FROM_ALL)
        set(MINI_TREE_CMSIS_DSP_DIR "${_dsp_dir}" CACHE PATH "CMSIS-DSP root" FORCE)
    endif()
    target_link_libraries(${target} PUBLIC CMSISDSP)
    target_include_directories(${target} PUBLIC "${MINI_TREE_CMSIS_DSP_DIR}/Include")
endfunction()
