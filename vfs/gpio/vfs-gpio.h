/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-gpio.h
 *@brief vfs-gpio 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   GPIO VFS — 通用 IO 口 VFS 层
 *   架构位置: [VFS Layer (本文件)] → HAL Layer (无 bus)
 *   职责: file_operations + dev_lifecycle + DTS; ioctl 电平读/写/翻转。
 *   中断: open 时若 gpio-intr 非 disable，HAL 路由到 VIRQ(gpio, virq-idx)；
 *   业务上下半部由产品驱动 interrupt_virtual_register，不经本层 ioctl。
 *   @see hal/gpio/hal_gpio.h
 *   @see interrupt/interrupt.h
 *   @=========================================================================================================================
 */

#ifndef VFS_GPIO_H
#define VFS_GPIO_H

#include "device.h"
#include "hal_gpio.h"
#include "status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define GPIO_CMD_BASE COMPAT_MAGIC(GPIO)
#define GPIO_CMD_TOGGLE GPIO_CMD_BASE + 0x01
#define GPIO_CMD_SET_LEVEL GPIO_CMD_BASE + 0x02
#define GPIO_CMD_GET_LEVEL GPIO_CMD_BASE + 0x03
#define GPIO_CMD_COUNT 3

    struct vfs_gpio_arg
    {
        int level; /**< 电平值 (0=LOW, 1=HIGH) */
        hal_gpio_dev_t* obj; /**< 指向 VFS priv 嵌入的 HAL 对象 */
    };

    /**
     * @brief 快速设置 GPIO 输出电平 (热路径, 不走生命周期)
     * @param[in] vfs_arg 参数包 (含 obj 与 level)
     * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL
     */
    COMPAT_STATIC_INLINE int vfs_gpio_set_level(struct vfs_gpio_arg* vfs_arg)
    {
        if (IS_ERR(vfs_arg))
            return PTR_ERR(vfs_arg);
        if (!vfs_arg || !vfs_arg->obj)
            return VFS_ERR_INVAL;
        return hal_gpio_fast_set_level(vfs_arg->obj, vfs_arg->level);
    }

    /**
     * @brief 快速读取 GPIO 输入电平 (热路径, 不走生命周期)
     * @param[in] vfs_arg 参数包 (含 obj; level 回传结果)
     * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL
     */
    COMPAT_STATIC_INLINE int vfs_gpio_get_level(struct vfs_gpio_arg* vfs_arg)
    {
        if (IS_ERR(vfs_arg))
            return PTR_ERR(vfs_arg);
        if (!vfs_arg || !vfs_arg->obj)
            return VFS_ERR_INVAL;
        return hal_gpio_fast_get_level(vfs_arg->obj, &vfs_arg->level);
    }

    /**
     * @brief 快速翻转 GPIO 输出电平 (热路径, 不走生命周期)
     * @param[in] vfs_arg 参数包 (含 obj)
     * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL
     */
    COMPAT_STATIC_INLINE int vfs_gpio_toggle(struct vfs_gpio_arg* vfs_arg)
    {
        if (IS_ERR(vfs_arg))
            return PTR_ERR(vfs_arg);
        if (!vfs_arg || !vfs_arg->obj)
            return VFS_ERR_INVAL;
        return hal_gpio_fast_toggle(vfs_arg->obj);
    }

#ifdef __cplusplus
}
#endif

/*@=========================================================================================================================*
 * 分层隔离安全锁:
 * - vfs-gpio.c 定义 VFS_GPIO_IMPL, 可自由调用各个基础配置 API
 * - 其他任何包含本头文件的普通驱动/应用文件，如果尝试绕过 VFS 直接调用以下函数，编译时直接报错阻止
 * - 唯独保留 hal_gpio_fast_*，允许内联快路径高频直透
 *@=========================================================================================================================*/
#ifndef VFS_GPIO_IMPL
#pragma GCC poison hal_gpio_init hal_gpio_deinit
#pragma GCC poison hal_gpio_set_mode hal_gpio_get_mode
#pragma GCC poison hal_gpio_set_pull hal_gpio_get_pull
#pragma GCC poison hal_gpio_set_speed hal_gpio_get_speed
#pragma GCC poison hal_gpio_set_output_type hal_gpio_get_output_type
#pragma GCC poison hal_gpio_set_af hal_gpio_get_af hal_gpio_set_af_mode
#pragma GCC poison hal_gpio_irq_enable hal_gpio_irq_disable
#endif

#endif /* VFS_GPIO_H */
