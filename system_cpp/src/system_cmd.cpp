/**
 * @file system_cmd.cpp
 * @author H-000-H
 * @brief SystemCmd 实现
 * @note  单份实现; 命令表访问统一走 mini_critical 临界区
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#include "system_cmd.hpp"

#include <string.h>

SystemCmd::SystemCmd() : count_(0)
{
}

SystemCmd& SystemCmd::get_instance()
{
    static SystemCmd instance;
    return instance;
}

int SystemCmd::register_raw(const char* name, const RawHandler& wrapper, TypeIdToken args_id,
                            TypeIdToken ctx_id)
{
    if(name == nullptr)
        return MINI_ERR_INVAL;

    const size_t name_len = etl::strlen(name);
    if(name_len >= MaxNameLen)
        return MINI_ERR_INVAL;

    mini_irq_state_t irq = mini_critical_enter();

    if(count_ >= MaxCommands)
    {
        mini_critical_exit(irq);
        return MINI_ERR_NOSPC;
    }

    for(size_t i = 0; i < count_; i++)
    {
        if(strcmp(entries_[i].name_, name) == 0)
        {
            mini_critical_exit(irq);
            return MINI_ERR_BUSY;
        }
    }

    MINI_MEM_COPY(entries_[count_].name_, name, name_len + 1);
    entries_[count_].node_.wrapper_ = wrapper;
    entries_[count_].node_.args_id_ = args_id;
    entries_[count_].node_.ctx_id_ = ctx_id;
    count_++;

    mini_critical_exit(irq);
    return MINI_OK;
}

int SystemCmd::unregister_cmd(const char* name)
{
    if(name == nullptr)
        return MINI_ERR_INVAL;

    mini_irq_state_t irq = mini_critical_enter();

    for(size_t i = 0; i < count_; i++)
    {
        if(strcmp(entries_[i].name_, name) == 0)
        {
            entries_[i] = entries_[count_ - 1]; /* 末项填补, 命令顺序无语义 */
            count_--;
            mini_critical_exit(irq);
            return MINI_OK;
        }
    }

    mini_critical_exit(irq);
    return MINI_ERR_NODEV;
}

int SystemCmd::dispatch(const char* name, const void* arg, size_t arg_len, void* ctx,
                        TypeIdToken expected_args_id, TypeIdToken expected_ctx_id) const
{
    if(name == nullptr)
        return MINI_ERR_INVAL;

    bool        found = false;
    HandlerNode node;

    mini_irq_state_t irq = mini_critical_enter();

    for(size_t i = 0; i < count_; i++)
    {
        if(strcmp(entries_[i].name_, name) == 0)
        {
            node = entries_[i].node_; /* 拷贝后再退临界区: 回调里可能改表 */
            found = true;
            break;
        }
    }

    mini_critical_exit(irq);

    if(!found)
        return MINI_ERR_NODEV;
    if((expected_args_id != nullptr) && (node.args_id_ != expected_args_id))
        return MINI_ERR_NOTSUPP;
    if((expected_ctx_id != nullptr) && (node.ctx_id_ != expected_ctx_id))
        return MINI_ERR_NOTSUPP;

    return node.wrapper_(arg, arg_len, ctx) ? MINI_OK : MINI_ERR_INVAL;
}

bool SystemCmd::has_cmd(const char* name) const
{
    if(name == nullptr)
        return false;

    bool found = false;

    mini_irq_state_t irq = mini_critical_enter();

    for(size_t i = 0; i < count_; i++)
    {
        if(strcmp(entries_[i].name_, name) == 0)
        {
            found = true;
            break;
        }
    }

    mini_critical_exit(irq);

    return found;
}

size_t SystemCmd::count() const
{
    mini_irq_state_t irq = mini_critical_enter();
    const size_t     sz = count_;
    mini_critical_exit(irq);

    return sz;
}
