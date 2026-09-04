/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file spi_bus.c
 *@brief spi bus 实现
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   SPI BUS 实现 — SPI 总线子系统 bus 层 (平台中立共享代码)
 *   静态池: s_spi_hosts[HOST_MAX] (含 hal_host, ref_count) + s_spi_clients[DEV_ID_COUNT] +
 *   s_bridge_pool[DEV_ID_COUNT][HAL_SPI_MAX_ASYNC] (async bridge, 防 ISR UAF)
 *   数据流:
 *   同步: VFS → spi_bus_open/close/transfer → hal_spi_*
 *   异步: VFS → transfer_async → bridge 池 → hal → ISR cb → bridge 释放 (poll 无需 bridge)
 *   controller_ops 表注册到 bus_controller_bind_full; impl 实现逻辑, public 函数转发
 *   引用计数: register/unregister 改 ref_count (open/close 不改); deinit >0 拒绝销毁
 *   异步: in_use 单字节写 ISR/任务无竞态; trans/bridge 池按 idx 分组避免跨设备争用
 *   平台中立: 本文件不做任何 #ifdef 平台区分, async/slave 路径直接转发到 HAL 函数。
 *   各平台 HAL .c 决定是否支持: 不支持则返回 MINI_ERR_NOTSUPP, 支持则真实实现。
 *   --------------------------------------------------------------------------
 */

#define SPI_BUS_IMPL
#include "spi_bus.h"

#include "board_config.h"
#include "board_devtable.h"
#include "bus.h"
#include "compiler_compat.h"
#include "device.h"
#include "hal_spi.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

/* host 池 = DTS "spi-master" 节点数 (缺省 1, dtc-lite 生成 DTC_GEN_COUNT_SPI_MASTER) */
#ifndef DTC_GEN_COUNT_SPI_MASTER
#define DTC_GEN_COUNT_SPI_MASTER 1
#endif
#define SPI_BUS_HOST_MAX DTC_GEN_COUNT_SPI_MASTER

/** @brief SPI host 运行时描述符 (静态池, HAL 嵌入 + atomic ref_count) */
struct spi_bus_host
{
    struct device*          pdev;      /**< 关联设备 */
    struct hal_spi_bus_host hal_host;  /**< 嵌入 HAL host (非指针, HAL 无池管理) */
    MINI_ATOMIC_INT         ref_count; /**< atomic 无锁计数, ISR/任务安全 */
};

/** @brief SPI client 运行时描述符 (静态表, 按 device_id 索引) */
struct spi_bus_client
{
    struct device*               pdev;    /**< 关联设备 */
    struct spi_bus_host*         host;    /**< 所属 host */
    struct hal_spi_device_config cfg;     /**< 设备配置 (DTSI 直投) */
    struct hal_spi_dev           hal_dev; /**< HAL 设备对象 */
    int                          hw_open; /**< 硬件打开计数 */
};

static struct spi_bus_host   s_spi_hosts[SPI_BUS_HOST_MAX];
static uint8_t               s_spi_host_used[SPI_BUS_HOST_MAX];
static osal_pool_t           s_spi_host_pool_ctrl;
static struct spi_bus_client s_spi_clients[DEV_ID_COUNT];
static const char* const     k_tag = "spi_bus";

/**
 * @brief SPI Host 池启动初始化
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_RES_POOL) static void spi_bus_pool_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_spi_host_pool_ctrl, s_spi_host_used, SPI_BUS_HOST_MAX));
}

/* -------------------------------------------------------------------------- */
/* Host pool helpers */
/* -------------------------------------------------------------------------- */
/**
 * @brief 通过 device 指针查找对应的 spi_bus_host
 * @param[in] pdev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct spi_bus_host* spi_host_from_device(struct device* pdev)
{
    for (int index = 0; index < SPI_BUS_HOST_MAX; index++)
        if (osal_pool_is_used(&s_spi_host_pool_ctrl, index) && s_spi_hosts[index].pdev == pdev)
            return &s_spi_hosts[index];
    return NULL;
}

/**
 * @brief 通过 device 指针查找对应的 spi_bus_client (按 device_id 索引)
 * @param[in] pdev client device 指针
 * @return 找到返回 client 指针, 未找到返回 NULL
 */
static struct spi_bus_client* spi_client_from_device(struct device* pdev)
{
    int id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT || !s_spi_clients[id].pdev)
        return NULL;
    return &s_spi_clients[id];
}
/* -------------------------------------------------------------------------- */
/* controller_ops (host 级操作) */
/* -------------------------------------------------------------------------- */
/* 前向声明: s_spi_controller_ops 引用 impl 函数, 但 impl 定义在 ops 表之后 */
static int  spi_host_init_impl(struct device* pdev, const void* cfg);
static int  spi_host_deinit_impl(struct device* pdev);
static int  spi_host_role_impl(struct device* pdev);
static int  spi_client_register_impl(struct device* pdev, const void* cfg, void** out);
static void spi_client_unregister_impl(struct device* pdev);

static const struct bus_controller_ops s_spi_controller_ops = {
    .init = spi_host_init_impl,
    .deinit = spi_host_deinit_impl,
    .role = spi_host_role_impl,
    .client_register = spi_client_register_impl,
    .client_unregister = spi_client_unregister_impl,
};
/* -------------------------------------------------------------------------- */
/* Host API */
/* -------------------------------------------------------------------------- */
/**
 * @brief host 初始化实现 (controller_ops.init): 分配 host 池槽位, 调用 HAL 初始化并绑定 controller
 * @param[in] pdev controller device (host)
 * @param[in] cfg host 配置 (struct hal_spi_bus_config*)
 * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL/NOMEM/...
 */
static int spi_host_init_impl(struct device* pdev, const void* cfg)
{
    const struct hal_spi_bus_config* host_cfg;
    struct spi_bus_host*             host;
    int                              idx;
    int                              ret;

    if (!pdev || !cfg)
        return MINI_ERR_INVAL;

    host_cfg = (const struct hal_spi_bus_config*)cfg;

    if (spi_host_from_device(pdev))
        return MINI_OK;

    idx = osal_pool_claim(&s_spi_host_pool_ctrl);
    if (idx < 0)
        return MINI_ERR_NOMEM;

    host = &s_spi_hosts[idx];
    MINI_MEM_SET(host, 0, sizeof(*host));
    host->pdev = pdev;
    MINI_ATOMIC_RUNTIME_INIT(&host->ref_count, 0);

    /* HAL host 嵌入 bus host, 直接传对象指针, 零翻译透传 config。
     * max_transfer_sz 的 ceiling clamp 由 HAL 层负责 (见 hal_spi_bus_host_init)。 */
    ret = hal_spi_bus_host_init(&host->hal_host, idx, host_cfg);
    if (ret != MINI_OK)
    {
        MINI_MEM_SET(host, 0, sizeof(*host));
        MINI_IGNORE_RESULT(osal_pool_release(&s_spi_host_pool_ctrl, idx));
        return ret;
    }

    ret = bus_controller_bind_full(pdev, BUS_TYPE_SPI, &s_spi_controller_ops, host);
    if (ret != MINI_OK)
    {
        MINI_IGNORE_RESULT(hal_spi_bus_host_deinit(&host->hal_host));
        MINI_MEM_SET(host, 0, sizeof(*host));
        MINI_IGNORE_RESULT(osal_pool_release(&s_spi_host_pool_ctrl, idx));
        return ret;
    }

    SYS_LOGI(k_tag, "host init OK: %s role=%s spi=0x%lx", device_get_name(pdev), host_cfg->bus_role == HAL_SPI_BUS_ROLE_SLAVE ? "slave" : "master",
             (unsigned long)host_cfg->spi);
    return MINI_OK;
}

int spi_bus_host_init(struct device* pdev, const struct hal_spi_bus_config* cfg) { return spi_host_init_impl(pdev, cfg); }

/**
 * @brief host 反初始化实现 (controller_ops.deinit): 检查 ref_count, 解绑 controller, 释放池槽位
 * @param[in] pdev controller device (host)
 * @return 成功返回 MINI_OK, BUSY 返回 MINI_ERR_BUSY, 失败返回 VFS_ERR_*
 */
static int spi_host_deinit_impl(struct device* pdev)
{
    struct spi_bus_host* host;
    int                  idx;
    int                  ret;

    if (!pdev)
        return MINI_ERR_INVAL;

    host = spi_host_from_device(pdev);
    if (!host)
        return MINI_ERR_NODEV;

    /* atomic load: 无锁检查 ref_count, ISR/任务安全 */
    if (MINI_ATOMIC_LOAD(&host->ref_count, MINI_SEQ_CST) > 0)
    {
        SYS_LOGW(k_tag, "host deinit busy: ref_count=%d", MINI_ATOMIC_LOAD(&host->ref_count, MINI_SEQ_CST));
        return MINI_ERR_BUSY;
    }

    idx = (int)(host - s_spi_hosts);

    bus_controller_unbind(pdev);

    ret = hal_spi_bus_host_deinit(&host->hal_host);

    if (ret == MINI_OK)
    {
        MINI_MEM_SET(host, 0, sizeof(*host));
        MINI_IGNORE_RESULT(osal_pool_release(&s_spi_host_pool_ctrl, idx));
    }
    return ret;
}

int spi_bus_host_deinit(struct device* pdev) { return spi_host_deinit_impl(pdev); }

/**
 * @brief 查询 host 角色 (master/slave) 实现 (controller_ops.role)
 * @param[in] pdev controller device (host)
 * @return master 返回 SPI_BUS_ROLE_MASTER, slave 返回 SPI_BUS_ROLE_SLAVE, 失败返回 -1
 */
static int spi_host_role_impl(struct device* pdev)
{
    struct bus_controller* ctlr = NULL;
    struct spi_bus_host*   host;

    if (!pdev)
        return -1;

    /* 支持传 host 或 client: 先查自身, 再查 parent */
    if (bus_controller_get(pdev, &ctlr) != MINI_OK)
    {
        if (bus_controller_of(pdev, &ctlr) != MINI_OK)
            return -1;
    }

    if (!ctlr || ctlr->type != BUS_TYPE_SPI)
        return -1;

    host = (struct spi_bus_host*)ctlr->hw_ctx;
    if (!host)
        return -1;

    return host->hal_host.cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER ? SPI_BUS_ROLE_MASTER : SPI_BUS_ROLE_SLAVE;
}

int spi_bus_host_role(struct device* pdev) { return spi_host_role_impl(pdev); }
/* -------------------------------------------------------------------------- */
/* Client API */
/* -------------------------------------------------------------------------- */
/**
 * @brief client 注册实现 (controller_ops.client_register): 绑定 client 到 host, ref_count +1
 * @param[in] pdev client device
 * @param[in] cfg client 配置 (struct hal_spi_device_config*)
 * @param[out] out 输出 client 私有上下文指针
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int spi_client_register_impl(struct device* pdev, const void* cfg, void** out)
{
    const struct hal_spi_device_config* client_cfg;
    struct bus_controller*              ctlr;
    struct spi_bus_host*                host;
    struct spi_bus_client*              client;
    int                                 id;

    if (!pdev || !cfg || !out)
        return MINI_ERR_INVAL;
    *out = NULL;

    client_cfg = (const struct hal_spi_device_config*)cfg;

    if (bus_controller_of(pdev, &ctlr) != MINI_OK)
        return MINI_ERR_NODEV;

    if (ctlr->type != BUS_TYPE_SPI)
        return MINI_ERR_NODEV;

    host = (struct spi_bus_host*)ctlr->hw_ctx;
    if (!host)
        return MINI_ERR_IO;

    id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT)
        return MINI_ERR_INVAL;

    client = &s_spi_clients[id];
    /* 幂等: 已注册则直接返回, 避免重复 memset / ref_count++ */
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

int spi_bus_client_register(struct device* pdev, const struct hal_spi_device_config* cfg, struct spi_bus_client** out)
{
    return spi_client_register_impl(pdev, cfg, (void**)out);
}

/**
 * @brief client 注销实现 (controller_ops.client_unregister): 关闭 hw, ref_count -1, 清零槽位
 * @param[in] pdev client device
 */
static void spi_client_unregister_impl(struct device* pdev)
{
    struct spi_bus_client* client;
    struct spi_bus_host*   host;

    client = spi_client_from_device(pdev);
    if (!client)
        return;

    /* 若 client 仍 hw_open, 先 close 以释放 HAL 层 ref_count 与 master spi_device_handle */
    if (client->hw_open)
    {
        MINI_IGNORE_RESULT(spi_bus_close(pdev));
        client->hw_open = 0;
    }

    host = client->host;
    if (host)
        (void)MINI_ATOMIC_FETCH_SUB(&host->ref_count, 1, MINI_SEQ_CST);

    MINI_MEM_SET(client, 0, sizeof(*client));
}

void spi_bus_client_unregister(struct device* pdev) { spi_client_unregister_impl(pdev); }

/* -------------------------------------------------------------------------- */

/* Open / Close */
/* -------------------------------------------------------------------------- */
int spi_bus_open(struct device* pdev)
{
    struct spi_bus_client* client;
    int                    ret;

    client = spi_client_from_device(pdev);
    if (!client)
        return MINI_ERR_NODEV;

    if (client->hw_open)
        return MINI_OK;

    /* client->cfg 已是 hal_spi_device_config, 直接透传给 HAL, 零翻译 */
    MINI_IGNORE_RESULT(hal_spi_dev_init(&client->hal_dev, &client->host->hal_host, &client->cfg));
    ret = hal_spi_dev_hw_open(&client->hal_dev);
    if (ret != MINI_OK)
        return ret;

    client->hw_open = 1;
    return MINI_OK;
}

int spi_bus_close(struct device* pdev)
{
    struct spi_bus_client* client;

    client = spi_client_from_device(pdev);
    if (!client)
        return MINI_ERR_NODEV;

    if (client->hw_open)
    {
        MINI_IGNORE_RESULT(hal_spi_dev_hw_close(&client->hal_dev));
        client->hw_open = 0;
    }
    return MINI_OK;
}
/* -------------------------------------------------------------------------- */

/* Transfer API */
/* -------------------------------------------------------------------------- */
int spi_bus_transfer(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct spi_bus_client* client;
    int                    role;

    if (!pdev || len == 0)
        return MINI_ERR_INVAL;

    client = spi_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    role = spi_bus_host_role(pdev);
    if (role == SPI_BUS_ROLE_SLAVE)
        return hal_spi_slave_sync(&client->hal_dev, tx, rx, len, timeout_ms);
    if (role == SPI_BUS_ROLE_MASTER)
        return hal_spi_sync(&client->hal_dev, tx, rx, len, timeout_ms, xfer_mode);

    return MINI_ERR_NODEV;
}

/* -------------------------------------------------------------------------- */
/* Async transfer (master only) */
/* -------------------------------------------------------------------------- */
/* callback 桥接: HAL cb 传 hal_spi_dev* → bus_async_bridge_complete → 用户 device* cb
 * 池按 client idx 分组, 防跨设备争用 / ISR UAF */
static struct bus_async_bridge s_spi_bridge_pool[DEV_ID_COUNT][HAL_SPI_MAX_ASYNC];

/**
 * @brief HAL 异步完成回调桥接 → bus_async_bridge_complete (用户 device* cb)
 * @param[in] hal_dev HAL SPI 设备指针 (未使用)
 * @param[in] trans 传输完成描述符指针
 * @param[in] userdata bus_async_bridge 上下文指针
 */
static void spi_async_hal_cb(struct hal_spi_dev* hal_dev, const void* trans, void* userdata)
{
    MINI_IGNORE_RESULT(hal_dev);
    bus_async_bridge_complete(userdata, trans);
}

int spi_bus_transfer_async(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                           void (*cb)(struct device* pdev, const void* trans, void* userdata), void* userdata)
{
    struct spi_bus_client*   client;
    struct bus_async_bridge* bridge;
    int                      idx;
    int                      ret;

    if (!pdev || len == 0)
        return MINI_ERR_INVAL;

    client = spi_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    if (spi_bus_host_role(pdev) != SPI_BUS_ROLE_MASTER)
        return MINI_ERR_INVAL;

    if (!cb)
        return hal_spi_transfer_async(&client->hal_dev, tx, rx, len, NULL, NULL);

    idx = (int)(client - s_spi_clients);
    bridge = bus_async_bridge_claim(s_spi_bridge_pool[idx], HAL_SPI_MAX_ASYNC);
    if (!bridge)
        return MINI_ERR_BUSY;

    bus_async_bridge_bind(bridge, pdev, cb, userdata);

    ret = hal_spi_transfer_async(&client->hal_dev, tx, rx, len, spi_async_hal_cb, bridge);
    if (ret != MINI_OK)
        bus_async_bridge_release(bridge);
    return ret;
}

int spi_bus_transfer_poll(struct device* pdev, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!pdev)
        return MINI_ERR_INVAL;

    client = spi_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    if (spi_bus_host_role(pdev) != SPI_BUS_ROLE_MASTER)
        return MINI_ERR_INVAL;

    return hal_spi_transfer_poll(&client->hal_dev, timeout_ms);
}

int spi_bus_slave_sync(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!pdev || len == 0 || (!tx && !rx))
        return MINI_ERR_INVAL;

    client = spi_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    if (spi_bus_host_role(pdev) != SPI_BUS_ROLE_SLAVE)
        return MINI_ERR_INVAL;

    return hal_spi_slave_sync(&client->hal_dev, tx, rx, len, timeout_ms);
}

int spi_bus_slave_queue_tx(struct device* pdev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!pdev || !data || len == 0)
        return MINI_ERR_INVAL;

    client = spi_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    if (spi_bus_host_role(pdev) != SPI_BUS_ROLE_SLAVE)
        return MINI_ERR_INVAL;

    return hal_spi_slave_queue_tx(&client->hal_dev, data, len, timeout_ms);
}

int spi_bus_slave_get_trans_result(struct device* pdev, uint8_t* rx_data, size_t rx_cap, size_t* trans_len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!pdev)
        return MINI_ERR_INVAL;

    client = spi_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    if (spi_bus_host_role(pdev) != SPI_BUS_ROLE_SLAVE)
        return MINI_ERR_INVAL;

    return hal_spi_get_trans_result(&client->hal_dev, rx_data, rx_cap, trans_len, timeout_ms);
}
/* -------------------------------------------------------------------------- */
