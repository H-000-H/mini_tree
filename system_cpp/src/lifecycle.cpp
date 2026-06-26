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
