/* Altered native adaptation of Silent Hill 2: Enhanced Edition timing fixes.
 * Reference commit 30a86563d88b58c0b6de083e3b68967feedc94e9.
 * Original authors: mercury501, Murugo, Aero_, Polymega (2024), Murugo (2022).
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for damages arising from its use.
 * Permission is granted to anyone to use this software for any purpose, including
 * commercial applications, and to alter it and redistribute it freely, subject
 * to these restrictions: (1) The origin must not be misrepresented; you must not
 * claim you wrote the original software. (2) Altered versions must be plainly
 * marked, and must not be misrepresented as original. (3) This notice may not be
 * removed or altered from a source distribution.
 */
#if !defined(__SWITCH__) && !defined(SH2_STOCK)
#include "../runtime/rt.h"
#include "desktop.h"

void ee60_init(void) {
    if (ee_desktop_fps!=60) return;
    MEMF(0x006BC194u)=.0166665f;       /* water animation */
    MEMF(0x007F3E10u)=.0166665f;       /* low-health oscillation */
    MEMF(0x0070AE40u)=.00027777222565f;/* staircase light */
    MEMF(0x0089C1A4u)=20.f;           /* lowering water steps */
    MEMF(0x006BC1F4u)=5.333333492f;   /* rising water */
    MEMF(0x006C10A8u)=.093333332985f;  /* bug-room flashlight */
    rt_log("ee: 60 Hz timing adapters active (effects, timers, damage, footsteps, map)");
}
static int map_wait(unsigned frames) {
    uint32_t elapsed=MEM32(0x0094D8D4u)+MEM32(0x00A3336Cu);
    MEM32(0x0094D8D4u)=elapsed;return elapsed>=frames;
}
/* Return 0 to resume the original state switch, 1 to return, 2 to finish state4. */
int ee60_map_marker(void) {
    unsigned state=MEM32(0x0094D8DCu);
    if (!state) return map_wait(2) ? 0 : 1;
    if (state==1) {
        if (map_wait(34)) {MEM32(0x94D920)=1;MEM32(0x94D8DC)=2;MEM32(0x94D8D4)=0;}
        return 1;
    }
    if (state==2) {
        if (map_wait(2) && MEM32(0x94D920)==2) {MEM32(0x94D8DC)=3;MEM32(0x94D8D4)=0;}
        return 1;
    }
    if (state==3) {
        uint32_t alpha=g_ecx+MEM32(0x94D91C)*8+4;
        unsigned value=MEM8(alpha)+MEM32(0xA3336C)*4;
        MEM8(alpha)=value>=128 ? 128 : value;
        if (value>=128) MEM32(0x94D8DC)=4;
        return 1;
    }
    if (state==4) return map_wait(50) ? 2 : 1;
    return 0;
}
int ee60_spray_active(void) {
    static uint32_t previous_frame;static int active;
    uint32_t table=MEM32(0x00570B88u),counter=MEM32(0x00449331u);
    uint32_t head=MEM32(table+5*4);
    if (head==MEM32(head+4)) {active=1;return 1;}
    uint32_t frame=MEM32(counter);
    if (frame!=previous_frame) {previous_frame=frame;active^=1;}
    return active;
}
#endif
