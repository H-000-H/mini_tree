/**
 * @file obj.h
 * @author H-000-H
 * @brief Shared kernel-object header: reference count + storage/state flags
 * @note every primitive that can be created on the heap or over caller storage
 *       embeds mini_os_obj_t, so the storage flag, the "still referenced" rule
 *       and the deferred destroy path have exactly one implementation
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#ifndef MINI_OS_OBJ_H
#define MINI_OS_OBJ_H
#include "err.h"
#include "memory.h"
#include "redef.h"
#if defined(__cplusplus)
extern "C"
{
#endif

#define MINI_OS_OBJ_FLAG_HEAP_OWNED 0x01u /**< descriptor came from the heap */
#define MINI_OS_OBJ_FLAG_DESTROY 0x02u     /**< destroy requested: new users are refused */
#define MINI_OS_OBJ_FLAG_KILL 0x04u       /**< destroy may wake parked users instead of refusing */
#define MINI_OS_OBJ_FLAG_TIMEOUT 0x08u    /**< a woken parked user reports MINI_OS_ERR_TIMEOUT instead of
                                               MINI_OS_ERR_DEAD (only meaningful together with KILL) */

/**
 * @brief Kernel object header embedded in a sync-primitive descriptor
 * @note obj_ref is 1 (the creator) plus one per in-flight API call, and is only
 *       ever touched with interrupts masked
 */
typedef struct
{
    mini_os_uint8_t obj_ref;   /**< 1 (creator) + in-flight users */
    mini_os_uint8_t obj_flags; /**< MINI_OS_OBJ_FLAG_* */
} mini_os_obj_t;

/**
 * @brief One in-flight object reference a parked sync call holds
 * @note a public API acquires its object on entry and releases it on exit, so a
 *       thread deleted while parked would leak the reference and keep the
 *       waited-on object undeletable forever. The kernel therefore records the
 *       reference on the TCB (see mini_os_sync_wait_ref_push()) and drops it on
 *       the delete path, reclaiming the descriptor on the last one
 */
typedef struct mini_os_wait_ref
{
    mini_os_obj_t* obj;     /**< object the parked call acquired */
    void*          storage; /**< descriptor storage its release may reclaim */
    mini_os_size_t size;    /**< descriptor size for the reclaim above */
} mini_os_wait_ref_t;

/**
 * @brief Initialize an object header
 * @param[out] obj header to initialize
 * @param[in] heap_owned MINI_OS_TRUE when the descriptor came from the heap
 * @note the creator holds the first reference
 */
MINI_OS_STATIC_INLINE void mini_os_obj_init(mini_os_obj_t* obj, mini_os_bool_t heap_owned)
{
    obj->obj_ref = 1u;
    obj->obj_flags = (heap_owned != MINI_OS_FALSE) ? (mini_os_uint8_t)MINI_OS_OBJ_FLAG_HEAP_OWNED : (mini_os_uint8_t)0u;
}

/**
 * @brief Tell whether the descriptor storage belongs to the heap
 * @param[in] obj object header
 * @return MINI_OS_TRUE when the descriptor must be freed on destroy
 */
MINI_OS_STATIC_INLINE mini_os_bool_t mini_os_obj_is_heap_owned(const mini_os_obj_t* obj) { return ((obj->obj_flags & (mini_os_uint8_t)MINI_OS_OBJ_FLAG_HEAP_OWNED) != 0u) ? MINI_OS_TRUE : MINI_OS_FALSE; }

/**
 * @brief Tell whether the descriptor storage was provided by the caller
 * @param[in] obj object header
 * @return MINI_OS_TRUE for caller storage (the *_create_static()/delete_static()
 *         flavour), MINI_OS_FALSE when the descriptor came from the heap
 * @note a delete path compares this against its own flavour and returns
 *       MINI_OS_ERR_NOTSUPP when the caller picked the wrong one
 */
MINI_OS_STATIC_INLINE mini_os_bool_t mini_os_obj_is_static(const mini_os_obj_t* obj) { return (mini_os_obj_is_heap_owned(obj) == MINI_OS_FALSE) ? MINI_OS_TRUE : MINI_OS_FALSE; }

/**
 * @brief Tell whether destroy was requested
 * @param[in] obj object header
 * @return MINI_OS_TRUE once mini_os_obj_request_destroy() ran or the creator reference
 *         was dropped
 */
MINI_OS_STATIC_INLINE mini_os_bool_t mini_os_obj_is_destroyed(const mini_os_obj_t* obj) { return ((obj->obj_flags & (mini_os_uint8_t)MINI_OS_OBJ_FLAG_DESTROY) != 0u) ? MINI_OS_TRUE : MINI_OS_FALSE; }

/**
 * @brief Tell whether destroy may force parked users out
 * @param[in] obj object header
 * @return MINI_OS_TRUE once the KILL flag is armed
 */
MINI_OS_STATIC_INLINE mini_os_bool_t mini_os_obj_is_kill_enabled(const mini_os_obj_t* obj) { return ((obj->obj_flags & (mini_os_uint8_t)MINI_OS_OBJ_FLAG_KILL) != 0u) ? MINI_OS_TRUE : MINI_OS_FALSE; }

/**
 * @brief Tell whether a woken parked user reports a timeout instead of a dead object
 * @param[in] obj object header
 * @return MINI_OS_TRUE once the TIMEOUT flag is armed (which only the kill path sets)
 */
MINI_OS_STATIC_INLINE mini_os_bool_t mini_os_obj_kill_reports_timeout(const mini_os_obj_t* obj) { return ((obj->obj_flags & (mini_os_uint8_t)MINI_OS_OBJ_FLAG_TIMEOUT) != 0u) ? MINI_OS_TRUE : MINI_OS_FALSE; }

/**
 * @brief Error a parked user reports when the destroy path kills it
 * @param[in] obj object header
 * @return MINI_OS_ERR_TIMEOUT when the TIMEOUT flag is armed, MINI_OS_ERR_DEAD otherwise
 * @note the destroy path must be kill-armed for this to be reached; the wake helper
 *       uses it to decide whether the parked call has to re-check and report DEAD
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_obj_kill_error(const mini_os_obj_t* obj) { return (mini_os_obj_kill_reports_timeout(obj) != MINI_OS_FALSE) ? MINI_OS_ERR_TIMEOUT : MINI_OS_ERR_DEAD; }

/**
 * @brief Read the reference count
 * @param[in] obj object header
 * @return 1 when only the creator holds the object; more while API calls are in flight
 * @note only meaningful inside a critical section
 */
MINI_OS_STATIC_INLINE mini_os_uint8_t mini_os_obj_user_count(const mini_os_obj_t* obj) { return obj->obj_ref; }

/**
 * @brief Arm the destroy path so it may wake parked users (reported as DEAD)
 * @param[in,out] obj object header
 */
MINI_OS_STATIC_INLINE void mini_os_obj_enable_kill(mini_os_obj_t* obj)
{
    mini_os_irq_t irq = mini_os_irq_save();

    obj->obj_flags |= (mini_os_uint8_t)MINI_OS_OBJ_FLAG_KILL;
    mini_os_irq_restore(irq);
}

/**
 * @brief Arm the destroy path so it may wake parked users as a timeout
 * @param[in,out] obj object header
 * @note sets KILL and TIMEOUT together: a woken parked user then reports
 *       MINI_OS_ERR_TIMEOUT instead of MINI_OS_ERR_DEAD
 */
MINI_OS_STATIC_INLINE void mini_os_obj_enable_kill_reports_timeout(mini_os_obj_t* obj)
{
    mini_os_irq_t irq = mini_os_irq_save();

    obj->obj_flags |= (mini_os_uint8_t)(MINI_OS_OBJ_FLAG_KILL | MINI_OS_OBJ_FLAG_TIMEOUT);
    mini_os_irq_restore(irq);
}

/**
 * @brief Reclaim the descriptor storage of a destroyed object
 * @param[in] obj object header (inside the descriptor)
 * @param[in,out] storage descriptor to reclaim
 * @param[in] size descriptor size, used when the storage belongs to the caller
 * @note caller must hold the IRQ lock
 */
MINI_OS_STATIC_INLINE void mini_os_obj_free_storage(mini_os_obj_t* obj, void* storage, mini_os_size_t size)
{
    if (mini_os_obj_is_heap_owned(obj) != MINI_OS_FALSE)
        (void)mini_os_free(storage);
    else
        MINI_OS_MEMSET(storage, 0, size); /* never free caller storage, only clear it */
}

/**
 * @brief Take a reference for the duration of one API call
 * @param[in,out] obj object header
 * @return MINI_OS_OK on success; MINI_OS_ERR_REJECT once destroy was requested
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_obj_acquire(mini_os_obj_t* obj)
{
    mini_os_irq_t irq = mini_os_irq_save();
    mini_os_err_t err = MINI_OS_OK;

    if (mini_os_obj_is_destroyed(obj) != MINI_OS_FALSE)
        err = MINI_OS_ERR_REJECT;
    else
        obj->obj_ref = (mini_os_uint8_t)(obj->obj_ref + 1u);
    mini_os_irq_restore(irq);
    return err;
}

/**
 * @brief Drop one reference, reclaiming the descriptor once the last user leaves
 * @param[in,out] obj object header
 * @param[in,out] storage descriptor to reclaim
 * @param[in] size descriptor size, used when the storage belongs to the caller
 * @note reclamation only happens after a destroy request, so a plain release is
 *       just a decrement
 */
MINI_OS_STATIC_INLINE void mini_os_obj_release(mini_os_obj_t* obj, void* storage, mini_os_size_t size)
{
    mini_os_irq_t  irq = mini_os_irq_save();
    mini_os_bool_t reclaim = MINI_OS_FALSE;

    if (obj->obj_ref > 0u)
        obj->obj_ref = (mini_os_uint8_t)(obj->obj_ref - 1u);
    if (obj->obj_ref == 0u && mini_os_obj_is_destroyed(obj) != MINI_OS_FALSE)
        reclaim = MINI_OS_TRUE;
    if (reclaim != MINI_OS_FALSE)
        mini_os_obj_free_storage(obj, storage, size);
    mini_os_irq_restore(irq);
}

/**
 * @brief Request destroy and drop the creator reference
 * @param[in,out] obj object header
 * @param[in,out] storage descriptor to reclaim
 * @param[in] size descriptor size, used when the storage belongs to the caller
 * @return MINI_OS_OK when the creator was the last user and the descriptor is
 *         already reclaimed; MINI_OS_ERR_REJECT while other users are still in
 *         flight, in which case REJECT is set (new users are refused) and the
 *         last mini_os_obj_release() reclaims the descriptor
 * @note the caller must have refused the destroy already when kill is not armed
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_obj_request_destroy(mini_os_obj_t* obj, void* storage, mini_os_size_t size)
{
    mini_os_irq_t irq = mini_os_irq_save();
    mini_os_err_t err = MINI_OS_OK;

    obj->obj_flags |= (mini_os_uint8_t)MINI_OS_OBJ_FLAG_DESTROY;
    if (obj->obj_ref > 0u)
        obj->obj_ref = (mini_os_uint8_t)(obj->obj_ref - 1u); /* the creator reference */
    if (obj->obj_ref > 0u)
        err = MINI_OS_ERR_REJECT;
    else
        mini_os_obj_free_storage(obj, storage, size);
    mini_os_irq_restore(irq);
    return err;
}

#if defined(__cplusplus)
}
#endif
#endif /* MINI_OS_OBJ_H */
