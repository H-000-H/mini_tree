/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file buzzer_drv.c
 *@brief 蜂鸣器驱动实现 — 挂在 TIM（PWM）或 GPIO 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_buzzer_pool[BUZZER_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令见 buzzer_drv.h。
 *   两种后端：phandle pwm（TIM 快路径）或 beep-gpio（GPIO 电平）
 */

#include "buzzer_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"
#include "vfs-tim.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_GPIO_BUZZER_PASSIVE
#define DTC_GEN_COUNT_GPIO_BUZZER_PASSIVE 1
#endif
#define BUZZER_POOL_COUNT DTC_GEN_COUNT_GPIO_BUZZER_PASSIVE

/** @brief 蜂鸣器驱动实例（嵌入 fops 与双后端句柄） */
struct buzzer_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* tim_dev; /**< PWM TIM 设备（phandle: pwm，可选） */
    struct device* gpio_dev; /**< 电平 GPIO 设备（phandle: beep-gpio，可选） */
    struct vfs_tim_arg tim; /**< PWM 参数（快路径） */
    struct vfs_gpio_arg gpio; /**< GPIO 参数 */
    int use_tim; /**< 后端选择：1=TIM PWM，0=GPIO */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct buzzer_device s_buzzer_pool[BUZZER_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_buzzer_used[BUZZER_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_buzzer_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "buzzer";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void buzzer_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_buzzer_pool_ctrl, s_buzzer_used, BUZZER_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct buzzer_device* buzzer_get_drvdata(struct device* pdev)
{
    return (struct buzzer_device*)device_get_priv(pdev);
}

/**
 * @brief 首次 open 时打开对应后端（TIM 或 GPIO）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int buzzer_hw_create(struct buzzer_device* dev)
{
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    if (dev->use_tim && dev->tim_dev)
    {
        int ret = device_open(dev->tim_dev, NULL);
        if (ret != MINI_OK)
            return ret;
    }
    else if (dev->gpio_dev)
    {
        int ret = device_open(dev->gpio_dev, NULL);
        if (ret != MINI_OK)
            return ret;
        ret = device_ioctl(dev->gpio_dev, GPIO_CMD_GET_LEVEL, &dev->gpio, sizeof(dev->gpio), 0);
        if (ret != MINI_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭全部后端设备）
 */
static void buzzer_hw_destroy(struct buzzer_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->tim_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->tim_dev));
    if (dev->gpio_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->gpio_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int buzzer_open(struct device* pdev, void* arg)
{
    struct buzzer_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = buzzer_get_drvdata(pdev);
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
        ret = buzzer_hw_create(dev);
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
static int buzzer_close(struct device* pdev)
{
    struct buzzer_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = buzzer_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        buzzer_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*buzzer_ioctl_fn_t)(struct buzzer_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct buzzer_ioctl_map
{
    buzzer_ioctl_fn_t handler;
};

/**
 * @brief BUZZER_CMD_BEEP 实现：PWM 占空比或 GPIO 电平控制，可带时长
 */
static int buzzer_cmd_beep(struct buzzer_device* dev, void* arg, size_t len, uint32_t ms)
{
    int on;
    uint32_t dur;
    if (!dev->hw_ready || !arg || len != sizeof(int))
        return MINI_ERR_INVAL;
    on = *(int*)arg;
    dur = ms ? ms : 50U;
    if (dev->use_tim && dev->tim_dev)
    {
        dev->tim.arr = 1000U;
        dev->tim.ccr = on ? 500U : 0U;
        dev->tim.channel = 1U;
        COMPAT_IGNORE_RESULT(
            device_ioctl(dev->tim_dev, TIM_CMD_PWM_UPDATE, &dev->tim, sizeof(dev->tim), 100));
    }
    else if (dev->gpio_dev)
    {
        dev->gpio.level = on ? 1 : 0;
        vfs_gpio_set_level(&dev->gpio);
    }
    if (on)
        osal_delay_ms(dur);
    return MINI_OK;
}
static const struct buzzer_ioctl_map s_buzzer_map[BUZZER_CMD_COUNT] = {
    [BUZZER_CMD_BEEP - BUZZER_CMD_BASE - 1] = {buzzer_cmd_beep},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int buzzer_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct buzzer_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = buzzer_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)BUZZER_CMD_BASE;
    if (off < 1 || off > BUZZER_CMD_COUNT || !s_buzzer_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_buzzer_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations buzzer_fops = {
    .open = buzzer_open,
    .close = buzzer_close,
    .ioctl = buzzer_ioctl,
};

/**
 * @brief probe：claim 池项、绑定 pwm/beep-gpio 并挂 fops
 */
static int buzzer_probe(struct device* pdev)
{
    struct buzzer_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_buzzer_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_buzzer_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->tim_dev = device_get_phandle_dev(pdev, "pwm");
    dev->gpio_dev = device_get_phandle_dev(pdev, "beep-gpio");
    dev->use_tim = !IS_ERR(dev->tim_dev);
    if (!dev->use_tim && IS_ERR(dev->gpio_dev))
    {
        ret = MINI_ERR_INVAL;
        goto err;
    }
    if (IS_ERR(dev->tim_dev))
        dev->tim_dev = NULL;
    if (IS_ERR(dev->gpio_dev))
        dev->gpio_dev = NULL;

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = buzzer_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_buzzer_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int buzzer_remove(struct device* pdev)
{
    struct buzzer_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = buzzer_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_buzzer_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    buzzer_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_buzzer_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(buzzer, "gpio-buzzer-passive", buzzer_probe, buzzer_remove)
