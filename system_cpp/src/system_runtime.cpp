#include "system_runtime.hpp"
#include "system_init.hpp"

#include "system_log.hpp"

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
