/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * I2C BUS 实现 — I2C 总线子系统 bus 层 (平台中立共享代码)
 *
 * 静态池: s_i2c_hosts[HOST_MAX] (含 hal_host, ref_count) + s_i2c_clients[DEV_ID_COUNT]
 *
 * 数据流:
 *   同步: VFS → i2c_bus_open/close/transfer|write|read(xfer_mode) → hal_i2c_*
 *
 * controller_ops 表注册到 bus_controller_bind_full; impl 实现逻辑, public 函数转发
 *@=========================================================================================================================*/

#define I2C_BUS_IMPL
#include "i2c_bus.h"

#include "board_devtable.h"
#include "bus.h"
#include "compiler_compat.h"
#include "device.h"
#include "hal_i2c.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

#define I2C_BUS_HOST_MAX 4 /* 对齐 HAL/DTS host-max (I2C1/2/3) */

/** @brief I2C host 运行时描述符 (静态池, 含 HAL 嵌入 + atomic ref_count) */
struct i2c_bus_host
{
    struct device* pdev; /**< 关联设备 */
    struct hal_i2c_bus_host hal_host; /**< 嵌入 HAL host (非指针) */
    COMPAT_ATOMIC_INT ref_count; /**< atomic 引用计数 */
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
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void i2c_bus_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_i2c_host_pool_ctrl, s_i2c_host_used, I2C_BUS_HOST_MAX));
}
/*===========================================================================================================================================================*/
/* Host pool helpers */
/*===========================================================================================================================================================*/
/**
 * @brief 通过 device 指针查找对应的 i2c_bus_host
 * @param pdev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct i2c_bus_host* i2c_host_from_device(struct device* pdev)
{
    for (int i = 0; i < I2C_BUS_HOST_MAX; i++)
        if (osal_pool_is_used(&s_i2c_host_pool_ctrl, i) && s_i2c_hosts[i].pdev == pdev)
            return &s_i2c_hosts[i];
    return NULL;
}

/**
 * @brief 通过 device 指针查找对应的 i2c_bus_client
 * @param pdev client device 指针
 * @return 找到返回 client 指针, 未找到返回 NULL
 */
static struct i2c_bus_client* i2c_client_from_device(struct device* pdev)
{
    int id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT || !s_i2c_clients[id].pdev)
        return NULL;
    return &s_i2c_clients[id];
}

/*===========================================================================================================================================================*/
/* controller_ops (host 级操作) */
/*===========================================================================================================================================================*/
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
 * @param pdev host device 指针
 * @param cfg host 配置指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
static int i2c_host_init_impl(struct device* pdev, const void* cfg)
{
    const struct hal_i2c_bus_config* host_cfg;
    struct i2c_bus_host* host;
    int idx;
    int ret;

    if (!pdev || !cfg)
        return VFS_ERR_INVAL;

    host_cfg = (const struct hal_i2c_bus_config*)cfg;

    if (i2c_host_from_device(pdev))
        return VFS_OK;

    idx = osal_pool_claim(&s_i2c_host_pool_ctrl);
    if (idx < 0)
        return VFS_ERR_NOMEM;

    host = &s_i2c_hosts[idx];

    COMPAT_MEM_SET(host, 0, sizeof(*host));

    host->pdev = pdev;

    COMPAT_ATOMIC_RUNTIME_INIT(&host->ref_count, 0);

    ret = hal_i2c_bus_host_init(&host->hal_host, idx, host_cfg);
    if (ret != VFS_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_i2c_host_pool_ctrl, idx));
        return ret;
    }

    ret = bus_controller_bind_full(pdev, BUS_TYPE_I2C, &s_i2c_controller_ops, host);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(hal_i2c_bus_host_deinit(&host->hal_host));
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_i2c_host_pool_ctrl, idx));
        return ret;
    }

    return VFS_OK;
}

/**
 * @brief 初始化 I2C host 并绑定总线控制器
 * @param pdev host device 指针
 * @param cfg host 配置 (struct hal_i2c_bus_config*)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int i2c_bus_host_init(struct device* pdev, const struct hal_i2c_bus_config* cfg)
{
    return i2c_host_init_impl(pdev, cfg);
}

/**
 * @brief I2C 总线主机销毁实现
 * @param pdev host device 指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
static int i2c_host_deinit_impl(struct device* pdev)
{
    struct i2c_bus_host* host;
    int idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    host = i2c_host_from_device(pdev);
    if (!host)
        return VFS_ERR_NODEV;

    if (COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST) != 0)
    {
        SYS_LOGW(k_tag, "host deinit busy: ref_count=%d",
                 COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST));
        return VFS_ERR_BUSY;
    }

    idx = (int)(host - s_i2c_hosts);
    bus_controller_unbind(pdev);

    ret = hal_i2c_bus_host_deinit(&host->hal_host);
    if (ret == VFS_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_i2c_host_pool_ctrl, idx));
    }
    return ret;
}

/**
 * @brief 反初始化 I2C host 并释放对象池槽位
 * @param pdev host device 指针
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回负数错误码
 */
int i2c_bus_host_deinit(struct device* pdev) { return i2c_host_deinit_impl(pdev); }

/**
 * @brief 查询 host 角色 (支持传 host 或 client device)
 * @param pdev host 或 client device 指针
 * @return I2C_BUS_ROLE_MASTER 或 I2C_BUS_ROLE_SLAVE, 失败返回 -1
 */
static int i2c_host_role_impl(struct device* pdev)
{
    struct bus_controller* ctlr = NULL;
    struct i2c_bus_host* host;

    if (!pdev)
        return -1;

    if (bus_controller_get(pdev, &ctlr) != VFS_OK)
    {
        if (bus_controller_of(pdev, &ctlr) != VFS_OK)
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

/**
 * @brief 查询 I2C host 总线角色 (master/slave)
 * @param pdev host 或 client device 指针
 * @return I2C_BUS_ROLE_MASTER 或 I2C_BUS_ROLE_SLAVE, 失败返回 -1
 */
int i2c_bus_host_role(struct device* pdev) { return i2c_host_role_impl(pdev); }

/**
 * @brief I2C 总线客户端注册实现
 * @param pdev client device 指针
 * @param cfg client 配置指针
 * @param out 输出 client 指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
static int i2c_client_register_impl(struct device* pdev, const void* cfg, void** out)
{
    const struct hal_i2c_device_config* client_cfg;
    struct bus_controller* ctlr;
    struct i2c_bus_host* host;
    struct i2c_bus_client* client;
    int id;

    if (!pdev || !cfg || !out)
        return VFS_ERR_INVAL;

    client_cfg = (const struct hal_i2c_device_config*)cfg;

    if (bus_controller_of(pdev, &ctlr) != VFS_OK)
        return VFS_ERR_NODEV;

    host = (struct i2c_bus_host*)ctlr->hw_ctx;
    if (!host)
        return VFS_ERR_IO;

    id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT)
        return VFS_ERR_INVAL;

    client = &s_i2c_clients[id];

    if (client->pdev)
    {
        if (client->pdev != pdev)
            return VFS_ERR_BUSY;
        *out = client;
        return VFS_OK;
    }

    COMPAT_MEM_SET(client, 0, sizeof(*client));
    client->pdev = pdev;
    client->host = host;
    client->cfg = *client_cfg;

    (void)COMPAT_ATOMIC_FETCH_ADD(&host->ref_count, 1, COMPAT_MO_SEQ_CST);

    *out = client;
    return VFS_OK;
}

/**
 * @brief 注册 I2C client 并增加 host 引用计数
 * @param pdev client device 指针
 * @param cfg client 配置 (struct hal_i2c_device_config*)
 * @param out 输出 client 私有上下文指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int i2c_bus_client_register(struct device* pdev, const struct hal_i2c_device_config* cfg,
                            struct i2c_bus_client** out)
{
    return i2c_client_register_impl(pdev, cfg, (void**)out);
}

/**
 * @brief I2C 总线客户端销毁实现 (关 hw / 减 host 引用 / 清槽)
 * @param pdev client device 指针
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
        COMPAT_IGNORE_RESULT(i2c_bus_close(pdev));
        client->hw_open = 0;
    }

    host = client->host;
    if (host)
        (void)COMPAT_ATOMIC_FETCH_SUB(&host->ref_count, 1, COMPAT_MO_SEQ_CST);

    COMPAT_MEM_SET(client, 0, sizeof(*client));
}

/**
 * @brief 注销 I2C client (公开包装, vfs 层调用)
 * @param pdev client device 指针
 */
void i2c_bus_client_unregister(struct device* pdev)
{
    i2c_client_unregister_impl(pdev);
}

/**
 * @brief 打开 I2C client 硬件 (HAL init + hw_open)
 * @param pdev client device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int i2c_bus_open(struct device* pdev)
{
    struct i2c_bus_client* client;
    int ret;

    client = i2c_client_from_device(pdev);
    if (!client)
        return VFS_ERR_NODEV;

    if (client->hw_open)
        return VFS_OK;

    COMPAT_IGNORE_RESULT(hal_i2c_dev_init(&client->hal_dev, &client->host->hal_host, &client->cfg));
    ret = hal_i2c_dev_hw_open(&client->hal_dev);
    if (ret != VFS_OK)
        return ret;

    client->hw_open = 1;
    return VFS_OK;
}

/**
 * @brief 关闭 I2C client 硬件
 * @param pdev client device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int i2c_bus_close(struct device* pdev)
{
    struct i2c_bus_client* client;

    client = i2c_client_from_device(pdev);
    if (!client)
        return VFS_ERR_NODEV;

    if (client->hw_open)
    {
        COMPAT_IGNORE_RESULT(hal_i2c_dev_hw_close(&client->hal_dev));
        client->hw_open = 0;
    }
    return VFS_OK;
}

/**
 * @brief master 写: 按 xfer_mode 选 poll / DMA / AUTO
 * @param client I2C client 指针
 * @param tx 发送缓冲区
 * @param len 字节数
 * @param timeout_ms 超时 (ms)
 * @param xfer_mode 传输模式 (POLL / DMA / AUTO)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
static int i2c_master_write_mode(struct i2c_bus_client* client, const uint8_t* tx, size_t len,
                                 uint32_t timeout_ms, uint32_t xfer_mode)
{
    if (xfer_mode > HAL_I2C_XFER_DMA)
        return VFS_ERR_INVAL;

    if (xfer_mode == HAL_I2C_XFER_POLL)
        return hal_i2c_write(&client->hal_dev, tx, len, timeout_ms);

    if (xfer_mode == HAL_I2C_XFER_DMA)
        return hal_i2c_dma_write(&client->hal_dev, tx, len, timeout_ms);

    /* AUTO: DMA 可用则优先, 否则 poll */
    if (client->host->hal_host.cfg.dma_tx.dma_enable)
    {
        int ret = hal_i2c_dma_write(&client->hal_dev, tx, len, timeout_ms);
        if (ret != VFS_ERR_NOTSUPP)
            return ret;
    }
    return hal_i2c_write(&client->hal_dev, tx, len, timeout_ms);
}

/**
 * @brief master 读: 按 xfer_mode 选 poll / DMA / AUTO
 * @param client I2C client 指针
 * @param rx 接收缓冲区
 * @param len 字节数
 * @param timeout_ms 超时 (ms)
 * @param xfer_mode 传输模式 (POLL / DMA / AUTO)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
static int i2c_master_read_mode(struct i2c_bus_client* client, uint8_t* rx, size_t len,
                                uint32_t timeout_ms, uint32_t xfer_mode)
{
    if (xfer_mode > HAL_I2C_XFER_DMA)
        return VFS_ERR_INVAL;

    if (xfer_mode == HAL_I2C_XFER_POLL)
        return hal_i2c_read(&client->hal_dev, rx, len, timeout_ms);

    if (xfer_mode == HAL_I2C_XFER_DMA)
        return hal_i2c_dma_read(&client->hal_dev, rx, len, timeout_ms);

    if (client->host->hal_host.cfg.dma_rx.dma_enable)
    {
        int ret = hal_i2c_dma_read(&client->hal_dev, rx, len, timeout_ms);
        if (ret != VFS_ERR_NOTSUPP)
            return ret;
    }
    return hal_i2c_read(&client->hal_dev, rx, len, timeout_ms);
}

/**
 * @brief I2C 传输 (master 写/读/写后读, slave 走 slave_sync)
 * @param pdev client device 指针
 * @param tx 发送缓冲 (可为 NULL)
 * @param rx 接收缓冲 (可为 NULL)
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @param xfer_mode 传输模式 (POLL/DMA/AUTO)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int i2c_bus_transfer(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                     uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct i2c_bus_client* client;
    int role;

    if (!pdev || len == 0 || (!tx && !rx))
        return VFS_ERR_INVAL;

    client = i2c_client_from_device(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    role = i2c_bus_host_role(pdev);
    if (role == I2C_BUS_ROLE_SLAVE)
        return i2c_bus_slave_sync(pdev, tx, rx, len, timeout_ms);
    if (role != I2C_BUS_ROLE_MASTER)
        return VFS_ERR_NODEV;

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
            if (ret != VFS_ERR_NOTSUPP)
                return ret;
        }
        return hal_i2c_sync(&client->hal_dev, tx, rx, len, timeout_ms);
    }
    if (tx)
        return i2c_master_write_mode(client, tx, len, timeout_ms, xfer_mode);
    return i2c_master_read_mode(client, rx, len, timeout_ms, xfer_mode);
}

/**
 * @brief I2C master 写
 * @param pdev client device 指针
 * @param tx 发送缓冲
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @param xfer_mode 传输模式 (POLL/DMA/AUTO)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int i2c_bus_write(struct device* pdev, const uint8_t* tx, size_t len, uint32_t timeout_ms,
                  uint32_t xfer_mode)
{
    struct i2c_bus_client* client;

    if (!pdev || !tx || len == 0)
        return VFS_ERR_INVAL;

    client = i2c_client_from_device(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (i2c_bus_host_role(pdev) != I2C_BUS_ROLE_MASTER)
        return VFS_ERR_NOTSUPP;

    return i2c_master_write_mode(client, tx, len, timeout_ms, xfer_mode);
}

/**
 * @brief I2C master 读
 * @param pdev client device 指针
 * @param rx 接收缓冲
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @param xfer_mode 传输模式 (POLL/DMA/AUTO)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int i2c_bus_read(struct device* pdev, uint8_t* rx, size_t len, uint32_t timeout_ms,
                 uint32_t xfer_mode)
{
    struct i2c_bus_client* client;

    if (!pdev || !rx || len == 0)
        return VFS_ERR_INVAL;

    client = i2c_client_from_device(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (i2c_bus_host_role(pdev) != I2C_BUS_ROLE_MASTER)
        return VFS_ERR_NOTSUPP;

    return i2c_master_read_mode(client, rx, len, timeout_ms, xfer_mode);
}

/*===========================================================================================================================================================*/
/* Slave API — 故意空壳: STM32 路径固定返回 NOTSUPP */
/*===========================================================================================================================================================*/
/**
 * @brief I2C slave 同步传输 (当前平台未实现)
 * @param pdev client device 指针
 * @param tx 发送缓冲 (可为 NULL)
 * @param rx 接收缓冲 (可为 NULL)
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 固定返回 VFS_ERR_NOTSUPP
 */
int i2c_bus_slave_sync(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                       uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(tx);
    COMPAT_IGNORE_RESULT(rx);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

/**
 * @brief I2C slave 排队发送 (当前平台未实现)
 * @param pdev client device 指针
 * @param data 发送缓冲
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 固定返回 VFS_ERR_NOTSUPP
 */
int i2c_bus_slave_queue_tx(struct device* pdev, const uint8_t* data, size_t len,
                           uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(data);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

/**
 * @brief 获取 I2C slave 传输结果 (当前平台未实现)
 * @param pdev client device 指针
 * @param rx_data 接收缓冲
 * @param rx_cap 接收缓冲容量
 * @param trans_len 输出实际传输长度
 * @param timeout_ms 超时 (毫秒)
 * @return 固定返回 VFS_ERR_NOTSUPP
 */
int i2c_bus_slave_get_trans_result(struct device* pdev, uint8_t* rx_data, size_t rx_cap,
                                   size_t* trans_len, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(rx_data);
    COMPAT_IGNORE_RESULT(rx_cap);
    COMPAT_IGNORE_RESULT(trans_len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}
