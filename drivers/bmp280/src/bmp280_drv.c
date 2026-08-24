/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file bmp280_drv.c
 *@brief BMP280 气压/温度传感器驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_bmp280_pool[BMP280_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与采样结构见 bmp280_drv.h，寄存器定义见 bmp280_regs.h。
 *   数据流: VFS ioctl → bmp280_cmd_read → device_read/write(I2C) → HAL
 */

#include "bmp280_drv.h"

#include "bmp280_regs.h"
#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-i2c.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_BOSCH_BMP280
#define DTC_GEN_COUNT_BOSCH_BMP280 1
#endif
#define BMP280_POOL_COUNT DTC_GEN_COUNT_BOSCH_BMP280

/** @brief BMP280 驱动实例（嵌入 fops 与校准系数） */
struct bmp280_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */
    uint16_t dig_T1; /**< 温度校准 T1（小端） */
    int16_t dig_T2; /**< 温度校准 T2 */
    int16_t dig_T3; /**< 温度校准 T3 */
    uint16_t dig_P1; /**< 气压校准 P1 */
    int16_t dig_P2; /**< 气压校准 P2 */
    int16_t dig_P3; /**< 气压校准 P3 */
    int16_t dig_P4; /**< 气压校准 P4 */
    int16_t dig_P5; /**< 气压校准 P5 */
    int16_t dig_P6; /**< 气压校准 P6 */
    int16_t dig_P7; /**< 气压校准 P7 */
    int16_t dig_P8; /**< 气压校准 P8 */
    int16_t dig_P9; /**< 气压校准 P9 */
    int32_t t_fine; /**< 温度补偿中间量（共享给 P） */
    int calib_ok; /**< 校准参数已加载 */
    int hw_ready; /**< 硬件已初始化标志 */
};

static struct bmp280_device s_bmp280_pool[BMP280_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_bmp280_used[BMP280_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_bmp280_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "bmp280";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void bmp280_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_bmp280_pool_ctrl, s_bmp280_used, BMP280_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct bmp280_device* bmp280_get_drvdata(struct device* pdev)
{
    return (struct bmp280_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @param[in] dev 驱动实例
 * @param[in] tx 发送缓冲
 * @param[in] len 发送长度
 * @param[in] timeout_ms 超时（ms）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int bmp280_i2c_wr(struct bmp280_device* dev, const uint8_t* tx, size_t len,
                         uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !tx || len == 0U)
        return MINI_ERR_INVAL;
    return device_write(dev->i2c_dev, tx, len, timeout_ms);
}
/**
 * @brief 从 I2C 总线读数据
 * @param[in] dev 驱动实例
 * @param[out] rx 接收缓冲
 * @param[in] len 接收长度
 * @param[in] timeout_ms 超时（ms）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int bmp280_i2c_rd(struct bmp280_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !rx || len == 0U)
        return MINI_ERR_INVAL;
    return device_read(dev->i2c_dev, rx, len, timeout_ms);
}

/**
 * @brief 读连续寄存器（先写起始地址，再读）
 * @param[in] start 起始寄存器地址
 * @return MINI_OK 或 VFS_ERR_*
 */
static int bmp280_read_regs(struct bmp280_device* dev, uint8_t start, uint8_t* buf, size_t len,
                            uint32_t timeout_ms)
{
    int ret = bmp280_i2c_wr(dev, &start, 1, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    return bmp280_i2c_rd(dev, buf, len, timeout_ms);
}

/**
 * @brief 加载校准系数（T/P）并校验可用
 * @return MINI_OK 或 VFS_ERR_*
 */
static int bmp280_load_calib(struct bmp280_device* dev, uint32_t timeout_ms)
{
    uint8_t calib_raw[24];
    int ret = bmp280_read_regs(dev, BMP280_REG_DIG_T1, calib_raw, sizeof(calib_raw), timeout_ms);
    if (ret != MINI_OK)
        return ret;
    dev->dig_T1 = (uint16_t)(calib_raw[0] | ((uint16_t)calib_raw[1] << 8));
    dev->dig_T2 = (int16_t)(calib_raw[2] | ((uint16_t)calib_raw[3] << 8));
    dev->dig_T3 = (int16_t)(calib_raw[4] | ((uint16_t)calib_raw[5] << 8));
    dev->dig_P1 = (uint16_t)(calib_raw[6] | ((uint16_t)calib_raw[7] << 8));
    dev->dig_P2 = (int16_t)(calib_raw[8] | ((uint16_t)calib_raw[9] << 8));
    dev->dig_P3 = (int16_t)(calib_raw[10] | ((uint16_t)calib_raw[11] << 8));
    dev->dig_P4 = (int16_t)(calib_raw[12] | ((uint16_t)calib_raw[13] << 8));
    dev->dig_P5 = (int16_t)(calib_raw[14] | ((uint16_t)calib_raw[15] << 8));
    dev->dig_P6 = (int16_t)(calib_raw[16] | ((uint16_t)calib_raw[17] << 8));
    dev->dig_P7 = (int16_t)(calib_raw[18] | ((uint16_t)calib_raw[19] << 8));
    dev->dig_P8 = (int16_t)(calib_raw[20] | ((uint16_t)calib_raw[21] << 8));
    dev->dig_P9 = (int16_t)(calib_raw[22] | ((uint16_t)calib_raw[23] << 8));
    dev->calib_ok = 1;
    return MINI_OK;
}

/* Bosch BMP280 datasheet 补偿公式（32-bit） */
/**
 * @brief 温度原始值补偿，输出 0.01℃（同时更新 t_fine）
 * @param[in] adc_t 温度 ADC 原始值
 * @return 温度 ×100
 */
static int32_t bmp280_compensate_t(struct bmp280_device* dev, int32_t adc_t)
{
    int32_t var1;
    int32_t var2;
    var1 = ((((adc_t >> 3) - ((int32_t)dev->dig_T1 << 1))) * ((int32_t)dev->dig_T2)) >> 11;
    var2 = (((((adc_t >> 4) - ((int32_t)dev->dig_T1)) * ((adc_t >> 4) - ((int32_t)dev->dig_T1))) >>
             12) *
            ((int32_t)dev->dig_T3)) >>
           14;
    dev->t_fine = var1 + var2;
    return (dev->t_fine * 5 + 128) >> 8; /* 0.01°C */
}

/**
 * @brief 气压原始值补偿，依赖 t_fine
 * @param[in] adc_p 气压 ADC 原始值
 * @return 气压 Pa
 */
static uint32_t bmp280_compensate_p(struct bmp280_device* dev, int32_t adc_p)
{
    int64_t var1;
    int64_t var2;
    int64_t pressure;
    var1 = ((int64_t)dev->t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)dev->dig_P6;
    var2 = var2 + ((var1 * (int64_t)dev->dig_P5) << 17);
    var2 = var2 + (((int64_t)dev->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dev->dig_P3) >> 8) + ((var1 * (int64_t)dev->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)dev->dig_P1) >> 33;
    if (var1 == 0)
        return 0;
    pressure = 1048576 - adc_p;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dev->dig_P9) * (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = (((int64_t)dev->dig_P8) * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) + (((int64_t)dev->dig_P7) << 4);
    return (uint32_t)(pressure >> 8); /* Pa */
}

/**
 * @brief 首次 open 时初始化硬件：软复位 + 加载校准
 * @return MINI_OK 或 VFS_ERR_*
 */
static int bmp280_hw_create(struct bmp280_device* dev)
{
    int ret;
    const uint8_t soft_rst[2] = {BMP280_REG_SOFT_RESET, BMP280_SOFT_RESET_VAL};
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    ret = device_open(dev->i2c_dev, NULL);
    if (ret != MINI_OK)
        return ret;
    ret = bmp280_i2c_wr(dev, soft_rst, 2, 100);
    if (ret != MINI_OK)
    {
        COMPAT_IGNORE_RESULT(device_close(dev->i2c_dev));
        return ret;
    }
    osal_delay_ms(10);
    ret = bmp280_load_calib(dev, 100);
    if (ret != MINI_OK)
    {
        COMPAT_IGNORE_RESULT(device_close(dev->i2c_dev));
        return ret;
    }
    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void bmp280_hw_destroy(struct bmp280_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;

    if (dev->i2c_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->i2c_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int bmp280_open(struct device* pdev, void* arg)
{
    struct bmp280_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = bmp280_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = MINI_OK;
    if (first == 1)
    {
        ret = bmp280_hw_create(dev);
        if (ret != MINI_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return MINI_OK;
}

/**
 * @brief fops.close：引用计数关闭，末次调用释放硬件
 */
static int bmp280_close(struct device* pdev)
{
    struct bmp280_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = bmp280_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        bmp280_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*bmp280_ioctl_fn_t)(struct bmp280_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct bmp280_ioctl_map
{
    bmp280_ioctl_fn_t handler;
};

/**
 * @brief BMP280_CMD_READ_PRESS_TEMP 实现：触发强制采样并读取 P/T
 */
static int bmp280_cmd_read(struct bmp280_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    const uint8_t ctrl[2] = {BMP280_REG_CTRL_MEAS, BMP280_CTRL_FORCED_X1};
    uint8_t raw[6];
    struct bmp280_sample* sample = (struct bmp280_sample*)arg;
    int ret;
    int32_t adc_p;
    int32_t adc_t;
    int32_t t_x100;
    if (!dev->hw_ready || !dev->calib_ok || !sample || len != sizeof(*sample))
        return MINI_ERR_INVAL;
    ret = bmp280_i2c_wr(dev, ctrl, 2, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    osal_delay_ms(10);
    ret = bmp280_read_regs(dev, BMP280_REG_PRESS_MSB, raw, sizeof(raw), timeout_ms);
    if (ret != MINI_OK)
        return ret;
    adc_p = (int32_t)(((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | (raw[2] >> 4));
    adc_t = (int32_t)(((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | (raw[5] >> 4));
    t_x100 = bmp280_compensate_t(dev, adc_t);
    sample->temp_c_x100 = (int16_t)t_x100;
    sample->press_pa = (int32_t)bmp280_compensate_p(dev, adc_p);
    return MINI_OK;
}

static const struct bmp280_ioctl_map s_bmp280_map[BMP280_CMD_COUNT] = {
    [BMP280_CMD_READ_PRESS_TEMP - BMP280_CMD_BASE - 1] = {bmp280_cmd_read},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int bmp280_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct bmp280_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = bmp280_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)BMP280_CMD_BASE;
    if (off < 1 || off > BMP280_CMD_COUNT || !s_bmp280_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_bmp280_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations bmp280_fops = {
    .open = bmp280_open,
    .close = bmp280_close,
    .ioctl = bmp280_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int bmp280_probe(struct device* pdev)
{
    struct bmp280_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_bmp280_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_bmp280_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->i2c_dev = device_get_parent(pdev);
    if (!dev->i2c_dev)
    {
        ret = MINI_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = bmp280_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_bmp280_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int bmp280_remove(struct device* pdev)
{
    struct bmp280_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = bmp280_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_bmp280_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    bmp280_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_bmp280_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(bmp280, "bosch,bmp280", bmp280_probe, bmp280_remove)
