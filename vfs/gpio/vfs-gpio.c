/* SPDX-License-Identifier: Apache-2.0 */
#define VFS_GPIO_IMPL  /* 激活豁免权限，允许本文件调用被毒死的 HAL 慢路径 API */
#include "vfs-gpio.h"
#include "VFS.h"
#include "board_config.h"
#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "hal_gpio.h"
#include "osal.h"
#include "system_log.h"

#include <stdint.h>

#define VFS_GPIO_PIN_COUNT DTC_GEN_COUNT_HETEROGENEOUS_GPIOS

static const char* const s_kTag = "vfs-gpio";

struct vfs_gpio_priv
{
    struct file_operations              ops;
    struct osal_mutex*                  io_mutex;
    hal_gpio_dev_t                      obj;
    int                                 default_level;
    int                                 pool_idx;
};

static struct vfs_gpio_priv s_gpio_priv_pool[VFS_GPIO_PIN_COUNT] COMPAT_ALIGNED(4);
static uint8_t              s_gpio_priv_used[VFS_GPIO_PIN_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t          s_gpio_priv_pool_ctrl COMPAT_ALIGNED(4);
static uint8_t s_gpio_mutex_storage[VFS_GPIO_PIN_COUNT][OSAL_MUTEX_STORAGE_SIZE] COMPAT_ALIGNED(4);

/**
 * @brief GPIO VFS 私有数据池启动初始化
 */
pre_execution(160)
static void gpio_priv_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_gpio_priv_pool_ctrl, s_gpio_priv_used,VFS_GPIO_PIN_COUNT));
}

/**
 * @brief GPIO 设备打开操作 (引用计数, 首次打开时调用 HAL 初始化)
 * @param pdev 设备对象指针
 * @param arg 打开参数 (未使用)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_gpio_open(struct device* pdev, void* arg)
{
    struct vfs_gpio_priv* priv;
    struct dev_lifecycle* lc;
    int first;
    int ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_gpio_priv, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;

    if (first == 1)
    {
        ret = hal_gpio_init(&priv->obj);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
        if (priv->default_level != 0)
        {
            ret = hal_gpio_fast_set_level(&priv->obj, priv->default_level);
            if (ret != VFS_OK)
            {
                COMPAT_IGNORE_RESULT(hal_gpio_deinit(&priv->obj));
                dev_lc_open_abort(lc);
                return ret;
            }
        }
    }

    dev_lc_open_end(lc);
    return VFS_OK;
}

/**
 * @brief GPIO 设备关闭操作 (引用计数, 末次关闭时调用 HAL 反初始化)
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_gpio_close(struct device* pdev)
{
    struct vfs_gpio_priv* priv;
    struct dev_lifecycle* lc;
    int last;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_gpio_priv, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;

    if (last)
        COMPAT_IGNORE_RESULT(hal_gpio_deinit(&priv->obj));

    dev_lc_close_end(lc);
    return VFS_OK;
}

/*===========================================================================================================================================================*/
/* ioctl 命令处理函数 — 每个函数封装一个 HAL 调用                                                                                                              */
/*===========================================================================================================================================================*/
typedef int (*gpio_cmd_handler_t)(struct vfs_gpio_priv* priv, void* arg, size_t arg_len);

typedef struct {
    gpio_cmd_handler_t handler;
} gpio_ioctl_map_t;

/**
 * @brief GPIO 命令处理: 翻转电平
 */
static int gpio_cmd_toggle(struct vfs_gpio_priv* priv, void* arg, size_t arg_len)
{
    const struct vfs_gpio_arg* vfs_arg = (const struct vfs_gpio_arg*)arg;
    if (!vfs_arg || arg_len != sizeof(*vfs_arg))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_toggle(&priv->obj);
}

/**
 * @brief GPIO 命令处理: 设置电平
 */
static int gpio_cmd_set_level(struct vfs_gpio_priv* priv, void* arg, size_t arg_len)
{
    const struct vfs_gpio_arg* vfs_arg = (const struct vfs_gpio_arg*)arg;
    if (!vfs_arg || arg_len != sizeof(*vfs_arg))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_set_level(&priv->obj, vfs_arg->level);
}

/**
 * @brief GPIO 命令处理: 获取电平 (回写 obj 供快路径复用)
 */
static int gpio_cmd_get_level(struct vfs_gpio_priv* priv, void* arg, size_t arg_len)
{
    struct vfs_gpio_arg* vfs_arg = (struct vfs_gpio_arg*)arg;
    if (!vfs_arg || arg_len != sizeof(*vfs_arg))
        return VFS_ERR_INVAL;
    vfs_arg->obj = &priv->obj;
    return hal_gpio_fast_get_level(&priv->obj, &vfs_arg->level);
}

/*===========================================================================================================================================================*/
/* ioctl 命令映射表 — index = (cmd - GPIO_CMD_BASE - 1), 与 GPIO_CMD_* 一一对应                                                                                */
/*===========================================================================================================================================================*/
static const gpio_ioctl_map_t s_gpio_ioctl_map[GPIO_CMD_COUNT] = {
    [GPIO_CMD_TOGGLE - GPIO_CMD_BASE - 1]    = { gpio_cmd_toggle },
    [GPIO_CMD_SET_LEVEL - GPIO_CMD_BASE - 1] = { gpio_cmd_set_level },
    [GPIO_CMD_GET_LEVEL - GPIO_CMD_BASE - 1] = { gpio_cmd_get_level },
};

/**
 * @brief GPIO 设备 ioctl 控制 (命令映射表 O(1) 派发)
 * @param pdev 设备对象指针
 * @param cmd 控制命令 (GPIO_CMD_*)
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (未使用)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_gpio_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len,
                          uint32_t timeout_ms)
{
    struct vfs_gpio_priv*  priv;
    struct dev_lifecycle*  lc;
    gpio_cmd_handler_t     handler = NULL;
    int                    ret;
    int32_t                offset;
    uint8_t                index;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    priv = container_of(pdev->ops, struct vfs_gpio_priv, ops);

    offset = (int32_t)cmd - (int32_t)GPIO_CMD_BASE;
    if (offset < 1 || offset > GPIO_CMD_COUNT)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }
    index = (uint8_t)(offset - 1);

    handler = s_gpio_ioctl_map[index].handler;
    if (handler != NULL)
        ret = handler(priv, arg, arg_len);
    else
        ret = VFS_ERR_INVAL;

    if (ret != VFS_OK && ret != VFS_ERR_INVAL)
        ret = VFS_ERR_IO;

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations gpio_fops =
{
    .open  = vfs_gpio_open,
    .close = vfs_gpio_close,
    .ioctl = vfs_gpio_ioctl,
};

/**
 * @brief GPIO 设备探测: 申请池槽, 解析 DTS 硬件直投值, 绑定 fops 与生命周期
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_gpio_probe(struct device* pdev)
{
    struct vfs_gpio_priv* priv;
    int port_val = 0, pin_val = 0, clk_val = 0;
    int mode_val = 0, pull_val = 0, speed_val = 0;
    int otype_val = 0, af_val = 0, default_level = 0;
    int deinit_mode_val = 0, deinit_pull_val = 0;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_gpio_priv_pool_ctrl);
    if (pool_idx < 0)
    {
        SYS_LOGE(s_kTag, "Failed to claim gpio pool");
        return VFS_ERR_NOMEM;
    }

    priv = &s_gpio_priv_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (device_get_prop_int(pdev, "gpio-port", &port_val) != VFS_OK ||
        device_get_prop_int(pdev, "gpio-pin", &pin_val) != VFS_OK ||
        device_get_prop_int(pdev, "gpio-clk",  &clk_val) != VFS_OK ||
        device_get_prop_int(pdev, "gpio-mode", &mode_val) != VFS_OK ||
        device_get_prop_int(pdev, "gpio-pull", &pull_val) != VFS_OK)
    {
        ret = VFS_ERR_INVAL;
        goto err_pool;
    }

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "gpio-speed", &speed_val));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "gpio-otype", &otype_val));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "gpio-af", &af_val));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "gpio-deinit-mode", &deinit_mode_val));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "gpio-deinit-pull", &deinit_pull_val));

    priv->obj.port             = (uintptr_t)port_val;
    priv->obj.pin              = (uint16_t)pin_val;
    priv->obj.clk_bus      = (uint32_t)clk_val;
    priv->obj.is_used          = true;
    priv->obj.cfg.mode         = (uint32_t)mode_val;
    priv->obj.cfg.pull         = (uint32_t)pull_val;
    priv->obj.cfg.speed        = (uint32_t)speed_val;
    priv->obj.cfg.output_type  = (uint32_t)otype_val;
    priv->obj.cfg.af           = (uint32_t)af_val;
    priv->obj.cfg.deinit_mode  = (uint32_t)deinit_mode_val;
    priv->obj.cfg.deinit_pull  = (uint32_t)deinit_pull_val;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "default-level", &default_level));
    priv->default_level = default_level;

    if (osal_mutex_create_static(&priv->io_mutex, s_gpio_mutex_storage[pool_idx],
                                 sizeof(s_gpio_mutex_storage[pool_idx])) != 0)
    {
        ret = VFS_ERR_NOMEM;
        goto err_pool;
    }

    device_lc_bind(pdev);
    priv->ops = gpio_fops;
    pdev->ops = &priv->ops;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_mutex;
    }

    SYS_LOGI(s_kTag, "probe OK: port=0x%x pin=0x%x clk=0x%x mode=%d",
             (unsigned)port_val, (unsigned)pin_val, (unsigned)clk_val, priv->obj.cfg.mode);
    return VFS_OK;

err_mutex:
    pdev->ops = NULL;
    dev_lc_reset(device_lc(pdev));
    osal_mutex_destroy(priv->io_mutex);
    priv->io_mutex = NULL;
err_pool:
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_gpio_priv_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief GPIO 设备移除: 拒新 IO, 等待已有 IO 排空, 释放池槽与互斥锁
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_gpio_remove(struct device* pdev)
{
    struct vfs_gpio_priv* priv;
    struct dev_lifecycle* lc;
    int pool_idx;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_gpio_priv, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        SYS_LOGE(s_kTag, "remove drain failed");
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    osal_mutex_destroy(priv->io_mutex);
    priv->io_mutex = NULL;
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_gpio_priv_pool_ctrl, pool_idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(gpios, "heterogeneous,gpios", vfs_gpio_probe, vfs_gpio_remove)