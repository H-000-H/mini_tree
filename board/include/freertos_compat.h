/*
 * freertos_compat.h — MinGW POSIX signal compat for FreeRTOS POSIX port
 * Provides SIGALRM, sigset_t, sigaction() and related definitions
 * that MinGW-w64 (standalone, not MSYS2) lacks.
 */
#ifndef FREERTOS_POSIX_COMPAT_H
#define FREERTOS_POSIX_COMPAT_H

#ifdef _WIN32
#ifndef __MSYS__

#include <signal.h>
#include <stddef.h>

/* ----- missing signal constants ----- */
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGUSR1
#define SIGUSR1 10
#endif
#ifndef SIGUSR2
#define SIGUSR2 12
#endif

/* ----- sigset_t (MinGW has _sigset_t in pthread.h) ----- */
#ifndef sigset_t
#if defined(_SIGSET_T) || defined(_SIGSET_T_)
/* already defined by pthread.h */
#else
typedef unsigned long sigset_t;
#endif
#endif

/* ----- sigset manipulation (void cast to suppress unused-value warning) ----- */
#ifndef sigfillset
#define sigfillset(s)  (void)(*(s) = (sigset_t)-1)
#endif
#ifndef sigdelset
#define sigdelset(s, n) (void)(*(s) &= ~(1UL << ((n)-1)))
#endif
#ifndef sigemptyset
#define sigemptyset(s) (void)(*(s) = 0)
#endif
#ifndef sigaddset
#define sigaddset(s, n) (void)(*(s) |= 1UL << ((n)-1))
#endif

/* ----- struct sigaction / sigaction() using signal() ----- */
#ifndef SA_RESTART
#define SA_RESTART 0
#endif
#ifndef SA_NOCLDSTOP
#define SA_NOCLDSTOP 0
#endif

#ifndef HAVE_SIGACTION
#define HAVE_SIGACTION

struct sigaction {
    void     (*sa_handler)(int);
    sigset_t   sa_mask;
    int        sa_flags;
};

static inline int sigaction(int signum, const struct sigaction *act,
                             struct sigaction *oldact)
{
    if (oldact) oldact->sa_handler = NULL;
    if (act) signal(signum, act->sa_handler);
    return 0;
}
#endif /* HAVE_SIGACTION */

#endif /* !__MSYS__ */
#endif /* _WIN32 */

#endif /* FREERTOS_POSIX_COMPAT_H */
