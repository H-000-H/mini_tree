/* SPDX-License-Identifier: Apache-2.0 */
/*
 * system_runtime.cpp — SystemRuntime 单例实现 (已弃用外观)
 *
 * getInstance 返回 Meyers 单例, 禁止拷贝与赋值
 * init/start 打印 deprecation 警告并委托 MiniTree 两阶段初始化
 * stop/suspend/resume/state 仅维护 m_state 字段, 不触发实际副作用
 */
#include "system_runtime.hpp"
#include "system_init.hpp"

#include "system_log.h"
#include "compiler_compat_poison.h"

static constexpr const char* kTag = "SystemRuntime";

SystemRuntime& SystemRuntime::getInstance()
{
    static SystemRuntime runtime;
    return runtime;
}

bool SystemRuntime::init()
{
    SYS_LOGW(kTag, "SystemRuntime::init() is deprecated — use MiniTree::System_Pre_OS_Init()");
    MiniTree::System_Pre_OS_Init();
    m_state = ModuleState::Initialized;
    return true;
}

bool SystemRuntime::start()
{
    SYS_LOGW(kTag, "SystemRuntime::start() is deprecated — use MiniTree::System_Start_Tasks()");
    MiniTree::System_Start_Tasks();
    m_state = ModuleState::Started;
    return true;
}

void SystemRuntime::stop()
{
    m_state = ModuleState::Stopped;
}

void SystemRuntime::suspend()
{
    m_state = ModuleState::Suspended;
}

void SystemRuntime::resume()
{
    m_state = ModuleState::Started;
}

ModuleState SystemRuntime::state() const
{
    return m_state;
}
