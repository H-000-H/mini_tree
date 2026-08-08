/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file max98357a_drv.c
 * @brief MAX98357A 功放驱动实现 — SDN 经 vfs-gpio（禁止厂商 SDK）
 *
 * 静态池: s_max98357a_pool[MAX98357A_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令见 max98357a_drv.h。
 *
 * PCM 走 I2S 总线，本驱动仅控 SDN 引脚；支持挂起/恢复回调。
 */
#include "max98357a_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_MAXIM_MAX98357A
#define DTC_GEN_COUNT_MAXIM_MAX98357A 1
#endif

#define MAX98357A_COUNT DTC_GEN_COUNT_MAXIM_MAX98357A

/** @brief MAX98357A 驱动实例（嵌入 fops 与 SDN 引脚） */
struct max98357a_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* sdn_dev; /**< SDN 引脚 GPIO 设备（phandle: sdn-gpio） */
    struct vfs_gpio_arg sdn_gpio; /**< SDN 引脚操作参数 */
    int active_level; /**< 使能有效电平（DTS active-level） */
    int hw_ready; /**< 硬件已初始化标志 */
};

static struct max98357a_device s_max98357a_pool[MAX98357A_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_max98357a_used[MAX98357A_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_max98357a_pool_ctrl COMPAT_ALIGNED(4);

static const char* const k_tag = "max98357a";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void max98357a_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_max98357a_pool_ctrl, s_max98357a_used, MAX98357A_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct max98357a_device* max98357a_get_drvdata(struct device* pdev)
{
    return (struct max98357a_device*)device_get_priv(pdev);
}

/**
 * @brief 设置 SDN 电平（按有效极性换算）
 */
static int max98357a_set_level(struct max98357a_device* amp, int enable)
{
    if (!amp || !amp->sdn_gpio.obj)
        return VFS_ERR_INVAL;
    amp->sdn_gpio.level = enable ? amp->active_level : !amp->active_level;
    return vfs_gpio_set_level(&amp->sdn_gpio);
}

/**
 * @brief 首次 open 时打开 SDN GPIO 并默认使能功放
 * @return VFS_OK 或 VFS_ERR_*
 */
static int max98357a_hw_create(struct max98357a_device* amp)
{
    int ret;

    if (!amp || !amp->sdn_dev)
        return VFS_ERR_INVAL;
    if (amp->hw_ready)
        return VFS_OK;

    ret = device_open(amp->sdn_dev, NULL);
    if (ret != VFS_OK)
        return ret;

    ret = device_ioctl(amp->sdn_dev, GPIO_CMD_GET_LEVEL, &amp->sdn_gpio, sizeof(amp->sdn_gpio), 0);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(device_close(amp->sdn_dev));
        return ret;
    }

    ret = max98357a_set_level(amp, 1);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(device_close(amp->sdn_dev));
        amp->sdn_gpio.obj = NULL;
        return ret;
    }

    amp->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 SDN GPIO，功放下电）
 */
static void max98357a_hw_destroy(struct max98357a_device* amp)
{
    if (!amp || !amp->hw_ready)
        return;
    COMPAT_IGNORE_RESULT(max98357a_set_level(amp, 0));
    if (amp->sdn_dev)
        COMPAT_IGNORE_RESULT(device_close(amp->sdn_dev));
    amp->sdn_gpio.obj = NULL;
    amp->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int max98357a_open(struct device* pdev, void* arg)
{
    struct max98357a_device* amp;
    struct dev_lifecycle* lc;
    int first;
    int ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    amp = max98357a_get_drvdata(pdev);
    if (IS_ERR(amp))
        return PTR_ERR(amp);

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = max98357a_hw_create(amp);
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
static int max98357a_close(struct device* pdev)
{
    struct max98357a_device* amp;
    struct dev_lifecycle* lc;
    int last;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    amp = max98357a_get_drvdata(pdev);
    if (IS_ERR(amp))
        return PTR_ERR(amp);

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;

    if (last)
        max98357a_hw_destroy(amp);

    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief MAX98357A_CMD_SET_ENABLE 实现：功放使能/关闭
 */
static int max98357a_cmd_set_enable(struct max98357a_device* amp, void* arg, size_t arg_len,
                                    uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!amp || !arg || arg_len != sizeof(int))
        return VFS_ERR_INVAL;
    if (!amp->hw_ready)
        return VFS_ERR_IO;
    return max98357a_set_level(amp, *(int*)arg);
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*max98357a_ioctl_fn_t)(struct max98357a_device* amp, void* arg, size_t arg_len,
                                    uint32_t timeout_ms);

struct max98357a_ioctl_map
{
    max98357a_ioctl_fn_t handler;
};

static const struct max98357a_ioctl_map s_max98357a_ioctl_map[MAX98357A_CMD_COUNT] = {
    [MAX98357A_CMD_SET_ENABLE - MAX98357A_CMD_BASE - 1] = {max98357a_cmd_set_enable},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int max98357a_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len,
                           uint32_t timeout_ms)
{
    struct max98357a_device* amp;
    struct dev_lifecycle* lc;
    int32_t offset;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    amp = max98357a_get_drvdata(pdev);
    if (IS_ERR(amp))
        return PTR_ERR(amp);

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    offset = (int32_t)cmd - (int32_t)MAX98357A_CMD_BASE;
    if (offset < 1 || offset > MAX98357A_CMD_COUNT || !s_max98357a_ioctl_map[offset - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_max98357a_ioctl_map[offset - 1].handler(amp, arg, arg_len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief fops.suspend：关功放（静音）
 */
static int max98357a_suspend(struct device* pdev)
{
    struct max98357a_device* amp = max98357a_get_drvdata(pdev);

    if (IS_ERR(amp))
        return PTR_ERR(amp);
    return max98357a_set_level(amp, 0);
}

/**
 * @brief fops.resume：恢复功放使能
 */
static int max98357a_resume(struct device* pdev)
{
    struct max98357a_device* amp = max98357a_get_drvdata(pdev);

    if (IS_ERR(amp))
        return PTR_ERR(amp);
    return max98357a_set_level(amp, 1);
}

static const struct file_operations max98357a_fops = {
    .open = max98357a_open,
    .close = max98357a_close,
    .ioctl = max98357a_ioctl,
    .suspend = max98357a_suspend,
    .resume = max98357a_resume,
};

/**
 * @brief probe：claim 池项、绑定 sdn-gpio 与 active-level 并挂 fops
 */
static int max98357a_probe(struct device* pdev)
{
    struct max98357a_device* amp;
    struct device* sdn_dev;
    int active = 1;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    sdn_dev = device_get_phandle_dev(pdev, "sdn-gpio");
    if (IS_ERR(sdn_dev))
        return PTR_ERR(sdn_dev);

    pool_idx = osal_pool_claim(&s_max98357a_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    amp = &s_max98357a_pool[pool_idx];
    COMPAT_MEM_SET(amp, 0, sizeof(*amp));

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "active-level", &active));
    amp->sdn_dev = sdn_dev;
    amp->active_level = active ? 1 : 0;
    amp->hw_ready = 0;

    if (device_set_priv(pdev, amp) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_pool;
    }

    amp->ops = max98357a_fops;
    pdev->ops = &amp->ops;

    SYS_LOGI(k_tag, "probe OK: pool=%d sdn=%s active=%d", pool_idx, device_get_name(sdn_dev),
             amp->active_level);
    return VFS_OK;

err_pool:
    pdev->ops = NULL;
    COMPAT_MEM_SET(amp, 0, sizeof(*amp));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_max98357a_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int max98357a_remove(struct device* pdev)
{
    struct max98357a_device* amp;
    struct dev_lifecycle* lc;
    int pool_idx;

    if (!pdev)
        return VFS_ERR_INVAL;

    amp = max98357a_get_drvdata(pdev);
    if (IS_ERR(amp))
        return PTR_ERR(amp);

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = (int)(amp - s_max98357a_pool);

    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    max98357a_hw_destroy(amp);
    COMPAT_MEM_SET(amp, 0, sizeof(*amp));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_max98357a_pool_ctrl, pool_idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(max98357a, "maxim,max98357a", max98357a_probe, max98357a_remove)
