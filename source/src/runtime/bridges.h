/* Export records must be contiguous in both ELF and Mach-O, including dead-strip builds. */
#pragma once

typedef struct Bridge { const char *name; void (*fn)(void); } Bridge;
#ifdef __APPLE__
#define SH2_BRIDGE_SECTION "__DATA,sh2_bridges"
extern const Bridge sh2_bridges_begin[] __asm("section$start$__DATA$sh2_bridges");
extern const Bridge sh2_bridges_end[] __asm("section$end$__DATA$sh2_bridges");
#else
#define SH2_BRIDGE_SECTION "sh2_bridges"
extern const Bridge sh2_bridges_begin[] __asm("__start_sh2_bridges");
extern const Bridge sh2_bridges_end[] __asm("__stop_sh2_bridges");
#endif
#define BRIDGE_REG(cname, ename) \
    static const Bridge cname##_reg __attribute__((used, section(SH2_BRIDGE_SECTION), aligned(8))) = { ename, cname }
