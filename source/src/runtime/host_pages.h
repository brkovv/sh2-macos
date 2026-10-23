#ifndef SH2_HOST_PAGES_H
#define SH2_HOST_PAGES_H
#include <stdint.h>

/* Guest addresses remain 32-bit even when one host page covers several guest pages.
 * Use 64-bit ends so a commit of the last guest page does not wrap to zero. */
static inline int host_page_range(uint32_t va, uint32_t len, uint64_t page,
                                  uint64_t *lo, uint64_t *size) {
    const uint64_t arena = UINT64_C(1) << 32;
    uint64_t end = (uint64_t)va + len;
    if (!page || (page & (page - 1)) || page > arena || end > arena) return 0;
    *lo = (uint64_t)va & ~(page - 1);
    *size = len ? ((end + page - 1) & ~(page - 1)) - *lo : 0;
    return 1;
}
#endif
