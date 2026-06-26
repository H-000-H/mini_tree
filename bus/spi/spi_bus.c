/* SPDX-License-Identifier: GPL-2.0-or-later */
/*@=========================================================================================================================*
 * SPI BUS 实现 — SPI 总线子系统 bus 层
 *
 * 静态表设计:
 *   - s_spi_hosts[SPI_BUS_HOST_MAX]:     host 描述符池 (含 hal_host, ref_count, role)
 *   - s_spi_clients[SPI_BUS_CLIENT_MAX]: client 描述符池 (含 host 反向指针)
 *   - s_bridge_pool[DEV_ID_COUNT][HAL_SPI_MAX_ASYNC]: async bridge 池 (静态, 防 ISR UAF)
 *
 * 数据流:
 *   同步: VFS fops → spi_bus_open/close/transfer → spi_client_from_device → hal_spi_*
 *   异步: VFS → spi_bus_transfer_async → bridge 池 → hal_spi_transfer_async
 *         ISR → hal_spi_callback_t → spi_async_hal_cb → user cb → bridge 释放
 *         VFS → spi_bus_transfer_poll → hal_spi_transfer_poll → trans 回收
 *
 * controller_ops 架构:
 *   - s_spi_controller_ops 表注册到 bus_controller_bind_full
 *   - impl 函数实现具体逻辑, wrapper 函数转发 (保持 API 兼容)
 *
 * 引用计数 (atomic_int):
 *   - spi_client_register_impl: atomic_fetch_add (client 注册时 +1)
 *   - spi_client_unregister_impl: atomic_fetch_sub (client 注销时 -1)
 *   - bus_ops open/close (spi_bus_open/close): 只触 HAL open/close, 不改 ref_count
 *   - host_deinit: atomic_load 检查 > 0 拒绝销毁
 *
 * 异步传输关键点:
 *   - bridge 释放时机: callback 返回时 (非 poll), 因 bridge 生命周期只覆盖到 cb 触发
 *   - cb=NULL 的纯 poll 模式不需要 bridge
 *   - in_use 单字节写, ISR 与任务间无竞态 (ESP32-S3 单字节写原子)
 *   - trans 池按 hw_idx 分组, bridge 池按 client idx 分组, 避免跨设备争用
 *@=========================================================================================================================*/
#define SPI_BUS_IMPL
#include "spi_bus.h"
#include "bus.h"
#include "hal_spi.h"
#include "device.h"
#include "board_devtable.h"
#include "hal_gpio.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "system_log.h"

#include <string.h>

#define SPI_BUS_HOST_MAX  4

struct spi_bus_host {
    struct device*               dev;
    struct hal_spi_bus_host*     hal_host;
    atomic_int                   ref_count;   /* atomic: 无锁计数, ISR/任务安全 */
    uint8_t                      in_use;
};

struct spi_bus_client {
    struct device*               dev;
    struct spi_bus_host*         host;
    struct spi_bus_client_config cfg;
    struct hal_spi_dev           hal_dev;
    int                          hw_open;
};

static struct spi_bus_host   s_spi_hosts[SPI_BUS_HOST_MAX];
static struct spi_bus_client s_spi_clients[DEV_ID_COUNT];
static const char* const     kTag = "spi_bus";

/*===========================================================================================================================================================*/
                                                              /* Host pool helpers */
/*===========================================================================================================================================================*/
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

static void spi_host_pool_release(int idx)
{
    if (idx >= 0 && idx < SPI_BUS_HOST_MAX)
        __builtin_memset(&s_spi_hosts[idx], 0, sizeof(s_spi_hosts[idx]));
}

static struct spi_bus_host* spi_host_from_device(struct device* dev)
{
    for (int i = 0; i < SPI_BUS_HOST_MAX; i++)
    {
        if (s_spi_hosts[i].in_use && s_spi_hosts[i].dev == dev)
            return &s_spi_hosts[i];
    }
    return NULL;
}

static struct spi_bus_client* spi_client_from_device(struct device* dev)
{
    int id = (int)board_dev_find(device_get_name(dev));
    if (id < 0 || id >= DEV_ID_COUNT || !s_spi_clients[id].dev)
        return NULL;
    return &s_spi_clients[id];
}
/*===========================================================================================================================================================*/
                                                              /* Bus ops wrappers */
/*===========================================================================================================================================================*/
/* 前向声明: s_spi_controller_ops 引用 impl 函数, 但 impl 定义在 ops 表之后 */
static int  spi_host_init_impl(struct device* dev, const void* cfg);
static int  spi_host_deinit_impl(struct device* dev);
static int  spi_host_role_impl(struct device* dev);
static int  spi_client_register_impl(struct device* dev, const void* cfg, void** out);
static void spi_client_unregister_impl(struct device* dev);

static int spi_bus_ops_open(void* ctx)
{
    struct spi_bus_client* cli = (struct spi_bus_client*)ctx;
    if (!cli || !cli->dev)
        return VFS_ERR_INVAL;
    return spi_bus_open(cli->dev);
}

static int spi_bus_ops_close(void* ctx)
{
    struct spi_bus_client* cli = (struct spi_bus_client*)ctx;
    if (!cli || !cli->dev)
        return VFS_ERR_INVAL;
    return spi_bus_close(cli->dev);
}

static int spi_bus_ops_transfer(void* ctx, const void* tx, void* rx,size_t len, uint32_t timeout_ms)
{
    struct spi_bus_client* cli = (struct spi_bus_client*)ctx;
    if (!cli || !cli->dev)
        return VFS_ERR_INVAL;
    return spi_bus_transfer(cli->dev, (const uint8_t*)tx, (uint8_t*)rx, len, timeout_ms);
}

static int spi_bus_ops_write(void* ctx, const void* data, size_t len,uint32_t timeout_ms)
{
    struct spi_bus_client* cli = (struct spi_bus_client*)ctx;
    if (!cli || !cli->dev)
        return VFS_ERR_INVAL;
    return spi_bus_transfer(cli->dev, (const uint8_t*)data, NULL, len, timeout_ms);
}

static int spi_bus_ops_read(void* ctx, void* data, size_t len,uint32_t timeout_ms)
{
    struct spi_bus_client* cli = (struct spi_bus_client*)ctx;
    if (!cli || !cli->dev)
        return VFS_ERR_INVAL;
    return spi_bus_transfer(cli->dev, NULL, (uint8_t*)data, len, timeout_ms);
}

static int spi_bus_ops_ioctl(void* ctx, int cmd, void* arg, size_t arg_len)
{
    COMPAT_IGNORE_RESULT(ctx);
    COMPAT_IGNORE_RESULT(cmd);
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(arg_len);
    return VFS_ERR_INVAL;
}

static const struct bus_ops s_spi_bus_ops = {
    .open      = spi_bus_ops_open,
    .close     = spi_bus_ops_close,
    .transfer  = spi_bus_ops_transfer,
    .write     = spi_bus_ops_write,
    .read      = spi_bus_ops_read,
    .ioctl     = spi_bus_ops_ioctl,
};

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
static int spi_host_init_impl(struct device* dev, const void* cfg)
{
    const struct spi_bus_host_config* host_cfg;
    struct spi_bus_host*    host;
    struct hal_spi_bus_config hal_cfg;
    int                     idx;
    int                     ret;

    if (!dev || !cfg)
        return VFS_ERR_INVAL;

    host_cfg = (const struct spi_bus_host_config*)cfg;

    if (spi_host_from_device(dev))
        return VFS_OK;

    idx = spi_host_pool_claim();
    if (idx < 0)
        return VFS_ERR_NOMEM;

    host = &s_spi_hosts[idx];
    host->dev = dev;
    atomic_init(&host->ref_count, 0);

    __builtin_memset(&hal_cfg, 0, sizeof(hal_cfg));
    hal_cfg.host_id        = host_cfg->host_id;
    hal_cfg.mosi           = HAL_MAKE_PIN((host_cfg->mosi_pin >> 16) & 0xFFFF, host_cfg->mosi_pin & 0xFFFF);
    hal_cfg.miso           = HAL_MAKE_PIN((host_cfg->miso_pin >> 16) & 0xFFFF, host_cfg->miso_pin & 0xFFFF);
    hal_cfg.sclk           = HAL_MAKE_PIN((host_cfg->sclk_pin >> 16) & 0xFFFF, host_cfg->sclk_pin & 0xFFFF);
    hal_cfg.max_transfer_sz = host_cfg->max_transfer_sz;
    hal_cfg.dma_chan       = host_cfg->dma_chan;
    hal_cfg.bus_role       = host_cfg->bus_role == SPI_BUS_ROLE_MASTER ?
                              HAL_SPI_BUS_ROLE_MASTER : HAL_SPI_BUS_ROLE_SLAVE;

    /* ceiling: DTS 配置不得超过 HAL 静态缓冲区上限 */
    if (hal_cfg.max_transfer_sz > (int)HAL_SPI_MAX_TRANSFER_BYTES)
    {
        SYS_LOGW(kTag, "max_transfer_sz %d exceeds limit %d, clamped",
                 hal_cfg.max_transfer_sz, (int)HAL_SPI_MAX_TRANSFER_BYTES);
        hal_cfg.max_transfer_sz = (int)HAL_SPI_MAX_TRANSFER_BYTES;
    }

    ret = hal_spi_bus_host_init(host_cfg->host_id, &hal_cfg);
    if (ret != VFS_OK)
    {
        spi_host_pool_release(idx);
        return ret;
    }

    ret = hal_spi_bus_host_get(host_cfg->host_id, &host->hal_host);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(host_cfg->host_id));
        spi_host_pool_release(idx);
        return ret;
    }

    ret = bus_controller_bind_full(dev, BUS_TYPE_SPI, &s_spi_bus_ops,
                                   &s_spi_controller_ops, host);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(host_cfg->host_id));
        spi_host_pool_release(idx);
        return ret;
    }

    SYS_LOGI(kTag, "host init OK: %s role=%s host=%d",
             device_get_name(dev),
             host_cfg->bus_role == SPI_BUS_ROLE_SLAVE ? "slave" : "master",
             host_cfg->host_id);
    return VFS_OK;
}

int spi_bus_host_init(struct device* dev, const struct spi_bus_host_config* cfg)
{
    return spi_host_init_impl(dev, cfg);
}

static int spi_host_deinit_impl(struct device* dev)
{
    struct spi_bus_host* host;
    int                  host_id;
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

    host_id = host->hal_host ? host->hal_host->cfg.host_id : -1;
    idx = (int)(host - s_spi_hosts);

    bus_controller_unbind(dev);

    if (host_id >= 0)
        ret = hal_spi_bus_host_deinit(host_id);
    else
        ret = VFS_OK;

    if (ret == VFS_OK)
        spi_host_pool_release(idx);
    return ret;
}

int spi_bus_host_deinit(struct device* dev)
{
    return spi_host_deinit_impl(dev);
}

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
    if (!host || !host->hal_host)
        return -1;

    return host->hal_host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER ?
           SPI_BUS_ROLE_MASTER : SPI_BUS_ROLE_SLAVE;
}

int spi_bus_host_role(struct device* dev)
{
    return spi_host_role_impl(dev);
}
/*===========================================================================================================================================================*/
                                                              /* Client API */
/*===========================================================================================================================================================*/
static int spi_client_register_impl(struct device* dev,const void* cfg, void** out)
{
    const struct spi_bus_client_config* client_cfg;
    struct bus_controller* ctlr;
    struct spi_bus_host*   host;
    struct spi_bus_client* client;
    int                    id;

    if (!dev || !cfg || !out)
        return VFS_ERR_INVAL;
    *out = NULL;

    client_cfg = (const struct spi_bus_client_config*)cfg;

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

int spi_bus_client_register(struct device* dev, const struct spi_bus_client_config* cfg,struct spi_bus_client** out)
{
    return spi_client_register_impl(dev, cfg, (void**)out);
}

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

void spi_bus_client_unregister(struct device* dev)
{
    spi_client_unregister_impl(dev);
}
/*===========================================================================================================================================================*/

                                                              /* Open / Close */
/*===========================================================================================================================================================*/
int spi_bus_open(struct device* dev)
{
    struct spi_bus_client*       client;
    struct hal_spi_device_config dev_cfg;
    int                          ret;

    client = spi_client_from_device(dev);
    if (!client)
        return VFS_ERR_NODEV;

    if (client->hw_open)
        return VFS_OK;

    dev_cfg.mode           = client->cfg.mode;
    dev_cfg.clock_speed_hz = client->cfg.clock_speed_hz;
    dev_cfg.cs_pin         = HAL_MAKE_PIN((client->cfg.cs_pin >> 16) & 0xFFFF,
                                           client->cfg.cs_pin & 0xFFFF);
    dev_cfg.queue_size     = client->cfg.queue_size > 0 ? client->cfg.queue_size : 4;

    hal_spi_dev_init(&client->hal_dev, (int)(client - s_spi_clients),
                     client->host->hal_host, &dev_cfg);
    ret = hal_spi_dev_hw_open(&client->hal_dev);
    if (ret != VFS_OK)
        return ret;

    client->hw_open = 1;
    return VFS_OK;
}

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
int spi_bus_transfer(struct device* dev, const uint8_t* tx, uint8_t* rx,size_t len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev || len == 0)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    return spi_sync(&client->hal_dev, tx, rx, len, timeout_ms);
}

/*===========================================================================================================================================================*/
                                                              /* Async / Slave API (st/ch 不支持) */
/*===========================================================================================================================================================*/
int spi_bus_transfer_async(struct device* dev,const uint8_t* tx, uint8_t* rx,size_t len,void (*cb)(struct device* dev,const void* trans,void* userdata),void* userdata)
{
    COMPAT_IGNORE_RESULT(dev); COMPAT_IGNORE_RESULT(tx); COMPAT_IGNORE_RESULT(rx);
    COMPAT_IGNORE_RESULT(len); COMPAT_IGNORE_RESULT(cb); COMPAT_IGNORE_RESULT(userdata);
    return VFS_ERR_NOTSUPP;
}

int spi_bus_transfer_poll(struct device* dev, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev); COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

int spi_bus_slave_sync(struct device* dev, const uint8_t* tx, uint8_t* rx,size_t len, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev); COMPAT_IGNORE_RESULT(tx); COMPAT_IGNORE_RESULT(rx);
    COMPAT_IGNORE_RESULT(len); COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

int spi_bus_slave_queue_tx(struct device* dev, const uint8_t* data, size_t len,uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev); COMPAT_IGNORE_RESULT(data); COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

int spi_bus_slave_get_trans_result(struct device* dev, uint8_t* rx_data,size_t rx_cap, size_t* trans_len,uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev); COMPAT_IGNORE_RESULT(rx_data); COMPAT_IGNORE_RESULT(rx_cap);
    COMPAT_IGNORE_RESULT(trans_len); COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}
/*===========================================================================================================================================================*/
