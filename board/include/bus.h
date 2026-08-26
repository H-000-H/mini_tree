/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file bus.h
 *@brief bus 头文件
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   BUS CORE — 总线子系统通用框架层
 *   三层架构: VFS (file_operations + dev_lifecycle + DTS) → Bus (host/client 池,
 *   atomic ref_count, controller_ops) → HAL (寄存器/DMA/中断, opaque handle)
 *   隔离 (#pragma GCC poison 强制): bus 外禁止调 hal 符号, vfs 外禁止调 bus 符号
 *   引用计数: host->ref_count atomic, register +1/unregister -1, deinit >0 返回 BUSY;
 *   state 变更由上层 (board_device.c) 序列化
 *   controller_ops (host 级): init/deinit/role/client_register/client_unregister;
 *   client 级 I/O 由 bus_xxx_open/close/read/write 直接处理, 不经 ops 表
 *   --------------------------------------------------------------------------
 */

#ifndef BUS_H
#define BUS_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct device;

    /* -------------------------------------------------------------------------- */
    /* Bus Type (uint16 bitmap) */
    /* -------------------------------------------------------------------------- */
    typedef uint16_t bus_type_t;

#define BUS_TYPE_SPI ((bus_type_t)(1U << 0))
#define BUS_TYPE_UART ((bus_type_t)(1U << 1))
#define BUS_TYPE_I2C ((bus_type_t)(1U << 2))
#define BUS_TYPE_I2S ((bus_type_t)(1U << 3))
#define BUS_TYPE_CAN ((bus_type_t)(1U << 4))
#define BUS_TYPE_USB ((bus_type_t)(1U << 5))
#define BUS_TYPE_PCIE ((bus_type_t)(1U << 6))
    /* -------------------------------------------------------------------------- */
    /* Controller Operations */
    /* -------------------------------------------------------------------------- */

    /**
     * @brief Host 级控制器操作表 — 管理控制器生命周期与 client 挂载
     *
     *
     * @return 成功返回 0, BUSY 返回 MINI_ERR_BUSY, 失败返回 VFS_ERR_*
     */
    struct bus_controller_ops
    {
        int (*init)(struct device* pdev, const void* cfg); /**< 初始化 host */
        int (*deinit)(struct device* pdev); /**< 反初始化 host (返回 int, BUSY 时不销毁) */
        int (*role)(struct device* pdev); /**< 查询角色 (MASTER/SLAVE) */
        int (*client_register)(struct device* pdev, const void* cfg,
                               void** out); /**< 注册 client */
        void (*client_unregister)(struct device* pdev); /**< 注销 client */
    };
    /* -------------------------------------------------------------------------- */

    /* Controller */
    /* -------------------------------------------------------------------------- */

    /**
     * @brief 总线控制器 (host) 描述符
     *
     * 每个 controller device 对应一个 bus_controller, 由 bus_controller_bind_full 注册.
     * 存储在 s_controllers[device_id] 静态表中, O(1) 查找.
     */
    struct bus_controller
    {
        bus_type_t type; /**< 总线类型 (BUS_TYPE_SPI 等) */
        const struct bus_controller_ops* ctlr_ops; /**< host 级 ops */
        void* hw_ctx; /**< host 私有上下文 (struct xxx_bus_host*) */
    };

    /* -------------------------------------------------------------------------- */

    /* Controller API */
    /* -------------------------------------------------------------------------- */

    /**
     * @brief 绑定 controller (full, 带 ctlr_ops)
     *
     * 将 host device 注册为总线控制器, 存入 s_controllers[device_id].
     * 后续 bus_controller_of 通过 device parent 查找 controller.
     *
     * @param[in] pdev       controller device (host)
     * @param[in] type      总线类型 (BUS_TYPE_SPI 等)
     * @param[in] ctlr_ops  host 级 ops
     * @param[in] hw_ctx    host 私有上下文 (struct xxx_bus_host*)
     *
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int bus_controller_bind_full(struct device* pdev, bus_type_t type,
                                 const struct bus_controller_ops* ctlr_ops,
                                 void* hw_ctx) MINI_WARN_UNUSED_RESULT;

    /**
     * @brief 查找 device 自身绑定的 controller (传 host)
     * @param[in] pdev controller device (host)
     * @param[out] out 回传 bus_controller 指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_NODEV
     */
    int bus_controller_get(const struct device* pdev,
                           struct bus_controller** out) MINI_WARN_UNUSED_RESULT;

    /**
     * @brief 查找 client 所属的 controller
     *
     * 通过 device_get_parent(pdev) 找到 host, 再从 s_controllers 取出 bus_controller.
     * 用于 client device 探测其所属 host.
     *
     * @param[in] pdev  client device
     * @param[out] out  输出 bus_controller 指针
     *
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_NODEV
     */
    int bus_controller_of(const struct device* pdev,
                          struct bus_controller** out) MINI_WARN_UNUSED_RESULT;

    /**
     * @brief 解绑 controller
     *
     * 清空 s_controllers[device_id], 不检查 ref_count.
     * 调用者 (bus_xxx_host_deinit) 应先检查 ref_count > 0 拒绝解绑.
     * @param[in] pdev controller device (host)
     */
    void bus_controller_unbind(struct device* pdev);

    /* -------------------------------------------------------------------------- */
    /* Async callback bridge */
    /* -------------------------------------------------------------------------- */
    /**
     * @brief 用户侧异步完成回调 (device* 视图, 非 HAL 句柄)
     */
    typedef void (*bus_async_user_cb_t)(struct device* pdev, const void* trans, void* userdata);

    /**
     * @brief HAL→VFS 回调桥接描述符
     *
     * 静态池分配: async 提交时 claim → ISR 中 complete 调用用户 cb 并释放。
     * 必须静态: 回调在 ISR 异步触发, 栈帧已销毁。
     */
    struct bus_async_bridge
    {
        struct device* pdev; /**< 关联设备 */
        bus_async_user_cb_t cb; /**< 用户回调 */
        void* userdata; /**< 用户私有数据 */
        uint8_t in_use; /**< 槽位占用标志 */
    };

    /**
     * @brief 从 slots[0..slot_count) 申请一个空闲桥接槽
     * @param[in] slots 桥接槽数组
     * @param[in] slot_count 槽位数量
     * @return 成功返回 bridge 指针, 池满返回 NULL
     */
    struct bus_async_bridge* bus_async_bridge_claim(struct bus_async_bridge* slots,
                                                    size_t slot_count);

    /**
     * @brief 绑定 device / 用户回调 / userdata (claim 之后调用)
     * @param[in] bridge 桥接槽指针
     * @param[in] pdev 关联设备
     * @param[in] cb 用户完成回调
     * @param[in] userdata 用户私有数据
     */
    void bus_async_bridge_bind(struct bus_async_bridge* bridge, struct device* pdev,
                               bus_async_user_cb_t cb, void* userdata);

    /**
     * @brief 释放槽位 (HAL 提交失败时立即调用, 不触发用户 cb)
     * @param[in] bridge bridge 指针
     */
    void bus_async_bridge_release(struct bus_async_bridge* bridge);

    /**
     * @brief ISR 安全完成: 调用户 cb 后释放 in_use
     * @param[in] userdata 必须为 struct bus_async_bridge*
     * @param[in] trans    传给用户 cb 的传输描述符 (可为 NULL)
     */
    void bus_async_bridge_complete(void* userdata, const void* trans);
    /* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* BUS_H */
