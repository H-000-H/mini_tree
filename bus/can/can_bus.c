/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file can_bus.c
 *@brief can bus 实现
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   CAN BUS 实现 — CAN 总线子系统 bus 层 (平台中立共享代码)
 *   静态池: s_can_hosts[HOST_MAX] (含 hal_host, ref_count) + s_can_clients[DEV_ID_COUNT]
 *   数据流:
 *   同步: VFS → can_bus_open/close/transmit|receive|filter → hal_can_*
 *   controller_ops 表注册到 bus_controller_bind_full; impl 实现逻辑, public 函数转发
 *   @=========================================================================================================================
 */

#define CAN_BUS_IMPL
#include "can_bus.h"

#include "board_devtable.h"
#include "bus.h"
#include "compiler_compat.h"
#include "device.h"
#include "hal_can.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

#define CAN_BUS_HOST_MAX 2 /* 对齐 HAL/DTS host-max (CAN1/CAN2) */

/** @brief CAN host 运行时描述符 (静态池, 含 HAL 嵌入 + atomic ref_count) */
struct can_bus_host
{
    struct device* pdev; /**< 关联设备 */
    struct hal_can_bus_host hal_host; /**< 嵌入 HAL host (非指针) */
    COMPAT_ATOMIC_INT ref_count; /**< atomic 引用计数 */
};

/** @brief CAN client 运行时描述符 (静态表, 按 device_id 索引) */
struct can_bus_client
{
    struct device* pdev; /**< 关联设备 */
    struct can_bus_host* host; /**< 所属 host */
    struct hal_can_dev hal_dev; /**< HAL 设备对象 */
    int hw_open; /**< 硬件打开计数 */
};

static struct can_bus_host s_can_hosts[CAN_BUS_HOST_MAX];
static uint8_t s_can_host_used[CAN_BUS_HOST_MAX];
static osal_pool_t s_can_host_pool_ctrl;
static struct can_bus_client s_can_clients[DEV_ID_COUNT];
static const char* const k_tag = "can_bus";

/**
 * @brief CAN Host 池启动初始化
 */
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void can_bus_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_can_host_pool_ctrl, s_can_host_used, CAN_BUS_HOST_MAX));
}

/*===========================================================================================================================================================*/
/* Host pool helpers */
/*===========================================================================================================================================================*/
/**
 * @brief 通过 device 指针查找对应的 can_bus_host
 * @param[in] pdev host device 指针
 * @return 找到返回 host 指针, 未找到返回 NULL
 */
static struct can_bus_host* can_host_from_device(struct device* pdev)
{
    for (int i = 0; i < CAN_BUS_HOST_MAX; i++)
        if (osal_pool_is_used(&s_can_host_pool_ctrl, i) && s_can_hosts[i].pdev == pdev)
            return &s_can_hosts[i];
    return NULL;
}

/**
 * @brief 通过 device 指针查找对应的 can_bus_client
 * @param[in] pdev client device 指针
 * @return 找到返回 client 指针, 未找到返回 NULL
 */
static struct can_bus_client* can_client_from_device(struct device* pdev)
{
    int id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT || !s_can_clients[id].pdev)
        return NULL;
    return &s_can_clients[id];
}

/*===========================================================================================================================================================*/
/* controller_ops (host 级操作) */
/*===========================================================================================================================================================*/
static int can_host_init_impl(struct device* pdev, const void* cfg);
static int can_host_deinit_impl(struct device* pdev);
static int can_host_role_impl(struct device* pdev);
static int can_client_register_impl(struct device* pdev, const void* cfg, void** out);
static void can_client_unregister_impl(struct device* pdev);

/**
 * @brief CAN 总线控制器操作表
 */
static const struct bus_controller_ops s_can_controller_ops = {
    .init = can_host_init_impl,
    .deinit = can_host_deinit_impl,
    .role = can_host_role_impl,
    .client_register = can_client_register_impl,
    .client_unregister = can_client_unregister_impl,
};

/**
 * @brief CAN 总线主机初始化实现
 * @param[in] pdev host device 指针
 * @param[in] cfg host 配置指针
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int can_host_init_impl(struct device* pdev, const void* cfg)
{
    const struct hal_can_bus_config* host_cfg;
    struct can_bus_host* host;
    int idx;
    int ret;

    if (!pdev || !cfg)
        return MINI_ERR_INVAL;

    host_cfg = (const struct hal_can_bus_config*)cfg;

    if (can_host_from_device(pdev))
        return MINI_OK;

    idx = osal_pool_claim(&s_can_host_pool_ctrl);
    if (idx < 0)
        return MINI_ERR_NOMEM;

    host = &s_can_hosts[idx];

    COMPAT_MEM_SET(host, 0, sizeof(*host));

    host->pdev = pdev;

    COMPAT_ATOMIC_RUNTIME_INIT(&host->ref_count, 0);

    ret = hal_can_bus_host_init(&host->hal_host, idx, host_cfg);
    if (ret != MINI_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_can_host_pool_ctrl, idx));
        return ret;
    }

    ret = bus_controller_bind_full(pdev, BUS_TYPE_CAN, &s_can_controller_ops, host);
    if (ret != MINI_OK)
    {
        COMPAT_IGNORE_RESULT(hal_can_bus_host_deinit(&host->hal_host));
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_can_host_pool_ctrl, idx));
        return ret;
    }

    return MINI_OK;
}

int can_bus_host_init(struct device* pdev, const struct hal_can_bus_config* cfg)
{
    return can_host_init_impl(pdev, cfg);
}

/**
 * @brief CAN 总线主机销毁实现
 * @param[in] pdev host device 指针
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int can_host_deinit_impl(struct device* pdev)
{
    struct can_bus_host* host;
    int idx;
    int ret;

    if (!pdev)
        return MINI_ERR_INVAL;

    host = can_host_from_device(pdev);
    if (!host)
        return MINI_ERR_NODEV;

    if (COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST) != 0)
    {
        SYS_LOGW(k_tag, "host deinit busy: ref_count=%d",
                 COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST));
        return MINI_ERR_BUSY;
    }

    idx = (int)(host - s_can_hosts);
    bus_controller_unbind(pdev);

    ret = hal_can_bus_host_deinit(&host->hal_host);
    if (ret == MINI_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_can_host_pool_ctrl, idx));
    }
    return ret;
}

int can_bus_host_deinit(struct device* pdev) { return can_host_deinit_impl(pdev); }

/**
 * @brief 查询 host 角色 (CAN 无 master/slave, 固定返回 0)
 * @param[in] pdev host 或 client device 指针
 * @return 固定返回 0 (CAN 为对等总线)
 */
static int can_host_role_impl(struct device* pdev)
{
    COMPAT_IGNORE_RESULT(pdev);
    return 0; /* CAN 为对等总线, 无 master/slave 概念 */
}

/**
 * @brief CAN 总线客户端注册实现
 * @param[in] pdev client device 指针
 * @param[in] cfg 未使用 (CAN 无设备级配置)
 * @param[out] out 输出 client 指针
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
static int can_client_register_impl(struct device* pdev, const void* cfg, void** out)
{
    struct bus_controller* ctlr;
    struct can_bus_host* host;
    struct can_bus_client* client;
    int id;

    COMPAT_IGNORE_RESULT(cfg);

    if (!pdev || !out)
        return MINI_ERR_INVAL;

    if (bus_controller_of(pdev, &ctlr) != MINI_OK)
        return MINI_ERR_NODEV;

    host = (struct can_bus_host*)ctlr->hw_ctx;
    if (!host)
        return MINI_ERR_IO;

    id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT)
        return MINI_ERR_INVAL;

    client = &s_can_clients[id];

    if (client->pdev)
    {
        if (client->pdev != pdev)
            return MINI_ERR_BUSY;
        *out = client;
        return MINI_OK;
    }

    COMPAT_MEM_SET(client, 0, sizeof(*client));
    client->pdev = pdev;
    client->host = host;

    (void)COMPAT_ATOMIC_FETCH_ADD(&host->ref_count, 1, COMPAT_MO_SEQ_CST);

    *out = client;
    return MINI_OK;
}

int can_bus_client_register(struct device* pdev, struct can_bus_client** out)
{
    return can_client_register_impl(pdev, NULL, (void**)out);
}

/**
 * @brief CAN 总线客户端销毁实现 (关 hw / 减 host 引用 / 清槽)
 * @param[in] pdev client device 指针
 */
static void can_client_unregister_impl(struct device* pdev)
{
    struct can_bus_client* client;
    struct can_bus_host* host;

    client = can_client_from_device(pdev);
    if (!client)
        return;

    if (client->hw_open)
    {
        COMPAT_IGNORE_RESULT(can_bus_close(pdev));
        client->hw_open = 0;
    }

    host = client->host;
    if (host)
        (void)COMPAT_ATOMIC_FETCH_SUB(&host->ref_count, 1, COMPAT_MO_SEQ_CST);

    COMPAT_MEM_SET(client, 0, sizeof(*client));
}

void can_bus_client_unregister(struct device* pdev) { can_client_unregister_impl(pdev); }

int can_bus_open(struct device* pdev)
{
    struct can_bus_client* client;
    int ret;

    client = can_client_from_device(pdev);
    if (!client)
        return MINI_ERR_NODEV;

    if (client->hw_open)
        return MINI_OK;

    COMPAT_IGNORE_RESULT(hal_can_dev_init(&client->hal_dev, &client->host->hal_host));
    ret = hal_can_dev_hw_open(&client->hal_dev);
    if (ret != MINI_OK)
        return ret;

    client->hw_open = 1;
    return MINI_OK;
}

int can_bus_close(struct device* pdev)
{
    struct can_bus_client* client;

    client = can_client_from_device(pdev);
    if (!client)
        return MINI_ERR_NODEV;

    if (client->hw_open)
    {
        COMPAT_IGNORE_RESULT(hal_can_dev_hw_close(&client->hal_dev));
        client->hw_open = 0;
    }
    return MINI_OK;
}

int can_bus_transmit(struct device* pdev, const struct can_frame* frame, uint32_t timeout_ms)
{
    struct can_bus_client* client;

    if (!pdev || !frame)
        return MINI_ERR_INVAL;

    client = can_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    return hal_can_transmit(&client->hal_dev, frame, timeout_ms);
}

int can_bus_receive(struct device* pdev, struct can_frame* frame, uint32_t fifo,
                    uint32_t timeout_ms)
{
    struct can_bus_client* client;

    if (!pdev || !frame)
        return MINI_ERR_INVAL;

    client = can_client_from_device(pdev);
    if (!client || !client->hw_open)
        return MINI_ERR_NODEV;

    return hal_can_receive(&client->hal_dev, frame, fifo, timeout_ms);
}

int can_bus_filter_config(struct device* pdev, const struct hal_can_filter_config* filter)
{
    struct can_bus_client* client;

    if (!pdev || !filter)
        return MINI_ERR_INVAL;

    client = can_client_from_device(pdev);
    if (!client)
        return MINI_ERR_NODEV;

    return hal_can_filter_config(&client->host->hal_host, filter);
}

int can_bus_get_state(struct device* pdev, uint32_t* out_state)
{
    struct can_bus_client* client;

    if (!pdev || !out_state)
        return MINI_ERR_INVAL;

    client = can_client_from_device(pdev);
    if (!client || !client->host)
        return MINI_ERR_NODEV;

    return hal_can_get_state(&client->host->hal_host, out_state);
}
