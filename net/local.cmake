# net 本地扩展 (不入库, 见 .gitignore)
# 承载本地新增模块 (tls/https/mqtts/http/broker) 的源文件与链接逻辑,
# 由 net/CMakeLists.txt 末尾的 optional include 加载; 文件不存在时主树构建不受影响。

# mini_tree 根: 与 net/CMakeLists.txt 同算法, 不依赖 CMAKE_SOURCE_DIR (被上层工程
# add_subdirectory() 包含时它指向上层根)。独立加载本文件时也能自洽。
if(NOT DEFINED MINI_TREE_ROOT)
    set(MINI_TREE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
include("${MINI_TREE_ROOT}/cmake/corehttp.cmake")
include("${MINI_TREE_ROOT}/cmake/mbedtls.cmake")

# mbedtls 2.28 的 cmake_minimum_required(2.8) 低于 CMake 3.27+ 兼容下限,
# 注入策略版本下限 (仅影响其后 add_subdirectory 的 mbedtls 子项目)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# .config 桥接: bool 未选中时文件内为 "# CONFIG_XXX is not set", 选中时无该行
set(MINI_TREE_NET_DOTCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/../.config")
if(EXISTS "${MINI_TREE_NET_DOTCONFIG}")
    file(STRINGS "${MINI_TREE_NET_DOTCONFIG}" _net_local_corehttp_off REGEX "^# CONFIG_NET_HTTP_USE_COREHTTP is not set$")
    file(STRINGS "${MINI_TREE_NET_DOTCONFIG}" _net_local_altcp_tls_off REGEX "^# CONFIG_LWIP_ALTCP_TLS is not set$")
    file(STRINGS "${MINI_TREE_NET_DOTCONFIG}" _net_local_altcp_tls_mbedtls_off REGEX "^# CONFIG_LWIP_ALTCP_TLS_MBEDTLS is not set$")
    file(STRINGS "${MINI_TREE_NET_DOTCONFIG}" _net_local_mqtt_on REGEX "^CONFIG_NET_MQTT_USE_COREMQTT=y$")
else()
    set(_net_local_corehttp_off "")
    set(_net_local_altcp_tls_off "")
    set(_net_local_altcp_tls_mbedtls_off "")
    set(_net_local_mqtt_on "")
endif()

# HTTP Client (明文, coreHTTP 薄包装)
target_sources(mini_tree_net PRIVATE
    port/http/http_client.h
    port/http/http_client.c
)

# MQTT Broker (明文服务端, 依赖 coreMQTT; 随 NET_MQTT_USE_COREMQTT 门控, 关闭时不编入)
if(_net_local_mqtt_on)
    target_sources(mini_tree_net PRIVATE
        port/mqtt_broker/mqtt_broker.h
        port/mqtt_broker/mqtt_broker.c
    )
endif()

# 加密通道 (altcp_tls 直连封装): tls 底座 + https/mqtts 包装层 + mbedtls 配置
if(NOT _net_local_altcp_tls_off)
    target_sources(mini_tree_net PRIVATE
        port/tls/tls_client.h
        port/tls/tls_client.c
        port/tls/mbedtls_config.h
        port/https/https_client.h
        port/https/https_client.c
    )
    # MQTTS 依赖 coreMQTT + altcp_tls, 二者都开启时才编入
    if(_net_local_mqtt_on)
        target_sources(mini_tree_net PRIVATE
            port/mqtts/mqtts_client.h
            port/mqtts/mqtts_client.c
        )
    endif()
    if(NOT _net_local_altcp_tls_mbedtls_off)
        mini_tree_link_mbedtls(mini_tree_net "${CMAKE_CURRENT_SOURCE_DIR}/port/tls")

        # 握手缓冲大小: 从 .config 读 CONFIG_TLS_CONTENT_LEN, 注入给 net 与 mbedtls
        # 两侧 (mbedtls_config.h 未定义 MBEDTLS_SSL_MAX_CONTENT_LEN 时取本值)
        set(_tls_content_len 2048)
        if(EXISTS "${MINI_TREE_NET_DOTCONFIG}")
            file(STRINGS "${MINI_TREE_NET_DOTCONFIG}" _tls_content_line
                 REGEX "^CONFIG_TLS_CONTENT_LEN=[0-9]+$")
            if(_tls_content_line)
                string(REGEX REPLACE "^CONFIG_TLS_CONTENT_LEN=([0-9]+)$" "\\1"
                       _tls_content_len "${_tls_content_line}")
            endif()
        endif()
        target_compile_definitions(mini_tree_net PUBLIC
            "MBEDTLS_SSL_MAX_CONTENT_LEN=${_tls_content_len}")
        foreach(_mb_tgt mini_tree_mbedtls_mbedtls mini_tree_mbedtls_mbedx509
                        mini_tree_mbedtls_mbedcrypto)
            if(TARGET ${_mb_tgt})
                target_compile_definitions(${_mb_tgt} PUBLIC
                    "MBEDTLS_SSL_MAX_CONTENT_LEN=${_tls_content_len}")
            endif()
        endforeach()
    endif()
endif()

# coreHTTP: HTTP/HTTPS 共用 (https 传输直连 altcp_tls, 不经 transport_glue)
if(NOT _net_local_corehttp_off)
    mini_tree_link_corehttp(mini_tree_net "${CMAKE_CURRENT_SOURCE_DIR}/port/http")
endif()
