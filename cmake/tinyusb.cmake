include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_TINYUSB_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_TINYUSB_CMAKE_LOADED ON)

set(MINI_TREE_TINYUSB_VERSION "0.21.0" CACHE STRING "TinyUSB git tag")
message(STATUS "mini_tree TinyUSB: ${MINI_TREE_TINYUSB_VERSION} (local-or-fetch on link)")

# include 时求值: 函数内 CMAKE_CURRENT_LIST_DIR 解析为调用点目录（依赖调用位置，
# 与 lwip.cmake 的 MINI_TREE_LWIP_DOTCONFIG 同模式），须在函数外固化本文件路径。
set(MINI_TREE_TINYUSB_LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/tinyusb")

function(mini_tree_link_tinyusb target)
    if(NOT TARGET tinyusb)
        mini_tree_dep_get(_tinyusb_source_dir
            NAME tinyusb
            LOCAL_DIR "${MINI_TREE_TINYUSB_LOCAL_DIR}"
            MARKER "src/tusb.c"
            GIT_REPOSITORY https://github.com/hathach/tinyusb
            GIT_TAG ${MINI_TREE_TINYUSB_VERSION}
        )

        set(TINYUSB_SRC_DIR "${_tinyusb_source_dir}/src")
        # 不 include TinyUSB 的 src/CMakeLists.txt —— 其首行 cmake_minimum_required()
        # 在父工程 project() 之后执行会触发 CMP0000 硬错误 (根 CMakeLists 无 cmake_minimum_required,
        # 设计为被父工程 include)。此处内联其 tinyusb_sources_get 的源列表 
        if(EXISTS "${TINYUSB_SRC_DIR}/tusb.c")
            set(TINYUSB_CORE_SRCS
                "${TINYUSB_SRC_DIR}/tusb.c"
                "${TINYUSB_SRC_DIR}/common/tusb_fifo.c"
                "${TINYUSB_SRC_DIR}/device/usbd.c"
                "${TINYUSB_SRC_DIR}/class/audio/audio_device.c"
                "${TINYUSB_SRC_DIR}/class/cdc/cdc_device.c"
                "${TINYUSB_SRC_DIR}/class/dfu/dfu_device.c"
                "${TINYUSB_SRC_DIR}/class/dfu/dfu_rt_device.c"
                "${TINYUSB_SRC_DIR}/class/hid/hid_device.c"
                "${TINYUSB_SRC_DIR}/class/midi/midi_device.c"
                "${TINYUSB_SRC_DIR}/class/midi/midi2_device.c"
                "${TINYUSB_SRC_DIR}/class/msc/msc_device.c"
                "${TINYUSB_SRC_DIR}/class/mtp/mtp_device.c"
                "${TINYUSB_SRC_DIR}/class/net/ecm_rndis_device.c"
                "${TINYUSB_SRC_DIR}/class/net/ncm_device.c"
                "${TINYUSB_SRC_DIR}/class/printer/printer_device.c"
                "${TINYUSB_SRC_DIR}/class/usbtmc/usbtmc_device.c"
                "${TINYUSB_SRC_DIR}/class/vendor/vendor_device.c"
                "${TINYUSB_SRC_DIR}/class/video/video_device.c"
                "${TINYUSB_SRC_DIR}/host/usbh.c"
                "${TINYUSB_SRC_DIR}/host/hub.c"
                "${TINYUSB_SRC_DIR}/class/cdc/cdc_host.c"
                "${TINYUSB_SRC_DIR}/class/hid/hid_host.c"
                "${TINYUSB_SRC_DIR}/class/midi/midi_host.c"
                "${TINYUSB_SRC_DIR}/class/midi/midi2_host.c"
                "${TINYUSB_SRC_DIR}/class/msc/msc_host.c"
                "${TINYUSB_SRC_DIR}/typec/usbc.c"
            )
        else()
            # 离线/未提供场景: TinyUSB 未本地拉取时置空核心源（mini_tree 静态库默认不链接 tinyusb）
            message(STATUS "mini_tree TinyUSB: src/tusb.c missing — core sources empty (offline)")
            set(TINYUSB_CORE_SRCS "")
        endif()
        add_library(tinyusb INTERFACE)
        target_sources(tinyusb INTERFACE ${TINYUSB_CORE_SRCS})
        target_include_directories(tinyusb INTERFACE
            "${TINYUSB_SRC_DIR}"
            "${_tinyusb_source_dir}/lib/networking"
        )
        message(STATUS "mini_tree TinyUSB: ${MINI_TREE_TINYUSB_VERSION} @ ${_tinyusb_source_dir}")
    endif()
    target_link_libraries(${target} PUBLIC tinyusb)
endfunction()
