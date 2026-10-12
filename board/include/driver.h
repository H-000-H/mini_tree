/**
 * @file driver.h
 * @author H-000-H
 * @brief 板级驱动核心: probe/remove 遍历 + DRIVER_REGISTER 宏
 * @copyright SPDX-License-Identifier: Apache-2.0
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

/* -------------------------------------------------------------------------- */
mt_err_t board_driver_probe_all(void) MINI_WARN_UNUSED_RESULT;
mt_err_t board_driver_remove_all(void) MINI_WARN_UNUSED_RESULT;
void     board_register_all_drivers(void); /* dtc-lite 编译期生成的函数表注册 */

/* -------------------------------------------------------------------------- */
/* 安全停机回调: 各驱动 probe 阶段注册, 调度器启动后不可追加 */
typedef void (*safety_shutdown_fn_t)(void);
void board_safety_register_shutdown(safety_shutdown_fn_t fn);

/* -------------------------------------------------------------------------- */
/* 用法: DRIVER_REGISTER(my_drv, "mt-xxx", my_probe, my_remove); */
/* dtc-lite 扫描收录, 运行时无 strcmp */
#define DRIVER_REGISTER(name, compat, probe_fn, remove_fn)                                                                                           \
    int board_driver_probe_##name(struct device* pdev) { return probe_fn(pdev); }                                                                    \
    int board_driver_remove_##name(struct device* pdev) { return remove_fn(pdev); }

#ifdef __cplusplus
}
#endif

#endif /* BOARD_DRIVER_H */
