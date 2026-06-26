/*@=========================================================================================================================*
 * UART VFS 实现 — UART 总线子系统 VFS 层
 *
 * 两层结构:
 *   - Host VFS:       DTS 解析 + uart_bus_host_init (controller driver)
 *   - Client VFS:     uart_bus_client_register + fops 挂载 (bus client driver)
 *
 * 生命周期管理 (dev_lifecycle):
 *   - open:  dev_lc_open_begin → first? bus_open : noop → dev_lc_open_end
 *   - close: dev_lc_close_begin → last? bus_close : noop → dev_lc_close_end
 *   - io:    dev_lc_io_begin → bus_read/write → dev_lc_io_end
 *   - remove: dev_lc_remove_start → ops_unregister → remove_drain → bus_unregister
 *
 * I/O 路径:
 *   - read/write: 走 uart_bus_read/write
 *   - ioctl(UART_CMD_TRANSFER): 先 write(tx) 再 read(rx) (半双工模拟全双工)
 *
 * Host remove 安全:
 *   - uart_bus_host_deinit 返回 VFS_ERR_BUSY 时, remove 返回 BUSY 拒绝销毁
 *   - 防止 client 仍 open 时 host 被销毁 (UAF)
 *
 * @see bus/uart/uart_bus.h  bus 层接口
 *@=========================================================================================================================*/
#define UART_VFS_IMPL
#include "uart_vfs.h"
#include "uart_bus.h"
#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "hal_gpio.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#include <string.h>

/*===========================================================================================================================================================*/
                                                              /* Host VFS */
/*===========================================================================================================================================================*/
#define UART_HOST_VFS_COUNT 4

struct uart_host_vfs {
    struct uart_bus_host_config  cfg;
    int                          pool_idx;
};

static struct uart_host_vfs s_host_pool[UART_HOST_VFS_COUNT] COMPAT_ALIGNED(4);
static uint8_t              s_host_used[UART_HOST_VFS_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t          s_host_pool_ctrl COMPAT_ALIGNED(4);
static const char* const    kHostTag = "uart_host_vfs";

pre_execution(150)
static void uart_host_vfs_pool_init(void)
{
    osal_pool_init(&s_host_pool_ctrl, s_host_used, UART_HOST_VFS_COUNT);
}

static int uart_host_vfs_parse_dts(struct device* dev,
                                   struct uart_bus_host_config* cfg)
{
    hal_pin_t tx = {0};
    hal_pin_t rx = {0};
    int       host_id = 0;
    int       hw_instance = 0;
    int       data_bits = 0;
    int       stop_bits = 0;
    int       parity = 0;

    if (device_get_prop_int(dev, "host-id", &host_id) != VFS_OK ||
        hal_pin_probe(dev, "tx-port", "tx-pin", &tx) != VFS_OK ||
        hal_pin_probe(dev, "rx-port", "rx-pin", &rx) != VFS_OK ||
        device_get_prop_int(dev, "uart-trans-baund", (int*)&cfg->baud_rate) != VFS_OK ||
        device_get_prop_int(dev, "data-bit", &data_bits) != VFS_OK ||
        device_get_prop_int(dev, "stop-bit", &stop_bits) != VFS_OK ||
        device_get_prop_int(dev, "parity", &parity) != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "hw-instance", &hw_instance));
    if (hw_instance <= 0)
        hw_instance = host_id;

    cfg->data_bits = (uart_bus_data_bits_t)data_bits;
    cfg->stop_bits = (uart_bus_stop_bits_t)stop_bits;
    cfg->parity    = (uart_bus_parity_t)parity;
    cfg->tx_pin    = HAL_PIN_NUM(tx) | (HAL_PIN_PORT(tx) << 16);
    cfg->rx_pin    = HAL_PIN_NUM(rx) | (HAL_PIN_PORT(rx) << 16);
    cfg->uart_host = hw_instance;

    return VFS_OK;
}

static int uart_host_vfs_probe(struct device* dev)
{
    struct uart_host_vfs* priv;
    int                   pool_idx;
    int                   ret;

    if (!dev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_host_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_host_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (uart_host_vfs_parse_dts(dev, &priv->cfg) != VFS_OK)
    {
        SYS_LOGE(kHostTag, "dts parse failed: %s", device_get_name(dev));
        ret = VFS_ERR_INVAL;
        goto err_pool;
    }

    ret = uart_bus_host_init(dev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_pool;

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_bus;
    }

    SYS_LOGI(kHostTag, "probe OK: %s uart_host=%d baud=%lu",
             device_get_name(dev), priv->cfg.uart_host,
             (unsigned long)priv->cfg.baud_rate);
    return VFS_OK;

err_bus:
    uart_bus_host_deinit(dev);
err_pool:
    osal_pool_release(&s_host_pool_ctrl, pool_idx);
    return ret;
}

static int uart_host_vfs_remove(struct device* dev)
{
    struct uart_host_vfs* priv;
    int                   pool_idx;

    if (!dev)
        return VFS_ERR_INVAL;

    priv = (struct uart_host_vfs*)device_get_priv(dev);
    if (!priv)
        return VFS_ERR_NODEV;

    /* BUSY 时不销毁 (对齐 SPI, ref_count > 0 时 host_deinit 返回 VFS_ERR_BUSY) */
    if (uart_bus_host_deinit(dev) != VFS_OK)
        return VFS_ERR_BUSY;

    pool_idx = priv->pool_idx;
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_host_pool_ctrl, pool_idx);

    return VFS_OK;
}

/*===========================================================================================================================================================*/
                                                              /* Client VFS */
/*===========================================================================================================================================================*/
#define UART_VFS_COUNT 2

struct uart_vfs_client {
    struct file_operations ops;
    struct osal_mutex*     io_mutex;
    uint8_t                mutex_storage[OSAL_MUTEX_STORAGE_SIZE];
    int                    pool_idx;
};

static struct uart_vfs_client s_uart_vfs_pool[UART_VFS_COUNT];
static uint8_t                s_uart_vfs_used[UART_VFS_COUNT];
static osal_pool_t            s_uart_vfs_pool_ctrl;
static const char* const      kTag = "uart_vfs";

pre_execution(160)
static void uart_vfs_pool_init(void)
{
    osal_pool_init(&s_uart_vfs_pool_ctrl, s_uart_vfs_used, UART_VFS_COUNT);
}

static struct uart_vfs_client* uart_vfs_from_ops(const struct file_operations* ops)
{
    return container_of(ops, struct uart_vfs_client, ops);
}

static int uart_vfs_open(struct device* dev, void* arg)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle*   lc;
    int                     first;

    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = uart_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    if (first == 1)
    {
        if (uart_bus_open(dev) != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return VFS_ERR_IO;
        }
    }

    dev_lc_open_end(lc);
    return VFS_OK;
}

static int uart_vfs_close(struct device* dev)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle*   lc;
    int                     last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = uart_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last)
        COMPAT_IGNORE_RESULT(uart_bus_close(dev));

    dev_lc_close_end(lc);
    return VFS_OK;
}

static int uart_vfs_write(struct device* dev, const void* buf, size_t len,
                           uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int                   ret;

    if (!dev || !dev->ops || !buf || len == 0)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    ret = uart_bus_write(dev, (const uint8_t*)buf, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static int uart_vfs_read(struct device* dev, void* buf, size_t len,
                          uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int                   ret;

    if (!dev || !dev->ops || !buf || len == 0)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    ret = uart_bus_read(dev, (uint8_t*)buf, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static int uart_vfs_ioctl(struct device* dev, int cmd, void* arg,
                           size_t arg_len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int                   ret;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    switch (cmd)
    {
    case UART_CMD_TRANSFER:
    {
        struct uart_transfer_arg* t = (struct uart_transfer_arg*)arg;
        if (!t || arg_len != sizeof(*t) || !t->tx || !t->rx)
        {
            ret = VFS_ERR_INVAL;
            break;
        }
        ret = uart_bus_write(dev, t->tx, t->tx_len, timeout_ms);
        if (ret == VFS_OK && t->rx_len > 0)
            ret = uart_bus_read(dev, t->rx, t->rx_len, timeout_ms);
        break;
    }
    default:
        ret = VFS_ERR_INVAL;
        break;
    }

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations uart_vfs_fops = {
    .open   = uart_vfs_open,
    .close  = uart_vfs_close,
    .write  = uart_vfs_write,
    .read   = uart_vfs_read,
    .ioctl  = uart_vfs_ioctl,
};

int uart_vfs_probe(struct device* dev)
{
    struct uart_vfs_client*     priv;
    struct uart_bus_client_config cfg;
    int                         pool_idx;
    int                         ret;

    if (!dev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_uart_vfs_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_uart_vfs_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (osal_mutex_create_static(&priv->io_mutex, priv->mutex_storage,
                                  sizeof(priv->mutex_storage)) != 0)
    {
        osal_pool_release(&s_uart_vfs_pool_ctrl, pool_idx);
        return VFS_ERR_NOMEM;
    }

    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.host_id = 0;
    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "host-id", &cfg.host_id));

    ret = uart_bus_client_register(dev, &cfg);
    if (ret != VFS_OK)
        goto err_mutex;

    device_lc_bind(dev, priv->io_mutex);

    priv->ops = uart_vfs_fops;
    dev->ops  = &priv->ops;

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        uart_bus_client_unregister(dev);
        goto err_mutex;
    }

    SYS_LOGI(kTag, "probe OK: %s", device_get_name(dev));
    return VFS_OK;

err_mutex:
    dev->ops = NULL;                   /* 切断 fops, 防 UAF */
    dev_lc_reset(device_lc(dev));       /* 切断 io_lock 绑定 */
    osal_mutex_destroy(priv->io_mutex);
    osal_pool_release(&s_uart_vfs_pool_ctrl, pool_idx);
    return ret;
}

int uart_vfs_remove(struct device* dev)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle*   lc;
    int                     pool_idx;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = uart_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
        return VFS_ERR_IO;

    uart_bus_client_unregister(dev);
    osal_mutex_destroy(priv->io_mutex);
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_uart_vfs_pool_ctrl, pool_idx);

    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(uart_host_vfs, "stm32,uart1", uart_host_vfs_probe, uart_host_vfs_remove)
DRIVER_REGISTER(uart_host_vfs_ch32, "ch32,uart1", uart_host_vfs_probe, uart_host_vfs_remove)
DRIVER_REGISTER(uart_host_vfs_esp32, "esp32,uart1", uart_host_vfs_probe, uart_host_vfs_remove)
DRIVER_REGISTER(uart_vfs, "stm32,uart-client", uart_vfs_probe, uart_vfs_remove)
