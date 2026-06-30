/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * UART BUS 实现 — UART 总线子系统 bus 层
 *
 * 静态池: s_uart_hosts[HOST_MAX] (含 hal_uart_dev, ref_count) + s_uart_clients[CLIENT_MAX]
 * 数据流: VFS → uart_bus_open/close/read/write → uart_client_from_device → hal_uart_*
 *
 * HAL 直接调用 (无 vtable): host_init→hal_uart_dev_init, register→hw_open,
 *   unregister→hw_close, write/read→hal_uart_write/read
 *
 * controller_ops 表注册到 bus_controller_bind_full
 * 引用计数: register/unregister 改 ref_count (open/close 只 IO gate); deinit >0 拒绝销毁
 *
 * 平台中立: 本文件不引用任何厂商 SDK, 所有硬件细节由 HAL 实现 (hal_uart_*.c) 承载。
 * bus 层仅持有 hal_uart_dev (嵌入 host), 透传 hal_uart_config (VFS 从 DTSI 硬件直投填充)。
 *@=========================================================================================================================*/
#define UART_BUS_IMPL
#include "uart_bus.h"
#include "bus.h"
#include "device.h"
#include "driver.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "system_log.h"

#define UART_BUS_HOST_MAX   4
#define UART_BUS_CLIENT_MAX 8

struct uart_bus_host {
    struct device*             dev;
    struct hal_uart_dev        hal_dev;   /* 嵌入, 非 vtable 指针 */
    atomic_int                 ref_count; /* atomic, 无锁计数 */
    uint8_t                    in_use;
};

struct uart_bus_client {
    struct device*        dev;
    struct uart_bus_host* host;
    uint8_t               in_use;
};

static struct uart_bus_host   s_uart_hosts[UART_BUS_HOST_MAX];
static struct uart_bus_client s_uart_clients[UART_BUS_CLIENT_MAX];
static const char* const      kTag = "uart_bus";

/*===========================================================================================================================================================*/
                                                              /*Host Pool*/
/*===========================================================================================================================================================*/
/**
 * @brief 从 host 池中申请一个空闲槽位
 * @return 成功返回槽位索引, 池满返回 -1
 */
static int uart_host_pool_claim(void)
{
    for (int i = 0; i < UART_BUS_HOST_MAX; i++)
    {
        if (!s_uart_hosts[i].in_use)
        {
            s_uart_hosts[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

/**
 * @brief 释放 host 池槽位 (清零描述符)
 * @param idx 待释放的槽位索引
 */
static void uart_host_pool_release(int idx)
{
    if (idx >= 0 && idx < UART_BUS_HOST_MAX)
        __builtin_memset(&s_uart_hosts[idx], 0, sizeof(s_uart_hosts[idx]));
}

/**
 * @brief 通过 device 指针查找对应的 uart_bus_host
 * @param dev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct uart_bus_host* uart_host_from_device(struct device* dev)
{
    for (int i = 0; i < UART_BUS_HOST_MAX; i++)
    {
        if (s_uart_hosts[i].in_use && s_uart_hosts[i].dev == dev)
            return &s_uart_hosts[i];
    }
    return NULL;
}

/*===========================================================================================================================================================*/
                                                              /*Client Pool*/
/*===========================================================================================================================================================*/
/**
 * @brief 从 client 池中申请一个空闲槽位
 * @return 成功返回槽位索引, 池满返回 -1
 */
static int uart_client_pool_claim(void)
{
    for (int i = 0; i < UART_BUS_CLIENT_MAX; i++)
    {
        if (!s_uart_clients[i].in_use)
        {
            s_uart_clients[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

/**
 * @brief 释放 client 池槽位 (清零描述符)
 * @param idx 待释放的槽位索引
 */
static void uart_client_pool_release(int idx)
{
    if (idx >= 0 && idx < UART_BUS_CLIENT_MAX)
        __builtin_memset(&s_uart_clients[idx], 0, sizeof(s_uart_clients[idx]));
}

/**
 * @brief 通过 device 指针查找对应的 uart_bus_client
 * @param dev client device 指针
 * @return 找到返回 client 指针, 未找到返回 NULL
 */
static struct uart_bus_client* uart_client_from_device(struct device* dev)
{
    for (int i = 0; i < UART_BUS_CLIENT_MAX; i++)
    {
        if (s_uart_clients[i].in_use && s_uart_clients[i].dev == dev)
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

    idx = uart_host_pool_claim();
    if (idx < 0)
        return VFS_ERR_NOMEM;

    host = &s_uart_hosts[idx];
    host->dev = dev;
    atomic_init(&host->ref_count, 0);

    /* HAL dev 嵌入 host, 直接传对象指针, 零翻译透传 config */
    hal_uart_dev_init(&host->hal_dev, idx, host_cfg);

    ret = bus_controller_bind_full(dev, BUS_TYPE_UART,
                                    &s_uart_controller_ops, host);
    if (ret != VFS_OK)
    {
        uart_host_pool_release(idx);
        return ret;
    }

    SYS_LOGI(kTag, "host init OK: %s uart=%lu baud=%lu",
             device_get_name(dev), (unsigned long)host_cfg->uart,
             (unsigned long)host_cfg->baud_rate);
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

    if (!dev)
        return VFS_ERR_INVAL;

    host = uart_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    /* atomic 检查, BUSY 时不销毁 (对齐 SPI) */
    if (atomic_load(&host->ref_count) > 0)
        return VFS_ERR_BUSY;

    bus_controller_unbind(dev);

    /* HAL close: 关闭 UART (如果已 open) */
    if (host->hal_dev.hw_inited)
        COMPAT_IGNORE_RESULT(hal_uart_dev_hw_close(&host->hal_dev));

    uart_host_pool_release((int)(host - s_uart_hosts));
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
 * @brief host 初始化公开接口 (转发到 uart_host_init_impl)
 * @param dev controller device (host)
 * @param cfg host 配置 (struct hal_uart_config*)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int uart_bus_host_init(struct device* dev, const struct hal_uart_config* cfg)
{
    return uart_host_init_impl(dev, cfg);
}

/**
 * @brief host 反初始化公开接口 (转发到 uart_host_deinit_impl)
 * @param dev controller device (host)
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
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

    idx = uart_client_pool_claim();
    if (idx < 0)
        return VFS_ERR_NOMEM;

    cli = &s_uart_clients[idx];
    cli->dev  = dev;
    cli->host = host;

    /* HAL hw_open: 配置 UART + 引脚 (DTSI 硬件直投值) */
    ret = hal_uart_dev_hw_open(&host->hal_dev);
    if (ret != VFS_OK)
    {
        uart_client_pool_release(idx);
        return ret;
    }

    atomic_fetch_add(&host->ref_count, 1);  /* 对齐 spi: client_register +1 */

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

    cli = uart_client_from_device(dev);
    if (!cli)
        return;

    host = cli->host;

    /* 显式关闭 UART (对齐 SPI) */
    if (host && host->hal_dev.hw_inited)
        COMPAT_IGNORE_RESULT(hal_uart_dev_hw_close(&host->hal_dev));

    if (host)
        atomic_fetch_sub(&host->ref_count, 1);  /* 对齐 spi: client_unregister -1 */

    uart_client_pool_release((int)(cli - s_uart_clients));
}

/**
 * @brief client 注册公开接口 (转发到 uart_client_register_impl, 无 cfg)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int uart_bus_client_register(struct device* dev)
{
    return uart_client_register_impl(dev, NULL, NULL);
}

/**
 * @brief client 注销公开接口 (转发到 uart_client_unregister_impl)
 * @param dev client device
 */
void uart_bus_client_unregister(struct device* dev)
{
    uart_client_unregister_impl(dev);
}

/*===========================================================================================================================================================*/
                                                              /*I/O API (VFS 层调用)*/
/*===========================================================================================================================================================*/
/**
 * @brief 打开 UART client (仅 IO gate, ref_count 在 register/unregister 维护)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV
 */
int uart_bus_open(struct device* dev)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    if (!cli || !cli->host)
        return VFS_ERR_NODEV;
    return VFS_OK;  /* ref_count 在 client_register/unregister 维护 */
}

/**
 * @brief 关闭 UART client (仅 IO gate, 不改 ref_count)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV
 */
int uart_bus_close(struct device* dev)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    if (!cli || !cli->host)
        return VFS_ERR_NODEV;
    return VFS_OK;  /* ref_count 在 client_register/unregister 维护 */
}

/**
 * @brief UART 写数据 (调用 hal_uart_write)
 * @param dev client device
 * @param data 待写入数据
 * @param len 数据长度
 * @param timeout_ms 超时 (毫秒, 当前实现未使用)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL 或 HAL 错误码
 */
int uart_bus_write(struct device* dev,
                    const uint8_t* data, size_t len,
                    uint32_t timeout_ms)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!cli || !cli->host || !data || len == 0)
        return VFS_ERR_INVAL;
    return hal_uart_write(&cli->host->hal_dev, data, len);
}

/**
 * @brief UART 读数据 (调用 hal_uart_read)
 * @param dev client device
 * @param data 读取缓冲区
 * @param len 读取长度
 * @param timeout_ms 超时 (毫秒, 当前实现未使用)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL 或 HAL 错误码
 */
int uart_bus_read(struct device* dev,
                   uint8_t* data, size_t len,
                   uint32_t timeout_ms)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!cli || !cli->host || !data || len == 0)
        return VFS_ERR_INVAL;
    return hal_uart_read(&cli->host->hal_dev, data, len);
}
