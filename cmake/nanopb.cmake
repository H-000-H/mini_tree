# nanopb — Protobuf for embedded, lib/nanopb (0.4.9.1).
if(TARGET mini_tree_nanopb)
    return()
endif()

set(MINI_TREE_NANOPB_VERSION "0.4.9.1" CACHE STRING "nanopb release version")
set(MINI_TREE_NANOPB_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/nanopb" CACHE PATH "nanopb source root")

if(NOT EXISTS "${MINI_TREE_NANOPB_DIR}/pb.h")
    message(STATUS "mini_tree nanopb: not found — skip")
    return()
endif()

add_library(mini_tree_nanopb INTERFACE)
add_library(nanopb::nanopb ALIAS mini_tree_nanopb)
target_sources(mini_tree_nanopb INTERFACE
    "${MINI_TREE_NANOPB_DIR}/pb_common.c"
    "${MINI_TREE_NANOPB_DIR}/pb_decode.c"
    "${MINI_TREE_NANOPB_DIR}/pb_encode.c"
)
target_include_directories(mini_tree_nanopb INTERFACE "${MINI_TREE_NANOPB_DIR}")
message(STATUS "mini_tree nanopb: ${MINI_TREE_NANOPB_VERSION}")

function(mini_tree_link_nanopb target)
    target_link_libraries(${target} PUBLIC mini_tree_nanopb)
endfunction()
