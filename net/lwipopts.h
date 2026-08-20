/**
 * @license SPDX-License-Identifier: Apache-2.0
 * @file net/lwipopts.h
 * @brief lwIP 配置选项 (mini_tree 依赖, 由具体数值从kconfig导入)
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H
#ifdef __cplusplus
extern "C" {
#endif
/*-----------------------------------------------------------
 * 操作系统
 *----------------------------------------------------------*/
#define NO_SYS                      CONFIG_SYS/*是否使用操作系统*/
#define SYS_LIGHTWEIGHT_PROT        CONFIG_SYS_LIGHTWEIGHT_PROT/*是否使用轻量级保护*/

/*-----------------------------------------------------------
 * 内存
 *----------------------------------------------------------*/
#define MEM_ALIGNMENT               CONFIG_MEM_ALIGNMENT/*内存对齐方式*/
#define MEM_SIZE                    CONFIG_MEM_SIZE/*内存池大小*/
#define MEMP_NUM_PBUF               CONFIG_MEMP_NUM_PBUF/*buf数量*/
#define MEMP_NUM_TCP_PCB            CONFIG_MEMP_NUM_TCP_PCB/*TCP协议控制块数量*/
#define MEMP_NUM_UDP_PCB            CONFIG_MEMP_NUM_UDP_PCB/*UDP协议控制块数量*/
#define MEMP_NUM_TCP_PCB_LISTEN     CONFIG_MEMP_NUM_TCP_PCB_LISTEN/*TCP监听控制块数量*/
#define MEMP_NUM_TCP_SEG            CONFIG_MEMP_NUM_TCP_SEG/*TCP段数量*/
#define MEMP_NUM_SYS_TIMEOUT        CONFIG_MEMP_NUM_SYS_TIMEOUT/*定时器数量*/

/*-----------------------------------------------------------
 * PBUF
 *----------------------------------------------------------*/
#define PBUF_POOL_SIZE              CONFIG_PBUF_POOL_SIZE/*pbuf池大小*/
#define PBUF_POOL_BUFSIZE           CONFIG_PBUF_POOL_BUFSIZE/*pbuf池中每个pbuf的大小*/

/*-----------------------------------------------------------
 * UDP
 *----------------------------------------------------------*/
#define LWIP_UDP                    CONFIG_LWIP_UDP/*是否启用UDP协议*/
#define UDP_TTL                     CONFIG_UDP_TTL/*UDP数据包的默认生存时间*/

/*-----------------------------------------------------------
 * TCP
 *----------------------------------------------------------*/
#define LWIP_TCP                    CONFIG_LWIP_TCP/*是否启用TCP协议*/
#define TCP_TTL                     CONFIG_TCP_TTL/*TCP数据包默认生存时间*/
#define TCP_MSS                     CONFIG_TCP_MSS/*TCP最大报文段大小*/
#define TCP_WND                     CONFIG_TCP_WND/*TCP接收窗口*/
#define TCP_SND_BUF                 CONFIG_TCP_SND_BUF/*TCP发送缓冲*/

/*-----------------------------------------------------------
 * 以太网 / 网卡
 *----------------------------------------------------------*/
#define LWIP_ETHERNET               CONFIG_LWIP_ETHERNET/*是否启用以太网支持*/
#define LWIP_CHECKSUM_CTRL_PER_NETIF CONFIG_LWIP_CHECKSUM_CTRL_PER_NETIF/*每网口独立校验和控制*/

/*-----------------------------------------------------------
 * RAW API / 定时器 (裸机 NO_SYS 轮询)
 *----------------------------------------------------------*/
#define LWIP_RAW                    CONFIG_LWIP_RAW/*是否启用RAW API*/
#define LWIP_TIMERS                 CONFIG_LWIP_TIMERS/*是否启用协议栈定时器*/

/*-----------------------------------------------------------
 * 内存分配策略
 *----------------------------------------------------------*/
#define MEM_LIBC_MALLOC             CONFIG_MEM_LIBC_MALLOC/*0=使用lwIP内部内存池(裸机推荐)*/

/*-----------------------------------------------------------
 * 校验和
 *----------------------------------------------------------*/
#define CHECKSUM_GEN_IP             CONFIG_CHECKSUM_GEN_IP/*生成IP校验和*/
#define CHECKSUM_GEN_UDP            CONFIG_CHECKSUM_GEN_UDP/*生成UDP校验和*/
#define CHECKSUM_GEN_TCP            CONFIG_CHECKSUM_GEN_TCP/*生成TCP校验和*/
#define CHECKSUM_CHECK_IP           CONFIG_CHECKSUM_CHECK_IP/*校验入站IP校验和*/
#define CHECKSUM_CHECK_UDP          CONFIG_CHECKSUM_CHECK_UDP/*校验入站UDP校验和*/
#define CHECKSUM_CHECK_TCP          CONFIG_CHECKSUM_CHECK_TCP/*校验入站TCP校验和*/

/*-----------------------------------------------------------
 * ICMP / ARP
 *----------------------------------------------------------*/
#define LWIP_ICMP                   CONFIG_LWIP_ICMP/*是否启用ICMP协议*/
#define LWIP_ICMP6                  CONFIG_LWIP_ICMP6/*是否启用ICMPv6协议*/
#define LWIP_ARP                    CONFIG_LWIP_ARP/*是否启用ARP协议*/
#define ARP_TABLE_SIZE              CONFIG_ARP_TABLE_SIZE/*ARP表项数量*/

/*-----------------------------------------------------------
 * IP
 *----------------------------------------------------------*/
#define LWIP_IPV4                   CONFIG_LWIP_IPV4/*是否启用IPv4协议*/
#define LWIP_IPV6                   CONFIG_LWIP_IPV6/*是否启用IPv6协议*/
#define IP_REASSEMBLY               CONFIG_IP_REASSEMBLY/*是否启用IP分片重组*/
#define IP_FRAG                     CONFIG_IP_FRAG/*是否启用IP分片发送*/
#define IP_FRAG_USES_STATIC_BUF     CONFIG_IP_FRAG_USES_STATIC_BUF/*IP分片使用静态缓冲*/

/*-----------------------------------------------------------
 * DHCP
 *----------------------------------------------------------*/
#define LWIP_DHCP                   CONFIG_LWIP_DHCP/*是否启用DHCP协议*/
#define LWIP_AUTOIP                 CONFIG_LWIP_AUTOIP/*是否启用AutoIP协议*/

/*-----------------------------------------------------------
 * DNS
 *----------------------------------------------------------*/
#define LWIP_DNS                    CONFIG_LWIP_DNS/*是否启用DNS协议*/

/*-----------------------------------------------------------
 * Socket / Netconn
 *----------------------------------------------------------*/
#define LWIP_SOCKET                 CONFIG_LWIP_SOCKET/*是否启用Socket API*/
#define LWIP_NETCONN                CONFIG_LWIP_NETCONN/*是否启用Netconn API*/

/*-----------------------------------------------------------
 * 网络接口
 *----------------------------------------------------------*/
#define LWIP_NETIF_STATUS_CALLBACK  CONFIG_LWIP_NETIF_STATUS_CALLBACK/*是否启用网络接口状态回调*/
#define LWIP_NETIF_LINK_CALLBACK    CONFIG_LWIP_NETIF_LINK_CALLBACK/*是否启用网络接口链路回调*/
#define LWIP_NETIF_API              CONFIG_LWIP_NETIF_API/*是否启用netif API*/
#define LWIP_NETCONN_RECVTIMEO      CONFIG_LWIP_NETCONN_RECVTIMEO/*netconn接收超时支持*/

/*-----------------------------------------------------------
 * SOCKET/NETCONN 配套内存池
 *----------------------------------------------------------*/
#define MEMP_NUM_NETCONN            CONFIG_MEMP_NUM_NETCONN/*netconn数量*/
#define MEMP_NUM_NETBUF             CONFIG_MEMP_NUM_NETBUF/*netbuf数量*/
#define MEMP_NUM_SELECT_CB          CONFIG_MEMP_NUM_SELECT_CB/*select回调数量*/
#define MEMP_NUM_TCPIP_MSG_API      CONFIG_MEMP_NUM_TCPIP_MSG_API/*tcpip API消息数量*/
#define MEMP_NUM_TCPIP_MSG_INPKT    CONFIG_MEMP_NUM_TCPIP_MSG_INPKT/*tcpip入包消息数量*/

/*-----------------------------------------------------------
 * 调试
 *----------------------------------------------------------*/
#define LWIP_DEBUG                  CONFIG_LWIP_DEBUG/*是否启用调试*/
#define LWIP_STATS                  CONFIG_LWIP_STATS/*是否启用统计信息*/
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* LWIPOPTS_H */