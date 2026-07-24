# EasyFlash — vendored under lib/EasyFlash (upstream master, post-v4.1).
# Not linked by default: board must provide ef_cfg.h + ef_port.c (erase/read/write).
if(TARGET mini_tree_easyflash)
    return()
endif()

set(MINI_TREE_EASYFLASH_VERSION "master" CACHE STRING "EasyFlash version label")
set(MINI_TREE_EASYFLASH_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/EasyFlash" CACHE PATH "EasyFlash source root")
set(_ef_root "${MINI_TREE_EASYFLASH_DIR}/easyflash")

if(NOT EXISTS "${_ef_root}/inc/easyflash.h")
    message(STATUS "mini_tree EasyFlash: not found under ${MINI_TREE_EASYFLASH_DIR} — skip")
    return()
endif()

# Core sources (exclude sample port; board supplies ef_port.c)
set(_mini_tree_easyflash_SRCS
    ${_ef_root}/src/easyflash.c
    ${_ef_root}/src/ef_env.c
    ${_ef_root}/src/ef_env_legacy.c
    ${_ef_root}/src/ef_env_legacy_wl.c
    ${_ef_root}/src/ef_iap.c
    ${_ef_root}/src/ef_log.c
    ${_ef_root}/src/ef_utils.c
)

add_library(mini_tree_easyflash INTERFACE)
add_library(easyflash::easyflash ALIAS mini_tree_easyflash)
target_sources(mini_tree_easyflash INTERFACE ${_mini_tree_easyflash_SRCS})
target_include_directories(mini_tree_easyflash INTERFACE "${_ef_root}/inc")
message(STATUS "mini_tree EasyFlash: ${MINI_TREE_EASYFLASH_VERSION} @ ${MINI_TREE_EASYFLASH_DIR}")

# mini_tree_link_easyflash(<target> <port_dir>)
# port_dir must contain ef_cfg.h and typically ef_port.c
function(mini_tree_link_easyflash target)
    if(NOT TARGET mini_tree_easyflash)
        message(FATAL_ERROR "mini_tree_link_easyflash: mini_tree_easyflash missing")
    endif()
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_easyflash(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/ef_cfg.h")
        message(FATAL_ERROR "mini_tree_link_easyflash: ef_cfg.h not found in ${_port}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_easyflash)
    target_include_directories(${target} PUBLIC "${_port}")
    if(EXISTS "${_port}/ef_port.c")
        target_sources(${target} PUBLIC "${_port}/ef_port.c")
    endif()
endfunction()
