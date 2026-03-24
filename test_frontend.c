#include "dragon.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-60s ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)

int main(void)
{
    Dragon d;

    printf("=== Frontend Integration Tests ===\n\n");

    /* --- 1: Headless frame execution (same as --headless mode) --- */
    printf("Headless execution:\n");
    dragon_init(&d);
    int rc = dragon_load_rom("ROMS/d32.rom");
    CHECK(rc == 0, "ROM load");
    dragon_reset(&d);

    /* Run 10 frames headless */
    for (int f = 0; f < 10; f++)
        dragon_run_frame(&d);

    TEST("10 headless frames complete");
    CHECK(d.frame_count == 10, "expected 10 frames");

    TEST("CPU executed >100K cycles in 10 frames");
    CHECK(d.cpu.total_cycles > 100000, "too few cycles");
    printf("  (total: %d cycles)\n", d.cpu.total_cycles);

    TEST("Framebuffer has rendered content");
    const uint32_t *fb = dragon_get_framebuffer(&d);
    int has_content = 0;
    for (int i = 0; i < VDG_WIDTH * VDG_HEIGHT; i++) {
        if (fb[i] != 0) { has_content = 1; break; }
    }
    CHECK(has_content, "expected non-black pixels");

    /* --- 2: Keyboard matrix mapping coverage --- */
    printf("\nKeyboard matrix:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Set up PIA0 for keyboard scanning */
    mem_write(0xFF01, 0x00);  /* CRA DDR mode */
    mem_write(0xFF00, 0x00);  /* DDR A all input */
    mem_write(0xFF01, 0x04);  /* CRA data mode */
    mem_write(0xFF03, 0x00);  /* CRB DDR mode */
    mem_write(0xFF02, 0xFF);  /* DDR B all output */
    mem_write(0xFF03, 0x04);  /* CRB data mode */

    /* Test all letter keys A-Z mapping
     * PB selects column (driven axis), PA reads row (read axis).
     * dragon_key_press(row, col) → keyboard[col] bit row */
    int letter_row[] = {2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4, 5,5,5};
    int letter_col[] = {1,2,3,4,5,6,7, 0,1,2,3,4,5,6,7, 0,1,2,3,4,5,6,7, 0,1,2};

    int letters_ok = 1;
    for (int i = 0; i < 26; i++) {
        int row = letter_row[i];
        int col = letter_col[i];

        dragon_key_press(&d, row, col, true);

        /* Select the correct column via PB */
        mem_write(0xFF02, (uint8_t)(~(1 << col)));
        dragon_run_scanline(&d);

        uint8_t pa = mem_read(0xFF00);
        if (pa & (1 << row)) {
            printf("  FAIL: letter '%c' (row=%d, col=%d) not detected\n",
                   'A' + i, row, col);
            letters_ok = 0;
        }

        dragon_key_press(&d, row, col, false);
    }
    TEST("All 26 letter keys scannable (A-Z)");
    CHECK(letters_ok, "some letters failed");

    /* Test digit keys 0-9 */
    int digits_ok = 1;
    for (int i = 0; i < 10; i++) {
        int row = (i < 8) ? 0 : 1;
        int col = (i < 8) ? i : (i - 8);

        dragon_key_press(&d, row, col, true);
        mem_write(0xFF02, (uint8_t)(~(1 << col)));
        dragon_run_scanline(&d);

        uint8_t pa = mem_read(0xFF00);
        if (pa & (1 << row)) {
            printf("  FAIL: digit '%d' (row=%d, col=%d) not detected\n",
                   i, row, col);
            digits_ok = 0;
        }
        dragon_key_press(&d, row, col, false);
    }
    TEST("All 10 digit keys scannable (0-9)");
    CHECK(digits_ok, "some digits failed");

    /* Test ENTER (row 6, col 0): PB0 driven, PA6 read */
    dragon_key_press(&d, 6, 0, true);
    mem_write(0xFF02, (uint8_t)(~(1 << 0)));
    dragon_run_scanline(&d);
    TEST("ENTER key (row 6, col 0)");
    CHECK(!(mem_read(0xFF00) & (1 << 6)), "expected PA bit 6 low");
    dragon_key_press(&d, 6, 0, false);

    /* SPACE (row 5, col 7): PB7 driven, PA5 read */
    dragon_key_press(&d, 5, 7, true);
    mem_write(0xFF02, (uint8_t)(~(1 << 7)));
    dragon_run_scanline(&d);
    TEST("SPACE key (row 5, col 7)");
    CHECK(!(mem_read(0xFF00) & (1 << 5)), "expected PA bit 5 low");
    dragon_key_press(&d, 5, 7, false);

    /* SHIFT (row 6, col 7): PB7 driven, PA6 read */
    dragon_key_press(&d, 6, 7, true);
    mem_write(0xFF02, (uint8_t)(~(1 << 7)));
    dragon_run_scanline(&d);
    TEST("SHIFT key (row 6, col 7)");
    CHECK(!(mem_read(0xFF00) & (1 << 6)), "expected PA bit 6 low");
    dragon_key_press(&d, 6, 7, false);

    /* Test simultaneous keys: SHIFT (row 6, col 7) + A (row 2, col 1) */
    dragon_key_press(&d, 6, 7, true);  /* SHIFT */
    dragon_key_press(&d, 2, 1, true);  /* A */
    mem_write(0xFF02, (uint8_t)(~(1 << 1)));  /* Select col 1 for A */
    dragon_run_scanline(&d);
    TEST("SHIFT+A: 'A' detected on col 1");
    CHECK(!(mem_read(0xFF00) & (1 << 2)), "expected PA bit 2 low");
    mem_write(0xFF02, (uint8_t)(~(1 << 7)));  /* Select col 7 for SHIFT */
    dragon_run_scanline(&d);
    TEST("SHIFT+A: SHIFT detected on col 7");
    CHECK(!(mem_read(0xFF00) & (1 << 6)), "expected PA bit 6 low");
    dragon_key_press(&d, 6, 7, false);
    dragon_key_press(&d, 2, 1, false);

    /* --- 3: Audio DAC output --- */
    printf("\nAudio DAC:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Set up PIA1 port A for DAC output */
    mem_write(0xFF21, 0x00);  /* CRA DDR mode */
    mem_write(0xFF20, 0x3F);  /* DDR A bits 0-5 output */
    mem_write(0xFF21, 0x04);  /* CRA data mode */

    /* Write DAC value */
    mem_write(0xFF20, 0x20);  /* DAC = $20 = 32 (midpoint) */
    TEST("PIA1 ORA holds DAC value $20");
    CHECK((d.pia1.ora & 0x3F) == 0x20, "expected $20");

    mem_write(0xFF20, 0x3F);  /* DAC = max */
    TEST("PIA1 ORA holds DAC value $3F (max)");
    CHECK((d.pia1.ora & 0x3F) == 0x3F, "expected $3F");

    mem_write(0xFF20, 0x00);  /* DAC = 0 */
    TEST("PIA1 ORA holds DAC value $00 (min)");
    CHECK((d.pia1.ora & 0x3F) == 0x00, "expected $00");

    /* Set up PIA1 port B for single-bit sound */
    mem_write(0xFF23, 0x00);
    mem_write(0xFF22, 0x02);  /* DDR B bit 1 output */
    mem_write(0xFF23, 0x04);

    mem_write(0xFF22, 0x02);  /* SBS on */
    TEST("PIA1 ORB bit 1 (single-bit sound) set");
    CHECK((d.pia1.orb & 0x02) != 0, "expected bit 1 set");

    mem_write(0xFF22, 0x00);  /* SBS off */
    TEST("PIA1 ORB bit 1 (single-bit sound) clear");
    CHECK((d.pia1.orb & 0x02) == 0, "expected bit 1 clear");

    /* --- 4: VDG mode update path via PIA1 writes --- */
    printf("\nVDG mode update path:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Set PIA1 port B to RG6 mode: A/G=1, GM=111, CSS=0 -> $F0 */
    mem_write(0xFF23, 0x00);  /* DDR mode */
    mem_write(0xFF22, 0xF8);  /* DDR B: bits 7-3 output */
    mem_write(0xFF23, 0x04);  /* data mode */
    mem_write(0xFF22, 0xF0);  /* A/G=1, GM=111, CSS=0 */

    /* Run a scanline to trigger VDG mode update */
    dragon_run_scanline(&d);

    TEST("VDG mode updated to A/G=1 via PIA1 write");
    CHECK(d.vdg.ag == true, "expected A/G=1");

    TEST("VDG GM=7 via PIA1 write");
    CHECK(d.vdg.gm == 7, "expected GM=7");

    TEST("VDG CSS=0 via PIA1 write");
    CHECK(d.vdg.css == false, "expected CSS=0");

    /* Switch to text mode with CSS — check immediately after PIA write
     * (don't run scanline as ROM code may override PIA1 state) */
    mem_write(0xFF22, 0x08);  /* A/G=0, GM=000, CSS=1 */
    vdg_set_mode(&d.vdg, pia_get_output_b(&d.pia1));

    TEST("VDG switched to alpha mode");
    CHECK(d.vdg.ag == false, "expected A/G=0");

    TEST("VDG CSS=1 after mode change");
    CHECK(d.vdg.css == true, "expected CSS=1");

    /* --- 5: SAM display address via memory writes --- */
    printf("\nSAM display address integration:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Set display at $0400: F1=1, all others 0 */
    mem_write(0xFFC6, 0x00);  /* Clear F0 */
    mem_write(0xFFC9, 0x00);  /* Set F1 */
    mem_write(0xFFCA, 0x00);  /* Clear F2 */
    mem_write(0xFFCC, 0x00);  /* Clear F3 */
    mem_write(0xFFCE, 0x00);  /* Clear F4 */
    mem_write(0xFFD0, 0x00);  /* Clear F5 */
    mem_write(0xFFD2, 0x00);  /* Clear F6 */

    TEST("SAM display address = $0400 after setup");
    CHECK(sam_get_display_addr(&d.sam) == 0x0400, "expected $0400");

    /* --- 6: Timing accuracy over multiple frames --- */
    printf("\nTiming accuracy:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    int total = 0;
    for (int f = 0; f < 60; f++)
        total += dragon_run_frame(&d);

    /* 60 frames at PAL: 60 * 17784 = 1,067,040 cycles
     * Allow some slack for instruction overrun at scanline boundaries */
    TEST("60 frames ~1.07M cycles (within 5%%)");
    int expected = 60 * DRAGON_CYCLES_PER_FRAME;
    double ratio = (double)total / expected;
    CHECK(ratio > 0.95 && ratio < 1.05, "timing out of range");
    printf("  (expected ~%d, got %d, ratio=%.3f)\n", expected, total, ratio);

    TEST("Frame counter = 60");
    CHECK(d.frame_count == 60, "expected 60");

    /* --- 7: Reset clears state properly --- */
    printf("\nReset behavior:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Run 5 frames, then reset */
    for (int f = 0; f < 5; f++)
        dragon_run_frame(&d);
    int pre_cycles = d.cpu.total_cycles;
    TEST("Pre-reset cycles > 0");
    CHECK(pre_cycles > 0, "expected cycles");

    dragon_reset(&d);
    TEST("After reset: PC = reset vector");
    uint16_t rv = (mem_read(0xFFFE) << 8) | mem_read(0xFFFF);
    CHECK(d.cpu.pc == rv, "PC doesn't match reset vector");

    TEST("After reset: CC has F|I set");
    CHECK((d.cpu.cc & (CC_F | CC_I)) == (CC_F | CC_I), "expected F|I");

    /* --- Summary --- */
    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
