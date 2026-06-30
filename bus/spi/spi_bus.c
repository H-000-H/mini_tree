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
#include "VFS.h"
#include "compiler_compat.h"
#include "system_log.h"

#define SPI_BUS_HOST_MAX  4

struct spi_bus_host {
    struct device*               dev;
    struct hal_spi_bus_host      hal_host;  /* 嵌入, 非指针 — HAL 无池管理 */
    atomic_int                   ref_count;   /* atomic: 无锁计数, ISR/任务安全 */
    uint8_t                      in_use;
};

struct spi_bus_client {
    struct device*               dev;
    struct spi_bus_host*         host;
    struct hal_spi_device_config cfg;
    struct hal_spi_dev           hal_dev;
    int                          hw_open;
};

static struct spi_bus_host   s_spi_hosts[SPI_BUS_HOST_MAX];
static struct spi_bus_client s_spi_clients[DEV_ID_COUNT];
static const char* const     kTag = "spi_bus";

/*===========================================================================================================================================================*/
                                                              /* Host pool helpers */
/*===========================================================================================================================================================*/
/**
 * @brief 从 host 池中申请一个空闲槽位
 * @return 成功返回槽位索引, 池满返回 -1
 */
static int spi_host_pool_claim(void)
{
    for (int i = 0; i < SPI_BUS_HOST_MAX; i++)
    {
        if (!s_spi_hosts[i].in_use)
        {
            s_spi_hosts[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

/**
 * @brief 释放 host 池槽位 (清零描述符)
 * @param idx 待释放的槽位索引
 */
static void spi_host_pool_release(int idx)
{
    if (idx >= 0 && idx < SPI_BUS_HOST_MAX)
        __builtin_memset(&s_spi_hosts[idx], 0, sizeof(s_spi_hosts[idx]));
}

/**
 * @brief 通过 device 指针查找对应的 spi_bus_host
 * @param dev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct spi_bus_host* spi_host_from_device(struct device* dev)
{
    for (int i = 0; i < SPI_BUS_HOST_MAX; i++)
    {
        if (s_spi_hosts[i].in_use && s_spi_hosts[i].dev == dev)
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

    idx = spi_host_pool_claim();
    if (idx < 0)
        return VFS_ERR_NOMEM;

    host = &s_spi_hosts[idx];
    host->dev = dev;
    atomic_init(&host->ref_count, 0);

    /* HAL host 嵌入 bus host, 直接传对象指针, 零翻译透传 config。
     * max_transfer_sz 的 ceiling clamp 由 HAL 层负责 (见 hal_spi_bus_host_init)。 */
    ret = hal_spi_bus_host_init(&host->hal_host, idx, host_cfg);
    if (ret != VFS_OK)
    {
        spi_host_pool_release(idx);
        return ret;
    }

    ret = bus_controller_bind_full(dev, BUS_TYPE_SPI,
                                   &s_spi_controller_ops, host);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(&host->hal_host));
        spi_host_pool_release(idx);
        return ret;
    }

    SYS_LOGI(kTag, "host init OK: %s role=%s spi=0x%lx",
             device_get_name(dev),
             host_cfg->bus_role == HAL_SPI_BUS_ROLE_SLAVE ? "slave" : "master",
             (unsigned long)host_cfg->spi);
    return VFS_OK;
}

/**
 * @brief host 初始化公开接口 (转发到 spi_host_init_impl)
 * @param dev controller device (host)
 * @param cfg host 配置 (struct hal_spi_bus_config*)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
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
    if (atomic_load(&host->ref_count) > 0)
    {
        SYS_LOGW(kTag, "host deinit busy: ref_count=%d",
                 atomic_load(&host->ref_count));
        return VFS_ERR_BUSY;
    }

    idx = (int)(host - s_spi_hosts);

    bus_controller_unbind(dev);

    ret = hal_spi_bus_host_deinit(&host->hal_host);

    if (ret == VFS_OK)
        spi_host_pool_release(idx);
    return ret;
}

/**
 * @brief host 反初始化公开接口 (转发到 spi_host_deinit_impl)
 * @param dev controller device (host)
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
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
    struct bus_controller* ctlr;

    if (!dev)
        return -1;

    if (bus_controller_of(dev, &ctlr) != VFS_OK)
        return -1;

    if (ctlr->type != BUS_TYPE_SPI)
        return -1;

    struct spi_bus_host* host = (struct spi_bus_host*)ctlr->hw_ctx;
    if (!host)
        return -1;

    return host->hal_host.cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER ?
           SPI_BUS_ROLE_MASTER : SPI_BUS_ROLE_SLAVE;
}

/**
 * @brief 查询 host 角色公开接口 (转发到 spi_host_role_impl)
 * @param dev controller device (host)
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
static int spi_client_register_impl(struct device* dev,
                                     const void* cfg, void** out)
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
    __builtin_memset(client, 0, sizeof(*client));
    client->dev  = dev;
    client->host = host;
    client->cfg  = *client_cfg;

    atomic_fetch_add(&host->ref_count, 1);

    *out = client;
    return VFS_OK;
}

/**
 * @brief client 注册公开接口 (转发到 spi_client_register_impl)
 * @param dev client device
 * @param cfg client 配置 (struct hal_spi_device_config*)
 * @param out 输出 spi_bus_client 指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int spi_bus_client_register(struct device* dev,
                             const struct hal_spi_device_config* cfg,
                             struct spi_bus_client** out)
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
        atomic_fetch_sub(&host->ref_count, 1);

    __builtin_memset(client, 0, sizeof(*client));
}

/**
 * @brief client 注销公开接口 (转发到 spi_client_unregister_impl)
 * @param dev client device
 */
void spi_bus_client_unregister(struct device* dev)
{
    spi_client_unregister_impl(dev);
}
/*===========================================================================================================================================================*/

                                                              /* Open / Close */
/*===========================================================================================================================================================*/
/**
 * @brief 打开 client 硬件 (初始化 hal_dev 并 hw_open, 幂等)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV 或 HAL 错误码
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
    hal_spi_dev_init(&client->hal_dev, (int)(client - s_spi_clients),
                     &client->host->hal_host, &client->cfg);
    ret = hal_spi_dev_hw_open(&client->hal_dev);
    if (ret != VFS_OK)
        return ret;

    client->hw_open = 1;
    return VFS_OK;
}

/**
 * @brief 关闭 client 硬件 (hw_close, 幂等)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV
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
 * @brief 同步传输 (全双工, 按 host 角色调用 spi_sync 或 spi_slave_sync)
 * @param dev client device
 * @param tx 发送缓冲区 (可 NULL 表示只收)
 * @param rx 接收缓冲区 (可 NULL 表示只发)
 * @param len 传输字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL/NODEV 或 HAL 错误码
 */
int spi_bus_transfer(struct device* dev, const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms)
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
        return spi_slave_sync(&client->hal_dev, tx, rx, len, timeout_ms);

    return spi_sync(&client->hal_dev, tx, rx, len, timeout_ms);
}

/*===========================================================================================================================================================*/
                                                              /* Async transfer (master only) */
/*===========================================================================================================================================================*/
/* callback 桥接: HAL callback 传 hal_spi_dev*, 转换为 struct device* 调用户 cb
 *
 * 生命周期: async 提交时分配 → ISR callback 触发时使用 → callback 返回时释放(in_use=0)
 * 必须静态分配: callback 在 ISR 中异步触发, 栈帧早已销毁 */
struct spi_async_bridge {
    struct device* dev;
    void (*cb)(struct device* dev, const void* trans, void* userdata);
    void* userdata;
    uint8_t in_use;
};
static struct spi_async_bridge s_bridge_pool[DEV_ID_COUNT][HAL_SPI_MAX_ASYNC];

/**
 * @brief 从 async bridge 池中分配一个空闲桥接描述符 (按 client idx 分组, 防 ISR UAF)
 * @param dev client device
 * @return 成功返回 bridge 指针, 池满返回 NULL
 */
static struct spi_async_bridge* spi_bridge_alloc(struct device* dev)
{
    struct spi_bus_client* client;
    int idx;
    int i;

    client = spi_client_from_device(dev);
    if (!client)
        return NULL;
    idx = (int)(client - s_spi_clients);

    for (i = 0; i < HAL_SPI_MAX_ASYNC; i++)
    {
        if (!s_bridge_pool[idx][i].in_use)
        {
            s_bridge_pool[idx][i].in_use = 1;
            return &s_bridge_pool[idx][i];
        }
    }
    return NULL;
}

/**
 * @brief 异步传输 HAL 回调桥接 (将 hal_spi_dev 转为 struct device 调用户 cb, ISR 安全)
 * @param hal_dev HAL 设备指针 (未使用, 由 HAL 传入)
 * @param trans 传输描述符
 * @param userdata 用户数据 (struct spi_async_bridge*)
 */
static void spi_async_hal_cb(struct hal_spi_dev* hal_dev,
                             const void* trans, void* userdata)
{
    struct spi_async_bridge* bridge = (struct spi_async_bridge*)userdata;
    if (!bridge)
        return;

    if (bridge->cb)
        bridge->cb(bridge->dev, trans, bridge->userdata);

    /* ISR 安全: 单字节写, 释放 bridge 供下次 async 复用 */
    bridge->in_use = 0;
}

/**
 * @brief 异步传输 (master 模式, 分配 bridge 桥接 ISR 回调到用户 cb)
 * @param dev client device
 * @param tx 发送缓冲区
 * @param rx 接收缓冲区
 * @param len 传输字节数
 * @param cb 传输完成回调 (可 NULL, 表示无需回调)
 * @param userdata 回调用户数据
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL/NODEV/BUSY
 */
int spi_bus_transfer_async(struct device* dev,
                           const uint8_t* tx, uint8_t* rx,
                           size_t len,
                           void (*cb)(struct device* dev,
                                      const void* trans,
                                      void* userdata),
                           void* userdata)
{
    struct spi_bus_client* client;
    struct spi_async_bridge* bridge;

    if (!dev || len == 0)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (!cb)
        return hal_spi_transfer_async(&client->hal_dev, tx, rx, len, NULL, NULL);

    bridge = spi_bridge_alloc(dev);
    if (!bridge)
        return VFS_ERR_BUSY;

    bridge->dev      = dev;
    bridge->cb       = cb;
    bridge->userdata = userdata;

    return hal_spi_transfer_async(&client->hal_dev, tx, rx, len,
                                 spi_async_hal_cb, bridge);
}

/**
 * @brief 轮询等待异步传输完成 (master 模式, 转发到 hal_spi_transfer_poll)
 * @param dev client device
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL/NODEV
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
 * @brief slave 模式同步传输 (校验 slave 角色后调用 spi_slave_sync)
 * @param dev client device
 * @param tx 发送缓冲区
 * @param rx 接收缓冲区
 * @param len 传输字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL/NODEV
 */
int spi_bus_slave_sync(struct device* dev, const uint8_t* tx, uint8_t* rx,
                       size_t len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev || len == 0 || (!tx && !rx))
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    return spi_slave_sync(&client->hal_dev, tx, rx, len, timeout_ms);
}

/**
 * @brief slave 模式排队发送 (转发到 spi_slave_queue_tx)
 * @param dev client device
 * @param data 发送数据
 * @param len 数据长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL/NODEV
 */
int spi_bus_slave_queue_tx(struct device* dev, const uint8_t* data, size_t len,
                           uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    return spi_slave_queue_tx(&client->hal_dev, data, len, timeout_ms);
}

/**
 * @brief slave 模式获取传输结果 (校验 slave 角色后调用 hal_spi_get_trans_result)
 * @param dev client device
 * @param rx_data 接收缓冲区
 * @param rx_cap 接收缓冲区容量
 * @param trans_len 输出实际传输长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL/NODEV
 */
int spi_bus_slave_get_trans_result(struct device* dev, uint8_t* rx_data,
                                   size_t rx_cap, size_t* trans_len,
                                   uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    return hal_spi_get_trans_result(&client->hal_dev, rx_data, rx_cap,
                                     trans_len, timeout_ms);
}
/*===========================================================================================================================================================*/
