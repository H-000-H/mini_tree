/** * @license SPDX-License-Identifier: Apache-2.0  
 * @file vfs_adc.c
 * @brief ADC VFS 实现 — ADC 总线子系统 VFS 层实现文件
 */
 #define VFS_ADC_IMPL  /* 激活豁免权限，允许本文件调用被毒死的 HAL 慢路径 API */
 #define ADC_VFS_IMPL
 
 #include "vfs-adc.h"
 #include "osal.h"
 #include <stdio.h>
 #include "system_log.h"
 #include "device.h"
 #include "driver.h"
 #include "interrupt.h"
 
 #ifndef ADC_VFS_PRIV_COUNT
 #define ADC_VFS_PRIV_COUNT 4
 #endif
 
 #ifndef DTS_ADC_PIN_FIELD_COUNT
 #define DTS_ADC_PIN_FIELD_COUNT     8
 #endif
 #define VFS_ADC_PIN_FIELD_COUNT     DTS_ADC_PIN_FIELD_COUNT
 
 #ifndef DTS_ADC_CHANNEL_FIELD_COUNT
 #define DTS_ADC_CHANNEL_FIELD_COUNT 5
 #endif
 #define VFS_ADC_CHANNEL_FIELD_COUNT DTS_ADC_CHANNEL_FIELD_COUNT
 
 #ifndef DTS_ADC_MULTI_FIELD_COUNT
 #define DTS_ADC_MULTI_FIELD_COUNT 4
 #endif
 #define VFS_ADC_MULTI_FIELD_COUNT   DTS_ADC_MULTI_FIELD_COUNT
 
 #ifndef DTS_ADC_DMA_FIELD_COUNT
 #define DTS_ADC_DMA_FIELD_COUNT 16
 #endif
 #define VFS_ADC_DMA_FIELD_COUNT      DTS_ADC_DMA_FIELD_COUNT
 
 #ifndef DTS_ADC_KEY_MAX
 #define DTS_ADC_KEY_MAX 40
 #endif
 #define VFS_ADC_KEY_MAX              DTS_ADC_KEY_MAX
 
 struct vfs_adc_priv
 {
     struct file_operations             ops;
     struct hal_adc_host_cfg            cfg;      
     struct hal_adc_platform_unique_cfg unique;
     struct hal_adc_device              adc;
     hal_adc_channel_config             channels[HAL_ADC_MAX_CHANNELS];
     hal_adc_multi_config               multi;
     struct hal_adc_private_cfg         private;
     int                                pool_idx;
 };
 
 static struct vfs_adc_priv s_adc_priv_pool[ADC_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
 static uint8_t             s_adc_priv_used[ADC_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
 static osal_pool_t         s_adc_priv_pool_ctrl COMPAT_ALIGNED(4);
 static const char* const   s_kTag = "vfs-adc-host";
 
 /*=======================================================================================================================*/
 /* IOCTL 后台控制命令私有实现 */
 /*=======================================================================================================================*/
 
 static int adc_cmd_get_value(struct vfs_adc_priv* priv, void* arg, size_t arg_len)
 {
     if(!priv || !arg || arg_len != sizeof(struct vfs_adc_arg_t))
         return VFS_ERR_INVAL;
     struct vfs_adc_arg_t *adc_arg = (struct vfs_adc_arg_t*)arg;
     return hal_adc_read_value(&priv->adc, adc_arg->channel_id, &adc_arg->value);
 }
 
 static int adc_cmd_get_channel_sample_time(struct vfs_adc_priv* priv, void* arg, size_t arg_len)
 {
     if(!priv || !arg || arg_len != sizeof(struct vfs_adc_arg_t))
         return VFS_ERR_INVAL;
     struct vfs_adc_arg_t *adc_arg = (struct vfs_adc_arg_t*)arg;
     return hal_adc_get_channel_sample_time(&priv->adc, adc_arg->channel_index, &adc_arg->sample_time);
 }
 
 static int adc_cmd_get_channel_id(struct vfs_adc_priv* priv, void* arg, size_t arg_len)
 {
     if(!priv || !arg || arg_len != sizeof(struct vfs_adc_arg_t))
         return VFS_ERR_INVAL;
     struct vfs_adc_arg_t *adc_arg = (struct vfs_adc_arg_t*)arg;
     return hal_adc_get_channel_id(&priv->adc, adc_arg->channel_index, &adc_arg->channel_id);
 }
 
 static int adc_cmd_get_channel_count(struct vfs_adc_priv* priv, void* arg, size_t arg_len)
 {
     if(!priv || !arg || arg_len != sizeof(struct vfs_adc_arg_t))
         return VFS_ERR_INVAL;
     struct vfs_adc_arg_t *adc_arg = (struct vfs_adc_arg_t*)arg;
     return hal_adc_get_channel_count(&priv->adc, &adc_arg->channel_count);
 }
 
 /**
  * @brief 【修正】：修复 pool_conversation 拼写乌龙，变更为标准的 poll_conversion 
  */
 static int adc_cmd_poll_conversion(struct vfs_adc_priv* priv, void* arg, size_t arg_len)
 {
     if(!priv || !arg || arg_len != sizeof(struct vfs_adc_arg_t))
         return VFS_ERR_INVAL;
     struct vfs_adc_arg_t *adc_arg = (struct vfs_adc_arg_t*)arg;
     return hal_adc_poll_for_conversion(&priv->adc, &adc_arg->done_status);
 }
 
 /**
  * @brief 【修正】：将原传入的错误的 channel_index 变更为底层需要的物理 channel_id
  */
 static int adc_cmd_close_channel(struct vfs_adc_priv* priv, void* arg, size_t arg_len)
 {
     if(!priv || !arg || arg_len != sizeof(struct vfs_adc_arg_t))
         return VFS_ERR_INVAL;
     struct vfs_adc_arg_t *adc_arg = (struct vfs_adc_arg_t*)arg;
     return hal_adc_deinit_adcx_channel(&priv->adc, adc_arg->channel_id);
 }

/*===========================================================================================*/
/* ioctl 命令派发基础设施 — typedef + 映射表                                                    */
/*===========================================================================================*/
typedef int (*adc_cmd_handler_t)(struct vfs_adc_priv* priv, void* arg, size_t arg_len);

typedef struct {
    adc_cmd_handler_t handler;
} adc_ioctl_map_t;

static const adc_ioctl_map_t s_adc_ioctl_map[ADC_CMD_COUNT] = {
    [ADC_CMD_GET_CHANNEL_SAMPLE_TIME - ADC_CMD_BASE - 1] = { adc_cmd_get_channel_sample_time },
    [ADC_CMD_GET_CHANNEL_ID            - ADC_CMD_BASE - 1] = { adc_cmd_get_channel_id },
    [ADC_CMD_GET_CHANNEL_COUNT         - ADC_CMD_BASE - 1] = { adc_cmd_get_channel_count },
    [ADC_CMD_POLL_FOR_CONVERSION       - ADC_CMD_BASE - 1] = { adc_cmd_poll_conversion },
    [ADC_CMD_CLOSE_CHANNEL             - ADC_CMD_BASE - 1] = { adc_cmd_close_channel },
    [ADC_CMD_READ_VALUE                - ADC_CMD_BASE - 1] = { adc_cmd_get_value },
};

 pre_execution(150)
 static void vfs_adc_priv_pool_init()
 {
     COMPAT_IGNORE_RESULT(osal_pool_init(&s_adc_priv_pool_ctrl, s_adc_priv_used, ADC_VFS_PRIV_COUNT));
 }
 
 /**
  * @brief 解析 ADC Host DTS 属性
  */
 static int vfs_adc_priv_parse_dts(struct device* pdev, hal_adc_host_config* cfg)
 {
     struct vfs_adc_priv* priv;
     int                  adc_base = 0;
     int                  channel_num = 0;
     int                  tmp = 0;
     int                  pin_arr[VFS_ADC_PIN_FIELD_COUNT];
     int                  multi_arr[VFS_ADC_MULTI_FIELD_COUNT];
     int                  dma_arr[VFS_ADC_DMA_FIELD_COUNT];
 
     if(!pdev || !cfg)
         return VFS_ERR_INVAL;
 
     priv = container_of(cfg, struct vfs_adc_priv, cfg);
     cfg->channels     = priv->channels;
     cfg->multi_cfg    = &priv->multi;
     cfg->private_cfg  = &priv->private;
 
     if(device_get_prop_int(pdev, "adc-base", &adc_base) != VFS_OK) return VFS_ERR_INVAL;
     cfg->adc_handle = (uintptr_t)adc_base;
 
     if(device_get_prop_int(pdev, "adc-clk-bus",       &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.adc_clk_bus = (int32_t)tmp;
     if(device_get_prop_int(pdev, "channel-num",       &channel_num) != VFS_OK) return VFS_ERR_INVAL;
     if(device_get_prop_int(pdev, "resolution",        &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.resolution = (int32_t)tmp;
     if(device_get_prop_int(pdev, "align",             &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.align = (int32_t)tmp;
     if(device_get_prop_int(pdev, "sequencer-mode",    &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.sequencer_mode = (int32_t)tmp;
     if(device_get_prop_int(pdev, "continuous-mode",   &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.continuous_mode = (int32_t)tmp;
     if(device_get_prop_int(pdev, "trigger-src",       &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.trigger_src = (int32_t)tmp;
     if(device_get_prop_int(pdev, "sequencer-length",  &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.SequencerLength = (int32_t)tmp;
     if(device_get_prop_int(pdev, "sequencer-discont", &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.SequencerDiscont = (int32_t)tmp;
     if(device_get_prop_int(pdev, "eoc-flag",          &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.EOC_flag = (int32_t)tmp;
     if(device_get_prop_int(pdev, "dma-mode",          &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.dma_mode = (int32_t)tmp;
     if(device_get_prop_int(pdev, "output-buf",        &tmp) != VFS_OK) return VFS_ERR_INVAL;
     cfg->config.output_buf = (int32_t)tmp;
 
     if(channel_num <= 0 || channel_num > HAL_ADC_MAX_CHANNELS)
         return VFS_ERR_INVAL;
 
     cfg->config.channel_num = (int32_t)channel_num;
     cfg->channel_count      = (uint32_t)channel_num;
 
     if(device_get_prop_int(pdev, "internal-ch-enable", &tmp) == VFS_OK)
         cfg->config.internal_ch_enable = (int32_t)tmp;

     COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "internal-ch-select", &tmp));
     cfg->config.internal_ch_select = (uint32_t)tmp;
     COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-reg-mode", &tmp));
     cfg->config.dma_reg_mode = (uint32_t)tmp;
     COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sw-trigger", &tmp));
     cfg->config.sw_trigger = (uint32_t)tmp;
 
     if(device_get_prop_int_array(pdev, "gpio-pin", pin_arr, VFS_ADC_PIN_FIELD_COUNT) == VFS_ADC_PIN_FIELD_COUNT)
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
 
     if(device_get_prop_int_array(pdev, "multi-cfg", multi_arr, VFS_ADC_MULTI_FIELD_COUNT) == VFS_ADC_MULTI_FIELD_COUNT)
     {
         cfg->multi_cfg->multimode      = (uint32_t)multi_arr[0];
         cfg->multi_cfg->common_clock   = (uint32_t)multi_arr[1];
         cfg->multi_cfg->multi_dma      = (uint32_t)multi_arr[2];
         cfg->multi_cfg->sampling_delay = (uint32_t)multi_arr[3];
     }
     else 
         return VFS_ERR_INVAL;
 
     if(device_get_prop_int_array(pdev, "dma-cfg", dma_arr, VFS_ADC_DMA_FIELD_COUNT) == VFS_ADC_DMA_FIELD_COUNT)
     {
         cfg->dma_cfg.dma_handle          = (uintptr_t)dma_arr[0];
         cfg->dma_cfg.dma_stream          = (uint32_t)dma_arr[1];
         cfg->dma_cfg.dma_channel         = (uint32_t)dma_arr[2];
         cfg->dma_cfg.dma_priority        = (uint32_t)dma_arr[3];
         cfg->dma_cfg.dma_memory_size     = (uint32_t)dma_arr[4];
         cfg->dma_cfg.dma_it_enable       = (uint32_t)dma_arr[5];
         cfg->dma_cfg.dma_enable          = (uint32_t)dma_arr[6];
         cfg->dma_cfg.dma_direction       = (uint32_t)dma_arr[7];
         cfg->dma_cfg.dma_mode            = (uint32_t)dma_arr[8];
         cfg->dma_cfg.dma_periph_inc      = (uint32_t)dma_arr[9];
         cfg->dma_cfg.dma_mem_inc         = (uint32_t)dma_arr[10];
         cfg->dma_cfg.dma_periph_data_size = (uint32_t)dma_arr[11];
         cfg->dma_cfg.dma_fifo_mode       = (uint32_t)dma_arr[12];
         cfg->dma_cfg.dma_fifo_threshold  = (uint32_t)dma_arr[13];
         cfg->dma_cfg.dma_mem_burst       = (uint32_t)dma_arr[14];
         cfg->dma_cfg.dma_periph_burst    = (uint32_t)dma_arr[15];
     }
     else
         return VFS_ERR_INVAL;
 
     for(int i = 0; i < channel_num; i++)
     {
         char k[VFS_ADC_KEY_MAX];
         int  ch_arr[VFS_ADC_CHANNEL_FIELD_COUNT];
 
         snprintf(k, sizeof(k), "channel%d", i);
         if(device_get_prop_int_array(pdev, k, ch_arr, VFS_ADC_CHANNEL_FIELD_COUNT) != VFS_ADC_CHANNEL_FIELD_COUNT)
             return VFS_ERR_INVAL;
 
         cfg->channels[i].channel_id  = (uint32_t)ch_arr[0];
         cfg->channels[i].rank        = (uint32_t)ch_arr[1];
         cfg->channels[i].sample_time = (uint32_t)ch_arr[2];
         cfg->channels[i].diff_mode   = (uint32_t)ch_arr[3];
         cfg->channels[i].attenuation = (uint32_t)ch_arr[4];
     }
 
     return VFS_OK;
 }
 
 /*=======================================================================================================================*/
 /* VFS 标准核心生命周期接口 */
 /*=======================================================================================================================*/
 
 static int vfs_adc_open(struct device* pdev, void* arg)
 {
     if(!pdev || !pdev->ops)
         return VFS_ERR_INVAL;
     struct vfs_adc_priv* priv;
     struct dev_lifecycle* lc;
     int first, ret;
     COMPAT_IGNORE_RESULT(arg);
 
     priv = container_of(pdev->ops, struct vfs_adc_priv, ops);
     lc = device_lc(pdev);
     if(IS_ERR(lc))
         return PTR_ERR(lc);
 
     first = dev_lc_open_begin(lc);
     if(first < 0)
         return first;
 
     if(first == 1)
     {
         if(priv->adc.host->dma_cfg.dma_it_enable)
             ret = hal_adc_dma_it_start(&priv->adc);
         else if(priv->adc.host->dma_cfg.dma_enable)
             ret = hal_adc_dma_start(&priv->adc);
         else
             ret = hal_adc_start(&priv->adc);
             
         if(ret != VFS_OK)
         {
             dev_lc_open_abort(lc);
             return ret;
         }
     }
     dev_lc_open_end(lc);
     return VFS_OK;
 }
 
 static int vfs_adc_close(struct device* pdev)
 {
     if(!pdev || !pdev->ops)
         return VFS_ERR_INVAL;
     struct vfs_adc_priv* priv;
     struct dev_lifecycle* lc;
     int last;
 
     priv = container_of(pdev->ops, struct vfs_adc_priv, ops);
     lc = device_lc(pdev);
     if(IS_ERR(lc))
         return PTR_ERR(lc);
 
     last = dev_lc_close_begin(lc);
     if(last < 0)
         return last;
 
     if(last)
         COMPAT_IGNORE_RESULT(hal_adc_deinit_all_adcx(&priv->adc));
 
     dev_lc_close_end(lc);
     return VFS_OK;
 }
 
 static int vfs_adc_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
 {
     struct vfs_adc_priv*   priv;
     struct dev_lifecycle*  lc;
     adc_cmd_handler_t      handler = NULL;
     int                    ret;
     int32_t                offset;
     uint8_t                index;
     COMPAT_IGNORE_RESULT(timeout_ms);

     if(!pdev || !pdev->ops)
         return VFS_ERR_INVAL;

     lc = device_lc(pdev);
     if(IS_ERR(lc))
         return PTR_ERR(lc);
     ret = dev_lc_io_begin(lc);
     if(ret != VFS_OK)
         return ret;

     priv = container_of(pdev->ops, struct vfs_adc_priv, ops);

     offset = (int32_t)cmd - (int32_t)ADC_CMD_BASE;
     if(offset < 1 || offset > ADC_CMD_COUNT)
     {
         dev_lc_io_end(lc);
         return VFS_ERR_INVAL;
     }
     index = (uint8_t)(offset - 1);

     handler = s_adc_ioctl_map[index].handler;
     if(handler != NULL)
         ret = handler(priv, arg, arg_len);
     else
         ret = VFS_ERR_INVAL;

     dev_lc_io_end(lc);
     return ret;
 }
 
 static const struct file_operations fops =
 {
     .close = vfs_adc_close,
     .open  = vfs_adc_open,
     .ioctl = vfs_adc_ioctl,
 };
 
 static int vfs_adc_probe(struct device* pdev)
 {
     struct vfs_adc_priv* priv;
     int                  pool_idx;
     int                  ret;
 
     if(!pdev)
         return VFS_ERR_INVAL;
     
     pool_idx = osal_pool_claim(&s_adc_priv_pool_ctrl);
     if(pool_idx < 0)
         return VFS_ERR_NOMEM;
 
     priv = &s_adc_priv_pool[pool_idx];
     COMPAT_MEM_SET(priv, 0, sizeof(*priv));
     priv->pool_idx = pool_idx;
 
     if(vfs_adc_priv_parse_dts(pdev, &priv->cfg) != VFS_OK)
     {
         SYS_LOGE(s_kTag, "dts parse failed: %s", device_get_name(pdev));
         ret = VFS_ERR_INVAL;
         goto err_pool;
     }
 
     ret = hal_adc_device_init(&priv->adc, &priv->unique, &priv->cfg);
     if(ret != VFS_OK)
     {
         SYS_LOGE(s_kTag, "hal_adc_device_init failed: %s", device_get_name(pdev));
         goto err_pool;
     }
 

     ret = hal_adc_init(&priv->adc);
     if(ret != VFS_OK)
     {
         SYS_LOGE(s_kTag, "hal_adc_init hardware failed: %s", device_get_name(pdev));
         goto err_deinit;
     }
 
     /**
      * 注册 ADC 虚拟中断 — 上半部 hal_virtual_adc_irq_callback, 下半部 g_adc_dma_bottom_half_work
      * arg 传入 &priv->adc, ISR 触发时 dispatch 从 VIRQ 表读取, 无需 ISR 硬编码设备指针
      */
     interrupt_virtual_register(VIRQ(adc, 0), hal_virtual_adc_irq_callback,
                                &g_adc_dma_bottom_half_work, &priv->adc);

     int dma_irqn = -1;
     COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-irqn", &dma_irqn));
     interrupt_hw_enable(dma_irqn, 5);

     priv->ops = fops;
     pdev->ops = &priv->ops;
 
     if(device_set_priv(pdev, priv) != VFS_OK)
     {
         ret = VFS_ERR_IO;
         goto err_hardware_deinit;
     }
 
     SYS_LOGI(s_kTag, "probe OK %s", device_get_name(pdev));
     return VFS_OK;
 
 err_hardware_deinit:
     pdev->ops = NULL;
     COMPAT_IGNORE_RESULT(hal_adc_deinit_all_adcx(&priv->adc));
 err_deinit:
     /* 【修正】：解绑设备指针句柄（对齐底层更名为 hal_adc_device_deinit 后的函数） */
     COMPAT_IGNORE_RESULT(hal_adc_device_deinit(&priv->adc));
 err_pool:
     COMPAT_IGNORE_RESULT(osal_pool_release(&s_adc_priv_pool_ctrl, pool_idx));
     return ret;
 }
 
 static int vfs_adc_remove(struct device* pdev)
 {
     if(!pdev || !pdev->ops)
         return VFS_ERR_INVAL;
     struct vfs_adc_priv* priv;
     struct dev_lifecycle* lc;
     int pool_idx;
 
     priv = container_of(pdev->ops, struct vfs_adc_priv, ops);
     lc = device_lc(pdev);
     if(IS_ERR(lc))
         return PTR_ERR(lc);
     
     pool_idx = priv->pool_idx;
     dev_lc_remove_start(lc);
     device_ops_unregister(pdev);
     if(dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
     {
         dev_lc_remove_finish(lc);
         return VFS_ERR_IO;
     }
 
     COMPAT_IGNORE_RESULT(hal_adc_deinit_all_adcx(&priv->adc));
     COMPAT_IGNORE_RESULT(hal_adc_device_deinit(&priv->adc));
 
     COMPAT_MEM_SET(priv, 0, sizeof(*priv));
     COMPAT_IGNORE_RESULT(osal_pool_release(&s_adc_priv_pool_ctrl, pool_idx));
 
     dev_lc_remove_finish(lc);
     return VFS_OK;
 }
 
 DRIVER_REGISTER(vfs_adc_priv, "adc", vfs_adc_probe, vfs_adc_remove)