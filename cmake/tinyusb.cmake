# TinyUSB — 通用 USB 核心（不绑 MCU）。按需加载：本地 lib/tinyusb 优先，
# 缺失时首次 mini_tree_link_tinyusb() 才 FetchContent 拉取。不链接则不拉取。
# 板级自行链接 DCD / CFG_TUSB_MCU，中间件不绑 MCU。
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_TINYUSB_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_TINYUSB_CMAKE_LOADED ON)

set(MINI_TREE_TINYUSB_VERSION "0.21.0" CACHE STRING "TinyUSB git tag")
message(STATUS "mini_tree TinyUSB: ${MINI_TREE_TINYUSB_VERSION} (local-or-fetch on link)")

function(mini_tree_link_tinyusb target)
    if(NOT TARGET tinyusb)
        mini_tree_dep_get(_tinyusb_source_dir
            NAME tinyusb
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/tinyusb"
            MARKER "src/tusb.c"
            GIT_REPOSITORY https://github.com/hathach/tinyusb
            GIT_TAG ${MINI_TREE_TINYUSB_VERSION}
        )

        set(TINYUSB_SRC_DIR "${_tinyusb_source_dir}/src")
        if(EXISTS "${TINYUSB_SRC_DIR}/CMakeLists.txt")
            include("${TINYUSB_SRC_DIR}/CMakeLists.txt")
            tinyusb_sources_get(TINYUSB_CORE_SRCS)
        else()
            # 离线/未提供场景: TinyUSB 未本地拉取时置空核心源（mini_tree 静态库默认不链接 tinyusb）
            message(STATUS "mini_tree TinyUSB: src/CMakeLists.txt missing — core sources empty (offline)")
            set(TINYUSB_CORE_SRCS "")
        endif()
        add_library(tinyusb INTERFACE)
        target_sources(tinyusb INTERFACE ${TINYUSB_CORE_SRCS})
        target_include_directories(tinyusb INTERFACE "${TINYUSB_SRC_DIR}")
        message(STATUS "mini_tree TinyUSB: ${MINI_TREE_TINYUSB_VERSION} @ ${_tinyusb_source_dir}")
    endif()
    target_link_libraries(${target} PUBLIC tinyusb)
endfunction()
