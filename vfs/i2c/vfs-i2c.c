/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-i2c.c
 *@brief vfs-i2c 实现
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   I2C VFS 实现 : Host + Client, master/slave 分 compatible
 *   DTS:
 *   i2c@n (i2c-master / i2c-slave)              ← host
 *   └── i2c-*-client (heterogeneous,i2c-*-client) ← client (fops)
 *   └── sensor@addr                           ← leaf driver
 *   @=========================================================================================================================
 */

#define I2C_VFS_IMPL
#include "vfs-i2c.h"

#include "board_define_i2c.h"
#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "i2c_bus.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

/*===========================================================================================================================================================*/
/*Host VFS*/
/*===========================================================================================================================================================*/
/* 池大小宏见 board_define_i2c.h (数量由 DTS 节点数自动生成) */

/** @brief I2C Host 私有数据 (静态池, 存 host 配置 + 池索引) */
struct vfs_i2c_priv
{
    struct hal_i2c_bus_config cfg; /**< host 总线配置 (DTSI 直投) */
    int pool_idx; /**< 池索引 */
};

static struct vfs_i2c_priv s_i2c_priv_pool[I2C_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_i2c_priv_used[I2C_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_i2c_priv_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_host_tag = "i2c_vfs_host";

/**
 * @brief I2C Host 私有数据池启动初始化
 */
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void vfs_i2c_priv_pool_init(void)
{
    COMPAT_IGNORE_RESULT(
        osal_pool_init(&s_i2c_priv_pool_ctrl, s_i2c_priv_used, I2C_VFS_PRIV_COUNT));
}

/**
 * @brief 解析 I2C Host DTS 属性, 填入 hal_i2c_bus_config
 * @param pdev 设备对象指针
 * @param cfg 配置结构指针
 * @param bus_role 总线角色 (MASTER/SLAVE)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_i2c_priv_parse_dts(struct device* pdev, struct hal_i2c_bus_config* cfg, int bus_role)
{
    int i2c_base = 0, i2c_clk = 0;
    int scl_port = 0, scl_pin = 0, scl_clk = 0, scl_af = 0;
    int sda_port = 0, sda_pin = 0, sda_clk = 0, sda_af = 0;
    int scl_output_type = 0, scl_speed = 0, scl_mode = 0, scl_pull = 0;
    int sda_output_type = 0, sda_speed = 0, sda_mode = 0, sda_pull = 0;

    if (device_get_prop_int(pdev, "i2c-base", &i2c_base) != VFS_OK ||
        device_get_prop_int(pdev, "i2c-clk", &i2c_clk) != VFS_OK ||
        device_get_prop_int(pdev, "scl-port", &scl_port) != VFS_OK ||
        device_get_prop_int(pdev, "scl-pin", &scl_pin) != VFS_OK ||
        device_get_prop_int(pdev, "scl-clk", &scl_clk) != VFS_OK ||
        device_get_prop_int(pdev, "scl-af", &scl_af) != VFS_OK ||
        device_get_prop_int(pdev, "sda-port", &sda_port) != VFS_OK ||
        device_get_prop_int(pdev, "sda-pin", &sda_pin) != VFS_OK ||
        device_get_prop_int(pdev, "sda-clk", &sda_clk) != VFS_OK ||
        device_get_prop_int(pdev, "sda-af", &sda_af) != VFS_OK)
        return VFS_ERR_INVAL;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "scl-output-type", &scl_output_type));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "scl-speed", &scl_speed));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "scl-mode", &scl_mode));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "scl-pull", &scl_pull));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sda-output-type", &sda_output_type));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sda-speed", &sda_speed));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sda-mode", &sda_mode));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sda-pull", &sda_pull));

    COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
    {
        int irqn = -1, irq_priority = 0, it_enable = 0, mode = 0;
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "irqn", &irqn));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "irq-priority", &irq_priority));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "it-enable", &it_enable));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "i2c-mode", &mode));
        cfg->irqn = (int32_t)irqn;
        cfg->irq_priority = (uint32_t)irq_priority;
        cfg->it_enable = (uint32_t)it_enable;
        cfg->mode = (uint32_t)mode;
    }

    cfg->i2c = (uintptr_t)i2c_base;
    cfg->i2c_clk_periph = (uint32_t)i2c_clk;
    cfg->bus_role = (uint32_t)bus_role;
    cfg->scl = (struct hal_i2c_pin_cfg){
        .port = (uintptr_t)scl_port,
        .pin = (uint16_t)scl_pin,
        .clk_bus = (uint32_t)scl_clk,
        .af = (uint32_t)scl_af,
        .output_type = (uint32_t)scl_output_type,
        .speed = (uint32_t)scl_speed,
        .mode = (uint32_t)scl_mode,
        .pull = (uint32_t)scl_pull,
    };
    cfg->sda = (struct hal_i2c_pin_cfg){
        .port = (uintptr_t)sda_port,
        .pin = (uint16_t)sda_pin,
        .clk_bus = (uint32_t)sda_clk,
        .af = (uint32_t)sda_af,
        .output_type = (uint32_t)sda_output_type,
        .speed = (uint32_t)sda_speed,
        .mode = (uint32_t)sda_mode,
        .pull = (uint32_t)sda_pull,
    };

    {
        int max_transfer_sz = 0;
        int dma_arr[14];
        int n;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "max-transfer-buffer", &max_transfer_sz));
        cfg->max_transfer_sz = (size_t)(max_transfer_sz > 0 ? max_transfer_sz : 0);

        n = device_get_prop_int_array(pdev, "dma-tx-cfg", dma_arr, 14);
        if (n >= 6)
        {
            cfg->dma_tx.dma_handle = (uintptr_t)dma_arr[0];
            cfg->dma_tx.dma_stream = (uint32_t)dma_arr[1];
            cfg->dma_tx.dma_channel = (uint32_t)dma_arr[2];
            cfg->dma_tx.dma_priority = (uint32_t)dma_arr[3];
            cfg->dma_tx.dma_memory_size = (uint32_t)dma_arr[4];
            cfg->dma_tx.dma_enable = (uint32_t)dma_arr[5];
        }
        n = device_get_prop_int_array(pdev, "dma-rx-cfg", dma_arr, 14);
        if (n >= 6)
        {
            cfg->dma_rx.dma_handle = (uintptr_t)dma_arr[0];
            cfg->dma_rx.dma_stream = (uint32_t)dma_arr[1];
            cfg->dma_rx.dma_channel = (uint32_t)dma_arr[2];
            cfg->dma_rx.dma_priority = (uint32_t)dma_arr[3];
            cfg->dma_rx.dma_memory_size = (uint32_t)dma_arr[4];
            cfg->dma_rx.dma_enable = (uint32_t)dma_arr[5];
        }
    }

    return VFS_OK;
}

/**
 * @brief I2C Host 探测公共实现: 分配私有池, 解析 DTS, 初始化总线
 * @param pdev 设备对象指针
 * @param bus_role 总线角色 (MASTER/SLAVE)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_i2c_priv_probe_impl(struct device* pdev, int bus_role)
{
    struct vfs_i2c_priv* priv;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_i2c_priv_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_i2c_priv_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    ret = vfs_i2c_priv_parse_dts(pdev, &priv->cfg, bus_role);
    if (ret != VFS_OK)
        goto err_pool;

    ret = i2c_bus_host_init(pdev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_pool;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_bus;
    }

    SYS_LOGI(k_host_tag, "probe OK: %s role=%s", device_get_name(pdev),
             bus_role == I2C_BUS_ROLE_MASTER ? "master" : "slave");
    return VFS_OK;

err_bus:
    COMPAT_IGNORE_RESULT(i2c_bus_host_deinit(pdev));
err_pool:
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_i2c_priv_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief I2C Master Host 驱动 probe 入口
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_i2c_priv_probe_master(struct device* pdev)
{
    return vfs_i2c_priv_probe_impl(pdev, I2C_BUS_ROLE_MASTER);
}

/**
 * @brief I2C Slave Host 驱动 probe 入口
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_i2c_priv_probe_slave(struct device* pdev)
{
    return vfs_i2c_priv_probe_impl(pdev, I2C_BUS_ROLE_SLAVE);
}

/**
 * @brief I2C Host 移除: remove_start → 排空 IO → host_deinit → 释放私有池
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_i2c_priv_remove(struct device* pdev)
{
    struct vfs_i2c_priv* priv;
    struct dev_lifecycle* lc;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    priv = (struct vfs_i2c_priv*)device_get_priv(pdev);
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

    ret = i2c_bus_host_deinit(pdev);
    if (ret != VFS_OK)
    {
        SYS_LOGE(k_host_tag, "host remove busy: %s (ret=%d)", device_get_name(pdev), ret);
        dev_lc_remove_finish(lc);
        return ret;
    }

    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_i2c_priv_pool_ctrl, pool_idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

/*===========================================================================================================================================================*/
/*Client VFS*/
/*===========================================================================================================================================================*/
/* client 池宏见 board_define_i2c.h */

/** @brief I2C Client 运行时对象 (静态池, 含 fops + 设备配置 + 传输模式) */
struct i2c_vfs_client
{
    struct file_operations ops; /**< VFS 操作表 */
    struct hal_i2c_device_config cfg; /**< 设备配置 (DTSI 直投) */
    int role; /**< 角色 (MASTER/SLAVE) */
    uint32_t xfer_mode; /**< I2C_XFER_*; write/read 默认 AUTO */
    int pool_idx; /**< 池索引 */
};

static struct i2c_vfs_client s_client_pool[I2C_VFS_CLIENT_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_client_used[I2C_VFS_CLIENT_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_client_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_client_tag = "i2c_vfs_client";

/**
 * @brief I2C Client 私有数据池启动初始化
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void i2c_vfs_client_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_client_pool_ctrl, s_client_used, I2C_VFS_CLIENT_COUNT));
}

/**
 * @brief I2C Client 打开: 引用计数, 首次打开时调用 i2c_bus_open
 * @param pdev 设备对象指针
 * @param arg 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2c_vfs_open(struct device* pdev, void* arg)
{
    struct dev_lifecycle* lc;
    int first;
    int ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = i2c_bus_open(pdev);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
    }
    if (ret == VFS_OK)
        dev_lc_open_end(lc);
    return ret;
}

/**
 * @brief I2C Client 关闭: 引用计数, 末次关闭时调用 i2c_bus_close
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2c_vfs_close(struct device* pdev)
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
        COMPAT_IGNORE_RESULT(i2c_bus_close(pdev));
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief I2C Client 写: Master 走 i2c_bus_write, Slave 走 i2c_bus_slave_sync (当前固定 NOTSUPP)
 * @param pdev 设备对象指针
 * @param buffer 待发送数据缓冲区
 * @param len 请求写入字节数 (len==0 返回 VFS_OK)
 * @param timeout_ms 超时 (毫秒)
 * @return Master 成功返回实际传输字节数; Slave 返回 VFS_ERR_NOTSUPP; 失败返回负数错误码
 */
static int i2c_vfs_write(struct device* pdev, const void* buffer, size_t len, uint32_t timeout_ms)
{
    struct i2c_vfs_client* priv;
    struct dev_lifecycle* lc;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct i2c_vfs_client, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    if (len == 0)
    {
        dev_lc_io_end(lc);
        return VFS_OK;
    }
    if (!buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    if (priv->role == I2C_BUS_ROLE_MASTER)
        ret = i2c_bus_write(pdev, (const uint8_t*)buffer, len, timeout_ms, priv->xfer_mode);
    else
        ret = i2c_bus_slave_sync(pdev, (const uint8_t*)buffer, NULL, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief I2C Client 读: Master 走 i2c_bus_read, Slave 走 i2c_bus_slave_sync (当前固定 NOTSUPP)
 * @param pdev 设备对象指针
 * @param buffer 接收数据缓冲区
 * @param len 请求读取字节数 (len==0 返回 VFS_OK)
 * @param timeout_ms 超时 (毫秒)
 * @return Master 成功返回实际传输字节数; Slave 返回 VFS_ERR_NOTSUPP; 失败返回负数错误码
 */
static int i2c_vfs_read(struct device* pdev, void* buffer, size_t len, uint32_t timeout_ms)
{
    struct i2c_vfs_client* priv;
    struct dev_lifecycle* lc;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct i2c_vfs_client, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    if (len == 0)
    {
        dev_lc_io_end(lc);
        return VFS_OK;
    }
    if (!buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    if (priv->role == I2C_BUS_ROLE_MASTER)
        ret = i2c_bus_read(pdev, (uint8_t*)buffer, len, timeout_ms, priv->xfer_mode);
    else
        ret = i2c_bus_slave_sync(pdev, NULL, (uint8_t*)buffer, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

typedef int (*i2c_ioctl_fn_t)(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms);

/** @brief I2C ioctl 派发表项 (函数指针包装) */
struct i2c_ioctl_map
{
    i2c_ioctl_fn_t handler; /**< ioctl 处理函数 */
};

/**
 * @brief I2C 命令: 全双工/半双工传输
 * @param pdev 设备对象指针
 * @param arg i2c_transfer_arg 参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回实际传输字节数, 失败返回负数错误码
 */
static int i2c_cmd_transfer(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct i2c_transfer_arg* ta = (const struct i2c_transfer_arg*)arg;
    struct i2c_vfs_client* priv;
    uint32_t mode;

    if (!pdev || !pdev->ops || !ta || arg_len != sizeof(*ta) || ta->len == 0)
        return VFS_ERR_INVAL;
    if (!ta->tx && !ta->rx)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct i2c_vfs_client, ops);
    mode = (ta->xfer_mode == I2C_XFER_AUTO) ? priv->xfer_mode : ta->xfer_mode;
    if (mode > I2C_XFER_DMA)
        return VFS_ERR_INVAL;

    return i2c_bus_transfer(pdev, ta->tx, ta->rx, ta->len, timeout_ms, mode);
}

/**
 * @brief I2C 命令: 设置默认传输模式 (POLL/DMA/AUTO)
 * @param pdev 设备对象指针
 * @param arg i2c_xfer_mode_arg 参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2c_cmd_set_xfer_mode(struct device* pdev, void* arg, size_t arg_len,
                                 uint32_t timeout_ms)
{
    const struct i2c_xfer_mode_arg* ma = (const struct i2c_xfer_mode_arg*)arg;
    struct i2c_vfs_client* priv;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops || !ma || arg_len != sizeof(*ma))
        return VFS_ERR_INVAL;
    if (ma->xfer_mode > I2C_XFER_DMA)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct i2c_vfs_client, ops);
    priv->xfer_mode = ma->xfer_mode;
    return VFS_OK;
}

/**
 * @brief I2C 命令: 读取当前默认传输模式
 * @param pdev 设备对象指针
 * @param arg i2c_xfer_mode_arg 输出参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2c_cmd_get_xfer_mode(struct device* pdev, void* arg, size_t arg_len,
                                 uint32_t timeout_ms)
{
    struct i2c_xfer_mode_arg* ma = (struct i2c_xfer_mode_arg*)arg;
    struct i2c_vfs_client* priv;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops || !ma || arg_len != sizeof(*ma))
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct i2c_vfs_client, ops);
    ma->xfer_mode = priv->xfer_mode;
    return VFS_OK;
}

/**
 * @brief I2C 命令: Slave 模式预入队发送 (STM32 路径当前固定返回 VFS_ERR_NOTSUPP)
 * @param pdev 设备对象指针
 * @param arg i2c_queue_arg 参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 未实现或失败返回负数错误码
 */
static int i2c_cmd_queue_tx(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct i2c_queue_arg* qa = (const struct i2c_queue_arg*)arg;
    if (!qa || arg_len != sizeof(*qa) || !qa->data || qa->len == 0)
        return VFS_ERR_INVAL;
    return i2c_bus_slave_queue_tx(pdev, qa->data, qa->len, timeout_ms);
}

/**
 * @brief I2C 命令: Slave 模式获取本次传输结果 (STM32 路径当前固定返回 VFS_ERR_NOTSUPP)
 * @param pdev 设备对象指针
 * @param arg i2c_trans_result_arg 参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 未实现或失败返回负数错误码
 */
static int i2c_cmd_get_trans_result(struct device* pdev, void* arg, size_t arg_len,
                                    uint32_t timeout_ms)
{
    const struct i2c_trans_result_arg* tra = (const struct i2c_trans_result_arg*)arg;
    if (!tra || arg_len != sizeof(*tra))
        return VFS_ERR_INVAL;
    return i2c_bus_slave_get_trans_result(pdev, tra->data, tra->len, tra->trans_len, timeout_ms);
}

static const struct i2c_ioctl_map s_i2c_ioctl_map[I2C_CMD_COUNT] = {
    [I2C_CMD_TRANSFER - I2C_CMD_BASE - 1] = {i2c_cmd_transfer},
    [I2C_CMD_QUEUE_TX - I2C_CMD_BASE - 1] = {i2c_cmd_queue_tx},
    [I2C_CMD_GET_TRANS_RESULT - I2C_CMD_BASE - 1] = {i2c_cmd_get_trans_result},
    [I2C_CMD_SET_XFER_MODE - I2C_CMD_BASE - 1] = {i2c_cmd_set_xfer_mode},
    [I2C_CMD_GET_XFER_MODE - I2C_CMD_BASE - 1] = {i2c_cmd_get_xfer_mode},
};

/**
 * @brief I2C Client ioctl 派发入口
 * @param pdev 设备对象指针
 * @param cmd 控制命令
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒, 部分命令透传)
 * @return 成功返回 VFS_OK 或实际传输字节数, 未知命令返回 VFS_ERR_INVAL, 失败返回负数错误码
 */
static int i2c_vfs_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len,
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

    offset = (int32_t)cmd - (int32_t)I2C_CMD_BASE;
    if (offset < 1 || offset > I2C_CMD_COUNT || !s_i2c_ioctl_map[offset - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_i2c_ioctl_map[offset - 1].handler(pdev, arg, arg_len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations i2c_vfs_fops = {
    .open = i2c_vfs_open,
    .close = i2c_vfs_close,
    .write = i2c_vfs_write,
    .read = i2c_vfs_read,
    .ioctl = i2c_vfs_ioctl,
};

/**
 * @brief 解析 I2C Client DTS 属性 (地址, 时钟频率等)
 * @param pdev 设备对象指针
 * @param cfg 输出的设备配置结构指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2c_vfs_parse_dts(struct device* pdev, struct hal_i2c_device_config* cfg)
{
    int clock_speed = 100000;
    int address = -1;
    int ack_enable = 1;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "clock-frequency", &clock_speed));
    if (device_get_prop_int(pdev, "reg", &address) != VFS_OK)
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "i2c-address", &address));
    if (address < 0)
        return VFS_ERR_INVAL;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "ack-enable", &ack_enable));

    COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
    cfg->clock_speed_hz = (uint32_t)(clock_speed > 0 ? clock_speed : 100000);
    cfg->address = (uint32_t)address;
    cfg->own_address = (uint32_t)address;
    cfg->ack_enable = (uint32_t)ack_enable;
    return VFS_OK;
}

/**
 * @brief I2C Client 探测: 注册 fops 并绑定总线客户端
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2c_vfs_probe(struct device* pdev)
{
    struct i2c_vfs_client* priv;
    struct i2c_bus_client* bus_cli;
    int role;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    role = i2c_bus_host_role(pdev);
    if (role != I2C_BUS_ROLE_MASTER && role != I2C_BUS_ROLE_SLAVE)
    {
        SYS_LOGE(k_client_tag, "invalid I2C role: %s", device_get_name(pdev));
        return VFS_ERR_INVAL;
    }

    pool_idx = osal_pool_claim(&s_client_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_client_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;
    priv->role = role;
    priv->xfer_mode = I2C_XFER_AUTO;

    ret = i2c_vfs_parse_dts(pdev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_pool;

    ret = i2c_bus_client_register(pdev, &priv->cfg, &bus_cli);
    if (ret != VFS_OK)
        goto err_pool;

    priv->ops = i2c_vfs_fops;
    pdev->ops = &priv->ops;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        i2c_bus_client_unregister(pdev);
        ret = VFS_ERR_IO;
        goto err_pool;
    }

    SYS_LOGI(k_client_tag, "probe OK: %s role=%s addr=0x%x freq=%u", device_get_name(pdev),
             role == I2C_BUS_ROLE_MASTER ? "master" : "slave", (unsigned)priv->cfg.address,
             (unsigned)priv->cfg.clock_speed_hz);
    return VFS_OK;

err_pool:
    pdev->ops = NULL;
    dev_lc_reset(device_lc(pdev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief I2C Client 移除: remove_start → 排空 IO → unregister → 释放私有池
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2c_vfs_remove(struct device* pdev)
{
    struct i2c_vfs_client* priv;
    struct dev_lifecycle* lc;
    int pool_idx;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct i2c_vfs_client, ops);
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

    i2c_bus_client_unregister(pdev);
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, pool_idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(i2c_host_master, "i2c-master", vfs_i2c_priv_probe_master, vfs_i2c_priv_remove)
DRIVER_REGISTER(i2c_host_slave, "i2c-slave", vfs_i2c_priv_probe_slave, vfs_i2c_priv_remove)
DRIVER_REGISTER(i2c_vfs_master, "heterogeneous,i2c-master-client", i2c_vfs_probe, i2c_vfs_remove)
DRIVER_REGISTER(i2c_vfs_slave, "heterogeneous,i2c-slave-client", i2c_vfs_probe, i2c_vfs_remove)
