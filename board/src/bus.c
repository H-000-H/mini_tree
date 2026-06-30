/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * BUS CORE 实现 — 总线子系统通用框架
 *
 * 静态表: s_controllers[DEV_ID_COUNT] (按 device_id 索引) + s_controller_used[] 位图
 * 查找: device_get_name → board_dev_find → device_id → s_controllers[id]
 *
 * 线程安全: 本层无锁 (写入由上层 probe/remove 序列化); ref_count 在 bus_xxx 层用
 *   atomic_int 保护; 并发 client_register 需在 bus_xxx 层加 mutex
 *@=========================================================================================================================*/
#include "bus.h"
#include "device.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "board_devtable.h"

#include <stddef.h>
#include <string.h>

static struct bus_controller s_controllers[DEV_ID_COUNT] COMPAT_ALIGNED(4);
static uint8_t               s_controller_used[DEV_ID_COUNT] COMPAT_ALIGNED(4);

/**
 * @brief 将 device 转换为 device_id (通过 board_dev_find 线性扫描)
 * @param dev 输入的 device 指针
 * @return 找到返回 device_id, 未找到返回 (device_id_t)-1
 */
static device_id_t device_to_id(const struct device* dev)
{
    if (!dev || !dev->node)
        return (device_id_t)-1;
    return board_dev_find(device_get_name(dev));
}

/**
 * @brief 绑定 controller (full, 带 ctlr_ops)
 * @param dev controller device (host)
 * @param type 总线类型 (BUS_TYPE_SPI 等)
 * @param ctlr_ops host 级 ops 表
 * @param hw_ctx host 私有上下文 (struct xxx_bus_host*)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int bus_controller_bind_full(struct device* dev, bus_type_t type,
                             const struct bus_controller_ops* ctlr_ops,
                             void* hw_ctx)
{
    device_id_t id;

    if (!dev || type == 0)
        return VFS_ERR_INVAL;

    id = device_to_id(dev);
    if (id == (device_id_t)-1 || (int)id >= DEV_ID_COUNT)
        return VFS_ERR_INVAL;

    s_controllers[id].type     = type;
    s_controllers[id].ctlr_ops = ctlr_ops;
    s_controllers[id].hw_ctx   = hw_ctx;
    s_controller_used[id]      = 1;
    return VFS_OK;
}

/**
 * @brief 查找 client 所属的 controller
 * @param dev client device
 * @param out 输出 bus_controller 指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL 或 VFS_ERR_NODEV
 */
int bus_controller_of(const struct device* dev, struct bus_controller** out)
{
    struct device* parent;
    device_id_t    id;

    if (!out)
        return VFS_ERR_INVAL;
    *out = NULL;

    if (!dev)
        return VFS_ERR_INVAL;

    parent = device_get_parent(dev);
    if (IS_ERR(parent))
        return PTR_ERR(parent);

    id = device_to_id(parent);
    if (id == (device_id_t)-1 || (int)id >= DEV_ID_COUNT || !s_controller_used[id])
        return VFS_ERR_NODEV;

    *out = &s_controllers[id];
    return VFS_OK;
}

/**
 * @brief 解绑 controller (清空 s_controllers[device_id], 不检查 ref_count)
 * @param dev controller device (host)
 */
void bus_controller_unbind(struct device* dev)
{
    device_id_t id;

    if (!dev)
        return;

    id = device_to_id(dev);
    if (id == (device_id_t)-1 || (int)id >= DEV_ID_COUNT)
        return;

    s_controller_used[id] = 0;
    __builtin_memset(&s_controllers[id], 0, sizeof(s_controllers[id]));
}
