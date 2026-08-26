/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file ds18b20_drv.c
 *@brief DS18B20 单总线温度传感器驱动实现 — 挂在 GPIO 单总线（OW）下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_ds18b20_pool[DS18B20_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令见 ds18b20_drv.h，单总线命令定义见 ds18b20_regs.h。
 *   数据流: VFS ioctl → ds18b20_cmd_temp → GPIO 位时序（vfs_gpio_*）→ HAL
 */

#include "ds18b20_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "ds18b20_regs.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_MAXIM_DS18B20
#define DTC_GEN_COUNT_MAXIM_DS18B20 1
#endif
#define DS18B20_POOL_COUNT DTC_GEN_COUNT_MAXIM_DS18B20

/** @brief DS18B20 驱动实例（嵌入 fops 与 GPIO 操作参数） */
struct ds18b20_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* data_dev; /**< data 引脚所属 GPIO 设备（phandle: data-gpio） */
    struct vfs_gpio_arg data_gpio; /**< GPIO 操作参数（引脚号 + 电平） */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct ds18b20_device s_ds18b20_pool[DS18B20_POOL_COUNT] MINI_ALIGNED(4);
static uint8_t s_ds18b20_used[DS18B20_POOL_COUNT] MINI_ALIGNED(4);
static osal_pool_t s_ds18b20_pool_ctrl MINI_ALIGNED(4);
static const char* const k_tag = "ds18b20";

/**
 * @brief 驱动池启动初始化（mini_pre_execution 阶段，创建静态对象池）
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_DRIVER_POOL) static void ds18b20_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_ds18b20_pool_ctrl, s_ds18b20_used, DS18B20_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct ds18b20_device* ds18b20_get_drvdata(struct device* pdev)
{
    return (struct ds18b20_device*)device_get_priv(pdev);
}

/**
 * @brief 微秒延时（OSAL 转发）
 */
static void ds18b20_delay_us(uint32_t us) { osal_delay_us(us); }

/**
 * @brief 单总线复位脉冲：拉低 480us 后释放，检测存在脉冲
 * @return MINI_OK（检测到应答）或 MINI_ERR_IO（无应答）
 */
static int ds18b20_reset(struct ds18b20_device* dev)
{
    int present;

    dev->data_gpio.level = 0;
    if (vfs_gpio_set_level(&dev->data_gpio) != MINI_OK)
        return MINI_ERR_IO;
    ds18b20_delay_us(480);
    dev->data_gpio.level = 1;
    if (vfs_gpio_set_level(&dev->data_gpio) != MINI_OK)
        return MINI_ERR_IO;
    ds18b20_delay_us(70);
    if (vfs_gpio_get_level(&dev->data_gpio) != MINI_OK)
        return MINI_ERR_IO;
    present = (dev->data_gpio.level == 0);
    ds18b20_delay_us(410);
    return present ? MINI_OK : MINI_ERR_IO;
}

/**
 * @brief 写 1bit（写 1：短拉低；写 0：长拉低）
 */
static int ds18b20_write_bit(struct ds18b20_device* dev, int bit)
{
    dev->data_gpio.level = 0;
    if (vfs_gpio_set_level(&dev->data_gpio) != MINI_OK)
        return MINI_ERR_IO;
    ds18b20_delay_us(bit ? 6U : 60U);
    dev->data_gpio.level = 1;
    if (vfs_gpio_set_level(&dev->data_gpio) != MINI_OK)
        return MINI_ERR_IO;
    ds18b20_delay_us(bit ? 64U : 10U);
    return MINI_OK;
}

/**
 * @brief 读 1bit（拉低 3us 后释放，采样电平）
 * @param[in] bit 输出读到的位
 */
static int ds18b20_read_bit(struct ds18b20_device* dev, int* bit)
{
    dev->data_gpio.level = 0;
    if (vfs_gpio_set_level(&dev->data_gpio) != MINI_OK)
        return MINI_ERR_IO;
    ds18b20_delay_us(3);
    dev->data_gpio.level = 1;
    if (vfs_gpio_set_level(&dev->data_gpio) != MINI_OK)
        return MINI_ERR_IO;
    ds18b20_delay_us(10);
    if (vfs_gpio_get_level(&dev->data_gpio) != MINI_OK)
        return MINI_ERR_IO;
    *bit = dev->data_gpio.level ? 1 : 0;
    ds18b20_delay_us(50);
    return MINI_OK;
}

/**
 * @brief 写 1B（LSB 先行）
 */
static int ds18b20_write_byte(struct ds18b20_device* dev, uint8_t val)
{
    int index;
    for (index = 0; index < 8; index++)
    {
        int ret = ds18b20_write_bit(dev, (val >> index) & 1);
        if (ret != MINI_OK)
            return ret;
    }
    return MINI_OK;
}

/**
 * @brief 读 1B（LSB 先行）
 * @param[in] val 输出读到的字节
 */
static int ds18b20_read_byte(struct ds18b20_device* dev, uint8_t* val)
{
    int index;
    int bit_val;
    uint8_t out = 0;
    for (index = 0; index < 8; index++)
    {
        if (ds18b20_read_bit(dev, &bit_val) != MINI_OK)
            return MINI_ERR_IO;
        if (bit_val)
            out |= (uint8_t)(1U << index);
    }
    *val = out;
    return MINI_OK;
}

/**
 * @brief 首次 open 时打开 GPIO 设备并查询默认电平（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int ds18b20_hw_create(struct ds18b20_device* dev)
{
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    {
        int ret = device_open(dev->data_dev, NULL);
        if (ret != MINI_OK)
            return ret;
        ret = device_ioctl(dev->data_dev, GPIO_CMD_GET_LEVEL, &dev->data_gpio,
                           sizeof(dev->data_gpio), 0);
        if (ret != MINI_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 GPIO 设备）
 */
static void ds18b20_hw_destroy(struct ds18b20_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->data_dev)
        MINI_IGNORE_RESULT(device_close(dev->data_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int ds18b20_open(struct device* pdev, void* arg)
{
    struct ds18b20_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    MINI_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = ds18b20_get_drvdata(pdev);
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
        ret = ds18b20_hw_create(dev);
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
static int ds18b20_close(struct device* pdev)
{
    struct ds18b20_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = ds18b20_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ds18b20_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*ds18b20_ioctl_fn_t)(struct ds18b20_device* dev, void* arg, size_t arg_len,
                                  uint32_t ms);
struct ds18b20_ioctl_map
{
    ds18b20_ioctl_fn_t handler;
};

/**
 * @brief DS18B20_CMD_READ_TEMP 实现：复位 → 转换（750ms）→ 读暂存器换算温度
 */
static int ds18b20_cmd_temp(struct ds18b20_device* dev, void* arg, size_t len, uint32_t ms)
{
    uint8_t lo = 0;
    uint8_t hi = 0;
    int16_t raw;
    int* temp_out = (int*)arg;
    MINI_IGNORE_RESULT(ms);
    if (!dev->hw_ready || !temp_out || len != sizeof(int))
        return MINI_ERR_INVAL;
    if (ds18b20_reset(dev) != MINI_OK)
        return MINI_ERR_IO;
    if (ds18b20_write_byte(dev, DS18B20_OW_SKIP_ROM) != MINI_OK)
        return MINI_ERR_IO;
    if (ds18b20_write_byte(dev, DS18B20_OW_CONVERT_T) != MINI_OK)
        return MINI_ERR_IO;
    osal_delay_ms(DS18B20_CONVERT_MS);
    if (ds18b20_reset(dev) != MINI_OK)
        return MINI_ERR_IO;
    if (ds18b20_write_byte(dev, DS18B20_OW_SKIP_ROM) != MINI_OK)
        return MINI_ERR_IO;
    if (ds18b20_write_byte(dev, DS18B20_OW_READ_SCRATCHPAD) != MINI_OK)
        return MINI_ERR_IO;
    if (ds18b20_read_byte(dev, &lo) != MINI_OK)
        return MINI_ERR_IO;
    if (ds18b20_read_byte(dev, &hi) != MINI_OK)
        return MINI_ERR_IO;
    raw = (int16_t)(((uint16_t)hi << 8) | lo);
    *temp_out = (int)(raw / DS18B20_TEMP_LSB_PER_C);
    return MINI_OK;
}
static const struct ds18b20_ioctl_map s_ds18b20_map[DS18B20_CMD_COUNT] = {
    [DS18B20_CMD_READ_TEMP - DS18B20_CMD_BASE - 1] = {ds18b20_cmd_temp},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int ds18b20_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ds18b20_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = ds18b20_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)DS18B20_CMD_BASE;
    if (off < 1 || off > DS18B20_CMD_COUNT || !s_ds18b20_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_ds18b20_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ds18b20_fops = {
    .open = ds18b20_open,
    .close = ds18b20_close,
    .ioctl = ds18b20_ioctl,
};

/**
 * @brief probe：claim 池项、绑定 data-gpio 设备并挂 fops
 */
static int ds18b20_probe(struct device* pdev)
{
    struct ds18b20_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ds18b20_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_ds18b20_pool[pool_idx];
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    dev->data_dev = device_get_phandle_dev(pdev, "data-gpio");
    if (IS_ERR(dev->data_dev))
    {
        ret = PTR_ERR(dev->data_dev);
        goto err;
    }

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = ds18b20_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_ds18b20_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int ds18b20_remove(struct device* pdev)
{
    struct ds18b20_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = ds18b20_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_ds18b20_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    ds18b20_hw_destroy(dev);
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_ds18b20_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(ds18b20, "maxim,ds18b20", ds18b20_probe, ds18b20_remove)
