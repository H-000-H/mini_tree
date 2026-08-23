/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file driver.h
 *@brief driver 头文件
 *@author H-000-H
 *@details
 *   driver.h — 板级驱动核心头文件
 *   声明 board_driver_probe_all/remove_all 按 probe 顺序遍历设备匹配驱动,
 *   提供 DRIVER_REGISTER 宏 (dtc-lite 编译期扫描生成 probe/remove 函数表).
 *   定义安全停机回调注册接口 (仅 probe 阶段可注册, 运行期不可追加).
 */

#ifndef BOARD_DRIVER_H
#define BOARD_DRIVER_H

#include "board_config.h"
#include "dev_lifecycle.h"
#include "device.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Driver 核心 API ── */
    /**
     * @brief 遍历设备树 → 匹配 driver → 逐一 probe 初始化
     * @return 全部成功返回 MINI_OK, 任一失败返回负数错误码
     */
    int board_driver_probe_all(void) COMPAT_WARN_UNUSED_RESULT; /* 遍历设备 → 匹配 driver → probe */
    /**
     * @brief 按 probe 逆序逐一 remove, 安全停机全部设备
     * @return 全部成功返回 MINI_OK, 任一失败返回负数错误码
     */
    int board_driver_remove_all(void) COMPAT_WARN_UNUSED_RESULT;

    /* dtc-lite 编译期生成 probe/remove 函数表, 运行时无需注册 */
    /**
     * @brief 注册全部编译期生成的驱动 probe/remove 函数 (dtc-lite 生成)
     */
    void board_register_all_drivers(void);

    /* ── 安全停机回调注册 (Observer 模式) ──
     * 框架不感知具体执行器类型,
     * 由各驱动在 probe 阶段注册自己的停机回调.
     * 仅允许在调度器启动前 (probe 阶段) 注册, 运行期不可追加.
     */
    typedef void (*safety_shutdown_fn_t)(void);

    /**
     * @brief 注册安全停机回调 (Observer 模式)
     * @param[in] fn 停机回调 (可重复注册多个)
     * @note 仅允许 probe 阶段注册; 调度器启动后调用行为未定义
     */
    void board_safety_register_shutdown(safety_shutdown_fn_t fn);

/* ── DRIVER_REGISTER 宏 ──
 * 在驱动 .c 文件中使用:
 *   DRIVER_REGISTER(my_drv, "compat,vendor", my_probe, my_remove);
 * 生成 board_driver_probe_my_drv() / board_driver_remove_my_drv()
 * 由编译期 dtc-lite.py 扫描收录, 运行时无 strcmp 匹配
 *
 * 带 fops 的驱动 remove 标准序列 (dev_lifecycle):
 *   dev_lc_remove_start(device_lc(pdev));
 *   device_ops_unregister(pdev);
 *   dev_lc_remove_drain(device_lc(pdev), OSAL_WAIT_FOREVER);  // 原子轮询, 无持锁
 *   ... teardown ...
 *   dev_lc_remove_finish(device_lc(pdev));
 * probe 阶段: device_lc_bind(pdev);
 */
#define DRIVER_REGISTER(name, compat, probe_fn, remove_fn)                                                                                                                                             \
    int board_driver_probe_##name(struct device* pdev) { return probe_fn(pdev); }                                                                                                                      \
    int board_driver_remove_##name(struct device* pdev) { return remove_fn(pdev); }

#ifdef __cplusplus
}
#endif

#endif /* BOARD_DRIVER_H */
