include("${CMAKE_SOURCE_DIR}/cmake/coremqtt.cmake")

file(GLOB MINI_TREE_NET_PORT_SRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/port/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/pppif/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/tcp/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/transport_glue/*.c"
)

set(MINI_TREE_NET_DOTCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/../.config")
set(_net_mqtt_on "")
if(EXISTS "${MINI_TREE_NET_DOTCONFIG}")
    file(STRINGS "${MINI_TREE_NET_DOTCONFIG}" _net_mqtt_y REGEX "^CONFIG_NET_MQTT_USE_COREMQTT=y$")
    set(_net_mqtt_on "${_net_mqtt_y}")
endif()

add_library(mini_tree_net STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_arch.c"
    ${MINI_TREE_NET_PORT_SRCS}
    port/pppif/pppif.h
    port/transport_glue/transport_glue.h
    port/transport_glue/transport_glue.c
    port/net_error.h
)

if(_net_mqtt_on)
    # mqtt_client.h 顶部无条件 #include "core_mqtt.h" —— 包装层源文件必须随开关
    # 一同编入, 否则关闭开关后仍编译 mqtt_client.c 会因 coreMQTT 头缺失而构建失败
    # (修复: Kconfig "关闭后 MQTT 客户端不参与链接" 由此真正生效)。
    # transport_glue 无 coreMQTT 头依赖, 保持无条件编入 (未引用部分由 gc-sections 回收)。
    target_sources(mini_tree_net PRIVATE
        port/mqtt/mqtt_client.h
        port/mqtt/mqtt_client.c
        port/mqtt/core_mqtt_config.h
    )
endif()

# USB 网卡 (CDC-ECM / RNDIS) 的 lwIP 适配层: 仅 .config 显式 CONFIG_USB_NET=y 时编入。
# usbethif.c 直接读 CONFIG_USB_NET_MTU / CONFIG_USB_NET_MAC* / CONFIG_USB_NET_*_TIMEOUT_MS
# 等编译期宏 (Kconfig 里 depends on USB_NET), 并依赖板级 board_define_usb.h 的
# DTC_GEN_COUNT_HETEROGENEOUS_USB_CDC_ECM。与 USB 侧软编码门控同源, 同样 fail-closed:
# 键缺失 / .config 不存在 / 读取失败一律不编 (缺宏会直接编译失败)。
set(_net_usb_on "")
if(EXISTS "${MINI_TREE_NET_DOTCONFIG}")
    file(STRINGS "${MINI_TREE_NET_DOTCONFIG}" _net_usb_y REGEX "^CONFIG_USB_NET=y$")
    set(_net_usb_on "${_net_usb_y}")
endif()

if(_net_usb_on)
    target_sources(mini_tree_net PRIVATE
        port/usb/usbethif.h
        port/usb/usbethif.c
    )
    # board_define_usb.h (DTC_GEN_COUNT_USB_OTG_HOST / ..._CDC_ECM 等默认值)
    target_include_directories(mini_tree_net PRIVATE
        "${CMAKE_SOURCE_DIR}/board/define/vfs"
    )
endif()

target_include_directories(mini_tree_net PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/port"
    "${CMAKE_SOURCE_DIR}/lib/lwip/src/include"
    "${CMAKE_SOURCE_DIR}/core/include"
    "${CMAKE_SOURCE_DIR}/mini-log/inc"
    "${CMAKE_SOURCE_DIR}/algorithm/buffer"
    "${CMAKE_SOURCE_DIR}/board/include"
    "${CMAKE_SOURCE_DIR}/hal/gpio"
    "${CMAKE_SOURCE_DIR}/system_c/include"
    "${CMAKE_SOURCE_DIR}/time_slice/task"
    "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree"
    "${CMAKE_BINARY_DIR}/generated/board/mini_tree"
)
mini_tree_link_lwip(mini_tree_net "${CMAKE_CURRENT_SOURCE_DIR}")

if(_net_mqtt_on)
    mini_tree_link_coremqtt(mini_tree_net "${CMAKE_CURRENT_SOURCE_DIR}/port/mqtt")
endif()

# 本地扩展 (不入库): 存在 local.cmake 时加载, 承载本地新增模块的源文件与链接
include("${CMAKE_CURRENT_SOURCE_DIR}/local.cmake" OPTIONAL)
