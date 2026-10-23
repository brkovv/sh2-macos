/* Optional private text pack. The engine keeps its six original language IDs;
 * Russian uses German message names only inside its own overlay. No EXE edits. */
#include "../runtime/rt.h"
#include <unistd.h>
#include <sys/stat.h>

static int russian;
int ee_russian_text(void) { return russian; }

static int language_setting(int lang) {
    /* This file contains the game's language setting; preserve other lines. */
    FILE *f = fopen("language.ini", "rb");
    char input[4096] = {0}, output[4160];
    size_t n = f ? fread(input, 1, sizeof input - 1, f) : 0;
    if (f) { int more = fgetc(f); fclose(f); if (more != EOF) return 0; }
    size_t used = 0;
    for (char *line = input; line < input + n;) {
        char *end = memchr(line, '\n', input + n - line);
        size_t len = end ? (size_t)(end - line) + 1 : (size_t)(input + n - line);
        int value;
        if (sscanf(line, " SET DX_CONFIG_LANGUAGE %d", &value) != 1) {
            memcpy(output + used, line, len); used += len;
        }
        line += len;
    }
    if (used && output[used - 1] != '\n') output[used++] = '\n';
    used += snprintf(output + used, sizeof output - used, "SET DX_CONFIG_LANGUAGE %d\n", lang);
    if (used == n && !memcmp(output, input, n)) return 1;
    struct stat st;
    if (!lstat("language.ini", &st) && !S_ISREG(st.st_mode)) return 0;
    char temporary[] = ".sh2-language-XXXXXX";
    int fd = mkstemp(temporary);
    if (fd < 0) return 0;
    f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(temporary); return 0; }
    int ok = fwrite(output, 1, used, f) == used;
    if (fclose(f)) ok = 0;
    if (ok) ok = rename(temporary, "language.ini") == 0;
    if (!ok) unlink(temporary);
    return ok;
}

void ee_language_init(void) {
#if defined(SH2_STOCK) || defined(__SWITCH__)
    return;
#else
    int selected = rt_option_int("text_language", -1);
    russian = selected == 6;
    if (russian) {
        FILE *f = fopen("translations/russian/pause.bin", "rb");
        char data[2304];
        size_t n = f ? fread(data, 1, sizeof data, f) : 0;
        if (f) fclose(f);
        /* Installer emits nine NUL-terminated CP1251 strings from the UTF-8 pack.
         * ASCII drawing maps CP1251 C0..FF to glyph A0..DF, matching the font. */
        size_t at = 0, offsets[9];
        int valid = n && n < sizeof data;
        for (int i = 0; valid && i < 9; i++) {
            offsets[i] = at;
            char *end = memchr(data + at, 0, n - at);
            valid = end && end != data + at;
            if (valid) at = (size_t)(end - data) + 1;
        }
        valid = valid && at == n;
        if (!valid) {
            russian = 0;
            rt_log("language: Russian pack missing or invalid; keeping the game's language");
            return;
        }
        /* Original v10-ee German pause table, verified against the supported PE. */
        static const uint32_t expected[9] = {0x62EFB0,0x62EFA0,0x62EF8C,0x62EF78,0x62EF68,0x62EF5C,0x62EF40,0x62EF34,0x62EF28};
        for (int i = 0; i < 9; i++)
            if (MEM32(0x7ABDCCu + 4*i) != expected[i]) rt_fatal("language: unexpected pause table");
        for (int i = 0; i < 9; i++) MEM32(0x7ABDCCu + 4*i) = gstrdup(data + offsets[i]);
        selected = 3;
        rt_log("language: Russian text, Metallist / Hellbringer / ilyuXer; original voices");
    }
    if (selected >= 0 && selected <= 5 && !language_setting(selected))
        rt_fatal("language: cannot save language.ini");
#endif
}
