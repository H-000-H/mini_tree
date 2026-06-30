/* SPDX-License-Identifier: Apache-2.0 */
/*
 * lifecycle.cpp — Lifecycle 状态转移规则实现
 *
 * 实现 Lifecycle::can_transit, 守护 ModuleState 合法转移
 * Failed 为吸收态, 仅允许转入 Failed 本身; Stopped 可回到 Initialized
 * 标准前向链: Created→Initialized→Started, Started↔Suspended 互转
 */
#include "lifecycle.hpp"
#include "compiler_compat_poison.h"

bool Lifecycle::can_transit(ModuleState from, ModuleState to)
{
    if (from == ModuleState::Failed && to != ModuleState::Failed)
    {
        return false;
    }

    if (to == ModuleState::Failed)
    {
        return true;
    }

    if (to == ModuleState::Stopped)
    {
        return true;
    }

    switch (from)
    {
    case ModuleState::Created:
        return to == ModuleState::Initialized;

    case ModuleState::Initialized:
        return to == ModuleState::Started;

    case ModuleState::Started:
        return to == ModuleState::Suspended;

    case ModuleState::Suspended:
        return to == ModuleState::Started;

    case ModuleState::Stopped:
        return to == ModuleState::Initialized;

    default:
        return false;
    }
}
