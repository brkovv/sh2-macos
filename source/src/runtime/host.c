#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
/* What differs between Linux and the Switch: threads (and pausing them), the guest address window,
 * crash handlers and backtraces. */
#include "host.h"
#include "rt.h"
#include <pthread.h>
#ifndef __SWITCH__
#include <stdatomic.h>
#endif
#ifdef __SWITCH__
#include <switch.h>
#include <malloc.h>
#include <stdalign.h>
#endif

struct HostThread {
    void (*fn)(void *);
    void *arg;
#ifdef __SWITCH__
    Thread thr;
    Mutex m;
    CondVar cv;
    int wakes, stopped;  /* resumes not yet used up; paused by another thread (svcSetThreadActivity) */
#else
    pthread_t pt;
    int wake[2];
    _Atomic uint32_t pauses, resumes, pause_seen;
    _Atomic int finished;
#endif
};

static __thread HostThread *self;
HostThread *host_thread_self(void) { return self; }

#ifdef __SWITCH__
/* ======================================================================== Switch */

static void thread_entry(void *p) {
    self = p;
    self->fn(self->arg);
}

/* Guest threads run at 0x3B, the one priority Horizon time-slices, spread over the three cores the
 * application gets: the game spins on Sleep(0) and on its own flags, and at any other priority a
 * spinning thread keeps its core from everything at or below it. */
static int only_core = -1;

/* The game's CRI audio engine races when its threads run in parallel (the "sound loops forever" bug the
 * PC fix works around by pinning the game to one core): with a core given here, every guest thread,
 * the calling one included, is confined to it. */
void host_use_single_core(int core) {
    only_core = core;
    if (core >= 0) svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
}

HostThread *host_thread_start(void (*fn)(void *), void *arg, size_t stack) {
    static int next_core;
    HostThread *t = calloc(1, sizeof *t);
    t->fn = fn;
    t->arg = arg;
    Result rc = threadCreate(&t->thr, thread_entry, t, NULL, stack, 0x3B, only_core >= 0 ? only_core : next_core++ % 3);
    if (R_FAILED(rc)) rt_fatal("threadCreate: %x", rc);
    threadStart(&t->thr);
    return t;
}

HostThread *host_thread_adopt(void) {
    self = calloc(1, sizeof *self);
    return self;
}

void host_main_thread_prepare(size_t stack) { (void)stack; }

void host_thread_exit(void) { threadExit(); }

/* A resume may land before the pause it answers (SuspendThread counts under a lock, then pauses
 * outside it, and CRI's worker suspends itself while another thread resumes it): it is kept as a
 * wake that the pause then uses up instead of stopping. */
void host_thread_pause(HostThread *t) {
    mutexLock(&t->m);
    if (t == self) {  /* Horizon pauses other threads only: a thread suspending itself waits here */
        while (!t->wakes) condvarWait(&t->cv, &t->m);
        t->wakes--;
    } else if (t->wakes) t->wakes--;
    else if (t->thr.handle) {
        t->stopped = 1;
        svcSetThreadActivity(t->thr.handle, ThreadActivity_Paused);
    }
    mutexUnlock(&t->m);
}

void host_thread_resume(HostThread *t) {
    mutexLock(&t->m);
    if (t->stopped) {
        t->stopped = 0;
        svcSetThreadActivity(t->thr.handle, ThreadActivity_Runnable);
    } else {
        t->wakes++;
        condvarWakeOne(&t->cv);
    }
    mutexUnlock(&t->m);
}

/* ---- the guest window: 4 GB of address space, memory mapped in as the guest needs it ----
 * Heap memory is aliased as code memory (svcMapProcessCodeMemory), which may then be mapped at any
 * address of the window (svcMapProcessMemory); the same sequence the NFSMW port uses. */
#define WINDOW (1ull << 32)
#define CHUNK_ALIGN 0x200000u
static uint8_t *window;

static Handle proc(void) { return envGetOwnProcessHandle(); }

void *host_arena_reserve(void) {
    virtmemLock();
    window = virtmemFindAslr(WINDOW, CHUNK_ALIGN);
    if (window) virtmemAddReservation(window, WINDOW);
    virtmemUnlock();
    if (!window) rt_fatal("cannot reserve the guest window");
    return window;
}

void host_arena_commit(uint32_t va, uint32_t len) {
    uint32_t lo = va & ~0xFFFu, hi = (va + len + 0xFFFu) & ~0xFFFu;
    size_t n = hi - lo, align = (n >= CHUNK_ALIGN && !(lo & (CHUNK_ALIGN - 1))) ? CHUNK_ALIGN : 0x1000;
    void *backing = memalign(align, n);
    if (!backing) rt_fatal("out of memory committing %u KB of guest memory", (unsigned)(n >> 10));
    memset(backing, 0, n);
    virtmemLock();
    void *shadow = virtmemFindCodeMemory(n, align);
    Result rc = shadow ? svcMapProcessCodeMemory(proc(), (u64)shadow, (u64)backing, n) : MAKERESULT(Module_Libnx, 1);
    virtmemUnlock();
    if (R_SUCCEEDED(rc)) rc = svcSetProcessMemoryPermission(proc(), (u64)shadow, n, Perm_Rw);
    if (R_SUCCEEDED(rc)) rc = svcMapProcessMemory(window + lo, proc(), (u64)shadow, n);
    if (R_FAILED(rc)) rt_fatal("mapping guest memory %08X+%X failed: %x", lo, (unsigned)n, rc);
}

/* libnx calls this for any CPU exception in the process. The log gets pc/lr relative to main(), so
 * they can be looked up in the ELF (addr2line -e build/switch/sh2.elf) with main's link address. */
static void (*crash_cb)(uintptr_t);
alignas(16) u8 __nx_exception_stack[0x10000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
extern int main(int, char **);

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    FILE *f = fopen("sh2-crash.log", "w");
    if (f) {
        fprintf(f, "exception %u: pc main+%llX lr main+%llX far %llX\n", ctx->error_desc,
                (unsigned long long)(ctx->pc.x - (u64)main), (unsigned long long)(ctx->lr.x - (u64)main),
                (unsigned long long)ctx->far.x);
        fclose(f);
    }
    if (crash_cb) crash_cb(ctx->far.x);  /* rt_fatal appends the guest view and exits */
}

void host_crash_handlers(void (*on_crash)(uintptr_t addr)) { crash_cb = on_crash; }
void host_backtrace(int fd) { (void)fd; }

int host_console_docked(void) { return appletGetOperationMode() == AppletOperationMode_Console; }

#else
/* ======================================================================== Linux */
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/resource.h>
#include "host_pages.h"
#ifdef __linux__
#include <sched.h>
#endif

static uint64_t arena_page_size;

/* Standard signals coalesce: pipe bytes cannot stand for delivered pause handlers. Count requests
 * instead, and use the pipe only to wake a handler so it can recheck the current balance. A resume
 * arriving before its pause remains a logical credit, including for a self-pause. */
static void on_pause(int sig) {
    (void)sig;
    char c;
    while (self) {
        uint32_t pauses = atomic_load(&self->pauses);
        /* Publish only from the target: the caller must not modify CRI's shared state
         * while another CPU is still executing the thread it asked to suspend. */
        atomic_store(&self->pause_seen, pauses);
        uint32_t pending = pauses - atomic_load(&self->resumes);
        if (!pending || pending >= 0x80000000u) break;
        while (read(self->wake[0], &c, 1) < 0 && errno == EINTR) {}
    }
}

static void alt_stack(void);
void host_use_single_core(int core) {
#ifdef __linux__
    if (core < 0) return;
    cpu_set_t available, pinned;
    if (sched_getaffinity(0, sizeof available, &available)) rt_fatal("cannot read guest CPU affinity");
    int target = -1;
    for (int i = 0; i < CPU_SETSIZE; i++)
        if (CPU_ISSET(i, &available) && core-- == 0) { target = i; break; }
    if (target < 0) rt_fatal("guest CPU affinity index unavailable");
    CPU_ZERO(&pinned);
    CPU_SET(target, &pinned);
    /* Called by ee_init before guest workers/SDL audio start; all descendants inherit this mask. */
    if (sched_setaffinity(0, sizeof pinned, &pinned)) rt_fatal("cannot pin guest CPU affinity");
    rt_log("host: guest thread and future workers pinned to logical CPU %d", target);
#else
    (void)core;
    if (core >= 0) rt_log("host: single-CPU pinning is unavailable on this host");
#endif
}
int host_console_docked(void) { return getenv("SH2_HANDHELD") == NULL; }

static void *thread_entry(void *p) {
    self = p;
    alt_stack();
    self->fn(self->arg);
    atomic_store(&self->finished, 1);
    return NULL;
}

static HostThread *new_thread(void) {
    HostThread *t = calloc(1, sizeof *t);
    atomic_init(&t->pauses, 0);
    atomic_init(&t->resumes, 0);
    atomic_init(&t->pause_seen, 0);
    atomic_init(&t->finished, 0);
    if (pipe(t->wake)) rt_fatal("pipe");
    if (fcntl(t->wake[1], F_SETFL, O_NONBLOCK)) rt_fatal("nonblocking thread wake pipe");
    static int installed;
    if (!installed++) {
        struct sigaction sa = {.sa_handler = on_pause, .sa_flags = SA_RESTART};
        sigaction(SIGUSR2, &sa, NULL);
    }
    return t;
}

HostThread *host_thread_start(void (*fn)(void *), void *arg, size_t stack) {
    HostThread *t = new_thread();
    t->fn = fn;
    t->arg = arg;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, stack);
    if (pthread_create(&t->pt, &a, thread_entry, t)) rt_fatal("pthread_create failed");
    pthread_detach(t->pt);
    return t;
}

HostThread *host_thread_adopt(void) {
    self = new_thread();
    self->pt = pthread_self();
    return self;
}

void host_main_thread_prepare(size_t stack) {
#ifdef __APPLE__
    /* CMake sets the Mach-O main stack size. Moving SDL/Cocoa to a pthread is not a fallback. */
    if (!pthread_main_np()) rt_fatal("Cocoa game loop must run on the main thread");
    size_t actual = pthread_get_stacksize_np(pthread_self());
    if (actual < stack) rt_fatal("main stack %zu is smaller than required %zu; check -stack_size", actual, stack);
    rt_log("host: Cocoa main thread, host stack %zu bytes", actual);
#else
    /* Optional Linux route exercises the same guest/UI ownership before testing on Apple hardware. */
    struct rlimit lim;
    if (getrlimit(RLIMIT_STACK, &lim)) rt_fatal("cannot read main thread stack limit");
    if (lim.rlim_cur < stack) {
        if (lim.rlim_max < stack) rt_fatal("main thread stack hard limit below %zu", stack);
        lim.rlim_cur = stack;
        if (setrlimit(RLIMIT_STACK, &lim)) rt_fatal("cannot raise main thread stack limit");
    }
    rt_log("host: main-thread game loop, host stack limit at least %zu bytes", stack);
#endif
}

void host_thread_exit(void) {
    if (self) atomic_store(&self->finished, 1);
    pthread_exit(NULL);
}

static void wake_thread(HostThread *t) {
    int result;
    do { result = (int)write(t->wake[1], "", 1); } while (result < 0 && errno == EINTR);
    if (result < 0 && errno != EAGAIN) rt_log("thread wake failed");
}

void host_thread_pause(HostThread *t) {
    uint32_t request = atomic_fetch_add(&t->pauses, 1) + 1;
    if (t == self) on_pause(SIGUSR2);
    else {
        if (atomic_load(&t->finished)) return;
        int rc = pthread_kill(t->pt, SIGUSR2);
        if (rc == ESRCH) return;
        if (rc) rt_fatal("cannot signal suspended thread: %d", rc);
        /* A target already parked in this handler blocks SIGUSR2 until it returns.
         * Wake its pipe too, so it can acknowledge a newer pause without a resume. */
        wake_thread(t);
        while ((uint32_t)(atomic_load(&t->pause_seen) - request) >= 0x80000000u &&
               !atomic_load(&t->finished))
            usleep(50);
    }
}

void host_thread_resume(HostThread *t) {
    atomic_fetch_add(&t->resumes, 1);
    wake_thread(t);
}

void host_thread_signal(HostThread *t, int sig) { pthread_kill(t->pt, sig); }

void *host_arena_reserve(void) {
    long page = sysconf(_SC_PAGESIZE);
    uint64_t lo, size;
    if (page <= 0 || !host_page_range(0, 0, (uint64_t)page, &lo, &size))
        rt_fatal("invalid host page size");
    arena_page_size = (uint64_t)page;
    void *p = mmap(NULL, 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) rt_fatal("cannot reserve the 4 GB guest arena");
    return p;
}

void host_arena_commit(uint32_t va, uint32_t len) {
    uint64_t lo, size;
    if (!host_page_range(va, len, arena_page_size, &lo, &size))
        rt_fatal("invalid guest memory range %08X + %08X", va, len);
    if (size && mprotect((uint8_t *)g_xbox_mem_offset + lo, (size_t)size, PROT_READ | PROT_WRITE))
        rt_fatal("cannot commit guest memory %08X", va);
}

static void (*crash_cb)(uintptr_t);
static void on_signal(int sig, siginfo_t *si, void *uc) {
    (void)sig;
    (void)uc;
    crash_cb((uintptr_t)si->si_addr);
}

/* Each thread gets an alternate signal stack, so a stack overflow still reaches the handler. */
static void alt_stack(void) {
    stack_t ss = {.ss_sp = malloc(1 << 16), .ss_size = 1 << 16};
    sigaltstack(&ss, NULL);
}

void host_crash_handlers(void (*on_crash)(uintptr_t addr)) {
    crash_cb = on_crash;
    alt_stack();
    struct sigaction sa = {.sa_sigaction = on_signal, .sa_flags = SA_SIGINFO | SA_ONSTACK};
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);  /* glibc's heap checks (double free...) end here */
}

void host_backtrace(int fd) {
    void *bt[48];
    backtrace_symbols_fd(bt, backtrace(bt, 48), fd);
}
#endif
