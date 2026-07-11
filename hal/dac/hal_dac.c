/**
 * @license: SPDX-License-Identifier: Apache-2.0 
 * @file hal_dac.c
 * @brief DAC HAL 层 — 硬件抽象接口,硬件直投层
 * @note 所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 * @note 由于DAC是快速热路径外设所以DAC的初始化与配置应该尽量在硬件直投层完成
 * @note 文件约定：返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码 
 * @note 接收的参数必须为指针，并且必须为合法的指针，不能为空指针
 * @note 禁止使用enum,enum的问题dts已经解决没必要在hal层重复定义去映射enum不直观而且麻烦还容易出错
*/
#include "hal_dac.h"
#include "interrupt.h"

/** DAC 下半部工作项 (fn/arg 由 VFS 层绑定), 供 interrupt_virtual_register 注册 */
struct bottom_half_work g_dac_bottom_half_work;


/** @brief DMA TC 标志清除 helper (平台自行实现具体清除逻辑) */
static void hal_dac_dma_clear_tc(uintptr_t dma, uint32_t stream)
{
    (void)dma;
    (void)stream;
}

/**
 * @brief 配置 GPIO 引脚
 * @param gpio GPIO 配置
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
COMPAT_STATIC_INLINE int hal_dac_config_af_pin(hal_dac_gpio_config* gpio)
{
    if (!gpio)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 初始化 DAC 设备
 * @param pdev DAC 设备
 * @param host_cfg DAC 主机配置
 * @param unique_cfg DAC 平台唯一配置
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_dac_device_init(hal_dac_device* pdev, hal_dac_host_config* host_cfg, hal_dac_platform_unique_config* unique_cfg)
{
    if (!pdev || !host_cfg || !unique_cfg)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 关闭 DAC 设备
 * @param dev DAC 设备
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_dac_close(hal_dac_device* dev)
{
    if (!dev)
        return VFS_OK;

    return VFS_OK;
}

/**
 * @brief 设置 DAC 值 默认是12位右对齐
 * @param pdev DAC 设备
 * @param value DAC 值
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_dac_set_value(hal_dac_device* pdev, uint32_t value)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    (void)value;
    return VFS_OK;
}

/**
 * @brief 获取 DMA 当前发送进度 (新增建议接口)
 * @param pdev DAC 设备
 * @param remaining 返回剩余未发送的数据个数
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_dac_get_dma_progress(hal_dac_device* pdev, uint32_t* remaining)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle || !remaining)
        return VFS_ERR_INVAL;

    if (!pdev->host->config.dma_enable)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 向 DMA Buffer 中写入一段波形数据 (常用于流式数据填充)
 * @param pdev DAC 设备
 * @param data 待写入的数据源指针
 * @param len 待写入的数据长度
 * @return 成功写入的长度，失败返回负数 (错误码)
 */
 int hal_dac_write_dma_buffer(hal_dac_device* pdev, const uint16_t* data, uint32_t len)
 {
     if (!pdev || !pdev->host || !pdev->host->dma_cfg.dma_fifo || !data || len == 0)
         return VFS_ERR_INVAL;

     return VFS_ERR_NOTSUPP;
 }
 
/**
 * @brief 获取 DAC 值
 * @param dev DAC 设备
 * @param value DAC 值
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_dac_get_value(hal_dac_device* pdev, uint32_t* value)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle || !value)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 使能 DAC 输出（可选 DMA 请求与 underrun 中断）
 * @param pdev DAC 设备
 * @param dma_req 非 0 时打开 DAC DMA 请求
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int hal_dac_enable_output(hal_dac_device* pdev, int dma_req)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    (void)dma_req;
    return VFS_OK;
}

/**
 * @brief 配置并启动 DMA（可选 TC 中断）
 * @param pdev DAC 设备
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int hal_dac_start_dma_or_it(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle || !pdev->host->dma_cfg.dma_handle
        || !pdev->host->dma_cfg.dma_fifo || !pdev->host->dma_cfg.dma_fifo->buf)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 非 DMA 模式启动 DAC
 * @param pdev DAC 设备
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int hal_dac_base_or_it_start(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

/**
 * @brief DAC 初始化
 */
int hal_dac_init(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 启动 DAC
 */
int hal_dac_start(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    return VFS_ERR_NOTSUPP;
}

/**
 * @brief 暂停 DAC 通道输出（关闭触发与通道，不关闭 DMA Stream）
 */
static int hal_dac_channel_pause(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 暂停 DMA 模式 DAC 输出 (保留 DMA 配置和当前位置)
 * @note  适用于波形/音频的暂停。不关闭 DMA Stream，只关闭 DAC 通道和触发。
 */
int hal_dac_dma_pause(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    if (!pdev->host->config.dma_enable)
        return VFS_ERR_NOTSUPP;

    return VFS_ERR_NOTSUPP;
}

/**
 * @brief 暂停非 DMA 模式 DAC 输出
 */
int hal_dac_base_pause(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    if (pdev->host->config.dma_enable)
        return VFS_ERR_NOTSUPP;

    return VFS_ERR_NOTSUPP;
}

/**
 * @brief 暂停 DAC 输出（按 DMA / 非 DMA 模式分发）
 */
int hal_dac_pause(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    return VFS_ERR_NOTSUPP;
}

/**
 * @brief 恢复 DAC 输出
 * @note  从暂停处继续输出。
 * @param pdev DAC 设备
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_dac_resume(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 停止 DMA 并彻底复位硬件状态
 * @note  增加了 Trigger 关闭和状态标志清理，防止停止后无法重启
 * @param pdev DAC 设备
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_dac_stop_dma(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    if (!pdev->host->config.dma_enable)
        return VFS_ERR_NOTSUPP;

    return VFS_OK;
}

int hal_dac_base_stop(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}
/**
 * @brief 强制停止 (复位状态)
 * @param dev DAC 设备
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_dac_force_stop(hal_dac_device* pdev)
{
    if (!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    return VFS_ERR_NOTSUPP;
}

/* =========================================================================================================================================================== */
/* ISR 虚拟中断回调                                                                                                                                              */
/* =========================================================================================================================================================== */

/**
 * @brief DAC 虚拟中断上半部回调 (ISR 内执行)
 * @param arg 参数 (hal_dac_device*)
 * @param irq_num 虚拟中断号
 * @return VFS_IRQ_ENTRY_BOTTOM 需要下半部; VFS_IRQ_ENTRY_NOBOTTOM 不需要
 * @note  DMA TC: 清 DMA TC 标志; DMA underrun: 清 underrun 标志。
 *        仅当 it_enable 为真时才需要下半部 (由 VFS 层通过 g_dac_bottom_half_work 注册)
 */
int hal_virtual_dac_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);
    hal_dac_device* pdev = (hal_dac_device*)arg;

    if (!pdev || !pdev->host || !pdev->host->dac_handle)
        return VFS_IRQ_ENTRY_NOBOTTOM;

    return VFS_IRQ_ENTRY_NOBOTTOM;
}
