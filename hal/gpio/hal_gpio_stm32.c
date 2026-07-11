/**
 * SPDX-License-Identifier: Apache-2.0
 * @brief STM32 GPIO 硬件直投实现
 * @note 设计理念：尽量使用ll库函数，减少代码量，提高代码可读性，减少错误率，不是不得已不使用寄存器操作
 */
 #include "hal_gpio.h"
 #include "VFS.h"
 #include "compiler_compat.h"
 
 int hal_gpio_fast_set_level(hal_gpio_dev_t* pdev, int level)
 {
     if (!pdev)
         return VFS_ERR_INVAL;
 
     /**<零分支映射：通过位移运算同时兼容高电平(BSRR低16位)与低电平(BSRR高16位)>*/
 
     return VFS_OK;
 }
 
 int hal_gpio_fast_get_level(hal_gpio_dev_t* pdev, int *level_out)
 {
     if (!pdev || !level_out)
         return VFS_ERR_INVAL;
 
     /**<归一化为标准的 1 或 0 返回给上层>*/
     return VFS_OK;
 }
 
 int hal_gpio_fast_toggle(hal_gpio_dev_t* pdev)
 {
     if (!pdev)
         return VFS_ERR_INVAL;
 
     return VFS_OK;
 }
 
 /* =========================================================================
  * 纯硬件直投初始化与运行时控制 API
  * ========================================================================= */
 
 int hal_gpio_init(hal_gpio_dev_t* pdev)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     /**<开启对应的硬件时钟>*/
     pdev->is_used = true;
    return VFS_OK;
 }
 
 int hal_gpio_deinit(hal_gpio_dev_t* pdev)
 {
    if (!pdev || !pdev->is_used)
        return VFS_ERR_INVAL;
    pdev->is_used = false;
     return VFS_OK;
 }
 
int hal_gpio_set_mode(hal_gpio_dev_t* pdev, uint32_t mode)
{
    if (!pdev || !pdev->is_used)
        return VFS_ERR_INVAL;
 
    return VFS_OK;
}
 
 int hal_gpio_get_mode(hal_gpio_dev_t* pdev, uint32_t *mode)
 {
     if (!pdev || !pdev->is_used || !mode)
         return VFS_ERR_INVAL;
 
     return VFS_OK;
 }
 
 int hal_gpio_set_pull(hal_gpio_dev_t* pdev, uint32_t pull)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     return VFS_OK;
 }
 
 int hal_gpio_get_pull(hal_gpio_dev_t* pdev, uint32_t *pull)
 {
     if (!pdev || !pdev->is_used || !pull)
         return VFS_ERR_INVAL;
 
     return VFS_OK;
 }
 
 int hal_gpio_set_speed(hal_gpio_dev_t* pdev, uint32_t speed)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     return VFS_OK;
 }
 
 int hal_gpio_get_speed(hal_gpio_dev_t* pdev, uint32_t *speed)
 {
     if (!pdev || !pdev->is_used || !speed)
         return VFS_ERR_INVAL;
 
     return VFS_OK;
 }
 
 int hal_gpio_set_output_type(hal_gpio_dev_t* pdev, uint32_t output_type)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     return VFS_OK;
 }
 
 int hal_gpio_get_output_type(hal_gpio_dev_t* pdev, uint32_t *output_type)
 {
     if (!pdev || !pdev->is_used || !output_type)
         return VFS_ERR_INVAL;
 
     return VFS_OK;
 }
 
 int hal_gpio_set_af(hal_gpio_dev_t* pdev, uint32_t af)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     uint32_t pin_pos = (uint32_t)COMPAT_CTZ(pdev->pin);
 
     /**<清空目标引脚所在的4位AFR空间，并写入新的AF值>*/
 
     return VFS_OK;
 }
 
 int hal_gpio_get_af(hal_gpio_dev_t* pdev, uint32_t *af)
 {
     if (!pdev || !pdev->is_used || !af)
         return VFS_ERR_INVAL;
 
     uint32_t pin_pos = (uint32_t)COMPAT_CTZ(pdev->pin);
 
     /**<零分支直读：右移并清空高位，提取出目标引脚对应的 4-bit AF 寄存器值>*/
 
     return VFS_OK;
 }
 
 int hal_gpio_set_af_mode(hal_gpio_dev_t* pdev, uint32_t af)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     /**<1. 直投配置 AFR 寄存器值>*/
     if (hal_gpio_set_af(pdev, af) != VFS_OK)
         return VFS_ERR_INVAL;
 
     /**<2. 将引脚工作模式切换为复用模式>*/
 
     return VFS_OK;
 }