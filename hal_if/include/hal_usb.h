#ifndef HAL_USB_H
#define HAL_USB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_usb_dev hal_usb_dev_t;

/* USB 工作模式 */
typedef enum
{
    HAL_USB_MODE_DEVICE,    /* 设备模式 */
    HAL_USB_MODE_HOST,      /* 主机模式 */
    HAL_USB_MODE_OTG,       /* OTG 模式 */
} hal_usb_mode_t;

/* USB 速率 */
typedef enum
{
    HAL_USB_SPEED_LOW,      /* 低速 1.5 Mbps */
    HAL_USB_SPEED_FULL,     /* 全速 12 Mbps */
    HAL_USB_SPEED_HIGH,     /* 高速 480 Mbps */
} hal_usb_speed_t;

/* 端点类型 */
typedef enum
{
    HAL_USB_EP_CTRL,        /* 控制端点 */
    HAL_USB_EP_BULK,        /* 批量端点 */
    HAL_USB_EP_INT,         /* 中断端点 */
    HAL_USB_EP_ISO,         /* 同步端点 */
} hal_usb_ep_type_t;

/* USB 控制器配置 */
typedef struct
{
    int                 usb_id;     /* USB 控制器编号 */
    hal_usb_mode_t      mode;       /* 工作模式 */
    hal_usb_speed_t     speed;      /* 目标速率 */
    int                 dp_pin;     /* DP 引脚 */
    int                 dm_pin;     /* DM 引脚 */
    int                 vbus_pin;   /* VBUS 检测引脚, -1 = 未用 */
} hal_usb_config_t;

/* 端点配置 */
typedef struct
{
    int              ep_addr;        /* 端点地址, bit7 = 方向(0=OUT, 1=IN) */
    hal_usb_ep_type_t type;          /* 端点类型 */
    int              max_pkt;        /* 最大包长 */
    uint32_t         fifo_size;      /* FIFO 分配大小(字节), IN 端点有效 */
} hal_usb_ep_config_t;

/* USB 事件回调, event 取值由实现定义 */
typedef void (*hal_usb_callback_t)(hal_usb_dev_t* usb, int event, void* user_data);

typedef struct
{
    int (*init)(hal_usb_dev_t* usb, const hal_usb_config_t* cfg);
    int (*deinit)(hal_usb_dev_t* usb);

    /* 通用控制 */
    int (*connect)(hal_usb_dev_t* usb);
    int (*disconnect)(hal_usb_dev_t* usb);
    int (*set_callback)(hal_usb_dev_t* usb, hal_usb_callback_t cb, void* user_data);

    /* 配置端点 */
    int (*ep_config)(hal_usb_dev_t* usb, const hal_usb_ep_config_t* ep_cfg);
    int (*ep_enable)(hal_usb_dev_t* usb, int ep_addr);
    int (*ep_disable)(hal_usb_dev_t* usb, int ep_addr);

    /* 数据传输 */
    int (*ep_write)(hal_usb_dev_t* usb, int ep_addr, const uint8_t* data, size_t len);
    int (*ep_read)(hal_usb_dev_t* usb, int ep_addr, uint8_t* data, size_t len);

    /* 复位与电源管理 */
    int (*reset)(hal_usb_dev_t* usb);
    int (*suspend)(hal_usb_dev_t* usb);     /* 进入挂起 */
    int (*resume)(hal_usb_dev_t* usb);      /* 退出挂起/远程唤醒 */
    void* _impl;
};

void hal_usb_init_struct(hal_usb_dev_t* usb);
void hal_usb_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_USB_H */
