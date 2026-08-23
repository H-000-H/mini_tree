/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file net/lwipopts.h
 *@brief lwIP 配置选项 (mini_tree 依赖, 由具体数值从 kconfig 导入)
 *@author H-000-H

 */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#ifdef __cplusplus
extern "C"
{
#endif

/*-----------------------------------------------------------
 * 操作系统与多任务适配
 *----------------------------------------------------------*/
#define NO_SYS CONFIG_SYS /* mini-tree无所谓你无脑带操作系统就行 */
#define SYS_LIGHTWEIGHT_PROT CONFIG_SYS_LIGHTWEIGHT_PROT /* 是否使用轻量级保护 (关中断) */
#define LWIP_COMPAT_MUTEX CONFIG_LWIP_COMPAT_MUTEX /* 0=使用原生sys_mutex_t, 1=用sys_sem_t模拟 */

/* TCPIP 核心线程参数 (NO_SYS == 0 必填) */
#define TCPIP_THREAD_NAME CONFIG_TCPIP_THREAD_NAME /* tcpip 线程名称 */
#define TCPIP_THREAD_STACKSIZE CONFIG_TCPIP_THREAD_STACKSIZE /* tcpip 线程栈大小 */
#define TCPIP_THREAD_PRIO CONFIG_TCPIP_THREAD_PRIO /* tcpip 线程优先级 */
#define TCPIP_MBOX_SIZE CONFIG_TCPIP_MBOX_SIZE /* tcpip 核心线程邮箱容量 */

/* 默认接收邮箱容量 (NO_SYS == 0 必填) */
#define DEFAULT_RAW_RECVMBOX_SIZE CONFIG_DEFAULT_RAW_RECVMBOX_SIZE /* RAW 接收邮箱大小 */
#define DEFAULT_UDP_RECVMBOX_SIZE CONFIG_DEFAULT_UDP_RECVMBOX_SIZE /* UDP 接收邮箱大小 */
#define DEFAULT_TCP_RECVMBOX_SIZE CONFIG_DEFAULT_TCP_RECVMBOX_SIZE /* TCP 接收邮箱大小 */
#define DEFAULT_ACCEPTMBOX_SIZE CONFIG_DEFAULT_ACCEPTMBOX_SIZE /* TCP Accept 接收邮箱大小 */

/*-----------------------------------------------------------
 * 内存管理
 *----------------------------------------------------------*/
#define MEM_ALIGNMENT CONFIG_MEM_ALIGNMENT /* 内存对齐字节数 (通常为 4 或 8) */
#define MEM_SIZE CONFIG_MEM_SIZE /* lwIP 堆内存池大小 */
#define MEMP_NUM_PBUF CONFIG_MEMP_NUM_PBUF /* pbuf 结构体数量 */
#define MEMP_NUM_TCP_PCB CONFIG_MEMP_NUM_TCP_PCB /* TCP 协议控制块数量 */
#define MEMP_NUM_UDP_PCB CONFIG_MEMP_NUM_UDP_PCB /* UDP 协议控制块数量 */
#define MEMP_NUM_TCP_PCB_LISTEN CONFIG_MEMP_NUM_TCP_PCB_LISTEN /* TCP 监听控制块数量 */
#define MEMP_NUM_TCP_SEG CONFIG_MEMP_NUM_TCP_SEG /* TCP 报文段数量 */
#define MEMP_NUM_SYS_TIMEOUT CONFIG_MEMP_NUM_SYS_TIMEOUT /* 活跃定时器最大数量 */
#define MEM_LIBC_MALLOC CONFIG_MEM_LIBC_MALLOC /* 0=使用 lwIP 堆, 1=使用系统 malloc */

/*-----------------------------------------------------------
 * PBUF 缓冲池
 *----------------------------------------------------------*/
#define PBUF_POOL_SIZE CONFIG_PBUF_POOL_SIZE /* PBUF 缓冲池数量 */
#define PBUF_POOL_BUFSIZE CONFIG_PBUF_POOL_BUFSIZE /* 每个 PBUF 缓冲区大小 */

/*-----------------------------------------------------------
 * UDP 协议
 *----------------------------------------------------------*/
#define LWIP_UDP CONFIG_LWIP_UDP /* 是否启用 UDP 协议 */
#define UDP_TTL CONFIG_UDP_TTL /* UDP 数据包默认生存时间 */

/*-----------------------------------------------------------
 * TCP 协议
 *----------------------------------------------------------*/
#define LWIP_TCP CONFIG_LWIP_TCP /* 是否启用 TCP 协议 */
#define TCP_TTL CONFIG_TCP_TTL /* TCP 数据包默认生存时间 */
#define TCP_MSS CONFIG_TCP_MSS /* TCP 最大报文段大小 */
#define TCP_WND CONFIG_TCP_WND /* TCP 接收窗口大小 */
#define TCP_SND_BUF CONFIG_TCP_SND_BUF /* TCP 发送缓冲区大小 */

/*-----------------------------------------------------------
 * altcp / TLS
 *----------------------------------------------------------*/
#define LWIP_ALTCP CONFIG_LWIP_ALTCP /* 应用层 TCP 抽象 (关闭时 altcp_* 退化为 tcp_*) */
#define LWIP_ALTCP_TLS CONFIG_LWIP_ALTCP_TLS /* altcp 之上的 TLS 层 */
#define LWIP_ALTCP_TLS_MBEDTLS CONFIG_LWIP_ALTCP_TLS_MBEDTLS /* altcp TLS 的 mbedTLS 后端 */

/*-----------------------------------------------------------
 * 以太网 / 网络接口 (netif)
 *----------------------------------------------------------*/
#define LWIP_ETHERNET CONFIG_LWIP_ETHERNET /* 是否启用以太网支持 */
#define LWIP_CHECKSUM_CTRL_PER_NETIF                                                               \
    CONFIG_LWIP_CHECKSUM_CTRL_PER_NETIF /* 每网口独立校验和控制                          \
                                         */
#define LWIP_NETIF_STATUS_CALLBACK                                                                 \
    CONFIG_LWIP_NETIF_STATUS_CALLBACK /* 是否启用网络接口状态回调                      \
                                       */
#define LWIP_NETIF_LINK_CALLBACK CONFIG_LWIP_NETIF_LINK_CALLBACK /* 是否启用网络接口链路回调 */
#define LWIP_NETIF_API CONFIG_LWIP_NETIF_API /* 是否启用 netif API */

/*-----------------------------------------------------------
 * RAW API / 协议栈定时器
 *----------------------------------------------------------*/
#define LWIP_RAW CONFIG_LWIP_RAW /* 是否启用 RAW API */
#define LWIP_TIMERS CONFIG_LWIP_TIMERS /* 是否启用协议栈定时器 */

/*-----------------------------------------------------------
 * 硬件/软件校验和计算
 *----------------------------------------------------------*/
#define CHECKSUM_GEN_IP CONFIG_CHECKSUM_GEN_IP /* 生成 IP 校验和 */
#define CHECKSUM_GEN_UDP CONFIG_CHECKSUM_GEN_UDP /* 生成 UDP 校验和 */
#define CHECKSUM_GEN_TCP CONFIG_CHECKSUM_GEN_TCP /* 生成 TCP 校验和 */
#define CHECKSUM_CHECK_IP CONFIG_CHECKSUM_CHECK_IP /* 校验入站 IP 校验和 */
#define CHECKSUM_CHECK_UDP CONFIG_CHECKSUM_CHECK_UDP /* 校验入站 UDP 校验和 */
#define CHECKSUM_CHECK_TCP CONFIG_CHECKSUM_CHECK_TCP /* 校验入站 TCP 校验和 */

/*-----------------------------------------------------------
 * ICMP / ARP 协议
 *----------------------------------------------------------*/
#define LWIP_ICMP CONFIG_LWIP_ICMP /* 是否启用 ICMP 协议 */
#define LWIP_ICMP6 CONFIG_LWIP_ICMP6 /* 是否启用 ICMPv6 协议 */
#define LWIP_ARP CONFIG_LWIP_ARP /* 是否启用 ARP 协议 */
#define ARP_TABLE_SIZE CONFIG_ARP_TABLE_SIZE /* ARP 映射表大小 */

/*-----------------------------------------------------------
 * IP 层选项
 *----------------------------------------------------------*/
#define LWIP_IPV4 CONFIG_LWIP_IPV4 /* 是否启用 IPv4 */
#define LWIP_IPV6 CONFIG_LWIP_IPV6 /* 是否启用 IPv6 */
#define IP_REASSEMBLY CONFIG_IP_REASSEMBLY /* 是否启用 IP 分片重组 */
#define IP_FRAG CONFIG_IP_FRAG /* 是否启用 IP 分片发送 */
#define IP_FRAG_USES_STATIC_BUF CONFIG_IP_FRAG_USES_STATIC_BUF /* 分片发送是否使用静态内存 */

/*-----------------------------------------------------------
 * 网络服务 (DHCP / AutoIP / DNS)
 *----------------------------------------------------------*/
#define LWIP_DHCP CONFIG_LWIP_DHCP /* 是否启用 DHCP 客户端 */
#define LWIP_AUTOIP CONFIG_LWIP_AUTOIP /* 是否启用 AutoIP */
#define LWIP_DNS CONFIG_LWIP_DNS /* 是否启用 DNS 域名解析 */

/*-----------------------------------------------------------
 * 高级 API (Socket / Netconn)
 *----------------------------------------------------------*/
#define LWIP_SOCKET CONFIG_LWIP_SOCKET /* 是否启用 BSD Socket API */
#define LWIP_NETCONN CONFIG_LWIP_NETCONN /* 是否启用 Netconn API */
#define LWIP_NETCONN_RECVTIMEO CONFIG_LWIP_NETCONN_RECVTIMEO /* 是否支持 Netconn 接收超时 */

/* BSD Socket 属性配置 */
#define LWIP_SO_RCVTIMEO CONFIG_LWIP_SO_RCVTIMEO /* 是否支持 SO_RCVTIMEO 接收超时 */
#define LWIP_SO_SNDTIMEO CONFIG_LWIP_SO_SNDTIMEO /* 是否支持 SO_SNDTIMEO 发送超时 */
#define LWIP_SO_RCVBUF CONFIG_LWIP_SO_RCVBUF /* 是否支持 SO_RCVBUF 接收缓冲调节 */
#define LWIP_TIMEVAL_PRIVATE                                                                       \
    CONFIG_LWIP_TIMEVAL_PRIVATE /* 0=使用系统 struct timeval, 1=lwIP 内置 */
#define LWIP_POSIX_SOCKETS_IO_NAMES                                                                \
    CONFIG_LWIP_POSIX_SOCKETS_IO_NAMES /* 是否映射 read/write/close */

/*-----------------------------------------------------------
 * Socket / Netconn 配套内存池
 *----------------------------------------------------------*/
#define MEMP_NUM_NETCONN CONFIG_MEMP_NUM_NETCONN /* netconn 结构体数量 */
#define MEMP_NUM_NETBUF CONFIG_MEMP_NUM_NETBUF /* netbuf 结构体数量 */
#define MEMP_NUM_SELECT_CB CONFIG_MEMP_NUM_SELECT_CB /* select 阻塞控制块数量 */
#define MEMP_NUM_TCPIP_MSG_API CONFIG_MEMP_NUM_TCPIP_MSG_API /* tcpip API 消息数量 */
#define MEMP_NUM_TCPIP_MSG_INPKT CONFIG_MEMP_NUM_TCPIP_MSG_INPKT /* tcpip 数据包投递消息数量 */

/*-----------------------------------------------------------
 * 调试与统计
 *----------------------------------------------------------*/
#define LWIP_DEBUG CONFIG_LWIP_DEBUG /* 是否开启 lwIP 调试日志 */
#define LWIP_STATS CONFIG_LWIP_STATS /* 是否启用协议栈统计信息 */

#define PPP_SUPPORT CONFIG_PPP_SUPPORT /* 启用 PPP 功能 */
#define PPPOS_SUPPORT CONFIG_PPPOS_SUPPORT /* 启用 PPP over Serial (串口 PPP) */
#define PAP_SUPPORT CONFIG_PAP_SUPPORT /* 启用 PAP 认证  */
#define CHAP_SUPPORT CONFIG_CHAP_SUPPORT /* 启用 CHAP 认证  */
#define PPP_IPV6_SUPPORT CONFIG_PPP_IPV6_SUPPORT /* PPP 链路 IPv6 (IPV6CP) 协商 */
#define PPP_INPROC_IRQ_SAFE CONFIG_PPP_INPROC_IRQ_SAFE /* 允许中断上下文 pppos_input */
#define PPP_MAXMRU CONFIG_PPP_MAXMRU /* PPP 最大接收单元 */
#define PPP_MAXMTU CONFIG_PPP_MAXMTU /* PPP 最大发送单元 */
#ifdef __cplusplus
}
#endif

#endif /* LWIPOPTS_H */