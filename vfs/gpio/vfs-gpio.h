#ifndef VFS_GPIO_H
#define VFS_GPIO_H

#include "VFS.h"
#include "device.h"
#include "hal_gpio.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPIO_CMD_BASE         COMPAT_MAGIC(GPIO)
#define GPIO_CMD_TOGGLE       GPIO_CMD_BASE+0x01
#define GPIO_CMD_SET_LEVEL    GPIO_CMD_BASE+0x02
#define GPIO_CMD_GET_LEVEL    GPIO_CMD_BASE+0x03

struct vfs_gpio_arg
{
    int level;
    hal_pin_t pin;
};

static int inline vfs_gpio_set_level(struct vfs_gpio_arg* vfs_arg)
{
    if (IS_ERR(vfs_arg))
        return PTR_ERR(vfs_arg);
    if (!hal_pin_is_valid(vfs_arg->pin))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_set_level(vfs_arg->pin, vfs_arg->level);
}

static int inline vfs_gpio_get_level(struct vfs_gpio_arg* vfs_arg)
{
    if (IS_ERR(vfs_arg))
        return PTR_ERR(vfs_arg);
    if (!hal_pin_is_valid(vfs_arg->pin))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_get_level(vfs_arg->pin, &vfs_arg->level);
}

static int inline vfs_gpio_toggle(struct vfs_gpio_arg* vfs_arg)
{
    if (IS_ERR(vfs_arg))
        return PTR_ERR(vfs_arg);
    if (!hal_pin_is_valid(vfs_arg->pin))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_toggle(vfs_arg->pin);
}

#ifdef __cplusplus
}
#endif

/*@=========================================================================================================================*
 * 分层隔离:
 *   - vfs-gpio.c 定义 VFS_GPIO_IMPL, 可调用 hal_gpio_init/deinit 等 HAL API
 *   - 其他文件包含本头, 非 fast 的 hal_gpio_* 符号被 #pragma GCC poison
 *   - 保留 hal_gpio_fast_* 及 hal_pin_map_hw_gpio (fast inline 内部依赖)
 *   - 强制走 vfs_gpio_set_level / vfs_gpio_ioctl 等 VFS API
 *@=========================================================================================================================*/
#ifndef VFS_GPIO_IMPL
#pragma GCC poison hal_gpio_init hal_gpio_deinit
#pragma GCC poison hal_gpio_set_level hal_gpio_get_level hal_gpio_read_level
#pragma GCC poison hal_gpio_toggle
#pragma GCC poison hal_gpio_write_raw_dts hal_gpio_read_raw_dts
#pragma GCC poison hal_gpio_dts_resolve
#endif

#endif /* VFS_GPIO_H */
