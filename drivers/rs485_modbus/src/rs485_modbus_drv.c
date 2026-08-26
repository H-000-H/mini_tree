/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file rs485_modbus_drv.c
 *@brief RS485 Modbus RTU 驱动实现 — 挂在 UART 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_rs485_modbus_pool[RS485_MODBUS_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 rs485_modbus_drv.h。
 *   数据流: VFS ioctl → rs485_modbus_cmd_* → rs485_modbus_uart_xchg（DE 切换 + device_write/read）→
 *   HAL
 */

#include "rs485_modbus_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"
#include "vfs-uart.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_MODBUS_RTU_RS485
#define DTC_GEN_COUNT_MODBUS_RTU_RS485 1
#endif
#define RS485_MODBUS_POOL_COUNT DTC_GEN_COUNT_MODBUS_RTU_RS485

/** @brief RS485 Modbus 驱动实例（嵌入 fops 与收发控制引脚） */
struct rs485_modbus_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* uart_dev; /**< 所属 UART client 设备 */
    struct device* de_dev; /**< DE/RE 方向控制 GPIO 设备 */
    struct vfs_gpio_arg de_gpio; /**< 方向控制 GPIO 参数 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct rs485_modbus_device s_rs485_modbus_pool[RS485_MODBUS_POOL_COUNT] MINI_ALIGNED(4);
static uint8_t s_rs485_modbus_used[RS485_MODBUS_POOL_COUNT] MINI_ALIGNED(4);
static osal_pool_t s_rs485_modbus_pool_ctrl MINI_ALIGNED(4);
static const char* const k_tag = "rs485_modbus";

/**
 * @brief 驱动池启动初始化（mini_pre_execution 阶段，创建静态对象池）
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_DRIVER_POOL) static void rs485_modbus_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(
        osal_pool_init(&s_rs485_modbus_pool_ctrl, s_rs485_modbus_used, RS485_MODBUS_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct rs485_modbus_device* rs485_modbus_get_drvdata(struct device* pdev)
{
    return (struct rs485_modbus_device*)device_get_priv(pdev);
}

/**
 * @brief 切换 DE/RE 方向（1=发送，0=接收）
 */
static int rs485_de(struct rs485_modbus_device* dev, int tx)
{
    dev->de_gpio.level = tx ? 1 : 0;
    return vfs_gpio_set_level(&dev->de_gpio);
}

/**
 * @brief RS485 半双工帧交换（DE 拉高发送 → DE 拉低接收）
 * @param[in] dev   驱动实例
 * @param[in] tx  发送帧（含 CRC）
 * @param[in] tx_len 发送长度
 * @param[out] rx  接收缓冲（NULL 则不接收）
 * @param[out] rx_len 期望接收长度
 * @param[in] timeout_ms  超时 ms
 * @return 实际接收字节数（>=0），或 VFS_ERR_*
 */
static int rs485_modbus_uart_xchg(struct rs485_modbus_device* dev, const uint8_t* tx, size_t tx_len,
                                  uint8_t* rx, size_t rx_len, uint32_t timeout_ms)
{
    int count;
    if (!dev || !dev->uart_dev || !tx || tx_len == 0)
        return MINI_ERR_INVAL;
    rs485_de(dev, 1);
    count = device_write(dev->uart_dev, tx, tx_len, timeout_ms);
    rs485_de(dev, 0);
    if (count < 0)
        return count;
    if (!rx || rx_len == 0)
        return 0;
    return device_read(dev->uart_dev, rx, rx_len, timeout_ms);
}

/**
 * @brief CRC16（Modbus RTU 多项式 0xA001）
 */
static uint16_t rs485_modbus_crc(const uint8_t* data, size_t count)
{
    uint16_t crc_val = 0xFFFF;
    size_t byte_index, bit_index;
    for (byte_index = 0; byte_index < count; byte_index++)
    {
        crc_val ^= data[byte_index];
        for (bit_index = 0; bit_index < 8; bit_index++)
            crc_val = (crc_val & 1) ? (crc_val >> 1) ^ 0xA001U : (crc_val >> 1);
    }
    return crc_val;
}

/**
 * @brief 首次 open 时打开 UART 与方向控制 GPIO 并绑定参数
 * @return MINI_OK 或 VFS_ERR_*
 */
static int rs485_modbus_hw_create(struct rs485_modbus_device* dev)
{
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    {
        int ret = device_open(dev->uart_dev, NULL);
        if (ret != MINI_OK)
            return ret;
        ret = device_open(dev->de_dev, NULL);
        if (ret != MINI_OK)
            return ret;
        ret = device_ioctl(dev->de_dev, GPIO_CMD_GET_LEVEL, &dev->de_gpio, sizeof(dev->de_gpio), 0);
        if (ret != MINI_OK)
            return ret;
    }
    dev->de_gpio.level = 0;
    vfs_gpio_set_level(&dev->de_gpio);
    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 UART 与 DE 设备）
 */
static void rs485_modbus_hw_destroy(struct rs485_modbus_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->uart_dev)
        MINI_IGNORE_RESULT(device_close(dev->uart_dev));
    if (dev->de_dev)
        MINI_IGNORE_RESULT(device_close(dev->de_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int rs485_modbus_open(struct device* pdev, void* arg)
{
    struct rs485_modbus_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    MINI_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = rs485_modbus_get_drvdata(pdev);
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
        ret = rs485_modbus_hw_create(dev);
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
static int rs485_modbus_close(struct device* pdev)
{
    struct rs485_modbus_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = rs485_modbus_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        rs485_modbus_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*rs485_modbus_ioctl_fn_t)(struct rs485_modbus_device* dev, void* arg, size_t arg_len,
                                       uint32_t ms);
struct rs485_modbus_ioctl_map
{
    rs485_modbus_ioctl_fn_t handler;
};

/**
 * @brief RS485_MODBUS_CMD_READ_HOLDING 实现：03 功能码读保持寄存器并回填
 */
static int rs485_modbus_cmd_read(struct rs485_modbus_device* dev, void* arg, size_t len,
                                 uint32_t timeout_ms)
{
    struct modbus_read* rd = (struct modbus_read*)arg;
    uint8_t req[8];
    uint8_t rsp[32];
    uint16_t crc;
    int rx_len;
    if (!dev->hw_ready || !rd || len != sizeof(*rd))
        return MINI_ERR_INVAL;
    req[0] = rd->slave;
    req[1] = 0x03;
    req[2] = (uint8_t)(rd->addr >> 8);
    req[3] = (uint8_t)rd->addr;
    req[4] = 0;
    req[5] = 1;
    crc = rs485_modbus_crc(req, 6);
    req[6] = (uint8_t)crc;
    req[7] = (uint8_t)(crc >> 8);
    rx_len = rs485_modbus_uart_xchg(dev, req, 8, rsp, sizeof(rsp), timeout_ms);
    if (rx_len < 7)
        return MINI_ERR_IO;
    crc = rs485_modbus_crc(rsp, (size_t)(rx_len - 2));
    if (rsp[rx_len - 2] != (uint8_t)crc || rsp[rx_len - 1] != (uint8_t)(crc >> 8))
        return MINI_ERR_IO;
    rd->value = (uint16_t)((rsp[3] << 8) | rsp[4]);
    return MINI_OK;
}
/**
 * @brief RS485_MODBUS_CMD_WRITE_SINGLE 实现：06 功能码写单寄存器
 */
static int rs485_modbus_cmd_write(struct rs485_modbus_device* dev, void* arg, size_t len,
                                  uint32_t timeout_ms)
{
    struct modbus_write* wr = (struct modbus_write*)arg;
    uint8_t req[8];
    uint16_t crc;
    int tx_len;
    if (!dev->hw_ready || !wr || len != sizeof(*wr))
        return MINI_ERR_INVAL;
    req[0] = wr->slave;
    req[1] = 0x06;
    req[2] = (uint8_t)(wr->addr >> 8);
    req[3] = (uint8_t)wr->addr;
    req[4] = (uint8_t)(wr->value >> 8);
    req[5] = (uint8_t)wr->value;
    crc = rs485_modbus_crc(req, 6);
    req[6] = (uint8_t)crc;
    req[7] = (uint8_t)(crc >> 8);
    tx_len = rs485_modbus_uart_xchg(dev, req, 8, NULL, 0, timeout_ms);
    if (tx_len < 0)
        return MINI_ERR_IO;
    return MINI_OK;
}
static const struct rs485_modbus_ioctl_map s_rs485_modbus_map[RS485_MODBUS_CMD_COUNT] = {
    [RS485_MODBUS_CMD_READ_HOLDING - RS485_MODBUS_CMD_BASE - 1] = {rs485_modbus_cmd_read},
    [RS485_MODBUS_CMD_WRITE_SINGLE - RS485_MODBUS_CMD_BASE - 1] = {rs485_modbus_cmd_write},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int rs485_modbus_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct rs485_modbus_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = rs485_modbus_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)RS485_MODBUS_CMD_BASE;
    if (off < 1 || off > RS485_MODBUS_CMD_COUNT || !s_rs485_modbus_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_rs485_modbus_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations rs485_modbus_fops = {
    .open = rs485_modbus_open,
    .close = rs485_modbus_close,
    .ioctl = rs485_modbus_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 UART 设备与 DE 引脚并挂 fops
 */
static int rs485_modbus_probe(struct device* pdev)
{
    struct rs485_modbus_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_rs485_modbus_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_rs485_modbus_pool[pool_idx];
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    dev->uart_dev = device_get_parent(pdev);
    if (!dev->uart_dev)
    {
        ret = MINI_ERR_NODEV;
        goto err;
    }
    dev->de_dev = device_get_phandle_dev(pdev, "de-gpio");
    if (IS_ERR(dev->de_dev))
    {
        ret = PTR_ERR(dev->de_dev);
        goto err;
    }

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = rs485_modbus_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_rs485_modbus_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int rs485_modbus_remove(struct device* pdev)
{
    struct rs485_modbus_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = rs485_modbus_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_rs485_modbus_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    rs485_modbus_hw_destroy(dev);
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_rs485_modbus_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(rs485_modbus, "modbus,rtu-rs485", rs485_modbus_probe, rs485_modbus_remove)
