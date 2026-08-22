/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-i2s.c
 *@brief I2S VFS — Host/Client + sync DMA/poll + circular + HT/TC/async ioctl
 *@author H-000-H
 *@details
 *   @note        虚拟中断在 i2s_bus_open 注册 (对齐 ADC probe), 不进 ioctl
 */

#define I2S_VFS_IMPL
#include "vfs-i2s.h"

#include "board_define_i2s.h"
#include "buffer.h"
#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "i2s_bus.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

/* 池/环缓大小宏见 board_define_i2s.h (数量由 DTS 节点数自动生成) */

/**
 * @brief Host VFS 私有数据 (环缓 + DTSI 配置)
 */
struct vfs_i2s_host_priv
{
    struct hal_i2s_bus_config cfg; /**< host 总线配置 (DTSI 直投) */
    struct fifo_spsc circ_fifo; /**< circular 环缓 */
    fifo_data_type circ_buf[I2S_CIRC_FIFO_SIZE] COMPAT_ALIGNED(32); /**< 环缓数据区 */
    int pool_idx; /**< 池索引 */
};

/**
 * @brief Client VFS 私有数据 (fops + 设备参数 + 默认同步路径)
 */
struct vfs_i2s_client
{
    struct file_operations ops; /**< VFS 操作表 */
    struct hal_i2s_device_config cfg; /**< 设备配置 (DTSI 直投) */
    uint32_t xfer_mode; /**< I2S_XFER_AUTO/POLL/DMA */
    int pool_idx; /**< 池索引 */
};

static struct vfs_i2s_host_priv s_host_pool[I2S_HOST_POOL];
static uint8_t s_host_used[I2S_HOST_POOL];
static osal_pool_t s_host_pool_ctrl;
static struct vfs_i2s_client s_client_pool[I2S_CLIENT_POOL];
static uint8_t s_client_used[I2S_CLIENT_POOL];
static osal_pool_t s_client_pool_ctrl;
static const char* k_host = "i2s_host";
static const char* k_cli = "i2s_vfs";

/**
 * @brief Host/Client 私有池启动初始化
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void pools(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_host_pool_ctrl, s_host_used, I2S_HOST_POOL));
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_client_pool_ctrl, s_client_used, I2S_CLIENT_POOL));
}

/**
 * @brief 解析 dma-tx/rx-cfg 元组到 hal_i2s_dma_config
 * @param[in] pdev 设备对象指针
 * @param[in] prop DTS 属性名 (如 "dma-tx-cfg")
 * @param[in] d 输出的 DMA 配置结构指针
 * @return 成功返回 VFS_OK (属性缺失或不足 6 元视为可选, 不报错)
 */
static int parse_dma_tuple(struct device* pdev, const char* prop, struct hal_i2s_dma_config* d)
{
    int dma_arr[14];
    int n;

    COMPAT_MEM_SET(d, 0, sizeof(*d));
    n = device_get_prop_int_array(pdev, prop, dma_arr, 14);
    if (n < 6)
        return VFS_OK;

    d->dma_handle = (uintptr_t)dma_arr[0];
    d->dma_stream = (uint32_t)dma_arr[1];
    d->dma_channel = (uint32_t)dma_arr[2];
    d->dma_priority = (uint32_t)dma_arr[3];
    d->dma_memory_size = (uint32_t)dma_arr[4];
    d->dma_enable = (uint32_t)dma_arr[5];
    if (n >= 14)
    {
        d->dma_mode = (uint32_t)dma_arr[6];
        d->dma_periph_inc = (uint32_t)dma_arr[7];
        d->dma_mem_inc = (uint32_t)dma_arr[8];
        d->dma_periph_data_size = (uint32_t)dma_arr[9];
        d->dma_fifo_mode = (uint32_t)dma_arr[10];
        d->dma_fifo_threshold = (uint32_t)dma_arr[11];
        d->dma_mem_burst = (uint32_t)dma_arr[12];
        d->dma_periph_burst = (uint32_t)dma_arr[13];
    }
    return VFS_OK;
}

/**
 * @brief 解析一组 I2S 引脚属性 (port/pin/clk/af 键名由调用方指定)
 * @param[in] pdev 设备对象指针
 * @param[in] port_k port 属性键名
 * @param[in] pin_k pin 属性键名
 * @param[in] clk_k clk 属性键名
 * @param[in] af_k af 属性键名
 * @param[out] out 输出的引脚配置结构指针
 * @return 成功返回 VFS_OK (port 缺失时 out 清零且不报错)
 */
static int parse_one_pin(struct device* pdev, const char* port_k, const char* pin_k, const char* clk_k, const char* af_k, struct hal_i2s_pin_cfg* out)
{
    int v;

    COMPAT_MEM_SET(out, 0, sizeof(*out));
    if (device_get_prop_int(pdev, port_k, &v) != VFS_OK)
        return VFS_OK;

    out->port = (uintptr_t)v;
    if (device_get_prop_int(pdev, pin_k, &v) == VFS_OK)
        out->pin = (uint16_t)v;
    if (device_get_prop_int(pdev, clk_k, &v) == VFS_OK)
        out->clk_bus = (uint32_t)v;
    if (device_get_prop_int(pdev, af_k, &v) == VFS_OK)
        out->af = (uint32_t)v;
    return VFS_OK;
}

/**
 * @brief 解析 Host DTSI 属性, 填入 hal_i2s_bus_config
 * @param[in] pdev 设备对象指针
 * @param[in] cfg 输出的总线配置结构指针
 * @param[in] role 总线角色 (MASTER/SLAVE)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int parse_host(struct device* pdev, struct hal_i2s_bus_config* cfg, uint32_t role)
{
    int v;
    int irqn = -1;
    int irqn_rx = -1;
    int irq_priority = 0;
    int it_enable = 0;

    COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
    cfg->bus_role = role;
    cfg->irqn = -1;
    cfg->irqn_rx = -1;

    if (device_get_prop_int(pdev, "spi-base", &v) != VFS_OK && device_get_prop_int(pdev, "hw-instance", &v) != VFS_OK)
        return VFS_ERR_INVAL;
    cfg->spi = (uintptr_t)v;

    if (device_get_prop_int(pdev, "spi-clk", &v) != VFS_OK)
        return VFS_ERR_INVAL;
    cfg->spi_clk_periph = (uint32_t)v;

    (void)parse_one_pin(pdev, "ws-port", "ws-pin", "ws-clk", "ws-af", &cfg->ws);
    (void)parse_one_pin(pdev, "ck-port", "ck-pin", "ck-clk", "ck-af", &cfg->ck);
    (void)parse_one_pin(pdev, "sd-port", "sd-pin", "sd-clk", "sd-af", &cfg->sd);
    (void)parse_one_pin(pdev, "mck-port", "mck-pin", "mck-clk", "mck-af", &cfg->mck);
    (void)parse_dma_tuple(pdev, "dma-tx-cfg", &cfg->dma_tx);
    (void)parse_dma_tuple(pdev, "dma-rx-cfg", &cfg->dma_rx);

    /* 虚拟中断 / NVIC: 对齐 ADC dma-irqn; 缺省不使能 */
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-tx-irqn", &irqn));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-rx-irqn", &irqn_rx));
    if (irqn < 0)
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-irqn", &irqn));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "irq-priority", &irq_priority));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "it-enable", &it_enable));
    cfg->irqn = (int32_t)irqn;
    cfg->irqn_rx = (int32_t)irqn_rx;
    cfg->irq_priority = (uint32_t)irq_priority;
    cfg->it_enable = (uint32_t)it_enable;

    return VFS_OK;
}

/**
 * @brief 解析 Client DTSI 属性, 填入 hal_i2s_device_config (字段均为可选, 缺省为 0)
 * @param[in] pdev 设备对象指针
 * @param[in] cfg 输出的设备配置结构指针
 * @return 成功返回 VFS_OK
 */
static int parse_client(struct device* pdev, struct hal_i2s_device_config* cfg)
{
    int v;

    COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "i2s-mode", &v));
    cfg->mode = (uint32_t)v;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "i2s-standard", &v));
    cfg->standard = (uint32_t)v;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "data-format", &v));
    cfg->data_format = (uint32_t)v;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "mclk-output", &v));
    cfg->mclk_output = (uint32_t)v;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "audio-freq", &v));
    cfg->audio_freq = (uint32_t)v;
    return VFS_OK;
}

/**
 * @brief Host 探测: 申请池槽, 解析 DTSI, 初始化环缓, 调用 i2s_bus_host_init
 * @param[in] pdev 设备对象指针
 * @param[in] role 总线角色 (MASTER/SLAVE)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 * @note 虚拟中断不在此处注册 (尚无 hal_i2s_dev); 见 i2s_bus_open
 */
static int host_probe(struct device* pdev, uint32_t role)
{
    struct vfs_i2s_host_priv* priv;
    int idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    idx = osal_pool_claim(&s_host_pool_ctrl);
    if (idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_host_pool[idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = idx;

    ret = parse_host(pdev, &priv->cfg, role);
    if (ret != VFS_OK)
        goto err;

    fifo_init(&priv->circ_fifo, priv->circ_buf, (uint16_t)I2S_CIRC_FIFO_SIZE);
    priv->cfg.circ_fifo = &priv->circ_fifo;

    ret = i2s_bus_host_init(pdev, &priv->cfg);
    if (ret != VFS_OK)
        goto err;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(i2s_bus_host_deinit(pdev));
        ret = VFS_ERR_IO;
        goto err;
    }

    SYS_LOGI(k_host, "probe OK %s (dma_tx=%u dma_rx=%u it=%u)", device_get_name(pdev), (unsigned)priv->cfg.dma_tx.dma_enable, (unsigned)priv->cfg.dma_rx.dma_enable, (unsigned)priv->cfg.it_enable);
    return VFS_OK;

err:
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_host_pool_ctrl, idx));
    return ret;
}

/**
 * @brief Host 移除: i2s_bus_host_deinit 后归还私有池 (不做 lifecycle drain)
 * @param[in] pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int host_remove(struct device* pdev)
{
    struct vfs_i2s_host_priv* priv = device_get_priv(pdev);
    int ret;
    int idx;

    if (IS_ERR(priv))
        return PTR_ERR(priv);

    idx = priv->pool_idx;
    ret = i2s_bus_host_deinit(pdev);
    if (ret != VFS_OK)
        return ret;
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_host_pool_ctrl, idx));
    return VFS_OK;
}

/**
 * @brief Host Master 角色探测入口
 * @param[in] p 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int host_probe_master(struct device* p) { return host_probe(p, I2S_BUS_ROLE_MASTER); }

/**
 * @brief Host Slave 角色探测入口
 * @param[in] p 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int host_probe_slave(struct device* p) { return host_probe(p, I2S_BUS_ROLE_SLAVE); }

/**
 * @brief Client 打开: 引用计数, 首次打开时调用 i2s_bus_open (含虚拟中断注册)
 * @param[in] pdev 设备对象指针
 * @param[in] arg 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_open(struct device* pdev, void* arg)
{
    struct dev_lifecycle* lc;
    int first;
    int ret;

    COMPAT_IGNORE_RESULT(arg);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = i2s_bus_open(pdev);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
    }
    if (ret == VFS_OK)
        dev_lc_open_end(lc);
    return ret;
}

/**
 * @brief Client 关闭: 引用计数, 末次关闭时调用 i2s_bus_close
 * @param[in] pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_close(struct device* pdev)
{
    struct dev_lifecycle* lc;
    int last;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last == 1)
        COMPAT_IGNORE_RESULT(i2s_bus_close(pdev));
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief Client 写: 同步 TX, 路径由 priv->xfer_mode 决定
 * @param[in] pdev 设备对象指针
 * @param[in] buf 待发送采样缓冲区 (16-bit)
 * @param[in] len 请求写入字节数 (内部按 sizeof(uint16_t) 换算采样数)
 * @param[in] to 超时 (毫秒)
 * @return 成功返回 (int)len (实际写入字节数), 失败返回负数错误码
 */
static int i2s_write(struct device* pdev, const void* buf, size_t len, uint32_t to)
{
    struct vfs_i2s_client* priv;
    struct dev_lifecycle* lc;
    size_t samples;
    int ret;

    if (!pdev || !pdev->ops || !buf || len == 0)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_i2s_client, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    samples = len / sizeof(uint16_t);
    ret = i2s_bus_transfer(pdev, (const uint16_t*)buf, NULL, samples, to, priv->xfer_mode);
    dev_lc_io_end(lc);
    return (ret == VFS_OK) ? (int)len : ret;
}

/**
 * @brief Client 读: 同步 RX, 路径由 priv->xfer_mode 决定
 * @param[in] pdev 设备对象指针
 * @param[in] buf 接收采样缓冲区 (16-bit)
 * @param[in] len 请求读取字节数 (内部按 sizeof(uint16_t) 换算采样数)
 * @param[in] to 超时 (毫秒)
 * @return 成功返回 (int)len (实际读取字节数), 失败返回负数错误码
 */
static int i2s_read(struct device* pdev, void* buf, size_t len, uint32_t to)
{
    struct vfs_i2s_client* priv;
    struct dev_lifecycle* lc;
    size_t samples;
    int ret;

    if (!pdev || !pdev->ops || !buf || len == 0)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_i2s_client, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    samples = len / sizeof(uint16_t);
    ret = i2s_bus_transfer(pdev, NULL, (uint16_t*)buf, samples, to, priv->xfer_mode);
    dev_lc_io_end(lc);
    return (ret == VFS_OK) ? (int)len : ret;
}

/*===========================================================================================================================================================*/
/* ioctl 命令映射表 — index = (cmd - I2S_CMD_BASE - 1), 与 I2S_CMD_* 编号一一对应 (对齐 SPI) */
/*===========================================================================================================================================================*/
typedef int (*i2s_ioctl_fn_t)(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms);

struct i2s_ioctl_map
{
    i2s_ioctl_fn_t handler; /**< ioctl 处理函数 */
};

/**
 * @brief I2S 命令: 同步传输 (samples 为 16-bit 采样数; arg.xfer_mode==AUTO 用 client 偏好)
 * @param[in] pdev 设备对象指针
 * @param[in] arg i2s_transfer_arg 参数指针
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_transfer(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct i2s_transfer_arg* a = (const struct i2s_transfer_arg*)arg;
    struct vfs_i2s_client* priv;
    uint32_t mode;

    if (!pdev || !pdev->ops || !a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_i2s_client, ops);
    mode = (a->xfer_mode == I2S_XFER_AUTO) ? priv->xfer_mode : a->xfer_mode;
    if (mode > I2S_XFER_DMA)
        return VFS_ERR_INVAL;

    return i2s_bus_transfer(pdev, a->tx, a->rx, a->samples, timeout_ms, mode);
}

/**
 * @brief I2S 命令: 设置 write/read/默认 transfer 的传输路径偏好
 * @param[in] pdev 设备对象指针
 * @param[in] arg i2s_xfer_mode_arg 参数指针
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_set_xfer_mode(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct i2s_xfer_mode_arg* a = (const struct i2s_xfer_mode_arg*)arg;
    struct vfs_i2s_client* priv;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops || !a || arg_len != sizeof(*a) || a->xfer_mode > I2S_XFER_DMA)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_i2s_client, ops);
    priv->xfer_mode = a->xfer_mode;
    return VFS_OK;
}

/**
 * @brief I2S 命令: 查询当前 xfer_mode
 * @param[in] pdev 设备对象指针
 * @param[in] arg i2s_xfer_mode_arg 输出参数指针
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_get_xfer_mode(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct i2s_xfer_mode_arg* a = (struct i2s_xfer_mode_arg*)arg;
    struct vfs_i2s_client* priv;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops || !a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_i2s_client, ops);
    a->xfer_mode = priv->xfer_mode;
    return VFS_OK;
}

/**
 * @brief I2S 命令: 异步提交 (总线层当前忽略 cb/userdata, 占位路径; 完成需 I2S_CMD_ASYNC_WAIT)
 * @param[in] pdev 设备对象指针
 * @param[in] arg i2s_transfer_async_arg 参数指针
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_transfer_async(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct i2s_transfer_async_arg* a = (const struct i2s_transfer_async_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !a || arg_len != sizeof(*a) || a->samples == 0)
        return VFS_ERR_INVAL;
    if (!a->tx && !a->rx)
        return VFS_ERR_INVAL;

    return i2s_bus_transfer_async(pdev, a->tx, a->rx, a->samples, a->cb, a->userdata);
}

/**
 * @brief I2S 命令: 等待异步传输完成
 * @param[in] pdev 设备对象指针
 * @param[in] arg 未使用
 * @param[in] arg_len 未使用
 * @param[in] timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_async_wait(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(arg_len);
    if (!pdev)
        return VFS_ERR_INVAL;
    return i2s_bus_transfer_poll(pdev, timeout_ms);
}

/**
 * @brief I2S 命令: 启动 DMA circular (TX/RX 由 arg 选择)
 * @param[in] pdev 设备对象指针
 * @param[in] arg i2s_circ_arg 参数指针
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_circ_start(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct i2s_circ_arg* a = (const struct i2s_circ_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return i2s_bus_dma_circ_start(pdev, (int)a->tx_enable, (int)a->rx_enable);
}

/**
 * @brief I2S 命令: 停止 DMA circular
 * @param[in] pdev 设备对象指针
 * @param[in] arg 未使用
 * @param[in] arg_len 未使用
 * @param[in] timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_circ_stop(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(arg_len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev)
        return VFS_ERR_INVAL;
    return i2s_bus_dma_circ_stop(pdev);
}

/**
 * @brief I2S 命令: 向 circular 环缓写入采样
 * @param[in] pdev 设备对象指针
 * @param[in] arg i2s_circ_buf_arg 参数指针 (含 data/samples)
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_circ_write(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct i2s_circ_buf_arg* a = (const struct i2s_circ_buf_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !a || arg_len != sizeof(*a) || !a->data || a->samples == 0)
        return VFS_ERR_INVAL;
    return i2s_bus_dma_circ_write(pdev, a->data, a->samples);
}

/**
 * @brief I2S 命令: 从 circular 环缓读取采样
 * @param[in] pdev 设备对象指针
 * @param[in] arg i2s_circ_buf_arg 参数指针 (含 data/samples)
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_circ_read(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct i2s_circ_buf_arg* a = (struct i2s_circ_buf_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !a || arg_len != sizeof(*a) || !a->data || a->samples == 0)
        return VFS_ERR_INVAL;
    return i2s_bus_dma_circ_read(pdev, a->data, a->samples);
}

/**
 * @brief I2S 命令: 设置 DMA HT/TC 中断模式
 * @param[in] pdev 设备对象指针
 * @param[in] arg i2s_dma_irq_mode_arg 参数指针
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_set_dma_irq_mode(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct i2s_dma_irq_mode_arg* a = (const struct i2s_dma_irq_mode_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !a || arg_len != sizeof(*a) || a->irq_mode > I2S_IRQ_HT_TC)
        return VFS_ERR_INVAL;
    return i2s_bus_set_dma_irq_mode(pdev, a->irq_mode);
}

/**
 * @brief I2S 命令: 查询 DMA HT/TC 中断模式
 * @param[in] pdev 设备对象指针
 * @param[in] arg i2s_dma_irq_mode_arg 输出参数指针
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_cmd_get_dma_irq_mode(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct i2s_dma_irq_mode_arg* a = (struct i2s_dma_irq_mode_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return i2s_bus_get_dma_irq_mode(pdev, &a->irq_mode);
}

static const struct i2s_ioctl_map s_i2s_ioctl_map[I2S_CMD_COUNT] = {
    [I2S_CMD_TRANSFER - I2S_CMD_BASE - 1] = {i2s_cmd_transfer},
    [I2S_CMD_SET_XFER_MODE - I2S_CMD_BASE - 1] = {i2s_cmd_set_xfer_mode},
    [I2S_CMD_GET_XFER_MODE - I2S_CMD_BASE - 1] = {i2s_cmd_get_xfer_mode},
    [I2S_CMD_TRANSFER_ASYNC - I2S_CMD_BASE - 1] = {i2s_cmd_transfer_async},
    [I2S_CMD_ASYNC_WAIT - I2S_CMD_BASE - 1] = {i2s_cmd_async_wait},
    [I2S_CMD_CIRC_START - I2S_CMD_BASE - 1] = {i2s_cmd_circ_start},
    [I2S_CMD_CIRC_STOP - I2S_CMD_BASE - 1] = {i2s_cmd_circ_stop},
    [I2S_CMD_CIRC_WRITE - I2S_CMD_BASE - 1] = {i2s_cmd_circ_write},
    [I2S_CMD_CIRC_READ - I2S_CMD_BASE - 1] = {i2s_cmd_circ_read},
    [I2S_CMD_SET_DMA_IRQ_MODE - I2S_CMD_BASE - 1] = {i2s_cmd_set_dma_irq_mode},
    [I2S_CMD_GET_DMA_IRQ_MODE - I2S_CMD_BASE - 1] = {i2s_cmd_get_dma_irq_mode},
};

/**
 * @brief I2S Client ioctl: 命令映射表 O(1) 派发 (对齐 SPI)
 * @param[in] pdev 设备对象指针
 * @param[in] cmd 控制命令
 * @param[in] arg 命令参数指针
 * @param[in] arg_len 参数长度
 * @param[in] to 超时 (毫秒, 部分命令透传)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t to)
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

    offset = (int32_t)cmd - (int32_t)I2S_CMD_BASE;
    if (offset < 1 || offset > I2S_CMD_COUNT || !s_i2s_ioctl_map[offset - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_i2s_ioctl_map[offset - 1].handler(pdev, arg, arg_len, to);

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations s_fops = {
    .open = i2s_open,
    .close = i2s_close,
    .read = i2s_read,
    .write = i2s_write,
    .ioctl = i2s_ioctl,
};

/**
 * @brief Client 探测: 申请池槽, 解析 DTSI, 注册 bus client, 绑定 fops 与 lifecycle
 * @param[in] pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int client_probe(struct device* pdev)
{
    struct vfs_i2s_client* priv;
    struct i2s_bus_client* bus_cli;
    int idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    idx = osal_pool_claim(&s_client_pool_ctrl);
    if (idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_client_pool[idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = idx;
    priv->xfer_mode = I2S_XFER_AUTO;

    ret = parse_client(pdev, &priv->cfg);
    if (ret != VFS_OK)
        goto err;

    ret = i2s_bus_client_register(pdev, &priv->cfg, &bus_cli);
    if (ret != VFS_OK)
        goto err;

    priv->ops = s_fops;
    pdev->ops = &priv->ops;
    device_lc_bind(pdev);

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        i2s_bus_client_unregister(pdev);
        ret = VFS_ERR_IO;
        goto err;
    }

    SYS_LOGI(k_cli, "probe OK %s", device_get_name(pdev));
    return VFS_OK;

err:
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, idx));
    return ret;
}

/**
 * @brief Client 移除: remove_start → 排空 IO → unregister → 归还私有池
 * @param[in] pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int client_remove(struct device* pdev)
{
    struct vfs_i2s_client* priv = device_get_priv(pdev);
    struct dev_lifecycle* lc;
    int idx;

    if (IS_ERR(priv))
        return PTR_ERR(priv);

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    idx = priv->pool_idx;
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    i2s_bus_client_unregister(pdev);
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(i2s_host_master, "i2s-master", host_probe_master, host_remove)
DRIVER_REGISTER(i2s_host_slave, "i2s-slave", host_probe_slave, host_remove)
DRIVER_REGISTER(i2s_vfs_master, "heterogeneous,i2s-master-client", client_probe, client_remove)
DRIVER_REGISTER(i2s_vfs_slave, "heterogeneous,i2s-slave-client", client_probe, client_remove)
