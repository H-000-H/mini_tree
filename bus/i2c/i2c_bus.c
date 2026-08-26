/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file i2c_bus.c
 *@brief i2c bus 实现
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   I2C BUS 实现 — I2C 总线子系统 bus 层 (平台中立共享代码)
 *   静态池: s_i2c_hosts[HOST_MAX] (含 hal_host, ref_count) + s_i2c_clients[DEV_ID_COUNT]
 *   数据流:
 *   同步: VFS → i2c_bus_open/close/transfer|write|read(xfer_mode) → hal_i2c_*
 *   controller_ops 表注册到 bus_controller_bind_full; impl 实现逻辑, public 函数转发
 *   --------------------------------------------------------------------------
 */

#define I2C_BUS_IMPL
#include "i2c_bus.h"

#include "board_config.h"
#include "board_devtable.h"
#include "bus.h"
#include "compiler_compat.h"
#include "device.h"
#include "hal_i2c.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

/* host 池 = DTS "i2c-master" 节点数 (缺省 1, dtc-lite 生成 DTC_GEN_COUNT_I2C_MASTER) */
#ifndef DTC_GEN_COUNT_I2C_MASTER
#define DTC_GEN_COUNT_I2C_MASTER 1
#endif
#define I2C_BUS_HOST_MAX DTC_GEN_COUNT_I2C_MASTER

/** @brief I2C host 运行时描述符 (静态池, 含 HAL 嵌入 + atomic ref_count) */
struct i2c_bus_host
{
    struct device* pdev; /**< 关联设备 */
    struct hal_i2c_bus_host hal_host; /**< 嵌入 HAL host (非指针) */
    MINI_ATOMIC_INT ref_count; /**< atomic 引用计数 */
};

/** @brief I2C client 运行时描述符 (静态表, 按 device_id 索引) */
struct i2c_bus_client
{
    struct device* pdev; /**< 关联设备 */
    struct i2c_bus_host* host; /**< 所属 host */
    struct hal_i2c_device_config cfg; /**< 设备配置 (DTSI 直投) */
    struct hal_i2c_dev hal_dev; /**< HAL 设备对象 */
    int hw_open; /**< 硬件打开计数 */
};

static struct i2c_bus_host s_i2c_hosts[I2C_BUS_HOST_MAX];
static uint8_t s_i2c_host_used[I2C_BUS_HOST_MAX];
static osal_pool_t s_i2c_host_pool_ctrl;
static struct i2c_bus_client s_i2c_clients[DEV_ID_COUNT];
static const char* const k_tag = "i2c_bus";

/**
 * @brief I2C Host 池启动初始化
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_RES_POOL) static void i2c_bus_pool_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_i2c_host_pool_ctrl, s_i2c_host_used, I2C_BUS_HOST_MAX));
}
/* -------------------------------------------------------------------------- */
/* Host pool helpers */
/* -------------------------------------------------------------------------- */
/**
 * @brief 通过 device 指针查找对应的 i2c_bus_host
 * @param[in] pdev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct i2c_bus_host* i2c_host_from_device(struct device* pdev)
{
    for (int index = 0; index < I2C_BUS_HOST_MAX; index++)
        if (osal_pool_is_used(&s_i2c_host_pool_ctrl, index) && s_i2c_hosts[index].pdev == pdev)
            return &s_i2c_hosts[index];
    return NULL;
}

/**
 * @brief 通过 device 指针查找对应的 i2c_bus_client
 * @param[in] pdev client device 指针
 * @return 找到返回 client 指针, 未找到返回 NULL
 */
static struct i2c_bus_client* i2c_client_from_device(struct device* pdev)
{
    int id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT || !s_i2c_clients[id].pdev)
        return NULL;
    return &s_i2c_clients[id];
}

/* -------------------------------------------------------------------------- */
/* controller_ops (host 级操作) */
/* -------------------------------------------------------------------------- */
/* 前向声明: s_i2c_controller_ops 引用 impl 函数, 但 impl 定义在 ops 表之后 */
static int i2c_host_init_impl(struct device* pdev, const void* cfg);
static int i2c_host_deinit_impl(struct device* pdev);
static int i2c_host_role_impl(struct device* pdev);
static int i2c_client_register_impl(struct device* pdev, const void* cfg, void** out);
static void i2c_client_unregister_impl(struct device* pdev);

/**
 * @brief I2C 总线控制器操作表
 * @note 控制器操作表注册到 bus_controller_bind_full; impl 实现逻辑, public 函数转发
 */
static const struct bus_controller_ops s_i2c_controller_ops = {
    .init = i2c_host_init_impl,
    .deinit = i2c_host_deinit_impl,
    .role = i2c_host_role_impl,
    .client_register = i2c_client_register_impl,
    .client_unregister = i2c_client_unregister_impl,
};

/**
 * @brief I2C 总线主机初始化实现
 * @param[in] pdev host device 指针
 * @param[in] cfg host 配置指针
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int i2c_host_init_impl(struct device* pdev, const void* cfg)
{
    const struct hal_i2c_bus_config* host_cfg;
    struct i2c_bus_host* host;
    int idx;
    int ret;

    if (!pdev || !cfg)
        return MINI_ERR_INVAL;

    host_cfg = (const struct hal_i2c_bus_config*)cfg;

    if (i2c_host_from_device(pdev))
        return MINI_OK;

    idx = osal_pool_claim(&s_i2c_host_pool_ctrl);
    if (idx < 0)
        return MINI_ERR_NOMEM;

    host = &s_i2c_hosts[idx];

    MINI_MEM_SET(host, 0, sizeof(*host));

    host->pdev = pdev;

    MINI_ATOMIC_RUNTIME_INIT(&host->ref_count, 0);

    ret = hal_i2c_bus_host_init(&host->hal_host, idx, host_cfg);
    if (ret != MINI_OK)
    {
        MINI_MEM_SET(host, 0, sizeof(*host));
        MINI_IGNORE_RESULT(osal_pool_release(&s_i2c_host_pool_ctrl, idx));
        return ret;
    }

    ret = bus_controller_bind_full(pdev, BUS_TYPE_I2C, &s_i2c_controller_ops, host);
    if (ret != MINI_OK)
    {
        MINI_IGNORE_RESULT(hal_i2c_bus_host_deinit(&host->hal_host));
        MINI_MEM_SET(host, 0, sizeof(*host));
        MINI_IGNORE_RESULT(osal_pool_release(&s_i2c_host_pool_ctrl, idx));
        return ret;
    }

    return MINI_OK;
}

int i2c_bus_host_init(struct device* pdev, const struct hal_i2c_bus_config* cfg)
{
    return i2c_host_init_impl(pdev, cfg);
}

/**
 * @brief I2C 总线主机销毁实现
 * @param[in] pdev host device 指针
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int i2c_host_deinit_impl(struct device* pdev)
{
    struct i2c_bus_host* host;
    int idx;
    int ret;

    if (!pdev)
        return MINI_ERR_INVAL;

    host = i2c_host_from_device(pdev);
    if (!host)
        return MINI_ERR_NODEV;

    if (MINI_ATOMIC_LOAD(&host->ref_count, MINI_SEQ_CST) != 0)
    {
        SYS_LOGW(k_tag, "host deinit busy: ref_count=%d",
                 MINI_ATOMIC_LOAD(&host->ref_count, MINI_SEQ_CST));
        return MINI_ERR_BUSY;
    }

    idx = (int)(host - s_i2c_hosts);
    bus_controller_unbind(pdev);

    ret = hal_i2c_bus_host_deinit(&host->hal_host);
    if (ret == MINI_OK)
    {
        MINI_MEM_SET(host, 0, sizeof(*host));
        MINI_IGNORE_RESULT(osal_pool_release(&s_i2c_host_pool_ctrl, idx));
    }
    return ret;
}

int i2c_bus_host_deinit(struct device* pdev) { return i2c_host_deinit_impl(pdev); }

/**
 * @brief 查询 host 角色 (支持传 host 或 client device)
 * @param[in] pdev host 或 client device 指针
 * @return I2C_BUS_ROLE_MASTER 或 I2C_BUS_ROLE_SLAVE, 失败返回 -1
 */
static int i2c_host_role_impl(struct device* pdev)
{
    struct bus_controller* ctlr = NULL;
    struct i2c_bus_host* host;

    if (!pdev)
        return -1;

    if (bus_controller_get(pdev, &ctlr) != MINI_OK)
    {
        if (bus_controller_of(pdev, &ctlr) != MINI_OK)
            return -1;
    }

    if (!ctlr || ctlr->type != BUS_TYPE_I2C)
        return -1;

    host = (struct i2c_bus_host*)ctlr->hw_ctx;
    if (!host)
        return -1;

    return host->hal_host.cfg.bus_role == HAL_I2C_BUS_ROLE_MASTER ? I2C_BUS_ROLE_MASTER :
                                                                    I2C_BUS_ROLE_SLAVE;
}

int i2c_bus_host_role(struct device* pdev) { return i2c_host_role_impl(pdev); }

/**
 * @brief I2C 总线客户端注册实现
 * @param[in] pdev client device 指针
 * @param[in] cfg client 配置指针
 * @param[out] out 输出 client 指针
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int i2c_client_register_impl(struct device* pdev, const void* cfg, void** out)
{
    const struct hal_i2c_device_config* client_cfg;
    struct bus_controller* ctlr;
    struct i2c_bus_host* host;
    struct i2c_bus_client* client;
    int id;

    if (!pdev || !cfg || !out)
        return MINI_ERR_INVAL;

    client_cfg = (const struct hal_i2c_device_config*)cfg;

    if (bus_controller_of(pdev, &ctlr) != MINI_OK)
        return MINI_ERR_NODEV;

    host = (struct i2c_bus_host*)ctlr->hw_ctx;
    if (!host)
        return MINI_ERR_IO;

    id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT)
        return MINI_ERR_INVAL;

    client = &s_i2c_clients[id];

    if (client->pdev)
    {
        if (client->pdev != pdev)
            return MINI_ERR_BUSY;
        *out = client;
        return MINI_OK;
    }

    MINI_MEM_SET(client, 0, sizeof(*client));
    client->pdev = pdev;
    client->host = host;
    client->cfg = *client_cfg;

    (void)MINI_ATOMIC_FETCH_ADD(&host->ref_count, 1, MINI_SEQ_CST);

    *out = client;
    return MINI_OK;
}

int i2c_bus_client_register(struct device* pdev, const struct hal_i2c_device_config* cfg,
                            struct i2c_bus_client** out)
{
    return i2c_client_register_impl(pdev, cfg, (void**)out);
}

/**
 * @brief I2C 总线客户端销毁实现 (关 hw / 减 host 引用 / 清槽)
 * @param[in] pdev client device 指针
 */
static void i2c_client_unregister_impl(struct device* pdev)
{
    struct i2c_bus_client* client;
    struct i2c_bus_host* host;

    client = i2c_client_from_device(pdev);
    if (!client)
        return;

    /* 若 client 仍 hw_open, 先 close 以释放 HAL 层 ref_count 与 HAL 句柄 */
    if (client->hw_open)
    {
        MINI_IGNORE_RESULT(i2c_bus_close(pdev));
        client->hw_open = 0;
    }

    host = client->host;
    if (host)
        (void)MINI_ATOMIC_FETCH_SUB(&host->ref_count, 1, MINI_SEQ_CST);

    MINI_MEM_SET(client, 0, sizeof(*client));
}

void i2c_bus_client_unregister(struct device* pdev) { i2c_client_unregister_impl(pdev); }

int i2c_bus_open(struct device* pdev)
{
    struct i2c_bus_client* client;
    int ret;

    client = i2c_client_from_device(pdev);
    if (!client)
        return MINI_ERR_NODEV;

    if (client->hw_open)
        return MINI_OK;

    MINI_IGNORE_RESULT(hal_i2c_dev_init(&client->hal_dev, &client->host->hal_host, &client->cfg));
    ret = hal_i2c_dev_hw_open(&client->hal_dev);
    if (ret != MINI_OK)
        return ret;

    client->hw_open = 1;
    return MINI_OK;
}

int i2c_bus_close(struct device* pdev)
{
    struct i2c_bus_client* client;

    client = i2c_client_from_device(pdev);
    if (!client)
        return MINI_ERR_NODEV;

    if (client->hw_open)
    {
        MINI_IGNORE_RESULT(hal_i2c_dev_hw_close(&client->hal_dev));
        client->hw_open = 0;
    }
    return MINI_OK;
}

/**
 * @brief master 写: 按 xfer_mode 选 poll / DMA / AUTO
 * @param[in] client I2C client 指针
 * @param[in] tx 发送缓冲区
 * @param[in] len 字节数
 * @param[in] timeout_ms 超时 (ms)
 * @param[in] xfer_mode 传输模式 (POLL / DMA / AUTO)
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int i2c_master_write_mode(struct i2c_bus_client* client, const uint8_t* tx, size_t len,
                                 uint32_t timeout_ms, uint32_t xfer_mode)
{
    if (xfer_mode > HAL_I2C_XFER_DMA)
        return MINI_ERR_INVAL;

    if (xfer_mode == HAL_I2C_XFER_POLL)
        return hal_i2c_write(&client->hal_dev, tx, len, timeout_ms);

    if (xfer_mode == HAL_I2C_XFER_DMA)
        return hal_i2c_dma_write(&client->hal_dev, tx, len, timeout_ms);

    /* AUTO: DMA 可用则优先, 否则 poll */
    if (client->host->hal_host.cfg.dma_tx.dma_enable)
    {
        int ret = hal_i2c_dma_write(&client->hal_dev, tx, len, timeout_ms);
        if (ret != MINI_ERR_NOTSUPP)
            return ret;
    }
    return hal_i2c_write(&client->hal_dev, tx, len, timeout_ms);
}

/**
 * @brief master 读: 按 xfer_mode 选 poll / DMA / AUTO
 * @param[in] client I2C client 指针
 * @param[out] rx 接收缓冲区
 * @param[in] len 字节数
 * @param[in] timeout_ms 超时 (ms)
 * @param[in] xfer_mode 传输模式 (POLL / DMA / AUTO)
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int i2c_master_read_mode(struct i2c_bus_client* client, uint8_t* rx, size_t len,
                                uint32_t timeout_ms, uint32_t xfer_mode)
{
    if (xfer_mode > HAL_I2C_XFER_DMA)
        return MINI_ERR_INVAL;

    if (xfer_mode == HAL_I2C_XFER_POLL)
        return hal_i2c_read(&client->hal_dev, rx, len, timeout_ms);

    if (xfer_mode == HAL_I2C_XFER_DMA)
        return hal_i2c_dma_read(&client->hal_dev, rx, len, timeout_ms);

    if (client->host->hal_host.cfg.dma_rx.dma_enable)
    {
        int ret = hal_i2c_dma_read(&client->hal_dev, rx, len, timeout_ms);
        if (ret != MINI_ERR_NOTSUPP)
            return ret;
    }
    return hal_i2c_read(&client->hal_dev, rx, len, timeout_ms);
}

int i2c_bus_transfer(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                     uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct i2c_bus_client* client;
    int role;

    if (!pdev || len == 0 || (!tx && !rx))
        return MINI_ERR_INVAL;

    client = i2c_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    role = i2c_bus_host_role(pdev);
    if (role == I2C_BUS_ROLE_SLAVE)
        return i2c_bus_slave_sync(pdev, tx, rx, len, timeout_ms);
    if (role != I2C_BUS_ROLE_MASTER)
        return MINI_ERR_NODEV;

    if (tx && rx)
    {
        /* 先写后读: Repeated START (中间无 STOP) */
        if (xfer_mode == HAL_I2C_XFER_POLL)
            return hal_i2c_sync(&client->hal_dev, tx, rx, len, timeout_ms);
        if (xfer_mode == HAL_I2C_XFER_DMA)
            return hal_i2c_dma_write_then_read(&client->hal_dev, tx, rx, len, timeout_ms);
        /* AUTO: DMA 可用则走 DMA 组合, 否则 poll */
        if (client->host->hal_host.cfg.dma_tx.dma_enable &&
            (len == 1U || client->host->hal_host.cfg.dma_rx.dma_enable))
        {
            int ret = hal_i2c_dma_write_then_read(&client->hal_dev, tx, rx, len, timeout_ms);
            if (ret != MINI_ERR_NOTSUPP)
                return ret;
        }
        return hal_i2c_sync(&client->hal_dev, tx, rx, len, timeout_ms);
    }
    if (tx)
        return i2c_master_write_mode(client, tx, len, timeout_ms, xfer_mode);
    return i2c_master_read_mode(client, rx, len, timeout_ms, xfer_mode);
}

int i2c_bus_write(struct device* pdev, const uint8_t* tx, size_t len, uint32_t timeout_ms,
                  uint32_t xfer_mode)
{
    struct i2c_bus_client* client;

    if (!pdev || !tx || len == 0)
        return MINI_ERR_INVAL;

    client = i2c_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    if (i2c_bus_host_role(pdev) != I2C_BUS_ROLE_MASTER)
        return MINI_ERR_NOTSUPP;

    return i2c_master_write_mode(client, tx, len, timeout_ms, xfer_mode);
}

int i2c_bus_read(struct device* pdev, uint8_t* rx, size_t len, uint32_t timeout_ms,
                 uint32_t xfer_mode)
{
    struct i2c_bus_client* client;

    if (!pdev || !rx || len == 0)
        return MINI_ERR_INVAL;

    client = i2c_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    if (i2c_bus_host_role(pdev) != I2C_BUS_ROLE_MASTER)
        return MINI_ERR_NOTSUPP;

    return i2c_master_read_mode(client, rx, len, timeout_ms, xfer_mode);
}

/* -------------------------------------------------------------------------- */
/* Slave API — 故意空壳: STM32 路径固定返回 NOTSUPP */
/* -------------------------------------------------------------------------- */
int i2c_bus_slave_sync(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                       uint32_t timeout_ms)
{
    MINI_IGNORE_RESULT(pdev);
    MINI_IGNORE_RESULT(tx);
    MINI_IGNORE_RESULT(rx);
    MINI_IGNORE_RESULT(len);
    MINI_IGNORE_RESULT(timeout_ms);
    return MINI_ERR_NOTSUPP;
}

int i2c_bus_slave_queue_tx(struct device* pdev, const uint8_t* data, size_t len,
                           uint32_t timeout_ms)
{
    MINI_IGNORE_RESULT(pdev);
    MINI_IGNORE_RESULT(data);
    MINI_IGNORE_RESULT(len);
    MINI_IGNORE_RESULT(timeout_ms);
    return MINI_ERR_NOTSUPP;
}

int i2c_bus_slave_get_trans_result(struct device* pdev, uint8_t* rx_data, size_t rx_cap,
                                   size_t* trans_len, uint32_t timeout_ms)
{
    MINI_IGNORE_RESULT(pdev);
    MINI_IGNORE_RESULT(rx_data);
    MINI_IGNORE_RESULT(rx_cap);
    MINI_IGNORE_RESULT(trans_len);
    MINI_IGNORE_RESULT(timeout_ms);
    return MINI_ERR_NOTSUPP;
}
