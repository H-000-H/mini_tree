/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file epaper_drv.c
 *@brief 电子纸驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_epaper_pool[EPAPER_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 epaper_drv.h。
 *   引脚: DC/RST/BUSY 均为 GPIO（phandle: dc-gpio / reset-gpio / busy-gpio）；
 *   数据流: VFS ioctl → epaper_cmd_* → SPI transfer（vfs-spi）→ HAL
 */

#include "epaper_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "display_drv.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "epaper_regs.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"
#include "vfs-spi.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_GOODDISPLAY_EPAPER
#define DTC_GEN_COUNT_GOODDISPLAY_EPAPER 1
#endif
#define EPAPER_POOL_COUNT DTC_GEN_COUNT_GOODDISPLAY_EPAPER

/** @brief 电子纸驱动实例（嵌入 fops 与全部引脚） */
struct epaper_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* spi_dev; /**< 所属 SPI client 设备 */
    struct device* dc_dev; /**< DC 引脚 GPIO 设备 */
    struct device* rst_dev; /**< RST 引脚 GPIO 设备 */
    struct device* busy_dev; /**< BUSY 引脚 GPIO 设备 */
    struct vfs_gpio_arg dc_gpio; /**< DC 引脚操作参数 */
    struct vfs_gpio_arg rst_gpio; /**< RST 引脚操作参数 */
    struct vfs_gpio_arg busy_gpio; /**< BUSY 引脚操作参数 */

    int width; /**< 面板宽（像素，DTS: width） */
    int height; /**< 面板高（像素，DTS: height） */
    uint32_t busy_timeout_ms; /**< BUSY 等待超时（DTS: busy-timeout-ms，可选） */
    int hw_ready; /**< 硬件已初始化标志 */
};

static struct epaper_device s_epaper_pool[EPAPER_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_epaper_used[EPAPER_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_epaper_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "epaper";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void epaper_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_epaper_pool_ctrl, s_epaper_used, EPAPER_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct epaper_device* epaper_get_drvdata(struct device* pdev)
{
    return (struct epaper_device*)device_get_priv(pdev);
}

/**
 * @brief SPI 全双工传输（AUTO 模式）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int epaper_spi_xfer(struct epaper_device* dev, const uint8_t* tx, uint8_t* rx, size_t len,
                           uint32_t timeout_ms)
{
    struct spi_transfer_arg arg;
    if (!dev || !dev->spi_dev || len == 0U)
        return VFS_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.len = len;
    arg.xfer_mode = SPI_XFER_AUTO;
    return device_ioctl(dev->spi_dev, SPI_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

/**
 * @brief 设置 DC 引脚（命令/数据选择）
 */
static int epaper_dc(struct epaper_device* dev, int data)
{
    dev->dc_gpio.level = data ? 1 : 0;
    return vfs_gpio_set_level(&dev->dc_gpio);
}
/**
 * @brief 等待 BUSY 释放（低电平表示空闲）
 * @return VFS_OK 或 VFS_ERR_BUSY（超时）
 */
static int epaper_wait_busy(struct epaper_device* dev, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    int ret;
    while (elapsed <= timeout_ms)
    {
        ret = vfs_gpio_get_level(&dev->busy_gpio);
        if (ret != VFS_OK)
            return ret;
        if (dev->busy_gpio.level == 0)
            return VFS_OK;
        osal_delay_ms(1);
        elapsed++;
    }
    return VFS_ERR_BUSY;
}

/**
 * @brief 首次 open 时打开 SPI/DC/RST/BUSY 并绑定 GPIO 参数
 * @return VFS_OK 或 VFS_ERR_*
 */
static int epaper_hw_create(struct epaper_device* dev)
{
    if (!dev)
        return VFS_ERR_INVAL;
    if (dev->hw_ready)
        return VFS_OK;
    {
        int ret;
        ret = device_open(dev->spi_dev, NULL);
        if (ret != VFS_OK)
            return ret;
        ret = device_open(dev->dc_dev, NULL);
        if (ret != VFS_OK)
            return ret;
        ret = device_ioctl(dev->dc_dev, GPIO_CMD_GET_LEVEL, &dev->dc_gpio, sizeof(dev->dc_gpio), 0);
        if (ret != VFS_OK)
            return ret;
        ret = device_open(dev->rst_dev, NULL);
        if (ret != VFS_OK)
            return ret;
        ret = device_ioctl(dev->rst_dev, GPIO_CMD_GET_LEVEL, &dev->rst_gpio, sizeof(dev->rst_gpio),
                           0);
        if (ret != VFS_OK)
            return ret;
        ret = device_open(dev->busy_dev, NULL);
        if (ret != VFS_OK)
            return ret;
        ret = device_ioctl(dev->busy_dev, GPIO_CMD_GET_LEVEL, &dev->busy_gpio,
                           sizeof(dev->busy_gpio), 0);
        if (ret != VFS_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭全部设备）
 */
static void epaper_hw_destroy(struct epaper_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->spi_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->spi_dev));
    if (dev->dc_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->dc_dev));
    if (dev->rst_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->rst_dev));
    if (dev->busy_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->busy_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int epaper_open(struct device* pdev, void* arg)
{
    struct epaper_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = epaper_get_drvdata(pdev);
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
        ret = epaper_hw_create(dev);
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
static int epaper_close(struct device* pdev)
{
    struct epaper_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = epaper_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        epaper_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*epaper_ioctl_fn_t)(struct epaper_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct epaper_ioctl_map
{
    epaper_ioctl_fn_t handler;
};

/**
 * @brief DISPLAY_CMD_CLEAR 实现：写空白帧并等待刷新完成
 */
static int epaper_cmd_clear(struct epaper_device* dev, void* arg, size_t len, uint32_t ms)
{
    const struct display_clear_arg* darg = (const struct display_clear_arg*)arg;
    uint8_t blank = 0x00;
    COMPAT_IGNORE_RESULT(darg);
    COMPAT_IGNORE_RESULT(len);
    if (!dev->hw_ready)
        return VFS_ERR_INVAL;
    epaper_dc(dev, 1);
    epaper_spi_xfer(dev, &blank, NULL, 1, ms);
    return epaper_wait_busy(dev, ms ? ms : dev->busy_timeout_ms);
}
/**
 * @brief DISPLAY_CMD_DRAW_AREA / FLUSH 实现：写整帧位图并等待刷新完成
 */
static int epaper_cmd_draw(struct epaper_device* dev, void* arg, size_t len, uint32_t ms)
{
    const struct display_draw_arg* darg = (const struct display_draw_arg*)arg;
    if (!dev->hw_ready || !darg || len != sizeof(*darg) || darg->format != DISPLAY_FMT_MONO_1BPP ||
        !darg->data)
        return VFS_ERR_INVAL;
    if (darg->x != 0 || darg->y != 0 || darg->w != dev->width || darg->h != dev->height)
        return VFS_ERR_INVAL;
    epaper_dc(dev, 1);
    if (epaper_spi_xfer(dev, darg->data, NULL, (size_t)dev->width * (size_t)dev->height / 8U, ms) !=
        VFS_OK)
        return VFS_ERR_IO;
    return epaper_wait_busy(dev, ms ? ms : dev->busy_timeout_ms);
}
/**
 * @brief DISPLAY_CMD_FILL_RECT 实现：单色屏仅支持全屏矩形
 */
static int epaper_cmd_fill_rect(struct epaper_device* dev, void* arg, size_t len, uint32_t ms)
{
    const struct display_rect_arg* darg = (const struct display_rect_arg*)arg;
    struct display_clear_arg clear_arg;
    if (!dev->hw_ready || !darg || len != sizeof(*darg))
        return VFS_ERR_INVAL;
    if (darg->x != 0 || darg->y != 0 || darg->w != dev->width || darg->h != dev->height)
        return VFS_ERR_INVAL;
    clear_arg.value = darg->color ? 1U : 0U;
    return epaper_cmd_clear(dev, &clear_arg, sizeof(clear_arg), ms);
}
/**
 * @brief DISPLAY_CMD_GET_INFO 实现：返回默认几何
 */
static int epaper_cmd_get_info(struct epaper_device* dev, void* arg, size_t len, uint32_t ms)
{
    struct display_info_arg* info = (struct display_info_arg*)arg;
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(ms);
    if (!info || len != sizeof(*info))
        return VFS_ERR_INVAL;
    info->width = (uint16_t)dev->width;
    info->height = (uint16_t)dev->height;
    info->format = DISPLAY_FMT_MONO_1BPP;
    return VFS_OK;
}
/**
 * @brief DISPLAY_CMD_SET_BRIGHTNESS 实现：电子纸无背光/对比度控制
 */
static int epaper_cmd_set_brightness(struct epaper_device* dev, void* arg, size_t len, uint32_t ms)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(ms);
    return VFS_ERR_NOTSUPP;
}
static const struct epaper_ioctl_map s_epaper_map[DISPLAY_CMD_COUNT] = {
    [DISPLAY_CMD_GET_INFO - DISPLAY_CMD_BASE - 1] = {epaper_cmd_get_info},
    [DISPLAY_CMD_CLEAR - DISPLAY_CMD_BASE - 1] = {epaper_cmd_clear},
    [DISPLAY_CMD_FILL_RECT - DISPLAY_CMD_BASE - 1] = {epaper_cmd_fill_rect},
    [DISPLAY_CMD_DRAW_AREA - DISPLAY_CMD_BASE - 1] = {epaper_cmd_draw},
    [DISPLAY_CMD_FLUSH - DISPLAY_CMD_BASE - 1] = {epaper_cmd_draw},
    [DISPLAY_CMD_SET_BRIGHTNESS - DISPLAY_CMD_BASE - 1] = {epaper_cmd_set_brightness},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int epaper_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct epaper_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = epaper_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)DISPLAY_CMD_BASE;
    if (off < 1 || off > DISPLAY_CMD_COUNT || !s_epaper_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_epaper_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations epaper_fops = {
    .open = epaper_open,
    .close = epaper_close,
    .ioctl = epaper_ioctl,
};

/**
 * @brief probe：claim 池项、绑定 SPI/DC/RST/BUSY、读 DTS 几何并挂 fops
 *
 * width/height 为 DTS 必填属性（对齐 ST7789）；busy-timeout-ms 可选，缺省 2000。
 */
static int epaper_probe(struct device* pdev)
{
    struct epaper_device* dev;
    int width = 0;
    int height = 0;
    int busy_to = 0;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_epaper_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_epaper_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->spi_dev = device_get_parent(pdev);
    if (!dev->spi_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }
    dev->dc_dev = device_get_phandle_dev(pdev, "dc-gpio");
    dev->rst_dev = device_get_phandle_dev(pdev, "reset-gpio");
    dev->busy_dev = device_get_phandle_dev(pdev, "busy-gpio");
    if (IS_ERR(dev->dc_dev) || IS_ERR(dev->rst_dev) || IS_ERR(dev->busy_dev))
    {
        ret = VFS_ERR_INVAL;
        goto err;
    }

    /* 几何参数走 DTS（必填），不依赖驱动内部默认常量 */
    if (device_get_prop_int(pdev, "width", &width) != VFS_OK ||
        device_get_prop_int(pdev, "height", &height) != VFS_OK || width <= 0 || height <= 0)
    {
        SYS_LOGE(k_tag, "probe requires width/height in DTS");
        ret = VFS_ERR_INVAL;
        goto err;
    }
    dev->width = width;
    dev->height = height;
    dev->busy_timeout_ms = EPAPER_BUSY_TIMEOUT_MS;
    if (device_get_prop_int(pdev, "busy-timeout-ms", &busy_to) == VFS_OK && busy_to > 0)
        dev->busy_timeout_ms = (uint32_t)busy_to;

    if (device_set_priv(pdev, dev) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    dev->ops = epaper_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev %dx%dev", pool_idx, width, height);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_epaper_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int epaper_remove(struct device* pdev)
{
    struct epaper_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = epaper_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_epaper_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    epaper_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_epaper_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(epaper, "gooddisplay,epaper", epaper_probe, epaper_remove)
