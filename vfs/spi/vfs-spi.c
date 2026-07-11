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
 *   w25q64_spi_probe: device_get_parent(pdev) → client (有 fops) → spi_vfs_transfer
 *@=========================================================================================================================*/
#define SPI_VFS_IMPL
#include "vfs-spi.h"
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
/*Host VFS*/
/*===========================================================================================================================================================*/
#ifndef SPI_VFS_PRIV_COUNT
#define SPI_VFS_PRIV_COUNT 4
#endif

struct vfs_spi_priv {
    struct hal_spi_bus_config  cfg;
    int                        pool_idx;
};

static struct vfs_spi_priv s_spi_priv_pool[SPI_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_spi_priv_used[SPI_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_spi_priv_pool_ctrl COMPAT_ALIGNED(4);
static const char* const   s_kHostTag = "spi_vfs_host";

/**
 * @brief SPI Host VFS 私有数据池启动初始化
 */
pre_execution(150)
static void vfs_spi_priv_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_spi_priv_pool_ctrl, s_spi_priv_used, SPI_VFS_PRIV_COUNT));
}

/**
 * @brief 解析 SPI Host DTS 属性 (硬件直投值), 填入 hal_spi_bus_config
 * @param pdev 设备对象指针
 * @param cfg 配置结构指针
 * @param bus_role 总线角色 (MASTER/SLAVE)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_spi_priv_parse_dts(struct device* pdev,struct hal_spi_bus_config* cfg,int bus_role)
{
    int spi_base = 0, spi_clk = 0;
    int mosi_port = 0, mosi_pin = 0, mosi_clk = 0, mosi_af = 0;
    int miso_port = 0, miso_pin = 0, miso_clk = 0, miso_af = 0;
    int mosi_output_type = 0, mosi_speed = 0, mosi_mode = 0, mosi_pull = 0;
    int miso_output_type = 0, miso_speed = 0, miso_mode = 0, miso_pull = 0;
    int sclk_port = 0, sclk_pin = 0, sclk_clk = 0, sclk_af = 0;
    int sclk_output_type = 0, sclk_speed = 0, sclk_mode = 0, sclk_pull = 0;

    if (device_get_prop_int(pdev, "spi-base",  &spi_base)  != VFS_OK ||
        device_get_prop_int(pdev, "spi-clk",   &spi_clk)   != VFS_OK ||
        device_get_prop_int(pdev, "mosi-port", &mosi_port) != VFS_OK ||
        device_get_prop_int(pdev, "mosi-pin",  &mosi_pin)  != VFS_OK ||
        device_get_prop_int(pdev, "mosi-clk",  &mosi_clk)  != VFS_OK ||
        device_get_prop_int(pdev, "mosi-af",   &mosi_af)   != VFS_OK ||
        device_get_prop_int(pdev, "miso-port", &miso_port) != VFS_OK ||
        device_get_prop_int(pdev, "miso-pin",  &miso_pin)  != VFS_OK ||
        device_get_prop_int(pdev, "miso-clk",  &miso_clk)  != VFS_OK ||
        device_get_prop_int(pdev, "miso-af",   &miso_af)   != VFS_OK ||
        device_get_prop_int(pdev, "sclk-port", &sclk_port) != VFS_OK ||
        device_get_prop_int(pdev, "sclk-pin",  &sclk_pin)  != VFS_OK ||
        device_get_prop_int(pdev, "sclk-clk",  &sclk_clk)  != VFS_OK ||
        device_get_prop_int(pdev, "sclk-af",   &sclk_af)   != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }
    /** pin_cfg 扩展字段: DTS 未定义时 LL_GPIO_Init 取 0 */
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "mosi-output-type", &mosi_output_type));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "mosi-speed",       &mosi_speed));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "mosi-mode",        &mosi_mode));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "mosi-pull",        &mosi_pull));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "miso-output-type", &miso_output_type));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "miso-speed",       &miso_speed));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "miso-mode",        &miso_mode));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "miso-pull",        &miso_pull));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sclk-output-type", &sclk_output_type));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sclk-speed",       &sclk_speed));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sclk-mode",        &sclk_mode));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sclk-pull",        &sclk_pull));

    COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
    cfg->spi           = (uintptr_t)spi_base;
    cfg->spi_clk_periph = (uint32_t)spi_clk;
    cfg->mosi = (struct hal_spi_pin_cfg){
        .port        = (uintptr_t)mosi_port,
        .pin         = (uint16_t)mosi_pin,
        .clk_bus     = (uint32_t)mosi_clk,
        .af          = (uint32_t)mosi_af,
        .output_type = (uint32_t)mosi_output_type,
        .speed       = (uint32_t)mosi_speed,
        .mode        = (uint32_t)mosi_mode,
        .pull        = (uint32_t)mosi_pull,
    };
    cfg->miso = (struct hal_spi_pin_cfg){
        .port        = (uintptr_t)miso_port,
        .pin         = (uint16_t)miso_pin,
        .clk_bus     = (uint32_t)miso_clk,
        .af          = (uint32_t)miso_af,
        .output_type = (uint32_t)miso_output_type,
        .speed       = (uint32_t)miso_speed,
        .mode        = (uint32_t)miso_mode,
        .pull        = (uint32_t)miso_pull,
    };
    cfg->sclk = (struct hal_spi_pin_cfg){
        .port        = (uintptr_t)sclk_port,
        .pin         = (uint16_t)sclk_pin,
        .clk_bus     = (uint32_t)sclk_clk,
        .af          = (uint32_t)sclk_af,
        .output_type = (uint32_t)sclk_output_type,
        .speed       = (uint32_t)sclk_speed,
        .mode        = (uint32_t)sclk_mode,
        .pull        = (uint32_t)sclk_pull,
    };
    cfg->max_transfer_sz = 0;
    cfg->bus_role = bus_role;

    {
        int max_transfer_sz = 0;
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "max-trans-buffer", &max_transfer_sz));
        if (max_transfer_sz <= 0)
            COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "max-transfer-buffer", &max_transfer_sz));
        cfg->max_transfer_sz = (size_t)(max_transfer_sz > 0 ? max_transfer_sz : 0);
    }

    /** DMA 配置 (硬件直投, 仿 ADC): dma-tx-cfg/dma-rx-cfg
     *  = <handle stream channel priority memory_size enable
     *     mode periph_inc mem_inc periph_data_size fifo_mode fifo_threshold mem_burst periph_burst> */
    {
        int dma_arr[14];
        if (device_get_prop_int_array(pdev, "dma-tx-cfg", dma_arr, 14) == 14)
        {
            cfg->dma_tx.dma_handle           = (uintptr_t)dma_arr[0];
            cfg->dma_tx.dma_stream            = (uint32_t)dma_arr[1];
            cfg->dma_tx.dma_channel           = (uint32_t)dma_arr[2];
            cfg->dma_tx.dma_priority          = (uint32_t)dma_arr[3];
            cfg->dma_tx.dma_memory_size       = (uint32_t)dma_arr[4];
            cfg->dma_tx.dma_enable           = (uint32_t)dma_arr[5];
            cfg->dma_tx.dma_mode             = (uint32_t)dma_arr[6];
            cfg->dma_tx.dma_periph_inc        = (uint32_t)dma_arr[7];
            cfg->dma_tx.dma_mem_inc           = (uint32_t)dma_arr[8];
            cfg->dma_tx.dma_periph_data_size  = (uint32_t)dma_arr[9];
            cfg->dma_tx.dma_fifo_mode         = (uint32_t)dma_arr[10];
            cfg->dma_tx.dma_fifo_threshold    = (uint32_t)dma_arr[11];
            cfg->dma_tx.dma_mem_burst         = (uint32_t)dma_arr[12];
            cfg->dma_tx.dma_periph_burst      = (uint32_t)dma_arr[13];
        }
        if (device_get_prop_int_array(pdev, "dma-rx-cfg", dma_arr, 14) == 14)
        {
            cfg->dma_rx.dma_handle           = (uintptr_t)dma_arr[0];
            cfg->dma_rx.dma_stream            = (uint32_t)dma_arr[1];
            cfg->dma_rx.dma_channel           = (uint32_t)dma_arr[2];
            cfg->dma_rx.dma_priority          = (uint32_t)dma_arr[3];
            cfg->dma_rx.dma_memory_size       = (uint32_t)dma_arr[4];
            cfg->dma_rx.dma_enable           = (uint32_t)dma_arr[5];
            cfg->dma_rx.dma_mode             = (uint32_t)dma_arr[6];
            cfg->dma_rx.dma_periph_inc        = (uint32_t)dma_arr[7];
            cfg->dma_rx.dma_mem_inc           = (uint32_t)dma_arr[8];
            cfg->dma_rx.dma_periph_data_size  = (uint32_t)dma_arr[9];
            cfg->dma_rx.dma_fifo_mode         = (uint32_t)dma_arr[10];
            cfg->dma_rx.dma_fifo_threshold    = (uint32_t)dma_arr[11];
            cfg->dma_rx.dma_mem_burst         = (uint32_t)dma_arr[12];
            cfg->dma_rx.dma_periph_burst      = (uint32_t)dma_arr[13];
        }
    }

    /* ceiling 由 spi_bus 层 clamp (HAL 静态缓冲区上限) */

    return VFS_OK;
}

/**
 * @brief SPI Host VFS 探测实现: 申请池槽, 解析 DTS, 调用 spi_bus_host_init
 * @param pdev 设备对象指针
 * @param bus_role 总线角色 (MASTER/SLAVE)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_spi_priv_probe_impl(struct device* pdev, int bus_role)
{
    struct vfs_spi_priv* priv;
    int                  pool_idx;
    int                  ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_spi_priv_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_spi_priv_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    ret = vfs_spi_priv_parse_dts(pdev, &priv->cfg, bus_role);
    if (ret != VFS_OK)
        goto err_pool;

    ret = spi_bus_host_init(pdev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_pool;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_bus;
    }

    SYS_LOGI(s_kHostTag, "probe OK: %s role=%s",
             device_get_name(pdev),
             bus_role == SPI_BUS_ROLE_MASTER ? "master" : "slave");
    return VFS_OK;

err_bus:
    COMPAT_IGNORE_RESULT(spi_bus_host_deinit(pdev));
err_pool:
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_spi_priv_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief SPI Host Master 角色探测入口
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_spi_priv_probe_master(struct device* pdev)
{
    return vfs_spi_priv_probe_impl(pdev, SPI_BUS_ROLE_MASTER);
}

/**
 * @brief SPI Host Slave 角色探测入口
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_spi_priv_probe_slave(struct device* pdev)
{
    return vfs_spi_priv_probe_impl(pdev, SPI_BUS_ROLE_SLAVE);
}

/**
 * @brief SPI Host 设备移除: remove_start → ops_unregister → remove_drain → host_deinit → 释放池槽
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_spi_priv_remove(struct device* pdev)
{
    struct vfs_spi_priv* priv;
    struct dev_lifecycle* lc;
    int                   pool_idx;
    int                   ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    priv = (struct vfs_spi_priv*)device_get_priv(pdev);
    if (IS_ERR(priv))
        return PTR_ERR(priv);

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    ret = spi_bus_host_deinit(pdev);
    if (ret != VFS_OK)
    {
        SYS_LOGE(s_kHostTag, "host remove busy: %s (ret=%d) — keeping resources",
                 device_get_name(pdev), ret);
        dev_lc_remove_finish(lc);
        return ret;
    }

    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_spi_priv_pool_ctrl, pool_idx));

    dev_lc_remove_finish(lc);
    return VFS_OK;
}
/*===========================================================================================================================================================*/
/*Client VFS (master + slave unified)*/
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
static const char* const    s_kClientTag = "spi_vfs_client";

/**
 * @brief SPI Client VFS 私有数据池启动初始化
 */
pre_execution(160)
static void spi_vfs_client_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_client_pool_ctrl, s_client_used, SPI_VFS_CLIENT_COUNT));
}

/*===========================================================================================================================================================*/
/*open / close (master/slave 统一)*/
/*===========================================================================================================================================================*/
/**
 * @brief SPI Client 设备打开操作 (引用计数, 首次打开时调用 spi_bus_open)
 * @param pdev 设备对象指针
 * @param arg 命令参数指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_open(struct device* pdev, void* arg)
{
    struct dev_lifecycle*   lc;
    int                     first;
    int                     ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = spi_bus_open(pdev);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
    }

    if (ret == VFS_OK)
        dev_lc_open_end(lc);

    return ret;
}

/**
 * @brief SPI Client 设备关闭操作 (引用计数, 末次关闭时调用 spi_bus_close)
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_close(struct device* pdev)
{
    struct dev_lifecycle* lc;
    int                   last;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;

    if (last)
        COMPAT_IGNORE_RESULT(spi_bus_close(pdev));

    dev_lc_close_end(lc);
    return VFS_OK;
}

/*===========================================================================================================================================================*/
/*write / read (按 role 分派: master=transfer, slave=slave_sync)*/
/*===========================================================================================================================================================*/
/**
 * @brief SPI Client 设备写操作 (按 role 分派: master=spi_bus_transfer, slave=spi_bus_slave_sync)
 * @param pdev 设备对象指针
 * @param buffer 数据缓冲
 * @param len 数据长度 (字节)
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_write(struct device* pdev, const void* buffer,
                          size_t len, uint32_t timeout_ms)
{
    struct spi_vfs_client*  priv;
    struct dev_lifecycle*   lc;
    int                     ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_vfs_client, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
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
        ret = spi_bus_transfer(pdev, (const uint8_t*)buffer, NULL, len, timeout_ms);
    else
        ret = spi_bus_slave_sync(pdev, (const uint8_t*)buffer, NULL, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief SPI Client 设备读操作 (按 role 分派: master=spi_bus_transfer, slave=spi_bus_slave_sync)
 * @param pdev 设备对象指针
 * @param buffer 数据缓冲
 * @param len 数据长度 (字节)
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_read(struct device* pdev, void* buffer,
                         size_t len, uint32_t timeout_ms)
{
    struct spi_vfs_client*  priv;
    struct dev_lifecycle*   lc;
    int                     ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_vfs_client, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
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
        ret = spi_bus_transfer(pdev, NULL, (uint8_t*)buffer, len, timeout_ms);
    else
        ret = spi_bus_slave_sync(pdev, NULL, (uint8_t*)buffer, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

/*===========================================================================================================================================================*/
/*ioctl 命令映射表*/
/*===========================================================================================================================================================*/
typedef int (*spi_ioctl_fn_t)(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms);

struct spi_ioctl_map
{
    spi_ioctl_fn_t handler;
};

/**
 * @brief SPI 命令处理: 全双工传输
 * @param pdev 设备对象指针
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_cmd_transfer(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct spi_transfer_arg* ta = (const struct spi_transfer_arg*)arg;
    if (!ta || arg_len != sizeof(*ta))
        return VFS_ERR_INVAL;
    return spi_bus_transfer(pdev, ta->tx, ta->rx, ta->len, timeout_ms);
}

/**
 * @brief SPI 命令处理: Slave 发送队列入队
 * @param pdev 设备对象指针
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_cmd_queue_tx(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct spi_queue_arg* qa = (const struct spi_queue_arg*)arg;
    if (!qa || arg_len != sizeof(*qa) || !qa->data || qa->len == 0)
        return VFS_ERR_INVAL;
    return spi_bus_slave_queue_tx(pdev, qa->data, qa->len, timeout_ms);
}

/**
 * @brief SPI 命令处理: 获取 Slave 传输结果
 * @param pdev 设备对象指针
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_cmd_get_trans_result(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct spi_trans_result_arg* tra = (const struct spi_trans_result_arg*)arg;
    if (!tra || arg_len != sizeof(*tra))
        return VFS_ERR_INVAL;
    return spi_bus_slave_get_trans_result(pdev, tra->data, tra->len, tra->trans_len, timeout_ms);
}

static const struct spi_ioctl_map s_spi_ioctl_map[SPI_CMD_COUNT] = {
    [SPI_CMD_TRANSFER - SPI_CMD_BASE - 1]         = { spi_cmd_transfer },
    [SPI_CMD_QUEUE_TX - SPI_CMD_BASE - 1]         = { spi_cmd_queue_tx },
    [SPI_CMD_GET_TRANS_RESULT - SPI_CMD_BASE - 1] = { spi_cmd_get_trans_result },
};

/**
 * @brief SPI Client 设备 ioctl 控制 (命令映射表 O(1) 派发)
 * @param pdev 设备对象指针
 * @param cmd 控制命令
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_ioctl(struct device* pdev, int cmd, void* arg,
                          size_t arg_len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int32_t               offset;
    int                   ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    offset = (int32_t)cmd - (int32_t)SPI_CMD_BASE;
    if (offset < 1 || offset > SPI_CMD_COUNT || !s_spi_ioctl_map[offset - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_spi_ioctl_map[offset - 1].handler(pdev, arg, arg_len, timeout_ms);

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
/*便捷 API (上层驱动调用)*/
/*===========================================================================================================================================================*/
/**
 * @brief 便捷 SPI 全双工传输 (带锁, 经 device_ioctl 派发)
 * @param pdev 设备对象指针
 * @param tx 发送缓冲
 * @param rx 接收缓冲
 * @param len 数据长度 (字节)
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_vfs_transfer(struct device* pdev, const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms)
{
    struct spi_transfer_arg arg;

    if (!pdev || len == 0)
        return VFS_ERR_INVAL;

    arg.tx  = tx;
    arg.rx  = rx;
    arg.len = len;

    return device_ioctl(pdev, SPI_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

/*===========================================================================================================================================================*/
/*Client Probe / Remove*/
/*===========================================================================================================================================================*/

/*===========================================================================================================================================================*/
/*parse_dts (master/slave 基本相同)*/
/*===========================================================================================================================================================*/
/**
 * @brief 解析 SPI Client DTS 属性 (硬件直投值), 填入 hal_spi_device_config
 * @param pdev 设备对象指针
 * @param cfg 配置结构指针
 * @param role 总线角色 (MASTER/SLAVE)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_parse_dts(struct device* pdev, struct hal_spi_device_config* cfg,
                              int role)
{
    int cs_port = 0, cs_pin = 0, cs_clk = 0;
    int mode = 0, freq = 0;

    if (device_get_prop_int(pdev, "cs-port", &cs_port) != VFS_OK ||
        device_get_prop_int(pdev, "cs-pin",  &cs_pin)  != VFS_OK ||
        device_get_prop_int(pdev, "cs-clk",  &cs_clk)  != VFS_OK ||
        device_get_prop_int(pdev, "spi-mode", &mode) != VFS_OK ||
        device_get_prop_int(pdev, "spi-max-frequency", &freq) != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
    cfg->cs_port        = (uintptr_t)cs_port;
    cfg->cs_pin         = (uint16_t)cs_pin;
    cfg->cs_clk_periph  = (uint32_t)cs_clk;
    cfg->mode           = mode;
    cfg->clock_speed_hz = freq;

    /** 扩展字段: DTS 可选, 未定义时取 0 (LL 库默认行为) */
    {
        int transfer_direction = 0, data_width = 0, nss = 0, bit_order = 0;
        int crc_calculation = 0, crc_poly = 0, standard = 0;
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "transfer-direction", &transfer_direction));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "data-width",         &data_width));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "nss",                &nss));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "bit-order",          &bit_order));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "crc-calculation",    &crc_calculation));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "crc-poly",           &crc_poly));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "standard",           &standard));
        cfg->transfer_direction = (uint32_t)transfer_direction;
        cfg->data_width         = (uint32_t)data_width;
        cfg->nss                = (uint32_t)nss;
        cfg->bit_order          = (uint32_t)bit_order;
        cfg->crc_calculation    = (uint32_t)crc_calculation;
        cfg->crc_poly           = (uint32_t)crc_poly;
        cfg->standard           = (uint32_t)standard;
    }

    return VFS_OK;
}

/*===========================================================================================================================================================*/
/*spi_vfs_probe (统一 probe: master + slave)*/
/*===========================================================================================================================================================*/
/**
 * @brief SPI Client 设备探测: 获取 role, 申请池槽/互斥锁, 解析 DTS, 注册 client, 绑定 fops
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_probe(struct device* pdev)
{
    struct spi_vfs_client*   priv;
    struct spi_bus_client*   bus_cli;
    int                       role;
    int                       pool_idx;
    int                       ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    role = spi_bus_host_role(pdev);
    if (role != SPI_BUS_ROLE_MASTER && role != SPI_BUS_ROLE_SLAVE)
    {
        SYS_LOGE(s_kClientTag, "invalid SPI role: %s", device_get_name(pdev));
        return VFS_ERR_INVAL;
    }

    pool_idx = osal_pool_claim(&s_client_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_client_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;
    priv->role      = role;

    if (osal_mutex_create_static(&priv->io_mutex, priv->mutex_storage,
                                  sizeof(priv->mutex_storage)) != 0)
    {
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, pool_idx));
        return VFS_ERR_NOMEM;
    }

    ret = spi_vfs_parse_dts(pdev, &priv->cfg, role);
    if (ret != VFS_OK)
        goto err_mutex;

    ret = spi_bus_client_register(pdev, &priv->cfg, &bus_cli);
    if (ret != VFS_OK)
        goto err_mutex;

    priv->ops = spi_vfs_fops;
    pdev->ops  = &priv->ops;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        spi_bus_client_unregister(pdev);
        ret = VFS_ERR_IO;
        goto err_mutex;
    }

    SYS_LOGI(s_kClientTag, "probe OK: %s role=%s mode=%d freq=%d",
             device_get_name(pdev),
             role == SPI_BUS_ROLE_MASTER ? "master" : "slave",
             priv->cfg.mode, priv->cfg.clock_speed_hz);
    return VFS_OK;

err_mutex:
    pdev->ops = NULL;                   /* 切断 fops, 防 UAF */
    dev_lc_reset(device_lc(pdev));       /* 重置生命周期 */
    osal_mutex_destroy(priv->io_mutex);
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, pool_idx));
    return ret;
}

/*===========================================================================================================================================================*/
/*spi_vfs_remove (统一 remove: master + slave)*/
/*===========================================================================================================================================================*/
/**
 * @brief SPI Client 设备移除: 拒新 IO, 排空已有 IO, 注销 client, 释放池槽与互斥锁
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int spi_vfs_remove(struct device* pdev)
{
    struct spi_vfs_client*  priv;
    struct dev_lifecycle*   lc;
    int                     pool_idx;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_vfs_client, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    spi_bus_client_unregister(pdev);
    osal_mutex_destroy(priv->io_mutex);
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, pool_idx));

    dev_lc_remove_finish(lc);
    return VFS_OK;
}
/*===========================================================================================================================================================*/
/*Driver Registration*/
/*===========================================================================================================================================================*/
DRIVER_REGISTER(spi_host_master, "spi-master",
                vfs_spi_priv_probe_master, vfs_spi_priv_remove)
DRIVER_REGISTER(spi_host_slave, "spi-slave",
                vfs_spi_priv_probe_slave, vfs_spi_priv_remove)
DRIVER_REGISTER(spi_vfs_master, "heterogeneous,spi-master-client",
                spi_vfs_probe, spi_vfs_remove)
DRIVER_REGISTER(spi_vfs_slave, "heterogeneous,fft-spi-slave",
                spi_vfs_probe, spi_vfs_remove)
/*===========================================================================================================================================================*/
