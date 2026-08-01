/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file rs485_modbus_drv.c
 * @brief RS485 Modbus RTU 驱动实现 — 挂在 UART 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_rs485_modbus_pool[RS485_MODBUS_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与参数结构见 rs485_modbus_drv.h。
 *
 * 数据流: VFS ioctl → rs485_modbus_cmd_* → rs485_modbus_uart_xchg（DE 切换 + device_write/read）→ HAL
 */
#include "rs485_modbus_drv.h"
#include "vfs-uart.h"
#include "vfs-gpio.h"
#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "status.h"
#include "dt_config_gen.h"
#include "compiler_compat.h"
#include "osal.h"
#include "system_log.h"
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_MODBUS_RTU_RS485
#define DTC_GEN_COUNT_MODBUS_RTU_RS485  1
#endif
#define RS485_MODBUS_POOL_COUNT  DTC_GEN_COUNT_MODBUS_RTU_RS485

/** @brief RS485 Modbus 驱动实例（嵌入 fops 与收发控制引脚） */
struct rs485_modbus_device
{
    struct file_operations ops;      /**< 挂入 device 的 fops */
    struct device* uart_dev;         /**< 所属 UART client 设备 */
    struct device* de_dev;           /**< DE/RE 方向控制 GPIO 设备 */
    struct vfs_gpio_arg de_gpio;     /**< 方向控制 GPIO 参数 */

    int                    hw_ready; /**< 硬件已初始化标志 */
};

static struct rs485_modbus_device s_rs485_modbus_pool[RS485_MODBUS_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_rs485_modbus_used[RS485_MODBUS_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_rs485_modbus_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "rs485_modbus";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160)
static void rs485_modbus_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_rs485_modbus_pool_ctrl, s_rs485_modbus_used, RS485_MODBUS_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param dev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct rs485_modbus_device* rs485_modbus_get_drvdata(struct device* dev)
{
    return (struct rs485_modbus_device*)device_get_priv(dev);
}

/**
 * @brief 切换 DE/RE 方向（1=发送，0=接收）
 */
static int rs485_de(struct rs485_modbus_device* d, int tx)
{
    d->de_gpio.level = tx ? 1 : 0;
    return vfs_gpio_set_level(&d->de_gpio);
}

/**
 * @brief RS485 半双工帧交换（DE 拉高发送 → DE 拉低接收）
 * @param d   驱动实例
 * @param tx  发送帧（含 CRC）
 * @param tx_len 发送长度
 * @param rx  接收缓冲（NULL 则不接收）
 * @param rx_len 期望接收长度
 * @param to  超时 ms
 * @return 实际接收字节数（>=0），或 VFS_ERR_*
 */
static int rs485_modbus_uart_xchg(struct rs485_modbus_device* d, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, uint32_t to)
{
    int n;
    if (!d || !d->uart_dev || !tx || tx_len == 0)
    {
        return VFS_ERR_INVAL;
    }
    rs485_de(d, 1);
    n = device_write(d->uart_dev, tx, tx_len, to);
    rs485_de(d, 0);
    if (n < 0)
    {
        return n;
    }
    if (!rx || rx_len == 0)
    {
        return 0;
    }
    return device_read(d->uart_dev, rx, rx_len, to);
}

/**
 * @brief CRC16（Modbus RTU 多项式 0xA001）
 */
static uint16_t rs485_modbus_crc(const uint8_t* p, size_t n)
{
    uint16_t c=0xFFFF; size_t i, j;
    for(i=0;i<n;i++){ c^=p[i]; for(j=0;j<8;j++) c=(c&1)?(c>>1)^0xA001U:(c>>1); }
    return c;
}


/**
 * @brief 首次 open 时打开 UART 与方向控制 GPIO 并绑定参数
 * @return VFS_OK 或 VFS_ERR_*
 */
static int rs485_modbus_hw_create(struct rs485_modbus_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->uart_dev, NULL); if (r != VFS_OK) return r;
      r = device_open(d->de_dev, NULL); if (r != VFS_OK) return r;
      r = device_ioctl(d->de_dev, GPIO_CMD_GET_LEVEL, &d->de_gpio, sizeof(d->de_gpio), 0);
      if (r != VFS_OK) return r; }
    d->de_gpio.level = 0; vfs_gpio_set_level(&d->de_gpio);
    d->hw_ready = 1; return VFS_OK;

}

/**
 * @brief 释放硬件资源（关闭 UART 与 DE 设备）
 */
static void rs485_modbus_hw_destroy(struct rs485_modbus_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->uart_dev) COMPAT_IGNORE_RESULT(device_close(d->uart_dev));
    if (d->de_dev) COMPAT_IGNORE_RESULT(device_close(d->de_dev));
    d->hw_ready = 0;

}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int rs485_modbus_open(struct device* dev, void* arg)
{
    struct rs485_modbus_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = rs485_modbus_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = rs485_modbus_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

/**
 * @brief fops.close：引用计数关闭，末次调用释放硬件
 */
static int rs485_modbus_close(struct device* dev)
{
    struct rs485_modbus_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = rs485_modbus_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        rs485_modbus_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*rs485_modbus_ioctl_fn_t)(struct rs485_modbus_device* d, void* arg, size_t arg_len, uint32_t ms);
struct rs485_modbus_ioctl_map { rs485_modbus_ioctl_fn_t handler; };


/**
 * @brief RS485_MODBUS_CMD_READ_HOLDING 实现：03 功能码读保持寄存器并回填
 */
static int rs485_modbus_cmd_read(struct rs485_modbus_device* d, void* arg, size_t len, uint32_t to)
{
    struct modbus_read* r = (struct modbus_read*)arg;
    uint8_t req[8];
    uint8_t rsp[32];
    uint16_t crc;
    int n;
    if (!d->hw_ready || !r || len != sizeof(*r))
    {
        return VFS_ERR_INVAL;
    }
    req[0] = r->slave;
    req[1] = 0x03;
    req[2] = (uint8_t)(r->addr >> 8);
    req[3] = (uint8_t)r->addr;
    req[4] = 0;
    req[5] = 1;
    crc = rs485_modbus_crc(req, 6);
    req[6] = (uint8_t)crc;
    req[7] = (uint8_t)(crc >> 8);
    n = rs485_modbus_uart_xchg(d, req, 8, rsp, sizeof(rsp), to);
    if (n < 7)
    {
        return VFS_ERR_IO;
    }
    crc = rs485_modbus_crc(rsp, (size_t)(n - 2));
    if (rsp[n - 2] != (uint8_t)crc || rsp[n - 1] != (uint8_t)(crc >> 8))
    {
        return VFS_ERR_IO;
    }
    r->value = (uint16_t)((rsp[3] << 8) | rsp[4]);
    return VFS_OK;
}
/**
 * @brief RS485_MODBUS_CMD_WRITE_SINGLE 实现：06 功能码写单寄存器
 */
static int rs485_modbus_cmd_write(struct rs485_modbus_device* d, void* arg, size_t len, uint32_t to)
{
    struct modbus_write* w = (struct modbus_write*)arg;
    uint8_t req[8];
    uint16_t crc;
    int n;
    if (!d->hw_ready || !w || len != sizeof(*w))
    {
        return VFS_ERR_INVAL;
    }
    req[0] = w->slave;
    req[1] = 0x06;
    req[2] = (uint8_t)(w->addr >> 8);
    req[3] = (uint8_t)w->addr;
    req[4] = (uint8_t)(w->value >> 8);
    req[5] = (uint8_t)w->value;
    crc = rs485_modbus_crc(req, 6);
    req[6] = (uint8_t)crc;
    req[7] = (uint8_t)(crc >> 8);
    n = rs485_modbus_uart_xchg(d, req, 8, NULL, 0, to);
    if (n < 0)
    {
        return VFS_ERR_IO;
    }
    return VFS_OK;
}
static const struct rs485_modbus_ioctl_map s_rs485_modbus_map[RS485_MODBUS_CMD_COUNT] = {
    [RS485_MODBUS_CMD_READ_HOLDING - RS485_MODBUS_CMD_BASE - 1] = { rs485_modbus_cmd_read },
    [RS485_MODBUS_CMD_WRITE_SINGLE - RS485_MODBUS_CMD_BASE - 1] = { rs485_modbus_cmd_write },
};


/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int rs485_modbus_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct rs485_modbus_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = rs485_modbus_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)RS485_MODBUS_CMD_BASE;
    if (off < 1 || off > RS485_MODBUS_CMD_COUNT || !s_rs485_modbus_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_rs485_modbus_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations rs485_modbus_fops =
{
    .open  = rs485_modbus_open,
    .close = rs485_modbus_close,
    .ioctl = rs485_modbus_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 UART 设备与 DE 引脚并挂 fops
 */
static int rs485_modbus_probe(struct device* dev)
{
    struct rs485_modbus_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_rs485_modbus_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_rs485_modbus_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->uart_dev = device_get_parent(dev);
    if (!d->uart_dev) { ret = VFS_ERR_NODEV; goto err; }
    d->de_dev = device_get_phandle_dev(dev, "de-gpio");
    if (IS_ERR(d->de_dev)) { ret = PTR_ERR(d->de_dev); goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = rs485_modbus_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_rs485_modbus_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int rs485_modbus_remove(struct device* dev)
{
    struct rs485_modbus_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = rs485_modbus_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_rs485_modbus_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    rs485_modbus_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_rs485_modbus_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(rs485_modbus, "modbus,rtu-rs485", rs485_modbus_probe, rs485_modbus_remove)
