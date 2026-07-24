# u8g2 — monochrome graphics (I2C/SPI OLED etc.), vendored under lib/u8g2 (2.37.1).
# Not linked by default. Board supplies hw I2C/SPI byte callbacks (u8x8_byte_* / gpio).
if(TARGET mini_tree_u8g2)
    return()
endif()

set(MINI_TREE_U8G2_VERSION "2.37.1" CACHE STRING "u8g2 release version")
set(MINI_TREE_U8G2_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/u8g2" CACHE PATH "u8g2 source root")

if(NOT EXISTS "${MINI_TREE_U8G2_DIR}/csrc/u8g2.h")
    message(STATUS "mini_tree u8g2: not found under ${MINI_TREE_U8G2_DIR} — skip")
    return()
endif()

file(GLOB _mini_tree_u8g2_SRCS "${MINI_TREE_U8G2_DIR}/csrc/*.c")

add_library(mini_tree_u8g2 STATIC EXCLUDE_FROM_ALL ${_mini_tree_u8g2_SRCS})
add_library(u8g2::u8g2 ALIAS mini_tree_u8g2)
target_include_directories(mini_tree_u8g2 PUBLIC "${MINI_TREE_U8G2_DIR}/csrc")
message(STATUS "mini_tree u8g2: ${MINI_TREE_U8G2_VERSION} @ ${MINI_TREE_U8G2_DIR}")

function(mini_tree_link_u8g2 target)
    if(NOT TARGET mini_tree_u8g2)
        message(FATAL_ERROR "mini_tree_link_u8g2: mini_tree_u8g2 missing")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_u8g2)
endfunction()
