/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_wwdg.h
 *@brief WWDG HAL — 窗口看门狗 (PCLK1), 两层模型无 bus
 *@author H-000-H
 *@details
 *   @note        喂狗必须在窗口内写 T; 由上层保证时机。启动后 close 不关硬件。
 *   @note        超时约: t = (T[5:0]+1) * 4096 * 2^WDGTB / PCLK1
 *   @note        文件约定: 返回值用 int + status.h 错误码; 禁止 enum。
 */

#ifndef HAL_WWDG_H
#define HAL_WWDG_H

#include "compiler_compat.h"
#include "status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief WWDG 配置
     * @note  counter(T) 必须在 0x40..0x7F; window(W) 可为 0..0x7F (典型 W < T)
     */
    struct hal_wwdg_config
    {
        uint32_t window; /**< W[6:0] 窗口比较值, 0..0x7F (非 T 的 0x40 下限约束) */
        uint32_t counter; /**< T[6:0] 初值, 必须 0x40..0x7F (T[6] 为 1) */
        uint32_t prescaler; /**< WDGTB 0..3 → /1 /2 /4 /8 */
        uint32_t ewi_enable; /**< 早期唤醒中断 EWI: 0=关, 1=开 */
    };

    /**
     * @brief WWDG 设备对象
     */
    struct hal_wwdg_dev
    {
        struct hal_wwdg_config cfg; /**< 配置 */
        int active; /**< 是否已 start */
    };

    /**
     * @brief 初始化 WWDG 软件对象 (不启动硬件)
     * @param[in] pdev 设备对象
     * @param[in] cfg  配置 (校验 counter/window 范围)
     * @return MINI_OK 或 MINI_ERR_INVAL
     */
    int hal_wwdg_init(struct hal_wwdg_dev* pdev,
                      const struct hal_wwdg_config* cfg) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 使能 APB1 时钟并启动 WWDG (置 WDGA)
     * @param[in] pdev 设备对象
     * @return MINI_OK 或 MINI_ERR_INVAL
     */
    int hal_wwdg_start(struct hal_wwdg_dev* pdev) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 窗口内喂狗 (重写 CR 的 T 字段)
     * @param[in] pdev 设备对象 (须已 start); 调用方负责窗口时机
     * @return MINI_OK 或 MINI_ERR_NODEV
     */
    int hal_wwdg_feed(struct hal_wwdg_dev* pdev) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif
#endif
