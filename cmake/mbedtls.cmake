# Mbed TLS — lib/mbedtls (mbedtls-4.2.0). Built on demand; needs mbedtls_config.h in port.
if(DEFINED MINI_TREE_MBEDTLS_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_MBEDTLS_CMAKE_LOADED ON)

set(MINI_TREE_MBEDTLS_VERSION "4.2.0" CACHE STRING "Mbed TLS release version")
set(MINI_TREE_MBEDTLS_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/mbedtls" CACHE PATH "Mbed TLS root")

if(NOT EXISTS "${MINI_TREE_MBEDTLS_DIR}/include/mbedtls")
    message(STATUS "mini_tree mbedtls: not found — skip")
    return()
endif()

message(STATUS "mini_tree mbedtls: ${MINI_TREE_MBEDTLS_VERSION} (link via mini_tree_link_mbedtls)")

# mini_tree_link_mbedtls(<target> <port_dir>)
function(mini_tree_link_mbedtls target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_mbedtls(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/mbedtls_config.h")
        message(FATAL_ERROR "mini_tree_link_mbedtls: mbedtls_config.h not found in ${_port}")
    endif()

    if(NOT TARGET mbedtls)
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
        # Prefer user config from port
        set(MBEDTLS_CONFIG_FILE "${_port}/mbedtls_config.h" CACHE FILEPATH "" FORCE)
        add_subdirectory("${MINI_TREE_MBEDTLS_DIR}" "${CMAKE_BINARY_DIR}/mini_tree_mbedtls" EXCLUDE_FROM_ALL)
    endif()

    target_link_libraries(${target} PUBLIC mbedtls mbedx509)
    if(TARGET tfpsacrypto)
        target_link_libraries(${target} PUBLIC tfpsacrypto)
    elseif(TARGET mbedcrypto)
        target_link_libraries(${target} PUBLIC mbedcrypto)
    endif()
    target_include_directories(${target} PUBLIC "${_port}")
endfunction()
