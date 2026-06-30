/* SPDX-License-Identifier: Apache-2.0 */
#define VFS_GPIO_IMPL
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
#define kTAG               "vfs-gpio"

/* VFS priv: hal_gpio_obj_t 嵌入 (非指针), 生命周期由 VFS osal_pool 管理。
 * HAL 层无池管理, 无 alloc/free。 */
struct vfs_gpio_priv
{
    struct file_operations ops;
    struct osal_mutex*     io_mutex;
    hal_gpio_obj_t         obj;        /* probe 时直接填硬件直投值 */
    struct hal_gpio_mode_cfg mode_cfg;
    int                    default_level;
    int                    pool_idx;
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
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_gpio_priv_pool_ctrl, s_gpio_priv_used,
                                        VFS_GPIO_PIN_COUNT));
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

    first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = hal_gpio_init(&priv->obj, &priv->mode_cfg);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
        else if (priv->default_level != 0)
            ret = hal_gpio_fast_set_level(&priv->obj, priv->default_level);
    }

    if (ret == VFS_OK)
        dev_lc_open_end(lc);
    return ret;
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

    last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last)
        COMPAT_IGNORE_RESULT(hal_gpio_deinit(&priv->obj));

    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief GPIO 设备 ioctl 控制 (toggle/get_level/set_level 等命令)
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
    struct vfs_gpio_priv* priv;
    struct dev_lifecycle* lc;
    int ret = VFS_ERR_INVAL;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_gpio_priv, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    switch (cmd)
    {
    case GPIO_CMD_TOGGLE:
    {
        const struct vfs_gpio_arg* vfs_arg = (const struct vfs_gpio_arg*)arg;
        if (!vfs_arg || arg_len != sizeof(*vfs_arg))
            ret = VFS_ERR_INVAL;
        else
            ret = hal_gpio_fast_toggle(&priv->obj);
        break;
    }
    case GPIO_CMD_GET_LEVEL:
    {
        struct vfs_gpio_arg* vfs_arg = (struct vfs_gpio_arg*)arg;
        if (!vfs_arg || arg_len != sizeof(*vfs_arg))
            ret = VFS_ERR_INVAL;
        else
        {
            vfs_arg->obj = &priv->obj;
            ret = hal_gpio_fast_get_level(&priv->obj, &vfs_arg->level);
        }
        break;
    }
    case GPIO_CMD_SET_LEVEL:
    {
        const struct vfs_gpio_arg* vfs_arg = (const struct vfs_gpio_arg*)arg;
        if (!vfs_arg || arg_len != sizeof(*vfs_arg))
            ret = VFS_ERR_INVAL;
        else
            ret = hal_gpio_fast_set_level(&priv->obj, vfs_arg->level);
        break;
    }
    default:
        ret = VFS_ERR_INVAL;
        break;
    }

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
    int port_val = 0;
    int pin_val  = 0;
    int clk_val  = 0;
    int mode_val = 0;
    int pull_val = 0;
    int default_level = 0;
    int pool_idx;

    pool_idx = osal_pool_claim(&s_gpio_priv_pool_ctrl);
    if (pool_idx < 0)
    {
        SYS_LOGE(kTAG, "Failed to claim gpio pool");
        return VFS_ERR_NOMEM;
    }

    priv = &s_gpio_priv_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;
    priv->mode_cfg = (struct hal_gpio_mode_cfg){
        .mode = 0,
        .pull = 0,
    };

    /* DTSI 直接提供硬件直投值: STM32 = GPIOA_BASE/GPIO_PIN_5/LL_AHB1_GRP1_PERIPH_GPIOA,
     * ESP32 = 0/gpio_num (clk_periph 留 0, ESP32 无 AHB 时钟概念)。
     * VFS 只读取透传, 不做任何翻译, 直接填入嵌入的 hal_gpio_obj_t, 无需 HAL alloc。 */
    if (device_get_prop_int(pdev, "gpio-port", &port_val) ||
        device_get_prop_int(pdev, "gpio-pin", &pin_val) ||
        device_get_prop_int(pdev, "gpio-clk",  &clk_val)  ||
        device_get_prop_int(pdev, "gpio-mode", &mode_val) ||
        device_get_prop_int(pdev, "gpio-pull", &pull_val))
        goto err_pool;

    priv->obj.port      = (uintptr_t)port_val;
    priv->obj.pin       = (uint16_t)pin_val;
    priv->obj.clk_periph = (uint32_t)clk_val;
    priv->obj.is_used    = true;
    priv->mode_cfg.mode  = (uint32_t)mode_val;
    priv->mode_cfg.pull  = (uint32_t)pull_val;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "default-level", &default_level));
    priv->default_level = default_level;

    if (osal_mutex_create_static(&priv->io_mutex, s_gpio_mutex_storage[pool_idx],
                                 sizeof(s_gpio_mutex_storage[pool_idx])) != 0)
        goto err_pool;

    device_lc_bind(pdev, priv->io_mutex);
    priv->ops = gpio_fops;
    pdev->ops = &priv->ops;

    if (device_set_priv(pdev, priv) != VFS_OK)
        goto err_mutex;

    SYS_LOGI(kTAG, "probe OK: port=0x%x pin=0x%x clk=0x%x mode=%d",
             (unsigned)port_val, (unsigned)pin_val, (unsigned)clk_val, priv->mode_cfg.mode);
    return VFS_OK;

err_mutex:
    pdev->ops = NULL;                  /* 切断 fops, 防 UAF */
    dev_lc_reset(device_lc(pdev));     /* 切断 io_lock 绑定 */
    osal_mutex_destroy(priv->io_mutex);
    priv->io_mutex = NULL;
err_pool:
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_gpio_priv_pool_ctrl, pool_idx);
    return VFS_ERR_IO;
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
        SYS_LOGE(kTAG, "remove drain failed");
        return VFS_ERR_IO;
    }

    osal_mutex_destroy(priv->io_mutex);
    priv->io_mutex = NULL;
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_gpio_priv_pool_ctrl, pool_idx);
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(gpios, "heterogeneous,gpios", vfs_gpio_probe, vfs_gpio_remove)
