/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Lifecycle — 系统模块统一生命周期接口
 *
 * 定义 ModuleState 状态机: Created→Initialized→Started→Suspended→Stopped/Failed
 * 抽象基类强制子类实现 init/start/stop/suspend/resume/state 六个钩子
 * can_transit 守护状态转移合法性, Failed 为终态不可恢复
 */
#pragma once
#include "osal.h"

enum class ModuleState : uint8_t
{
    Created = 0,
    Initialized,
    Started,
    Suspended,
    Stopped,
    Failed,
};

class Lifecycle
{
public:
    virtual ~Lifecycle() = default;

    virtual bool init() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void suspend() = 0;
    virtual void resume() = 0;
    virtual ModuleState state() const = 0;

protected:
    static bool can_transit(ModuleState from, ModuleState to);
};
