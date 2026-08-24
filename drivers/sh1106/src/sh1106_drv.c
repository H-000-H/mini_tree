/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file sh1106_drv.c
 *@brief SH1106 OLED 驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_sh1106_pool[SH1106_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 sh1106_drv.h，寄存器定义见 sh1106_regs.h。
 *   数据流: VFS ioctl → sh1106_cmd_* → device_write(I2C) → HAL
 */

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "display_drv.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "sh1106_regs.h"
#include "status.h"
#include "system_log.h"
#include "vfs-i2c.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_SINOWEALTH_SH1106
#define DTC_GEN_COUNT_SINOWEALTH_SH1106 1
#endif
#define SH1106_POOL_COUNT DTC_GEN_COUNT_SINOWEALTH_SH1106

/** @brief SH1106 驱动实例（嵌入 fops） */
struct sh1106_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct sh1106_device s_sh1106_pool[SH1106_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_sh1106_used[SH1106_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_sh1106_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "sh1106";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void sh1106_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_sh1106_pool_ctrl, s_sh1106_used, SH1106_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct sh1106_device* sh1106_get_drvdata(struct device* pdev)
{
    return (struct sh1106_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @return MINI_OK 或 VFS_ERR_*
 */
static int sh1106_i2c_wr(struct sh1106_device* dev, const uint8_t* tx, size_t len,
                         uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !tx || len == 0U)
        return MINI_ERR_INVAL;
    return device_write(dev->i2c_dev, tx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int sh1106_hw_create(struct sh1106_device* dev)
{
    int ret;
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    ret = device_open(dev->i2c_dev, NULL);
    if (ret != MINI_OK)
        return ret;

    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void sh1106_hw_destroy(struct sh1106_device* dev)
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
static int sh1106_open(struct device* pdev, void* arg)
{
    struct sh1106_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = sh1106_get_drvdata(pdev);
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
        ret = sh1106_hw_create(dev);
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
static int sh1106_close(struct device* pdev)
{
    struct sh1106_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = sh1106_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        sh1106_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*sh1106_ioctl_fn_t)(struct sh1106_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct sh1106_ioctl_map
{
    sh1106_ioctl_fn_t handler;
};

/**
 * @brief 写 1B 命令/数据（ctrl 字节 + 值）
 */
static int sh1106_cmd_byte(struct sh1106_device* dev, uint8_t ctrl, uint8_t val,
                           uint32_t timeout_ms)
{
    uint8_t buf[2] = {ctrl, val};
    return sh1106_i2c_wr(dev, buf, 2, timeout_ms);
}

/**
 * @brief 定位到指定页（页 + 列地址命令序列，含列偏移）
 */
static int sh1106_set_page_col(struct sh1106_device* dev, uint8_t page, uint32_t timeout_ms)
{
    uint8_t col = SH1106_COL_OFFSET;
    int ret = sh1106_cmd_byte(dev, SH1106_I2C_CTRL_CMD,
                              (uint8_t)(SH1106_REG_SET_PAGE | (page & 0x07U)), timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = sh1106_cmd_byte(dev, SH1106_I2C_CTRL_CMD,
                          (uint8_t)(SH1106_REG_SET_COL_LO | (col & 0x0FU)), timeout_ms);
    if (ret != MINI_OK)
        return ret;
    return sh1106_cmd_byte(dev, SH1106_I2C_CTRL_CMD,
                           (uint8_t)(SH1106_REG_SET_COL_HI | ((col >> 4) & 0x0FU)), timeout_ms);
}

/**
 * @brief DISPLAY_CMD_CLEAR 实现：逐页填充 0x00/0xFF（0=灭 1=亮）
 */
static int sh1106_cmd_clear(struct sh1106_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    const struct display_clear_arg* darg = (const struct display_clear_arg*)arg;
    uint8_t val;
    int page_index;
    uint32_t to_ms = timeout_ms ? timeout_ms : 100U;
    uint8_t page_buf[1 + SH1106_WIDTH];
    size_t index;
    if (!dev->hw_ready || !darg || len != sizeof(*darg))
        return MINI_ERR_INVAL;
    val = darg->value ? 0xFFU : 0x00U;
    page_buf[0] = SH1106_I2C_CTRL_DATA;
    for (index = 1; index < sizeof(page_buf); index++)
        page_buf[index] = val;
    for (page_index = 0; page_index < SH1106_PAGES; page_index++)
    {
        int ret = sh1106_set_page_col(dev, (uint8_t)page_index, to_ms);
        if (ret != MINI_OK)
            return ret;
        ret = sh1106_i2c_wr(dev, page_buf, sizeof(page_buf), to_ms);
        if (ret != MINI_OK)
            return ret;
    }
    return MINI_OK;
}

/**
 * @brief DISPLAY_CMD_GET_INFO 实现：返回面板几何与像素格式
 */
static int sh1106_cmd_get_info(struct sh1106_device* dev, void* arg, size_t len,
                               uint32_t timeout_ms)
{
    struct display_info_arg* info = (struct display_info_arg*)arg;
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!info || len != sizeof(*info))
        return MINI_ERR_INVAL;
    info->width = SH1106_WIDTH;
    info->height = SH1106_HEIGHT;
    info->format = DISPLAY_FMT_MONO_1BPP;
    return MINI_OK;
}

/**
 * @brief DISPLAY_CMD_FILL_RECT 实现：单色屏仅支持全屏矩形
 */
static int sh1106_cmd_fill_rect(struct sh1106_device* dev, void* arg, size_t len,
                                uint32_t timeout_ms)
{
    const struct display_rect_arg* darg = (const struct display_rect_arg*)arg;
    struct display_clear_arg clear_arg;
    if (!dev->hw_ready || !darg || len != sizeof(*darg))
        return MINI_ERR_INVAL;
    if (darg->x != 0 || darg->y != 0 || darg->w != SH1106_WIDTH || darg->h != SH1106_HEIGHT)
        return MINI_ERR_INVAL;
    clear_arg.value = darg->color ? 1U : 0U;
    return sh1106_cmd_clear(dev, &clear_arg, sizeof(clear_arg), timeout_ms);
}

/**
 * @brief DISPLAY_CMD_DRAW_AREA 实现：单色屏仅支持整帧 page-major 位图
 */
static int sh1106_cmd_draw_area(struct sh1106_device* dev, void* arg, size_t len,
                                uint32_t timeout_ms)
{
    const struct display_draw_arg* darg = (const struct display_draw_arg*)arg;
    int page;
    uint32_t to_ms = timeout_ms ? timeout_ms : 100U;
    if (!dev->hw_ready || !darg || len != sizeof(*darg) || darg->format != DISPLAY_FMT_MONO_1BPP ||
        !darg->data)
        return MINI_ERR_INVAL;
    if (darg->x != 0 || darg->y != 0 || darg->w != SH1106_WIDTH || darg->h != SH1106_HEIGHT)
        return MINI_ERR_INVAL;
    for (page = 0; page < SH1106_PAGES; page++)
    {
        uint8_t chunk[1 + 64];
        size_t off = 0;
        if (sh1106_set_page_col(dev, (uint8_t)page, to_ms) != MINI_OK)
            return MINI_ERR_IO;
        chunk[0] = SH1106_I2C_CTRL_DATA;
        while (off < SH1106_WIDTH)
        {
            size_t count = SH1106_WIDTH - off;
            if (count > 64U)
                count = 64U;
            COMPAT_IGNORE_RESULT(
                COMPAT_MEM_COPY(&chunk[1], &darg->data[page * SH1106_WIDTH + off], count));
            if (sh1106_i2c_wr(dev, chunk, count + 1U, to_ms) != MINI_OK)
                return MINI_ERR_IO;
            off += count;
        }
    }
    return MINI_OK;
}

/**
 * @brief DISPLAY_CMD_FLUSH 实现：整帧 page-major 位图
 */
static int sh1106_cmd_flush(struct sh1106_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    const struct display_draw_arg* darg = (const struct display_draw_arg*)arg;
    if (!dev->hw_ready || !darg || len != sizeof(*darg) || darg->format != DISPLAY_FMT_MONO_1BPP ||
        !darg->data)
        return MINI_ERR_INVAL;
    return sh1106_cmd_draw_area(dev, (void*)darg, sizeof(*darg), timeout_ms);
}

/**
 * @brief DISPLAY_CMD_SET_BRIGHTNESS 实现：亮度映射为对比度
 */
static int sh1106_cmd_set_brightness(struct sh1106_device* dev, void* arg, size_t len,
                                     uint32_t timeout_ms)
{
    const struct display_bright_arg* darg = (const struct display_bright_arg*)arg;
    uint32_t to_ms = timeout_ms ? timeout_ms : 100U;
    if (!dev->hw_ready || !darg || len != sizeof(*darg))
        return MINI_ERR_INVAL;
    if (sh1106_cmd_byte(dev, SH1106_I2C_CTRL_CMD, SH1106_REG_SET_CONTRAST, to_ms) != MINI_OK)
        return MINI_ERR_IO;
    return sh1106_cmd_byte(dev, SH1106_I2C_CTRL_CMD, darg->value, to_ms);
}

static const struct sh1106_ioctl_map s_sh1106_map[DISPLAY_CMD_COUNT] = {
    [DISPLAY_CMD_GET_INFO - DISPLAY_CMD_BASE - 1] = {sh1106_cmd_get_info},
    [DISPLAY_CMD_CLEAR - DISPLAY_CMD_BASE - 1] = {sh1106_cmd_clear},
    [DISPLAY_CMD_FILL_RECT - DISPLAY_CMD_BASE - 1] = {sh1106_cmd_fill_rect},
    [DISPLAY_CMD_DRAW_AREA - DISPLAY_CMD_BASE - 1] = {sh1106_cmd_draw_area},
    [DISPLAY_CMD_FLUSH - DISPLAY_CMD_BASE - 1] = {sh1106_cmd_flush},
    [DISPLAY_CMD_SET_BRIGHTNESS - DISPLAY_CMD_BASE - 1] = {sh1106_cmd_set_brightness},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int sh1106_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct sh1106_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = sh1106_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)DISPLAY_CMD_BASE;
    if (off < 1 || off > DISPLAY_CMD_COUNT || !s_sh1106_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_sh1106_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations sh1106_fops = {
    .open = sh1106_open,
    .close = sh1106_close,
    .ioctl = sh1106_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int sh1106_probe(struct device* pdev)
{
    struct sh1106_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_sh1106_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_sh1106_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->i2c_dev = device_get_parent(pdev);
    if (!dev->i2c_dev)
    {
        ret = MINI_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = sh1106_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sh1106_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int sh1106_remove(struct device* pdev)
{
    struct sh1106_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = sh1106_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_sh1106_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    sh1106_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sh1106_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(sh1106, "sinowealth,sh1106", sh1106_probe, sh1106_remove)
