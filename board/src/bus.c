/*@=========================================================================================================================*
 * BUS CORE 实现 — 总线子系统通用框架
 *
 * 静态表设计:
 *   - s_controllers[DEV_ID_COUNT]:  controller 描述符表 (host 级, 按 device_id 索引)
 *   - s_clients[DEV_ID_COUNT]:      client 描述符表 (client 级, 按 device_id 索引)
 *   - s_controller_used[]/s_client_used[]:  使用位图, O(1) 查找
 *
 * 查找路径:
 *   - controller: device_get_name(dev) → board_dev_find(name) → device_id → s_controllers[id]
 *   - client:     同上, 但 client 通常通过 device_get_parent(dev) 先找 controller
 *
 * 线程安全:
 *   - 本层无锁 (s_controllers/s_clients 写入由上层 probe/remove 序列化)
 *   - ref_count 在 bus_xxx 层用 atomic_int 保护
 *   - 若未来需要并发 client_register, 应在 bus_xxx 层加 mutex
 *@=========================================================================================================================*/
#include "bus.h"
#include "device.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "board_devtable.h"

#include <stddef.h>
#include <string.h>

static struct bus_controller s_controllers[DEV_ID_COUNT] COMPAT_ALIGNED(4);
static struct bus_client     s_clients[DEV_ID_COUNT] COMPAT_ALIGNED(4);
static uint8_t               s_controller_used[DEV_ID_COUNT] COMPAT_ALIGNED(4);
static uint8_t               s_client_used[DEV_ID_COUNT] COMPAT_ALIGNED(4);

/*@=========================================================================================================================*
 * device → device_id 转换
 *
 * 通过 board_dev_find(name) linear scan, O(n).
 * 返回 (device_id_t)-1 表示未找到 (-fshort-enums 下为 0xFF, 不能用 (int)id < 0 判断).
 *@=========================================================================================================================*/
static device_id_t device_to_id(const struct device* dev)
{
    if (!dev || !dev->node)
        return (device_id_t)-1;
    return board_dev_find(device_get_name(dev));
}

/*@=========================================================================================================================*
 * bus_controller_bind_full — 绑定 controller (full, 带 ctlr_ops)
 *
 * 将 host device 注册为总线控制器, 存入 s_controllers[device_id].
 * 后续 bus_client_bind 通过 device parent 查找 controller.
 *
 * @param dev       controller device (host)
 * @param type      总线类型 (BUS_TYPE_SPI 等)
 * @param ops       client 级 ops (可 NULL, 由 controller_ops.client_register 内部设置)
 * @param ctlr_ops  host 级 ops (可 NULL, 老驱动兼容)
 * @param hw_ctx    host 私有上下文 (struct xxx_bus_host*)
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 *@=========================================================================================================================*/
int bus_controller_bind_full(struct device* dev, bus_type_t type,
                             const struct bus_ops* ops,
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
    s_controllers[id].ops      = ops;
    s_controllers[id].ctlr_ops = ctlr_ops;
    s_controllers[id].hw_ctx   = hw_ctx;
    s_controller_used[id]      = 1;
    return VFS_OK;
}

/*@=========================================================================================================================*
 * bus_controller_of — 查找 client 所属的 controller
 *
 * 通过 device_get_parent(dev) 找到 host, 再从 s_controllers 取出 bus_controller.
 * 用于 client device 探测其所属 host.
 *
 * @param dev  client device
 * @param out  输出 bus_controller 指针
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV
 *@=========================================================================================================================*/
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

/*@=========================================================================================================================*
 * bus_client_bind — 绑定 client 到 controller
 *
 * 将 child device 注册为 controller 的客户端, 存入 s_clients[child_id].
 * cli_ctx 为 client 私有上下文, 后续 bus_client_priv 取出.
 *
 * @param child       client device
 * @param controller  controller device (host, 通常为 child 的 parent)
 * @param cli_ctx     client 私有上下文 (struct xxx_bus_client*)
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 *@=========================================================================================================================*/
int bus_client_bind(struct device* child, struct device* controller, void* cli_ctx)
{
    device_id_t        child_id;
    device_id_t        ctl_id;
    struct bus_controller* ctrl;

    if (!child || !controller)
        return VFS_ERR_INVAL;

    child_id = device_to_id(child);
    if (child_id == (device_id_t)-1 || (int)child_id >= DEV_ID_COUNT)
        return VFS_ERR_INVAL;

    ctl_id = device_to_id(controller);
    if (ctl_id == (device_id_t)-1 || (int)ctl_id >= DEV_ID_COUNT || !s_controller_used[ctl_id])
        return VFS_ERR_NODEV;

    /* parent 一致性: child 必须是 controller 的子设备 (防止跨树挂载).
     * device_get_parent 返回 ERR_PTR 时 != controller, 自动走 INVAL 分支. */
    if (device_get_parent(child) != controller)
        return VFS_ERR_INVAL;

    ctrl = &s_controllers[ctl_id];
    s_clients[child_id].ctrl    = ctrl;
    s_clients[child_id].cli_ctx = cli_ctx;
    s_client_used[child_id]     = 1;
    return VFS_OK;
}

/*@=========================================================================================================================*
 * bus_client_priv — 取出 client 私有上下文
 *
 * 从 s_clients[child_id].cli_ctx 取出, 供 bus_ops 回调使用.
 *
 * @param child  client device
 * @param out    输出 cli_ctx 指针
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV
 *@=========================================================================================================================*/
int bus_client_priv(const struct device* child, void** out)
{
    device_id_t id;

    if (!out)
        return VFS_ERR_INVAL;
    *out = NULL;

    if (!child)
        return VFS_ERR_INVAL;

    id = device_to_id(child);
    if (id == (device_id_t)-1 || (int)id >= DEV_ID_COUNT || !s_client_used[id])
        return VFS_ERR_NODEV;

    *out = s_clients[id].cli_ctx;
    return VFS_OK;
}

/*@=========================================================================================================================*
 * bus_controller_unbind — 解绑 controller
 *
 * 清空 s_controllers[device_id], 不检查 ref_count.
 * 调用者 (bus_xxx_host_deinit) 应先检查 ref_count > 0 拒绝解绑.
 *@=========================================================================================================================*/
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

/*@=========================================================================================================================*
 * bus_client_unbind — 解绑 client
 *
 * 清空 s_clients[child_id].
 * 调用者 (bus_xxx_client_unregister) 应先释放 client 资源.
 *@=========================================================================================================================*/
void bus_client_unbind(const struct device* child)
{
    device_id_t id;

    if (!child)
        return;

    id = device_to_id(child);
    if (id == (device_id_t)-1 || (int)id >= DEV_ID_COUNT)
        return;

    s_client_used[id] = 0;
    __builtin_memset(&s_clients[id], 0, sizeof(s_clients[id]));
}
