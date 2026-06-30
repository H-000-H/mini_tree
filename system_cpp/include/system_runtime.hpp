/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SystemRuntime — 系统运行时单例 (已弃用外观)
 *
 * 继承 Lifecycle, 提供单例 getInstance() 与状态查询
 * init/start 实际委托 MiniTree::System_Pre_OS_Init / System_Start_Tasks
 * 保留用于兼容旧代码, 新代码应直接调用 MiniTree 命名空间接口
 */
#pragma once

#include "lifecycle.hpp"

class SystemRuntime : public Lifecycle
{
public:
    static SystemRuntime& getInstance();

    bool init() override;
    bool start() override;
    void stop() override;
    void suspend() override;
    void resume() override;
    ModuleState state() const override;

private:
    SystemRuntime() = default;
    SystemRuntime(const SystemRuntime&) = delete;
    SystemRuntime& operator=(const SystemRuntime&) = delete;

    ModuleState m_state = ModuleState::Created;
};
