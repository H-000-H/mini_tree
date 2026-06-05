/*
 * POSIX (x86) atomic operations using GCC __sync builtins.
 * SPDX-Identifier: Apache-2.0
 */

#include <rtthread.h>

rt_atomic_t rt_hw_atomic_load(volatile rt_atomic_t *ptr)
{
    return __sync_fetch_and_add(ptr, 0);
}
void rt_hw_atomic_store(volatile rt_atomic_t *ptr, rt_atomic_t val)
{
    __sync_synchronize();
    *ptr = val;
}
rt_atomic8_t rt_hw_atomic_load8(volatile rt_atomic8_t *ptr)
{
    return __sync_fetch_and_add(ptr, 0);
}
void rt_hw_atomic_store8(volatile rt_atomic8_t *ptr, rt_atomic8_t val)
{
    __sync_synchronize();
    *ptr = val;
}
rt_atomic16_t rt_hw_atomic_load16(volatile rt_atomic16_t *ptr)
{
    return __sync_fetch_and_add(ptr, 0);
}
void rt_hw_atomic_store16(volatile rt_atomic16_t *ptr, rt_atomic16_t val)
{
    __sync_synchronize();
    *ptr = val;
}
rt_atomic_t rt_hw_atomic_add(volatile rt_atomic_t *ptr, rt_atomic_t val)
{
    return __sync_fetch_and_add(ptr, val);
}
rt_atomic_t rt_hw_atomic_sub(volatile rt_atomic_t *ptr, rt_atomic_t val)
{
    return __sync_fetch_and_sub(ptr, val);
}
rt_atomic8_t rt_hw_atomic_and8(volatile rt_atomic8_t *ptr, rt_atomic8_t val)
{
    return __sync_fetch_and_and(ptr, val);
}
rt_atomic8_t rt_hw_atomic_or8(volatile rt_atomic8_t *ptr, rt_atomic8_t val)
{
    return __sync_fetch_and_or(ptr, val);
}
rt_atomic16_t rt_hw_atomic_and16(volatile rt_atomic16_t *ptr, rt_atomic16_t val)
{
    return __sync_fetch_and_and(ptr, val);
}
rt_atomic16_t rt_hw_atomic_or16(volatile rt_atomic16_t *ptr, rt_atomic16_t val)
{
    return __sync_fetch_and_or(ptr, val);
}
rt_atomic_t rt_hw_atomic_and(volatile rt_atomic_t *ptr, rt_atomic_t val)
{
    return __sync_fetch_and_and(ptr, val);
}
rt_atomic_t rt_hw_atomic_or(volatile rt_atomic_t *ptr, rt_atomic_t val)
{
    return __sync_fetch_and_or(ptr, val);
}
rt_atomic_t rt_hw_atomic_xor(volatile rt_atomic_t *ptr, rt_atomic_t val)
{
    return __sync_fetch_and_xor(ptr, val);
}
rt_atomic_t rt_hw_atomic_exchange(volatile rt_atomic_t *ptr, rt_atomic_t val)
{
    return __sync_lock_test_and_set(ptr, val);
}
void rt_hw_atomic_flag_clear(volatile rt_atomic_t *ptr)
{
    __sync_lock_release(ptr);
}
rt_atomic_t rt_hw_atomic_flag_test_and_set(volatile rt_atomic_t *ptr)
{
    return __sync_lock_test_and_set(ptr, 1);
}
rt_atomic_t rt_hw_atomic_compare_exchange_strong(volatile rt_atomic_t *ptr,
                                                  rt_atomic_t *expected,
                                                  rt_atomic_t desired)
{
    rt_atomic_t old = __sync_val_compare_and_swap(ptr, *expected, desired);
    if (old == *expected)
        return 1;
    *expected = old;
    return 0;
}
