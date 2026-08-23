/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_flash.h
 *@brief hal flash 头文件
 *@author H-000-H
 *@details
 *   Flash HAL — 只读巡检与应用地址查询
 *   提供 hal_flash_read 用于 CRC 校验与镜像巡检
 *   暴露应用程序起始地址与大小供引导/校验使用
 */

#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*Flash 读取 API*/
    /*===========================================================================================================================================================*/
    /**
     * @brief 只读 Flash 数据 (用于 CRC 校验与镜像巡检)
     * @param[in] addr 起始地址
     * @param[out] buf 数据缓冲区
     * @param[in] len 读取长度
     * @return MINI_OK 成功; MINI_ERR_INVAL 地址越界; weak stub 返回 MINI_ERR_NOTSUPP
     */
    int hal_flash_read(uint32_t addr, uint8_t* buf, size_t len);
    /*===========================================================================================================================================================*/

    /*应用程序地址与大小*/
    /*===========================================================================================================================================================*/
    /**
     * @brief 获取应用程序起始地址 (引导/校验用)
     * @return 应用起始地址
     */
    uint32_t hal_flash_get_app_addr(void); /* 应用程序起始地址 */
    /**
     * @brief 获取应用程序大小 (引导/校验用)
     * @return 应用大小 (字节)
     */
    uint32_t hal_flash_get_app_size(void); /* 应用程序大小(字节) */
    /*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_H */
