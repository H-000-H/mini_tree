/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file i2s_bus.c
 *@brief i2s bus 实现
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   I2S BUS 实现 — I2S 总线子系统 bus 层
 *   静态池: s_i2s_hosts[HOST_MAX] (含 hal_host, ref_count) + s_i2s_clients[DEV_ID_COUNT]
 *   职责: host init/open/close + client register + transfer (poll/DMA) + circular + HT/TC irq_mode
 *   @see bus/i2s/i2s_bus.h
 *   @=========================================================================================================================
 */

#define I2S_BUS_IMPL
#include "i2s_bus.h"

#include "board_devtable.h"
#include "bus.h"
#include "compiler_compat.h"
#include "device.h"
#include "hal_i2s.h"
#include "interrupt.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

#define I2S_BUS_HOST_MAX 3

/** @brief I2S host 运行时描述符 (静态池, HAL 嵌入 + atomic ref_count) */
struct i2s_bus_host
{
    struct device* pdev; /**< 关联设备 */
    struct hal_i2s_bus_host hal_host; /**< 嵌入 HAL host */
    COMPAT_ATOMIC_INT ref_count; /**< atomic 引用计数 */
};

/** @brief I2S client 运行时描述符 (静态表, 按 device_id 索引) */
struct i2s_bus_client
{
    struct device* pdev; /**< 关联设备 */
    struct i2s_bus_host* host; /**< 所属 host */
    struct hal_i2s_device_config cfg; /**< 设备配置 (DTSI 直投) */
    struct hal_i2s_dev hal_i2s_dev; /**< HAL 设备对象 */
    int hw_open; /**< 硬件打开计数 */
};

static struct i2s_bus_host s_hosts[I2S_BUS_HOST_MAX];
static uint8_t s_host_used[I2S_BUS_HOST_MAX];
static osal_pool_t s_host_pool;
static struct i2s_bus_client s_clients[DEV_ID_COUNT];
static const char* k_tag = "i2s_bus";

/**
 * @brief 初始化 I2S 总线 host 对象池
 */
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void i2s_bus_pool_init(void) { COMPAT_IGNORE_RESULT(osal_pool_init(&s_host_pool, s_host_used, I2S_BUS_HOST_MAX)); }

/**
 * @brief 根据 device 查找已注册的 I2S host
 * @param[in] pdev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct i2s_bus_host* host_from_dev(struct device* pdev)
{
    int i;
    for (i = 0; i < I2S_BUS_HOST_MAX; i++)
        if (osal_pool_is_used(&s_host_pool, i) && s_hosts[i].pdev == pdev)
            return &s_hosts[i];
    return NULL;
}

/**
 * @brief 根据 device 查找已注册的 I2S client
 * @param[in] pdev client device 指针
 * @return 找到返回 client 指针, 未找到返回 NULL
 */
static struct i2s_bus_client* client_from_dev(struct device* pdev)
{
    int id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT || !s_clients[id].pdev)
        return NULL;
    return &s_clients[id];
}

static int i2s_host_init_impl(struct device* pdev, const void* cfg);
static int i2s_host_deinit_impl(struct device* pdev);
static int i2s_host_role_impl(struct device* pdev);
static int i2s_client_register_impl(struct device* pdev, const void* cfg, void** out);
static void i2s_client_unregister_impl(struct device* pdev);

static const struct bus_controller_ops s_ops = {
    .init = i2s_host_init_impl,
    .deinit = i2s_host_deinit_impl,
    .role = i2s_host_role_impl,
    .client_register = i2s_client_register_impl,
    .client_unregister = i2s_client_unregister_impl,
};

/**
 * @brief 初始化 I2S host 并绑定总线控制器
 * @param[in] pdev host device 指针
 * @param[in] cfg host 配置 (struct hal_i2s_bus_config*)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_host_init_impl(struct device* pdev, const void* cfg)
{
    const struct hal_i2s_bus_config* host_cfg = cfg;
    struct i2s_bus_host* host;
    int idx, ret;

    if (!pdev || !cfg)
        return VFS_ERR_INVAL;
    if (host_from_dev(pdev))
        return VFS_OK;
    idx = osal_pool_claim(&s_host_pool);
    if (idx < 0)
        return VFS_ERR_NOMEM;
    host = &s_hosts[idx];
    COMPAT_MEM_SET(host, 0, sizeof(*host));
    host->pdev = pdev;
    COMPAT_ATOMIC_RUNTIME_INIT(&host->ref_count, 0);
    ret = hal_i2s_bus_host_init(&host->hal_host, idx, host_cfg);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_host_pool, idx));
        return ret;
    }
    ret = bus_controller_bind_full(pdev, BUS_TYPE_I2S, &s_ops, host);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(hal_i2s_bus_host_deinit(&host->hal_host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_host_pool, idx));
        return ret;
    }
    SYS_LOGI(k_tag, "host init OK: %s", device_get_name(pdev));
    return VFS_OK;
}

/**
 * @brief 反初始化 I2S host 并释放对象池槽位
 * @param[in] pdev host device 指针
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回负数错误码
 */
static int i2s_host_deinit_impl(struct device* pdev)
{
    struct i2s_bus_host* host;
    int idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    host = host_from_dev(pdev);
    if (!host)
        return VFS_ERR_NODEV;
    if (COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST) != 0)
        return VFS_ERR_BUSY;
    idx = (int)(host - s_hosts);
    bus_controller_unbind(pdev);
    ret = hal_i2s_bus_host_deinit(&host->hal_host);
    if (ret == VFS_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_host_pool, idx));
    }
    return ret;
}

/**
 * @brief 查询 I2S host 总线角色 (master/slave)
 * @param[in] pdev host 或 client device 指针
 * @return master 返回 bus_role 值, 失败返回 -1
 */
static int i2s_host_role_impl(struct device* pdev)
{
    struct bus_controller* ctlr = NULL;
    struct i2s_bus_host* host;
    if (!pdev)
        return -1;
    if (bus_controller_get(pdev, &ctlr) == VFS_OK && ctlr)
        host = ctlr->hw_ctx;
    else if (bus_controller_of(pdev, &ctlr) == VFS_OK && ctlr)
        host = ctlr->hw_ctx;
    else
        return -1;
    if (!host)
        return -1;
    return (int)host->hal_host.cfg.bus_role;
}

/**
 * @brief 注册 I2S client 并增加 host 引用计数
 * @param[in] pdev client device 指针
 * @param[in] cfg client 配置 (struct hal_i2s_device_config*)
 * @param[out] out 输出 client 私有上下文指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int i2s_client_register_impl(struct device* pdev, const void* cfg, void** out)
{
    const struct hal_i2s_device_config* c = cfg;
    struct bus_controller* ctlr;
    struct i2s_bus_host* host;
    struct i2s_bus_client* client;
    int id;

    if (!pdev || !cfg || !out)
        return VFS_ERR_INVAL;
    if (bus_controller_of(pdev, &ctlr) != VFS_OK)
        return VFS_ERR_NODEV;
    host = ctlr->hw_ctx;
    if (!host)
        return VFS_ERR_IO;
    id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT)
        return VFS_ERR_INVAL;
    client = &s_clients[id];
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
    client->cfg = *c;
    (void)COMPAT_ATOMIC_FETCH_ADD(&host->ref_count, 1, COMPAT_MO_SEQ_CST);
    *out = client;
    return VFS_OK;
}

/**
 * @brief 注销 I2S client 并递减 host 引用计数
 * @param[in] pdev client device 指针
 */
static void i2s_client_unregister_impl(struct device* pdev)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client)
        return;
    if (client->hw_open)
        COMPAT_IGNORE_RESULT(i2s_bus_close(pdev));
    if (client->host)
        (void)COMPAT_ATOMIC_FETCH_SUB(&client->host->ref_count, 1, COMPAT_MO_SEQ_CST);
    COMPAT_MEM_SET(client, 0, sizeof(*client));
}

int i2s_bus_host_init(struct device* pdev, const struct hal_i2s_bus_config* cfg) { return i2s_host_init_impl(pdev, cfg); }

int i2s_bus_host_deinit(struct device* pdev) { return i2s_host_deinit_impl(pdev); }

int i2s_bus_host_role(struct device* pdev) { return i2s_host_role_impl(pdev); }

int i2s_bus_client_register(struct device* pdev, const struct hal_i2s_device_config* cfg, struct i2s_bus_client** out) { return i2s_client_register_impl(pdev, cfg, (void**)out); }

void i2s_bus_client_unregister(struct device* pdev) { i2s_client_unregister_impl(pdev); }

int i2s_bus_open(struct device* pdev)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    struct hal_i2s_bus_host* host;
    int ret;

    if (!client || !client->host)
        return VFS_ERR_NODEV;
    if (client->hw_open)
        return VFS_OK;

    ret = hal_i2s_dev_init(&client->hal_i2s_dev, &client->host->hal_host, &client->cfg);
    if (ret != VFS_OK)
        return ret;
    ret = hal_i2s_dev_hw_open(&client->hal_i2s_dev);
    if (ret != VFS_OK)
        return ret;

    host = &client->host->hal_host;
#ifdef CONFIG_VIRQ
    /*
     * 虚拟中断注册 (对齐 ADC vfs_adc_probe; 不走 ioctl):
     *   top  = hal_virtual_i2s_irq_callback
     *   work = &g_i2s_bottom_half_work (fn/arg 在 circular/async 使能 IT 时绑定)
     *   arg  = &client->hal_i2s_dev
     */
    if (host->cfg.it_enable)
    {
        interrupt_virtual_register(VIRQ(i2s, host->hw_idx), hal_virtual_i2s_irq_callback, &g_i2s_bottom_half_work, &client->hal_i2s_dev);
        interrupt_hw_enable((int)host->cfg.irqn, host->cfg.irq_priority);
        interrupt_hw_enable((int)host->cfg.irqn_rx, host->cfg.irq_priority);
    }
#endif

    client->hw_open = 1;
    return VFS_OK;
}

int i2s_bus_close(struct device* pdev)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client)
        return VFS_ERR_NODEV;
    if (!client->hw_open)
        return VFS_OK;
    COMPAT_IGNORE_RESULT(hal_i2s_dev_hw_close(&client->hal_i2s_dev));
    COMPAT_IGNORE_RESULT(hal_i2s_dev_deinit(&client->hal_i2s_dev));
    client->hw_open = 0;
    return VFS_OK;
}

int i2s_bus_transfer(struct device* pdev, const uint16_t* tx, uint16_t* rx, size_t samples, uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;
    return hal_i2s_sync(&client->hal_i2s_dev, tx, rx, samples, timeout_ms, xfer_mode);
}

int i2s_bus_transfer_async(struct device* pdev, const uint16_t* tx, uint16_t* rx, size_t samples, void (*cb)(struct device*, const void*, void*), void* userdata)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    COMPAT_IGNORE_RESULT(cb);
    COMPAT_IGNORE_RESULT(userdata);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;
    return hal_i2s_transfer_async(&client->hal_i2s_dev, tx, rx, samples, NULL, NULL);
}

int i2s_bus_transfer_poll(struct device* pdev, uint32_t timeout_ms)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;
    return hal_i2s_transfer_poll(&client->hal_i2s_dev, timeout_ms);
}

int i2s_bus_set_dma_irq_mode(struct device* pdev, uint32_t irq_mode)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;
    return hal_i2s_set_dma_irq_mode(&client->hal_i2s_dev, irq_mode);
}

int i2s_bus_get_dma_irq_mode(struct device* pdev, uint32_t* irq_mode)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;
    return hal_i2s_get_dma_irq_mode(&client->hal_i2s_dev, irq_mode);
}

int i2s_bus_dma_circ_start(struct device* pdev, int tx_enable, int rx_enable)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;
    return hal_i2s_dma_circ_start(&client->hal_i2s_dev, tx_enable, rx_enable);
}

int i2s_bus_dma_circ_stop(struct device* pdev)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;
    return hal_i2s_dma_circ_stop(&client->hal_i2s_dev);
}

int i2s_bus_dma_circ_write(struct device* pdev, const uint16_t* data, uint32_t samples)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;
    return hal_i2s_dma_circ_write(&client->hal_i2s_dev, data, samples);
}

int i2s_bus_dma_circ_read(struct device* pdev, uint16_t* data, uint32_t samples)
{
    struct i2s_bus_client* client = client_from_dev(pdev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;
    return hal_i2s_dma_circ_read(&client->hal_i2s_dev, data, samples);
}
