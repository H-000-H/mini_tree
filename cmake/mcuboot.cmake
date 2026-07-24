# MCUBoot — local lib/mcuboot or FetchContent (v2.4.0). Link on demand.
# Board port: mcuboot_config.h (+ flash map / crypto glue).
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_MCUBOOT_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_MCUBOOT_CMAKE_LOADED ON)

set(MINI_TREE_MCUBOOT_VERSION "v2.4.0" CACHE STRING "MCUBoot git tag")
message(STATUS "mini_tree MCUBoot: ${MINI_TREE_MCUBOOT_VERSION} (local-or-fetch on link)")

function(mini_tree_link_mcuboot target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_mcuboot(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/mcuboot_config.h" AND NOT EXISTS "${_port}/mcuboot_config/mcuboot_config.h")
        message(FATAL_ERROR "mini_tree_link_mcuboot: mcuboot_config.h not found under ${_port}")
    endif()

    if(NOT TARGET mini_tree_mcuboot)
        mini_tree_dep_get(_mc_root
            NAME mcuboot
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/mcuboot"
            MARKER "boot/bootutil/include/bootutil/bootutil.h"
            GIT_REPOSITORY https://github.com/mcu-tools/mcuboot.git
            GIT_TAG ${MINI_TREE_MCUBOOT_VERSION}
        )
        set(_bu "${_mc_root}/boot/bootutil")
        add_library(mini_tree_mcuboot INTERFACE)
        add_library(mcuboot::bootutil ALIAS mini_tree_mcuboot)
        target_sources(mini_tree_mcuboot INTERFACE
            ${_bu}/src/boot_record.c
            ${_bu}/src/bootutil_area.c
            ${_bu}/src/bootutil_find_key.c
            ${_bu}/src/bootutil_img_hash.c
            ${_bu}/src/bootutil_img_security_cnt.c
            ${_bu}/src/bootutil_loader.c
            ${_bu}/src/bootutil_misc.c
            ${_bu}/src/bootutil_public.c
            ${_bu}/src/caps.c
            ${_bu}/src/fault_injection_hardening.c
            ${_bu}/src/image_validate.c
            ${_bu}/src/loader.c
            ${_bu}/src/swap_misc.c
            ${_bu}/src/swap_move.c
            ${_bu}/src/swap_offset.c
            ${_bu}/src/swap_scratch.c
            ${_bu}/src/tlv.c
        )
        target_include_directories(mini_tree_mcuboot INTERFACE
            "${_bu}/include"
            "${_bu}/src"
        )
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_mcuboot)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
