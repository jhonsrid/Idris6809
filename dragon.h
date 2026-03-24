#ifndef DRAGON_H
#define DRAGON_H

#include "cpu6809.h"
#include "memory.h"
#include "sam.h"
#include "vdg.h"
#include "pia.h"
#include "acia.h"
#include <stdbool.h>

typedef enum {
    DRAGON_64,
    DRAGON_32
} DragonModel;

/*
 * Dragon machine state — ties all components together.
 *
 * Timing (PAL):
 *   Master crystal: 14.218 MHz
 *   CPU clock:      14,218,000 / 16 = 888,625 Hz (~0.89 MHz)
 *   Scanlines:      312 per frame (PAL)
 *   Cycles/line:    57 (912 master clocks / 16)
 *   Cycles/frame:   17,784
 *   Frame rate:     888,625 / 17,784 ≈ 49.97 Hz
 *   Frame period:   ~20.01 ms
 *
 * Note: CPU cycles per scanline = 57 E-clock cycles, NOT 228.
 * 228 is the SAM memory address count per line (at master/4).
 * The CPU E-clock runs at master/16, so 228/4 = 57.
 */

#define DRAGON_MASTER_HZ            14218000
#define DRAGON_CYCLES_PER_SCANLINE  57
#define DRAGON_SCANLINES_PER_FRAME  312
#define DRAGON_CYCLES_PER_FRAME     (DRAGON_CYCLES_PER_SCANLINE * DRAGON_SCANLINES_PER_FRAME)
#define DRAGON_CPU_HZ               (DRAGON_MASTER_HZ / 16)  /* 888,625 Hz */
#define DRAGON_FPS                  50

typedef struct {
    CPU6809  cpu;
    SAM      sam;
    VDG      vdg;
    PIA      pia0;      /* $FF00-$FF03: keyboard, joystick, HSYNC, FSYNC */
    PIA      pia1;      /* $FF20-$FF23: DAC, sound, cassette, VDG mode */
    ACIA     acia;      /* $FF04-$FF07: serial port (Dragon 64 only) */

    /* Keyboard matrix: 8 rows × 8 columns.
     * Each byte is a row; bit=0 means key pressed (active low). */
    uint8_t  keyboard[8];

    /* Cycle overshoot carried from previous scanline */
    int      cycle_debt;

    /* Frame counter */
    int      frame_count;

    /* Machine model */
    DragonModel model;

    /* Running state */
    bool     running;
} Dragon;

/* Initialize the machine for the given model. Does NOT load ROMs. */
void dragon_init(Dragon *d, DragonModel model);

/* Load ROMs. Returns 0 on success.
 * Two-ROM mode: rom1 at $8000-$BFFF, rom2 at $C000-$FFFF (Dragon 64).
 * Single-ROM mode: pass rom2 as NULL. 16KB ROM loaded at $8000-$BFFF
 * and mirrored to $C000-$FFFF (Dragon 32). 32KB ROM fills both. */
int dragon_load_roms(Dragon *d, const char *rom1_path, const char *rom2_path);

/* Reset the machine (as if pressing the reset button). */
void dragon_reset(Dragon *d);

/* Run one complete frame (262 scanlines).
 * Executes CPU, updates VDG, fires interrupts.
 * Returns total CPU cycles executed this frame. */
int dragon_run_frame(Dragon *d);

/* Run a single scanline (228 CPU cycles).
 * Returns total CPU cycles executed this scanline. */
int dragon_run_scanline(Dragon *d);

/* Press/release a key in the keyboard matrix.
 * row: 0-7, col: 0-7, pressed: true=down, false=up */
void dragon_key_press(Dragon *d, int row, int col, bool pressed);

/* Get the framebuffer for display */
const uint32_t *dragon_get_framebuffer(const Dragon *d);

#endif
