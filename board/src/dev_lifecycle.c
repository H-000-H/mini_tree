#include "dev_lifecycle.h"

#include "osal.h"
#include "compiler_compat_poison.h"

static int dev_lc_lock_live(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    if (!lc || !lc->io_lock)
        return VFS_ERR_INVAL;

    if (lc->state != DEV_LC_LIVE)
        return VFS_ERR_NODEV;

    if (osal_mutex_lock(lc->io_lock, timeout_ms) != 0)
        return VFS_ERR_TIMEOUT;

    if (lc->state != DEV_LC_LIVE)
    {
        (void)osal_mutex_unlock(lc->io_lock);
        return VFS_ERR_NODEV;
    }

    return VFS_OK;
}

void dev_lc_init(struct dev_lifecycle* lc, struct osal_mutex* io_lock)
{
    if (!lc)
        return;

    lc->io_lock   = io_lock;
    lc->opens     = 0;
    lc->io_active = 0;
    lc->state     = DEV_LC_LIVE;
}

void dev_lc_reset(struct dev_lifecycle* lc)
{
    if (!lc)
        return;

    lc->io_lock   = NULL;
    lc->opens     = 0;
    lc->io_active = 0;
    lc->state     = DEV_LC_UNINITIALIZED;
}

dev_lc_state_t dev_lc_state(const struct dev_lifecycle* lc)
{
    return lc ? lc->state : DEV_LC_UNINITIALIZED;
}

int dev_lc_opens(const struct dev_lifecycle* lc)
{
    return lc ? lc->opens : 0;
}

int dev_lc_io_active_count(const struct dev_lifecycle* lc)
{
    return lc ? lc->io_active : 0;
}

int dev_lc_open_begin(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    int ret = dev_lc_lock_live(lc, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    lc->opens++;
    return (lc->opens == 1) ? 1 : 0;
}

void dev_lc_open_end(struct dev_lifecycle* lc)
{
    if (lc && lc->io_lock)
        (void)osal_mutex_unlock(lc->io_lock);
}

void dev_lc_open_abort(struct dev_lifecycle* lc)
{
    if (!lc)
        return;

    if (lc->opens > 0)
        lc->opens--;

    dev_lc_open_end(lc);
}

int dev_lc_close_begin(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    int ret = dev_lc_lock_live(lc, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    if (lc->opens <= 0)
    {
        (void)osal_mutex_unlock(lc->io_lock);
        return VFS_ERR_IO;
    }

    lc->opens--;
    return (lc->opens == 0) ? 1 : 0;
}

void dev_lc_close_end(struct dev_lifecycle* lc)
{
    if (lc && lc->io_lock)
        (void)osal_mutex_unlock(lc->io_lock);
}

int dev_lc_io_begin(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    int ret = dev_lc_lock_live(lc, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    lc->io_active++;
    return VFS_OK;
}

void dev_lc_io_end(struct dev_lifecycle* lc)
{
    if (!lc || !lc->io_lock)
        return;

    if (lc->io_active > 0)
        lc->io_active--;

    (void)osal_mutex_unlock(lc->io_lock);
}

void dev_lc_remove_start(struct dev_lifecycle* lc)
{
    if (!lc)
        return;

    lc->state = DEV_LC_REMOVING;
}

int dev_lc_remove_drain(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    if (!lc || !lc->io_lock)
        return VFS_ERR_INVAL;

    if (lc->state != DEV_LC_REMOVING)
        return VFS_ERR_IO;

    const uint32_t start_ms = osal_time_ms();
    for (;;)
    {
        if (osal_mutex_lock(lc->io_lock, timeout_ms) != 0)
            return VFS_ERR_TIMEOUT;

        if (lc->opens == 0 && lc->io_active == 0)
            return VFS_OK;

        (void)osal_mutex_unlock(lc->io_lock);

        if (timeout_ms != OSAL_WAIT_FOREVER)
        {
            const uint32_t elapsed = osal_time_ms() - start_ms;
            if (elapsed >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }

        osal_delay_ms(1);
    }
}

void dev_lc_remove_finish(struct dev_lifecycle* lc)
{
    if (!lc)
        return;

    if (lc->io_lock)
        (void)osal_mutex_unlock(lc->io_lock);

    dev_lc_reset(lc);
}

