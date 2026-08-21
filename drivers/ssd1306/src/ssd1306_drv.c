/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file ssd1306_drv.c
 *@brief SSD1306 OLED 驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_ssd1306_pool[SSD1306_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 ssd1306_drv.h，寄存器定义见 ssd1306_regs.h。
 *   数据流: VFS ioctl → ssd1306_cmd_* → device_write(I2C) → HAL
 */

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "display_drv.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "ssd1306_regs.h"
#include "status.h"
#include "system_log.h"
#include "vfs-i2c.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_SOLOMON_SSD1306
#define DTC_GEN_COUNT_SOLOMON_SSD1306 1
#endif
#define SSD1306_POOL_COUNT DTC_GEN_COUNT_SOLOMON_SSD1306

/** @brief SSD1306 驱动实例（嵌入 fops） */
struct ssd1306_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct ssd1306_device s_ssd1306_pool[SSD1306_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_ssd1306_used[SSD1306_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_ssd1306_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "ssd1306";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void ssd1306_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_ssd1306_pool_ctrl, s_ssd1306_used, SSD1306_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct ssd1306_device* ssd1306_get_drvdata(struct device* pdev)
{
    return (struct ssd1306_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ssd1306_i2c_wr(struct ssd1306_device* dev, const uint8_t* tx, size_t len,
                          uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(dev->i2c_dev, tx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ssd1306_hw_create(struct ssd1306_device* dev)
{
    if (!dev)
        return VFS_ERR_INVAL;
    if (dev->hw_ready)
        return VFS_OK;
    {
        int ret = device_open(dev->i2c_dev, NULL);
        if (ret != VFS_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void ssd1306_hw_destroy(struct ssd1306_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->i2c_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->i2c_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int ssd1306_open(struct device* pdev, void* arg)
{
    struct ssd1306_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = ssd1306_get_drvdata(pdev);
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
        ret = ssd1306_hw_create(dev);
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
static int ssd1306_close(struct device* pdev)
{
    struct ssd1306_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = ssd1306_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ssd1306_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*ssd1306_ioctl_fn_t)(struct ssd1306_device* dev, void* arg, size_t arg_len,
                                  uint32_t ms);
struct ssd1306_ioctl_map
{
    ssd1306_ioctl_fn_t handler;
};

/**
 * @brief 写 1B 命令/数据（ctrl 字节 + 值）
 */
static int ssd1306_wr_ctrl(struct ssd1306_device* dev, uint8_t ctrl, uint8_t val,
                           uint32_t timeout_ms)
{
    uint8_t tx[2] = {ctrl, val};
    return ssd1306_i2c_wr(dev, tx, 2, timeout_ms);
}

/**
 * @brief DISPLAY_CMD_CLEAR 实现：逐页填充指定值（0=灭 1=亮）
 */
static int ssd1306_cmd_clear(struct ssd1306_device* dev, void* arg, size_t len, uint32_t ms)
{
    const struct display_clear_arg* darg = (const struct display_clear_arg*)arg;
    uint8_t page_buf[1 + SSD1306_WIDTH];
    size_t i;
    int page;
    uint8_t fill_val;
    uint32_t timeout_ms = ms ? ms : 100U;
    if (!dev->hw_ready || !darg || len != sizeof(*darg))
        return VFS_ERR_INVAL;
    fill_val = darg->value ? 0xFFU : 0x00U;
    page_buf[0] = SSD1306_I2C_CTRL_DATA;
    for (i = 1; i < sizeof(page_buf); i++)
        page_buf[i] = fill_val;
    for (page = 0; page < SSD1306_PAGES; page++)
    {
        if (ssd1306_wr_ctrl(dev, SSD1306_I2C_CTRL_CMD, (uint8_t)(SSD1306_REG_SET_PAGE | page),
                            timeout_ms) != VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_wr_ctrl(dev, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_COL_LO, timeout_ms) !=
            VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_wr_ctrl(dev, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_COL_HI, timeout_ms) !=
            VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_i2c_wr(dev, page_buf, sizeof(page_buf), timeout_ms) != VFS_OK)
            return VFS_ERR_IO;
    }
    return VFS_OK;
}

/**
 * @brief DISPLAY_CMD_GET_INFO 实现：返回面板几何与像素格式
 */
static int ssd1306_cmd_get_info(struct ssd1306_device* dev, void* arg, size_t len, uint32_t ms)
{
    struct display_info_arg* info = (struct display_info_arg*)arg;
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(ms);
    if (!info || len != sizeof(*info))
        return VFS_ERR_INVAL;
    info->width = SSD1306_WIDTH;
    info->height = SSD1306_HEIGHT;
    info->format = DISPLAY_FMT_MONO_1BPP;
    return VFS_OK;
}

/**
 * @brief DISPLAY_CMD_FILL_RECT 实现：单色屏仅支持全屏矩形
 */
static int ssd1306_cmd_fill_rect(struct ssd1306_device* dev, void* arg, size_t len, uint32_t ms)
{
    const struct display_rect_arg* darg = (const struct display_rect_arg*)arg;
    struct display_clear_arg clear_arg;
    if (!dev->hw_ready || !darg || len != sizeof(*darg))
        return VFS_ERR_INVAL;
    if (darg->x != 0 || darg->y != 0 || darg->w != SSD1306_WIDTH || darg->h != SSD1306_HEIGHT)
        return VFS_ERR_INVAL;
    clear_arg.value = darg->color ? 1U : 0U;
    return ssd1306_cmd_clear(dev, &clear_arg, sizeof(clear_arg), ms);
}

/**
 * @brief DISPLAY_CMD_DRAW_AREA 实现：单色屏仅支持整帧 page-major 位图
 */
static int ssd1306_cmd_draw_area(struct ssd1306_device* dev, void* arg, size_t len, uint32_t ms)
{
    const struct display_draw_arg* darg = (const struct display_draw_arg*)arg;
    int page;
    uint32_t timeout_ms = ms ? ms : 100U;
    if (!dev->hw_ready || !darg || len != sizeof(*darg) || darg->format != DISPLAY_FMT_MONO_1BPP ||
        !darg->data)
        return VFS_ERR_INVAL;
    if (darg->x != 0 || darg->y != 0 || darg->w != SSD1306_WIDTH || darg->h != SSD1306_HEIGHT)
        return VFS_ERR_INVAL;
    for (page = 0; page < SSD1306_PAGES; page++)
    {
        uint8_t chunk[1 + 64];
        size_t off = 0;
        if (ssd1306_wr_ctrl(dev, SSD1306_I2C_CTRL_CMD, (uint8_t)(SSD1306_REG_SET_PAGE | page),
                            timeout_ms) != VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_wr_ctrl(dev, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_COL_LO, timeout_ms) !=
            VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_wr_ctrl(dev, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_COL_HI, timeout_ms) !=
            VFS_OK)
            return VFS_ERR_IO;
        chunk[0] = SSD1306_I2C_CTRL_DATA;
        while (off < SSD1306_WIDTH)
        {
            size_t chunk_len = SSD1306_WIDTH - off;
            if (chunk_len > 64U)
                chunk_len = 64U;
            COMPAT_IGNORE_RESULT(
                COMPAT_MEM_COPY(&chunk[1], &darg->data[page * SSD1306_WIDTH + off], chunk_len));
            if (ssd1306_i2c_wr(dev, chunk, chunk_len + 1U, timeout_ms) != VFS_OK)
                return VFS_ERR_IO;
            off += chunk_len;
        }
    }
    return VFS_OK;
}

/**
 * @brief DISPLAY_CMD_FLUSH 实现：整帧 page-major 位图
 */
static int ssd1306_cmd_flush(struct ssd1306_device* dev, void* arg, size_t len, uint32_t ms)
{
    const struct display_draw_arg* darg = (const struct display_draw_arg*)arg;
    if (!dev->hw_ready || !darg || len != sizeof(*darg) || darg->format != DISPLAY_FMT_MONO_1BPP ||
        !darg->data)
        return VFS_ERR_INVAL;
    return ssd1306_cmd_draw_area(dev, (void*)darg, sizeof(*darg), ms);
}

/**
 * @brief DISPLAY_CMD_SET_BRIGHTNESS 实现：亮度映射为对比度
 */
static int ssd1306_cmd_set_brightness(struct ssd1306_device* dev, void* arg, size_t len,
                                      uint32_t ms)
{
    const struct display_bright_arg* darg = (const struct display_bright_arg*)arg;
    uint32_t timeout_ms = ms ? ms : 100U;
    if (!dev->hw_ready || !darg || len != sizeof(*darg))
        return VFS_ERR_INVAL;
    if (ssd1306_wr_ctrl(dev, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_CONTRAST, timeout_ms) != VFS_OK)
        return VFS_ERR_IO;
    return ssd1306_wr_ctrl(dev, SSD1306_I2C_CTRL_CMD, darg->value, timeout_ms);
}

static const struct ssd1306_ioctl_map s_ssd1306_map[DISPLAY_CMD_COUNT] = {
    [DISPLAY_CMD_GET_INFO - DISPLAY_CMD_BASE - 1] = {ssd1306_cmd_get_info},
    [DISPLAY_CMD_CLEAR - DISPLAY_CMD_BASE - 1] = {ssd1306_cmd_clear},
    [DISPLAY_CMD_FILL_RECT - DISPLAY_CMD_BASE - 1] = {ssd1306_cmd_fill_rect},
    [DISPLAY_CMD_DRAW_AREA - DISPLAY_CMD_BASE - 1] = {ssd1306_cmd_draw_area},
    [DISPLAY_CMD_FLUSH - DISPLAY_CMD_BASE - 1] = {ssd1306_cmd_flush},
    [DISPLAY_CMD_SET_BRIGHTNESS - DISPLAY_CMD_BASE - 1] = {ssd1306_cmd_set_brightness},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int ssd1306_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ssd1306_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = ssd1306_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)DISPLAY_CMD_BASE;
    if (off < 1 || off > DISPLAY_CMD_COUNT || !s_ssd1306_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_ssd1306_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ssd1306_fops = {
    .open = ssd1306_open,
    .close = ssd1306_close,
    .ioctl = ssd1306_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int ssd1306_probe(struct device* pdev)
{
    struct ssd1306_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ssd1306_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_ssd1306_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->i2c_dev = device_get_parent(pdev);
    if (!dev->i2c_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, dev) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    dev->ops = ssd1306_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ssd1306_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int ssd1306_remove(struct device* pdev)
{
    struct ssd1306_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = ssd1306_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_ssd1306_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    ssd1306_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ssd1306_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(ssd1306, "solomon,ssd1306", ssd1306_probe, ssd1306_remove)
