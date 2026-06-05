/*
 * sys/times.h — MinGW compat: emulate POSIX times() via Windows GetProcessTimes
 */
#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

#ifdef _WIN32

#include <windows.h>
#include <time.h>

struct tms {
    clock_t tms_utime;  /* user CPU time */
    clock_t tms_stime;  /* system CPU time */
    clock_t tms_cutime; /* children user CPU time (unsupported) */
    clock_t tms_cstime; /* children system CPU time (unsupported) */
};

static inline clock_t times(struct tms *buffer)
{
    FILETIME create, exit, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) {
        /* Convert 100-ns intervals to clock ticks */
        ULARGE_INTEGER k, u;
        k.LowPart  = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        u.LowPart  = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;
        buffer->tms_stime  = (clock_t)(k.QuadPart / 10000);
        buffer->tms_utime  = (clock_t)(u.QuadPart / 10000);
    } else {
        buffer->tms_stime  = 0;
        buffer->tms_utime  = 0;
    }
    buffer->tms_cutime = 0;
    buffer->tms_cstime = 0;
    return buffer->tms_utime;
}

#else
#include_next <sys/times.h>
#endif

#endif /* _SYS_TIMES_H */
