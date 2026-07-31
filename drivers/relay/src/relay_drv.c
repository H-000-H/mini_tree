/* SPDX-License-Identifier: Apache-2.0 */
#include "relay_drv.h"
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

#ifndef DTC_GEN_COUNT_GPIO_RELAY
#define DTC_GEN_COUNT_GPIO_RELAY  1
#endif
#define RELAY_POOL_COUNT  DTC_GEN_COUNT_GPIO_RELAY

struct relay_device
{
    struct file_operations ops;
    struct device* gdev;
    struct vfs_gpio_arg gpio;

    int                    hw_ready;
};

static struct relay_device s_relay_pool[RELAY_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_relay_used[RELAY_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_relay_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "relay";

pre_execution(160)
static void relay_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_relay_pool_ctrl, s_relay_used, RELAY_POOL_COUNT));
}

static struct relay_device* relay_get_drvdata(struct device* dev)
{
    return (struct relay_device*)device_get_priv(dev);
}


static int relay_gpio_on(struct relay_device* d, struct device* g, struct vfs_gpio_arg* a)
{
    int r = device_open(g, NULL);
    if (r != VFS_OK) return r;
    r = device_ioctl(g, GPIO_CMD_GET_LEVEL, a, sizeof(*a), 0);
    if (r != VFS_OK) { COMPAT_IGNORE_RESULT(device_close(g)); return r; }
    return VFS_OK;
}


static int relay_hw_create(struct relay_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = relay_gpio_on(d, d->gdev, &d->gpio); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;
}

static void relay_hw_destroy(struct relay_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->gdev)
        COMPAT_IGNORE_RESULT(device_close(d->gdev));
    d->gpio.obj = NULL;
    d->hw_ready = 0;
}

static int relay_open(struct device* dev, void* arg)
{
    struct relay_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = relay_get_drvdata(dev);
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
        ret = relay_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int relay_close(struct device* dev)
{
    struct relay_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = relay_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        relay_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*relay_ioctl_fn_t)(struct relay_device* d, void* arg, size_t arg_len, uint32_t ms);
struct relay_ioctl_map { relay_ioctl_fn_t handler; };


static int relay_cmd(struct relay_device* d, void* arg, size_t len, uint32_t ms)
{
    COMPAT_IGNORE_RESULT(ms);
    if (!d->hw_ready || !arg || len != sizeof(int)) return VFS_ERR_INVAL;
    d->gpio.level = (*(int*)arg) ? 1 : 0;
    return vfs_gpio_set_level(&d->gpio);
}

static const struct relay_ioctl_map s_relay_map[RELAY_CMD_COUNT] = {
    [RELAY_CMD_SET - RELAY_CMD_BASE - 1] = { relay_cmd },
};


static int relay_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct relay_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = relay_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)RELAY_CMD_BASE;
    if (off < 1 || off > RELAY_CMD_COUNT || !s_relay_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_relay_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations relay_fops =
{
    .open  = relay_open,
    .close = relay_close,
    .ioctl = relay_ioctl,
};

static int relay_probe(struct device* dev)
{
    struct relay_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_relay_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_relay_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->gdev = device_get_phandle_dev(dev, "ctl-gpio");
    if (IS_ERR(d->gdev)) { ret = (int)PTR_ERR(d->gdev); goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = relay_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_relay_pool_ctrl, pool_idx));
    return ret;
}

static int relay_remove(struct device* dev)
{
    struct relay_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = relay_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_relay_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    relay_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_relay_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(relay, "gpio-relay", relay_probe, relay_remove)
