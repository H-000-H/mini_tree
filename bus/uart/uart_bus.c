/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * UART BUS 实现 — UART 总线子系统 bus 层
 *
 * 静态池: s_uart_hosts[HOST_MAX] (含 hal_uart_bus_host, ref_count) + s_uart_clients[CLIENT_MAX]
 * 数据流: VFS → uart_bus_open/close/read/write/transfer → uart_client_from_device → hal_uart_*
 *
 * HAL 直接调用 (无 vtable): host_init→hal_uart_dev_init, register→hw_open,
 *   unregister→hw_close, write/read/transfer→hal_uart_write/read
 *
 * controller_ops 表注册到 bus_controller_bind_full
 * 引用计数: register/unregister 改 ref_count (open/close 只 IO gate); deinit >0 拒绝销毁
 *
 * 平台中立: 本文件不引用任何厂商 SDK, 所有硬件细节由 HAL 实现 (hal_uart_*.c) 承载。
 * bus 层仅持有 hal_uart_bus_host (嵌入 host), 透传 hal_uart_config (VFS 从 DTSI 硬件直投填充)。
 *@=========================================================================================================================*/
#define UART_BUS_IMPL
#include "uart_bus.h"
#include "bus.h"
#include "device.h"
#include "driver.h"
#include "status.h"
#include "compiler_compat.h"
#include "system_log.h"
#include "osal.h"

#define UART_BUS_HOST_MAX   6  /* 对齐 DTS host-max (USART1..6 / UART4/5) */
#define UART_BUS_CLIENT_MAX 8

/** @brief UART host 运行时描述符 (静态池, HAL 嵌入 + atomic ref_count) */
struct uart_bus_host {
    struct device*             dev;       /**< 关联设备 */
    struct hal_uart_bus_host   hal_host;  /**< 嵌入 HAL host (非 vtable 指针) */
    COMPAT_ATOMIC_INT           ref_count; /**< atomic 无锁计数 */
};

/** @brief UART client 运行时描述符 (静态池, 按 client_id 索引) */
struct uart_bus_client {
    struct device*        dev;    /**< 关联设备 */
    struct uart_bus_host* host;   /**< 所属 host */
};

static struct uart_bus_host   s_uart_hosts[UART_BUS_HOST_MAX];
static uint8_t                s_uart_host_used[UART_BUS_HOST_MAX];
static osal_pool_t            s_uart_host_pool_ctrl;
static struct uart_bus_client s_uart_clients[UART_BUS_CLIENT_MAX];
static uint8_t                s_uart_client_used[UART_BUS_CLIENT_MAX];
static osal_pool_t            s_uart_client_pool_ctrl;
static const char* const      kTag = "uart_bus";

/**
 * @brief UART Host/Client 池启动初始化
 */
pre_execution(150)
static void uart_bus_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_uart_host_pool_ctrl, s_uart_host_used, UART_BUS_HOST_MAX));
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_uart_client_pool_ctrl, s_uart_client_used, UART_BUS_CLIENT_MAX));
}

/*===========================================================================================================================================================*/
                                                              /*Host Pool*/
/*===========================================================================================================================================================*/
/**
 * @brief 通过 device 指针查找对应的 uart_bus_host
 * @param dev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct uart_bus_host* uart_host_from_device(struct device* dev)
{
    for (int i = 0; i < UART_BUS_HOST_MAX; i++)
    {
        if (osal_pool_is_used(&s_uart_host_pool_ctrl, i) && s_uart_hosts[i].dev == dev)
            return &s_uart_hosts[i];
    }
    return NULL;
}

/*===========================================================================================================================================================*/
                                                              /*Client Pool*/
/*===========================================================================================================================================================*/
/**
 * @brief 通过 device 指针查找对应的 uart_bus_client
 * @param dev client device 指针
 * @return 找到返回 client 指针, 未找到返回 NULL
 */
static struct uart_bus_client* uart_client_from_device(struct device* dev)
{
    for (int i = 0; i < UART_BUS_CLIENT_MAX; i++)
    {
        if (osal_pool_is_used(&s_uart_client_pool_ctrl, i) && s_uart_clients[i].dev == dev)
            return &s_uart_clients[i];
    }
    return NULL;
}

/*===========================================================================================================================================================*/
                                                              /*controller_ops (host 级操作)*/
/*===========================================================================================================================================================*/
static int uart_host_init_impl(struct device* dev, const void* cfg);
static int uart_host_deinit_impl(struct device* dev);
static int uart_host_role_impl(struct device* dev);
static int uart_client_register_impl(struct device* dev, const void* cfg, void** out);
static void uart_client_unregister_impl(struct device* dev);

static const struct bus_controller_ops s_uart_controller_ops = {
    .init              = uart_host_init_impl,
    .deinit            = uart_host_deinit_impl,
    .role              = uart_host_role_impl,
    .client_register   = uart_client_register_impl,
    .client_unregister = uart_client_unregister_impl,
};

/*===========================================================================================================================================================*/
                                                              /*Host API*/
/*===========================================================================================================================================================*/
/**
 * @brief host 初始化实现 (controller_ops.init): 分配 host 池, 调用 hal_uart_dev_init, 绑定 controller
 * @param dev controller device (host)
 * @param cfg host 配置 (struct hal_uart_config*, VFS 从 DTSI 硬件直投填充, bus 零翻译透传)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
static int uart_host_init_impl(struct device* dev, const void* cfg)
{
    const struct hal_uart_config* host_cfg = (const struct hal_uart_config*)cfg;
    struct uart_bus_host*     host;
    int                       idx;
    int                       ret;

    if (!dev || !host_cfg)
        return VFS_ERR_INVAL;

    if (uart_host_from_device(dev))
        return VFS_OK;

    idx = osal_pool_claim(&s_uart_host_pool_ctrl);
    if (idx < 0)
        return VFS_ERR_NOMEM;

    host = &s_uart_hosts[idx];
    COMPAT_MEM_SET(host, 0, sizeof(*host));
    host->dev = dev;
    COMPAT_ATOMIC_RUNTIME_INIT(&host->ref_count, 0);

    /* HAL dev 嵌入 host, 直接传对象指针, 零翻译透传 config */
    ret = hal_uart_dev_init(&host->hal_host, host_cfg);
    if (ret != VFS_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_host_pool_ctrl, idx));
        return ret;
    }

    ret = bus_controller_bind_full(dev, BUS_TYPE_UART, &s_uart_controller_ops, host);
    if (ret != VFS_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_host_pool_ctrl, idx));
        return ret;
    }

    SYS_LOGI(kTag, "host init OK: %s uart=%lu baud=%lu", device_get_name(dev), (unsigned long)host_cfg->uart, (unsigned long)host_cfg->baud_rate);
    return VFS_OK;
}

/**
 * @brief host 反初始化实现 (controller_ops.deinit): 检查 ref_count, 解绑 controller, 释放池槽位
 * @param dev controller device (host)
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
 */
static int uart_host_deinit_impl(struct device* dev)
{
    struct uart_bus_host* host;
    int                   idx;

    if (!dev)
        return VFS_ERR_INVAL;

    host = uart_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    /* atomic 检查, BUSY 时不销毁 (对齐 SPI) */
    if (COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST) > 0)
        return VFS_ERR_BUSY;

    bus_controller_unbind(dev);

    /* HAL close: 关闭 UART (如果已 open) */
    if (host->hal_host.hw_inited)
        COMPAT_IGNORE_RESULT(hal_uart_dev_hw_close(&host->hal_host));

    idx = (int)(host - s_uart_hosts);
    COMPAT_MEM_SET(host, 0, sizeof(*host));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_host_pool_ctrl, idx));
    return VFS_OK;
}

/**
 * @brief 查询 host 角色 (controller_ops.role, UART 无 master/slave 之分, 固定返回 0)
 * @param dev controller device (host)
 * @return 固定返回 0
 */
static int uart_host_role_impl(struct device* dev)
{
    COMPAT_IGNORE_RESULT(dev);
    return 0;  /* UART 无 master/slave 之分 */
}

/**
 * @brief 初始化 UART host 并绑定总线控制器
 * @param dev host device 指针
 * @param cfg host 配置 (struct hal_uart_config*)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_bus_host_init(struct device* dev, const struct hal_uart_config* cfg)
{
    return uart_host_init_impl(dev, cfg);
}

/**
 * @brief 反初始化 UART host 并释放对象池槽位
 * @param dev host device 指针
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回负数错误码
 */
int uart_bus_host_deinit(struct device* dev)
{
    return uart_host_deinit_impl(dev);
}

/*===========================================================================================================================================================*/
                                                              /*Client API*/
/*===========================================================================================================================================================*/
/**
 * @brief client 注册实现 (controller_ops.client_register): 分配 client, 绑定 host, 调用 hal_uart_dev_hw_open
 * @param dev client device
 * @param cfg client 配置 (UART 无 per-client 配置, 此参数忽略)
 * @param out 输出 client 私有上下文 (可 NULL)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
static int uart_client_register_impl(struct device* dev, const void* cfg, void** out)
{
    struct bus_controller*   ctlr;
    struct uart_bus_host*    host;
    struct uart_bus_client*  cli;
    int                       idx;
    int                       ret;

    COMPAT_IGNORE_RESULT(cfg);
    if (!dev)
        return VFS_ERR_INVAL;

    if (uart_client_from_device(dev))
        return VFS_OK;

    /* 通过 parent 查找 host (dev = client → device_get_parent → host device → s_controllers → hw_ctx) */
    if (bus_controller_of(dev, &ctlr) != VFS_OK)
        return VFS_ERR_NODEV;
    if (ctlr->type != BUS_TYPE_UART)
        return VFS_ERR_NODEV;
    host = (struct uart_bus_host*)ctlr->hw_ctx;
    if (!host)
        return VFS_ERR_IO;

    idx = osal_pool_claim(&s_uart_client_pool_ctrl);
    if (idx < 0)
        return VFS_ERR_NOMEM;

    cli = &s_uart_clients[idx];
    COMPAT_MEM_SET(cli, 0, sizeof(*cli));
    cli->dev  = dev;
    cli->host = host;

    /* HAL hw_open: 配置 UART + 引脚 (DTSI 硬件直投值) */
    ret = hal_uart_dev_hw_open(&host->hal_host);
    if (ret != VFS_OK)
    {
        COMPAT_MEM_SET(cli, 0, sizeof(*cli));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_client_pool_ctrl, idx));
        return ret;
    }

    (void)COMPAT_ATOMIC_FETCH_ADD(&host->ref_count, 1, COMPAT_MO_SEQ_CST);  /* 对齐 spi: client_register +1 */

    if (out)
        *out = cli;
    return VFS_OK;
}

/**
 * @brief client 注销实现 (controller_ops.client_unregister): 关闭 UART, ref_count -1, 释放池槽位
 * @param dev client device
 */
static void uart_client_unregister_impl(struct device* dev)
{
    struct uart_bus_client* cli;
    struct uart_bus_host*   host;
    int                     prev;
    int                     idx;

    cli = uart_client_from_device(dev);
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

/**
 * @brief 注册 UART client 并打开硬件 (ref_count +1)
 * @param dev client device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_bus_client_register(struct device* dev)
{
    return uart_client_register_impl(dev, NULL, NULL);
}

/**
 * @brief 注销 UART client (末 client 时 hw_close, ref_count -1)
 * @param dev client device 指针
 */
void uart_bus_client_unregister(struct device* dev)
{
    uart_client_unregister_impl(dev);
}

/*===========================================================================================================================================================*/
                                                              /*I/O API (VFS 层调用)*/
/*===========================================================================================================================================================*/
/**
 * @brief 打开 UART client (IO 门控, ref_count 在 register/unregister 维护)
 * @param dev client device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_bus_open(struct device* dev)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    if (!cli || !cli->host)
        return VFS_ERR_NODEV;
    return VFS_OK;  /* ref_count 在 client_register/unregister 维护 */
}

/**
 * @brief 关闭 UART client (IO 门控, ref_count 在 register/unregister 维护)
 * @param dev client device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_bus_close(struct device* dev)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    if (!cli || !cli->host)
        return VFS_ERR_NODEV;
    return VFS_OK;  /* ref_count 在 client_register/unregister 维护 */
}

/**
 * @brief UART 写
 * @param dev client device 指针
 * @param data 发送缓冲
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_bus_write(struct device* dev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    struct hal_uart_dev     hal_dev;
    if (!cli || !cli->host || !data || len == 0)
        return VFS_ERR_INVAL;
    hal_dev.ctlr = &cli->host->hal_host;
    return hal_uart_write(&hal_dev, data, len, timeout_ms);
}

/**
 * @brief UART 读
 * @param dev client device 指针
 * @param data 接收缓冲
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回已读字节数或 VFS_OK, 失败返回负数错误码
 */
int uart_bus_read(struct device* dev, uint8_t* data, size_t len, uint32_t timeout_ms)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    struct hal_uart_dev     hal_dev;
    if (!cli || !cli->host || !data || len == 0)
        return VFS_ERR_INVAL;
    hal_dev.ctlr = &cli->host->hal_host;
    return hal_uart_read(&hal_dev, data, len, timeout_ms);
}

/**
 * @brief UART 半双工传输 (先写后读)
 * @param dev client device 指针
 * @param tx 发送缓冲 (可为 NULL)
 * @param rx 接收缓冲 (可为 NULL)
 * @param tx_len 发送字节数
 * @param rx_len 接收字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_bus_transfer(struct device* dev, const uint8_t* tx, uint8_t* rx, size_t tx_len, size_t rx_len, uint32_t timeout_ms)
{
    int ret = VFS_OK;

    if (!dev || (!tx && !rx))
        return VFS_ERR_INVAL;

    if (tx && tx_len > 0)
    {
        ret = uart_bus_write(dev, tx, tx_len, timeout_ms);
        if (ret != VFS_OK)
            return ret;
    }
    if (rx && rx_len > 0)
        return uart_bus_read(dev, rx, rx_len, timeout_ms);
    return ret;
}
