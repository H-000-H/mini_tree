/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * UART VFS 实现 — UART 总线子系统 VFS 层
 *
 * 两层结构:
 *   - Host VFS:   DTS 解析 + uart_bus_host_init (controller driver)
 *   - Client VFS: uart_bus_client_register + fops 挂载 (bus client driver)
 *
 * 生命周期 (dev_lifecycle): open/close 引用计数, io 互斥, remove drain。
 * I/O: read/write 走 uart_bus_read/write; ioctl(TRANSFER) 先 write(tx) 再 read(rx) (半双工模拟全双工)。
 * Host remove 安全: host_deinit 返回 BUSY 时 remove 拒绝销毁, 防 client 仍 open 时 host UAF。
 *
 * @see bus/uart/uart_bus.h  bus 层接口
 *@=========================================================================================================================*/
#define UART_VFS_IMPL
#include "uart_vfs.h"
#include "uart_bus.h"
#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#include <string.h>

/*===========================================================================================================================================================*/
                                                              /*Host VFS*/
/*===========================================================================================================================================================*/
#define UART_HOST_VFS_COUNT 4

struct uart_host_vfs {
    struct hal_uart_config  cfg;
    int                     pool_idx;
};

static struct uart_host_vfs s_host_pool[UART_HOST_VFS_COUNT] COMPAT_ALIGNED(4);
static uint8_t              s_host_used[UART_HOST_VFS_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t          s_host_pool_ctrl COMPAT_ALIGNED(4);
static const char* const    kHostTag = "uart_host_vfs";

/**
 * @brief UART Host VFS 私有数据池启动初始化
 */
pre_execution(150)
static void uart_host_vfs_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_host_pool_ctrl, s_host_used, UART_HOST_VFS_COUNT));
}

/**
 * @brief 解析 UART Host DTS 属性 (硬件直投值), 填入 hal_uart_config
 * @param dev 设备对象指针
 * @param cfg 输出的 HAL 配置结构
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int uart_host_vfs_parse_dts(struct device* dev,
                                   struct hal_uart_config* cfg)
{
    /* 硬件直投: DTSI 提供厂商宏值, VFS 零翻译填入 hal_uart_config。
     * device_get_prop_int 取 int*, 指针/uint32 字段用 int temp + (uintptr_t) cast。 */
    int uart_base = 0, uart_clk = 0, uart_baud = 0;
    int data_width = 0, parity = 0, stop_bits = 0;
    int tx_port = 0, tx_pin = 0, tx_clk = 0, tx_af = 0;
    int rx_port = 0, rx_pin = 0, rx_clk = 0, rx_af = 0;

    if (device_get_prop_int(dev, "uart-base",     &uart_base)     != VFS_OK ||
        device_get_prop_int(dev, "uart-clk",      &uart_clk)      != VFS_OK ||
        device_get_prop_int(dev, "uart-baud",     &uart_baud)     != VFS_OK ||
        device_get_prop_int(dev, "data-width",    &data_width)    != VFS_OK ||
        device_get_prop_int(dev, "parity",        &parity)        != VFS_OK ||
        device_get_prop_int(dev, "stop-bits",     &stop_bits)     != VFS_OK ||
        device_get_prop_int(dev, "tx-port",       &tx_port)       != VFS_OK ||
        device_get_prop_int(dev, "tx-pin",        &tx_pin)        != VFS_OK ||
        device_get_prop_int(dev, "tx-clk",        &tx_clk)        != VFS_OK ||
        device_get_prop_int(dev, "tx-af",         &tx_af)         != VFS_OK ||
        device_get_prop_int(dev, "rx-port",       &rx_port)       != VFS_OK ||
        device_get_prop_int(dev, "rx-pin",        &rx_pin)        != VFS_OK ||
        device_get_prop_int(dev, "rx-clk",        &rx_clk)        != VFS_OK ||
        device_get_prop_int(dev, "rx-af",         &rx_af)         != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    __builtin_memset(cfg, 0, sizeof(*cfg));
    cfg->uart           = (uintptr_t)uart_base;
    cfg->uart_clk_periph = (uint32_t)uart_clk;
    cfg->baud_rate      = (uint32_t)uart_baud;
    cfg->data_width     = (uint32_t)data_width;
    cfg->parity         = (uint32_t)parity;
    cfg->stop_bits      = (uint32_t)stop_bits;
    cfg->tx = (struct hal_uart_pin_cfg){
        .port       = (uintptr_t)tx_port,
        .pin        = (uint16_t)tx_pin,
        .clk_periph = (uint32_t)tx_clk,
        .af         = (uint32_t)tx_af,
    };
    cfg->rx = (struct hal_uart_pin_cfg){
        .port       = (uintptr_t)rx_port,
        .pin        = (uint16_t)rx_pin,
        .clk_periph = (uint32_t)rx_clk,
        .af         = (uint32_t)rx_af,
    };

    return VFS_OK;
}

/**
 * @brief UART Host 设备探测: 申请池槽, 解析 DTS, 调用 uart_bus_host_init
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
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

    SYS_LOGI(kHostTag, "probe OK: %s baud=%lu",
             device_get_name(dev), (unsigned long)priv->cfg.baud_rate);
    return VFS_OK;

err_bus:
    COMPAT_IGNORE_RESULT(uart_bus_host_deinit(dev));
err_pool:
    osal_pool_release(&s_host_pool_ctrl, pool_idx);
    return ret;
}

/**
 * @brief UART Host 设备移除: BUSY 时拒绝销毁, 否则调用 host_deinit 并释放池槽
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_BUSY
 */
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
                                                              /*Client VFS*/
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

/**
 * @brief UART Client VFS 私有数据池启动初始化
 */
pre_execution(160)
static void uart_vfs_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_uart_vfs_pool_ctrl, s_uart_vfs_used, UART_VFS_COUNT));
}

/**
 * @brief UART Client 设备打开操作 (引用计数, 首次打开时调用 uart_bus_open)
 * @param dev 设备对象指针
 * @param arg 打开参数 (未使用)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int uart_vfs_open(struct device* dev, void* arg)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle*   lc;
    int                     first;

    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct uart_vfs_client, ops);
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

/**
 * @brief UART Client 设备关闭操作 (引用计数, 末次关闭时调用 uart_bus_close)
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int uart_vfs_close(struct device* dev)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle*   lc;
    int                     last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct uart_vfs_client, ops);
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

/**
 * @brief UART Client 设备写操作 (io 互斥, 调用 uart_bus_write)
 * @param dev 设备对象指针
 * @param buf 待发送数据缓冲
 * @param len 待发送字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
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

/**
 * @brief UART Client 设备读操作 (io 互斥, 调用 uart_bus_read)
 * @param dev 设备对象指针
 * @param buf 接收缓冲
 * @param len 接收字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
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

/**
 * @brief UART Client 设备 ioctl 控制 (TRANSFER: 先写后读, 半双工模拟全双工)
 * @param dev 设备对象指针
 * @param cmd 控制命令 (UART_CMD_*)
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (未使用, bus 层自行管理)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
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

/**
 * @brief UART Client 设备探测: 申请池槽/互斥锁, 注册 client, 绑定 fops 与生命周期
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_vfs_probe(struct device* dev)
{
    struct uart_vfs_client* priv;
    int                     pool_idx;
    int                     ret;

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

    /* UART 无 per-client 配置, client_register 无需 cfg */
    ret = uart_bus_client_register(dev);
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

/**
 * @brief UART Client 设备移除: 拒新 IO, 排空已有 IO, 注销 client, 释放池槽与互斥锁
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_vfs_remove(struct device* dev)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle*   lc;
    int                     pool_idx;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct uart_vfs_client, ops);
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

DRIVER_REGISTER(uart_host_vfs, "uart", uart_host_vfs_probe, uart_host_vfs_remove)
DRIVER_REGISTER(uart_vfs, "uart-client", uart_vfs_probe, uart_vfs_remove)
