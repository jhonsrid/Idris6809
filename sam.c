#include "sam.h"
#include "memory.h"

/*
 * SAM register bit layout ($FFC0-$FFDF):
 *
 * Each bit is controlled by a pair of addresses:
 *   Even address = clear bit, Odd address = set bit.
 *   Written data is ignored — only the address matters.
 *
 * Addr pair  Bit#  Field
 * FFC0/FFC1   0    V0
 * FFC2/FFC3   1    V1
 * FFC4/FFC5   2    V2
 * FFC6/FFC7   3    F0
 * FFC8/FFC9   4    F1
 * FFCA/FFCB   5    F2
 * FFCC/FFCD   6    F3
 * FFCE/FFCF   7    F4
 * FFD0/FFD1   8    F5
 * FFD2/FFD3   9    F6
 * FFD4/FFD5  10    R0
 * FFD6/FFD7  11    R1
 * FFD8/FFD9  12    P1
 * FFDA/FFDB  13    (unused)
 * FFDC/FFDD  14    (unused)
 * FFDE/FFDF  15    TY
 */

void sam_init(SAM *sam)
{
    sam->v  = 0;  /* V2:V1:V0 = 000 */
    sam->f  = 0;  /* F6..F0 = 0000000 */
    sam->r  = 0;  /* R1:R0 = 00 (normal speed) */
    sam->p  = 0;  /* P1 = 0 */
    sam->ty = 0;  /* TY = 0 (ROM mode) */

    /* Ensure memory subsystem matches */
    mem_set_rom_mode(true);
}

void sam_write(SAM *sam, uint16_t addr)
{
    if (addr < 0xFFC0 || addr > 0xFFDF)
        return;

    /* Bit number = (addr - $FFC0) / 2 */
    uint8_t bit_num = (uint8_t)((addr - 0xFFC0) >> 1);
    /* Set if odd address, clear if even */
    uint8_t set = (uint8_t)(addr & 1);

    switch (bit_num) {
    /* V2:V1:V0 — bits 0-2 */
    case 0:  /* V0 */
        if (set) sam->v |= 0x01; else sam->v &= ~0x01;
        break;
    case 1:  /* V1 */
        if (set) sam->v |= 0x02; else sam->v &= ~0x02;
        break;
    case 2:  /* V2 */
        if (set) sam->v |= 0x04; else sam->v &= ~0x04;
        break;

    /* F6:F5:F4:F3:F2:F1:F0 — bits 3-9 */
    case 3:  /* F0 */
        if (set) sam->f |= 0x01; else sam->f &= ~0x01;
        break;
    case 4:  /* F1 */
        if (set) sam->f |= 0x02; else sam->f &= ~0x02;
        break;
    case 5:  /* F2 */
        if (set) sam->f |= 0x04; else sam->f &= ~0x04;
        break;
    case 6:  /* F3 */
        if (set) sam->f |= 0x08; else sam->f &= ~0x08;
        break;
    case 7:  /* F4 */
        if (set) sam->f |= 0x10; else sam->f &= ~0x10;
        break;
    case 8:  /* F5 */
        if (set) sam->f |= 0x20; else sam->f &= ~0x20;
        break;
    case 9:  /* F6 */
        if (set) sam->f |= 0x40; else sam->f &= ~0x40;
        break;

    /* R1:R0 — bits 10-11 */
    case 10: /* R0 */
        if (set) sam->r |= 0x01; else sam->r &= ~0x01;
        break;
    case 11: /* R1 */
        if (set) sam->r |= 0x02; else sam->r &= ~0x02;
        break;

    /* P1 — bit 12 */
    case 12: /* P1 */
        if (set) sam->p = 1; else sam->p = 0;
        break;

    /* bits 13-14: unused */
    case 13:
    case 14:
        break;

    /* TY — bit 15 */
    case 15: /* TY */
        if (set) sam->ty = 1; else sam->ty = 0;
        /* Side effect: update memory map */
        mem_set_rom_mode(sam->ty == 0);
        break;
    }
}

uint16_t sam_get_display_addr(const SAM *sam)
{
    /*
     * The display start address is formed from F and V bits:
     *   F6:F5:F4:F3:F2:F1:F0 form bits 15-9
     *   In text/semigraphics mode, V bits select the display offset mode
     *   but the base address is determined by F alone.
     *
     * Default: F=0000010 (bit F1 set) = $0400 for text mode.
     * Actually the Dragon sets F1 via software to point at $0400.
     *
     * Address = (F << 9)
     * The V bits affect the VDG row addressing (how many bytes per row)
     * but for display start, F is what matters.
     */
    return (uint16_t)(sam->f) << 9;
}

uint8_t sam_get_vdg_mode(const SAM *sam)
{
    return sam->v;
}

uint8_t sam_get_cpu_rate(const SAM *sam)
{
    return sam->r;
}

bool sam_get_ty(const SAM *sam)
{
    return sam->ty != 0;
}
