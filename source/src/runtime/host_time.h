/* Condition deadlines stay on CLOCK_MONOTONIC even when wall time changes.
 * Darwin exposes relative waits instead of pthread_condattr_setclock. */
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#if defined(__APPLE__) && !defined(SH2_COND_RELATIVE_WAIT)
#define SH2_COND_RELATIVE_WAIT pthread_cond_timedwait_relative_np
#endif

static inline uint64_t host_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static inline int host_cond_init_monotonic(pthread_cond_t *cv) {
#ifdef SH2_COND_RELATIVE_WAIT
    return pthread_cond_init(cv, NULL);
#else
    pthread_condattr_t attr;
    int rc = pthread_condattr_init(&attr);
    if (rc) return rc;
    rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!rc) rc = pthread_cond_init(cv, &attr);
    pthread_condattr_destroy(&attr);
    return rc;
#endif
}

/* The caller rechecks its predicate after every wake, using the original deadline. */
static inline int host_cond_wait_until(pthread_cond_t *cv, pthread_mutex_t *mx, uint64_t deadline) {
    uint64_t ns = deadline;
#ifdef SH2_COND_RELATIVE_WAIT
    uint64_t now = host_monotonic_ns();
    if (deadline <= now) return ETIMEDOUT;
    ns -= now;
#endif
    struct timespec ts = {(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)};
#ifdef SH2_COND_RELATIVE_WAIT
    return SH2_COND_RELATIVE_WAIT(cv, mx, &ts);
#else
    return pthread_cond_timedwait(cv, mx, &ts);
#endif
}
