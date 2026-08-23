/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file max7219_bridge.h
 *@brief MAX7219 ↔ 应用层整帧刷新薄封装
 *@author H-000-H

 */

#ifndef MAX7219_BRIDGE_H
#define MAX7219_BRIDGE_H

#include "device.h"
#include "max7219_drv.h"
#include "status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 整帧刷新 8×8 点阵
     * @param[in] pdev MAX7219 device
     * @param[in] rows 帧缓冲（长度 MAX7219_MATRIX_BYTES，MSB=左）
     * @param[in] timeout_ms 超时（ms）
     * @return MINI_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int max7219_flush_matrix(struct device* pdev, const uint8_t rows[MAX7219_MATRIX_BYTES], uint32_t timeout_ms)
    {
        struct max7219_fb a = {.rows = rows, .len = MAX7219_MATRIX_BYTES};
        if (!pdev || !rows)
            return MINI_ERR_INVAL;
        return device_ioctl(pdev, MAX7219_CMD_FLUSH_FB, &a, sizeof(a), timeout_ms);
    }

#ifdef __cplusplus
}
#endif

#endif /* MAX7219_BRIDGE_H */
