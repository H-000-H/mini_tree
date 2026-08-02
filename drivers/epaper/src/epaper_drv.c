/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file epaper_drv.c
 * @brief 电子纸驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_epaper_pool[EPAPER_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与参数结构见 epaper_drv.h。
 *
 * 引脚: DC/RST/BUSY 均为 GPIO（phandle: dc-gpio / reset-gpio / busy-gpio）；
 * 数据流: VFS ioctl → epaper_cmd_* → SPI transfer（vfs-spi）→ HAL
 */
#include "epaper_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
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

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct epaper_device s_epaper_pool[EPAPER_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_epaper_used[EPAPER_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_epaper_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "epaper";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160) static void epaper_pool_boot_init(void)
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
static int epaper_spi_xfer(struct epaper_device* d, const uint8_t* tx, uint8_t* rx, size_t len,
                           uint32_t to)
{
    struct spi_transfer_arg arg;
    if (!d || !d->spi_dev || len == 0U)
        return VFS_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.len = len;
    arg.xfer_mode = SPI_XFER_AUTO;
    return device_ioctl(d->spi_dev, SPI_CMD_TRANSFER, &arg, sizeof(arg), to);
}

/**
 * @brief 设置 DC 引脚（命令/数据选择）
 */
static int epaper_dc(struct epaper_device* d, int data)
{
    d->dc_gpio.level = data ? 1 : 0;
    return vfs_gpio_set_level(&d->dc_gpio);
}
/**
 * @brief 等待 BUSY 释放（低电平表示空闲）
 * @return VFS_OK 或 VFS_ERR_BUSY（超时）
 */
static int epaper_wait_busy(struct epaper_device* d, uint32_t to)
{
    uint32_t e = 0;
    int r;
    while (e <= to)
    {
        r = vfs_gpio_get_level(&d->busy_gpio);
        if (r != VFS_OK)
            return r;
        if (d->busy_gpio.level == 0)
            return VFS_OK;
        osal_delay_ms(1);
        e++;
    }
    return VFS_ERR_BUSY;
}

/**
 * @brief 首次 open 时打开 SPI/DC/RST/BUSY 并绑定 GPIO 参数
 * @return VFS_OK 或 VFS_ERR_*
 */
static int epaper_hw_create(struct epaper_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    {
        int r;
        r = device_open(d->spi_dev, NULL);
        if (r != VFS_OK)
            return r;
        r = device_open(d->dc_dev, NULL);
        if (r != VFS_OK)
            return r;
        r = device_ioctl(d->dc_dev, GPIO_CMD_GET_LEVEL, &d->dc_gpio, sizeof(d->dc_gpio), 0);
        if (r != VFS_OK)
            return r;
        r = device_open(d->rst_dev, NULL);
        if (r != VFS_OK)
            return r;
        r = device_ioctl(d->rst_dev, GPIO_CMD_GET_LEVEL, &d->rst_gpio, sizeof(d->rst_gpio), 0);
        if (r != VFS_OK)
            return r;
        r = device_open(d->busy_dev, NULL);
        if (r != VFS_OK)
            return r;
        r = device_ioctl(d->busy_dev, GPIO_CMD_GET_LEVEL, &d->busy_gpio, sizeof(d->busy_gpio), 0);
        if (r != VFS_OK)
            return r;
    }
    d->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭全部设备）
 */
static void epaper_hw_destroy(struct epaper_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->spi_dev)
        COMPAT_IGNORE_RESULT(device_close(d->spi_dev));
    if (d->dc_dev)
        COMPAT_IGNORE_RESULT(device_close(d->dc_dev));
    if (d->rst_dev)
        COMPAT_IGNORE_RESULT(device_close(d->rst_dev));
    if (d->busy_dev)
        COMPAT_IGNORE_RESULT(device_close(d->busy_dev));
    d->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int epaper_open(struct device* pdev, void* arg)
{
    struct epaper_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = epaper_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = epaper_hw_create(d);
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
    struct epaper_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = epaper_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        epaper_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*epaper_ioctl_fn_t)(struct epaper_device* d, void* arg, size_t arg_len, uint32_t ms);
struct epaper_ioctl_map
{
    epaper_ioctl_fn_t handler;
};

/**
 * @brief EPAPER_CMD_INIT 实现：复位脉冲 + 等待 BUSY
 */
static int epaper_cmd_init(struct epaper_device* d, void* arg, size_t len, uint32_t ms)
{
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(len);
    d->rst_gpio.level = 0;
    vfs_gpio_set_level(&d->rst_gpio);
    osal_delay_ms(EPAPER_RESET_HOLD_MS);
    d->rst_gpio.level = 1;
    vfs_gpio_set_level(&d->rst_gpio);
    osal_delay_ms(EPAPER_RESET_HOLD_MS);
    return epaper_wait_busy(d, ms ? ms : 500U);
}
/**
 * @brief EPAPER_CMD_CLEAR 实现：写空白帧并等待刷新完成
 */
static int epaper_cmd_clear(struct epaper_device* d, void* arg, size_t len, uint32_t ms)
{
    uint8_t z = 0x00;
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(len);
    epaper_dc(d, 1);
    epaper_spi_xfer(d, &z, NULL, 1, ms);
    return epaper_wait_busy(d, ms ? ms : EPAPER_BUSY_TIMEOUT_MS);
}
/**
 * @brief EPAPER_CMD_DRAW_BITMAP 实现：写整帧位图并等待刷新完成
 */
static int epaper_cmd_draw(struct epaper_device* d, void* arg, size_t len, uint32_t ms)
{
    struct epaper_bitmap* bm = (struct epaper_bitmap*)arg;
    if (!bm || len != sizeof(*bm) || !bm->data || !bm->len)
        return VFS_ERR_INVAL;
    epaper_dc(d, 1);
    if (epaper_spi_xfer(d, bm->data, NULL, bm->len, ms) != VFS_OK)
        return VFS_ERR_IO;
    return epaper_wait_busy(d, ms ? ms : EPAPER_BUSY_TIMEOUT_MS);
}
/**
 * @brief EPAPER_CMD_GET_INFO 实现：返回默认几何
 */
static int epaper_cmd_get_info(struct epaper_device* d, void* arg, size_t len, uint32_t ms)
{
    struct epaper_info* info = (struct epaper_info*)arg;
    COMPAT_IGNORE_RESULT(d);
    COMPAT_IGNORE_RESULT(ms);
    if (!info || len != sizeof(*info))
        return VFS_ERR_INVAL;
    info->width = EPAPER_DEFAULT_WIDTH;
    info->height = EPAPER_DEFAULT_HEIGHT;
    info->bpp = EPAPER_DEFAULT_BPP;
    return VFS_OK;
}
static const struct epaper_ioctl_map s_epaper_map[EPAPER_CMD_COUNT] = {
    [EPAPER_CMD_INIT - EPAPER_CMD_BASE - 1] = {epaper_cmd_init},
    [EPAPER_CMD_CLEAR - EPAPER_CMD_BASE - 1] = {epaper_cmd_clear},
    [EPAPER_CMD_DRAW_BITMAP - EPAPER_CMD_BASE - 1] = {epaper_cmd_draw},
    [EPAPER_CMD_GET_INFO - EPAPER_CMD_BASE - 1] = {epaper_cmd_get_info},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int epaper_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct epaper_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = epaper_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)EPAPER_CMD_BASE;
    if (off < 1 || off > EPAPER_CMD_COUNT || !s_epaper_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_epaper_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations epaper_fops = {
    .open = epaper_open,
    .close = epaper_close,
    .ioctl = epaper_ioctl,
};

/**
 * @brief probe：claim 池项、绑定 SPI/DC/RST/BUSY 并挂 fops
 */
static int epaper_probe(struct device* pdev)
{
    struct epaper_device* d;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_epaper_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_epaper_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->spi_dev = device_get_parent(pdev);
    if (!d->spi_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }
    d->dc_dev = device_get_phandle_dev(pdev, "dc-gpio");
    d->rst_dev = device_get_phandle_dev(pdev, "reset-gpio");
    d->busy_dev = device_get_phandle_dev(pdev, "busy-gpio");
    if (IS_ERR(d->dc_dev) || IS_ERR(d->rst_dev) || IS_ERR(d->busy_dev))
    {
        ret = VFS_ERR_INVAL;
        goto err;
    }

    if (device_set_priv(pdev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = epaper_fops;
    pdev->ops = &d->ops;
    SYS_LOGI(k_tag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_epaper_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int epaper_remove(struct device* pdev)
{
    struct epaper_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    d = epaper_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_epaper_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    epaper_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_epaper_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(epaper, "gooddisplay,epaper", epaper_probe, epaper_remove)
