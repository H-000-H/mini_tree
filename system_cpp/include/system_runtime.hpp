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
