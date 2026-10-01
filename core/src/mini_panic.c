/**
 * @file mini_panic.c
 * @author H-000-H
 * @brief Panic 辅助接口的 weak 兜底实现
 * @note 板级未覆盖时的 weak 兜底符号:
 * @note MINI_WEAK void safety_hardware_shutdown(void) { MINI_TRAP(); }
 * @note MINI_WEAK void mini_panic_interlock(void) {}
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#include "mini_panic.h"

#include "compiler_compat.h"

MINI_WEAK void safety_hardware_shutdown(void) { MINI_TRAP(); }

MINI_WEAK void mini_panic_interlock(void) {}
