# TinyUSB — 通用 USB 核心（不绑 MCU）。本地 lib/tinyusb 优先，缺失时 FetchContent 自动拉取。
# 板级自行链接 DCD / CFG_TUSB_MCU，中间件不绑 MCU。
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(TARGET tinyusb)
    return()
endif()

set(MINI_TREE_TINYUSB_VERSION "0.21.0" CACHE STRING "TinyUSB git tag")

mini_tree_dep_get(_tinyusb_source_dir
    NAME tinyusb
    LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/tinyusb"
    MARKER "src/tusb.c"
    GIT_REPOSITORY https://github.com/hathach/tinyusb
    GIT_TAG ${MINI_TREE_TINYUSB_VERSION}
)

set(TINYUSB_SRC_DIR "${_tinyusb_source_dir}/src")
include("${TINYUSB_SRC_DIR}/CMakeLists.txt")
tinyusb_sources_get(TINYUSB_CORE_SRCS)
add_library(tinyusb INTERFACE)
target_sources(tinyusb INTERFACE ${TINYUSB_CORE_SRCS})
target_include_directories(tinyusb INTERFACE "${TINYUSB_SRC_DIR}")
message(STATUS "mini_tree TinyUSB: ${MINI_TREE_TINYUSB_VERSION} @ ${_tinyusb_source_dir}")
