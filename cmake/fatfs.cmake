# FatFs — ChaN R0.16 (abbrev/fatfs mirror), vendored under lib/FatFs.
# Not linked by default: board must provide ffconf.h (+ diskio.c/h).
if(TARGET mini_tree_fatfs)
    return()
endif()

set(MINI_TREE_FATFS_VERSION "R0.16" CACHE STRING "FatFs release version")
set(MINI_TREE_FATFS_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/FatFs" CACHE PATH "FatFs source root")
set(_ff_src "${MINI_TREE_FATFS_DIR}/source")

if(NOT EXISTS "${_ff_src}/ff.h")
    message(STATUS "mini_tree FatFs: not found under ${MINI_TREE_FATFS_DIR} — skip")
    return()
endif()

add_library(mini_tree_fatfs INTERFACE)
add_library(fatfs::fatfs ALIAS mini_tree_fatfs)
target_sources(mini_tree_fatfs INTERFACE
    "${_ff_src}/ff.c"
    "${_ff_src}/ffsystem.c"
    "${_ff_src}/ffunicode.c"
)
target_include_directories(mini_tree_fatfs INTERFACE "${_ff_src}")
message(STATUS "mini_tree FatFs: ${MINI_TREE_FATFS_VERSION} @ ${MINI_TREE_FATFS_DIR}")

# mini_tree_link_fatfs(<target> <port_dir>)
# port_dir must contain ffconf.h; if diskio.c exists it is also added.
function(mini_tree_link_fatfs target)
    if(NOT TARGET mini_tree_fatfs)
        message(FATAL_ERROR "mini_tree_link_fatfs: mini_tree_fatfs missing")
    endif()
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_fatfs(<target> <port_dir>)")
    endif()
    set(_port "${ARGV1}")
    if(NOT EXISTS "${_port}/ffconf.h")
        message(FATAL_ERROR "mini_tree_link_fatfs: ffconf.h not found in ${_port}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_fatfs)
    target_include_directories(${target} PUBLIC "${_port}")
    if(EXISTS "${_port}/diskio.c")
        target_sources(${target} PUBLIC "${_port}/diskio.c")
    endif()
endfunction()
