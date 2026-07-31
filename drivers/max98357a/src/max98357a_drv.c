/* SPDX-License-Identifier: Apache-2.0 */
/*
 * MAX98357A 驱动 — SDN 经 vfs-gpio（禁止厂商 SDK）
 */
#include "max98357a_drv.h"
#include "vfs-gpio.h"
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

#ifndef DTC_GEN_COUNT_MAXIM_MAX98357A
#define DTC_GEN_COUNT_MAXIM_MAX98357A  1
#endif

#define MAX98357A_COUNT  DTC_GEN_COUNT_MAXIM_MAX98357A

struct max98357a_device
{
    struct file_operations ops;
    struct device*         sdn_dev;
    struct vfs_gpio_arg    sdn_gpio;
    int                    active_level;
    int                    hw_ready;
};

static struct max98357a_device s_max98357a_pool[MAX98357A_COUNT] COMPAT_ALIGNED(4);
static uint8_t                 s_max98357a_used[MAX98357A_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t             s_max98357a_pool_ctrl COMPAT_ALIGNED(4);

static const char* const kTag = "max98357a";

pre_execution(160)
static void max98357a_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_max98357a_pool_ctrl, s_max98357a_used,
                                         MAX98357A_COUNT));
}

static struct max98357a_device* max98357a_get_drvdata(struct device* dev)
{
    return (struct max98357a_device*)device_get_priv(dev);
}

static int max98357a_set_level(struct max98357a_device* amp, int enable)
{
    if (!amp || !amp->sdn_gpio.obj)
        return VFS_ERR_INVAL;
    amp->sdn_gpio.level = enable ? amp->active_level : !amp->active_level;
    return vfs_gpio_set_level(&amp->sdn_gpio);
}

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

    ret = device_ioctl(amp->sdn_dev, GPIO_CMD_GET_LEVEL, &amp->sdn_gpio,
                       sizeof(amp->sdn_gpio), 0);
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

static int max98357a_open(struct device* dev, void* arg)
{
    struct max98357a_device* amp;
    struct dev_lifecycle*    lc;
    int                      first;
    int                      ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    amp = max98357a_get_drvdata(dev);
    if (IS_ERR(amp))
        return PTR_ERR(amp);

    lc = device_lc(dev);
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

static int max98357a_close(struct device* dev)
{
    struct max98357a_device* amp;
    struct dev_lifecycle*    lc;
    int                      last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    amp = max98357a_get_drvdata(dev);
    if (IS_ERR(amp))
        return PTR_ERR(amp);

    lc = device_lc(dev);
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

static int max98357a_cmd_set_enable(struct max98357a_device* amp, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!amp || !arg || arg_len != sizeof(int))
        return VFS_ERR_INVAL;
    if (!amp->hw_ready)
        return VFS_ERR_IO;
    return max98357a_set_level(amp, *(int*)arg);
}

typedef int (*max98357a_ioctl_fn_t)(struct max98357a_device* amp, void* arg, size_t arg_len, uint32_t timeout_ms);

struct max98357a_ioctl_map
{
    max98357a_ioctl_fn_t handler;
};

static const struct max98357a_ioctl_map s_max98357a_ioctl_map[MAX98357A_CMD_COUNT] =
{
    [MAX98357A_CMD_SET_ENABLE - MAX98357A_CMD_BASE - 1] = { max98357a_cmd_set_enable },
};

static int max98357a_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct max98357a_device* amp;
    struct dev_lifecycle*    lc;
    int32_t                  offset;
    int                      ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    amp = max98357a_get_drvdata(dev);
    if (IS_ERR(amp))
        return PTR_ERR(amp);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    offset = (int32_t)cmd - (int32_t)MAX98357A_CMD_BASE;
    if (offset < 1 || offset > MAX98357A_CMD_COUNT ||
        !s_max98357a_ioctl_map[offset - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_max98357a_ioctl_map[offset - 1].handler(amp, arg, arg_len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static int max98357a_suspend(struct device* dev)
{
    struct max98357a_device* amp = max98357a_get_drvdata(dev);

    if (IS_ERR(amp))
        return PTR_ERR(amp);
    return max98357a_set_level(amp, 0);
}

static int max98357a_resume(struct device* dev)
{
    struct max98357a_device* amp = max98357a_get_drvdata(dev);

    if (IS_ERR(amp))
        return PTR_ERR(amp);
    return max98357a_set_level(amp, 1);
}

static const struct file_operations max98357a_fops =
{
    .open    = max98357a_open,
    .close   = max98357a_close,
    .ioctl   = max98357a_ioctl,
    .suspend = max98357a_suspend,
    .resume  = max98357a_resume,
};

static int max98357a_probe(struct device* dev)
{
    struct max98357a_device* amp;
    struct device*           sdn_dev;
    int                      active = 1;
    int                      pool_idx;
    int                      ret;

    if (!dev)
        return VFS_ERR_INVAL;

    sdn_dev = device_get_phandle_dev(dev, "sdn-gpio");
    if (IS_ERR(sdn_dev))
        return PTR_ERR(sdn_dev);

    pool_idx = osal_pool_claim(&s_max98357a_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    amp = &s_max98357a_pool[pool_idx];
    COMPAT_MEM_SET(amp, 0, sizeof(*amp));

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "active-level", &active));
    amp->sdn_dev      = sdn_dev;
    amp->active_level = active ? 1 : 0;
    amp->hw_ready     = 0;

    if (device_set_priv(dev, amp) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_pool;
    }

    amp->ops = max98357a_fops;
    dev->ops = &amp->ops;

    SYS_LOGI(kTag, "probe OK: pool=%d sdn=%s active=%d",
             pool_idx, device_get_name(sdn_dev), amp->active_level);
    return VFS_OK;

err_pool:
    dev->ops = NULL;
    COMPAT_MEM_SET(amp, 0, sizeof(*amp));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_max98357a_pool_ctrl, pool_idx));
    return ret;
}

static int max98357a_remove(struct device* dev)
{
    struct max98357a_device* amp;
    struct dev_lifecycle*    lc;
    int                      pool_idx;

    if (!dev)
        return VFS_ERR_INVAL;

    amp = max98357a_get_drvdata(dev);
    if (IS_ERR(amp))
        return PTR_ERR(amp);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = (int)(amp - s_max98357a_pool);

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);

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
