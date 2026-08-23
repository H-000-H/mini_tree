/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_sdio.h
 *@brief hal sdio 头文件
 *@author H-000-H
 *@details
 *   SDIO HAL — SD/MMC/SDIO 总线抽象
 *   支持 1/4/8 位总线宽度与默认/高速模式, CLK/CMD/D0-D3 引脚可配置
 *   按扇区读写并查询扇区大小、总扇区数与卡类型 (SD/MMC/SDIO)
 */

#ifndef HAL_SDIO_H
#define HAL_SDIO_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*总线宽度与速度枚举*/
    /*===========================================================================================================================================================*/
    /** @brief SDIO 总线宽度 */
    typedef enum
    {
        HAL_SDIO_BUS_1BIT, /* 1-bit 模式 */
        HAL_SDIO_BUS_4BIT, /* 4-bit 模式 */
        HAL_SDIO_BUS_8BIT, /* 8-bit 模式 */
    } hal_sdio_bus_width_t;

    /** @brief SDIO 时钟速度模式 */
    typedef enum
    {
        HAL_SDIO_SPEED_DEFAULT, /* 默认速度 */
        HAL_SDIO_SPEED_HIGH, /* 高速模式 */
    } hal_sdio_speed_t;
    /*===========================================================================================================================================================*/

    /*SDIO 配置*/
    /*===========================================================================================================================================================*/
    struct hal_sdio_config
    {
        int sdio_id; /**< SDIO 控制器编号, 0 = SDIO1 */
        hal_sdio_bus_width_t bus_width; /**< 总线宽度 */
        hal_sdio_speed_t speed; /**< 速度模式 */
        int clk_pin; /**< CLK 引脚 (DTSI 直投, 平台语义: STM32/WCH=port<<16|pin, ESP32=SoC GPIO
                        编号) */
        int cmd_pin; /**< CMD 引脚 */
        int d0_pin; /**< D0 引脚 */
        int d1_pin; /**< D1 引脚, -1 = 未用 */
        int d2_pin; /**< D2 引脚, -1 = 未用 */
        int d3_pin; /**< D3 引脚, -1 = 未用 */
    };
    /*===========================================================================================================================================================*/

    /*卡信息结构*/
    /*===========================================================================================================================================================*/
    struct hal_sdio_info
    {
        uint32_t sector_size; /**< 扇区大小(字节), 通常 512 */
        uint32_t sector_count; /**< 总扇区数 */
        uint32_t card_type; /**< 0 = SD, 1 = MMC, 2 = SDIO */
    };
    /*===========================================================================================================================================================*/

    /*SDIO 实体与 API*/
    /*===========================================================================================================================================================*/
    struct hal_sdio
    {
        int (*init)(struct hal_sdio* sdio, const struct hal_sdio_config* cfg); /**< 初始化 */
        int (*read)(struct hal_sdio* sdio, uint8_t* buf, uint32_t sector,
                    size_t count); /**< 按扇区读 */
        int (*write)(struct hal_sdio* sdio, const uint8_t* buf, uint32_t sector,
                     size_t count); /**< 按扇区写 */
        int (*get_info)(struct hal_sdio* sdio, struct hal_sdio_info* info); /**< 查询卡信息 */
        int (*deinit)(struct hal_sdio* sdio); /**< 反初始化 */
        void* _impl; /**< 平台私有实现指针 */
    };

    /**
     * @brief 初始化 SDIO 操作表 (零初始化后由平台实现填充函数指针)
     * @param[in] sdio SDIO 实体指针
     * @return 成功返回 MINI_OK, sdio 为空返回 MINI_ERR_INVAL
     */
    int hal_sdio_init_struct(struct hal_sdio* sdio) COMPAT_WARN_UNUSED_RESULT;
    /*===========================================================================================================================================================*/

    /*安全停机*/
    /*===========================================================================================================================================================*/
    /**
     * @brief 强制停止 SDIO 控制器 (安全停机/断电保护)
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int hal_sdio_force_stop(void) COMPAT_WARN_UNUSED_RESULT;
    /*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_SDIO_H */
