/* Platform layer (host.c): POSIX and Horizon. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct HostThread HostThread;
/* Runs fn(arg) on a new thread with a host stack of `stack` bytes. */
HostThread *host_thread_start(void (*fn)(void *), void *arg, size_t stack);
void host_use_single_core(int core);   /* confines every guest thread to one core (Switch); -1 spreads them */
HostThread *host_thread_adopt(void);   /* registers the calling thread */
void host_main_thread_prepare(size_t stack); /* validates/sizes the desktop UI thread's host stack */
HostThread *host_thread_self(void);
void host_thread_exit(void) __attribute__((noreturn));
/* Stops / restarts a thread (a thread may pause itself). POSIX external pause waits
 * for the target's acknowledgement, so it cannot keep executing on another CPU. */
void host_thread_pause(HostThread *t);
void host_thread_resume(HostThread *t);
void host_thread_signal(HostThread *t, int sig);  /* Linux debugging (SH2_DUMP_AFTER) */

int host_console_docked(void);  /* the Switch in its dock (1) or handheld (0); Linux: 0 with SH2_HANDHELD set */

void *host_arena_reserve(void);                     /* 4 GB of address space for the guest */
void host_arena_commit(uint32_t va, uint32_t len);  /* backs [va, va+len) with zeroed memory */
void host_crash_handlers(void (*on_crash)(uintptr_t addr));
void host_backtrace(int fd);  /* host frames, where the platform can name them */
