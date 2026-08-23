/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hc05_drv.c
 *@brief HC-05 蓝牙串口模块驱动实现 — 挂在 UART 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_hc05_pool[HC05_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 hc05_drv.h。
 *   数据流: VFS ioctl → hc05_cmd_send → device_write(UART) → HAL
 */

#include "hc05_drv.h"

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

#ifndef DTC_GEN_COUNT_HC05_BLE
#define DTC_GEN_COUNT_HC05_BLE 1
#endif
#define HC05_POOL_COUNT DTC_GEN_COUNT_HC05_BLE

/** @brief HC-05 驱动实例（嵌入 fops） */
struct hc05_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* uart_dev; /**< 所属 UART client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct hc05_device s_hc05_pool[HC05_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_hc05_used[HC05_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_hc05_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "hc05";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void hc05_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_hc05_pool_ctrl, s_hc05_used, HC05_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct hc05_device* hc05_get_drvdata(struct device* pdev)
{
    return (struct hc05_device*)device_get_priv(pdev);
}

/**
 * @brief UART 双向传输（UART_CMD_TRANSFER）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int hc05_uart_xchg(struct hc05_device* dev, const uint8_t* tx, size_t tx_len, uint8_t* rx,
                          size_t rx_len, uint32_t timeout_ms)
{
    struct uart_transfer_arg arg;
    if (!dev || !dev->uart_dev)
        return MINI_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.tx_len = tx_len;
    arg.rx_len = rx_len;
    return device_ioctl(dev->uart_dev, UART_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

/**
 * @brief 首次 open 时打开 UART 总线（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int hc05_hw_create(struct hc05_device* dev)
{
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    {
        int ret = device_open(dev->uart_dev, NULL);
        if (ret != MINI_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 UART client）
 */
static void hc05_hw_destroy(struct hc05_device* dev)
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
static int hc05_open(struct device* pdev, void* arg)
{
    struct hc05_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = hc05_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = MINI_OK;
    if (first == 1)
    {
        ret = hc05_hw_create(dev);
        if (ret != MINI_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return MINI_OK;
}

/**
 * @brief fops.close：引用计数关闭，末次调用释放硬件
 */
static int hc05_close(struct device* pdev)
{
    struct hc05_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = hc05_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        hc05_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*hc05_ioctl_fn_t)(struct hc05_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct hc05_ioctl_map
{
    hc05_ioctl_fn_t handler;
};

/**
 * @brief HC05_CMD_AT_SEND 实现：UART 发送 AT 命令
 */
static int hc05_cmd_send(struct hc05_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct hc05_at* a = (struct hc05_at*)arg;
    if (!dev->hw_ready || !a || len != sizeof(*a) || !a->tx || !a->tx_len)
        return MINI_ERR_INVAL;
    return device_write(dev->uart_dev, a->tx, a->tx_len, timeout_ms);
}
static const struct hc05_ioctl_map s_hc05_map[HC05_CMD_COUNT] = {
    [HC05_CMD_AT_SEND - HC05_CMD_BASE - 1] = {hc05_cmd_send},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int hc05_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct hc05_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = hc05_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)HC05_CMD_BASE;
    if (off < 1 || off > HC05_CMD_COUNT || !s_hc05_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_hc05_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations hc05_fops = {
    .open = hc05_open,
    .close = hc05_close,
    .ioctl = hc05_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 UART 设备并挂 fops
 */
static int hc05_probe(struct device* pdev)
{
    struct hc05_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_hc05_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_hc05_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->uart_dev = device_get_parent(pdev);
    if (!dev->uart_dev)
    {
        ret = MINI_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = hc05_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_hc05_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int hc05_remove(struct device* pdev)
{
    struct hc05_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = hc05_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_hc05_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    hc05_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_hc05_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(hc05, "hc05,ble", hc05_probe, hc05_remove)
