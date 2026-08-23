/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_iwdg.h
 *@brief IWDG HAL — 独立看门狗 (LSI 时钟), 两层模型无 bus
 *@author H-000-H
 *@details
 *   @note        一旦 start 后硬件不可真正关闭; close 仅释放上层引用。
 *   @note        超时按 LSI≈32kHz 估算: timeout ≈ (RLR+1) * (4<<PR) / 32 ms。
 *   @note        文件约定: 返回值用 int + status.h 错误码; 禁止 enum。
 */

#ifndef HAL_IWDG_H
#define HAL_IWDG_H

#include "compiler_compat.h"
#include "status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief IWDG 配置
     * @note  prer/rlr 填 0xFFFFFFFF 时由 HAL 按 timeout_ms 自动推算
     */
    struct hal_iwdg_config
    {
        uint32_t timeout_ms; /**< 超时目标 (ms); 硬件约 0.125ms..32768ms (LSI≈32kHz) */
        uint32_t prer; /**< 预分频寄存器 PR 值 0..6; 0xFFFFFFFF=按 timeout_ms 自动 */
        uint32_t rlr; /**< 重装载 RLR 值 0..0xFFF; 0xFFFFFFFF=按 timeout_ms 自动 */
    };

    /**
     * @brief IWDG 设备对象
     */
    struct hal_iwdg_dev
    {
        struct hal_iwdg_config cfg; /**< 当前生效配置 */
        int active; /**< 是否已 start (硬件喂狗有效) */
        uint32_t normal_timeout_ms; /**< restore 时回退的正常超时 */
    };

    /**
     * @brief 初始化 IWDG 软件对象 (不启动硬件)
     * @param[in] pdev 设备对象
     * @param[in] cfg  配置 (timeout_ms 不可为 0)
     * @return MINI_OK 或 MINI_ERR_INVAL
     */
    int hal_iwdg_init(struct hal_iwdg_dev* pdev,
                      const struct hal_iwdg_config* cfg) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 写入 PR/RLR 并启动 IWDG (之后不可真正关闭)
     * @param[in] pdev 设备对象
     * @return MINI_OK 或 MINI_ERR_INVAL
     */
    int hal_iwdg_start(struct hal_iwdg_dev* pdev) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 喂狗 (写 KR=0xAAAA)
     * @param[in] pdev 设备对象 (须已 start)
     * @return MINI_OK 或 MINI_ERR_NODEV
     */
    int hal_iwdg_feed(struct hal_iwdg_dev* pdev) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 更新超时并重算 PR/RLR; 若已 start 则立即写保护序列热更新
     * @param[in] pdev        设备对象
     * @param[in] timeout_ms  新超时 (ms, 不可为 0; 超出硬件范围时落到最接近档)
     * @return MINI_OK 或 MINI_ERR_INVAL
     */
    int hal_iwdg_set_timeout_ms(struct hal_iwdg_dev* pdev,
                                uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 拉长超时至硬件上限 (~32768ms), 供 OTA 等长耗时场景
     * @param[in] pdev 设备对象
     * @return MINI_OK 或 MINI_ERR_INVAL
     */
    int hal_iwdg_set_long_timeout(struct hal_iwdg_dev* pdev) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 恢复为 init 时记录的 normal_timeout_ms
     * @param[in] pdev 设备对象
     * @return MINI_OK 或 MINI_ERR_INVAL
     */
    int hal_iwdg_restore_timeout(struct hal_iwdg_dev* pdev) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif
#endif
