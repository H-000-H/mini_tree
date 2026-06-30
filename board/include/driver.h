/* SPDX-License-Identifier: Apache-2.0 */
/*
 * driver.h — 板级驱动核心头文件
 *
 * 声明 board_driver_probe_all/remove_all 按 probe 顺序遍历设备匹配驱动,
 * 提供 DRIVER_REGISTER 宏 (dtc-lite 编译期扫描生成 probe/remove 函数表).
 * 定义 IEC 61508 安全停机回调注册接口 (仅 probe 阶段可注册, 运行期不可追加).
 */
#ifndef BOARD_DRIVER_H
#define BOARD_DRIVER_H

#include "device.h"
#include "board_config.h"
#include "dev_lifecycle.h"

#ifdef __cplusplus
extern "C" 
{
#endif

/* ── Driver 核心 API ── */
int board_driver_probe_all(void) COMPAT_WARN_UNUSED_RESULT;   /* 遍历设备 → 匹配 driver → probe */
int board_driver_remove_all(void) COMPAT_WARN_UNUSED_RESULT;

/* dtc-lite 编译期生成 probe/remove 函数表, 运行时无需注册 */
void board_register_all_drivers(void);

/* ── 安全停机回调注册 (Observer 模式) ──
 * IEC 61508 §7.4.3.4: 框架不感知具体执行器类型,
 * 由各驱动在 probe 阶段注册自己的停机回调.
 * 仅允许在调度器启动前 (probe 阶段) 注册, 运行期不可追加.
 */
typedef void (*safety_shutdown_fn_t)(void);

void board_safety_register_shutdown(safety_shutdown_fn_t fn);

/* ── DRIVER_REGISTER 宏 ──
 * 在驱动 .c 文件中使用:
 *   DRIVER_REGISTER(my_drv, "compat,vendor", my_probe, my_remove);
 * 生成 board_driver_probe_my_drv() / board_driver_remove_my_drv()
 * 由编译期 dtc-lite.py 扫描收录, 运行时无 strcmp 匹配
 *
 * 带 fops 的驱动 remove 标准序列 (dev_lifecycle):
 *   dev_lc_remove_start(device_lc(dev));
 *   device_ops_unregister(dev);
 *   dev_lc_remove_drain(device_lc(dev), OSAL_WAIT_FOREVER);  // 持 io_lock 返回
 *   ... teardown ...
 *   dev_lc_remove_finish(device_lc(dev));
 * probe 阶段: device_lc_bind(dev, io_mutex);  io_mutex 用 osal_mutex_create_static (plain)
 */
#define DRIVER_REGISTER(name, compat, probe_fn, remove_fn)        \
    int board_driver_probe_##name(struct device* dev)             \
    {                                                             \
        return probe_fn(dev);                                     \
    }                                                             \
    int board_driver_remove_##name(struct device* dev)            \
    {                                                             \
        return remove_fn(dev);                                     \
    }

#ifdef __cplusplus
}
#endif

#endif /* BOARD_DRIVER_H */

