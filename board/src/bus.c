/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file bus.c
 *@brief bus 实现
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   BUS CORE 实现 — 总线子系统通用框架
 *   静态表: s_controllers[DEV_ID_COUNT] (按 device_id 索引) + s_controller_used[] 位图
 *   查找: device_get_name → board_dev_find → device_id → s_controllers[id]
 *   线程安全: 本层无锁 (写入由上层 probe/remove 序列化); ref_count 在 bus_xxx 层用
 *   atomic_int 保护; 并发 client_register 需在 bus_xxx 层加 mutex
 *   --------------------------------------------------------------------------
 */

#include "bus.h"

#include "board_devtable.h"
#include "compiler_compat.h"
#include "device.h"
#include "status.h"
#include <stddef.h>
#include <string.h>

static struct bus_controller s_controllers[DEV_ID_COUNT] MINI_ALIGNED(4);
static uint8_t s_controller_used[DEV_ID_COUNT] MINI_ALIGNED(4);

/**
 * @brief 将 device 转换为 device_id (通过 board_dev_find 线性扫描)
 * @param[in] pdev 输入的 device 指针
 * @return 找到返回 device_id, 未找到返回 (device_id_t)-1
 */
static device_id_t device_to_id(const struct device* pdev)
{
    if (!pdev || !pdev->node)
        return (device_id_t)-1;
    return board_dev_find(device_get_name(pdev));
}

/**
 * @brief 绑定 device 到总线控制器静态表
 * @param[in] pdev controller device 指针
 * @param[in] type 总线类型
 * @param[in] ctlr_ops 控制器操作表
 * @param[in] hw_ctx 硬件上下文指针
 * @return 成功返回 MINI_OK, 失败返回负数错误码
 */
int bus_controller_bind_full(struct device* pdev, bus_type_t type,
                             const struct bus_controller_ops* ctlr_ops, void* hw_ctx)
{
    device_id_t id;

    if (!pdev || type == 0)
        return MINI_ERR_INVAL;

    id = device_to_id(pdev);
    if (id == (device_id_t)-1 || (int)id >= DEV_ID_COUNT)
        return MINI_ERR_INVAL;

    s_controllers[id].type = type;
    s_controllers[id].ctlr_ops = ctlr_ops;
    s_controllers[id].hw_ctx = hw_ctx;
    s_controller_used[id] = 1;
    return MINI_OK;
}

/**
 * @brief 获取 device 自身绑定的总线控制器
 * @param[in] pdev device 指针
 * @param[out] out 输出 bus_controller 指针
 * @return 成功返回 MINI_OK, 失败返回负数错误码
 */
int bus_controller_get(const struct device* pdev, struct bus_controller** out)
{
    device_id_t id;

    if (!out)
        return MINI_ERR_INVAL;
    *out = NULL;
    if (!pdev)
        return MINI_ERR_INVAL;

    id = device_to_id(pdev);
    if (id == (device_id_t)-1 || (int)id >= DEV_ID_COUNT || !s_controller_used[id])
        return MINI_ERR_NODEV;

    *out = &s_controllers[id];
    return MINI_OK;
}

/**
 * @brief 获取 device 父节点绑定的总线控制器 (client 查 host)
 * @param[in] pdev client device 指针
 * @param[out] out 输出 bus_controller 指针
 * @return 成功返回 MINI_OK, 失败返回负数错误码
 */
int bus_controller_of(const struct device* pdev, struct bus_controller** out)
{
    struct device* parent;
    device_id_t id;

    if (!out)
        return MINI_ERR_INVAL;
    *out = NULL;

    if (!pdev)
        return MINI_ERR_INVAL;

    parent = device_get_parent(pdev);
    if (IS_ERR(parent))
        return PTR_ERR(parent);

    id = device_to_id(parent);
    if (id == (device_id_t)-1 || (int)id >= DEV_ID_COUNT || !s_controller_used[id])
        return MINI_ERR_NODEV;

    *out = &s_controllers[id];
    return MINI_OK;
}

/**
 * @brief 解绑 device 的总线控制器并清零槽位
 * @param[in] pdev controller device 指针
 */
void bus_controller_unbind(struct device* pdev)
{
    device_id_t id;

    if (!pdev)
        return;

    id = device_to_id(pdev);
    if (id == (device_id_t)-1 || (int)id >= DEV_ID_COUNT)
        return;

    s_controller_used[id] = 0;
    MINI_MEM_SET(&s_controllers[id], 0, sizeof(s_controllers[id]));
}

/* -------------------------------------------------------------------------- */
/* Async callback bridge */
/* -------------------------------------------------------------------------- */
/**
 * @brief 从异步桥接池申请空闲槽位
 * @param slots 桥接槽位数组
 * @param slot_count 槽位数量
 * @return 成功返回 bridge 指针, 池满返回 NULL
 */
struct bus_async_bridge* bus_async_bridge_claim(struct bus_async_bridge* slots, size_t slot_count)
{
    size_t index;

    if (!slots || slot_count == 0)
        return NULL;

    for (index = 0; index < slot_count; index++)
    {
        if (!slots[index].in_use)
        {
            slots[index].in_use = 1;
            return &slots[index];
        }
    }
    return NULL;
}

/**
 * @brief 绑定异步桥接槽位到用户回调
 * @param[in] bridge bridge 指针
 * @param[in] pdev 关联 device 指针
 * @param[in] cb 用户完成回调
 * @param[in] userdata 回调用户数据
 */
void bus_async_bridge_bind(struct bus_async_bridge* bridge, struct device* pdev,
                           bus_async_user_cb_t cb, void* userdata)
{
    if (!bridge)
        return;
    bridge->pdev = pdev;
    bridge->cb = cb;
    bridge->userdata = userdata;
}

void bus_async_bridge_release(struct bus_async_bridge* bridge)
{
    if (!bridge)
        return;
    bridge->pdev = NULL;
    bridge->cb = NULL;
    bridge->userdata = NULL;
    bridge->in_use = 0;
}

void bus_async_bridge_complete(void* userdata, const void* trans)
{
    struct bus_async_bridge* bridge = (struct bus_async_bridge*)userdata;

    if (!bridge)
        return;

    if (bridge->cb)
        bridge->cb(bridge->pdev, trans, bridge->userdata);

    /* ISR 安全: 单字节写释放槽位 */
    bridge->in_use = 0;
}
