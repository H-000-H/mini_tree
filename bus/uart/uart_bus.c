/*@=========================================================================================================================*
 * UART BUS 实现 — UART 总线子系统 bus 层
 *
 * 静态表设计:
 *   - s_uart_hosts[UART_BUS_HOST_MAX]:   host 描述符池 (含 hal_dev, vtable, ref_count)
 *   - s_uart_clients[UART_BUS_CLIENT_MAX]: client 描述符池 (含 host 反向指针)
 *   - in_use 位图标记使用状态
 *
 * 数据流:
 *   VFS fops → uart_bus_open/close/read/write → uart_client_from_device → host->vtable
 *
 * controller_ops 架构:
 *   - s_uart_controller_ops 表注册到 bus_controller_bind_full
 *   - impl 函数实现具体逻辑, wrapper 函数转发 (保持 API 兼容)
 *
 * 引用计数 (atomic_int):
 *   - uart_client_register_impl: atomic_fetch_add (client 注册时 +1, HAL open)
 *   - uart_client_unregister_impl: atomic_fetch_sub (client 注销时 -1, HAL close)
 *   - bus_ops open/close: 只做 IO gate, 不改 ref_count (HAL open 在 client_register)
 *   - host_deinit: atomic_load 检查 > 0 拒绝销毁
 *@=========================================================================================================================*/
#define UART_BUS_IMPL
#include "uart_bus.h"
#include "hal_uart.h"
#include "bus.h"
#include "device.h"
#include "driver.h"
#include "hal_gpio.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "system_log.h"

#include <string.h>

#define UART_BUS_HOST_MAX   4
#define UART_BUS_CLIENT_MAX 8

struct uart_bus_host {
    struct device*             dev;
    struct hal_uart_dev        hal_dev;
    const struct hal_uart_bus* vtable;
    atomic_int                 ref_count;          /* atomic, 无锁计数 */
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
                                                              /* Host Pool */
/*===========================================================================================================================================================*/
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

static void uart_host_pool_release(int idx)
{
    if (idx >= 0 && idx < UART_BUS_HOST_MAX)
        __builtin_memset(&s_uart_hosts[idx], 0, sizeof(s_uart_hosts[idx]));
}

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
                                                              /* Client Pool */
/*===========================================================================================================================================================*/
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

static void uart_client_pool_release(int idx)
{
    if (idx >= 0 && idx < UART_BUS_CLIENT_MAX)
        __builtin_memset(&s_uart_clients[idx], 0, sizeof(s_uart_clients[idx]));
}

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
                                                              /* bus_ops 分派 */
/*===========================================================================================================================================================*/
static int uart_bus_ops_open(void* ctx)
{
    struct uart_bus_client* cli = (struct uart_bus_client*)ctx;
    if (!cli || !cli->host)
        return VFS_ERR_NODEV;
    return VFS_OK;  /* HAL open 在 client_register, bus_ops open 只做 IO gate */
}

static int uart_bus_ops_close(void* ctx)
{
    struct uart_bus_client* cli = (struct uart_bus_client*)ctx;
    if (!cli || !cli->host)
        return VFS_ERR_NODEV;
    return VFS_OK;  /* HAL close 在 client_unregister, bus_ops close 只做 IO gate */
}

static int uart_bus_ops_write(void* ctx, const void* data, size_t len,
                               uint32_t timeout_ms)
{
    struct uart_bus_client* cli = (struct uart_bus_client*)ctx;
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!cli || !cli->host || !data || len == 0)
        return VFS_ERR_INVAL;
    if (cli->host->vtable && cli->host->vtable->write)
        return cli->host->vtable->write(&cli->host->hal_dev,
                                         (const uint8_t*)data, len);
    return VFS_ERR_IO;
}

static int uart_bus_ops_read(void* ctx, void* data, size_t len,
                              uint32_t timeout_ms)
{
    struct uart_bus_client* cli = (struct uart_bus_client*)ctx;
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!cli || !cli->host || !data || len == 0)
        return VFS_ERR_INVAL;
    if (cli->host->vtable && cli->host->vtable->read)
        return cli->host->vtable->read(&cli->host->hal_dev,
                                        (uint8_t*)data, len);
    return VFS_ERR_IO;
}

static const struct bus_ops s_uart_bus_ops = {
    .open   = uart_bus_ops_open,
    .close  = uart_bus_ops_close,
    .write  = uart_bus_ops_write,
    .read   = uart_bus_ops_read,
    .transfer = NULL,
    .ioctl  = NULL,
};

/*===========================================================================================================================================================*/
                                                              /* controller_ops (host 级操作, 对齐 SPI 架构) */
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
                                                              /* Host API */
/*===========================================================================================================================================================*/
/*@=========================================================================================================================*
 * uart_host_init_impl — host 初始化 (controller_ops.init)
 *
 * 从池分配 host, 填充 hal_cfg, 绑定 bus_controller (含 ctlr_ops).
 * 幂等: 若 dev 已有 host, 直接返回 VFS_OK.
 *
 * @param dev  controller device (host)
 * @param cfg  struct uart_bus_host_config
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 *@=========================================================================================================================*/
static int uart_host_init_impl(struct device* dev, const void* cfg)
{
    const struct uart_bus_host_config* host_cfg = (const struct uart_bus_host_config*)cfg;
    struct uart_bus_host*     host;
    struct hal_uart_config_t  hal_cfg;
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
    host->vtable = hal_uart_bus_get();
    atomic_init(&host->ref_count, 0);

    __builtin_memset(&hal_cfg, 0, sizeof(hal_cfg));
    hal_cfg.baud_rate = host_cfg->baud_rate;
    hal_cfg.data_bits = (hal_uart_data_bits_t)host_cfg->data_bits;
    hal_cfg.parity    = (hal_uart_parity_t)host_cfg->parity;
    hal_cfg.stop_bits = (hal_uart_stop_bits_t)host_cfg->stop_bits;
    hal_cfg.tx_io     = HAL_MAKE_PIN((host_cfg->tx_pin >> 16) & 0xFFFF, host_cfg->tx_pin & 0xFFFF);
    hal_cfg.rx_io     = HAL_MAKE_PIN((host_cfg->rx_pin >> 16) & 0xFFFF, host_cfg->rx_pin & 0xFFFF);
    hal_cfg.uart_host = host_cfg->uart_host;

    __builtin_memset(&host->hal_dev, 0, sizeof(host->hal_dev));
    host->hal_dev.cfg       = hal_cfg;
    host->hal_dev.pool_idx  = host_cfg->uart_host;
    host->hal_dev.status    = UART_STATE_UNINIT;

    ret = bus_controller_bind_full(dev, BUS_TYPE_UART, &s_uart_bus_ops,
                                    &s_uart_controller_ops, host);
    if (ret != VFS_OK)
    {
        uart_host_pool_release(idx);
        return ret;
    }

    SYS_LOGI(kTag, "host init OK: %s uart_host=%d baud=%lu",
             device_get_name(dev), host_cfg->uart_host, (unsigned long)host_cfg->baud_rate);
    return VFS_OK;
}

/*@=========================================================================================================================*
 * uart_host_deinit_impl — host 反初始化 (controller_ops.deinit)
 *
 * 检查 ref_count > 0 时返回 VFS_ERR_BUSY 拒绝销毁 (防止 UAF).
 * 解绑 bus_controller, 调用 HAL deinit, 释放 host 池槽位.
 *
 * @param dev  controller device (host)
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
 *@=========================================================================================================================*/
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

    if (host->vtable && host->vtable->deinit)
        COMPAT_IGNORE_RESULT(host->vtable->deinit(&host->hal_dev.cfg));

    uart_host_pool_release((int)(host - s_uart_hosts));
    return VFS_OK;
}

static int uart_host_role_impl(struct device* dev)
{
    COMPAT_IGNORE_RESULT(dev);
    return 0;  /* UART 无 master/slave 之分 */
}

int uart_bus_host_init(struct device* dev, const struct uart_bus_host_config* cfg)
{
    return uart_host_init_impl(dev, cfg);
}

int uart_bus_host_deinit(struct device* dev)
{
    return uart_host_deinit_impl(dev);
}

/*===========================================================================================================================================================*/
                                                              /* Client API */
/*===========================================================================================================================================================*/
/*@=========================================================================================================================*
 * uart_client_register_impl — client 注册 (controller_ops.client_register)
 *
 * 从池分配 client, 绑定到 host, 调用 HAL open.
 * 幂等: 若 dev 已有 client, 直接返回 VFS_OK.
 *
 * @param dev  client device
 * @param cfg  struct uart_bus_client_config (host_id)
 * @param out  输出 client 私有上下文 (可 NULL)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 *@=========================================================================================================================*/
static int uart_client_register_impl(struct device* dev, const void* cfg, void** out)
{
    const struct uart_bus_client_config* client_cfg = (const struct uart_bus_client_config*)cfg;
    struct bus_controller*   ctlr;
    struct uart_bus_host*     host;
    struct uart_bus_client*   cli;
    int                       idx;
    int                       ret;

    if (!dev || !client_cfg)
        return VFS_ERR_INVAL;

    if (uart_client_from_device(dev))
        return VFS_OK;

    /* 通过 parent 查找 host (对齐 spi_bus.c, 修复 dev 直接匹配 host 池的 bug:
     *   s_uart_hosts[i].dev 存的是 host device, client dev 永远匹配不上)
     *   dev = client → device_get_parent → host device → s_controllers → hw_ctx */
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

    if (host->vtable && host->vtable->open)
    {
        ret = host->vtable->open(&host->hal_dev.cfg);
        if (ret != VFS_OK)
        {
            uart_client_pool_release(idx);
            return ret;
        }
    }

    atomic_fetch_add(&host->ref_count, 1);  /* 对齐 spi: client_register +1 */

    if (out)
        *out = cli;
    return VFS_OK;
}

/*@=========================================================================================================================*
 * uart_client_unregister_impl — client 注销 (controller_ops.client_unregister)
 *
 * 显式调用 HAL close 关闭 client (对齐 SPI, 防止资源泄漏).
 * 解绑 bus_client, 释放 client 池槽位.
 *
 * @param dev  client device
 *@=========================================================================================================================*/
static void uart_client_unregister_impl(struct device* dev)
{
    struct uart_bus_client* cli;
    struct uart_bus_host*   host;

    cli = uart_client_from_device(dev);
    if (!cli)
        return;

    host = cli->host;

    /* 显式关闭 client (对齐 SPI) */
    if (host && host->vtable && host->vtable->close)
        COMPAT_IGNORE_RESULT(host->vtable->close(&host->hal_dev.cfg));

    if (host)
        atomic_fetch_sub(&host->ref_count, 1);  /* 对齐 spi: client_unregister -1 */

    uart_client_pool_release((int)(cli - s_uart_clients));
}

int uart_bus_client_register(struct device* dev,
                              const struct uart_bus_client_config* cfg)
{
    return uart_client_register_impl(dev, cfg, NULL);
}

void uart_bus_client_unregister(struct device* dev)
{
    uart_client_unregister_impl(dev);
}

/*===========================================================================================================================================================*/
                                                              /* 兼容旧 API（内部通过 client 查找） */
/*===========================================================================================================================================================*/
int uart_bus_open(struct device* dev)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    if (!cli || !cli->host)
        return VFS_ERR_NODEV;
    return VFS_OK;  /* ref_count 在 client_register/unregister 维护 */
}

int uart_bus_close(struct device* dev)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    if (!cli || !cli->host)
        return VFS_ERR_NODEV;
    return VFS_OK;  /* ref_count 在 client_register/unregister 维护 */
}

int uart_bus_write(struct device* dev,
                    const uint8_t* data, size_t len,
                    uint32_t timeout_ms)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!cli || !cli->host || !data || len == 0)
        return VFS_ERR_INVAL;
    if (cli->host->vtable && cli->host->vtable->write)
        return cli->host->vtable->write(&cli->host->hal_dev, data, len);
    return VFS_ERR_IO;
}

int uart_bus_read(struct device* dev,
                   uint8_t* data, size_t len,
                   uint32_t timeout_ms)
{
    struct uart_bus_client* cli = uart_client_from_device(dev);
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!cli || !cli->host || !data || len == 0)
        return VFS_ERR_INVAL;
    if (cli->host->vtable && cli->host->vtable->read)
        return cli->host->vtable->read(&cli->host->hal_dev, data, len);
    return VFS_ERR_IO;
}
