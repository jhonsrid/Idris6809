#ifndef VDG_H
#define VDG_H

#include <stdint.h>
#include <stdbool.h>

/*
 * MC6847 Video Display Generator
 *
 * Native output: 256x192 active pixels (PAL: 312 scanlines total).
 * Modes controlled by external pins from PIA1 port B:
 *   A/G   (bit 7) — 0=alphanumeric, 1=graphics
 *   GM2:1:0 (bits 6-4) — graphics mode select
 *   CSS   (bit 3) — color set select
 *
 * In alpha mode, each byte in video RAM:
 *   Bit 7: 1=semigraphics, 0=internal character
 *   Bit 6: inverse video (alpha) / not used (SG4)
 *   Bits 5-0: character code (alpha) or SG4 pattern
 *
 * Timing (PAL):
 *   312 scanlines/frame, 57 CPU cycles/scanline
 *   Active: 192 scanlines (lines 0-191)
 *   VBlank: 120 scanlines
 *   FS (frame sync) asserted at start of vblank
 */

/* Framebuffer dimensions (native VDG output) */
#define VDG_WIDTH   256
#define VDG_HEIGHT  192

/* PAL timing */
#define VDG_SCANLINES_PER_FRAME  312
#define VDG_CYCLES_PER_SCANLINE  57
#define VDG_ACTIVE_LINES         192
#define VDG_VBLANK_START         192  /* First vblank line (active lines 0-191) */
#define VDG_TOP_BORDER           37   /* PAL: (312 - 192) / 2 - some overscan ≈ 37 */

/* Graphics mode values (GM2:GM1:GM0 from PIA1 port B bits 6:5:4).
 * When A/G=0, the GM bits are ignored (alpha/semigraphics mode).
 * When A/G=1, GM selects the graphics mode:
 *   0 = CG1  (64x64, 4-color)
 *   1 = RG1  (128x64, 2-color)
 *   2 = CG2  (128x64, 4-color)
 *   3 = RG2  (128x96, 2-color)
 *   4 = CG3  (128x96, 4-color)
 *   5 = RG3  (128x192, 2-color)
 *   6 = CG6  (128x192, 4-color)
 *   7 = RG6  (256x192, 2-color)
 */

typedef struct {
    /* Framebuffer: 256x192 pixels, each pixel is an RGB value packed as
     * 0x00RRGGBB. Rendering fills this; the frontend blits it to screen. */
    uint32_t framebuffer[VDG_HEIGHT][VDG_WIDTH];

    /* Mode pins (set by PIA1 port B) */
    bool     ag;        /* A/G: 0=alpha, 1=graphics */
    uint8_t  gm;        /* GM2:GM1:GM0 (3 bits) */
    bool     css;       /* Color set select */

    /* Timing state */
    int      scanline;  /* Current scanline (0-311) */
    bool     fs;        /* Frame sync (true during vblank) */

    /* Pointer to RAM (set during init) */
    const uint8_t *ram;
} VDG;

/* Initialize VDG state */
void vdg_init(VDG *vdg, const uint8_t *ram);

/* Set mode pins from PIA1 port B value */
void vdg_set_mode(VDG *vdg, uint8_t pia1b);

/* Render a single active scanline (0-191) reading from video RAM.
 * display_addr: SAM-provided display start address.
 * Returns number of bytes of video RAM consumed by this scanline. */
void vdg_render_scanline(VDG *vdg, int line, uint16_t display_addr);

/* Render a complete frame into the framebuffer.
 * Convenience function that calls vdg_render_scanline for all 192 lines. */
void vdg_render_frame(VDG *vdg, uint16_t display_addr);

/* Advance one scanline in timing. Returns true if FS just became active
 * (i.e., we just entered vblank — signals PIA0 CB1 interrupt). */
bool vdg_tick_scanline(VDG *vdg);

/* Get the internal MC6847 character ROM glyph data.
 * Returns pointer to 64 characters × 12 rows of 8-pixel-wide bitmaps. */
const uint8_t *vdg_get_font(void);

#endif
