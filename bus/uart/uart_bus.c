/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file uart_bus.c
 *@brief uart bus 实现
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   UART BUS 实现 — UART 总线子系统 bus 层
 *   静态池: s_uart_hosts[HOST_MAX] (含 hal_uart_bus_host, ref_count) + s_uart_clients[CLIENT_MAX]
 *   数据流: VFS → uart_bus_open/close/read/write/transfer → uart_client_from_device → hal_uart_*
 *   HAL 直接调用 (无 vtable): host_init→hal_uart_dev_init, register→hw_open,
 *   unregister→hw_close, write/read/transfer→hal_uart_write/read
 *   controller_ops 表注册到 bus_controller_bind_full
 *   引用计数: register/unregister 改 ref_count (open/close 只 IO gate); deinit >0 拒绝销毁
 *   平台中立: 本文件不引用任何厂商 SDK, 所有硬件细节由 HAL 实现 (hal_uart_*.c) 承载。
 *   bus 层仅持有 hal_uart_bus_host (嵌入 host), 透传 hal_uart_config (VFS 从 DTSI 硬件直投填充)。
 *   --------------------------------------------------------------------------
 */

#define UART_BUS_IMPL
#include "uart_bus.h"

#include "board_config.h"
#include "bus.h"
#include "compiler_compat.h"
#include "device.h"
#include "driver.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

/* host 池 = DTS "uart" 节点数 (缺省 1, dtc-lite 生成 DTC_GEN_COUNT_UART) */
#ifndef DTC_GEN_COUNT_UART
#define DTC_GEN_COUNT_UART 1
#endif
#define UART_BUS_HOST_MAX DTC_GEN_COUNT_UART

/* client 池 = DTS "uart-client" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_UART_CLIENT
#define DTC_GEN_COUNT_UART_CLIENT 1
#endif
#define UART_BUS_CLIENT_MAX DTC_GEN_COUNT_UART_CLIENT

/** @brief UART host 运行时描述符 (静态池, HAL 嵌入 + atomic ref_count) */
struct uart_bus_host
{
    struct device* pdev; /**< 关联设备 */
    struct hal_uart_bus_host hal_host; /**< 嵌入 HAL host (非 vtable 指针) */
    COMPAT_ATOMIC_INT ref_count; /**< atomic 无锁计数 */
};

/** @brief UART client 运行时描述符 (静态池, 按 client_id 索引) */
struct uart_bus_client
{
    struct device* pdev; /**< 关联设备 */
    struct uart_bus_host* host; /**< 所属 host */
};

static struct uart_bus_host s_uart_hosts[UART_BUS_HOST_MAX];
static uint8_t s_uart_host_used[UART_BUS_HOST_MAX];
static osal_pool_t s_uart_host_pool_ctrl;
static struct uart_bus_client s_uart_clients[UART_BUS_CLIENT_MAX];
static uint8_t s_uart_client_used[UART_BUS_CLIENT_MAX];
static osal_pool_t s_uart_client_pool_ctrl;
static const char* const k_tag = "uart_bus";

/**
 * @brief UART Host/Client 池启动初始化
 */
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void uart_bus_pool_init(void)
{
    COMPAT_IGNORE_RESULT(
        osal_pool_init(&s_uart_host_pool_ctrl, s_uart_host_used, UART_BUS_HOST_MAX));
    COMPAT_IGNORE_RESULT(
        osal_pool_init(&s_uart_client_pool_ctrl, s_uart_client_used, UART_BUS_CLIENT_MAX));
}

/* -------------------------------------------------------------------------- */
/*Host Pool*/
/* -------------------------------------------------------------------------- */
/**
 * @brief 通过 device 指针查找对应的 uart_bus_host
 * @param[in] pdev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct uart_bus_host* uart_host_from_device(struct device* pdev)
{
    for (int index = 0; index < UART_BUS_HOST_MAX; index++)
        if (osal_pool_is_used(&s_uart_host_pool_ctrl, index) && s_uart_hosts[index].pdev == pdev)
            return &s_uart_hosts[index];
    return NULL;
}

/* -------------------------------------------------------------------------- */
/*Client Pool*/
/* -------------------------------------------------------------------------- */
/**
 * @brief 通过 device 指针查找对应的 uart_bus_client
 * @param[in] pdev client device 指针
 * @return 找到返回 client 指针, 未找到返回 NULL
 */
static struct uart_bus_client* uart_client_from_device(struct device* pdev)
{
    for (int index = 0; index < UART_BUS_CLIENT_MAX; index++)
        if (osal_pool_is_used(&s_uart_client_pool_ctrl, index) &&
            s_uart_clients[index].pdev == pdev)
            return &s_uart_clients[index];
    return NULL;
}

/* -------------------------------------------------------------------------- */
/*controller_ops (host 级操作)*/
/* -------------------------------------------------------------------------- */
static int uart_host_init_impl(struct device* pdev, const void* cfg);
static int uart_host_deinit_impl(struct device* pdev);
static int uart_host_role_impl(struct device* pdev);
static int uart_client_register_impl(struct device* pdev, const void* cfg, void** out);
static void uart_client_unregister_impl(struct device* pdev);

static const struct bus_controller_ops s_uart_controller_ops = {
    .init = uart_host_init_impl,
    .deinit = uart_host_deinit_impl,
    .role = uart_host_role_impl,
    .client_register = uart_client_register_impl,
    .client_unregister = uart_client_unregister_impl,
};

/* -------------------------------------------------------------------------- */
/*Host API*/
/* -------------------------------------------------------------------------- */
/**
 * @brief host 初始化实现 (controller_ops.init): 分配 host 池, 调用 hal_uart_dev_init, 绑定
 * controller
 * @param[in] pdev controller device (host)
 * @param[in] cfg host 配置 (struct hal_uart_config*, VFS 从 DTSI 硬件直投填充, bus 零翻译透传)
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int uart_host_init_impl(struct device* pdev, const void* cfg)
{
    const struct hal_uart_config* host_cfg = (const struct hal_uart_config*)cfg;
    struct uart_bus_host* host;
    int idx;
    int ret;

    if (!pdev || !host_cfg)
        return MINI_ERR_INVAL;

    if (uart_host_from_device(pdev))
        return MINI_OK;

    idx = osal_pool_claim(&s_uart_host_pool_ctrl);
    if (idx < 0)
        return MINI_ERR_NOMEM;

    host = &s_uart_hosts[idx];
    COMPAT_MEM_SET(host, 0, sizeof(*host));
    host->pdev = pdev;
    COMPAT_ATOMIC_RUNTIME_INIT(&host->ref_count, 0);

    /* HAL pdev 嵌入 host, 直接传对象指针, 零翻译透传 config */
    ret = hal_uart_dev_init(&host->hal_host, host_cfg);
    if (ret != MINI_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_host_pool_ctrl, idx));
        return ret;
    }

    ret = bus_controller_bind_full(pdev, BUS_TYPE_UART, &s_uart_controller_ops, host);
    if (ret != MINI_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_host_pool_ctrl, idx));
        return ret;
    }

    SYS_LOGI(k_tag, "host init OK: %s uart=%lu baud=%lu", device_get_name(pdev),
             (unsigned long)host_cfg->uart, (unsigned long)host_cfg->baud_rate);
    return MINI_OK;
}

/**
 * @brief host 反初始化实现 (controller_ops.deinit): 检查 ref_count, 解绑 controller, 释放池槽位
 * @param[in] pdev controller device (host)
 * @return 成功返回 MINI_OK, BUSY 返回 MINI_ERR_BUSY, 失败返回 VFS_ERR_*
 */
static int uart_host_deinit_impl(struct device* pdev)
{
    struct uart_bus_host* host;
    int idx;

    if (!pdev)
        return MINI_ERR_INVAL;

    host = uart_host_from_device(pdev);
    if (!host)
        return MINI_ERR_NODEV;

    /* atomic 检查, BUSY 时不销毁 (对齐 SPI) */
    if (COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST) > 0)
        return MINI_ERR_BUSY;

    bus_controller_unbind(pdev);

    /* HAL close: 关闭 UART (如果已 open) */
    if (host->hal_host.hw_inited)
        COMPAT_IGNORE_RESULT(hal_uart_dev_hw_close(&host->hal_host));

    idx = (int)(host - s_uart_hosts);
    COMPAT_MEM_SET(host, 0, sizeof(*host));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_host_pool_ctrl, idx));
    return MINI_OK;
}

/**
 * @brief 查询 host 角色 (controller_ops.role, UART 无 master/slave 之分, 固定返回 0)
 * @param[in] pdev controller device (host)
 * @return 固定返回 0
 */
static int uart_host_role_impl(struct device* pdev)
{
    COMPAT_IGNORE_RESULT(pdev);
    return 0; /* UART 无 master/slave 之分 */
}

int uart_bus_host_init(struct device* pdev, const struct hal_uart_config* cfg)
{
    return uart_host_init_impl(pdev, cfg);
}

int uart_bus_host_deinit(struct device* pdev) { return uart_host_deinit_impl(pdev); }

/* -------------------------------------------------------------------------- */
/*Client API*/
/* -------------------------------------------------------------------------- */
/**
 * @brief client 注册实现 (controller_ops.client_register): 分配 client, 绑定 host, 调用
 * hal_uart_dev_hw_open
 * @param[in] pdev client device
 * @param[in] cfg client 配置 (UART 无 per-client 配置, 此参数忽略)
 * @param[out] out 输出 client 私有上下文 (可 NULL)
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int uart_client_register_impl(struct device* pdev, const void* cfg, void** out)
{
    struct bus_controller* ctlr;
    struct uart_bus_host* host;
    struct uart_bus_client* cli;
    int idx;
    int ret;

    COMPAT_IGNORE_RESULT(cfg);
    if (!pdev)
        return MINI_ERR_INVAL;

    if (uart_client_from_device(pdev))
        return MINI_OK;

    /* 通过 parent 查找 host (pdev = client → device_get_parent → host device → s_controllers →
     * hw_ctx) */
    if (bus_controller_of(pdev, &ctlr) != MINI_OK)
        return MINI_ERR_NODEV;
    if (ctlr->type != BUS_TYPE_UART)
        return MINI_ERR_NODEV;
    host = (struct uart_bus_host*)ctlr->hw_ctx;
    if (!host)
        return MINI_ERR_IO;

    idx = osal_pool_claim(&s_uart_client_pool_ctrl);
    if (idx < 0)
        return MINI_ERR_NOMEM;

    cli = &s_uart_clients[idx];
    COMPAT_MEM_SET(cli, 0, sizeof(*cli));
    cli->pdev = pdev;
    cli->host = host;

    /* HAL hw_open: 配置 UART + 引脚 (DTSI 硬件直投值) */
    ret = hal_uart_dev_hw_open(&host->hal_host);
    if (ret != MINI_OK)
    {
        COMPAT_MEM_SET(cli, 0, sizeof(*cli));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_client_pool_ctrl, idx));
        return ret;
    }

    (void)COMPAT_ATOMIC_FETCH_ADD(&host->ref_count, 1,
                                  COMPAT_MO_SEQ_CST); /* 对齐 spi: client_register +1 */

    if (out)
        *out = cli;
    return MINI_OK;
}

/**
 * @brief client 注销实现 (controller_ops.client_unregister): 关闭 UART, ref_count -1, 释放池槽位
 * @param[in] pdev client device
 */
static void uart_client_unregister_impl(struct device* pdev)
{
    struct uart_bus_client* cli;
    struct uart_bus_host* host;
    int prev;
    int idx;

    cli = uart_client_from_device(pdev);
    if (!cli)
        return;

    host = cli->host;

    /* 多 client 共享同一 UART: 仅最后一个 unregister 时 hw_close */
    if (host)
    {
        prev = COMPAT_ATOMIC_FETCH_SUB(&host->ref_count, 1, COMPAT_MO_SEQ_CST);
        if (prev == 1 && host->hal_host.hw_inited)
            COMPAT_IGNORE_RESULT(hal_uart_dev_hw_close(&host->hal_host));
    }

    idx = (int)(cli - s_uart_clients);
    COMPAT_MEM_SET(cli, 0, sizeof(*cli));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_client_pool_ctrl, idx));
}

int uart_bus_client_register(struct device* pdev)
{
    return uart_client_register_impl(pdev, NULL, NULL);
}

void uart_bus_client_unregister(struct device* pdev) { uart_client_unregister_impl(pdev); }

/* -------------------------------------------------------------------------- */
/*I/O API (VFS 层调用)*/
/* -------------------------------------------------------------------------- */
int uart_bus_open(struct device* pdev)
{
    struct uart_bus_client* cli = uart_client_from_device(pdev);
    if (!cli || !cli->host)
        return MINI_ERR_NODEV;
    return MINI_OK; /* ref_count 在 client_register/unregister 维护 */
}

int uart_bus_close(struct device* pdev)
{
    struct uart_bus_client* cli = uart_client_from_device(pdev);
    if (!cli || !cli->host)
        return MINI_ERR_NODEV;
    return MINI_OK; /* ref_count 在 client_register/unregister 维护 */
}

int uart_bus_write(struct device* pdev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    struct uart_bus_client* cli = uart_client_from_device(pdev);
    struct hal_uart_dev hal_dev;
    if (!cli || !cli->host || !data || len == 0)
        return MINI_ERR_INVAL;
    hal_dev.ctlr = &cli->host->hal_host;
    return hal_uart_write(&hal_dev, data, len, timeout_ms);
}

int uart_bus_read(struct device* pdev, uint8_t* data, size_t len, uint32_t timeout_ms)
{
    struct uart_bus_client* cli = uart_client_from_device(pdev);
    struct hal_uart_dev hal_dev;
    if (!cli || !cli->host || !data || len == 0)
        return MINI_ERR_INVAL;
    hal_dev.ctlr = &cli->host->hal_host;
    return hal_uart_read(&hal_dev, data, len, timeout_ms);
}

int uart_bus_transfer(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t tx_len,
                      size_t rx_len, uint32_t timeout_ms)
{
    int ret = MINI_OK;

    if (!pdev || (!tx && !rx))
        return MINI_ERR_INVAL;

    if (tx && tx_len > 0)
    {
        ret = uart_bus_write(pdev, tx, tx_len, timeout_ms);
        if (ret != MINI_OK)
            return ret;
    }
    if (rx && rx_len > 0)
        return uart_bus_read(pdev, rx, rx_len, timeout_ms);
    return ret;
}
