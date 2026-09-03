/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file pn532_drv.c
 *@brief PN532 NFC 模块驱动实现 — 挂在 UART（HSU）总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_pn532_pool[PN532_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 pn532_drv.h。
 *   数据流: VFS ioctl → pn532_cmd_fw → device_read/write(UART) → HAL
 */

#include "pn532_drv.h"

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

#ifndef DTC_GEN_COUNT_NXP_PN532_HSU
#define DTC_GEN_COUNT_NXP_PN532_HSU 1
#endif
#define PN532_POOL_COUNT DTC_GEN_COUNT_NXP_PN532_HSU

/** @brief PN532 驱动实例（嵌入 fops） */
struct pn532_device
{
    struct file_operations ops;      /**< 挂入 device 的 fops */
    struct device*         uart_dev; /**< 所属 UART client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct pn532_device           s_pn532_pool[PN532_POOL_COUNT] MINI_ALIGNED(4);
static uint8_t                       s_pn532_used[PN532_POOL_COUNT] MINI_ALIGNED(4);
static osal_pool_t s_pn532_pool_ctrl MINI_ALIGNED(4);
static const char* const             k_tag = "pn532";

/**
 * @brief 驱动池启动初始化（mini_pre_execution 阶段，创建静态对象池）
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_DRIVER_POOL) static void pn532_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_pn532_pool_ctrl, s_pn532_used, PN532_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct pn532_device* pn532_get_drvdata(struct device* pdev) { return (struct pn532_device*)device_get_priv(pdev); }

/**
 * @brief 向 UART 总线写数据
 * @return MINI_OK 或 VFS_ERR_*
 */
static int pn532_uart_wr(struct pn532_device* dev, const uint8_t* tx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->uart_dev || !tx || len == 0U)
        return MINI_ERR_INVAL;
    return device_write(dev->uart_dev, tx, len, timeout_ms);
}
/**
 * @brief 从 UART 总线读数据
 * @return 读取字节数或 VFS_ERR_*
 */
static int pn532_uart_rd(struct pn532_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->uart_dev || !rx || len == 0U)
        return MINI_ERR_INVAL;
    return device_read(dev->uart_dev, rx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 UART 总线（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int pn532_hw_create(struct pn532_device* dev)
{
    int ret;
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    ret = device_open(dev->uart_dev, NULL);
    if (ret != MINI_OK)
        return ret;

    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 UART client）
 */
static void pn532_hw_destroy(struct pn532_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;

    if (dev->uart_dev)
        MINI_IGNORE_RESULT(device_close(dev->uart_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int pn532_open(struct device* pdev, void* arg)
{
    struct pn532_device*  dev;
    struct dev_lifecycle* lc;
    int                   first, ret;
    MINI_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = pn532_get_drvdata(pdev);
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
        ret = pn532_hw_create(dev);
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
static int pn532_close(struct device* pdev)
{
    struct pn532_device*  dev;
    struct dev_lifecycle* lc;
    int                   last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = pn532_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        pn532_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*pn532_ioctl_fn_t)(struct pn532_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct pn532_ioctl_map
{
    pn532_ioctl_fn_t handler;
};

/**
 * @brief PN532_CMD_GET_FIRMWARE 实现：HSU 唤醒 + GetFirmwareVersion 帧解析
 */
static int pn532_cmd_fw(struct pn532_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    /* HSU wake + GetFirmwareVersion 帧 */
    static const uint8_t wake[] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t cmd[] = {0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00};
    uint8_t              rx[32];
    struct pn532_fw*     fw = (struct pn532_fw*)arg;
    int                  ret;
    if (!dev->hw_ready || !fw || len != sizeof(*fw))
        return MINI_ERR_INVAL;
    ret = pn532_uart_wr(dev, wake, sizeof(wake), timeout_ms);
    if (ret != MINI_OK)
        return ret;
    osal_delay_ms(10);
    ret = pn532_uart_wr(dev, cmd, sizeof(cmd), timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = pn532_uart_rd(dev, rx, sizeof(rx), timeout_ms);
    if (ret < 0)
        return ret;
    if (ret < 13)
        return MINI_ERR_IO;
    fw->ic = rx[9];
    fw->ver = rx[10];
    fw->rev = rx[11];
    fw->support = rx[12];
    return MINI_OK;
}

static const struct pn532_ioctl_map s_pn532_map[PN532_CMD_COUNT] = {
    [PN532_CMD_GET_FIRMWARE - PN532_CMD_BASE - 1] = {pn532_cmd_fw},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int pn532_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct pn532_device*  dev;
    struct dev_lifecycle* lc;
    int32_t               off;
    int                   ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = pn532_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)PN532_CMD_BASE;
    if (off < 1 || off > PN532_CMD_COUNT || !s_pn532_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_pn532_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations pn532_fops = {
    .open = pn532_open,
    .close = pn532_close,
    .ioctl = pn532_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 UART 设备并挂 fops
 */
static int pn532_probe(struct device* pdev)
{
    struct pn532_device* dev;
    int                  pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_pn532_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_pn532_pool[pool_idx];
    MINI_MEM_SET(dev, 0, sizeof(*dev));
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
    dev->ops = pn532_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_pn532_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int pn532_remove(struct device* pdev)
{
    struct pn532_device*  dev;
    struct dev_lifecycle* lc;
    int                   idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = pn532_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_pn532_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    pn532_hw_destroy(dev);
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_pn532_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(pn532, "nxp,pn532-hsu", pn532_probe, pn532_remove)
