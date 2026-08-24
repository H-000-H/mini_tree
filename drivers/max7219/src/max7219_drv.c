/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file max7219_drv.c
 *@brief MAX7219 LED 点阵驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_max7219_pool[MAX7219_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 max7219_drv.h，寄存器定义见 max7219_regs.h。
 *   数据流: VFS ioctl → max7219_cmd_* → SPI transfer（vfs-spi）→ HAL
 */

#include "max7219_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-spi.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_MAXIM_MAX7219
#define DTC_GEN_COUNT_MAXIM_MAX7219 1
#endif
#define MAX7219_POOL_COUNT DTC_GEN_COUNT_MAXIM_MAX7219

/** @brief MAX7219 驱动实例（嵌入 fops） */
struct max7219_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* spi_dev; /**< 所属 SPI client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct max7219_device s_max7219_pool[MAX7219_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_max7219_used[MAX7219_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_max7219_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "max7219";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void max7219_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_max7219_pool_ctrl, s_max7219_used, MAX7219_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct max7219_device* max7219_get_drvdata(struct device* pdev)
{
    return (struct max7219_device*)device_get_priv(pdev);
}

/**
 * @brief SPI 全双工传输（AUTO 模式）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int max7219_spi_xfer(struct max7219_device* dev, const uint8_t* tx, uint8_t* rx, size_t len,
                            uint32_t timeout_ms)
{
    struct spi_transfer_arg arg;
    if (!dev || !dev->spi_dev || len == 0U)
        return MINI_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.len = len;
    arg.xfer_mode = SPI_XFER_AUTO;
    return device_ioctl(dev->spi_dev, SPI_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

/**
 * @brief 首次 open 时打开 SPI 总线（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int max7219_hw_create(struct max7219_device* dev)
{
    int ret;
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    ret = device_open(dev->spi_dev, NULL);
    if (ret != MINI_OK)
        return ret;

    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 SPI client）
 */
static void max7219_hw_destroy(struct max7219_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;

    if (dev->spi_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->spi_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int max7219_open(struct device* pdev, void* arg)
{
    struct max7219_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = max7219_get_drvdata(pdev);
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
        ret = max7219_hw_create(dev);
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
static int max7219_close(struct device* pdev)
{
    struct max7219_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = max7219_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        max7219_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*max7219_ioctl_fn_t)(struct max7219_device* dev, void* arg, size_t arg_len,
                                  uint32_t ms);
struct max7219_ioctl_map
{
    max7219_ioctl_fn_t handler;
};

/**
 * @brief 写寄存器（addr + data 一次传输）
 */
static int max7219_wr(struct max7219_device* dev, uint8_t addr, uint8_t data, uint32_t timeout_ms)
{
    uint8_t tx[2] = {addr, data};
    return max7219_spi_xfer(dev, tx, NULL, 2, timeout_ms);
}
/**
 * @brief MAX7219_CMD_INIT 实现：退出关断 + 全扫描 + 亮度/解码/测试配置
 */
static int max7219_cmd_init(struct max7219_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    int ret;
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(len);
    if (!dev->hw_ready)
        return MINI_ERR_INVAL;
    ret = max7219_wr(dev, MAX7219_REG_SHUTDOWN, 0x01, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = max7219_wr(dev, MAX7219_REG_SCAN_LIMIT, 0x07, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = max7219_wr(dev, MAX7219_REG_INTENSITY, 0x08, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = max7219_wr(dev, MAX7219_REG_DECODE, 0x00, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    return max7219_wr(dev, MAX7219_REG_DISPLAY_TEST, 0x00, timeout_ms);
}
/**
 * @brief MAX7219_CMD_SET_DIGIT 实现：写单个位
 */
static int max7219_cmd_digit(struct max7219_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct max7219_digit* digit_arg = (struct max7219_digit*)arg;
    if (!dev->hw_ready || !digit_arg || len != sizeof(*digit_arg) ||
        digit_arg->digit < MAX7219_REG_DIGIT0 || digit_arg->digit > MAX7219_REG_DIGIT7)
        return MINI_ERR_INVAL;
    return max7219_wr(dev, digit_arg->digit, digit_arg->value, timeout_ms);
}
/**
 * @brief MAX7219_CMD_CLEAR 实现：全部位清零
 */
static int max7219_cmd_clear(struct max7219_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    int index;
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(len);
    if (!dev->hw_ready)
        return MINI_ERR_INVAL;
    for (index = (int)MAX7219_REG_DIGIT0; index <= (int)MAX7219_REG_DIGIT7; index++)
    {
        int ret = max7219_wr(dev, (uint8_t)index, 0x00, timeout_ms);
        if (ret != MINI_OK)
            return ret;
    }
    return MINI_OK;
}
/**
 * @brief MAX7219_CMD_FLUSH_FB 实现：整帧逐位写入
 */
static int max7219_cmd_flush_fb(struct max7219_device* dev, void* arg, size_t len,
                                uint32_t timeout_ms)
{
    struct max7219_fb* fb_arg = (struct max7219_fb*)arg;
    int index;
    if (!dev->hw_ready || !fb_arg || len != sizeof(*fb_arg) || !fb_arg->rows ||
        fb_arg->len < MAX7219_MATRIX_BYTES)
        return MINI_ERR_INVAL;
    for (index = 0; index < MAX7219_DIGITS; index++)
    {
        int ret =
            max7219_wr(dev, (uint8_t)(MAX7219_REG_DIGIT0 + index), fb_arg->rows[index], timeout_ms);
        if (ret != MINI_OK)
            return ret;
    }
    return MINI_OK;
}

static const struct max7219_ioctl_map s_max7219_map[MAX7219_CMD_COUNT] = {
    [MAX7219_CMD_INIT - MAX7219_CMD_BASE - 1] = {max7219_cmd_init},
    [MAX7219_CMD_SET_DIGIT - MAX7219_CMD_BASE - 1] = {max7219_cmd_digit},
    [MAX7219_CMD_CLEAR - MAX7219_CMD_BASE - 1] = {max7219_cmd_clear},
    [MAX7219_CMD_FLUSH_FB - MAX7219_CMD_BASE - 1] = {max7219_cmd_flush_fb},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int max7219_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct max7219_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = max7219_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)MAX7219_CMD_BASE;
    if (off < 1 || off > MAX7219_CMD_COUNT || !s_max7219_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_max7219_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations max7219_fops = {
    .open = max7219_open,
    .close = max7219_close,
    .ioctl = max7219_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 SPI 设备并挂 fops
 */
static int max7219_probe(struct device* pdev)
{
    struct max7219_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_max7219_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_max7219_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->spi_dev = device_get_parent(pdev);
    if (!dev->spi_dev)
    {
        ret = MINI_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = max7219_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_max7219_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int max7219_remove(struct device* pdev)
{
    struct max7219_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = max7219_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_max7219_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    max7219_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_max7219_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(max7219, "maxim,max7219", max7219_probe, max7219_remove)
