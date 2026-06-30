/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * SPI VFS 实现 — SPI 总线子系统 VFS 层
 *
 * 两层结构:
 *   - Host VFS:   DTS 解析 + spi_bus_host_init (controller driver)
 *   - Client VFS: spi_bus_client_register + fops 挂载 (master+slave 统一, role 分派)
 *
 * 生命周期 (dev_lifecycle): open/close 引用计数, io 互斥, remove drain。
 * I/O 按 role 分派: master=spi_bus_transfer (单工/全双工), slave=spi_bus_slave_sync。
 *
 * DTS 三层嵌套 (Linux 风格):
 *   spi@1 (spi-master)                                ← host controller
 *   └── spi-master@0 (heterogeneous,spi-master-client) ← bus client (spi_vfs)
 *       └── w25q64@0 (winbond,w25q64)                 ← leaf device (w25q64_spi)
 *
 *   w25q64_spi_probe: device_get_parent(dev) → client (有 fops) → spi_vfs_transfer
 *@=========================================================================================================================*/
#define SPI_VFS_IMPL
#include "spi_vfs.h"
#include "spi_bus.h"
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
                                                              /* Host VFS */
/*===========================================================================================================================================================*/
#define SPI_HOST_VFS_COUNT 4

struct spi_host_vfs {
    struct hal_spi_bus_config  cfg;
    int                        pool_idx;
};

static struct spi_host_vfs s_host_pool[SPI_HOST_VFS_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_host_used[SPI_HOST_VFS_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_host_pool_ctrl COMPAT_ALIGNED(4);
static const char* const   kHostTag = "spi_vfs_host";

/**
 * @brief SPI Host VFS 私有数据池启动初始化
 */
pre_execution(150)
static void spi_host_vfs_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_host_pool_ctrl, s_host_used, SPI_HOST_VFS_COUNT));
}

/**
 * @brief 解析 SPI Host DTS 属性 (硬件直投值), 填入 hal_spi_bus_config
 * @param dev 设备对象指针
 * @param cfg 输出的 HAL 总线配置结构
 * @param bus_role 总线角色 (MASTER/SLAVE)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int spi_host_vfs_parse_dts(struct device* dev,
                                  struct hal_spi_bus_config* cfg,
                                  int bus_role)
{
    int spi_base = 0, spi_clk = 0;
    int mosi_port = 0, mosi_pin = 0, mosi_clk = 0, mosi_af = 0;
    int miso_port = 0, miso_pin = 0, miso_clk = 0, miso_af = 0;
    int sclk_port = 0, sclk_pin = 0, sclk_clk = 0, sclk_af = 0;

    if (device_get_prop_int(dev, "spi-base",  &spi_base)  != VFS_OK ||
        device_get_prop_int(dev, "spi-clk",   &spi_clk)   != VFS_OK ||
        device_get_prop_int(dev, "mosi-port", &mosi_port) != VFS_OK ||
        device_get_prop_int(dev, "mosi-pin",  &mosi_pin)  != VFS_OK ||
        device_get_prop_int(dev, "mosi-clk",  &mosi_clk)  != VFS_OK ||
        device_get_prop_int(dev, "mosi-af",   &mosi_af)   != VFS_OK ||
        device_get_prop_int(dev, "miso-port", &miso_port) != VFS_OK ||
        device_get_prop_int(dev, "miso-pin",  &miso_pin)  != VFS_OK ||
        device_get_prop_int(dev, "miso-clk",  &miso_clk)  != VFS_OK ||
        device_get_prop_int(dev, "miso-af",   &miso_af)   != VFS_OK ||
        device_get_prop_int(dev, "sclk-port", &sclk_port) != VFS_OK ||
        device_get_prop_int(dev, "sclk-pin",  &sclk_pin)  != VFS_OK ||
        device_get_prop_int(dev, "sclk-clk",  &sclk_clk)  != VFS_OK ||
        device_get_prop_int(dev, "sclk-af",   &sclk_af)   != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    __builtin_memset(cfg, 0, sizeof(*cfg));
    cfg->spi           = (uintptr_t)spi_base;
    cfg->spi_clk_periph = (uint32_t)spi_clk;
    cfg->mosi = (struct hal_spi_pin_cfg){
        .port       = (uintptr_t)mosi_port,
        .pin        = (uint16_t)mosi_pin,
        .clk_periph = (uint32_t)mosi_clk,
        .af         = (uint32_t)mosi_af,
    };
    cfg->miso = (struct hal_spi_pin_cfg){
        .port       = (uintptr_t)miso_port,
        .pin        = (uint16_t)miso_pin,
        .clk_periph = (uint32_t)miso_clk,
        .af         = (uint32_t)miso_af,
    };
    cfg->sclk = (struct hal_spi_pin_cfg){
        .port       = (uintptr_t)sclk_port,
        .pin        = (uint16_t)sclk_pin,
        .clk_periph = (uint32_t)sclk_clk,
        .af         = (uint32_t)sclk_af,
    };
    cfg->dma_chan = -1;
    cfg->max_transfer_sz = 0;
    cfg->bus_role = bus_role;

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "dma-chan", &cfg->dma_chan));
    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "max-trans-buffer", &cfg->max_transfer_sz));
    if (cfg->max_transfer_sz <= 0)
        COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "max-transfer-buffer", &cfg->max_transfer_sz));

    /* ceiling 由 spi_bus 层 clamp (HAL 静态缓冲区上限) */

    return VFS_OK;
}

/**
 * @brief SPI Host VFS 探测实现: 申请池槽, 解析 DTS, 调用 spi_bus_host_init
 * @param dev 设备对象指针
 * @param bus_role 总线角色 (MASTER/SLAVE)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_host_vfs_probe_impl(struct device* dev, int bus_role)
{
    struct spi_host_vfs* priv;
    int                  pool_idx;
    int                  ret;

    if (!dev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_host_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_host_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    ret = spi_host_vfs_parse_dts(dev, &priv->cfg, bus_role);
    if (ret != VFS_OK)
        goto err_pool;

    ret = spi_bus_host_init(dev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_pool;

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_bus;
    }

    SYS_LOGI(kHostTag, "probe OK: %s role=%s",
             device_get_name(dev),
             bus_role == SPI_BUS_ROLE_MASTER ? "master" : "slave");
    return VFS_OK;

err_bus:
    COMPAT_IGNORE_RESULT(spi_bus_host_deinit(dev));
err_pool:
    osal_pool_release(&s_host_pool_ctrl, pool_idx);
    return ret;
}

/**
 * @brief SPI Host Master 角色探测入口
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_host_vfs_probe_master(struct device* dev)
{
    return spi_host_vfs_probe_impl(dev, SPI_BUS_ROLE_MASTER);
}

/**
 * @brief SPI Host Slave 角色探测入口
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_host_vfs_probe_slave(struct device* dev)
{
    return spi_host_vfs_probe_impl(dev, SPI_BUS_ROLE_SLAVE);
}

/**
 * @brief SPI Host 设备移除: 调用 host_deinit, 释放池槽
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_host_vfs_remove(struct device* dev)
{
    struct spi_host_vfs* priv;
    int                  pool_idx;
    int                  ret;

    if (!dev)
        return VFS_ERR_INVAL;

    priv = (struct spi_host_vfs*)device_get_priv(dev);
    if (IS_ERR(priv))
        return PTR_ERR(priv);

    pool_idx = priv->pool_idx;

    ret = spi_bus_host_deinit(dev);
    if (ret != VFS_OK)
    {
        SYS_LOGE(kHostTag, "host remove busy: %s (ret=%d) — keeping resources",
                 device_get_name(dev), ret);
        return ret;
    }

    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_host_pool_ctrl, pool_idx);

    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                              /* Client VFS (master + slave unified) */
/*===========================================================================================================================================================*/
#ifndef DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE
#define DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE 0
#endif

#define SPI_VFS_MASTER_COUNT 4
#define SPI_VFS_SLAVE_COUNT  DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE
/* SLAVE_COUNT=0 时仍要求数组长度 > 0 (零长数组非标准) */
#define SPI_VFS_CLIENT_COUNT (SPI_VFS_MASTER_COUNT + (SPI_VFS_SLAVE_COUNT ? SPI_VFS_SLAVE_COUNT : 1))

struct spi_vfs_client {
    struct file_operations       ops;
    struct hal_spi_device_config cfg;
    struct osal_mutex*           io_mutex;
    uint8_t                      mutex_storage[OSAL_MUTEX_STORAGE_SIZE];
    int                          role;      /* SPI_BUS_ROLE_MASTER / SLAVE, probe 时设置 */
    int                          pool_idx;
};

static struct spi_vfs_client s_client_pool[SPI_VFS_CLIENT_COUNT] COMPAT_ALIGNED(4);
static uint8_t              s_client_used[SPI_VFS_CLIENT_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t          s_client_pool_ctrl COMPAT_ALIGNED(4);
static const char* const    kClientTag = "spi_vfs_client";

/**
 * @brief SPI Client VFS 私有数据池启动初始化
 */
pre_execution(160)
static void spi_vfs_client_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_client_pool_ctrl, s_client_used, SPI_VFS_CLIENT_COUNT));
}

/*@=========================================================================================================================*
 * open/close — master/slave 完全相同, 直接统一 (first→bus_open, last→bus_close)
 *@=========================================================================================================================*/
/**
 * @brief SPI Client 设备打开操作 (引用计数, 首次打开时调用 spi_bus_open)
 * @param dev 设备对象指针
 * @param arg 打开参数 (未使用)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_open(struct device* dev, void* arg)
{
    struct dev_lifecycle*   lc;
    int                     first;
    int                     ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = spi_bus_open(dev);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
    }

    if (ret == VFS_OK)
        dev_lc_open_end(lc);

    return ret;
}

/**
 * @brief SPI Client 设备关闭操作 (引用计数, 末次关闭时调用 spi_bus_close)
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_close(struct device* dev)
{
    struct dev_lifecycle* lc;
    int                   last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last)
        COMPAT_IGNORE_RESULT(spi_bus_close(dev));

    dev_lc_close_end(lc);
    return VFS_OK;
}

/*@=========================================================================================================================*
 * write/read — 按 role 分派: master=spi_bus_transfer (active), slave=spi_bus_slave_sync (passive)
 *@=========================================================================================================================*/
/**
 * @brief SPI Client 设备写操作 (按 role 分派: master=spi_bus_transfer, slave=spi_bus_slave_sync)
 * @param dev 设备对象指针
 * @param buffer 发送缓冲
 * @param len 发送字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_write(struct device* dev, const void* buffer,
                          size_t len, uint32_t timeout_ms)
{
    struct spi_vfs_client*  priv;
    struct dev_lifecycle*   lc;
    int                     ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct spi_vfs_client, ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    if (len == 0)
    {
        dev_lc_io_end(lc);
        return VFS_OK;
    }
    if (!buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    if (priv->role == SPI_BUS_ROLE_MASTER)
        ret = spi_bus_transfer(dev, (const uint8_t*)buffer, NULL, len, timeout_ms);
    else
        ret = spi_bus_slave_sync(dev, (const uint8_t*)buffer, NULL, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief SPI Client 设备读操作 (按 role 分派: master=spi_bus_transfer, slave=spi_bus_slave_sync)
 * @param dev 设备对象指针
 * @param buffer 接收缓冲
 * @param len 接收字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_read(struct device* dev, void* buffer,
                         size_t len, uint32_t timeout_ms)
{
    struct spi_vfs_client*  priv;
    struct dev_lifecycle*   lc;
    int                     ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct spi_vfs_client, ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    if (len == 0)
    {
        dev_lc_io_end(lc);
        return VFS_OK;
    }
    if (!buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    if (priv->role == SPI_BUS_ROLE_MASTER)
        ret = spi_bus_transfer(dev, NULL, (uint8_t*)buffer, len, timeout_ms);
    else
        ret = spi_bus_slave_sync(dev, NULL, (uint8_t*)buffer, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

/*@=========================================================================================================================*
 * ioctl — 按 role 分派:
 *   MASTER: SPI_CMD_TRANSFER → spi_bus_transfer (全双工)
 *   SLAVE:  SPI_CMD_QUEUE_TX → spi_bus_slave_queue_tx, SPI_CMD_GET_TRANS_RESULT → spi_bus_slave_get_trans_result
 *@=========================================================================================================================*/
/**
 * @brief SPI Client 设备 ioctl 控制 (按 role 分派: master=TRANSFER, slave=QUEUE_TX/GET_TRANS_RESULT)
 * @param dev 设备对象指针
 * @param cmd 控制命令 (SPI_CMD_*)
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_ioctl(struct device* dev, int cmd, void* arg,
                          size_t arg_len, uint32_t timeout_ms)
{
    struct spi_vfs_client*  priv;
    struct dev_lifecycle*   lc;
    int                     ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct spi_vfs_client, ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    if (priv->role == SPI_BUS_ROLE_MASTER)
    {
        switch (cmd)
        {
        case SPI_CMD_TRANSFER:
        {
            struct spi_transfer_arg* ta = (struct spi_transfer_arg*)arg;
            if (!ta || arg_len != sizeof(*ta) || !ta->tx)
                ret = VFS_ERR_INVAL;
            else
                ret = spi_bus_transfer(dev, (const uint8_t*)ta->tx,
                                       (uint8_t*)ta->rx, ta->len, timeout_ms);
            break;
        }
        default:
            ret = VFS_ERR_INVAL;
            break;
        }
    }
    else
    {
        switch (cmd)
        {
        case SPI_CMD_QUEUE_TX:
        {
            struct spi_queue_arg* qa = (struct spi_queue_arg*)arg;
            if (!qa || arg_len != sizeof(*qa) || !qa->data || qa->len == 0)
                ret = VFS_ERR_INVAL;
            else
                ret = spi_bus_slave_queue_tx(dev, qa->data, qa->len, timeout_ms);
            break;
        }
        case SPI_CMD_GET_TRANS_RESULT:
        {
            struct spi_trans_result_arg* tra = (struct spi_trans_result_arg*)arg;
            if (!tra || arg_len != sizeof(*tra))
                ret = VFS_ERR_INVAL;
            else
                ret = spi_bus_slave_get_trans_result(dev, tra->data, tra->len,
                                                      tra->trans_len, timeout_ms);
            break;
        }
        default:
            ret = VFS_ERR_INVAL;
            break;
        }
    }

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations spi_vfs_fops = {
    .open  = spi_vfs_open,
    .close = spi_vfs_close,
    .write = spi_vfs_write,
    .read  = spi_vfs_read,
    .ioctl = spi_vfs_ioctl,
};

/*===========================================================================================================================================================*/
                                                              /* 便捷 API (上层驱动调用) */
/*===========================================================================================================================================================*/
/**
 * @brief 便捷 SPI 全双工传输 (带锁, 经 device_ioctl 派发)
 * @param dev SPI 设备指针
 * @param tx 发送缓冲
 * @param rx 接收缓冲
 * @param len 传输字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回传输字节数, 失败返回 VFS_ERR_*
 */
int spi_vfs_transfer(struct device* dev, const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms)
{
    struct spi_transfer_arg arg;

    if (!dev || len == 0)
        return VFS_ERR_INVAL;

    arg.tx  = tx;
    arg.rx  = rx;
    arg.len = len;

    return device_ioctl(dev, SPI_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

/*===========================================================================================================================================================*/
                                                              /* Client Probe / Remove */
/*===========================================================================================================================================================*/

/*@=========================================================================================================================*
 * parse_dts — master/slave 基本相同, slave 多 queue-size (master queue_size=0)
 *@=========================================================================================================================*/
/**
 * @brief 解析 SPI Client DTS 属性 (硬件直投值), 填入 hal_spi_device_config
 * @param dev 设备对象指针
 * @param cfg 输出的 HAL 设备配置结构
 * @param role 总线角色 (MASTER/SLAVE, slave 额外解析 queue-size)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int spi_vfs_parse_dts(struct device* dev, struct hal_spi_device_config* cfg,
                              int role)
{
    int cs_port = 0, cs_pin = 0, cs_clk = 0;
    int mode = 0, freq = 0;

    if (device_get_prop_int(dev, "cs-port", &cs_port) != VFS_OK ||
        device_get_prop_int(dev, "cs-pin",  &cs_pin)  != VFS_OK ||
        device_get_prop_int(dev, "cs-clk",  &cs_clk)  != VFS_OK ||
        device_get_prop_int(dev, "spi-mode", &mode) != VFS_OK ||
        device_get_prop_int(dev, "spi-max-frequency", &freq) != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    __builtin_memset(cfg, 0, sizeof(*cfg));
    cfg->cs_port       = (uintptr_t)cs_port;
    cfg->cs_pin        = (uint16_t)cs_pin;
    cfg->cs_clk_periph = (uint32_t)cs_clk;
    cfg->mode          = mode;
    cfg->clock_speed_hz = freq;
    cfg->queue_size    = 0;

    /* slave 解析 queue-size, master 不需要 */
    if (role == SPI_BUS_ROLE_SLAVE)
    {
        int queue_size = -1;
        COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "queue-size", &queue_size));
        cfg->queue_size = queue_size;
    }

    return VFS_OK;
}

/*@=========================================================================================================================*
 * spi_vfs_probe — 统一 probe (master + slave): role 获取 → pool/mutex → parse_dts → client_register → lc_bind + fops
 * err 路径: ops=NULL (切断 fops) + dev_lc_reset (切断 io_lock) + mutex destroy + pool release
 *@=========================================================================================================================*/
/**
 * @brief SPI Client 设备探测: 获取 role, 申请池槽/互斥锁, 解析 DTS, 注册 client, 绑定 fops
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_probe(struct device* dev)
{
    struct spi_vfs_client*   priv;
    struct spi_bus_client*   bus_cli;
    int                       role;
    int                       pool_idx;
    int                       ret;

    if (!dev)
        return VFS_ERR_INVAL;

    role = spi_bus_host_role(dev);
    if (role != SPI_BUS_ROLE_MASTER && role != SPI_BUS_ROLE_SLAVE)
    {
        SYS_LOGE(kClientTag, "invalid SPI role: %s", device_get_name(dev));
        return VFS_ERR_INVAL;
    }

    pool_idx = osal_pool_claim(&s_client_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_client_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;
    priv->role      = role;

    if (osal_mutex_create_static(&priv->io_mutex, priv->mutex_storage,
                                  sizeof(priv->mutex_storage)) != 0)
    {
        osal_pool_release(&s_client_pool_ctrl, pool_idx);
        return VFS_ERR_NOMEM;
    }

    ret = spi_vfs_parse_dts(dev, &priv->cfg, role);
    if (ret != VFS_OK)
        goto err_mutex;

    ret = spi_bus_client_register(dev, &priv->cfg, &bus_cli);
    if (ret != VFS_OK)
        goto err_mutex;

    device_lc_bind(dev, priv->io_mutex);
    priv->ops = spi_vfs_fops;
    dev->ops  = &priv->ops;

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        spi_bus_client_unregister(dev);
        ret = VFS_ERR_IO;
        goto err_mutex;
    }

    SYS_LOGI(kClientTag, "probe OK: %s role=%s mode=%d freq=%d",
             device_get_name(dev),
             role == SPI_BUS_ROLE_MASTER ? "master" : "slave",
             priv->cfg.mode, priv->cfg.clock_speed_hz);
    return VFS_OK;

err_mutex:
    dev->ops = NULL;                   /* 切断 fops, 防 UAF */
    dev_lc_reset(device_lc(dev));       /* 切断 io_lock 绑定 */
    osal_mutex_destroy(priv->io_mutex);
    osal_pool_release(&s_client_pool_ctrl, pool_idx);
    return ret;
}

/*@=========================================================================================================================*
 * spi_vfs_remove — 统一 remove: remove_start (拒新 IO) → ops_unregister → remove_drain (等已有 IO) → bus_unregister + pool release
 *@=========================================================================================================================*/
/**
 * @brief SPI Client 设备移除: 拒新 IO, 排空已有 IO, 注销 client, 释放池槽与互斥锁
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_remove(struct device* dev)
{
    struct spi_vfs_client*  priv;
    struct dev_lifecycle*   lc;
    int                     pool_idx;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct spi_vfs_client, ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
        return VFS_ERR_IO;

    spi_bus_client_unregister(dev);
    osal_mutex_destroy(priv->io_mutex);
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_client_pool_ctrl, pool_idx);

    dev_lc_remove_finish(lc);
    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                              /* Driver Registration */
/*===========================================================================================================================================================*/
DRIVER_REGISTER(spi_host_master, "spi-master",
                spi_host_vfs_probe_master, spi_host_vfs_remove)
DRIVER_REGISTER(spi_host_slave, "spi-slave",
                spi_host_vfs_probe_slave, spi_host_vfs_remove)
DRIVER_REGISTER(spi_vfs_master, "heterogeneous,spi-master-client",
                spi_vfs_probe, spi_vfs_remove)
DRIVER_REGISTER(spi_vfs_slave, "heterogeneous,fft-spi-slave",
                spi_vfs_probe, spi_vfs_remove)
/*===========================================================================================================================================================*/
