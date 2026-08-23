/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file dfplayer_drv.c
 *@brief DFPlayer MP3 模块驱动实现 — 挂在 UART 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_dfplayer_pool[DFPLAYER_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 dfplayer_drv.h，帧格式见 dfplayer_regs.h。
 *   数据流: VFS ioctl → dfplayer_cmd_* → dfplayer_frame → device_write(UART) → HAL
 */

#include "dfplayer_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "dfplayer_regs.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-uart.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_DFROBOT_DFPLAYER
#define DTC_GEN_COUNT_DFROBOT_DFPLAYER 1
#endif
#define DFPLAYER_POOL_COUNT DTC_GEN_COUNT_DFROBOT_DFPLAYER

/** @brief DFPlayer 驱动实例（嵌入 fops） */
struct dfplayer_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* uart_dev; /**< 所属 UART client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct dfplayer_device s_dfplayer_pool[DFPLAYER_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_dfplayer_used[DFPLAYER_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_dfplayer_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "dfplayer";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void dfplayer_pool_boot_init(void) { COMPAT_IGNORE_RESULT(osal_pool_init(&s_dfplayer_pool_ctrl, s_dfplayer_used, DFPLAYER_POOL_COUNT)); }

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct dfplayer_device* dfplayer_get_drvdata(struct device* pdev) { return (struct dfplayer_device*)device_get_priv(pdev); }

/**
 * @brief 向 UART 总线写数据
 * @return MINI_OK 或 VFS_ERR_*
 */
static int dfplayer_uart_wr(struct dfplayer_device* dev, const uint8_t* tx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->uart_dev || !tx || len == 0U)
        return MINI_ERR_INVAL;
    return device_write(dev->uart_dev, tx, len, timeout_ms);
}
/**
 * @brief 从 UART 总线读数据
 * @return MINI_OK 或 VFS_ERR_*
 */
static int dfplayer_uart_rd(struct dfplayer_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->uart_dev || !rx || len == 0U)
        return MINI_ERR_INVAL;
    return device_read(dev->uart_dev, rx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 UART 总线（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int dfplayer_hw_create(struct dfplayer_device* dev)
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
static void dfplayer_hw_destroy(struct dfplayer_device* dev)
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
static int dfplayer_open(struct device* pdev, void* arg)
{
    struct dfplayer_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = dfplayer_get_drvdata(pdev);
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
        ret = dfplayer_hw_create(dev);
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
static int dfplayer_close(struct device* pdev)
{
    struct dfplayer_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = dfplayer_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        dfplayer_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

typedef int (*dfplayer_ioctl_fn_t)(struct dfplayer_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct dfplayer_ioctl_map
{
    dfplayer_ioctl_fn_t handler;
};

/**
 * @brief 组装并发送一帧命令（起始/版本/长度/反馈 + 参数 + 校验 + 结束）
 */
static int dfplayer_frame(struct dfplayer_device* dev, uint8_t cmd, uint16_t param, uint32_t timeout_ms)
{
    uint8_t frame[10];
    uint16_t sum;
    frame[0] = DFPLAYER_FRAME_START;
    frame[1] = DFPLAYER_FRAME_VER;
    frame[2] = DFPLAYER_FRAME_LEN;
    frame[3] = cmd;
    frame[4] = DFPLAYER_FRAME_FEEDBACK;
    frame[5] = (uint8_t)(param >> 8);
    frame[6] = (uint8_t)param;
    sum = (uint16_t)(0xFFFF - (frame[1] + frame[2] + frame[3] + frame[4] + frame[5] + frame[6]) + 1);
    frame[7] = (uint8_t)(sum >> 8);
    frame[8] = (uint8_t)sum;
    frame[9] = DFPLAYER_FRAME_END;
    return dfplayer_uart_wr(dev, frame, sizeof(frame), timeout_ms);
}

/**
 * @brief DFPLAYER_CMD_PLAY 实现：播放指定曲目
 */
static int dfplayer_cmd_play(struct dfplayer_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct dfplayer_track* a = (struct dfplayer_track*)arg;
    if (!dev->hw_ready || !a || len != sizeof(*a))
        return MINI_ERR_INVAL;
    return dfplayer_frame(dev, DFPLAYER_OP_PLAY_TRACK, a->track, timeout_ms);
}

/**
 * @brief DFPLAYER_CMD_VOLUME 实现：设置音量（0..30）
 */
static int dfplayer_cmd_vol(struct dfplayer_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    uint8_t* val = (uint8_t*)arg;
    if (!dev->hw_ready || !val || len != sizeof(uint8_t) || *val > 30U)
        return MINI_ERR_INVAL;
    return dfplayer_frame(dev, DFPLAYER_OP_SET_VOL, *val, timeout_ms);
}

static const struct dfplayer_ioctl_map s_dfplayer_map[DFPLAYER_CMD_COUNT] = {
    [DFPLAYER_CMD_PLAY - DFPLAYER_CMD_BASE - 1] = {dfplayer_cmd_play},
    [DFPLAYER_CMD_VOLUME - DFPLAYER_CMD_BASE - 1] = {dfplayer_cmd_vol},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int dfplayer_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct dfplayer_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = dfplayer_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)DFPLAYER_CMD_BASE;
    if (off < 1 || off > DFPLAYER_CMD_COUNT || !s_dfplayer_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_dfplayer_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations dfplayer_fops = {
    .open = dfplayer_open,
    .close = dfplayer_close,
    .ioctl = dfplayer_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 UART 设备并挂 fops
 */
static int dfplayer_probe(struct device* pdev)
{
    struct dfplayer_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_dfplayer_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_dfplayer_pool[pool_idx];
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
    dev->ops = dfplayer_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_dfplayer_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int dfplayer_remove(struct device* pdev)
{
    struct dfplayer_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = dfplayer_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_dfplayer_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    dfplayer_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_dfplayer_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(dfplayer, "dfrobot,dfplayer", dfplayer_probe, dfplayer_remove)
