/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * UART VFS 实现 — UART 总线子系统 VFS 层
 *
 * 两层结构:
 *   - Host VFS:   DTS 解析 + uart_bus_host_init (controller driver)
 *   - Client VFS: uart_bus_client_register + fops 挂载 (bus client driver)
 *
 * 生命周期 (dev_lifecycle): open/close 引用计数, io 门控 (dev_lc_io_*), remove drain。
 * I/O: read/write 走 uart_bus_*; ioctl(TRANSFER) 走 uart_bus_transfer (先写后读)。
 * Host remove 安全: host_deinit 返回 BUSY 时 remove 拒绝销毁, 防 client 仍 open 时 host UAF。
 *
 * @see bus/uart/uart_bus.h  bus 层接口
 *@=========================================================================================================================*/
#define UART_VFS_IMPL
#include "vfs-uart.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "board_define_uart.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "uart_bus.h"
#include <string.h>

/*===========================================================================================================================================================*/
/*Host VFS*/
/*===========================================================================================================================================================*/
/* 池大小宏见 board_define_uart.h (数量由 DTS 节点数自动生成) */

struct vfs_uart_priv
{
    struct hal_uart_config cfg; /**< host 配置 (DTSI 直投) */
    int pool_idx; /**< 池索引 */
};

static struct vfs_uart_priv s_uart_priv_pool[UART_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_uart_priv_used[UART_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_uart_priv_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_host_tag = "uart_host_vfs";

/**
 * @brief UART Host VFS 私有数据池启动初始化
 */
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void vfs_uart_priv_pool_init(void)
{
    COMPAT_IGNORE_RESULT(
        osal_pool_init(&s_uart_priv_pool_ctrl, s_uart_priv_used, UART_VFS_PRIV_COUNT));
}

/**
 * @brief 解析 UART Host DTS 属性 (硬件直投值), 填入 hal_uart_config
 * @param pdev 设备对象指针
 * @param cfg 配置结构指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_uart_priv_parse_dts(struct device* pdev, struct hal_uart_config* cfg)
{
    /* 硬件直投: DTSI 提供厂商宏值, VFS 零翻译填入 hal_uart_config。 device_get_prop_int 取 int*,
     * 指针/uint32 字段用 int temp + (uintptr_t) cast。 */
    int uart_base = 0, uart_clk = 0, uart_baud = 0;
    int data_width = 0, parity = 0, stop_bits = 0;
    int direction = 0, hw_control = 0, oversampling = 0;
    int tx_port = 0, tx_pin = 0, tx_clk = 0, tx_af = 0;
    int tx_output_type = 0, tx_speed = 0, tx_mode = 0, tx_pull = 0;
    int rx_port = 0, rx_pin = 0, rx_clk = 0, rx_af = 0;
    int rx_output_type = 0, rx_speed = 0, rx_mode = 0, rx_pull = 0;

    if (device_get_prop_int(pdev, "uart-base", &uart_base) != VFS_OK ||
        device_get_prop_int(pdev, "uart-clk", &uart_clk) != VFS_OK ||
        device_get_prop_int(pdev, "uart-baud", &uart_baud) != VFS_OK ||
        device_get_prop_int(pdev, "data-width", &data_width) != VFS_OK ||
        device_get_prop_int(pdev, "parity", &parity) != VFS_OK ||
        device_get_prop_int(pdev, "stop-bits", &stop_bits) != VFS_OK ||
        device_get_prop_int(pdev, "tx-port", &tx_port) != VFS_OK ||
        device_get_prop_int(pdev, "tx-pin", &tx_pin) != VFS_OK ||
        device_get_prop_int(pdev, "tx-clk", &tx_clk) != VFS_OK ||
        device_get_prop_int(pdev, "tx-af", &tx_af) != VFS_OK ||
        device_get_prop_int(pdev, "rx-port", &rx_port) != VFS_OK ||
        device_get_prop_int(pdev, "rx-pin", &rx_pin) != VFS_OK ||
        device_get_prop_int(pdev, "rx-clk", &rx_clk) != VFS_OK ||
        device_get_prop_int(pdev, "rx-af", &rx_af) != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }
    /** 扩展字段: DTS 可选, 未定义时取 0 (LL 库默认行为) */
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "direction", &direction));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "hw-control", &hw_control));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "oversampling", &oversampling));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tx-output-type", &tx_output_type));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tx-speed", &tx_speed));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tx-mode", &tx_mode));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tx-pull", &tx_pull));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "rx-output-type", &rx_output_type));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "rx-speed", &rx_speed));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "rx-mode", &rx_mode));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "rx-pull", &rx_pull));

    {
        int irqn = -1, irq_priority = 0, it_enable = 0;
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "irqn", &irqn));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "irq-priority", &irq_priority));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "it-enable", &it_enable));
        COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
        cfg->irqn = (int32_t)irqn;
        cfg->irq_priority = (uint32_t)irq_priority;
        cfg->it_enable = (uint32_t)it_enable;
    }
    cfg->uart = (uintptr_t)uart_base;
    cfg->uart_clk_periph = (uint32_t)uart_clk;
    cfg->baud_rate = (uint32_t)uart_baud;
    cfg->data_width = (uint32_t)data_width;
    cfg->parity = (uint32_t)parity;
    cfg->stop_bits = (uint32_t)stop_bits;
    cfg->direction = (uint32_t)direction;
    cfg->hw_control = (uint32_t)hw_control;
    cfg->oversampling = (uint32_t)oversampling;
    cfg->tx = (struct hal_uart_pin_cfg){
        .port = (uintptr_t)tx_port,
        .pin = (uint16_t)tx_pin,
        .clk_bus = (uint32_t)tx_clk,
        .af = (uint32_t)tx_af,
        .output_type = (uint32_t)tx_output_type,
        .speed = (uint32_t)tx_speed,
        .mode = (uint32_t)tx_mode,
        .pull = (uint32_t)tx_pull,
    };
    cfg->rx = (struct hal_uart_pin_cfg){
        .port = (uintptr_t)rx_port,
        .pin = (uint16_t)rx_pin,
        .clk_bus = (uint32_t)rx_clk,
        .af = (uint32_t)rx_af,
        .output_type = (uint32_t)rx_output_type,
        .speed = (uint32_t)rx_speed,
        .mode = (uint32_t)rx_mode,
        .pull = (uint32_t)rx_pull,
    };

    /** DMA 短元组 (≥6): <handle stream channel priority memory_size enable>
     *  长元组 (≥15): 其后接 direction/mode/inc/... (缺省由 HAL 补默认) */
    {
        int dma_arr[15];
        int n = device_get_prop_int_array(pdev, "dma-cfg", dma_arr, 15);
        if (n >= 6)
        {
            cfg->dma_cfg.dma_handle = (uintptr_t)dma_arr[0];
            cfg->dma_cfg.dma_stream = (uint32_t)dma_arr[1];
            cfg->dma_cfg.dma_channel = (uint32_t)dma_arr[2];
            cfg->dma_cfg.dma_priority = (uint32_t)dma_arr[3];
            cfg->dma_cfg.dma_memory_size = (uint32_t)dma_arr[4];
            cfg->dma_cfg.dma_enable = (uint32_t)dma_arr[5];
            if (n >= 15)
            {
                cfg->dma_cfg.dma_direction = (uint32_t)dma_arr[6];
                cfg->dma_cfg.dma_mode = (uint32_t)dma_arr[7];
                cfg->dma_cfg.dma_periph_inc = (uint32_t)dma_arr[8];
                cfg->dma_cfg.dma_mem_inc = (uint32_t)dma_arr[9];
                cfg->dma_cfg.dma_periph_data_size = (uint32_t)dma_arr[10];
                cfg->dma_cfg.dma_fifo_mode = (uint32_t)dma_arr[11];
                cfg->dma_cfg.dma_fifo_threshold = (uint32_t)dma_arr[12];
                cfg->dma_cfg.dma_mem_burst = (uint32_t)dma_arr[13];
                cfg->dma_cfg.dma_periph_burst = (uint32_t)dma_arr[14];
            }
        }
    }

    return VFS_OK;
}

/**
 * @brief UART Host 设备探测: 申请池槽, 解析 DTS, 调用 uart_bus_host_init
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_uart_priv_probe(struct device* pdev)
{
    struct vfs_uart_priv* priv;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_uart_priv_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_uart_priv_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (vfs_uart_priv_parse_dts(pdev, &priv->cfg) != VFS_OK)
    {
        SYS_LOGE(k_host_tag, "dts parse failed: %s", device_get_name(pdev));
        ret = VFS_ERR_INVAL;
        goto err_pool;
    }

    ret = uart_bus_host_init(pdev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_pool;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_bus;
    }

    SYS_LOGI(k_host_tag, "probe OK: %s baud=%lu", device_get_name(pdev),
             (unsigned long)priv->cfg.baud_rate);
    return VFS_OK;

err_bus:
    COMPAT_IGNORE_RESULT(uart_bus_host_deinit(pdev));
err_pool:
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_priv_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief UART Host 设备移除: remove_start → ops_unregister → remove_drain → host_deinit → 释放池槽
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_uart_priv_remove(struct device* pdev)
{
    struct vfs_uart_priv* priv;
    struct dev_lifecycle* lc;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    priv = (struct vfs_uart_priv*)device_get_priv(pdev);
    if (IS_ERR(priv))
        return PTR_ERR(priv);

    lc = device_lc(pdev);
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

    ret = uart_bus_host_deinit(pdev);
    if (ret != VFS_OK)
    {
        SYS_LOGE(k_host_tag, "host_deinit failed: %s", device_get_name(pdev));
        dev_lc_remove_finish(lc);
        return ret;
    }

    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_priv_pool_ctrl, pool_idx));

    dev_lc_remove_finish(lc);
    return VFS_OK;
}

/*===========================================================================================================================================================*/
/*Client VFS*/
/*===========================================================================================================================================================*/
/* client 池宏见 board_define_uart.h */

struct uart_vfs_client
{
    struct file_operations ops; /**< VFS 操作表 */
    int pool_idx; /**< 池索引 */
};

static struct uart_vfs_client s_uart_vfs_pool[UART_VFS_COUNT];
static uint8_t s_uart_vfs_used[UART_VFS_COUNT];
static osal_pool_t s_uart_vfs_pool_ctrl;
static const char* const k_tag = "uart_vfs";

/**
 * @brief UART Client VFS 私有数据池启动初始化
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void uart_vfs_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_uart_vfs_pool_ctrl, s_uart_vfs_used, UART_VFS_COUNT));
}

/**
 * @brief UART Client 设备打开操作 (引用计数, 首次打开时调用 uart_bus_open)
 * @param pdev 设备对象指针
 * @param arg 命令参数指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int uart_vfs_open(struct device* pdev, void* arg)
{
    struct dev_lifecycle* lc;
    int first;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;

    if (first == 1)
    {
        if (uart_bus_open(pdev) != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return VFS_ERR_IO;
        }
    }

    dev_lc_open_end(lc);
    return VFS_OK;
}

/**
 * @brief UART Client 设备关闭操作 (引用计数, 末次关闭时调用 uart_bus_close)
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int uart_vfs_close(struct device* pdev)
{
    struct dev_lifecycle* lc;
    int last;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;

    if (last)
        COMPAT_IGNORE_RESULT(uart_bus_close(pdev));

    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief UART Client 设备写操作 (dev_lc_io 门控, 调用 uart_bus_write)
 * @param pdev 设备对象指针
 * @param buf 数据缓冲
 * @param len 数据长度 (字节)
 * @param timeout_ms 超时 (毫秒, 0=平台默认)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int uart_vfs_write(struct device* pdev, const void* buf, size_t len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int ret;

    if (!pdev || !pdev->ops || !buf || len == 0)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    ret = uart_bus_write(pdev, (const uint8_t*)buf, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief UART Client 设备读操作 (dev_lc_io 门控, 调用 uart_bus_read)
 * @param pdev 设备对象指针
 * @param buf 数据缓冲
 * @param len 数据长度 (字节)
 * @param timeout_ms 超时 (毫秒, 0=平台默认)
 * @return 成功返回已读字节数, 失败返回负数错误码
 */
static int uart_vfs_read(struct device* pdev, void* buf, size_t len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int ret;

    if (!pdev || !pdev->ops || !buf || len == 0)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    ret = uart_bus_read(pdev, (uint8_t*)buf, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

/*===========================================================================================================================================================*/
/*ioctl 命令映射表*/
/*===========================================================================================================================================================*/
typedef int (*uart_ioctl_fn_t)(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms);

struct uart_ioctl_map
{
    uart_ioctl_fn_t handler; /**< ioctl 处理函数 */
};

/**
 * @brief UART 命令处理: 半双工传输 (先写后读)
 * @param pdev 设备对象指针
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int uart_cmd_transfer(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct uart_transfer_arg* t = (const struct uart_transfer_arg*)arg;

    if (!t || arg_len != sizeof(*t) || (!t->tx && !t->rx))
        return VFS_ERR_INVAL;

    return uart_bus_transfer(pdev, t->tx, t->rx, t->tx_len, t->rx_len, timeout_ms);
}

static const struct uart_ioctl_map s_uart_ioctl_map[UART_CMD_COUNT] = {
    [UART_CMD_TRANSFER - UART_CMD_BASE - 1] = {uart_cmd_transfer},
};

/**
 * @brief UART Client 设备 ioctl 控制 (命令映射表 O(1) 派发)
 * @param pdev 设备对象指针
 * @param cmd 控制命令
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int uart_vfs_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len,
                          uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int32_t offset;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    offset = (int32_t)cmd - (int32_t)UART_CMD_BASE;
    if (offset < 1 || offset > UART_CMD_COUNT || !s_uart_ioctl_map[offset - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_uart_ioctl_map[offset - 1].handler(pdev, arg, arg_len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations uart_vfs_fops = {
    .open = uart_vfs_open,
    .close = uart_vfs_close,
    .write = uart_vfs_write,
    .read = uart_vfs_read,
    .ioctl = uart_vfs_ioctl,
};

/**
 * @brief UART Client VFS probe: 注册 bus client 并挂载 fops
 * @param pdev client device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_vfs_probe(struct device* pdev)
{
    struct uart_vfs_client* priv;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_uart_vfs_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_uart_vfs_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    /* UART 无 per-client 配置, client_register 无需 cfg */
    ret = uart_bus_client_register(pdev);
    if (ret != VFS_OK)
        goto err_pool;

    priv->ops = uart_vfs_fops;
    pdev->ops = &priv->ops;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        uart_bus_client_unregister(pdev);
        ret = VFS_ERR_IO;
        goto err_pool;
    }

    SYS_LOGI(k_tag, "probe OK: %s", device_get_name(pdev));
    return VFS_OK;

err_pool:
    pdev->ops = NULL; /* 切断 fops, 防 UAF */
    dev_lc_reset(device_lc(pdev)); /* 重置生命周期 */
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_vfs_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief UART Client VFS remove: drain 生命周期并注销 bus client
 * @param pdev client device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_vfs_remove(struct device* pdev)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle* lc;
    int pool_idx;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct uart_vfs_client, ops);
    lc = device_lc(pdev);
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

    uart_bus_client_unregister(pdev);
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_uart_vfs_pool_ctrl, pool_idx));

    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(vfs_uart_priv, "uart", vfs_uart_priv_probe, vfs_uart_priv_remove)
DRIVER_REGISTER(uart_vfs, "uart-client", uart_vfs_probe, uart_vfs_remove)
