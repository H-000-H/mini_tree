/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * SPI BUS 实现 — SPI 总线子系统 bus 层 (平台中立共享代码)
 *
 * 静态池: s_spi_hosts[HOST_MAX] (含 hal_host, ref_count) + s_spi_clients[DEV_ID_COUNT] +
 *   s_bridge_pool[DEV_ID_COUNT][HAL_SPI_MAX_ASYNC] (async bridge, 防 ISR UAF)
 *
 * 数据流:
 *   同步: VFS → spi_bus_open/close/transfer → hal_spi_*
 *   异步: VFS → transfer_async → bridge 池 → hal → ISR cb → bridge 释放 (poll 无需 bridge)
 *
 * controller_ops 表注册到 bus_controller_bind_full; impl 实现逻辑, public 函数转发
 * 引用计数: register/unregister 改 ref_count (open/close 不改); deinit >0 拒绝销毁
 * 异步: in_use 单字节写 ISR/任务无竞态; trans/bridge 池按 idx 分组避免跨设备争用
 *
 * 平台中立: 本文件不做任何 #ifdef 平台区分, async/slave 路径直接转发到 HAL 函数。
 * 各平台 HAL .c 决定是否支持: 不支持则返回 VFS_ERR_NOTSUPP, 支持则真实实现。
 *@=========================================================================================================================*/
#define SPI_BUS_IMPL
#include "spi_bus.h"
#include "bus.h"
#include "hal_spi.h"
#include "device.h"
#include "board_devtable.h"
#include "status.h"
#include "compiler_compat.h"
#include "system_log.h"
#include "osal.h"

#define SPI_BUS_HOST_MAX  3  /* 对齐 HAL/DTS host-max (SPI1/2/3) */

/** @brief SPI host 运行时描述符 (静态池, HAL 嵌入 + atomic ref_count) */
struct spi_bus_host {
    struct device*               dev;        /**< 关联设备 */
    struct hal_spi_bus_host      hal_host;   /**< 嵌入 HAL host (非指针, HAL 无池管理) */
    COMPAT_ATOMIC_INT             ref_count; /**< atomic 无锁计数, ISR/任务安全 */
};

/** @brief SPI client 运行时描述符 (静态表, 按 device_id 索引) */
struct spi_bus_client {
    struct device*               dev;    /**< 关联设备 */
    struct spi_bus_host*         host;   /**< 所属 host */
    struct hal_spi_device_config cfg;    /**< 设备配置 (DTSI 直投) */
    struct hal_spi_dev           hal_dev; /**< HAL 设备对象 */
    int                          hw_open; /**< 硬件打开计数 */
};

static struct spi_bus_host s_spi_hosts[SPI_BUS_HOST_MAX];
static uint8_t             s_spi_host_used[SPI_BUS_HOST_MAX];
static osal_pool_t         s_spi_host_pool_ctrl;
static struct spi_bus_client s_spi_clients[DEV_ID_COUNT];
static const char* const     k_tag = "spi_bus";

/**
 * @brief SPI Host 池启动初始化
 */
pre_execution(150)
static void spi_bus_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_spi_host_pool_ctrl, s_spi_host_used, SPI_BUS_HOST_MAX));
}

/*===========================================================================================================================================================*/
                                                              /* Host pool helpers */
/*===========================================================================================================================================================*/
/**
 * @brief 通过 device 指针查找对应的 spi_bus_host
 * @param dev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct spi_bus_host* spi_host_from_device(struct device* dev)
{
    for (int i = 0; i < SPI_BUS_HOST_MAX; i++)
    {
        if (osal_pool_is_used(&s_spi_host_pool_ctrl, i) && s_spi_hosts[i].dev == dev)
            return &s_spi_hosts[i];
    }
    return NULL;
}

/**
 * @brief 通过 device 指针查找对应的 spi_bus_client (按 device_id 索引)
 * @param dev client device 指针
 * @return 找到返回 client 指针, 未找到返回 NULL
 */
static struct spi_bus_client* spi_client_from_device(struct device* dev)
{
    int id = (int)board_dev_find(device_get_name(dev));
    if (id < 0 || id >= DEV_ID_COUNT || !s_spi_clients[id].dev)
        return NULL;
    return &s_spi_clients[id];
}
/*===========================================================================================================================================================*/
                                                              /* controller_ops (host 级操作) */
/*===========================================================================================================================================================*/
/* 前向声明: s_spi_controller_ops 引用 impl 函数, 但 impl 定义在 ops 表之后 */
static int  spi_host_init_impl(struct device* dev, const void* cfg);
static int  spi_host_deinit_impl(struct device* dev);
static int  spi_host_role_impl(struct device* dev);
static int  spi_client_register_impl(struct device* dev, const void* cfg, void** out);
static void spi_client_unregister_impl(struct device* dev);

static const struct bus_controller_ops s_spi_controller_ops = {
    .init              = spi_host_init_impl,
    .deinit            = spi_host_deinit_impl,
    .role              = spi_host_role_impl,
    .client_register   = spi_client_register_impl,
    .client_unregister = spi_client_unregister_impl,
};
/*===========================================================================================================================================================*/
                                                              /* Host API */
/*===========================================================================================================================================================*/
/**
 * @brief host 初始化实现 (controller_ops.init): 分配 host 池槽位, 调用 HAL 初始化并绑定 controller
 * @param dev controller device (host)
 * @param cfg host 配置 (struct hal_spi_bus_config*)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL/NOMEM/...
 */
static int spi_host_init_impl(struct device* dev, const void* cfg)
{
    const struct hal_spi_bus_config* host_cfg;
    struct spi_bus_host*    host;
    int                     idx;
    int                     ret;

    if (!dev || !cfg)
        return VFS_ERR_INVAL;

    host_cfg = (const struct hal_spi_bus_config*)cfg;

    if (spi_host_from_device(dev))
        return VFS_OK;

    idx = osal_pool_claim(&s_spi_host_pool_ctrl);
    if (idx < 0)
        return VFS_ERR_NOMEM;

    host = &s_spi_hosts[idx];
    COMPAT_MEM_SET(host, 0, sizeof(*host));
    host->dev = dev;
    COMPAT_ATOMIC_RUNTIME_INIT(&host->ref_count, 0);

    /* HAL host 嵌入 bus host, 直接传对象指针, 零翻译透传 config。
     * max_transfer_sz 的 ceiling clamp 由 HAL 层负责 (见 hal_spi_bus_host_init)。 */
    ret = hal_spi_bus_host_init(&host->hal_host, idx, host_cfg);
    if (ret != VFS_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_spi_host_pool_ctrl, idx));
        return ret;
    }

    ret = bus_controller_bind_full(dev, BUS_TYPE_SPI, &s_spi_controller_ops, host);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(&host->hal_host));
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_spi_host_pool_ctrl, idx));
        return ret;
    }

    SYS_LOGI(k_tag, "host init OK: %s role=%s spi=0x%lx", device_get_name(dev), host_cfg->bus_role == HAL_SPI_BUS_ROLE_SLAVE ? "slave" : "master", (unsigned long)host_cfg->spi);
    return VFS_OK;
}

/**
 * @brief 初始化 SPI host 并绑定总线控制器
 * @param dev host device 指针
 * @param cfg host 配置 (struct hal_spi_bus_config*)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_host_init(struct device* dev, const struct hal_spi_bus_config* cfg)
{
    return spi_host_init_impl(dev, cfg);
}

/**
 * @brief host 反初始化实现 (controller_ops.deinit): 检查 ref_count, 解绑 controller, 释放池槽位
 * @param dev controller device (host)
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
 */
static int spi_host_deinit_impl(struct device* dev)
{
    struct spi_bus_host* host;
    int                  idx;
    int                  ret;

    if (!dev)
        return VFS_ERR_INVAL;

    host = spi_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    /* atomic load: 无锁检查 ref_count, ISR/任务安全 */
    if (COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST) > 0)
    {
        SYS_LOGW(k_tag, "host deinit busy: ref_count=%d", COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST));
        return VFS_ERR_BUSY;
    }

    idx = (int)(host - s_spi_hosts);

    bus_controller_unbind(dev);

    ret = hal_spi_bus_host_deinit(&host->hal_host);

    if (ret == VFS_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_spi_host_pool_ctrl, idx));
    }
    return ret;
}

/**
 * @brief 反初始化 SPI host 并释放对象池槽位
 * @param dev host device 指针
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回负数错误码
 */
int spi_bus_host_deinit(struct device* dev)
{
    return spi_host_deinit_impl(dev);
}

/**
 * @brief 查询 host 角色 (master/slave) 实现 (controller_ops.role)
 * @param dev controller device (host)
 * @return master 返回 SPI_BUS_ROLE_MASTER, slave 返回 SPI_BUS_ROLE_SLAVE, 失败返回 -1
 */
static int spi_host_role_impl(struct device* dev)
{
    struct bus_controller* ctlr = NULL;
    struct spi_bus_host*   host;

    if (!dev)
        return -1;

    /* 支持传 host 或 client: 先查自身, 再查 parent */
    if (bus_controller_get(dev, &ctlr) != VFS_OK)
    {
        if (bus_controller_of(dev, &ctlr) != VFS_OK)
            return -1;
    }

    if (!ctlr || ctlr->type != BUS_TYPE_SPI)
        return -1;

    host = (struct spi_bus_host*)ctlr->hw_ctx;
    if (!host)
        return -1;

    return host->hal_host.cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER ? SPI_BUS_ROLE_MASTER : SPI_BUS_ROLE_SLAVE;
}

/**
 * @brief 查询 SPI host 总线角色 (master/slave)
 * @param dev host 或 client device 指针
 * @return master 返回 SPI_BUS_ROLE_MASTER, slave 返回 SPI_BUS_ROLE_SLAVE, 失败返回 -1
 */
int spi_bus_host_role(struct device* dev)
{
    return spi_host_role_impl(dev);
}
/*===========================================================================================================================================================*/
                                                              /* Client API */
/*===========================================================================================================================================================*/
/**
 * @brief client 注册实现 (controller_ops.client_register): 绑定 client 到 host, ref_count +1
 * @param dev client device
 * @param cfg client 配置 (struct hal_spi_device_config*)
 * @param out 输出 client 私有上下文指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
static int spi_client_register_impl(struct device* dev, const void* cfg, void** out)
{
    const struct hal_spi_device_config* client_cfg;
    struct bus_controller* ctlr;
    struct spi_bus_host*   host;
    struct spi_bus_client* client;
    int                    id;

    if (!dev || !cfg || !out)
        return VFS_ERR_INVAL;
    *out = NULL;

    client_cfg = (const struct hal_spi_device_config*)cfg;

    if (bus_controller_of(dev, &ctlr) != VFS_OK)
        return VFS_ERR_NODEV;

    if (ctlr->type != BUS_TYPE_SPI)
        return VFS_ERR_NODEV;

    host = (struct spi_bus_host*)ctlr->hw_ctx;
    if (!host)
        return VFS_ERR_IO;

    id = (int)board_dev_find(device_get_name(dev));
    if (id < 0 || id >= DEV_ID_COUNT)
        return VFS_ERR_INVAL;

    client = &s_spi_clients[id];
    /* 幂等: 已注册则直接返回, 避免重复 memset / ref_count++ */
    if (client->dev)
    {
        if (client->dev != dev)
            return VFS_ERR_BUSY;
        *out = client;
        return VFS_OK;
    }

    COMPAT_MEM_SET(client, 0, sizeof(*client));
    client->dev  = dev;
    client->host = host;
    client->cfg  = *client_cfg;

    (void)COMPAT_ATOMIC_FETCH_ADD(&host->ref_count, 1, COMPAT_MO_SEQ_CST);

    *out = client;
    return VFS_OK;
}

/**
 * @brief 注册 SPI client 并增加 host 引用计数
 * @param dev client device 指针
 * @param cfg client 配置 (struct hal_spi_device_config*)
 * @param out 输出 client 私有上下文指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_client_register(struct device* dev, const struct hal_spi_device_config* cfg, struct spi_bus_client** out)
{
    return spi_client_register_impl(dev, cfg, (void**)out);
}

/**
 * @brief client 注销实现 (controller_ops.client_unregister): 关闭 hw, ref_count -1, 清零槽位
 * @param dev client device
 */
static void spi_client_unregister_impl(struct device* dev)
{
    struct spi_bus_client* client;
    struct spi_bus_host*   host;

    client = spi_client_from_device(dev);
    if (!client)
        return;

    /* 若 client 仍 hw_open, 先 close 以释放 HAL 层 ref_count 与 master spi_device_handle */
    if (client->hw_open)
    {
        COMPAT_IGNORE_RESULT(spi_bus_close(dev));
        client->hw_open = 0;
    }

    host = client->host;
    if (host)
        (void)COMPAT_ATOMIC_FETCH_SUB(&host->ref_count, 1, COMPAT_MO_SEQ_CST);

    COMPAT_MEM_SET(client, 0, sizeof(*client));
}

/**
 * @brief 注销 SPI client 并递减 host 引用计数
 * @param dev client device 指针
 */
void spi_bus_client_unregister(struct device* dev)
{
    spi_client_unregister_impl(dev);
}
/*===========================================================================================================================================================*/

                                                              /* Open / Close */
/*===========================================================================================================================================================*/
/**
 * @brief 打开 SPI client 硬件 (HAL init + hw_open)
 * @param dev client device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_open(struct device* dev)
{
    struct spi_bus_client*       client;
    int                          ret;

    client = spi_client_from_device(dev);
    if (!client)
        return VFS_ERR_NODEV;

    if (client->hw_open)
        return VFS_OK;

    /* client->cfg 已是 hal_spi_device_config, 直接透传给 HAL, 零翻译 */
    COMPAT_IGNORE_RESULT(hal_spi_dev_init(&client->hal_dev, &client->host->hal_host, &client->cfg));
    ret = hal_spi_dev_hw_open(&client->hal_dev);
    if (ret != VFS_OK)
        return ret;

    client->hw_open = 1;
    return VFS_OK;
}

/**
 * @brief 关闭 SPI client 硬件
 * @param dev client device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_close(struct device* dev)
{
    struct spi_bus_client* client;

    client = spi_client_from_device(dev);
    if (!client)
        return VFS_ERR_NODEV;

    if (client->hw_open)
    {
        COMPAT_IGNORE_RESULT(hal_spi_dev_hw_close(&client->hal_dev));
        client->hw_open = 0;
    }
    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                              /* Transfer API */
/*===========================================================================================================================================================*/
/**
 * @brief SPI 同步传输 (master 走 hal_spi_sync, slave 走 hal_spi_slave_sync)
 * @param dev client device 指针
 * @param tx 发送缓冲 (可为 NULL)
 * @param rx 接收缓冲 (可为 NULL)
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @param xfer_mode 传输模式 (POLL/DMA/AUTO, 仅 master)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_transfer(struct device* dev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct spi_bus_client* client;
    int                    role;

    if (!dev || len == 0)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    role = spi_bus_host_role(dev);
    if (role == SPI_BUS_ROLE_SLAVE)
        return hal_spi_slave_sync(&client->hal_dev, tx, rx, len, timeout_ms);
    if (role == SPI_BUS_ROLE_MASTER)
        return hal_spi_sync(&client->hal_dev, tx, rx, len, timeout_ms, xfer_mode);

    return VFS_ERR_NODEV;
}

/*===========================================================================================================================================================*/
                                                              /* Async transfer (master only) */
/*===========================================================================================================================================================*/
/* callback 桥接: HAL cb 传 hal_spi_dev* → bus_async_bridge_complete → 用户 device* cb
 * 池按 client idx 分组, 防跨设备争用 / ISR UAF */
static struct bus_async_bridge s_spi_bridge_pool[DEV_ID_COUNT][HAL_SPI_MAX_ASYNC];

/**
 * @brief HAL 异步完成回调桥接 → bus_async_bridge_complete (用户 device* cb)
 * @param hal_dev HAL SPI 设备指针 (未使用)
 * @param trans 传输完成描述符指针
 * @param userdata bus_async_bridge 上下文指针
 */
static void spi_async_hal_cb(struct hal_spi_dev* hal_dev, const void* trans, void* userdata)
{
    COMPAT_IGNORE_RESULT(hal_dev);
    bus_async_bridge_complete(userdata, trans);
}

/**
 * @brief SPI 异步传输 (仅 master, 可选用户回调桥接)
 * @param dev client device 指针
 * @param tx 发送缓冲 (可为 NULL)
 * @param rx 接收缓冲 (可为 NULL)
 * @param len 字节数
 * @param cb 完成回调 (NULL 则无回调)
 * @param userdata 回调用户数据
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_transfer_async(struct device* dev, const uint8_t* tx, uint8_t* rx, size_t len, void (*cb)(struct device* dev, const void* trans, void* userdata), void* userdata)
{
    struct spi_bus_client*   client;
    struct bus_async_bridge* bridge;
    int                      idx;
    int                      ret;

    if (!dev || len == 0)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (!cb)
        return hal_spi_transfer_async(&client->hal_dev, tx, rx, len, NULL, NULL);

    idx = (int)(client - s_spi_clients);
    bridge = bus_async_bridge_claim(s_spi_bridge_pool[idx], HAL_SPI_MAX_ASYNC);
    if (!bridge)
        return VFS_ERR_BUSY;

    bus_async_bridge_bind(bridge, dev, cb, userdata);

    ret = hal_spi_transfer_async(&client->hal_dev, tx, rx, len, spi_async_hal_cb, bridge);
    if (ret != VFS_OK)
        bus_async_bridge_release(bridge);
    return ret;
}

/**
 * @brief 轮询等待 SPI 异步传输完成 (仅 master)
 * @param dev client device 指针
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_transfer_poll(struct device* dev, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    return hal_spi_transfer_poll(&client->hal_dev, timeout_ms);
}

/**
 * @brief SPI slave 同步传输
 * @param dev client device 指针
 * @param tx 发送缓冲 (可为 NULL)
 * @param rx 接收缓冲 (可为 NULL)
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_slave_sync(struct device* dev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev || len == 0 || (!tx && !rx))
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    return hal_spi_slave_sync(&client->hal_dev, tx, rx, len, timeout_ms);
}

/**
 * @brief SPI slave 排队发送
 * @param dev client device 指针
 * @param data 发送缓冲
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_slave_queue_tx(struct device* dev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    return hal_spi_slave_queue_tx(&client->hal_dev, data, len, timeout_ms);
}

/**
 * @brief 获取 SPI slave 传输结果
 * @param dev client device 指针
 * @param rx_data 接收缓冲
 * @param rx_cap 接收缓冲容量
 * @param trans_len 输出实际传输长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int spi_bus_slave_get_trans_result(struct device* dev, uint8_t* rx_data, size_t rx_cap, size_t* trans_len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    return hal_spi_get_trans_result(&client->hal_dev, rx_data, rx_cap, trans_len, timeout_ms);
}
/*===========================================================================================================================================================*/
