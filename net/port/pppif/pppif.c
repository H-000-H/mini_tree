/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file pppif.c
 *@brief PPP over Serial (PPPoS) 适配层 — 4G 模组拨号与 lwIP 适配
 *@author H-000-H
 *@details
 *   功能描述：
 *   1. 控制态 (AT Command):
 *   通过 VFS ioctl 执行 MODEM_CMD_AT_SEND/RECV 完成驻网、APN 配置及 ATD*99# 拨号。
 *   2. 数据态 (PPPoS):
 *   - 发送链路: lwIP -> pppos_output_cb -> device_write
 *   - 接收链路: 独立后台任务 -> device_read -> pppos_input -> tcpip_thread
 *   - 协议栈定时器由 NO_SYS=0 架构下的 tcpip 核心线程统一调度。
 */

#include "compiler_compat.h"
/* IWYU pragma: keep — PPP_SUPPORT / PPPOS_SUPPORT / NO_SYS 宏由 opt.h -> lwipopts.h -> config.h 提供 */
#include "lwip/opt.h" /* IWYU pragma: keep */

#if PPP_SUPPORT && PPPOS_SUPPORT

#include "device.h"
#include "drivers/modem/include/modem_drv.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h" /* IWYU pragma: keep */
#include "lwip/timeouts.h"
#include "net/arch/sys_arch.h"
#include "netif/ppp/ppp.h"
#include "netif/ppp/pppos.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/** @brief PPPoS 数据态接收缓存大小 (字节) */
#ifdef CONFIG_PPPIF_RX_BUF_SIZE
#define PPPIF_RX_BUF_SIZE CONFIG_PPPIF_RX_BUF_SIZE
#else
#define PPPIF_RX_BUF_SIZE 512U
#endif

/** @brief PPPoS 后台接收处理任务的栈空间大小 (字节) */
#ifdef CONFIG_PPPIF_RX_TASK_STACK_SIZE
#define PPPIF_RX_TASK_STACK_SIZE CONFIG_PPPIF_RX_TASK_STACK_SIZE
#else
#define PPPIF_RX_TASK_STACK_SIZE 2048U
#endif

/** @brief PPPoS 后台接收处理任务的优先级 */
#ifdef CONFIG_PPPIF_RX_TASK_PRIO
#define PPPIF_RX_TASK_PRIO CONFIG_PPPIF_RX_TASK_PRIO
#else
#define PPPIF_RX_TASK_PRIO 6U
#endif

#define PPPIF_AT_BUF_SIZE 160U /**< AT 应答缓存区大小 (字节) */
#define PPPIF_AT_RECV_CHUNK_MS 200U /**< 单次 AT 分片接收轮询超时 (ms) */
#define PPPIF_AT_TIMEOUT_MS 3000U /**< 单条 AT 命令整体超时 (ms) */
#define PPPIF_UART_TIMEOUT_MS 100U /**< 数据态 UART 阻塞读取超时 (ms) */

static const char* const k_tag = "pppif";

struct pppif_context
{
    struct device* modem_dev; /**< 模组设备句柄 */
    struct netif ppp_netif; /**< lwIP netif 实例 */
    ppp_pcb* ppp_pcb; /**< lwIP PPP 控制块 */
    osal_thread_t rx_thread; /**< 接收处理线程句柄 */
    volatile uint8_t is_running; /**< 线程运行标志位 */
    volatile bool is_link_up; /**< PPP 链路就绪标志 */
    uint8_t rx_buf[PPPIF_RX_BUF_SIZE]; /**< 接收数据流缓存 */
};

static struct pppif_context s_pppif_context = {0};

/**
 * @brief AT 指令交互：发送指令并阻塞匹配期望返回关键字
 * @param[in] cmd 发送的 AT 命令字符串 (需包含 \\r 结尾)
 * @param[in] expect_resp 期望匹配的关键响应 (如 "OK", "CONNECT")
 * @param[in] timeout_ms 指令整体执行超时时间 (ms)
 * @return 成功返回 VFS_OK，超时或失败返回对应错误码
 */
static int pppif_at_send_expect(const char* cmd, const char* expect_resp, uint32_t timeout_ms)
{
    struct modem_at_buf at_buf = {0};
    uint8_t resp_buf[PPPIF_AT_BUF_SIZE];
    uint32_t start_time = 0;
    size_t total_receive = 0;
    int ret = 0;

    if (!s_pppif_context.modem_dev || !cmd || !expect_resp)
        return VFS_ERR_INVAL;

    at_buf.tx = (const uint8_t*)cmd;
    at_buf.tx_len = strlen(cmd);
    at_buf.rx = resp_buf;
    at_buf.rx_cap = sizeof(resp_buf) - 1U;
    at_buf.rx_len = 0;

    ret = device_ioctl(s_pppif_context.modem_dev, MODEM_CMD_AT_SEND, &at_buf, sizeof(at_buf), timeout_ms);

    if (ret != VFS_OK)
        return ret;

    start_time = osal_time_ms();

    do
    {
        at_buf.rx = &resp_buf[total_receive];
        at_buf.rx_cap = sizeof(resp_buf) - 1 - total_receive;
        at_buf.rx_len = 0;
        ret = device_ioctl(s_pppif_context.modem_dev, MODEM_CMD_AT_RECV, &at_buf, sizeof(at_buf), PPPIF_AT_RECV_CHUNK_MS);
        if (ret == VFS_OK && at_buf.rx_len > 0)
        {
            total_receive += at_buf.rx_len;
            resp_buf[total_receive] = '\0';

            if (strstr((const char*)resp_buf, expect_resp) != NULL)
                return VFS_OK;

            if (strcmp(expect_resp, "CONNECT") != 0 && strstr((const char*)resp_buf, "ERROR") != NULL)
                return VFS_ERR_IO;
        }
    } while ((osal_time_ms() - start_time) < timeout_ms);

    return VFS_ERR_TIMEOUT;
}

/**
 * @brief 模组拨号控制流程：握手 -> 关回显 -> 设置 APN -> 进入数据透传态
 * @param[in] apn 运营商接入点名称
 * @return 成功返回 VFS_OK，失败返回错误码
 */
static int pppif_modem_dial(const char* apn)
{
    char cmd_buf[64];
    int ret;

    /*基础AT通信握手*/
    ret = pppif_at_send_expect("AT\r", "OK", PPPIF_AT_TIMEOUT_MS);
    if (ret != VFS_OK)
    {
        SYS_LOGE(k_tag, "Modem AT no response: %d", ret);
        return ret;
    }
    /* 关闭命令回显 */
    COMPAT_IGNORE_RESULT(pppif_at_send_expect("ATE0\r", "OK", PPPIF_AT_TIMEOUT_MS));
    if (apn && apn[0] != '\0')
    {
        /* 配置 PDP 上下文 / APN */
        int snprintf_ret = snprintf(cmd_buf, sizeof(cmd_buf), "AT+CGDCONT=1,\"IP\",\"%s\"\r", apn);
        if (snprintf_ret <= 0 || (size_t)snprintf_ret >= sizeof(cmd_buf))
        {
            SYS_LOGE(k_tag, "APN too long: %s", apn);
            return VFS_ERR_INVAL;
        }
        ret = pppif_at_send_expect(cmd_buf, "OK", PPPIF_AT_TIMEOUT_MS);
        if (ret != VFS_OK)
        {
            SYS_LOGE(k_tag, "Set APN failed: %d", ret);
            return ret;
        }
    }

    /*  触发 PPP 拨号命令，等待模组切入透传数据态 */
    ret = pppif_at_send_expect("ATD*99#\r", "CONNECT", PPPIF_AT_TIMEOUT_MS);
    if (ret != VFS_OK)
        SYS_LOGE(k_tag, "Modem dial failed: %d", ret);
    return ret;
}

/**
 * @brief PPPoS 发送输出回调函数 (对接 lwIP 官方签名: u32_t (*)(ppp_pcb*, u8_t*, u32_t, void*))
 * @param[in] pcb PPP 控制块
 * @param[in] data 待发送的 PPP 报文指针
 * @param[in] len 数据长度
 * @param[in] ctx 用户上下文指针
 * @return 实际写入的字节数，失败返回 0
 */
static uint32_t pppif_output_callback(ppp_pcb* pcb, const void* data, uint32_t len, void* ctx)
{
    struct pppif_context* pctx = (struct pppif_context*)ctx;
    int written;
    COMPAT_IGNORE_RESULT(pcb);
    if (!pctx->modem_dev || !data || len == 0U)
        return 0;
    written = device_write(pctx->modem_dev, data, len, PPPIF_UART_TIMEOUT_MS);
    return (written > 0) ? (uint32_t)written : 0;
}

/**
 * @brief PPP 状态变更与链路生命周期回调
 * @param[in] pcb PPP 控制块
 * @param[in] err_code 错误码/状态码 (PPPERR_NONE, PPPERR_USER 等)
 * @param[in] ctx 用户上下文指针
 */
static void pppif_link_status_callback(ppp_pcb* pcb, int err_code, void* ctx)
{
    struct pppif_context* p_ctx = (struct pppif_context*)ctx;
    struct netif* ppp_netif = ppp_netif(pcb);
    switch (err_code)
    {
    case PPPERR_NONE:
    {
        /*ppp成功获得IP*/
        p_ctx->is_link_up = true;
        SYS_LOGI(k_tag, "PPP link up. IP: %s, GW: %s, MASK: %s", ip4addr_ntoa(netif_ip4_addr(ppp_netif)), ip4addr_ntoa(netif_ip4_gw(ppp_netif)), ip4addr_ntoa(netif_ip4_netmask(ppp_netif)));
#if LWIP_DNS
        /* 打印 DNS 地址 */
        const ip_addr_t* primary_dns = dns_getserver(0);
        const ip_addr_t* secondary_dns = dns_getserver(1);
        SYS_LOGI(k_tag, "DNS Server: 1: %s, 2: %s", ipaddr_ntoa(primary_dns), ipaddr_ntoa(secondary_dns));
#endif
        netif_set_link_up(ppp_netif);
        netif_set_up(ppp_netif);
        break;
    }
    case PPPERR_USER:
    {
        /*主动关闭对话*/
        p_ctx->is_link_up = false;
        SYS_LOGI(k_tag, "PPP session closed gracefully");
        ppp_free(p_ctx->ppp_pcb);
        p_ctx->ppp_pcb = NULL;
        break;
    }
    default:
    {
        p_ctx->is_link_up = false;
        SYS_LOGW(k_tag, "PPP link down or error: %d", err_code);
        netif_set_link_down(ppp_netif);
        break;
    }
    }
}

#if !NO_SYS
/**
 * @brief PPPoS 数据流后台接收线程入口(仅 RTOS 模式, 就是一个任务)
 * @param[in] arg 用户上下文参数
 */
static void pppif_rx_thread_entry(void* param)
{
    struct pppif_context* p_ctx = (struct pppif_context*)param;
    int receive_len;
    SYS_LOGI(k_tag, "ppp rx task started");

    while (p_ctx->is_running)
    {
        if (!p_ctx->modem_dev || !p_ctx->ppp_pcb)
        {
            osal_delay_ms(10);
            continue;
        }
        receive_len = device_read(p_ctx->modem_dev, p_ctx->rx_buf, sizeof(p_ctx->rx_buf), PPPIF_UART_TIMEOUT_MS);
        if (receive_len > 0)
            pppos_input(p_ctx->ppp_pcb, p_ctx->rx_buf, receive_len);
    }
    SYS_LOGI(k_tag, "PPP RX task exited");
    osal_task_delete(NULL);
}
#endif /* !NO_SYS */

/**
 * @brief PPP 数据态裸机轮询驱动 (NO_SYS=1 主循环周期调用)
 * @details 读模组串口裸字节流喂 pppos_input, 并驱动 lwIP 超时
 *          (LCP/IPCP 协商重传依赖 sys_check_timeouts)。
 * @return VFS_OK 或 VFS_ERR_*
 */
int pppif_poll(void)
{
    int receive_len;
    if (!s_pppif_context.modem_dev || !s_pppif_context.ppp_pcb)
        return VFS_ERR_INVAL;

    receive_len = device_read(s_pppif_context.modem_dev, s_pppif_context.rx_buf, sizeof(s_pppif_context.rx_buf), PPPIF_UART_TIMEOUT_MS);
    if (receive_len > 0)
        pppos_input(s_pppif_context.ppp_pcb, s_pppif_context.rx_buf, receive_len);

    sys_check_timeouts();
    return VFS_OK;
}

/**
 * @brief PPP 拨号与网络协议栈接入初始化
 * @param[in] modem_label 模组设备在 VFS 中的注册标签 (如 "a7670", "air780e")
 * @param[in] apn 接入点 APN (可为 NULL)
 * @param[in] username PAP/CHAP 认证账号 (可为 NULL)
 * @param[in] password PAP/CHAP 认证密码 (可为 NULL)
 * @return 成功返回 VFS_OK，失败返回对应错误码
 */
int pppif_init(const char* modem_label, const char* apn, const char* username, const char* password)
{
    int ret;
    if (!modem_label)
        return VFS_ERR_INVAL;

    if (s_pppif_context.modem_dev || s_pppif_context.ppp_pcb)
        return VFS_ERR_BUSY;

    s_pppif_context.modem_dev = device_find_by_label(modem_label);
    if (IS_ERR(s_pppif_context.modem_dev))
    {
        ret = PTR_ERR(s_pppif_context.modem_dev);
        SYS_LOGE(k_tag, "Modem '%s' not found: %d", modem_label, ret);
        s_pppif_context.modem_dev = NULL;
        return ret;
    }

    ret = device_open(s_pppif_context.modem_dev, NULL);
    if (ret != VFS_OK)
    {
        SYS_LOGE(k_tag, "Open modem device failed: %d", ret);
        s_pppif_context.modem_dev = NULL;
        return ret;
    }

    /* 执行 AT 拨号切入数据态 */
    ret = pppif_modem_dial(apn);
    if (ret != VFS_OK)
        goto err_clean_device;

    /* 创建 lwIP PPPoS 控制块 */
    s_pppif_context.ppp_pcb = pppos_create(&s_pppif_context.ppp_netif, pppif_output_callback, pppif_link_status_callback, &s_pppif_context);
    if (!s_pppif_context.ppp_pcb)
    {
        ret = VFS_ERR_NOMEM;
        goto err_clean_device;
    }

    /* 配置认证、DNS 获取以及默认网卡路由 */
    if (username && password)
        ppp_set_auth(s_pppif_context.ppp_pcb, PPPAUTHTYPE_ANY, username, password);
    ppp_set_usepeerdns(s_pppif_context.ppp_pcb, 1);
    ppp_set_default(s_pppif_context.ppp_pcb);

#if !NO_SYS
    /* 创建后台接收数据处理任务 (RTOS 模式) */
    s_pppif_context.is_running = true;
    ret = osal_task_create_handle("pppos_rx", PPPIF_RX_TASK_STACK_SIZE, PPPIF_RX_TASK_PRIO, pppif_rx_thread_entry, &s_pppif_context, (int)-1, &s_pppif_context.rx_thread);
    if (ret != 0 || !s_pppif_context.rx_thread)
    {
        ret = VFS_ERR_NOMEM;
        goto err_clean_ppp;
    }
#else
    /* 裸机模式: 无独立接收任务, 由主循环周期调用 pppif_poll() 驱动 */
    s_pppif_context.is_running = true;
#endif /* !NO_SYS */

    ret = ppp_connect(s_pppif_context.ppp_pcb, 0);
    if (ret != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_clean_ppp;
    }

    return VFS_OK;
err_clean_ppp:
    if (s_pppif_context.ppp_pcb)
    {
        COMPAT_IGNORE_RESULT(ppp_close(s_pppif_context.ppp_pcb, 1));
        COMPAT_IGNORE_RESULT(ppp_free(s_pppif_context.ppp_pcb));
        s_pppif_context.ppp_pcb = NULL;
    }
err_clean_device:
    COMPAT_IGNORE_RESULT(device_close(s_pppif_context.modem_dev));
    s_pppif_context.modem_dev = NULL;
    return ret;
}

int pppif_deinit(void)
{
    if (!s_pppif_context.ppp_pcb && !s_pppif_context.modem_dev)
        return VFS_ERR_INVAL;

    /* 停止后台接收线程 (RTOS 模式) / 标记轮询停止 (裸机模式) */
    s_pppif_context.is_running = 0;
    s_pppif_context.is_link_up = 0;

    /* 异步优雅断开 PPP (触发 PPPERR_USER 在状态回调中释放控制块) */
    if (s_pppif_context.ppp_pcb)
        COMPAT_IGNORE_RESULT(ppp_close(s_pppif_context.ppp_pcb, 0));

    /* 关闭底层模组设备 */
    if (s_pppif_context.modem_dev)
    {
        COMPAT_IGNORE_RESULT(device_close(s_pppif_context.modem_dev));
        s_pppif_context.modem_dev = NULL;
    }

    return VFS_OK;
}

int pppif_is_link_up(void) { return s_pppif_context.is_link_up; }
#endif /* PPP_SUPPORT && PPPOS_SUPPORT */