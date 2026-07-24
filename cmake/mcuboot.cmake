# MCUBoot — vendored under lib/mcuboot (v2.4.0).
# Not linked by default. Full image needs a board port (mcuboot_config.h,
# flash_map_backend, crypto). This helper exposes bootutil for integration.
if(TARGET mini_tree_mcuboot)
    return()
endif()

set(MINI_TREE_MCUBOOT_VERSION "2.4.0" CACHE STRING "MCUBoot release version")
set(MINI_TREE_MCUBOOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/mcuboot" CACHE PATH "MCUBoot source root")
set(_bu "${MINI_TREE_MCUBOOT_DIR}/boot/bootutil")

if(NOT EXISTS "${_bu}/include/bootutil/bootutil.h")
    message(STATUS "mini_tree MCUBoot: not found under ${MINI_TREE_MCUBOOT_DIR} — skip")
    return()
endif()

# Core bootutil sources (crypto backends / encrypted swap are optional extras).
set(_mini_tree_mcuboot_SRCS
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

add_library(mini_tree_mcuboot INTERFACE)
add_library(mcuboot::bootutil ALIAS mini_tree_mcuboot)
target_sources(mini_tree_mcuboot INTERFACE ${_mini_tree_mcuboot_SRCS})
target_include_directories(mini_tree_mcuboot INTERFACE
    "${_bu}/include"
    "${_bu}/src"
)
message(STATUS "mini_tree MCUBoot: ${MINI_TREE_MCUBOOT_VERSION} @ ${MINI_TREE_MCUBOOT_DIR}")

# mini_tree_link_mcuboot(<target> <port_dir>)
# port_dir must provide mcuboot_config.h (and typically flash map / crypto glue).
function(mini_tree_link_mcuboot target)
    if(NOT TARGET mini_tree_mcuboot)
        message(FATAL_ERROR "mini_tree_link_mcuboot: mini_tree_mcuboot missing")
    endif()
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_mcuboot(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/mcuboot_config.h" AND NOT EXISTS "${_port}/mcuboot_config/mcuboot_config.h")
        message(FATAL_ERROR "mini_tree_link_mcuboot: mcuboot_config.h not found under ${_port}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_mcuboot)
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
