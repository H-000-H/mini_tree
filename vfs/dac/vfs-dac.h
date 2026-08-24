/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-dac.h
 *@brief vfs-dac 头文件
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   DAC VFS — DAC 子系统 VFS 层
 *   架构位置: [VFS Layer (本文件)] → HAL Layer
 *   职责: file_operations 挂载 + dev_lifecycle (互斥/引用计数) + DTS 解析; I/O 全走 HAL 层。
 *   隔离: 本文件定义 DAC_VFS_IMPL 可调 hal_dac API; 其他文件包含本头时 hal_dac 慢路径符号被 #pragma
 *   GCC poison。
 *   Driver 注册:
 *   - vfs_dac_priv: "dac"
 *   DAC 与 TIM/GPIO 类似, 直接挂载到 VFS 层, 通过文件操作接口进行操作。
 *   @see hal/dac/hal_dac.h  HAL 层接口
 *   --------------------------------------------------------------------------
 */

#ifndef VFS_DAC_H
#define VFS_DAC_H

#include "compiler_compat.h"
#include "hal_dac.h"
#include "status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct vfs_dac_arg_t
    {
        uint32_t value; /**< DAC 输出值 / 校准偏移量 */
        uint32_t remaining; /**< DMA 剩余未发送数据个数 */
        uint32_t len; /**< DMA 写入长度 */
        const uint16_t* data; /**< DMA 波形数据指针 */
        uint32_t pause; /**< 0 = 恢复, 非 0 = 暂停 (DAC_CMD_DMA_PAUSE / DAC_CMD_BASE_PAUSE) */
    };
    typedef struct vfs_dac_arg_t vfs_dac_arg;

    /**
     * @brief Fast Path: 单点写入 DAC 输出值
     * @param[in] pdev DAC 设备指针
     * @param[in] value 目标输出值 (0..4095 对应 12 位分辨率)
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    COMPAT_STATIC_INLINE int vfs_write_dac_value(hal_dac_device* pdev, uint32_t value)
    {
        if (!pdev)
            return MINI_ERR_INVAL;
        return hal_dac_set_value(pdev, value);
    }

    /**
     * @brief Fast Path: 读取当前 DAC 寄存器锁存值
     * @param[in] pdev DAC 设备指针
     * @param[out] out_val 回传当前输出值
     * @return 成功返回 MINI_OK, pdev 或 out_val 为空返回 MINI_ERR_INVAL
     */
    COMPAT_STATIC_INLINE int vfs_read_dac_value(hal_dac_device* pdev, uint32_t* out_val)
    {
        if (!pdev || !out_val)
            return MINI_ERR_INVAL;
        return hal_dac_get_value(pdev, out_val);
    }

/* -------------------------------------------------------------------------- */
/* IOCTL 命令控制字定义 */
/* -------------------------------------------------------------------------- */
#define DAC_CMD_BASE COMPAT_MAGIC(DAC)
#define DAC_CMD_WRITE_VALUE (DAC_CMD_BASE + 1) /**< 单点写入并立即同步触发输出 */
#define DAC_CMD_GET_VALUE (DAC_CMD_BASE + 2) /**< 读取当前寄存器锁存的值 */
#define DAC_CMD_CALIBRATE_OFFSET (DAC_CMD_BASE + 3) /**< DAC 偏移量自校准 */
#define DAC_CMD_DMA_PAUSE (DAC_CMD_BASE + 4) /**< DMA 模式暂停/恢复 */
#define DAC_CMD_START (DAC_CMD_BASE + 5) /**< 启动 DAC 输出 */
#define DAC_CMD_FORCE_STOP (DAC_CMD_BASE + 6) /**< 强制停止 DAC 输出 */
#define DAC_CMD_DMA_WRITE_BUFFER (DAC_CMD_BASE + 7) /**< DMA 写入波形数据 */
#define DAC_CMD_BASE_PAUSE (DAC_CMD_BASE + 8) /**< 非 DMA 暂停/恢复 */
#define DAC_CMD_COUNT 8

#ifdef __cplusplus
}
#endif

/*@=========================================================================================================================*
 * 分层隔离安全锁:
 * - vfs-dac.c 定义 DAC_VFS_IMPL, 可自由调用所有 hal_dac_* 慢路径 API
 * - 其他文件: hal_dac_set_value / hal_dac_get_value 允许内联快路径直透
 * - 其他文件: hal_dac_* 生命周期/配置函数编译报错阻止
 *-------------------------------------------------------------------------- */
#ifndef DAC_VFS_IMPL
#pragma GCC poison hal_dac_device_init hal_dac_close
#pragma GCC poison hal_dac_init hal_dac_start hal_dac_pause hal_dac_resume
#pragma GCC poison hal_dac_dma_pause hal_dac_base_pause
#pragma GCC poison hal_dac_get_dma_progress hal_dac_write_dma_buffer
#pragma GCC poison hal_dac_stop_dma hal_dac_base_stop hal_dac_force_stop
#endif

#endif /* VFS_DAC_H */
