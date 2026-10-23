/* USER32, GDI32, ADVAPI32, OLE32: one SDL window and a minimal message loop. */
#include "../runtime/rt.h"
#include <SDL2/SDL.h>

#define HWND_MAIN 0x00010001u
#define WM_QUIT 0x12

SDL_Window *rt_window;
static uint32_t wndproc;
static int quit_posted;
void rt_toggle_fullscreen(void) {
    if (rt_window) SDL_SetWindowFullscreen(rt_window, SDL_GetWindowFlags(rt_window) & SDL_WINDOW_FULLSCREEN ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

int rt_option_int(const char *key, int fallback) {
    FILE *f = fopen("sh2e.ini", "r");
    char line[256], name[64];
    int value;
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, " %63[^= ;] = %d", name, &value) == 2 && !strcmp(name, key)) fallback = value;
    if (f) fclose(f);
    return fallback;
}

static int input_w = 1280, input_h = 720, mouse_game = -1, mouse_captured;
static int motion_x, motion_y, wheel_steps, state_x, state_y, state_z;
static uint32_t mouse_buttons;
typedef struct { uint32_t offset, data, time, sequence; } MouseEvent;
static MouseEvent mouse_queue[256];
static unsigned mouse_read, mouse_write, mouse_sequence;

void rt_input_viewport(int width, int height) { input_w = width; input_h = height; }
int rt_mouse_game_enabled(void) {
    if (mouse_game < 0) {
        mouse_game = rt_option_int("mouse_mode", 1) != 0;
        rt_log("mouse: gameplay mode %s (F8 toggles; middle button: search camera)", mouse_game ? "on" : "off");
    }
    return mouse_game;
}
void rt_mouse_capture(int on) {
    on = on && rt_window && SDL_GetKeyboardFocus() == rt_window && !getenv("SH2_KEYS");
    if (on == mouse_captured) return;
    if (SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE) < 0 && on) {
        rt_log("mouse: relative mode unavailable: %s", SDL_GetError());
        return;
    }
    mouse_captured = on;
    rt_log("mouse: cursor %s", on ? "captured" : "released");
    motion_x = motion_y = 0;
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}
static void mouse_add(uint32_t offset, int data) {
    if (mouse_write - mouse_read == 256) mouse_read++;
    mouse_queue[mouse_write++ % 256] = (MouseEvent){offset, (uint32_t)data, SDL_GetTicks(), ++mouse_sequence};
}
uint32_t rt_mouse_events(uint32_t stride, uint32_t output, uint32_t count, int peek) {
    uint32_t available = mouse_write - mouse_read;
    if (!output) return available;
    if (stride < 16 || stride > 24) return 0;
    if (count > available) count = available;
    for (uint32_t i = 0; i < count; i++) {
        memset(GPTR(output + i * stride), 0, stride);
        memcpy(GPTR(output + i * stride), &mouse_queue[(mouse_read + i) % 256], 16);
    }
    if (!peek) mouse_read += count;
    return count;
}
void rt_mouse_state(uint32_t output, uint32_t size) {
    memset(GPTR(output), 0, size);
    if (size < 12) return;
    MEM32(output) = state_x; MEM32(output + 4) = state_y; MEM32(output + 8) = state_z;
    state_x = state_y = state_z = 0;
    for (uint32_t i = 0; i < 8 && i + 12 < size; i++) MEM8(output + 12 + i) = mouse_buttons & (1u << i) ? 128 : 0;
}
uint32_t rt_mouse_motion(int *x, int *y, int *wheel) {
    *x = motion_x; *y = motion_y; *wheel = wheel_steps;
    motion_x = motion_y = wheel_steps = 0;
    return mouse_buttons;
}
static void mouse_event(const SDL_Event *e) {
    if (getenv("SH2_KEYS") || !rt_window || SDL_GetMouseFocus() != rt_window) return;
    if (e->type == SDL_MOUSEMOTION) {
        int x = e->motion.xrel, y = e->motion.yrel;
        motion_x += x; motion_y += y; state_x += x; state_y += y;
        if (!mouse_captured) {
            int ww, wh;
            SDL_GetWindowSize(rt_window, &ww, &wh);
            float scale = (float)ww / input_w;
            if ((float)wh / input_h < scale) scale = (float)wh / input_h;
            if (scale <= 0) return;
            int px = (int)((e->motion.x - (ww - input_w * scale) / 2) * 640 / (input_w * scale));
            int py = (int)((e->motion.y - (wh - input_h * scale) / 2) * 480 / (input_h * scale));
            if (px < 0) px = 0; if (px > 640) px = 640;
            if (py < 0) py = 0; if (py > 480) py = 480;
            /* Original buffered DI mouse consumes deltas in a 640x480 logical space. */
            int qx = SMEM32(0x00941358u), qy = SMEM32(0x0094135Cu);
            for (unsigned i = mouse_read; i < mouse_write; i++) {
                MouseEvent *q = &mouse_queue[i % 256];
                if (q->offset == 0) qx += (int32_t)q->data;
                if (q->offset == 4) qy += (int32_t)q->data;
            }
            if (px != qx) mouse_add(0, px - qx);
            if (py != qy) mouse_add(4, py - qy);
        }
    } else if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) {
        unsigned button = e->button.button == SDL_BUTTON_LEFT ? 0 : e->button.button == SDL_BUTTON_RIGHT ? 1 :
                          e->button.button == SDL_BUTTON_MIDDLE ? 2 : e->button.button - 1;
        if (button >= 8) return;
        int down = e->type == SDL_MOUSEBUTTONDOWN;
        if (down) mouse_buttons |= 1u << button; else mouse_buttons &= ~(1u << button);
        mouse_add(12 + button, down ? 128 : 0);
    } else if (e->type == SDL_MOUSEWHEEL) {
        int amount = e->wheel.y * (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
        wheel_steps += amount; state_z += amount * 120;
        mouse_add(8, amount * 120);
    }
}

void rt_pump_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F11)
            rt_toggle_fullscreen();
        mouse_event(&e);
        if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F8) {
            mouse_game = !rt_mouse_game_enabled();
            rt_log("mouse: gameplay mode %s (F8)", mouse_game ? "on" : "off");
            if (!mouse_game) rt_mouse_capture(0);
        }
        if (e.type == SDL_QUIT) { rt_log("window closed"); quit_posted = 1; }
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
            rt_window && e.window.windowID == SDL_GetWindowID(rt_window)) {
            rt_mouse_capture(0);
            mouse_buttons = 0; motion_x = motion_y = wheel_steps = state_x = state_y = state_z = 0;
            mouse_read = mouse_write;
#if SDL_VERSION_ATLEAST(2, 24, 0)
            SDL_ResetKeyboard();
#endif
        }
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) SDL_StopTextInput();
    }
}

WINAPI(LoadCursorA, "LoadCursorA", 2) { return 1; }
WINAPI(RegisterClassExA, "RegisterClassExA", 1) {
    wndproc = MEM32(ARG(0) + 8);  /* WNDCLASSEXA.lpfnWndProc */
    return 0xC001;
}
/* The game's window, opened by the launcher (src/d3d8/launcher.c) or the game's CreateWindowExA. */
void rt_window_open(void) {
    if (!rt_window) {
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");  /* let SIGTERM/SIGINT stop the port */
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER))
            rt_fatal("SDL_Init: %s", SDL_GetError());
        /* Gameplay reads physical keys, never Cocoa text composition / press-and-hold accents. */
        SDL_StopTextInput();
        /* A scripted run (SH2_KEYS) stays off screen, unless SH2_SHOW is set to watch it: either way it
         * never reads the real keyboard. */
        rt_window = SDL_CreateWindow("Silent Hill 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                         (getenv("SH2_KEYS") && !getenv("SH2_SHOW") ? SDL_WINDOW_HIDDEN : 0));
        if (!rt_window) rt_fatal("SDL_CreateWindow: %s", SDL_GetError());
        SDL_StopTextInput();
    }
}
WINAPI(CreateWindowExA, "CreateWindowExA", 12) {
    if (rt_trace) rt_log("  CreateWindowExA(%s, %dx%d)", ARG(2) ? GSTR(ARG(2)) : "", ARG(6), ARG(7));
    rt_window_open();
    return HWND_MAIN;
}
WINAPI(ShowWindow, "ShowWindow", 2) { return 1; }
WINAPI(UpdateWindow, "UpdateWindow", 1) { return 1; }
WINAPI(DefWindowProcA, "DefWindowProcA", 4) { return 0; }

/* MSG: hwnd, message, wParam, lParam, time, pt. Input goes through DirectInput, so the only
 * message the loop sees is WM_QUIT. */
WINAPI(PeekMessageA, "PeekMessageA", 5) {
    rt_pump_events();
    if (!quit_posted) return 0;
    uint32_t m = ARG(0);
    memset(GPTR(m), 0, 28);
    MEM32(m) = HWND_MAIN;
    MEM32(m + 4) = WM_QUIT;
    return 1;
}
WINAPI(TranslateMessage, "TranslateMessage", 1) { return 0; }
static uint32_t call_wndproc(uint32_t hwnd, uint32_t msg, uint32_t w, uint32_t l) {
    uint32_t a[4] = {hwnd, msg, w, l};
    return wndproc ? guest_call(wndproc, 4, a, 1) : 0;
}
WINAPI(DispatchMessageA, "DispatchMessageA", 1) {
    uint32_t m = ARG(0);
    return call_wndproc(MEM32(m), MEM32(m + 4), MEM32(m + 8), MEM32(m + 12));
}
WINAPI(SendMessageA, "SendMessageA", 4) { return call_wndproc(ARG(0), ARG(1), ARG(2), ARG(3)); }
WINAPI(PostQuitMessage, "PostQuitMessage", 1) { rt_log("PostQuitMessage(%d)", ARG(0)); quit_posted = 1; return 0; }
WINAPI(BeginPaint, "BeginPaint", 2) { memset(GPTR(ARG(1)), 0, 64); return 1; }
WINAPI(EndPaint, "EndPaint", 2) { return 1; }
WINAPI(MessageBoxA, "MessageBoxA", 4) {
    rt_log("MessageBox [%s]: %s", ARG(2) ? GSTR(ARG(2)) : "", ARG(1) ? GSTR(ARG(1)) : "");
    return 1;  /* IDOK */
}
CDECL(wsprintfA, "wsprintfA") {
    char tmp[1025];
    int k = rt_format(tmp, sizeof tmp, GSTR(ARG(1)), g_esp + 12);
    strcpy(GPTR(ARG(0)), tmp);
    return k;
}

WINAPI(DeleteObject, "DeleteObject", 1) { return 1; }

/* No registry: every key is missing, so the game uses its defaults. */
WINAPI(RegOpenKeyA, "RegOpenKeyA", 3) {
    if (rt_trace) rt_log("  RegOpenKeyA(%08X, %s)", ARG(0), ARG(1) ? GSTR(ARG(1)) : "");
    return 2;  /* ERROR_FILE_NOT_FOUND */
}
WINAPI(RegQueryValueExA, "RegQueryValueExA", 6) { return 2; }
WINAPI(RegCloseKey, "RegCloseKey", 1) { return 0; }

WINAPI(CoInitialize, "CoInitialize", 1) { return 0; }
