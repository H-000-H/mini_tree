/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * BUS CORE — 总线子系统通用框架层
 *
 * 三层架构: VFS (file_operations + dev_lifecycle + DTS) → Bus (host/client 池,
 *   atomic ref_count, controller_ops) → HAL (寄存器/DMA/中断, opaque handle)
 *
 * 隔离 (#pragma GCC poison 强制): bus 外禁止调 hal 符号, vfs 外禁止调 bus 符号
 *
 * 引用计数: host->ref_count atomic, register +1/unregister -1, deinit >0 返回 BUSY;
 *   state 变更由上层 (board_device.c) 序列化
 *
 * controller_ops (host 级): init/deinit/role/client_register/client_unregister;
 *   client 级 I/O 由 bus_xxx_open/close/read/write 直接处理, 不经 ops 表
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
                                                              /* Controller Operations */
/*===========================================================================================================================================================*/

/**
 * @brief Host 级控制器操作表 — 管理控制器生命周期与 client 挂载
 *
 * @param dev  controller device (host)
 * @param cfg  host/client 配置 (struct hal_spi_bus_config / hal_uart_config 等)
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

                                                              /* Controller */
/*===========================================================================================================================================================*/

/**
 * @brief 总线控制器 (host) 描述符
 *
 * 每个 controller device 对应一个 bus_controller, 由 bus_controller_bind_full 注册.
 * 存储在 s_controllers[device_id] 静态表中, O(1) 查找.
 */
struct bus_controller {
    bus_type_t                          type;       /* 总线类型 (BUS_TYPE_SPI 等) */
    const struct bus_controller_ops*    ctlr_ops;   /* host 级 ops */
    void*                               hw_ctx;     /* host 私有上下文 (struct xxx_bus_host*) */
};

/*===========================================================================================================================================================*/

                                                              /* Controller API */
/*===========================================================================================================================================================*/

/**
 * @brief 绑定 controller (full, 带 ctlr_ops)
 *
 * 将 host device 注册为总线控制器, 存入 s_controllers[device_id].
 * 后续 bus_controller_of 通过 device parent 查找 controller.
 *
 * @param dev       controller device (host)
 * @param type      总线类型 (BUS_TYPE_SPI 等)
 * @param ctlr_ops  host 级 ops
 * @param hw_ctx    host 私有上下文 (struct xxx_bus_host*)
 *
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  bus_controller_bind_full(struct device* dev, bus_type_t type,
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

#ifdef __cplusplus
}
#endif

#endif /* BUS_H */
