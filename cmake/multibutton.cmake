# MultiButton — vendored under lib/MultiButton (v1.1.1). Not linked by default.
if(TARGET mini_tree_multibutton)
    return()
endif()

set(MINI_TREE_MULTIBUTTON_VERSION "1.1.1" CACHE STRING "MultiButton release version")
set(MINI_TREE_MULTIBUTTON_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/MultiButton" CACHE PATH "MultiButton source root")

if(NOT EXISTS "${MINI_TREE_MULTIBUTTON_DIR}/multi_button.h")
    message(STATUS "mini_tree MultiButton: not found under ${MINI_TREE_MULTIBUTTON_DIR} — skip")
    return()
endif()

add_library(mini_tree_multibutton INTERFACE)
add_library(multibutton::multibutton ALIAS mini_tree_multibutton)
target_sources(mini_tree_multibutton INTERFACE
    "${MINI_TREE_MULTIBUTTON_DIR}/multi_button.c"
)
target_include_directories(mini_tree_multibutton INTERFACE "${MINI_TREE_MULTIBUTTON_DIR}")
message(STATUS "mini_tree MultiButton: ${MINI_TREE_MULTIBUTTON_VERSION} @ ${MINI_TREE_MULTIBUTTON_DIR}")

function(mini_tree_link_multibutton target)
    if(NOT TARGET mini_tree_multibutton)
        message(FATAL_ERROR "mini_tree_link_multibutton: mini_tree_multibutton missing")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_multibutton)
endfunction()
