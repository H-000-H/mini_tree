/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file neo_m8n_drv.c
 *@brief NEO-M8N GPS 模块驱动实现 — 挂在 UART 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_neo_m8n_pool[NEO_M8N_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 neo_m8n_drv.h。
 *   数据流: VFS ioctl → neo_m8n_cmd_nmea → device_read(UART) → HAL
 */

#include "neo_m8n_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-uart.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_UBLOX_NEO_M8N
#define DTC_GEN_COUNT_UBLOX_NEO_M8N 1
#endif
#define NEO_M8N_POOL_COUNT DTC_GEN_COUNT_UBLOX_NEO_M8N

/** @brief NEO-M8N 驱动实例（嵌入 fops 与接收缓冲） */
struct neo_m8n_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* uart_dev; /**< 所属 UART client 设备 */
    uint8_t rxbuf[128]; /**< 接收缓冲（预留） */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct neo_m8n_device s_neo_m8n_pool[NEO_M8N_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_neo_m8n_used[NEO_M8N_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_neo_m8n_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "neo_m8n";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void neo_m8n_pool_boot_init(void) { COMPAT_IGNORE_RESULT(osal_pool_init(&s_neo_m8n_pool_ctrl, s_neo_m8n_used, NEO_M8N_POOL_COUNT)); }

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct neo_m8n_device* neo_m8n_get_drvdata(struct device* pdev) { return (struct neo_m8n_device*)device_get_priv(pdev); }

/**
 * @brief UART 双向传输（UART_CMD_TRANSFER）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int neo_m8n_uart_xchg(struct neo_m8n_device* dev, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, uint32_t timeout_ms)
{
    struct uart_transfer_arg arg;
    if (!dev || !dev->uart_dev)
        return VFS_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.tx_len = tx_len;
    arg.rx_len = rx_len;
    return device_ioctl(dev->uart_dev, UART_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

/**
 * @brief 首次 open 时打开 UART 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int neo_m8n_hw_create(struct neo_m8n_device* dev)
{
    if (!dev)
        return VFS_ERR_INVAL;
    if (dev->hw_ready)
        return VFS_OK;
    {
        int ret = device_open(dev->uart_dev, NULL);
        if (ret != VFS_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 UART client）
 */
static void neo_m8n_hw_destroy(struct neo_m8n_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->uart_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->uart_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int neo_m8n_open(struct device* pdev, void* arg)
{
    struct neo_m8n_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = neo_m8n_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = neo_m8n_hw_create(dev);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

/**
 * @brief fops.close：引用计数关闭，末次调用释放硬件
 */
static int neo_m8n_close(struct device* pdev)
{
    struct neo_m8n_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = neo_m8n_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        neo_m8n_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*neo_m8n_ioctl_fn_t)(struct neo_m8n_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct neo_m8n_ioctl_map
{
    neo_m8n_ioctl_fn_t handler;
};

/**
 * @brief NEO_M8N_CMD_READ_NMEA 实现：读满一帧 NMEA（到 \count 为止）
 */
static int neo_m8n_cmd_nmea(struct neo_m8n_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct neo_m8n_buf* b = (struct neo_m8n_buf*)arg;
    size_t got = 0;
    int ret;

    if (!dev->hw_ready || !b || len != sizeof(*b) || !b->data || b->cap == 0U)
        return VFS_ERR_INVAL;

    ret = device_read(dev->uart_dev, (uint8_t*)b->data, b->cap, timeout_ms);
    if (ret < 0)
        return ret;
    got = (size_t)ret;
    while (got < b->cap)
    {
        ret = device_read(dev->uart_dev, (uint8_t*)&b->data[got], 1, 10);
        if (ret <= 0)
            break;
        got += (size_t)ret;
        if (b->data[got - 1U] == '\n')
            break;
    }
    b->len = got;
    return VFS_OK;
}
static const struct neo_m8n_ioctl_map s_neo_m8n_map[NEO_M8N_CMD_COUNT] = {
    [NEO_M8N_CMD_READ_NMEA - NEO_M8N_CMD_BASE - 1] = {neo_m8n_cmd_nmea},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int neo_m8n_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct neo_m8n_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = neo_m8n_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)NEO_M8N_CMD_BASE;
    if (off < 1 || off > NEO_M8N_CMD_COUNT || !s_neo_m8n_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_neo_m8n_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations neo_m8n_fops = {
    .open = neo_m8n_open,
    .close = neo_m8n_close,
    .ioctl = neo_m8n_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 UART 设备并挂 fops
 */
static int neo_m8n_probe(struct device* pdev)
{
    struct neo_m8n_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_neo_m8n_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_neo_m8n_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->uart_dev = device_get_parent(pdev);
    if (!dev->uart_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, dev) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    dev->ops = neo_m8n_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_neo_m8n_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int neo_m8n_remove(struct device* pdev)
{
    struct neo_m8n_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = neo_m8n_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_neo_m8n_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    neo_m8n_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_neo_m8n_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(neo_m8n, "u-blox,neo-m8n", neo_m8n_probe, neo_m8n_remove)
