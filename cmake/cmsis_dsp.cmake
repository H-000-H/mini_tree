# CMSIS-DSP — lib/CMSIS-DSP (v1.17.1). Built on demand.
# For MCU builds set CMSISCORE to CMSIS Core Include path before linking.
if(DEFINED MINI_TREE_CMSIS_DSP_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_CMSIS_DSP_CMAKE_LOADED ON)

set(MINI_TREE_CMSIS_DSP_VERSION "1.17.1" CACHE STRING "CMSIS-DSP release version")
set(MINI_TREE_CMSIS_DSP_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/CMSIS-DSP" CACHE PATH "CMSIS-DSP root")

if(NOT EXISTS "${MINI_TREE_CMSIS_DSP_DIR}/Include/arm_math.h")
    message(STATUS "mini_tree CMSIS-DSP: not found — skip")
    return()
endif()

message(STATUS "mini_tree CMSIS-DSP: ${MINI_TREE_CMSIS_DSP_VERSION} (link via mini_tree_link_cmsis_dsp)")

function(mini_tree_link_cmsis_dsp target)
    if(NOT TARGET CMSISDSP)
        # HOST=ON allows PC/unit-test builds without CMSIS Core device headers.
        if(NOT DEFINED CMSISCORE AND NOT HOST)
            set(HOST ON CACHE BOOL "CMSIS-DSP host build" FORCE)
            message(STATUS "mini_tree CMSIS-DSP: CMSISCORE unset — enabling HOST=ON")
        endif()
        set(CMSISDSP_INSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory("${MINI_TREE_CMSIS_DSP_DIR}/Source"
            "${CMAKE_BINARY_DIR}/mini_tree_cmsis_dsp" EXCLUDE_FROM_ALL)
    endif()
    target_link_libraries(${target} PUBLIC CMSISDSP)
    target_include_directories(${target} PUBLIC "${MINI_TREE_CMSIS_DSP_DIR}/Include")
endfunction()
