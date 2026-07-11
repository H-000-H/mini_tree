/** * @license SPDX-License-Identifier: Apache-2.0
 * @file vfs-dac.c
 * @brief DAC VFS 实现 — DAC 子系统 VFS 层实现文件
 */
#define DAC_VFS_IMPL  /* 激活豁免权限，允许本文件调用被毒死的 HAL 慢路径 API */
#include "vfs-dac.h"
#include "VFS.h"
#include "board_config.h"
#include "buffer.h"
#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "system_log.h"

#ifndef DAC_VFS_DEVICE_COUNT
#define DAC_VFS_DEVICE_COUNT 4
#endif

#ifndef DAC_DMA_BUFFER_SIZE
#define DAC_DMA_BUFFER_SIZE 256
#endif

#define VFS_DAC_PIN_FIELD_COUNT 8
#define VFS_DAC_DMA_FIELD_COUNT 7

static const char* const s_kTag = "vfs-dac";

struct vfs_dac_priv
{
    struct file_operations              ops;
    struct hal_dac_host_cfg             cfg;
    struct hal_dac_platform_unique_cfg  unique;
    struct hal_dac_dev                  dac;
    struct fifo_spsc                    dma_fifo;                       /**< DMA 模式 FIFO 句柄 (dma_enable 时由 probe 初始化) */
    Fifo_Data_type                      dma_data_buf[DAC_DMA_BUFFER_SIZE] COMPAT_ALIGNED(32); /**< DMA 波形数据缓冲区 */
    int                                 pool_idx;
};

static struct vfs_dac_priv  s_dac_priv_pool[DAC_VFS_DEVICE_COUNT] COMPAT_ALIGNED(4);
static uint8_t              s_dac_priv_used[DAC_VFS_DEVICE_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t          s_dac_priv_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief DAC Ioctl 命令处理函数指针类型
 */
typedef int (*dac_cmd_handler_t)(struct vfs_dac_priv* priv, void* arg, size_t arg_len);

typedef struct {
    dac_cmd_handler_t handler;
} dac_ioctl_map_t;

/*===========================================================================================================================================================*/
/* ioctl 命令处理函数 — 每个函数封装一个 HAL 调用                                                                                                                */
/*===========================================================================================================================================================*/

static int dac_cmd_write_value(struct vfs_dac_priv* priv, void* arg, size_t arg_len)
{
    if (!arg || arg_len < sizeof(vfs_dac_arg))
        return VFS_ERR_INVAL;
    return hal_dac_set_value(&priv->dac, ((const vfs_dac_arg*)arg)->value);
}

static int dac_cmd_get_value(struct vfs_dac_priv* priv, void* arg, size_t arg_len)
{
    if (!arg || arg_len < sizeof(vfs_dac_arg))
        return VFS_ERR_INVAL;
    return hal_dac_get_value(&priv->dac, &((vfs_dac_arg*)arg)->value);
}

static int dac_cmd_calibrate_offset(struct vfs_dac_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(priv); COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    return VFS_ERR_NOTSUPP;
}

static int dac_cmd_dma_pause(struct vfs_dac_priv* priv, void* arg, size_t arg_len)
{
    if (!arg || arg_len < sizeof(vfs_dac_arg))
        return VFS_ERR_INVAL;
    const vfs_dac_arg* a = (const vfs_dac_arg*)arg;
    if (a->pause)
        return hal_dac_dma_pause(&priv->dac);
    return hal_dac_resume(&priv->dac);
}

static int dac_cmd_start(struct vfs_dac_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    return hal_dac_start(&priv->dac);
}

static int dac_cmd_force_stop(struct vfs_dac_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    return hal_dac_force_stop(&priv->dac);
}

static int dac_cmd_dma_write_buffer(struct vfs_dac_priv* priv, void* arg, size_t arg_len)
{
    if (!arg || arg_len < sizeof(vfs_dac_arg))
        return VFS_ERR_INVAL;
    const vfs_dac_arg* a = (const vfs_dac_arg*)arg;
    if (!a->data || !a->len)
        return VFS_ERR_INVAL;
    return hal_dac_write_dma_buffer(&priv->dac, a->data, a->len);
}

static int dac_cmd_base_pause(struct vfs_dac_priv* priv, void* arg, size_t arg_len)
{
    if (!arg || arg_len < sizeof(vfs_dac_arg))
        return VFS_ERR_INVAL;
    const vfs_dac_arg* a = (const vfs_dac_arg*)arg;
    if (a->pause)
        return hal_dac_base_pause(&priv->dac);
    return hal_dac_resume(&priv->dac);
}

/*===========================================================================================================================================================*/
/* ioctl 命令映射表 — index = (cmd - DAC_CMD_BASE - 1), 与 DAC_CMD_* 编号一一对应                                                                               */
/*===========================================================================================================================================================*/

static const dac_ioctl_map_t s_dac_ioctl_map[DAC_CMD_COUNT] =
{
    [DAC_CMD_WRITE_VALUE      - DAC_CMD_BASE - 1] = { dac_cmd_write_value },
    [DAC_CMD_GET_VALUE        - DAC_CMD_BASE - 1] = { dac_cmd_get_value },
    [DAC_CMD_CALIBRATE_OFFSET - DAC_CMD_BASE - 1] = { dac_cmd_calibrate_offset },
    [DAC_CMD_DMA_PAUSE        - DAC_CMD_BASE - 1] = { dac_cmd_dma_pause },
    [DAC_CMD_START            - DAC_CMD_BASE - 1] = { dac_cmd_start },
    [DAC_CMD_FORCE_STOP       - DAC_CMD_BASE - 1] = { dac_cmd_force_stop },
    [DAC_CMD_DMA_WRITE_BUFFER - DAC_CMD_BASE - 1] = { dac_cmd_dma_write_buffer },
    [DAC_CMD_BASE_PAUSE       - DAC_CMD_BASE - 1] = { dac_cmd_base_pause },
};

/**
 * @brief DAC Host VFS 私有数据池启动初始化
 */
pre_execution(151)
static void vfs_dac_priv_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_dac_priv_pool_ctrl, s_dac_priv_used, DAC_VFS_DEVICE_COUNT));
}

/**
 * @brief 解析 DAC Host DTS 属性 (硬件直投值), 填入 hal_dac_host_config
 */
static int vfs_dac_priv_parse_dts(struct device* pdev, struct hal_dac_host_cfg* cfg)
{
    int dac_base = 0;
    int tmp      = 0;
    int pin_arr[VFS_DAC_PIN_FIELD_COUNT];
    int dma_arr[VFS_DAC_DMA_FIELD_COUNT];

    if (!pdev || !cfg)
        return VFS_ERR_INVAL;

    if (device_get_prop_int(pdev, "dac-base", &dac_base) != VFS_OK)
        return VFS_ERR_INVAL;
    cfg->dac_handle = (uintptr_t)dac_base;

    if (device_get_prop_int(pdev, "dac-clk", &tmp) != VFS_OK)
        return VFS_ERR_INVAL;
    cfg->config.dac_clk_periph = (uint32_t)tmp;

    if (device_get_prop_int(pdev, "channel", &tmp) != VFS_OK)
        return VFS_ERR_INVAL;
    cfg->config.channel = (uint32_t)tmp;

    if (device_get_prop_int(pdev, "trigger-source", &tmp) != VFS_OK)
        return VFS_ERR_INVAL;
    cfg->config.trigger_source = (uint32_t)tmp;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "data-align", &tmp));
    cfg->config.data_align = (uint32_t)tmp;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "output-buf", &tmp));
    cfg->config.output_buf = (uint32_t)tmp;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-enable", &tmp));
    cfg->config.dma_enable = (uint32_t)tmp;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "it-enable", &tmp));
    cfg->config.it_enable = (uint32_t)tmp;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "wave-auto-generation-mode", &tmp));
    cfg->config.wave_auto_generation_mode = (uint32_t)tmp;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "wave-auto-generation-config", &tmp));
    cfg->config.wave_auto_generation_config = (uint32_t)tmp;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sw-trigger", &tmp));
    cfg->config.dac_sw_trigger = (uint32_t)tmp;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-data-align", &tmp));
    cfg->config.dma_data_align = (uint32_t)tmp;

    if (device_get_prop_int_array(pdev, "gpio-pin", pin_arr, VFS_DAC_PIN_FIELD_COUNT) == VFS_DAC_PIN_FIELD_COUNT)
    {
        cfg->gpio_cfg.port        = (uintptr_t)pin_arr[0];
        cfg->gpio_cfg.pin         = (uint16_t)pin_arr[1];
        cfg->gpio_cfg.clk_bus     = (uint32_t)pin_arr[2];
        cfg->gpio_cfg.af          = (uint32_t)pin_arr[3];
        cfg->gpio_cfg.output_type = (uint32_t)pin_arr[4];
        cfg->gpio_cfg.speed       = (uint32_t)pin_arr[5];
        cfg->gpio_cfg.mode        = (uint32_t)pin_arr[6];
        cfg->gpio_cfg.pull        = (uint32_t)pin_arr[7];
    }
    else
        return VFS_ERR_INVAL;

    if (cfg->config.dma_enable)
    {
        if (device_get_prop_int_array(pdev, "dma-cfg", dma_arr, VFS_DAC_DMA_FIELD_COUNT) != VFS_DAC_DMA_FIELD_COUNT)
            return VFS_ERR_INVAL;

        cfg->dma_cfg.dma_handle      = (uintptr_t)dma_arr[0];
        cfg->dma_cfg.dma_stream      = (uint32_t)dma_arr[1];
        cfg->dma_cfg.dma_channel     = (uint32_t)dma_arr[2];
        cfg->dma_cfg.dma_priority    = (uint32_t)dma_arr[3];
        cfg->dma_cfg.dma_buffer_size = (uint32_t)dma_arr[4];
        cfg->dma_cfg.dma_data_size   = (uint32_t)dma_arr[5];
        cfg->dma_cfg.dma_fifo_is_enable = (uint32_t)dma_arr[6];

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-mode", &tmp));
        cfg->dma_cfg.dma_mode = (uint32_t)tmp;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-fifo-mode", &tmp));
        cfg->dma_cfg.dma_fifo_mode = (uint32_t)tmp;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-mem-burst", &tmp));
        cfg->dma_cfg.dma_mem_burst = (uint32_t)tmp;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-periph-burst", &tmp));
        cfg->dma_cfg.dma_periph_burst = (uint32_t)tmp;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-direction", &tmp));
        cfg->dma_cfg.dma_direction = (uint32_t)tmp;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-periph-inc", &tmp));
        cfg->dma_cfg.dma_periph_inc = (uint32_t)tmp;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-mem-inc", &tmp));
        cfg->dma_cfg.dma_mem_inc = (uint32_t)tmp;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-periph-data-size", &tmp));
        cfg->dma_cfg.dma_periph_data_size = (uint32_t)tmp;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-fifo-threshold", &tmp));
        cfg->dma_cfg.dma_fifo_threshold = (uint32_t)tmp;
    }

    return VFS_OK;
}

/**
 * @brief DAC Host VFS 打开操作
 */
static int vfs_dac_open(struct device* pdev, void* arg)
{
    struct vfs_dac_priv*  priv;
    struct dev_lifecycle* lc;
    int                   first;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_dac_priv, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;

    if (first == 1)
    {
        if (hal_dac_start(&priv->dac) != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return VFS_ERR_IO;
        }
    }

    dev_lc_open_end(lc);
    return VFS_OK;
}

/**
 * @brief DAC Host VFS 关闭操作
 */
static int vfs_dac_close(struct device* pdev)
{
    struct vfs_dac_priv*  priv;
    struct dev_lifecycle* lc;
    int                   last;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_dac_priv, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;

    if (last)
        COMPAT_IGNORE_RESULT(hal_dac_force_stop(&priv->dac));

    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief DAC Host VFS 写入操作普通模式
 */
static int vfs_dac_write(struct device* pdev, const void* buf, size_t len, uint32_t timeout_ms)
{
    struct vfs_dac_priv*  priv;
    struct dev_lifecycle* lc;
    int                   ret;
    const vfs_dac_arg*    dac_arg;

    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops || !buf)
        return VFS_ERR_INVAL;

    dac_arg = (const vfs_dac_arg*)buf;
    lc      = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    priv = container_of(pdev->ops, struct vfs_dac_priv, ops);
    ret  = hal_dac_set_value(&priv->dac, dac_arg->value);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief DAC Host VFS 读取操作普通模式
 */
static int vfs_dac_read(struct device* pdev, void* buf, size_t len, uint32_t timeout_ms)
{
    struct vfs_dac_priv*  priv;
    struct dev_lifecycle* lc;
    int                   ret;
    vfs_dac_arg*          dac_arg;

    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops || !buf)
        return VFS_ERR_INVAL;

    dac_arg = (vfs_dac_arg*)buf;
    lc      = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    priv = container_of(pdev->ops, struct vfs_dac_priv, ops);
    ret  = hal_dac_get_value(&priv->dac, &dac_arg->value);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief DAC Host VFS ioctl 操作
 */
static int vfs_dac_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct vfs_dac_priv*  priv;
    struct dev_lifecycle* lc;
    int                   ret;
    dac_cmd_handler_t     handler = NULL;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    priv = container_of(pdev->ops, struct vfs_dac_priv, ops);

    int32_t offset = (int32_t)cmd - (int32_t)DAC_CMD_BASE;
    if (offset < 1 || offset > DAC_CMD_COUNT)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }
    uint8_t index = (uint8_t)(offset - 1);

    handler = s_dac_ioctl_map[index].handler;
    if (handler != NULL)
        ret = handler(priv, arg, arg_len);
    else
        ret = VFS_ERR_INVAL;

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief DAC Host VFS 挂起操作
 */
static int vfs_dac_suspend(struct device* pdev)
{
    struct vfs_dac_priv*  priv;
    struct dev_lifecycle* lc;
    int                   ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    priv = container_of(pdev->ops, struct vfs_dac_priv, ops);
    ret  = hal_dac_pause(&priv->dac);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief DAC Host VFS 恢复操作
 */
static int vfs_dac_resume(struct device* pdev)
{
    struct vfs_dac_priv*  priv;
    struct dev_lifecycle* lc;
    int                   ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    priv = container_of(pdev->ops, struct vfs_dac_priv, ops);
    ret  = hal_dac_resume(&priv->dac);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief DAC Host VFS 文件操作
 */
static const struct file_operations fops =
{
    .close    = vfs_dac_close,
    .open     = vfs_dac_open,
    .ioctl    = vfs_dac_ioctl,
    .read     = vfs_dac_read,
    .write    = vfs_dac_write,
    .suspend  = vfs_dac_suspend,
    .resume   = vfs_dac_resume,
};

/**
 * @brief DAC Host VFS 探测操作
 */
static int vfs_dac_probe(struct device* pdev)
{
    struct vfs_dac_priv* priv;
    int                  pool_idx;
    int                  ret;
    int                  private_cfg = 0;

    if (!pdev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_dac_priv_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_dac_priv_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (vfs_dac_priv_parse_dts(pdev, &priv->cfg) != VFS_OK)
    {
        SYS_LOGE(s_kTag, "dts parse failed: %s", device_get_name(pdev));
        ret = VFS_ERR_INVAL;
        goto err_pool;
    }

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "private-cfg", &private_cfg));
    priv->unique.private_cfg = (uintptr_t)private_cfg;

    if (priv->cfg.config.dma_enable)
    {
        fifo_init(&priv->dma_fifo, priv->dma_data_buf, DAC_DMA_BUFFER_SIZE);
        priv->cfg.dma_cfg.dma_fifo = &priv->dma_fifo;
    }

    ret = hal_dac_device_init(&priv->dac, &priv->cfg, &priv->unique);
    if (ret != VFS_OK)
    {
        SYS_LOGE(s_kTag, "hal_dac_device_init failed: %s", device_get_name(pdev));
        goto err_pool;
    }

    ret = hal_dac_init(&priv->dac);
    if (ret != VFS_OK)
    {
        SYS_LOGE(s_kTag, "hal_dac_init failed: %s", device_get_name(pdev));
        goto err_deinit;
    }

    priv->ops = fops;
    pdev->ops = &priv->ops;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_deinit;
    }

    SYS_LOGI(s_kTag, "probe OK %s", device_get_name(pdev));
    return VFS_OK;

err_deinit:
    pdev->ops = NULL;
    COMPAT_IGNORE_RESULT(hal_dac_close(&priv->dac));
err_pool:
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_dac_priv_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief DAC Host VFS 移除操作
 */
static int vfs_dac_remove(struct device* pdev)
{
    struct vfs_dac_priv*  priv;
    struct dev_lifecycle* lc;
    int                   pool_idx;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_dac_priv, ops);
    lc   = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    COMPAT_IGNORE_RESULT(hal_dac_force_stop(&priv->dac));
    COMPAT_IGNORE_RESULT(hal_dac_close(&priv->dac));

    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_dac_priv_pool_ctrl, pool_idx));

    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(vfs_dac_priv, "dac", vfs_dac_probe, vfs_dac_remove)
