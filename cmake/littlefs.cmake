# littlefs — vendored under lib/littlefs (v2.11.3). Not linked by default.
if(TARGET mini_tree_littlefs)
    return()
endif()

set(MINI_TREE_LITTLEFS_VERSION "2.11.3" CACHE STRING "littlefs release version")
set(MINI_TREE_LITTLEFS_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/littlefs" CACHE PATH "littlefs source root")

if(NOT EXISTS "${MINI_TREE_LITTLEFS_DIR}/lfs.h")
    message(STATUS "mini_tree littlefs: not found under ${MINI_TREE_LITTLEFS_DIR} — skip")
    return()
endif()

add_library(mini_tree_littlefs INTERFACE)
add_library(littlefs::littlefs ALIAS mini_tree_littlefs)
target_sources(mini_tree_littlefs INTERFACE
    "${MINI_TREE_LITTLEFS_DIR}/lfs.c"
    "${MINI_TREE_LITTLEFS_DIR}/lfs_util.c"
)
target_include_directories(mini_tree_littlefs INTERFACE "${MINI_TREE_LITTLEFS_DIR}")
message(STATUS "mini_tree littlefs: ${MINI_TREE_LITTLEFS_VERSION} @ ${MINI_TREE_LITTLEFS_DIR}")

function(mini_tree_link_littlefs target)
    if(NOT TARGET mini_tree_littlefs)
        message(FATAL_ERROR "mini_tree_link_littlefs: mini_tree_littlefs missing")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_littlefs)
endfunction()
