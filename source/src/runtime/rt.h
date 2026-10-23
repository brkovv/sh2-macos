/* Host runtime for the recompiled sh2pc.exe: guest memory, threads and the Win32 bridges. */
#pragma once
#include "recomp_types.h"
#include "bridges.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Guest <-> host pointers. Guest address 0 stays NULL. */
#define GPTR(va) ((va) ? (void *)XBOX_PTR(va) : NULL)
static inline uint32_t GVA(const void *p) {
    return p ? (uint32_t)((uintptr_t)p - (uintptr_t)g_xbox_mem_offset) : 0;
}
#define GSTR(va) ((const char *)GPTR(va))

/* Arguments of a bridged call: the guest pushed them, then the return address. */
#define ARG(n) MEM32(g_esp + 4 + 4 * (n))
#define ARGF(n) MEMF(g_esp + 4 + 4 * (n))

/* A bridge is a void(void) guest-callable function, found by its export name.
 * WINAPI(cname, "export", n): stdcall, the bridge pops n arguments.
 * CDECL(cname, "export"): the caller pops the arguments.
 * The body reads ARG(i) and returns EAX. */
#define WINAPI(cname, ename, n) \
    static uint32_t cname##_impl(void); \
    static void cname(void) { uint32_t _r = cname##_impl(); g_eax = _r; g_esp += 4 + 4 * (n); } \
    BRIDGE_REG(cname, ename); \
    static uint32_t cname##_impl(void)
#define CDECL(cname, ename) WINAPI(cname, ename, 0)

/* x87 stack, for bridges that return a double in ST(0) or take one there. */
static inline void rt_fpush(double v) { g_fp_top = (g_fp_top + 7) & 7; g_fp_stack[g_fp_top] = v; }
static inline double rt_fpop(void) { double v = g_fp_stack[g_fp_top]; g_fp_top = (g_fp_top + 1) & 7; return v; }

/* rt_core.c */
#ifdef SH2_LEAN  /* the release NRO (CMake SH2_LEAN): no log and no call trace, compiled out */
static inline __attribute__((format(printf, 1, 2))) void rt_log(const char *fmt, ...) { (void)fmt; }
#define rt_trace 0
#else
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern int rt_trace;                 /* SH2_TRACE=1: log every bridged call */
#endif
_Noreturn void rt_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_dump_thread(void);
extern int rt_frame;                 /* frames presented so far */
extern char rt_game_dir[512];        /* folder with sh2pc.exe and data/ */
uint32_t galloc(uint32_t size);      /* guest heap, 16-byte aligned, zeroed */
void gfree(uint32_t va);
uint32_t grealloc(uint32_t va, uint32_t size);
uint32_t gsize(uint32_t va);
uint32_t gstrdup(const char *s);
uint32_t rt_heap_used(void);   /* bytes of the guest heap handed out so far */
recomp_func_t rt_resolve(uint32_t va);
/* Calls guest code at va with args (pushed right to left). cdecl: we pop them; stdcall: the callee does. */
uint32_t guest_call(uint32_t va, int nargs, const uint32_t *args, int stdcall);
/* A host function the guest can call through a pointer (vtables, callbacks). */
uint32_t rt_host_fn(const char *name, void (*fn)(void));

/* COM objects live in guest memory: vtable pointer at +0, then the object's own fields.
 * Methods are stdcall with `this` as ARG(0); METHOD(cname, n) pops n arguments including this. */
#define METHOD(cname, n) \
    static uint32_t cname##_impl(void); \
    static void cname(void) { uint32_t _r = cname##_impl(); g_eax = _r; g_esp += 4 + 4 * (n); } \
    static uint32_t cname##_impl(void)
#define THIS ARG(0)
typedef struct { const char *name; void (*fn)(void); } ComEntry;  /* fn NULL: stops the port when called */
uint32_t com_vtable(const ComEntry *e, int n);
uint32_t com_new(uint32_t vtable, uint32_t size);  /* zeroed object of size bytes, vtable set */

/* msvcrt.c: vsnprintf with the arguments read from the guest va_list at args. */
int rt_format(char *out, size_t n, const char *fmt, uint32_t args);

/* user32.c */
extern struct SDL_Window *rt_window;
void rt_pump_events(void);
void rt_window_open(void);
void rt_toggle_fullscreen(void);
int rt_option_int(const char *key, int fallback);
void rt_input_viewport(int width, int height);
void rt_mouse_capture(int on);
uint32_t rt_mouse_events(uint32_t stride, uint32_t output, uint32_t count, int peek);
void rt_mouse_state(uint32_t output, uint32_t size);
uint32_t rt_mouse_motion(int *x, int *y, int *wheel);
int rt_mouse_game_enabled(void);
void ee_mouse_keyboard(uint32_t output, uint32_t size);
void ee_mouse_menu(void);
void ee_movie_vertices(uint32_t vertices, uint32_t width, uint32_t height);

/* kernel32.c */
void rt_thread_init_main(uint32_t stack_size);
/* Runs fn(arg) on a new host thread that has its own guest registers, stack and TIB, so fn can guest_call. */
void rt_spawn(void (*fn)(void *), void *arg, uint32_t stack_size);
/* Maps a guest path (backslashes, drive letters, any case) to a host path under rt_game_dir. */
const char *rt_path(const char *guest, char *out, size_t n);
int ee_russian_text(void);
void ee_language_init(void);
/* fread into guest memory; every host read the guest asks for goes through it (see kernel32.c). */
size_t rt_fread(uint32_t va, size_t n, FILE *f);
void rt_set_last_error(uint32_t e);
