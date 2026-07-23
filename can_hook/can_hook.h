/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * CAN_HOOK — Classic CAN 协议超集钩子 (COMPAT_WEAK)
 *
 * VFS 开闭读写一律经这些钩子（不是单独「hook 模式」）：
 *   无强符号 → 弱默认透传 = 普通 Classic CAN
 *   有强符号 → 同一路径叠加过滤/改写等扩展
 * 不是第二条总线；参数面不同于 DTSI 硬件配置。
 *@=========================================================================================================================*/
#ifndef CAN_HOOK_H
#define CAN_HOOK_H

#include <stdint.h>
#include "compiler_compat.h"
#include "status.h"
#include "hal_can.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;

int can_hook_on_open(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
int can_hook_on_close(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
int can_hook_pre_tx(struct device* pdev, struct can_frame* frame) COMPAT_WARN_UNUSED_RESULT;
int can_hook_post_tx(struct device* pdev, const struct can_frame* frame, int tx_ret) COMPAT_WARN_UNUSED_RESULT;
int can_hook_filter_match(struct device* pdev, const struct can_frame* frame) COMPAT_WARN_UNUSED_RESULT;
int can_hook_on_rx(struct device* pdev, struct can_frame* frame) COMPAT_WARN_UNUSED_RESULT;
int can_hook_on_err(struct device* pdev, int err) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* CAN_HOOK_H */
