#ifndef SAM_H
#define SAM_H

#include <stdint.h>
#include <stdbool.h>

/*
 * MC6883 / SN74LS783 Synchronous Address Multiplexer (SAM)
 *
 * Register bits (set/clear by writing to address pairs $FFC0-$FFDF):
 *   V2:V1:V0     — VDG display mode / offset select
 *   F6:F5:F4:F3:F2:F1:F0 — Display page address (bits 15-9)
 *   R1:R0        — CPU rate select
 *   P1           — Memory size (always 1 = 64K for Dragon 64)
 *   TY           — Map type (0 = ROM, 1 = all-RAM)
 */

typedef struct {
    uint8_t  v;       /* V2:V1:V0  (3 bits) — VDG mode/offset */
    uint8_t  f;       /* F6..F0    (7 bits) — display page */
    uint8_t  r;       /* R1:R0     (2 bits) — CPU rate */
    uint8_t  p;       /* P1        (1 bit)  — memory size */
    uint8_t  ty;      /* TY        (1 bit)  — map type */
} SAM;

/* Initialize SAM to boot defaults (all bits clear) */
void sam_init(SAM *sam);

/* Handle a write to $FFC0-$FFDF. Only the address matters; data is ignored.
 * This updates the appropriate SAM bit and side effects (e.g., ROM/RAM mode). */
void sam_write(SAM *sam, uint16_t addr);

/* Get the VDG display start address from F and V registers */
uint16_t sam_get_display_addr(const SAM *sam);

/* Get the VDG mode offset (V2:V1:V0) */
uint8_t sam_get_vdg_mode(const SAM *sam);

/* Get CPU rate (R1:R0): 0=normal, 1=address-dependent, 2/3=high speed */
uint8_t sam_get_cpu_rate(const SAM *sam);

/* Get map type: true = all-RAM mode */
bool sam_get_ty(const SAM *sam);

#endif
