/* SPDX-License-Identifier: Apache-2.0 */
#include "buzzer_drv.h"
#include "vfs-tim.h"
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

#ifndef DTC_GEN_COUNT_GPIO_BUZZER_PASSIVE
#define DTC_GEN_COUNT_GPIO_BUZZER_PASSIVE  1
#endif
#define BUZZER_POOL_COUNT  DTC_GEN_COUNT_GPIO_BUZZER_PASSIVE

struct buzzer_device
{
    struct file_operations ops;
    struct device* tim_dev;
    struct device* gpio_dev;
    struct vfs_tim_arg tim;
    struct vfs_gpio_arg gpio;
    int use_tim;

    int                    hw_ready;
};

static struct buzzer_device s_buzzer_pool[BUZZER_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_buzzer_used[BUZZER_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_buzzer_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "buzzer";

pre_execution(160)
static void buzzer_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_buzzer_pool_ctrl, s_buzzer_used, BUZZER_POOL_COUNT));
}

static struct buzzer_device* buzzer_get_drvdata(struct device* dev)
{
    return (struct buzzer_device*)device_get_priv(dev);
}



static int buzzer_hw_create(struct buzzer_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    if (d->use_tim && d->tim_dev) {
      int r = device_open(d->tim_dev, NULL); if (r != VFS_OK) return r;
    } else if (d->gpio_dev) {
      int r = device_open(d->gpio_dev, NULL); if (r != VFS_OK) return r;
      r = device_ioctl(d->gpio_dev, GPIO_CMD_GET_LEVEL, &d->gpio, sizeof(d->gpio), 0);
      if (r != VFS_OK) return r;
    }
    d->hw_ready = 1; return VFS_OK;

}

static void buzzer_hw_destroy(struct buzzer_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->tim_dev) COMPAT_IGNORE_RESULT(device_close(d->tim_dev));
    if (d->gpio_dev) COMPAT_IGNORE_RESULT(device_close(d->gpio_dev));
    d->hw_ready = 0;

}

static int buzzer_open(struct device* dev, void* arg)
{
    struct buzzer_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = buzzer_get_drvdata(dev);
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
        ret = buzzer_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int buzzer_close(struct device* dev)
{
    struct buzzer_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = buzzer_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        buzzer_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*buzzer_ioctl_fn_t)(struct buzzer_device* d, void* arg, size_t arg_len, uint32_t ms);
struct buzzer_ioctl_map { buzzer_ioctl_fn_t handler; };


static int buzzer_cmd_beep(struct buzzer_device* d, void* arg, size_t len, uint32_t ms)
{
    int on; uint32_t dur;
    if(!d->hw_ready||!arg||len!=sizeof(int)) return VFS_ERR_INVAL;
    on=*(int*)arg; dur=ms?ms:50U;
    if (d->use_tim && d->tim_dev)
    {
        d->tim.arr     = 1000U;
        d->tim.ccr     = on ? 500U : 0U;
        d->tim.channel = 1U;
        COMPAT_IGNORE_RESULT(device_ioctl(d->tim_dev, TIM_CMD_PWM_UPDATE, &d->tim,
                                        sizeof(d->tim), 100));
    }
    else if (d->gpio_dev)
    {
        d->gpio.level = on ? 1 : 0;
        vfs_gpio_set_level(&d->gpio);
    }
    if(on) osal_delay_ms(dur);
    return VFS_OK;
}
static const struct buzzer_ioctl_map s_buzzer_map[BUZZER_CMD_COUNT] = {
    [BUZZER_CMD_BEEP - BUZZER_CMD_BASE - 1] = { buzzer_cmd_beep },
};


static int buzzer_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct buzzer_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = buzzer_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)BUZZER_CMD_BASE;
    if (off < 1 || off > BUZZER_CMD_COUNT || !s_buzzer_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_buzzer_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations buzzer_fops =
{
    .open  = buzzer_open,
    .close = buzzer_close,
    .ioctl = buzzer_ioctl,
};

static int buzzer_probe(struct device* dev)
{
    struct buzzer_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_buzzer_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_buzzer_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->tim_dev = device_get_phandle_dev(dev, "pwm");
    d->gpio_dev = device_get_phandle_dev(dev, "beep-gpio");
    d->use_tim = !IS_ERR(d->tim_dev);
    if (!d->use_tim && IS_ERR(d->gpio_dev)) { ret = VFS_ERR_INVAL; goto err; }
    if (IS_ERR(d->tim_dev)) d->tim_dev = NULL;
    if (IS_ERR(d->gpio_dev)) d->gpio_dev = NULL;

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = buzzer_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_buzzer_pool_ctrl, pool_idx));
    return ret;
}

static int buzzer_remove(struct device* dev)
{
    struct buzzer_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = buzzer_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_buzzer_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    buzzer_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_buzzer_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(buzzer, "gpio-buzzer-passive", buzzer_probe, buzzer_remove)
