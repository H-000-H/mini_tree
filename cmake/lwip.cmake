# lwIP — 基础设施，vendored under lib/lwip (STABLE-2_2_1_RELEASE / 2.2.1).
# Not linked by default: board must provide lwipopts.h (+ arch/cc.h, sys_arch…).
if(TARGET mini_tree_lwip)
    return()
endif()

set(MINI_TREE_LWIP_VERSION "2.2.1" CACHE STRING "lwIP release version")
set(MINI_TREE_LWIP_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/lwip" CACHE PATH "lwIP source root")

if(NOT EXISTS "${MINI_TREE_LWIP_DIR}/src/include/lwip/init.h")
    message(STATUS "mini_tree lwIP: not found under ${MINI_TREE_LWIP_DIR} — skip")
    return()
endif()

set(LWIP_DIR "${MINI_TREE_LWIP_DIR}")

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
message(STATUS "mini_tree lwIP: ${MINI_TREE_LWIP_VERSION} @ ${MINI_TREE_LWIP_DIR}")

# Link lwIP into a target. Pass directory that contains lwipopts.h (and arch/).
# Example: mini_tree_link_lwip(my_fw "${CMAKE_CURRENT_SOURCE_DIR}/port")
function(mini_tree_link_lwip target)
    if(NOT TARGET mini_tree_lwip)
        message(FATAL_ERROR "mini_tree_link_lwip: mini_tree_lwip missing")
    endif()
    if(${ARGC} LESS 2)
        message(FATAL_ERROR "mini_tree_link_lwip(<target> <port_include_dir>)")
    endif()
    set(_port_inc "${ARGV1}")
    if(NOT EXISTS "${_port_inc}/lwipopts.h")
        message(FATAL_ERROR "mini_tree_link_lwip: lwipopts.h not found in ${_port_inc}")
    endif()
    target_link_libraries(${target} PUBLIC mini_tree_lwip)
    target_include_directories(${target} PUBLIC "${_port_inc}")
endfunction()
