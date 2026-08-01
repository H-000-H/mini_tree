/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file air780e_drv.c
 * @brief Air780E 4G 模块驱动实现 — 挂在 UART 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_air780e_pool[AIR780E_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与参数结构见 air780e_drv.h。
 *
 * 数据流: VFS ioctl → air780e_cmd_send/recv → device_read/write(UART) → HAL
 */
#include "air780e_drv.h"
#include "vfs-uart.h"
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

#ifndef DTC_GEN_COUNT_AIR780E_4G
#define DTC_GEN_COUNT_AIR780E_4G  1
#endif
#define AIR780E_POOL_COUNT  DTC_GEN_COUNT_AIR780E_4G

/** @brief Air780E 驱动实例（嵌入 fops） */
struct air780e_device
{
    struct file_operations ops;      /**< 挂入 device 的 fops */
    struct device* uart_dev;         /**< 所属 UART client 设备 */

    int                    hw_ready; /**< 硬件已初始化标志 */
};

static struct air780e_device s_air780e_pool[AIR780E_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_air780e_used[AIR780E_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_air780e_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "air780e";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160)
static void air780e_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_air780e_pool_ctrl, s_air780e_used, AIR780E_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param dev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct air780e_device* air780e_get_drvdata(struct device* dev)
{
    return (struct air780e_device*)device_get_priv(dev);
}


/**
 * @brief UART 双向传输（UART_CMD_TRANSFER）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int air780e_uart_xchg(struct air780e_device* d, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, uint32_t to)
{
    struct uart_transfer_arg arg;
    if (!d || !d->uart_dev)
        return VFS_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.tx_len = tx_len;
    arg.rx_len = rx_len;
    return device_ioctl(d->uart_dev, UART_CMD_TRANSFER, &arg, sizeof(arg), to);
}


/**
 * @brief 首次 open 时打开 UART 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int air780e_hw_create(struct air780e_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->uart_dev, NULL); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

/**
 * @brief 释放硬件资源（关闭 UART client）
 */
static void air780e_hw_destroy(struct air780e_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->uart_dev) COMPAT_IGNORE_RESULT(device_close(d->uart_dev));
    d->hw_ready = 0;

}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int air780e_open(struct device* dev, void* arg)
{
    struct air780e_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = air780e_get_drvdata(dev);
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
        ret = air780e_hw_create(d);
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
static int air780e_close(struct device* dev)
{
    struct air780e_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = air780e_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        air780e_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*air780e_ioctl_fn_t)(struct air780e_device* d, void* arg, size_t arg_len, uint32_t ms);
struct air780e_ioctl_map { air780e_ioctl_fn_t handler; };


/**
 * @brief AIR780E_CMD_AT_SEND 实现：UART 发送 AT 命令
 */
static int air780e_cmd_send(struct air780e_device* d, void* arg, size_t len, uint32_t to)
{
    struct air780e_at* a=(struct air780e_at*)arg;
    if(!d->hw_ready||!a||len!=sizeof(*a)||!a->tx||!a->tx_len) return VFS_ERR_INVAL;
    return device_write(d->uart_dev, a->tx, a->tx_len, to);
}
/**
 * @brief AIR780E_CMD_AT_RECV 实现：UART 接收 AT 应答并回填长度
 */
static int air780e_cmd_recv(struct air780e_device* d, void* arg, size_t len, uint32_t to)
{
    struct air780e_at* a = (struct air780e_at*)arg;
    int r;

    if (!d->hw_ready || !a || len != sizeof(*a) || !a->rx || a->rx_cap == 0U)
        return VFS_ERR_INVAL;
    r = device_read(d->uart_dev, a->rx, a->rx_cap, to);
    if (r < 0)
        return r;
    a->rx_len = (size_t)r;
    return VFS_OK;
}
static const struct air780e_ioctl_map s_air780e_map[AIR780E_CMD_COUNT] = {
    [AIR780E_CMD_AT_SEND - AIR780E_CMD_BASE - 1] = { air780e_cmd_send },
    [AIR780E_CMD_AT_RECV - AIR780E_CMD_BASE - 1] = { air780e_cmd_recv },
};


/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int air780e_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct air780e_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = air780e_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)AIR780E_CMD_BASE;
    if (off < 1 || off > AIR780E_CMD_COUNT || !s_air780e_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_air780e_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations air780e_fops =
{
    .open  = air780e_open,
    .close = air780e_close,
    .ioctl = air780e_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 UART 设备并挂 fops
 */
static int air780e_probe(struct device* dev)
{
    struct air780e_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_air780e_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_air780e_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->uart_dev = device_get_parent(dev);
    if (!d->uart_dev) { ret = VFS_ERR_NODEV; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = air780e_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_air780e_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int air780e_remove(struct device* dev)
{
    struct air780e_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = air780e_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_air780e_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    air780e_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_air780e_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(air780e, "air780e,4g", air780e_probe, air780e_remove)
