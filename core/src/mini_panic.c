/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file mini_panic.c
 *@brief Panic 辅助接口的 weak 兜底实现
 *@author H-000-H
 *@details
 *   板级未覆盖时的 weak 兜底符号:
 *     MINI_WEAK void safety_hardware_shutdown(void) { MINI_TRAP(); }
 *     MINI_WEAK void mini_panic_interlock(void) {}
 *   (板级仍可用强符号覆盖这两个接口)。
 *
 *   system_safety_hardware_shutdown 是板级强符号, 实现在板级 (不在本文件)。
 */

#include "mini_panic.h"

#include "compiler_compat.h"

MINI_WEAK void safety_hardware_shutdown(void) { MINI_TRAP(); }

MINI_WEAK void mini_panic_interlock(void) {}
