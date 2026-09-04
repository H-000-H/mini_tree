# Mbed TLS — local lib/mbedtls or FetchContent (mbedtls-2.28 LTS). On link.
# 版本钉 2.28: lwIP altcp_tls 官方仅兼容 mbedtls 2.x。
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_MBEDTLS_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_MBEDTLS_CMAKE_LOADED ON)

set(MINI_TREE_MBEDTLS_VERSION "mbedtls-2.28.9" CACHE STRING "Mbed TLS git tag")
message(STATUS "mini_tree mbedtls: ${MINI_TREE_MBEDTLS_VERSION} (local-or-fetch on link)")

function(mini_tree_link_mbedtls target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_mbedtls(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/mbedtls_config.h")
        message(FATAL_ERROR "mini_tree_link_mbedtls: mbedtls_config.h not found in ${_port}")
    endif()

    if(NOT TARGET mini_tree_mbedtls_mbedtls)
        mini_tree_dep_get(_mbedtls_dir
            NAME mbedtls
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/mbedtls"
            MARKER "include/mbedtls"
            GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
            GIT_TAG ${MINI_TREE_MBEDTLS_VERSION}
            GIT_SUBMODULES_RECURSE
        )
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
        # 目标前缀: 避免聚合目标 "lib" 与仓库根 lib/ 接口库同名 (CMP0002)
        set(MBEDTLS_TARGET_PREFIX "mini_tree_mbedtls_" CACHE STRING "" FORCE)
        set(MBEDTLS_CONFIG_FILE "${_port}/mbedtls_config.h" CACHE FILEPATH "" FORCE)
        add_subdirectory("${_mbedtls_dir}" "${CMAKE_BINARY_DIR}/mini_tree_mbedtls" EXCLUDE_FROM_ALL)
    endif()

    target_link_libraries(${target} PUBLIC mini_tree_mbedtls_mbedtls mini_tree_mbedtls_mbedx509)
    if(TARGET mini_tree_mbedtls_tfpsacrypto)
        target_link_libraries(${target} PUBLIC mini_tree_mbedtls_tfpsacrypto)
    elseif(TARGET mini_tree_mbedtls_mbedcrypto)
        target_link_libraries(${target} PUBLIC mini_tree_mbedtls_mbedcrypto)
    endif()
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
