/* brkovv launcher: the installed game's title art and narrow menu font.
 * Game assets are read locally and never embedded in the source update.
 * Source foundation: cutarev/sh2-nx. The game renderer is left a fresh GL state. */
#include "d3d8.h"
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <math.h>

#define FADE_MS 700  /* every screen fades in and out */
#define CW 1280
#define CH 720

#include "launcher_assets.h"  /* generated portable C; also works with Mach-O */
#include "../../assets/launcher_font_metrics.h"

/* ---- the game's menu sounds: entries of the effect table at 0x8A67DC (offsets into data/sound/sddata.bin) ---- */

#define SFX_TABLE 0x008A67DCu
enum { SND_CURSOR, SND_OK };
static SDL_AudioDeviceID snd_dev;
static struct { Uint8 *pcm; Uint32 len; } snd[2];

/* One RIFF of a sound bank, scaled by gain; with want_bytes, only if its sample data is that long. */
static int load_riff(FILE *f, uint32_t off, uint32_t want_bytes, float gain, SDL_AudioSpec *spec, int i) {
    uint8_t hdr[8], *riff;
    uint32_t n;
    if (!f || fseek(f, off, SEEK_SET) || fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "RIFF", 4)) return 0;
    memcpy(&n, hdr + 4, 4);
    if (n > (1u << 20) || !(riff = malloc(n + 8))) return 0;
    memcpy(riff, hdr, 8);
    int ok = fread(riff + 8, 1, n, f) == n && SDL_LoadWAV_RW(SDL_RWFromConstMem(riff, n + 8), 1, spec, &snd[i].pcm, &snd[i].len);
    free(riff);
    if (ok && ((want_bytes && snd[i].len != want_bytes) || spec->format != AUDIO_S16LSB)) {
        SDL_FreeWAV(snd[i].pcm);
        snd[i].pcm = NULL;
        ok = 0;
    }
    for (Uint32 k = 0; ok && k < snd[i].len / 2; k++) ((int16_t *)snd[i].pcm)[k] = (int16_t)(((int16_t *)snd[i].pcm)[k] * gain);
    return ok;
}

static void sound_init(void) {
#ifdef SH2_STOCK
    /* The launcher's v1.0 sound-table address is not valid for Director's Cut.
     * Game audio itself uses the original guest code and DirectSound bridge. */
    return;
#else
    /* What the title menu plays on a move and a choice: effect table entries 292 and 295, which are RIFFs 291
     * and 294 of the Enhanced Edition bank (48 kHz) at these offsets, if it is that version of the pack. */
    static const struct { int id; uint32_t ee_off, ee_bytes; } fx[2] = {{292, 47286718, 85286}, {295, 47632428, 102356}};
    /* the game's level: sound effect volume 12 (-1.98 dB), head-relative 3D at the listener (0.707 a side) */
    const float gain = 0.796f * 0.707f;
    SDL_AudioSpec spec;
    FILE *f = fopen("sh2e/sound/sddata.bin", "rb");
    int n = 0;
    for (int i = 0; i < 2; i++) n += load_riff(f, fx[i].ee_off, fx[i].ee_bytes, gain, &spec, i);
    if (f) fclose(f);
    if (n < 2) {  /* the original bank (16 kHz): its offsets are in the exe's table */
        for (int i = 0; i < 2; i++) { SDL_FreeWAV(snd[i].pcm); snd[i].pcm = NULL; }
        f = fopen("data/sound/sddata.bin", "rb");
        for (int i = 0; i < 2; i++) load_riff(f, MEM32(SFX_TABLE + 4 * fx[i].id), 0, gain, &spec, i);
        if (f) fclose(f);
    }
    if (!snd[SND_CURSOR].pcm || !snd[SND_OK].pcm) { rt_log("launcher: no menu sounds in sddata.bin"); return; }
    if ((snd_dev = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0))) SDL_PauseAudioDevice(snd_dev, 0);  /* one format: same bank */
    else rt_log("launcher: audio: %s", SDL_GetError());
#endif
}

static void sound(int i) {
    if (!snd_dev) return;
    SDL_ClearQueuedAudio(snd_dev);
    SDL_QueueAudio(snd_dev, snd[i].pcm, snd[i].len);
}

static void sound_close(void) {
    for (Uint64 t0 = SDL_GetTicks64(); snd_dev && SDL_GetQueuedAudioSize(snd_dev) && SDL_GetTicks64() - t0 < 1000;) SDL_Delay(10);
    if (snd_dev) SDL_CloseAudioDevice(snd_dev);
    for (int i = 0; i < 2; i++) SDL_FreeWAV(snd[i].pcm);
}

/* Decodes a PNG to RGBA rows; NULL if it can't. */
static uint32_t *decode(const uint8_t *png, const uint8_t *end, int *w, int *h) {
    int n = (int)(end - png);
    uint8_t *buf = calloc(1, n + AV_INPUT_BUFFER_PADDING_SIZE), *px = NULL;
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_PNG);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    if (buf && ctx && pkt && fr && avcodec_open2(ctx, codec, NULL) == 0) {
        memcpy(buf, png, n);
        pkt->data = buf;
        pkt->size = n;
        if (avcodec_send_packet(ctx, pkt) == 0 && avcodec_receive_frame(ctx, fr) == 0) {
            *w = fr->width;
            *h = fr->height;
            struct SwsContext *sws = sws_getContext(*w, *h, fr->format, *w, *h, AV_PIX_FMT_RGBA, SWS_POINT, NULL, NULL, NULL);
            if (sws && (px = malloc((size_t)*w * *h * 4))) {
                int stride = *w * 4;
                sws_scale(sws, (const uint8_t *const *)fr->data, fr->linesize, 0, *h, &px, &stride);
            }
            sws_freeContext(sws);
        }
    }
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    free(buf);
    return (uint32_t *)px;
}

static GLuint tex, fbo;
static int tex_w, tex_h;

/* Uploads w x h RGBA rows (row 0 at the top) and shows them letterboxed. */
static void present(const uint32_t *px, int w, int h) {
    if (w != tex_w || h != tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        tex_w = w;
        tex_h = h;
    } else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int ww, wh;
    SDL_GL_GetDrawableSize(rt_window, &ww, &wh);
    glClear(GL_COLOR_BUFFER_BIT);
    /* the texture's row 0 is the image's top, the window's row 0 its bottom */
    float s = fminf((float)ww / w, (float)wh / h);
    int dw = w * s, dh = h * s, dx = (ww - dw) / 2, dy = (wh - dh) / 2;
    glBlitFramebuffer(0, 0, w, h, dx, dy + dh, dx + dw, dy, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    SDL_GL_SwapWindow(rt_window);
    SDL_Delay(8);  /* Static launch screens do not need an uncapped busy loop. */
}

/* ---- the canvas ---- */

static uint32_t canvas[CW * CH];
static uint32_t *font_px;
static int font_w;
static unsigned char font_advance[480];

static uint32_t rgb(int r, int g, int b) { return 0xFF000000u | b << 16 | g << 8 | r; }  /* RGBA bytes */

/* Text at x (left edge) and baseline y; size = glyph height in px (80 = the atlas'), sampled bilinearly. */
static unsigned next_char(const char **string) {
    const unsigned char *s = (const unsigned char *)*string;
    unsigned c = *s++;
    if (c >= 0xC2 && c <= 0xDF && (s[0] & 0xC0) == 0x80) {
        c = ((c & 31) << 6) | (s[0] & 63); s++;
    } else if (c >= 0xE0 && c <= 0xEF && s[0] && s[1] && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
        c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2;
    } else if (c >= 128) c = '?';
    *string = (const char *)s;
    return c;
}
static int font_index(unsigned code) {
    if (code >= 32 && code < 256) return code - 32;
    if (code >= 0x400 && code < 0x500) return 224 + code - 0x400;
    return '?' - 32;
}
static int text(int x, int y, int size, uint32_t color, const char *s) {
    float k = size / 96.0f;
    int cw = (int)(72 * k + 0.5f), ch = size, top = y - (int)(72 * k);
    while (*s) {
        int c = font_index(next_char(&s));
        int adv = (int)(font_advance[c] * k + 0.5f);
        if (!font_px || !c) { x += adv; continue; }
        int gx = c % 16 * 72, gy = c / 16 * 96;
        for (int py = 0; py < ch; py++) {
            int oy = top + py;
            if (oy < 0 || oy >= CH) continue;
            float fy = (py + 0.5f) / k - 0.5f;
            int y0 = (int)fy;
            float ty = fy - y0;
            if (y0 < 0) y0 = 0, ty = 0;
            if (y0 >= 95) y0 = 94, ty = 1;
            for (int px = 0; px < cw; px++) {
                int ox = x + px;
                if (ox < 0 || ox >= CW) continue;
                float fx = (px + 0.5f) / k - 0.5f;
                int x0 = (int)fx;
                float tx = fx - x0;
                if (x0 < 0) x0 = 0, tx = 0;
                if (x0 >= 71) x0 = 70, tx = 1;
                const uint32_t *r0 = font_px + (size_t)(gy + y0) * font_w + gx + x0, *r1 = r0 + font_w;
                float a = ((r0[0] >> 24) * (1 - tx) + (r0[1] >> 24) * tx) * (1 - ty) +
                          ((r1[0] >> 24) * (1 - tx) + (r1[1] >> 24) * tx) * ty;
                if (a < 1) continue;
                uint32_t *d = &canvas[oy * CW + ox], o = 0xFF000000u;
                for (int sh = 0; sh < 24; sh += 8) {
                    float v = ((*d >> sh) & 0xFF) + (((color >> sh) & 0xFF) - (float)((*d >> sh) & 0xFF)) * a / 255;
                    o |= (uint32_t)v << sh;
                }
                *d = o;
            }
        }
        x += adv;
    }
    return x;
}
/* An RGBA image scaled bilinearly into the canvas rect x, y, dw, dh, blended by its alpha. */
static void image(const uint32_t *img, int w, int h, int x, int y, int dw, int dh) {
    for (int py = 0; py < dh; py++) {
        float fy = (py + 0.5f) * h / dh - 0.5f;
        int y0 = fy < 0 ? 0 : (int)fy, y1 = y0 + 1 < h ? y0 + 1 : y0;
        float ty = fy < 0 ? 0 : fy - y0;
        for (int px = 0; px < dw; px++) {
            float fx = (px + 0.5f) * w / dw - 0.5f;
            int x0 = fx < 0 ? 0 : (int)fx, x1 = x0 + 1 < w ? x0 + 1 : x0;
            float tx = fx < 0 ? 0 : fx - x0;
            const uint32_t q[4] = {img[y0 * w + x0], img[y0 * w + x1], img[y1 * w + x0], img[y1 * w + x1]};
            float k[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty}, a = 0, c[3] = {0};
            for (int i = 0; i < 4; i++) {
                float qa = (q[i] >> 24) * k[i];
                a += qa;
                for (int j = 0; j < 3; j++) c[j] += ((q[i] >> 8 * j) & 0xFF) * qa;
            }
            if (a < 1) continue;
            uint32_t *d = &canvas[(y + py) * CW + x + px], o = 0xFF000000u;
            for (int j = 0; j < 3; j++) {
                float dv = (*d >> 8 * j) & 0xFF;
                o |= (uint32_t)(dv + (c[j] / a - dv) * a / 255) << 8 * j;
            }
            *d = o;
        }
    }
}

static int text_w(int size, const char *s) {
    int width = 0;
    while (*s) width += (int)(font_advance[font_index(next_char(&s))] * size / 96.0f + 0.5f);
    return width;
}
static void text_c(int y, int size, uint32_t color, const char *s) { text((CW - text_w(size, s)) / 2, y, size, color, s); }

/* ---- sh2e.ini ---- */

typedef struct {
    const char *label, *key;
    const char *vals[9], *names[9];  /* NULL-terminated; vals[def] is what the game uses without the key */
    int def, cur, was;
    char other[32];  /* a value the file has that the list doesn't: shown until changed */
} Opt;

static Opt opts[] = {
    {"Resolution", "resolution", {"1920x1080", "1280x720", "960x720", "auto", "1280x800", "1600x1000", "1920x1200"},
        {"1920x1080 (16:9)", "1280x720 (16:9)", "960x720 (4:3)", "Match display shape", "1280x800 (16:10)", "1600x1000 (16:10)", "1920x1200 (16:10)"}},
#ifdef __SWITCH__
    {"Handheld render", "handheld", {"720", "480", "0"}, {"720p", "480p (faster)", "Game resolution"}},
    {"Docked render", "docked", {"0", "1080", "720"}, {"Game resolution", "1080p", "720p (faster)"}},
#endif
    {"Movies", "movie", {"0", "1"}, {"Original", "Enhanced (1080p60)"}},
    {"EE load/save sounds", "PreserveSoundsOnLoad", {"1", "0"}, {"On", "Off"}},
    {"Game threads", "cores", {"3", "1"}, {"Three cores", "One core"}},
    {"Movie scaling", "movie_scale", {"2", "1", "0"}, {"Fill (crop edges)", "Fit (keep all)", "Original"}},
    {"Mouse gameplay", "mouse_mode", {"0", "1"}, {"Keyboard / pad", "Mouse + keyboard (F8)"}, 1},
    {"Window", "fullscreen", {"0", "1"}, {"Windowed", "Fullscreen"}},
    {"Frame rate", "fps", {"30", "60"}, {"30 (original)", "60 (experimental)"}},
    {"Text language", "text_language", {"-1", "6", "1", "2", "3", "4", "5", "0"},
        {"Game settings", "Russian", "English", "French", "German", "Italian", "Spanish", "Japanese"}},
};
#define N_OPTS (int)(sizeof opts / sizeof *opts)
static int ui_ru;
static const char *tr(const char *en, const char *ru) { return ui_ru ? ru : en; }

static int n_vals(const Opt *o) { int n = 0; while (n < 9 && o->vals[n]) n++; return n; }
static const char *opt_name(const Opt *o) { return o->cur < 0 ? o->other : o->names[o->cur]; }

/* The value after "key =" on an uncommented line, as the game's own parsers (sscanf " key = ") read it. */
static int ini_value(const char *line, const char *key, char *val) {
    char fmt[64];
    snprintf(fmt, sizeof fmt, " %s = %%31[^ \t\r\n;]", key);
    return sscanf(line, fmt, val) == 1;
}

static void ini_load(void) {
    FILE *f = fopen("sh2e.ini", "r");
    char line[256], val[32];
    for (int i = 0; i < N_OPTS; i++) opts[i].cur = opts[i].def;
    while (f && fgets(line, sizeof line, f))
        for (int i = 0; i < N_OPTS; i++)
            if (ini_value(line, opts[i].key, val)) {
                Opt *o = &opts[i];
                o->cur = -1;
                snprintf(o->other, sizeof o->other, "%s", val);
                for (int j = 0; j < n_vals(o); j++)
                    if (!strcmp(o->vals[j], val)) o->cur = j;
            }
    if (f) fclose(f);
    for (int i = 0; i < N_OPTS; i++) opts[i].was = opts[i].cur;
    ui_ru = rt_option_int("text_language", -1) == 6;
}

/* Rewrites the changed keys in place (the last line that sets each, as that's the one the game ends up
 * with) and appends the ones the file lacks; comments and every other line stay as they are. */
static void ini_save(void) {
    char *lines[512];
    int n = 0;
    FILE *f = fopen("sh2e.ini", "r");
    char line[256], val[32];
    while (f && n < 512 && fgets(line, sizeof line, f)) lines[n++] = strdup(line);
    if (f) fclose(f);
    if (!(f = fopen("sh2e.ini", "w"))) { rt_log("launcher: can't write sh2e.ini"); return; }
    for (int i = 0; i < N_OPTS; i++) {
        Opt *o = &opts[i];
        if (o->cur == o->was) continue;
        int last = -1;
        for (int j = 0; j < n; j++)
            if (ini_value(lines[j], o->key, val)) last = j;
        char set[96];
        snprintf(set, sizeof set, "%s=%s\n", o->key, o->vals[o->cur]);
        if (last >= 0) { free(lines[last]); lines[last] = strdup(set); }
        else if (n < 512) {
            if (n && !strchr(lines[n - 1], '\n')) {  /* a last line without its newline */
                lines[n - 1] = realloc(lines[n - 1], strlen(lines[n - 1]) + 2);
                strcat(lines[n - 1], "\n");
            }
            lines[n++] = strdup(set);
        }
        o->was = o->cur;
    }
    for (int j = 0; j < n; j++) { fputs(lines[j], f); free(lines[j]); }
    fclose(f);
}

/* ---- input ---- */

enum { IN_NONE, IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_OK, IN_BACK };
static int mouse_select = -1, options_open, credits_open, input_transition;
static int option_top(void) { return 190; }
static int option_step(void) { return 380 / (N_OPTS + 1); }
static int menu_top(void) { return 432; }
enum { MENU_PLAY, MENU_OPTIONS, MENU_CREDITS, MENU_QUIT, MENU_COUNT };

/* Internal test events are delivered only to this process's SDL queue. */
static void scripted_launcher_input(void) {
#ifndef __SWITCH__
    static char *script, *next;
    static int initialized;
    static Uint64 began;
    if (!initialized) {
        const char *keys = getenv("SH2_LAUNCHER_KEYS");
        next = script = keys ? strdup(keys) : NULL;
        began = SDL_GetTicks64(); initialized = 1;
    }
    if(input_transition || SDL_HasEvent(SDL_KEYDOWN))return;
    while (next && *next) {
        char *colon = strchr(next, ':');
        if (!colon) break;
        double seconds = strtod(next, NULL);
        if ((SDL_GetTicks64() - began) / 1000.0 < seconds) break;
        char name[48]; char *end = strchr(colon + 1, ',');
        size_t n = end ? (size_t)(end-colon-1) : strlen(colon+1);
        if (n >= sizeof name) break;
        memcpy(name, colon+1, n); name[n]=0;
        SDL_Event event = {0}; event.type=SDL_KEYDOWN;
        event.key.keysym.sym=SDL_GetKeyFromName(name); SDL_PushEvent(&event);
        rt_log("launcher test: %.1f %s",seconds,name);
        next = end ? end+1 : NULL;
        break;  /* Deliver the next event only after the current action was consumed. */
    }
#endif
}

static int poll_input(void) {
    scripted_launcher_input();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN: {
            int ww, wh, mx, my;
            SDL_GetWindowSize(rt_window, &ww, &wh);
            SDL_GetMouseState(&mx, &my);
            float scale = fminf((float)ww / CW, (float)wh / CH);
            if (scale <= 0) break;
            int x = (mx - (ww - CW * scale) / 2) / scale;
            int y = (my - (wh - CH * scale) / 2) / scale;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) return IN_BACK;
            if (credits_open && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) return IN_OK;
            mouse_select = -1;
            if (options_open && x >= 120 && x < CW - 120 && y >= option_top() && y < option_top() + option_step() * (N_OPTS + 1))
                mouse_select = (y - option_top()) / option_step();
            if (!options_open && !credits_open && x >= 440 && x < 840 && y >= menu_top() && y < menu_top()+44*MENU_COUNT)
                mouse_select = (y - menu_top()) / 44;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && mouse_select >= 0) return IN_OK;
            break;
        }
        case SDL_MOUSEWHEEL: return e.wheel.y > 0 ? IN_LEFT : IN_RIGHT;
        case SDL_QUIT: exit(0);
        case SDL_CONTROLLERDEVICEADDED: SDL_GameControllerOpen(e.cdevice.which); break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
            case SDLK_F11: rt_toggle_fullscreen(); break;
            case SDLK_UP: return IN_UP;
            case SDLK_DOWN: return IN_DOWN;
            case SDLK_LEFT: return IN_LEFT;
            case SDLK_RIGHT: return IN_RIGHT;
            case SDLK_RETURN: case SDLK_SPACE: return IN_OK;
            case SDLK_ESCAPE: case SDLK_BACKSPACE: return IN_BACK;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP: return IN_UP;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return IN_DOWN;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return IN_LEFT;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return IN_RIGHT;
#ifdef __SWITCH__  /* Nintendo's way round, as in the game (dinput.c): confirm on A (east), back on B (south) */
            case SDL_CONTROLLER_BUTTON_B: return IN_OK;
            case SDL_CONTROLLER_BUTTON_A: return IN_BACK;
#else
            case SDL_CONTROLLER_BUTTON_A: return IN_OK;
            case SDL_CONTROLLER_BUTTON_B: return IN_BACK;
#endif
            case SDL_CONTROLLER_BUTTON_START: return IN_OK;
            }
            break;
        case SDL_CONTROLLERAXISMOTION: {  /* the left stick, once per push past half way */
            static int held[2];
            int ax = e.caxis.axis;
            if (ax != SDL_CONTROLLER_AXIS_LEFTX && ax != SDL_CONTROLLER_AXIS_LEFTY) break;
            int dir = e.caxis.value > 16000 ? 1 : e.caxis.value < -16000 ? -1 : 0, was = held[ax];
            held[ax] = dir;
            if (dir && dir != was) return ax == SDL_CONTROLLER_AXIS_LEFTX ? (dir > 0 ? IN_RIGHT : IN_LEFT) : (dir > 0 ? IN_DOWN : IN_UP);
            break;
        }
        }
    }
    return IN_NONE;
}

/* ---- screens ---- */

#define C_BG rgb(12, 13, 10)
#define C_TEXT rgb(172, 175, 160)
#define C_DIM rgb(116, 124, 112)
#define C_SEL rgb(240, 240, 216)
#define C_RUST rgb(133, 68, 47)
static uint32_t backdrop[CW * CH];
static int title_loaded;

static void shade_rect(int x,int y,int w,int h,int opacity) {
    for(int py=y;py<y+h && py<CH;py++) for(int px=x;px<x+w && px<CW;px++) {
        if(px<0 || py<0) continue;
        uint32_t p=canvas[py*CW+px],v=0xFF000000u;
        for(int c=0;c<24;c+=8) v|=(((p>>c)&255)*(255-opacity)/255)<<c;
        canvas[py*CW+px]=v;
    }
}
static void line(int x,int y,int width,uint32_t color) {
    if(y<0 || y>=CH) return;
    for(int i=0;i<width;i++) if(x+i>=0 && x+i<CW) canvas[y*CW+x+i]=color;
}
static void capture_canvas(const char *name) {
    const char *folder=getenv("SH2_LAUNCHER_SHOT");
    if(!folder) return;
    char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",folder,name);
    FILE *f=fopen(path,"wb");if(!f) return;
    fprintf(f,"P6\n%d %d\n255\n",CW,CH);
    unsigned char row[CW*3];
    for(int y=0;y<CH;y++) {
        for(int x=0;x<CW;x++) { uint32_t p=canvas[y*CW+x];row[3*x]=p;row[3*x+1]=p>>8;row[3*x+2]=p>>16; }
        fwrite(row,1,sizeof row,f);
    }
    fclose(f);
}
/* The installed EE/Russian font uses 44x64 cells. Keep the embedded licensed
 * font for missing files/glyphs; no extracted game font enters a package. */
static unsigned char *local_file(const char *path, size_t limit, size_t *length) {
    FILE *f=fopen(path,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)) { fclose(f);return NULL; }
    long n=ftell(f);
    if(n<=0 || (size_t)n>limit || fseek(f,0,SEEK_SET)) { fclose(f);return NULL; }
    unsigned char *data=malloc(n);
    if(!data || fread(data,1,n,f)!=(size_t)n) { free(data);data=NULL; }
    fclose(f);*length=data?(size_t)n:0;return data;
}
static void font_init(void) {
    int h;
    memcpy(font_advance,launcher_font_advance,sizeof font_advance);
    font_px=decode(font_png,font_png_end,&font_w,&h);
    const char *paths[]={"translations/russian/font/font000.png","sh2e/font/font000.png"};
    for(int pack=0;pack<2;pack++) {
        size_t n=0;unsigned char *bytes=local_file(paths[pack],32u<<20,&n);
        if(!bytes)continue;
        /* Reject unsupported grids before asking the PNG decoder to allocate. */
        if(n<24 || memcmp(bytes,"\x89PNG\r\n\x1a\n",8)) { free(bytes);continue; }
        unsigned w=0,ph=0;
        for(int k=0;k<4;k++) { w=(w<<8)|bytes[16+k];ph=(ph<<8)|bytes[20+k]; }
        if(!w || !ph || w>4400 || ph>9600 || w%44 || ph%128) { free(bytes);continue; }
        int sw,sh;uint32_t *sheet=decode(bytes,bytes+n,&sw,&sh);free(bytes);
        if(!sheet || !font_px) { free(sheet);continue; }
        size_t wn=0;unsigned char *widths=local_file("translations/russian/font/fontwdata.bin",448,&wn);
        if(pack!=0 || wn!=448) { free(widths);widths=NULL; }
        for(unsigned code=33;code<0x500;code++) {
            int id=-1;
            if(code<127)id=code-32;
            if(!pack && code>=0x410 && code<=0x44f)id=0xa0+code-0x410;
            if(!pack && code==0x401)id=0x88;
            if(!pack && code==0x451)id=0x98;
            if(id<0 || id>=(sw/44)*(sh/128))continue;
            int dest=font_index(code),sx=(id%(sw/44))*44,sy=(id/(sw/44))*64;
            int gx=dest%16*72,gy=dest/16*96,right=0;
            for(int y=0;y<96;y++)for(int x=0;x<72;x++) {
                uint32_t p=x<66?sheet[(sy+y*2/3)*sw+sx+x*2/3]:0;
                font_px[(gy+y)*font_w+gx+x]=p;
                if((p>>24)>32 && x>right)right=x;
            }
            int advance=widths && id<224?widths[id]*3:right+3;
            font_advance[dest]=advance>0 && advance<73?advance:66;
        }
        /* Space is handled separately by the game's text routine: its table
         * entry is zero, so the standalone launcher needs an explicit advance. */
        font_advance[0]=27;
        free(widths);free(sheet);
        rt_log("launcher: installed menu font %s",paths[pack]);break;
    }
}
static unsigned read16(const unsigned char *b) { return b[0]|(unsigned)b[1]<<8; }
static uint32_t color565(unsigned v) { return rgb(((v>>11)*255+15)/31,(((v>>5)&63)*255+31)/63,((v&31)*255+15)/31); }
/* Only the verified one-image DXT3 start00 TEX layout is accepted. */
static uint32_t *title_image(const char *path,int *w,int *h) {
    size_t n=0;unsigned char *b=local_file(path,32u<<20,&n);
    if(!b)return NULL;
    if(n<80 || memcmp(b,"\x01\x09\x99\x19",4) || read16(b+60)!=0x102) { free(b);return NULL; }
    *w=read16(b+20);*h=read16(b+22);
    size_t blocks=(size_t)((*w+3)/4)*((*h+3)/4);
    if(!*w || !*h || *w>4096 || *h>4096 || blocks>(n-80)/16) { free(b);return NULL; }
    uint32_t *px=malloc((size_t)*w**h*4);
    if(!px) { free(b);return NULL; }
    const unsigned char *p=b+80;
    for(int by=0;by<*h;by+=4)for(int bx=0;bx<*w;bx+=4,p+=16) {
        uint32_t colors[4]={color565(read16(p+8)),color565(read16(p+10)),0,0};
        for(int j=2;j<4;j++) {
            colors[j]=0xff000000u;
            for(int s=0;s<24;s+=8)colors[j]|=((((colors[0]>>s)&255)*(4-j)+((colors[1]>>s)&255)*(j-1))/3)<<s;
        }
        for(int y=0;y<4 && by+y<*h;y++)for(int x=0;x<4 && bx+x<*w;x++) {
            int k=y*4+x,alpha=((p[k/2]>>((k&1)*4))&15)*17;
            px[(by+y)**w+bx+x]=(colors[(p[12+k/4]>>((k%4)*2))&3]&0xffffffu)|((uint32_t)alpha<<24);
        }
    }
    free(b);return px;
}
static void theme_init(void) {
    int w,h;uint32_t *art=title_image("sh2e/pic/etc/start00.tex",&w,&h);
    if(!art)art=title_image("data/pic/etc/start00.tex",&w,&h);
    for(int i=0;i<CW*CH;i++)canvas[i]=C_BG;
    title_loaded=art!=NULL;
    if(art) {
        /* Cover the canvas without changing the artwork's proportions. */
        int crop_h=(int)((int64_t)w*CH/CW);
        if(crop_h<=h)image(art+(size_t)((h-crop_h)/3)*w,w,crop_h,0,0,CW,CH);
        else {
            int dw=(int)((int64_t)w*CH/h);
            image(art,w,h,(CW-dw)/2,0,dw,CH);
        }
        free(art);rt_log("launcher: installed title artwork loaded");
    }
    memcpy(backdrop,canvas,sizeof backdrop);
}
static void draw_background(int settings) {
    memcpy(canvas,backdrop,sizeof canvas);
    if(settings) shade_rect(0,0,CW,CH,231);
}
static const char *option_label(const Opt *o) {
    if(!ui_ru)return o->label;
    static const char *keys[]={"resolution","movie","PreserveSoundsOnLoad","cores","movie_scale","mouse_mode","fullscreen","fps","text_language"};
    static const char *names[]={"Разрешение","Видеоролики","Звуки загрузки и сохранения","Потоки игры","Масштаб роликов","Управление мышью","Режим экрана","Частота кадров","Язык текста"};
    for(unsigned i=0;i<sizeof keys/sizeof *keys;i++)if(!strcmp(o->key,keys[i]))return names[i];
    return o->label;
}
static const char *option_value(const Opt *o) {
    if(!ui_ru || o->cur<0)return opt_name(o);
    const char *const *names=NULL;
    static const char *movie[]={"Оригинальные","Улучшенные (1080p60)"};
    static const char *sounds[]={"Включены","Выключены"};
    static const char *cores[]={"Три потока","Один поток"};
    static const char *scaling[]={"Заполнить экран","Целиком, с полями","Оригинальный размер"};
    static const char *mouse[]={"Клавиатура / геймпад","Мышь и клавиатура (F8)"};
    static const char *window[]={"В окне","Полный экран"};
    static const char *fps[]={"30 (оригинал)","60 (экспериментально)"};
    static const char *language[]={"Настройки игры","Русский","Английский","Французский","Немецкий","Итальянский","Испанский","Японский"};
    if(!strcmp(o->key,"resolution") && o->cur==3)return "По пропорциям дисплея";
    if(!strcmp(o->key,"movie"))names=movie;
    if(!strcmp(o->key,"PreserveSoundsOnLoad"))names=sounds;
    if(!strcmp(o->key,"cores"))names=cores;
    if(!strcmp(o->key,"movie_scale"))names=scaling;
    if(!strcmp(o->key,"mouse_mode"))names=mouse;
    if(!strcmp(o->key,"fullscreen"))names=window;
    if(!strcmp(o->key,"fps"))names=fps;
    if(!strcmp(o->key,"text_language"))names=language;
    return names?names[o->cur]:opt_name(o);
}
static const char *option_hint(const Opt *o) {
    if(!strcmp(o->key,"text_language"))return tr("Applies when you start the game. Voices stay original.","Применяется при запуске игры. Озвучка остаётся оригинальной.");
    if(!strcmp(o->key,"fps"))return tr("60 FPS is experimental. Some later scenes still need testing.","Режим 60 кадров экспериментальный. Поздние сцены ещё проверяются.");
    if(!strcmp(o->key,"mouse_mode"))return tr("F8: on/off. Move: turn. LMB: action. Hold middle: look.","F8: вкл/выкл. Движение: поворот. ЛКМ: действие. Зажать колесо: обзор.");
    if(!strcmp(o->key,"movie_scale"))return tr("Fill crops the edges. Fit keeps the entire frame.","Заполнение обрезает края. Режим целиком сохраняет весь кадр.");
    return tr("Left / Right: change. Back saves your settings.","Влево / вправо: изменить. При выходе настройки сохраняются.");
}

static void draw_menu(int sel) {
    static const char *en[]={"Play","Settings","Credits","Quit"};
    static const char *ru[]={"Играть","Настройки","Авторы","Выход"};
    draw_background(0);
    if(!title_loaded)text_c(290,80,C_SEL,"SILENT HILL 2");
    for(int i=0;i<MENU_COUNT;i++) {
        int y=menu_top()+i*44+32;
        text_c(y,42,i==sel?C_SEL:C_DIM,ui_ru?ru[i]:en[i]);
    }
    text_c(657,28,C_TEXT,"brkovv   /   tg@brkovv");
    text_c(693,26,C_DIM,tr("Enter / click: select    Arrows: move","Enter / ЛКМ: выбрать    Стрелки: перемещение"));
    capture_canvas("menu");
}

static void draw_options(int sel) {
    draw_background(1);
    text(118,112,49,C_SEL,tr("Settings","Настройки"));
    text(CW-306,111,22,C_DIM,"brkovv / macOS");
    for (int i = 0; i <= N_OPTS; i++) {
        int top = option_top() + i * option_step(), y=top+25;
        uint32_t c = i == sel ? C_SEL : C_TEXT;
        if (i == N_OPTS) { text(128,y,31,c,tr("Back","Назад")); break; }
        text(128, y, 33, c, option_label(&opts[i]));
        char v[64];
        snprintf(v, sizeof v, i == sel ? "< %s >" : "%s", option_value(&opts[i]));
        text(CW - 128 - text_w(32, v), y, 32, c, v);
    }
    if(sel<N_OPTS)text(128,637,28,C_DIM,option_hint(&opts[sel]));
    text(128,681,28,C_TEXT,tr("Arrows: change   Enter: select   Esc / right click: back","Стрелки: изменить   Enter: выбрать   Esc / ПКМ: назад"));
    capture_canvas("settings");
}

static void draw_credits(int introductory) {
    draw_background(1);
    text_c(135,52,C_SEL,"SILENT HILL 2");
    text_c(180,27,C_DIM,tr("An unofficial native macOS adaptation","Неофициальный перенос для macOS"));
    text_c(264,29,C_TEXT,tr("macOS adaptation created by","Создатель macOS-адаптации"));
    text_c(323,68,C_SEL,"brkovv");
    text_c(366,31,C_TEXT,"tg@brkovv");
    text_c(436,27,C_DIM,tr("Built upon","Основано на"));
    text_c(475,30,C_TEXT,"cutarev/sh2-nx   /   xboxrecomp");
    text_c(514,27,C_TEXT,"Silent Hill 2: Enhanced Edition");
    text_c(574,25,C_DIM,tr("Original game: KONAMI / Team Silent","Оригинальная игра: KONAMI / Team Silent"));
    text_c(610,22,C_DIM,"github.com/cutarev/sh2-nx");
    text_c(680,24,C_TEXT,introductory?tr("Enter / click: continue","Enter / ЛКМ: продолжить"):tr("Enter / Esc: back","Enter / Esc: назад"));
    capture_canvas("credits");
}

/* Fades the canvas in from black, or out to black, then leaves it as it was. Input meanwhile is dropped. */
static void fade(int in) {
    input_transition=1;
    static uint32_t pic[CW * CH];
    memcpy(pic, canvas, sizeof pic);
    for (Uint64 t0 = SDL_GetTicks64(), t = 0; t < FADE_MS;) {
        poll_input();
        t = SDL_GetTicks64() - t0;
        uint32_t k = t >= FADE_MS ? 256 : (uint32_t)(t * 256 / FADE_MS);
        if (!in) k = 256 - k;
        for (int i = 0; i < CW * CH; i++)
            canvas[i] = 0xFF000000u | ((pic[i] & 0xFF00FFu) * k >> 8 & 0xFF00FFu) | ((pic[i] & 0xFF00u) * k >> 8 & 0xFF00u);
        present(canvas, CW, CH);
    }
    memcpy(canvas, pic, sizeof pic);
    input_transition=0;
}

static void options(void) {
    rt_log("launcher: settings opened");
    options_open = 1; mouse_select = -1;
    int sel = 0;
    draw_options(sel);
    fade(1);
    for (int dirty = 0;; ) {
        if (dirty) { draw_options(sel); dirty = 0; }
        present(canvas, CW, CH);
        int in = poll_input();
        if (mouse_select >= 0) { if (sel != mouse_select) dirty = 1; sel = mouse_select; mouse_select = -1; }
        if (in == IN_NONE) continue;
        dirty = 1;
        Opt *o = sel < N_OPTS ? &opts[sel] : NULL;
        if (in != IN_BACK) sound(!o && in == IN_OK ? SND_OK : SND_CURSOR);
        if (in == IN_UP) sel = (sel + N_OPTS) % (N_OPTS + 1);
        if (in == IN_DOWN) sel = (sel + 1) % (N_OPTS + 1);
        if (o && (in == IN_LEFT || in == IN_RIGHT || in == IN_OK)) {
            int n = n_vals(o);
            o->cur = o->cur < 0 ? 0 : (o->cur + (in == IN_LEFT ? n - 1 : 1)) % n;
            if(!strcmp(o->key,"text_language"))ui_ru=!strcmp(o->vals[o->cur],"6");
        }
        if (in == IN_BACK || (!o && in == IN_OK)) {
            ini_save();
            options_open = 0;
            fade(0);
            return;
        }
    }
}

static void show_credits(int introductory) {
    rt_log("launcher: %s credits",introductory?"intro":"menu");
    credits_open=1;mouse_select=-1;
    draw_credits(introductory);
    fade(1);
    Uint64 t0 = SDL_GetTicks64();
    for(;;) {
        int in=poll_input();Uint64 elapsed=SDL_GetTicks64()-t0;
        if((!introductory || elapsed>=1200) && (in==IN_OK || in==IN_BACK || (introductory && elapsed>=6500)))break;
        present(canvas, CW, CH);
    }
    sound(SND_OK);credits_open=0;
    fade(0);
}

void ee_init(void);  /* src/game/ee.c */

void launcher_run(void) {
#ifndef __SWITCH__
    /* Scripted runs (SH2_KEYS) go straight to the game; SH2_LAUNCHER=1 keeps the launcher for testing it. */
    if ((getenv("SH2_KEYS") || getenv("SH2_SKIP_LAUNCHER")) && !getenv("SH2_LAUNCHER")) { ee_init(); return; }
#endif
    rt_window_open();
    gl_context_open();
    font_init();
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glClearColor(0, 0, 0, 1);
    theme_init();
    sound_init();
    ini_load();
    show_credits(1);
    draw_menu(0);
    rt_log("launcher: main menu");
    fade(1);
    for (int sel = 0, dirty = 0;; ) {
        if (dirty) { draw_menu(sel); dirty = 0; }
        present(canvas, CW, CH);
        int in = poll_input();
        if (mouse_select >= 0) { if (sel != mouse_select) dirty = 1; sel = mouse_select; mouse_select = -1; }
        if(in==IN_UP)sel=(sel+MENU_COUNT-1)%MENU_COUNT,dirty=1,sound(SND_CURSOR);
        if(in==IN_DOWN)sel=(sel+1)%MENU_COUNT,dirty=1,sound(SND_CURSOR);
        if(in==IN_BACK)sel=MENU_QUIT,dirty=1;
        if (in == IN_OK) sound(SND_OK);
        if (in == IN_OK && sel == MENU_PLAY) { rt_log("launcher: play selected");fade(0); break; }
        if(in==IN_OK && sel==MENU_QUIT) { rt_log("launcher: quit selected");exit(0); }
        if(in==IN_OK && sel==MENU_CREDITS) { fade(0);show_credits(0);draw_menu(sel);fade(1); }
        if (in == IN_OK && sel == MENU_OPTIONS) {
            fade(0);
            options();
            draw_menu(sel);
            fade(1);
        }
    }
    /* leave GL as the game expects a new context: framebuffer 0, nothing bound */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    free(font_px);
    sound_close();
    ee_init();
}
