/* SPDX-License-Identifier: GPL-2.0-or-later */
/*@=========================================================================================================================*
 * BUS CORE — 总线子系统通用框架层
 *
 * ┌──────────────────────────────────────────────────────────────────────────┐
 * │ 三层架构 (从上到下)                                                       │
 * │                                                                          │
 * │  VFS Layer    (vfs/spi/spi_vfs.c, vfs/uart/uart_vfs.c)                  │
 * │     │  - file_operations 挂载                                            │
 * │     │  - dev_lifecycle (open/close/io 互斥)                             │
 * │     │  - DTS 解析 + driver_register                                      │
 * │     ▼                                                                    │
 * │  Bus Layer    (bus/spi/spi_bus.c, bus/uart/uart_bus.c)                  │
 * │     │  - host/client 池管理                                              │
 * │     │  - atomic ref_count (无锁引用计数)                                 │
 * │     │  - controller_ops (host 级生命周期)                                │
 * │     │  - bus_ops (client 级 open/close/read/write)                      │
 * │     ▼                                                                    │
 * │  HAL Layer    (hal/spi/hal_spi.c, hal/uart/hal_uart.c)                  │
 * │     - 硬件寄存器操作 (ESP32 IDF 封装)                                    │
 * │     - DMA / 中断处理                                                     │
 * │     - opaque handle (对 bus 层隐藏内部结构)                              │
 * └──────────────────────────────────────────────────────────────────────────┘
 *
 * 隔离机制:
 *   - bus 层外部禁止直接调用 hal 层符号 (bus/spi/spi_bus.h, bus/uart/uart_bus.h
 *     用 #pragma GCC poison 强制)
 *   - vfs 层外部禁止直接调用 bus 层符号 (vfs/spi/spi_vfs.h, vfs/uart/uart_vfs.h
 *     用 #pragma GCC poison 强制)
 *
 * 控制器操作分层 (controller_ops vs bus_ops):
 *   - bus_ops:        client 级 I/O 操作 (ctx = client 私有数据)
 *                     open/close/transfer/write/read/ioctl
 *   - controller_ops: host 级生命周期操作 (dev = controller device)
 *                     init/deinit/role/client_register/client_unregister
 *
 * 引用计数 (atomic_int ref_count):
 *   - host->ref_count 由 atomic_fetch_add/sub 维护, 无锁
 *   - client_register 时 +1, client_unregister 时 -1
 *   - host_deinit 检查 ref_count > 0 时返回 VFS_ERR_BUSY, 拒绝销毁
 *   - state 变更 (host init/deinit) 由上层 (board_device.c probe/remove) 序列化
 *@=========================================================================================================================*/
#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stddef.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;

/*===========================================================================================================================================================*/
                                                              /* Bus Type (uint16 bitmap) */
/*===========================================================================================================================================================*/
typedef uint16_t bus_type_t;

#define BUS_TYPE_SPI    ((bus_type_t)(1U << 0))
#define BUS_TYPE_UART   ((bus_type_t)(1U << 1))
#define BUS_TYPE_I2C    ((bus_type_t)(1U << 2))
#define BUS_TYPE_I2S    ((bus_type_t)(1U << 3))
#define BUS_TYPE_CAN    ((bus_type_t)(1U << 4))
#define BUS_TYPE_USB    ((bus_type_t)(1U << 5))
#define BUS_TYPE_PCIE   ((bus_type_t)(1U << 6))
/*===========================================================================================================================================================*/
                                                              /* Bus Operations */
/*===========================================================================================================================================================*/

/**
 * @brief Client 级 I/O 操作表 — 挂在 client device 上
 *
 * @param ctx  client 私有上下文 (bus_client.cli_ctx, 由 bus_xxx_client_register 设置)
 *
 * 调用路径: VFS fops → bus_xxx_open/close/read/write → bus_ops.open/close/read/write(ctx)
 */
struct bus_ops {
    int  (*open)(void* ctx);
    int  (*close)(void* ctx);
    int  (*transfer)(void* ctx, const void* tx, void* rx, size_t len, uint32_t timeout_ms);
    int  (*write)(void* ctx, const void* data, size_t len, uint32_t timeout_ms);
    int  (*read)(void* ctx, void* data, size_t len, uint32_t timeout_ms);
    int  (*ioctl)(void* ctx, int cmd, void* arg, size_t arg_len);
};

/**
 * @brief Host 级控制器操作表 — 管理控制器生命周期与 client 挂载
 *
 * 与 bus_ops (client 级) 互补:
 *   - bus_ops:        open/close/transfer (ctx = client 私有数据)
 *   - controller_ops: init/deinit/role/client_register/client_unregister (dev = controller device)
 *
 * 设计目的: 将 host 级操作抽象为 ops 表, 实现 host 与 client 解耦,
 *           便于后续扩展异构 HAL 实现 (如 SPI master vs slave 共用同一 bus 框架)
 *
 * @param dev  controller device (host)
 * @param cfg  host/client 配置 (struct xxx_bus_host_config / xxx_bus_client_config)
 * @param out  client_register 输出 client 私有上下文 (可 NULL)
 *
 * @return 成功返回 0, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
 */
struct bus_controller_ops {
    int  (*init)(struct device* dev, const void* cfg);
    int  (*deinit)(struct device* dev);              /* 返回 int, BUSY 时不销毁 */
    int  (*role)(struct device* dev);
    int  (*client_register)(struct device* dev, const void* cfg, void** out);
    void (*client_unregister)(struct device* dev);
};
/*===========================================================================================================================================================*/

                                                              /* Controller / Client */
/*===========================================================================================================================================================*/

/**
 * @brief 总线控制器 (host) 描述符
 *
 * 每个 controller device 对应一个 bus_controller, 由 bus_controller_bind_full 注册.
 * 存储在 s_controllers[device_id] 静态表中, O(1) 查找.
 */
struct bus_controller {
    bus_type_t                          type;       /* 总线类型 (BUS_TYPE_SPI 等) */
    const struct bus_ops*              ops;        /* client 级 ops */
    const struct bus_controller_ops*    ctlr_ops;   /* host 级 ops */
    void*                               hw_ctx;     /* host 私有上下文 (struct xxx_bus_host*) */
};

/**
 * @brief 总线客户端 (client) 描述符
 *
 * 每个 client device 对应一个 bus_client, 由 bus_client_bind 注册.
 * 存储在 s_clients[device_id] 静态表中, O(1) 查找.
 * cli_ctx 指向 bus_xxx_client 结构 (由 bus_xxx_client_register 分配).
 */
struct bus_client {
    struct bus_controller*  ctrl;       /* 指向所属 host 的 bus_controller */
    void*                   cli_ctx;    /* client 私有上下文 (struct xxx_bus_client*) */
};
/*===========================================================================================================================================================*/

                                                              /* Controller API */
/*===========================================================================================================================================================*/

/**
 * @brief 绑定 controller (full, 带 ctlr_ops)
 *
 * 将 host device 注册为总线控制器, 存入 s_controllers[device_id].
 * 后续 bus_client_bind 通过 device parent 查找 controller.
 *
 * @param dev       controller device (host)
 * @param type      总线类型 (BUS_TYPE_SPI 等)
 * @param ops       client 级 ops (可 NULL, 由 controller_ops.client_register 内部设置)
 * @param ctlr_ops  host 级 ops
 * @param hw_ctx    host 私有上下文 (struct xxx_bus_host*)
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  bus_controller_bind_full(struct device* dev, bus_type_t type,
                              const struct bus_ops* ops,
                              const struct bus_controller_ops* ctlr_ops,
                              void* hw_ctx)
    COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 查找 client 所属的 controller
 *
 * 通过 device_get_parent(dev) 找到 host, 再从 s_controllers 取出 bus_controller.
 * 用于 client device 探测其所属 host.
 *
 * @param dev  client device
 * @param out  输出 bus_controller 指针
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV
 */
int  bus_controller_of(const struct device* dev, struct bus_controller** out)
    COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 解绑 controller
 *
 * 清空 s_controllers[device_id], 不检查 ref_count.
 * 调用者 (bus_xxx_host_deinit) 应先检查 ref_count > 0 拒绝解绑.
 */
void bus_controller_unbind(struct device* dev);
/*===========================================================================================================================================================*/

                                                              /* Client API */
/*===========================================================================================================================================================*/

/**
 * @brief 绑定 client 到 controller
 *
 * 将 child device 注册为 controller 的客户端, 存入 s_clients[child_id].
 * cli_ctx 为 client 私有上下文, 后续 bus_client_priv 取出.
 *
 * @param child       client device
 * @param controller  controller device (host, 通常为 child 的 parent)
 * @param cli_ctx     client 私有上下文 (struct xxx_bus_client*)
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  bus_client_bind(struct device* child, struct device* controller, void* cli_ctx)
    COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 取出 client 私有上下文
 *
 * 从 s_clients[child_id].cli_ctx 取出, 供 bus_ops 回调使用.
 *
 * @param child  client device
 * @param out    输出 cli_ctx 指针
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV
 */
int  bus_client_priv(const struct device* child, void** out)
    COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 解绑 client
 *
 * 清空 s_clients[child_id].
 * 调用者 (bus_xxx_client_unregister) 应先释放 client 资源.
 */
void bus_client_unbind(const struct device* child);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* BUS_H */
