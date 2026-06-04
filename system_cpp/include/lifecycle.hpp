#pragma once

#include <cstdint>

/*      ┌─────────────┐    init()    ┌──────────────┐    start()   ┌─────────┐
        │   Created   │ ───────────► │ Initialized  │ ───────────► │ Started │
        └─────────────┘              └──────────────┘              └────┬────┘
            ^                            │                               │
            │                     stop() │                     suspend() │
            │                            ▼                               ▼
        ┌─────────────┐    start()    ┌──────────┐              ┌─────────────┐
        │   Failed    │ ◄─────────────│ Stopped  │              │ Suspended   │
        └─────────────┘               └──────────┘              └──────┬──────┘
            ^                                                          │
            │  fail()                                                  │
            └─────────────────────────────────────────────────────────—┘
                              resume()*/
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
