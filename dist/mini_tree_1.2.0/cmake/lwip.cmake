# lwIP — 基础设施。按需加载：本地 lib/lwip 优先，缺失时首次 mini_tree_link_lwip() 才 FetchContent 拉取。
# 不链接则不拉取。板级需提供 lwipopts.h (+ arch/cc.h, sys_arch…)。
include("${CMAKE_CURRENT_LIST_DIR}/dep_fetch.cmake")

if(DEFINED MINI_TREE_LWIP_CMAKE_LOADED)
    return()
endif()
set(MINI_TREE_LWIP_CMAKE_LOADED ON)

set(MINI_TREE_LWIP_VERSION "STABLE-2_2_1_RELEASE" CACHE STRING "lwIP git tag")
message(STATUS "mini_tree lwIP: ${MINI_TREE_LWIP_VERSION} (local-or-fetch on link)")

# Link lwIP into a target. Pass directory that contains lwipopts.h (and arch/).
# Example: mini_tree_link_lwip(my_fw "${CMAKE_CURRENT_SOURCE_DIR}/port")
function(mini_tree_link_lwip target)
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_lwip(<target> <port_include_dir>)")
    endif()
    set(_port_inc "${ARGV1}")
    if(NOT EXISTS "${_port_inc}/lwipopts.h")
        message(FATAL_ERROR "mini_tree_link_lwip: lwipopts.h not found in ${_port_inc}")
    endif()

    if(NOT TARGET mini_tree_lwip)
        mini_tree_dep_get(_lwip_source_dir
            NAME lwip
            LOCAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/lwip"
            MARKER "src/include/lwip/init.h"
            GIT_REPOSITORY https://github.com/lwip-tcpip/lwip
            GIT_TAG ${MINI_TREE_LWIP_VERSION}
        )

        set(LWIP_DIR "${_lwip_source_dir}")

        # Default stack (no PPP / 6LoWPAN / apps) — suitable for Ethernet / USB-RNDIS / etc.
        set(_mini_tree_lwip_SRCS
            ${LWIP_DIR}/src/core/init.c
            ${LWIP_DIR}/src/core/def.c
            ${LWIP_DIR}/src/core/dns.c
            ${LWIP_DIR}/src/core/inet_chksum.c
            ${LWIP_DIR}/src/core/ip.c
            ${LWIP_DIR}/src/core/mem.c
            ${LWIP_DIR}/src/core/memp.c
            ${LWIP_DIR}/src/core/netif.c
            ${LWIP_DIR}/src/core/pbuf.c
            ${LWIP_DIR}/src/core/raw.c
            ${LWIP_DIR}/src/core/stats.c
            ${LWIP_DIR}/src/core/sys.c
            ${LWIP_DIR}/src/core/altcp.c
            ${LWIP_DIR}/src/core/altcp_alloc.c
            ${LWIP_DIR}/src/core/altcp_tcp.c
            ${LWIP_DIR}/src/core/tcp.c
            ${LWIP_DIR}/src/core/tcp_in.c
            ${LWIP_DIR}/src/core/tcp_out.c
            ${LWIP_DIR}/src/core/timeouts.c
            ${LWIP_DIR}/src/core/udp.c
            ${LWIP_DIR}/src/core/ipv4/acd.c
            ${LWIP_DIR}/src/core/ipv4/autoip.c
            ${LWIP_DIR}/src/core/ipv4/dhcp.c
            ${LWIP_DIR}/src/core/ipv4/etharp.c
            ${LWIP_DIR}/src/core/ipv4/icmp.c
            ${LWIP_DIR}/src/core/ipv4/igmp.c
            ${LWIP_DIR}/src/core/ipv4/ip4_frag.c
            ${LWIP_DIR}/src/core/ipv4/ip4.c
            ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
            ${LWIP_DIR}/src/core/ipv6/dhcp6.c
            ${LWIP_DIR}/src/core/ipv6/ethip6.c
            ${LWIP_DIR}/src/core/ipv6/icmp6.c
            ${LWIP_DIR}/src/core/ipv6/inet6.c
            ${LWIP_DIR}/src/core/ipv6/ip6.c
            ${LWIP_DIR}/src/core/ipv6/ip6_addr.c
            ${LWIP_DIR}/src/core/ipv6/ip6_frag.c
            ${LWIP_DIR}/src/core/ipv6/mld6.c
            ${LWIP_DIR}/src/core/ipv6/nd6.c
            ${LWIP_DIR}/src/api/api_lib.c
            ${LWIP_DIR}/src/api/api_msg.c
            ${LWIP_DIR}/src/api/err.c
            ${LWIP_DIR}/src/api/if_api.c
            ${LWIP_DIR}/src/api/netbuf.c
            ${LWIP_DIR}/src/api/netdb.c
            ${LWIP_DIR}/src/api/netifapi.c
            ${LWIP_DIR}/src/api/sockets.c
            ${LWIP_DIR}/src/api/tcpip.c
            ${LWIP_DIR}/src/netif/ethernet.c
        )

        add_library(mini_tree_lwip INTERFACE)
        add_library(lwip::lwip ALIAS mini_tree_lwip)
        target_sources(mini_tree_lwip INTERFACE ${_mini_tree_lwip_SRCS})
        target_include_directories(mini_tree_lwip INTERFACE
            "${LWIP_DIR}/src/include"
        )
        message(STATUS "mini_tree lwIP: ${MINI_TREE_LWIP_VERSION} @ ${LWIP_DIR}")
    endif()

    target_link_libraries(${target} PUBLIC mini_tree_lwip)
    target_include_directories(${target} PUBLIC "${_port_inc}")
endfunction()
