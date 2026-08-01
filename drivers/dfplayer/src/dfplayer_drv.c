/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file dfplayer_drv.c
 * @brief DFPlayer MP3 模块驱动实现 — 挂在 UART 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_dfplayer_pool[DFPLAYER_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与参数结构见 dfplayer_drv.h，帧格式见 dfplayer_regs.h。
 *
 * 数据流: VFS ioctl → dfplayer_cmd_* → dfplayer_frame → device_write(UART) → HAL
 */
#include "dfplayer_drv.h"
#include "dfplayer_regs.h"
#include "vfs-uart.h"

#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "status.h"
#include "dt_config_gen.h"
#include "compiler_compat.h"
#include "osal.h"
#include "system_log.h"
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_DFROBOT_DFPLAYER
#define DTC_GEN_COUNT_DFROBOT_DFPLAYER  1
#endif
#define DFPLAYER_POOL_COUNT  DTC_GEN_COUNT_DFROBOT_DFPLAYER

/** @brief DFPlayer 驱动实例（嵌入 fops） */
struct dfplayer_device
{
    struct file_operations ops;      /**< 挂入 device 的 fops */
    struct device*         uart_dev; /**< 所属 UART client 设备 */

    int                    hw_ready; /**< 硬件已初始化标志 */
};

static struct dfplayer_device s_dfplayer_pool[DFPLAYER_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_dfplayer_used[DFPLAYER_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_dfplayer_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "dfplayer";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160)
static void dfplayer_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_dfplayer_pool_ctrl, s_dfplayer_used, DFPLAYER_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param dev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct dfplayer_device* dfplayer_get_drvdata(struct device* dev)
{
    return (struct dfplayer_device*)device_get_priv(dev);
}


/**
 * @brief 向 UART 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int dfplayer_uart_wr(struct dfplayer_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->uart_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->uart_dev, tx, len, to);
}
/**
 * @brief 从 UART 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int dfplayer_uart_rd(struct dfplayer_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->uart_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->uart_dev, rx, len, to);
}


/**
 * @brief 首次 open 时打开 UART 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int dfplayer_hw_create(struct dfplayer_device* d)
{
    int r;
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    r = device_open(d->uart_dev, NULL);
    if (r != VFS_OK)
        return r;

    d->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 UART client）
 */
static void dfplayer_hw_destroy(struct dfplayer_device* d)
{
    if (!d || !d->hw_ready)
        return;

    if (d->uart_dev)
        COMPAT_IGNORE_RESULT(device_close(d->uart_dev));
    d->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int dfplayer_open(struct device* dev, void* arg)
{
    struct dfplayer_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = dfplayer_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = dfplayer_hw_create(d);
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
static int dfplayer_close(struct device* dev)
{
    struct dfplayer_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = dfplayer_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        dfplayer_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*dfplayer_ioctl_fn_t)(struct dfplayer_device* d, void* arg, size_t arg_len, uint32_t ms);
struct dfplayer_ioctl_map { dfplayer_ioctl_fn_t handler; };


/**
 * @brief 组装并发送一帧命令（起始/版本/长度/反馈 + 参数 + 校验 + 结束）
 */
static int dfplayer_frame(struct dfplayer_device* d, uint8_t cmd, uint16_t param, uint32_t to)
{
    uint8_t f[10];
    uint16_t sum;
    f[0] = DFPLAYER_FRAME_START;
    f[1] = DFPLAYER_FRAME_VER;
    f[2] = DFPLAYER_FRAME_LEN;
    f[3] = cmd;
    f[4] = DFPLAYER_FRAME_FEEDBACK;
    f[5] = (uint8_t)(param >> 8);
    f[6] = (uint8_t)param;
    sum = (uint16_t)(0xFFFF - (f[1] + f[2] + f[3] + f[4] + f[5] + f[6]) + 1);
    f[7] = (uint8_t)(sum >> 8);
    f[8] = (uint8_t)sum;
    f[9] = DFPLAYER_FRAME_END;
    return dfplayer_uart_wr(d, f, sizeof(f), to);
}

/**
 * @brief DFPLAYER_CMD_PLAY 实现：播放指定曲目
 */
static int dfplayer_cmd_play(struct dfplayer_device* d, void* arg, size_t len, uint32_t to)
{
    struct dfplayer_track* a = (struct dfplayer_track*)arg;
    if (!d->hw_ready || !a || len != sizeof(*a))
        return VFS_ERR_INVAL;
    return dfplayer_frame(d, DFPLAYER_OP_PLAY_TRACK, a->track, to);
}

/**
 * @brief DFPLAYER_CMD_VOLUME 实现：设置音量（0..30）
 */
static int dfplayer_cmd_vol(struct dfplayer_device* d, void* arg, size_t len, uint32_t to)
{
    uint8_t* v = (uint8_t*)arg;
    if (!d->hw_ready || !v || len != sizeof(uint8_t) || *v > 30U)
        return VFS_ERR_INVAL;
    return dfplayer_frame(d, DFPLAYER_OP_SET_VOL, *v, to);
}


static const struct dfplayer_ioctl_map s_dfplayer_map[DFPLAYER_CMD_COUNT] = {
    [DFPLAYER_CMD_PLAY - DFPLAYER_CMD_BASE - 1] = { dfplayer_cmd_play },
    [DFPLAYER_CMD_VOLUME - DFPLAYER_CMD_BASE - 1] = { dfplayer_cmd_vol },
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int dfplayer_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct dfplayer_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = dfplayer_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)DFPLAYER_CMD_BASE;
    if (off < 1 || off > DFPLAYER_CMD_COUNT || !s_dfplayer_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_dfplayer_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations dfplayer_fops = {
    .open  = dfplayer_open,
    .close = dfplayer_close,
    .ioctl = dfplayer_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 UART 设备并挂 fops
 */
static int dfplayer_probe(struct device* dev)
{
    struct dfplayer_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_dfplayer_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_dfplayer_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->uart_dev = device_get_parent(dev);
    if (!d->uart_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = dfplayer_fops;
    dev->ops = &d->ops;
    SYS_LOGI(k_tag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_dfplayer_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int dfplayer_remove(struct device* dev)
{
    struct dfplayer_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = dfplayer_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_dfplayer_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    dfplayer_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_dfplayer_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(dfplayer, "dfrobot,dfplayer", dfplayer_probe, dfplayer_remove)
