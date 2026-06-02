#ifndef COMPAT_ARDUINO_IO_H
#define COMPAT_ARDUINO_IO_H

#include "device.h"
#include "hal_gpio.h"
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Arduino 兼容 API ──
 *
 * 通过 VFS 提供 Arduino 风格的 GPIO 操作。适用于快速原型开发。
 * 所有函数均为 static inline，零额外开销。
 *
 * 基础用法（需要已获取 device_t*）:
 *   device_t* led = device_find_by_label("led0");
 *   pinMode(led, HAL_MAKE_PIN(0, 5), OUTPUT);
 *   digitalWrite(led, HAL_MAKE_PIN(0, 5), HIGH);
 *   int val = digitalRead(led, HAL_MAKE_PIN(0, 3));
 *
 * 一步到位（通过 label 查找 + 操作）:
 *   digitalWriteByLabel("led0", HAL_MAKE_PIN(0, 5), HIGH);
 *
 * 延时:
 *   delay(100);   // 阻塞 100ms
 */

/* 电平常量 */
#define LOW  0
#define HIGH 1

/* 引脚模式常量 */
#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2

/* 配置引脚模式 */
static inline void pinMode(device_t* dev, hal_pin_t pin, int mode)
{
    hal_gpio_config_t cfg;
    cfg.pin       = pin;
    cfg.mode      = (mode == OUTPUT) ? HAL_GPIO_MODE_OUTPUT : HAL_GPIO_MODE_INPUT;
    cfg.pull      = (mode == INPUT_PULLUP) ? HAL_GPIO_PULL_UP : HAL_GPIO_PULL_NONE;
    cfg.intr_type = HAL_GPIO_INTR_DISABLE;
    device_ioctl(dev, GPIO_CMD_CONFIG, &cfg, sizeof(cfg), 1000);
}

/* 设置引脚电平 */
static inline void digitalWrite(device_t* dev, hal_pin_t pin, int val)
{
    gpio_level_arg_t arg;
    arg.pin   = pin;
    arg.level = val;
    device_ioctl(dev, GPIO_CMD_SET_LEVEL, &arg, sizeof(arg), 1000);
}

/* 读取引脚电平 */
static inline int digitalRead(device_t* dev, hal_pin_t pin)
{
    gpio_level_arg_t arg;
    arg.pin   = pin;
    arg.level = 0;
    device_ioctl(dev, GPIO_CMD_GET_LEVEL, &arg, sizeof(arg), 1000);
    return arg.level;
}

/* 阻塞延时（毫秒） */
static inline void delay(uint32_t ms)
{
    osal_task_sleep(ms);
}

/* ── 一步到位宏（label 查找 + 操作） ── */
#define digitalWriteByLabel(label, pin, val) \
    digitalWrite(device_find_by_label(label), (pin), (val))

#define digitalReadByLabel(label, pin) \
    digitalRead(device_find_by_label(label), (pin))

#define pinModeByLabel(label, pin, mode) \
    pinMode(device_find_by_label(label), (pin), (mode))

#ifdef __cplusplus
}
#endif

#endif /* COMPAT_ARDUINO_IO_H */
