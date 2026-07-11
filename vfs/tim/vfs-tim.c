/**  
 * @license SPDX-License-Identifier: Apache-2.0  
 * @brief TIM VFS 实现 — TIM 总线子系统 VFS 层实现文件
 */
#define VFS_TIM_IMPL  /* 激活豁免权限，允许本文件调用被毒死的 HAL 慢路径 API */
#define TIM_VFS_IMPL
#include "vfs-tim.h"
#include "device.h"
#include "driver.h"
#include "osal.h"
#include <stdio.h>
#include "system_log.h"
#ifndef TIM_VFS_PRIV_COUNT
#define TIM_VFS_PRIV_COUNT 4
#endif

/* pin 属性数组元素数 — 对应 hal_tim_pin_cfg 的字段数 */
#define VFS_TIM_PIN_FIELD_COUNT     8

/* DTS 属性键最大长度 */
#define VFS_TIM_KEY_MAX             40

struct vfs_tim_priv
{
    struct file_operations              ops;
    struct hal_tim_host_config          cfg;
    hal_tim_platform_unique_config      unique;
    struct hal_tim_device               tim;
    int                                 pool_idx;
};

static struct vfs_tim_priv          s_tim_priv_pool[TIM_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static uint8_t                      s_tim_priv_used[TIM_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t                  s_tim_priv_pool_ctrl COMPAT_ALIGNED(4);
static const char* const            s_kTag = "vfs-tim-host";

hal_tim_device* vfs_tim_get_hal_dev(struct device* dev)
{
    if (!dev)
        return NULL;
    struct vfs_tim_priv* priv = (struct vfs_tim_priv*)device_get_priv(dev);
    if (IS_ERR(priv))
        return NULL;
    return &priv->tim;
}

/**
 * @brief TIM Ioctl 命令处理函数指针类型
 * @param       priv 私有数据指针
 * @param       arg 参数指针
 * @param       arg_len 参数长度
 * @param       timeout_ms 超时时间
 * @details     该函数用于处理TIM Ioctl命令,根据命令类型调用相应的处理函数
 * @note        此处用函数回调的原因是TIM 的ioctl命令处理函数比较复杂,需要根据命令类型调用相应的处理函数若用switch语句则会导致代码冗长且难以维护,故用函数回调的方式来处理TIM Ioctl命令
 * @return      成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
typedef int (*tim_cmd_handler_t)(struct vfs_tim_priv *priv, void *arg, size_t arg_len);

typedef struct {
    tim_cmd_handler_t handler;
} tim_ioctl_map_t;

/*===========================================================================================================================================================*/
/* ioctl 命令处理函数 — 每个函数封装一个 HAL 调用                                                                                                                */
/*===========================================================================================================================================================*/

static int tim_cmd_stop(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    return hal_tim_force_stop(&priv->tim);
}

static int tim_cmd_pause(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    return hal_tim_base_stop(&priv->tim);
}

static int tim_cmd_resume(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    return hal_tim_base_start(&priv->tim);
}

static int tim_cmd_get_counter(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_get_counter(&priv->tim, (uint32_t*)arg);
}

static int tim_cmd_set_counter(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_set_counter(&priv->tim, *(const uint32_t*)arg);
}

static int tim_cmd_pwm_update(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(struct vfs_tim_arg))
        return VFS_ERR_INVAL;
    const struct vfs_tim_arg* a = (const struct vfs_tim_arg*)arg;
    return hal_tim_pwm_update(&priv->tim, a->channel, a->arr, a->ccr);
}

static int tim_cmd_get_capture(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(struct vfs_tim_arg))
        return VFS_ERR_INVAL;
    struct vfs_tim_arg* a = (struct vfs_tim_arg*)arg;
    return hal_tim_get_capture_value(&priv->tim, a->channel, &a->value);
}

static int tim_cmd_get_encoder(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_get_encoder_value(&priv->tim, (uint32_t*)arg);
}

static int tim_cmd_get_hall(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_get_hall_value(&priv->tim, (uint32_t*)arg);
}

static int tim_cmd_set_autoreload(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_set_autoreload(&priv->tim, *(const uint32_t*)arg);
}

static int tim_cmd_get_autoreload(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_get_autoreload(&priv->tim, (uint32_t*)arg);
}

static int tim_cmd_set_prescaler(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_set_prescaler(&priv->tim, *(const uint32_t*)arg);
}

static int tim_cmd_get_prescaler(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_get_prescaler(&priv->tim, (uint32_t*)arg);
}

static int tim_cmd_set_clock_division(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_set_clock_division(&priv->tim, *(const uint32_t*)arg);
}

static int tim_cmd_get_clock_division(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_get_clock_division(&priv->tim, (uint32_t*)arg);
}

static int tim_cmd_set_counter_mode(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_set_counter_mode(&priv->tim, *(const uint32_t*)arg);
}

static int tim_cmd_get_counter_mode(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_get_counter_mode(&priv->tim, (uint32_t*)arg);
}

static int tim_cmd_enable_arr_preload(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    return hal_tim_enable_arr_preload(&priv->tim);
}

static int tim_cmd_disable_arr_preload(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    return hal_tim_disable_arr_preload(&priv->tim);
}

static int tim_cmd_set_interrupt(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_interrupt_config(&priv->tim, *(const uint32_t*)arg);
}

static int tim_cmd_encoder_start(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    if(!arg || arg_len < sizeof(uint32_t))
        return VFS_ERR_INVAL;
    return hal_tim_encoder_start(&priv->tim, *(const uint32_t*)arg);
}

static int tim_cmd_hall_start(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    return hal_tim_hall_start(&priv->tim);
}

static int tim_cmd_start(struct vfs_tim_priv* priv, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len);
    /* start 根据 mode 分发: encoder/hall 走专用启动, 其余走 base_start */
    switch(priv->cfg.mode)
    {
    case HAL_TIM_MODE_ENCODER:
        if(!arg || arg_len < sizeof(uint32_t))
            return VFS_ERR_INVAL;
        return hal_tim_encoder_start(&priv->tim, *(const uint32_t*)arg);
    case HAL_TIM_MODE_HALLSENSOR:
        return hal_tim_hall_start(&priv->tim);
    default:
        return hal_tim_base_start(&priv->tim);
    }
}

/*===========================================================================================================================================================*/
/* ioctl 命令映射表 — index = (cmd & 0xFF) - 1, 与 TIM_CMD_* 编号一一对应                                                                                       */
/*===========================================================================================================================================================*/

static const tim_ioctl_map_t s_tim_ioctl_map[TIM_CMD_COUNT] =
{
    [TIM_CMD_START               - TIM_CMD_BASE - 1] = { tim_cmd_start },
    [TIM_CMD_STOP                - TIM_CMD_BASE - 1] = { tim_cmd_stop },
    [TIM_CMD_PAUSE               - TIM_CMD_BASE - 1] = { tim_cmd_pause },
    [TIM_CMD_RESUME              - TIM_CMD_BASE - 1] = { tim_cmd_resume },
    [TIM_CMD_GET_COUNTER         - TIM_CMD_BASE - 1] = { tim_cmd_get_counter },
    [TIM_CMD_SET_COUNTER         - TIM_CMD_BASE - 1] = { tim_cmd_set_counter },
    [TIM_CMD_PWM_UPDATE          - TIM_CMD_BASE - 1] = { tim_cmd_pwm_update },
    [TIM_CMD_GET_CAPTURE         - TIM_CMD_BASE - 1] = { tim_cmd_get_capture },
    [TIM_CMD_GET_ENCODER         - TIM_CMD_BASE - 1] = { tim_cmd_get_encoder },
    [TIM_CMD_GET_HALL            - TIM_CMD_BASE - 1] = { tim_cmd_get_hall },
    [TIM_CMD_SET_AUTORELOAD      - TIM_CMD_BASE - 1] = { tim_cmd_set_autoreload },
    [TIM_CMD_GET_AUTORELOAD      - TIM_CMD_BASE - 1] = { tim_cmd_get_autoreload },
    [TIM_CMD_SET_PRESCALER       - TIM_CMD_BASE - 1] = { tim_cmd_set_prescaler },
    [TIM_CMD_GET_PRESCALER       - TIM_CMD_BASE - 1] = { tim_cmd_get_prescaler },
    [TIM_CMD_SET_CLOCK_DIVISION  - TIM_CMD_BASE - 1] = { tim_cmd_set_clock_division },
    [TIM_CMD_GET_CLOCK_DIVISION  - TIM_CMD_BASE - 1] = { tim_cmd_get_clock_division },
    [TIM_CMD_SET_COUNTER_MODE    - TIM_CMD_BASE - 1] = { tim_cmd_set_counter_mode },
    [TIM_CMD_GET_COUNTER_MODE    - TIM_CMD_BASE - 1] = { tim_cmd_get_counter_mode },
    [TIM_CMD_ENABLE_ARR_PRELOAD  - TIM_CMD_BASE - 1] = { tim_cmd_enable_arr_preload },
    [TIM_CMD_DISABLE_ARR_PRELOAD - TIM_CMD_BASE - 1] = { tim_cmd_disable_arr_preload },
    [TIM_CMD_SET_INTERRUPT       - TIM_CMD_BASE - 1] = { tim_cmd_set_interrupt },
    [TIM_CMD_ENCODER_START       - TIM_CMD_BASE - 1] = { tim_cmd_encoder_start },
    [TIM_CMD_HALL_START          - TIM_CMD_BASE - 1] = { tim_cmd_hall_start },
};

/**
 * @brief TIM Host VFS 私有数据池启动初始化
 */
pre_execution(150)
static void vfs_tim_priv_pool_init()
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_tim_priv_pool_ctrl,s_tim_priv_used,TIM_VFS_PRIV_COUNT));
}

/**
 * @brief   解析 TIM Host DTS 属性 (硬件直投值), 填入 hal_tim_host_config
 * @note    unique不在此处解析因为unique属于特殊变量在probe解析
 * @param   dev 设备对象指针
 * @param   cfg 输出的 HAL 总线配置结构
 * @return  成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int vfs_tim_priv_parse_dts(struct device* pdev, struct hal_tim_host_config* cfg)
{
    if(!pdev||!cfg)
        return VFS_ERR_INVAL;
    int tim_mode =-1;
    if(device_get_prop_int(pdev,"tim-mode",&tim_mode)!=VFS_OK)
        return VFS_ERR_INVAL;
    cfg->mode = (uint32_t)tim_mode;

    /* 基础时基 — 必填, 缺任一即失败 */
    if(device_get_prop_int(pdev,"prescaler",     (int*)&cfg->base.prescaler)!=VFS_OK ||
       device_get_prop_int(pdev,"counter-mode",  (int*)&cfg->base.counter_mode)!=VFS_OK ||
       device_get_prop_int(pdev,"autoreload",    (int*)&cfg->base.autoreload)!=VFS_OK)
        return VFS_ERR_INVAL;

    /* 选填字段 — 属性不存在时字段保持 0 */
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev,"clock-division",     (int*)&cfg->base.clock_division));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev,"repetition-counter", (int*)&cfg->base.repetition_counter));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev,"active-chn-mask",    (int*)&cfg->active_chn_mask));

    /* 宽容解析: 缺字段记 warn, 用默认值 0, 不 fail 整个初始化 */
    switch(cfg->mode)
    {
    case HAL_TIM_MODE_BASE:
        break;

    case HAL_TIM_MODE_OC:
    {
        static const char* const fmt[] = {
            "oc%d-compare-value","oc%d-oc-mode","oc%d-oc-state","oc%d-oc-polarity",
            "oc%d-oc-idle-state","oc%d-oc-n-state","oc%d-oc-n-polarity","oc%d-oc-n-idle-state",
            "oc%d-channel-id","oc%d-chn-mode","oc%d-chn-polarity",
            "oc%d-chn-filter","oc%d-chn-prescaler","oc%d-chn-enable-comp"
        };
        for(int i=0;i<HAL_OUTPUT_COMPARE_TIM_MAX_CHANNELS;i++)
        {
            if(!(cfg->active_chn_mask & (1u<<i))) continue;
            char k[VFS_TIM_KEY_MAX];
            hal_output_compare_config* c=&cfg->oc_mode.config[i];
            hal_tim_channel_config*   ch=&cfg->oc_mode.channel[i];
            hal_tim_pin_config*        p=&cfg->oc_mode.pin[i];
            int pin_arr[VFS_TIM_PIN_FIELD_COUNT];
            int* dst[] = {
                (int*)&c->compare_value,(int*)&c->oc_mode,(int*)&c->oc_state,(int*)&c->oc_polarity,
                (int*)&c->oc_idle_state,(int*)&c->oc_n_state,(int*)&c->oc_n_polarity,(int*)&c->oc_n_idle_state,
                (int*)&ch->channel_id,(int*)&ch->mode,(int*)&ch->polarity,
                (int*)&ch->filter,(int*)&ch->prescaler,(int*)&ch->enable_complementary
            };
            for(int j=0;j<(int)(sizeof(fmt)/sizeof(fmt[0]));j++)
            {
                snprintf(k,sizeof(k),fmt[j],i);
                if(device_get_prop_int(pdev,k,dst[j])!=VFS_OK)
                    osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",k);
            }
            snprintf(k,sizeof(k),"oc%d-pin",i);
            if(device_get_prop_int_array(pdev,k,pin_arr,VFS_TIM_PIN_FIELD_COUNT)==VFS_TIM_PIN_FIELD_COUNT){
                p->port=(uintptr_t)pin_arr[0]; p->pin=(uint16_t)pin_arr[1]; p->clk_bus=(uint32_t)pin_arr[2];
                p->af=(uint32_t)pin_arr[3]; p->output_type=(uint32_t)pin_arr[4]; p->speed=(uint32_t)pin_arr[5];
                p->mode=(uint32_t)pin_arr[6]; p->pull=(uint32_t)pin_arr[7];
            } else
                osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",k);
        }
        break;
    }

    case HAL_TIM_MODE_IC:
    {
        static const char* const fmt[] = {
            "ic%d-polarity","ic%d-filter","ic%d-prescaler","ic%d-active-input",
            "ic%d-channel-id","ic%d-chn-mode","ic%d-chn-polarity",
            "ic%d-chn-filter","ic%d-chn-prescaler","ic%d-chn-enable-comp"
        };
        for(int i=0;i<HAL_INPUT_CAPTURE_TIM_MAX_CHANNELS;i++)
        {
            if(!(cfg->active_chn_mask & (1u<<i))) continue;
            char k[VFS_TIM_KEY_MAX];
            hal_input_capture_config* c=&cfg->ic_mode.config[i];
            hal_tim_channel_config*   ch=&cfg->ic_mode.channel[i];
            hal_tim_pin_config*        p=&cfg->ic_mode.pin[i];
            int pin_arr[VFS_TIM_PIN_FIELD_COUNT];
            int* dst[] = {
                (int*)&c->polarity,(int*)&c->filter,(int*)&c->prescaler,(int*)&c->active_input,
                (int*)&ch->channel_id,(int*)&ch->mode,(int*)&ch->polarity,
                (int*)&ch->filter,(int*)&ch->prescaler,(int*)&ch->enable_complementary
            };
            for(int j=0;j<(int)(sizeof(fmt)/sizeof(fmt[0]));j++)
            {
                snprintf(k,sizeof(k),fmt[j],i);
                if(device_get_prop_int(pdev,k,dst[j])!=VFS_OK)
                    osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",k);
            }
            snprintf(k,sizeof(k),"ic%d-pin",i);
            if(device_get_prop_int_array(pdev,k,pin_arr,VFS_TIM_PIN_FIELD_COUNT)==VFS_TIM_PIN_FIELD_COUNT){
                p->port=(uintptr_t)pin_arr[0]; p->pin=(uint16_t)pin_arr[1]; p->clk_bus=(uint32_t)pin_arr[2];
                p->af=(uint32_t)pin_arr[3]; p->output_type=(uint32_t)pin_arr[4]; p->speed=(uint32_t)pin_arr[5];
                p->mode=(uint32_t)pin_arr[6]; p->pull=(uint32_t)pin_arr[7];
            } else
                osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",k);
        }
        break;
    }

    case HAL_TIM_MODE_ENCODER:
    {
        hal_encoder_cfg* h=&cfg->encoder_mode.config.hw_cfg;
        static const char* const cfg_keys[] = {
            "encoder-mode","encoder-period",
            "encoder-ic1-active-input","encoder-ic1-polarity","encoder-ic1-filter","encoder-ic1-prescaler",
            "encoder-ic2-active-input","encoder-ic2-polarity","encoder-ic2-filter","encoder-ic2-prescaler",
            "encoder-pulse-per-rev"
        };
        int* cfg_dst[] = {
            (int*)&h->mode,(int*)&h->period,
            (int*)&h->ic1_active_input,(int*)&h->ic1_polarity,(int*)&h->ic1_filter,(int*)&h->ic1_prescaler,
            (int*)&h->ic2_active_input,(int*)&h->ic2_polarity,(int*)&h->ic2_filter,(int*)&h->ic2_prescaler,
            (int*)&cfg->encoder_mode.config.pulse_per_rev
        };
        for(int j=0;j<(int)(sizeof(cfg_keys)/sizeof(cfg_keys[0]));j++)
            if(device_get_prop_int(pdev,cfg_keys[j],cfg_dst[j])!=VFS_OK)
                osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",cfg_keys[j]);

        static const char* const ch_fmt[] = {
            "encoder-ch%d-channel-id","encoder-ch%d-chn-mode","encoder-ch%d-chn-polarity",
            "encoder-ch%d-chn-filter","encoder-ch%d-chn-prescaler"
        };
        for(int i=0;i<HAL_ENCODER_TIM_MAX_CHANNELS;i++)
        {
            char k[VFS_TIM_KEY_MAX];
            hal_tim_channel_config* ch=&cfg->encoder_mode.channel[i];
            hal_tim_pin_config*      p=&cfg->encoder_mode.pin[i];
            int pin_arr[VFS_TIM_PIN_FIELD_COUNT];
            int* dst[] = {
                (int*)&ch->channel_id,(int*)&ch->mode,(int*)&ch->polarity,
                (int*)&ch->filter,(int*)&ch->prescaler
            };
            for(int j=0;j<(int)(sizeof(ch_fmt)/sizeof(ch_fmt[0]));j++)
            {
                snprintf(k,sizeof(k),ch_fmt[j],i);
                if(device_get_prop_int(pdev,k,dst[j])!=VFS_OK)
                    osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",k);
            }
            snprintf(k,sizeof(k),"encoder-ch%d-pin",i);
            if(device_get_prop_int_array(pdev,k,pin_arr,VFS_TIM_PIN_FIELD_COUNT)==VFS_TIM_PIN_FIELD_COUNT){
                p->port=(uintptr_t)pin_arr[0]; p->pin=(uint16_t)pin_arr[1]; p->clk_bus=(uint32_t)pin_arr[2];
                p->af=(uint32_t)pin_arr[3]; p->output_type=(uint32_t)pin_arr[4]; p->speed=(uint32_t)pin_arr[5];
                p->mode=(uint32_t)pin_arr[6]; p->pull=(uint32_t)pin_arr[7];
            } else
                osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",k);
        }
        break;
    }

    case HAL_TIM_MODE_HALLSENSOR:
    {
        hal_hall_config* c=&cfg->hall_mode.config;
        static const char* const keys[] = {
            "hall-polarity","hall-filter-time","hall-prescaler","hall-commutation-delay-time",
            "hall-capture-channel-id","hall-capture-chn-mode","hall-capture-chn-polarity",
            "hall-capture-chn-filter","hall-capture-chn-prescaler"
        };
        int* dst[] = {
            (int*)&c->hall_polarity,(int*)&c->hall_filter_time,(int*)&c->hall_prescaler,
            (int*)&c->hall_commutation_delay_time,
            (int*)&cfg->hall_mode.capture_channel.channel_id,(int*)&cfg->hall_mode.capture_channel.mode,
            (int*)&cfg->hall_mode.capture_channel.polarity,(int*)&cfg->hall_mode.capture_channel.filter,
            (int*)&cfg->hall_mode.capture_channel.prescaler
        };
        for(int j=0;j<(int)(sizeof(keys)/sizeof(keys[0]));j++)
            if(device_get_prop_int(pdev,keys[j],dst[j])!=VFS_OK)
                osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",keys[j]);
        for(int i=0;i<HAL_HALL_TIM_MAX_CHANNELS;i++)
        {
            char k[VFS_TIM_KEY_MAX];
            hal_tim_pin_config* p=&cfg->hall_mode.phase_pins[i];
            int pin_arr[VFS_TIM_PIN_FIELD_COUNT];
            snprintf(k,sizeof(k),"hall-phase%d-pin",i);
            if(device_get_prop_int_array(pdev,k,pin_arr,VFS_TIM_PIN_FIELD_COUNT)==VFS_TIM_PIN_FIELD_COUNT){
                p->port=(uintptr_t)pin_arr[0]; p->pin=(uint16_t)pin_arr[1]; p->clk_bus=(uint32_t)pin_arr[2];
                p->af=(uint32_t)pin_arr[3]; p->output_type=(uint32_t)pin_arr[4]; p->speed=(uint32_t)pin_arr[5];
                p->mode=(uint32_t)pin_arr[6]; p->pull=(uint32_t)pin_arr[7];
            } else
                osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",k);
        }
        break;
    }

    default:
        return VFS_ERR_INVAL;
    }

    /* BDTR — 选填, 仅高级定时器生效 */
    {
        static const char* const keys[] = 
        {
            "bdtr-automatic-output","bdtr-break-state","bdtr-break-polarity","bdtr-break-filter",
            "bdtr-ossi-state","bdtr-ossr-state","bdtr-dead-time","bdtr-lock-level"
        };
        int* dst[] = {
            (int*)&cfg->bdtr.automatic_output,(int*)&cfg->bdtr.break_state,
            (int*)&cfg->bdtr.break_polarity,(int*)&cfg->bdtr.break_filter,
            (int*)&cfg->bdtr.ossi_state,(int*)&cfg->bdtr.ossr_state,
            (int*)&cfg->bdtr.dead_time,(int*)&cfg->bdtr.lock_level
        };
        for(int j=0;j<(int)(sizeof(keys)/sizeof(keys[0]));j++)
            if(device_get_prop_int(pdev,keys[j],dst[j])!=VFS_OK)
                osal_log(OSAL_LOG_WARN,s_kTag,"missing DTS prop %s\n",keys[j]);
    }

    return VFS_OK;
}

/**
 * @brief TIM Client 设备打开操作
 * @param pdev 设备对象指针
 * @param arg 参数
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int vfs_tim_open(struct device*pdev,void*arg)
{
    struct vfs_tim_priv*        priv;
    struct dev_lifecycle*       lc;
    int                         first;
    COMPAT_IGNORE_RESULT(arg);
    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops,struct vfs_tim_priv,ops);
    lc = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if(first<0)
        return first;

    if(first==1)
    {
        if(hal_tim_open(&priv->tim)!=VFS_OK)
        {
            dev_lc_open_abort(lc);
            return VFS_ERR_IO;
        }
    }

    dev_lc_open_end(lc);
    return VFS_OK;
}

/**
 * @brief TIM Client 设备关闭操作 (引用计数, 末次关闭时调用 hal_tim_close)
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_IO
 */
static int vfs_tim_close(struct device*pdev)
{
    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;
    struct vfs_tim_priv* priv ;
    struct dev_lifecycle *lc;
    int last;

    priv = container_of(pdev->ops,struct vfs_tim_priv,ops);
    lc = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc);
    if(last<0)
        return last;

    if(last)
        COMPAT_IGNORE_RESULT(hal_tim_close(&priv->tim));

    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief TIM Client 设备 Ioctl 操作 操作特殊mode的命令
 * @param pdev 设备对象指针
 * @param cmd 命令
 * @param arg 参数
 * @param arg_len 参数长度
 * @param timeout_ms 超时时间
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int vfs_tim_ioctl(struct device*pdev,int cmd,void*arg,size_t arg_len,uint32_t timeout_ms)
{
    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;
    struct vfs_tim_priv*    priv;
    int                     ret;
    struct dev_lifecycle*   lc;

    COMPAT_IGNORE_RESULT(timeout_ms);
    lc =device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    priv = container_of(pdev->ops,struct vfs_tim_priv,ops);
    tim_cmd_handler_t handler = NULL;

    /**<提取出低8位数据,因为命令格式固定为x宏且每个命令长度为256所以直接提取低8位可以直接作为index且第一个命令就是从0x01开始>*/
    int32_t offset           = (int32_t)cmd - (int32_t)TIM_CMD_BASE;
    if(offset < 1 || offset > TIM_CMD_COUNT)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }
    uint8_t index            = (uint8_t)(offset - 1);/*<此处用(cmd&0xff-1)也可以一样的效果*/

    handler = s_tim_ioctl_map[index].handler;
    if(handler != NULL)
        ret = handler(priv, arg, arg_len);
    else
        ret = VFS_ERR_INVAL;

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief TIM Client 设备读操作,为了语义明确,读操作只读取当前计数值,不读取其他状态
 * @param pdev 设备对象指针
 * @param buf 缓冲区
 * @param len 长度
 * @param timeout_ms 超时时间
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int vfs_tim_Base_read(struct device*pdev,void*buf,size_t len,uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;
    struct vfs_tim_priv*            priv;
    struct dev_lifecycle*           lc;
    int                             ret;
    lc = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    ret  =  dev_lc_io_begin(lc);
    if(ret!=VFS_OK)
        return ret;
    priv =  container_of(pdev->ops,struct vfs_tim_priv,ops);
    ret  =  hal_tim_get_counter(&priv->tim,(uint32_t*)buf);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief TIM Client 设备写操作,为了语义明确,写操作只写入当前计数值,不写入其他状态
 * @param pdev 设备对象指针
 * @param buf 缓冲区
 * @param len 长度
 * @param timeout_ms 超时时间
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int vfs_tim_Base_write(struct device*pdev,const void*buf,size_t len,uint32_t timeout_ms)
{
    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;
    COMPAT_IGNORE_RESULT(timeout_ms); COMPAT_IGNORE_RESULT(len);
    struct vfs_tim_priv* priv;
    struct dev_lifecycle*lc;
    int ret;
    const struct vfs_tim_arg* arg  = (const struct vfs_tim_arg*)buf;
    lc                      = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    ret                     = dev_lc_io_begin(lc);
    if(ret!=VFS_OK)
        return ret;
    priv                    = container_of(pdev->ops,struct vfs_tim_priv,ops);
    
    ret                     = hal_tim_set_counter(&priv->tim,arg->value);

    dev_lc_io_end(lc);
    return VFS_OK;
}

/**
 * @brief TIM Client 设备挂起操作,挂起操作只挂起定时器,不挂起其他状态
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int vfs_tim_Base_suspend(struct device*pdev)
{
    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;
    struct vfs_tim_priv*        priv;
    struct dev_lifecycle*       lc;
    int                         ret;

    lc                        = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    ret                       = dev_lc_io_begin(lc);
    if(ret!=VFS_OK)
        return VFS_ERR_IO;
    priv                      = container_of(pdev->ops,struct vfs_tim_priv,ops);

    ret                       = hal_tim_base_stop(&priv->tim);
    if(ret!=VFS_OK)
        return VFS_ERR_IO;

    dev_lc_io_end(lc);

    return VFS_OK;
}

/**
 * @brief TIM Client 设备恢复操作,恢复操作只恢复定时器,不恢复其他状态,如果定时器已经恢复,则返回VFS_ERR_ALREADY_DONE
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static int vfs_tim_Base_resume(struct device*pdev)
{
    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;
    struct          vfs_tim_priv* priv;
    struct          dev_lifecycle*lc;
    int             ret;

    lc =device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    ret     = dev_lc_io_begin(lc);
    if(ret != VFS_OK)
        return ret;

    priv    = container_of(pdev->ops,struct vfs_tim_priv,ops);
    ret     = hal_tim_base_start(&priv->tim);

    dev_lc_io_end(lc);
    return VFS_OK;
}

/**
 * @brief TIM Client 设备文件操作
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
static const struct file_operations fops=
{
    .close              = vfs_tim_close,
    .open               = vfs_tim_open,
    .ioctl              = vfs_tim_ioctl,
    .read               = vfs_tim_Base_read,
    .write              = vfs_tim_Base_write,
    .suspend            = vfs_tim_Base_suspend,
    .resume             = vfs_tim_Base_resume,
};


/**
 * @brief TIM Client 设备探测操作 (初始化定时器, 解析 DTS, 设置文件操作)
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL, VFS_ERR_NOMEM, VFS_ERR_IO
 */
static int vfs_tim_probe(struct device*pdev)
{
    struct vfs_tim_priv*        priv;
    int                         pool_idx;
    int                         ret;
    /*<本处只能看这个是不是null, 不能看ops是不是null, 因为ops是通过device_set_priv设置的*/
    if(!pdev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_tim_priv_pool_ctrl);
    if(pool_idx<0)
        return VFS_ERR_NOMEM;

    priv = &s_tim_priv_pool[pool_idx];
    COMPAT_MEM_SET(priv,0,sizeof(*priv));
    priv->pool_idx = pool_idx;

    if(vfs_tim_priv_parse_dts(pdev,&priv->cfg)!=VFS_OK)
    {
        SYS_LOGE(s_kTag,"dts parse failed: %s",device_get_name(pdev));
        ret = VFS_ERR_INVAL;
        goto err_pool;
    }

    {
        int hw_instance = 0;
        int clk_periph  = 0;
        int irqn        = -1;
        int int_mask    = 0;
        if(device_get_prop_int(pdev,"hw-instance",&hw_instance)!=VFS_OK)
        {
            SYS_LOGE(s_kTag,"missing hw-instance: %s",device_get_name(pdev));
            ret = VFS_ERR_INVAL;
            goto err_pool;
        }
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev,"clk-periph",&clk_periph));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev,"irqn",&irqn));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev,"interrupt-mask",&int_mask));
        priv->cfg.tim_handle  = (uintptr_t)hw_instance;
        priv->cfg.clk_periph  = (uint32_t)clk_periph;
        priv->cfg.irqn        = irqn;
        priv->cfg.int_mask    = (uint32_t)int_mask;
    }

    ret = hal_tim_device_init(&priv->tim, &priv->unique, &priv->cfg);
    if(ret!=VFS_OK)
    {
        SYS_LOGE(s_kTag,"hal_tim_device_init failed: %s",device_get_name(pdev));
        goto err_pool;
    }

    priv->ops = fops;
    pdev->ops = &priv->ops;

    if(device_set_priv(pdev,priv)!=VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_deinit;
    }

    SYS_LOGI(s_kTag,"probe OK %s",device_get_name(pdev));
    return VFS_OK;

err_deinit:
    pdev->ops = NULL;
    COMPAT_IGNORE_RESULT(hal_tim_device_deinit(&priv->tim));
err_pool:
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_tim_priv_pool_ctrl,pool_idx));
    return ret;
}

static int vfs_tim_remove(struct device*pdev)
{
    struct vfs_tim_priv*        priv;
    struct dev_lifecycle*       lc;
    int                         pool_idx;

    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops,struct vfs_tim_priv,ops);
    lc = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);

    if(dev_lc_remove_drain(lc,OSAL_WAIT_FOREVER)!=VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    COMPAT_IGNORE_RESULT(hal_tim_close(&priv->tim));
    COMPAT_IGNORE_RESULT(hal_tim_device_deinit(&priv->tim));

    COMPAT_MEM_SET(priv,0,sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_tim_priv_pool_ctrl,pool_idx));

    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(vfs_tim_priv,"tim",vfs_tim_probe,vfs_tim_remove)