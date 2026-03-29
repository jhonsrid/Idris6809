#include "vdg.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-60s ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)

/* Test RAM (64K) */
static uint8_t ram[0x10000];

int main(void)
{
    VDG vdg;

    printf("=== VDG (MC6847) Tests ===\n\n");

    /* --- Test 1: Initialization --- */
    printf("Initialization:\n");
    memset(ram, 0, sizeof(ram));
    vdg_init(&vdg, ram);

    TEST("A/G defaults to 0 (alpha mode)");
    CHECK(vdg.ag == false, "expected false");

    TEST("GM defaults to 0");
    CHECK(vdg.gm == 0, "expected 0");

    TEST("CSS defaults to 0");
    CHECK(vdg.css == false, "expected false");

    TEST("Scanline defaults to 0");
    CHECK(vdg.scanline == 0, "expected 0");

    TEST("FS defaults to false");
    CHECK(vdg.fs == false, "expected false");

    TEST("Framebuffer initialized to black");
    CHECK(vdg.framebuffer[0][0] == 0x00000000, "expected black");
    CHECK(vdg.framebuffer[191][255] == 0x00000000, "expected black");

    /* --- Test 2: Mode setting from PIA1 port B --- */
    printf("\nMode setting (vdg_set_mode):\n");

    /* Alpha mode: A/G=0, GM=0, CSS=0 -> PIA1B = $00 */
    vdg_set_mode(&vdg, 0x00);
    TEST("PIA1B=$00: A/G=0, GM=000, CSS=0");
    CHECK(!vdg.ag && vdg.gm == 0 && !vdg.css, "mode mismatch");

    /* RG6 mode: A/G=1, GM=111, CSS=0 -> PIA1B = $F0 */
    vdg_set_mode(&vdg, 0xF0);
    TEST("PIA1B=$F0: A/G=1, GM=111, CSS=0");
    CHECK(vdg.ag && vdg.gm == 7 && !vdg.css, "mode mismatch");

    /* CG3+CSS: A/G=1, GM=100, CSS=1 -> PIA1B = $C8 */
    vdg_set_mode(&vdg, 0xC8);
    TEST("PIA1B=$C8: A/G=1, GM=100, CSS=1");
    CHECK(vdg.ag && vdg.gm == 4 && vdg.css, "mode mismatch");

    /* Alpha with CSS: A/G=0, GM=0, CSS=1 -> PIA1B = $08 */
    vdg_set_mode(&vdg, 0x08);
    TEST("PIA1B=$08: A/G=0, CSS=1 (orange text)");
    CHECK(!vdg.ag && vdg.css, "mode mismatch");

    /* --- Test 3: Font data --- */
    printf("\nFont data:\n");
    const uint8_t *font = vdg_get_font();

    TEST("Font pointer is non-NULL");
    CHECK(font != NULL, "NULL font");

    /* Space character ($20) should be all zeros */
    int space_blank = 1;
    for (int row = 0; row < 12; row++) {
        if (font[0x20 * 12 + row] != 0) {
            space_blank = 0;
            break;
        }
    }
    TEST("Space ($20) is blank");
    CHECK(space_blank, "space has non-zero pixels");

    /* 'A' ($01) should have some non-zero rows */
    int a_nonzero = 0;
    for (int row = 0; row < 12; row++) {
        if (font[0x01 * 12 + row] != 0)
            a_nonzero++;
    }
    TEST("'A' ($01) has glyph data");
    CHECK(a_nonzero >= 5, "expected at least 5 rows with pixels");

    /* '@' ($00) should have glyph data */
    int at_nonzero = 0;
    for (int row = 0; row < 12; row++) {
        if (font[0x00 * 12 + row] != 0)
            at_nonzero++;
    }
    TEST("'@' ($00) has glyph data");
    CHECK(at_nonzero >= 5, "expected glyph data");

    /* --- Test 4: Text mode rendering --- */
    printf("\nText mode rendering:\n");
    memset(ram, 0x20, 512);  /* Fill with spaces */
    vdg_init(&vdg, ram);
    vdg_set_mode(&vdg, 0x00);  /* Alpha, CSS=0 */

    /* Put 'A' ($01) at position 0 (display_addr = $0400 equivalent, but we use 0) */
    ram[0] = 0x01;  /* 'A' */
    vdg_render_scanline(&vdg, 0, 0x0000);

    /* Row 0 of 'A' is font row 0 (should be blank - top padding) */
    TEST("Text 'A' row 0: top padding (background)");
    /* All 8 pixels of the first char cell should be background */
    uint32_t bg_green = 0x00005000;  /* dark green BG */
    int all_bg = 1;
    for (int x = 0; x < 8; x++) {
        if (vdg.framebuffer[0][x] != bg_green)
            all_bg = 0;
    }
    CHECK(all_bg, "expected all background on blank font row");

    /* Row 2 of 'A' has the peak: 0x10 = 00010000 -> pixel at x=3 */
    vdg_render_scanline(&vdg, 2, 0x0000);
    uint32_t fg_green = 0x0000FF00;  /* bright green FG */
    TEST("Text 'A' row 2: has foreground pixel");
    /* At least one pixel should be foreground */
    int found_fg = 0;
    for (int x = 0; x < 8; x++) {
        if (vdg.framebuffer[2][x] == fg_green)
            found_fg = 1;
    }
    CHECK(found_fg, "expected foreground pixel in 'A' glyph");

    /* Second column (x=8..15) should be all background (space char) */
    TEST("Text space at col 1: all background");
    all_bg = 1;
    for (int x = 8; x < 16; x++) {
        if (vdg.framebuffer[2][x] != bg_green)
            all_bg = 0;
    }
    CHECK(all_bg, "expected all background for space");

    /* --- Test 5: Inverse video --- */
    printf("\nInverse video:\n");
    memset(ram, 0x20, 512);
    vdg_init(&vdg, ram);
    vdg_set_mode(&vdg, 0x00);

    ram[0] = 0x41;  /* 'A' with inverse (bit 6 set): 0x40 | 0x01 */
    vdg_render_scanline(&vdg, 0, 0x0000);

    TEST("Inverse 'A' row 0: foreground (inverted blank row)");
    /* Blank font row inverted = all foreground */
    int all_fg = 1;
    for (int x = 0; x < 8; x++) {
        if (vdg.framebuffer[0][x] != fg_green)
            all_fg = 0;
    }
    CHECK(all_fg, "expected all foreground (inverted)");

    /* --- Test 6: Semigraphics 4 --- */
    printf("\nSemigraphics 4:\n");
    memset(ram, 0, 512);
    vdg_init(&vdg, ram);
    vdg_set_mode(&vdg, 0x00);

    /* SG4 byte: bit7=1, color=green(0), pattern=top-left only (bit3) */
    /* byte = 1_000_1000 = $88 */
    ram[0] = 0x88;
    vdg_render_scanline(&vdg, 0, 0x0000);

    TEST("SG4 top-left block: left half is green");
    uint32_t sg_green = 0x0000B400;
    int left_green = 1;
    for (int x = 0; x < 4; x++) {
        if (vdg.framebuffer[0][x] != sg_green)
            left_green = 0;
    }
    CHECK(left_green, "expected green on left half");

    TEST("SG4 top-left block: right half is black");
    uint32_t black = 0x00000000;
    int right_black = 1;
    for (int x = 4; x < 8; x++) {
        if (vdg.framebuffer[0][x] != black)
            right_black = 0;
    }
    CHECK(right_black, "expected black on right half");

    /* Bottom half should be black (bits 1,0 = 0) */
    vdg_render_scanline(&vdg, 6, 0x0000);  /* row 6 = bottom half */
    TEST("SG4 bottom half: all black");
    int bottom_black = 1;
    for (int x = 0; x < 8; x++) {
        if (vdg.framebuffer[6][x] != black)
            bottom_black = 0;
    }
    CHECK(bottom_black, "expected all black on bottom half");

    /* SG4 with red color (color=3=red): byte = 1_011_1111 = $BF (all blocks on) */
    ram[1] = 0xBF;
    vdg_render_scanline(&vdg, 0, 0x0000);
    TEST("SG4 all-on red: col 1 is red");
    uint32_t red = 0x00B40000;
    int all_red = 1;
    for (int x = 8; x < 16; x++) {
        if (vdg.framebuffer[0][x] != red)
            all_red = 0;
    }
    CHECK(all_red, "expected red in all 8 pixels");

    /* --- Test 7: CSS affects text color --- */
    printf("\nCSS color selection:\n");
    memset(ram, 0x20, 512);
    vdg_init(&vdg, ram);

    /* CSS=1 -> orange text */
    vdg_set_mode(&vdg, 0x08);
    ram[0] = 0x20;  /* space */
    vdg_render_scanline(&vdg, 0, 0x0000);
    uint32_t bg_orange = 0x00503000;
    TEST("CSS=1: text background is dark orange");
    CHECK(vdg.framebuffer[0][0] == bg_orange, "expected dark orange bg");

    /* --- Test 8: RG6 mode rendering (256x192, 1bpp) --- */
    printf("\nRG6 mode (256x192, 2-color):\n");
    memset(ram, 0, sizeof(ram));
    vdg_init(&vdg, ram);
    vdg_set_mode(&vdg, 0xF0);  /* A/G=1, GM=111, CSS=0 */

    /* First byte at display addr 0: $AA = 10101010 -> alternating pixels */
    ram[0] = 0xAA;
    vdg_render_scanline(&vdg, 0, 0x0000);

    uint32_t rg_green = 0x0000B400;
    TEST("RG6 byte $AA: pixel 0 is green (bit 7 set)");
    CHECK(vdg.framebuffer[0][0] == rg_green, "expected green");

    TEST("RG6 byte $AA: pixel 1 is black (bit 6 clear)");
    CHECK(vdg.framebuffer[0][1] == black, "expected black");

    TEST("RG6 byte $AA: pixel 2 is green (bit 5 set)");
    CHECK(vdg.framebuffer[0][2] == rg_green, "expected green");

    TEST("RG6 byte $AA: pixel 3 is black (bit 4 clear)");
    CHECK(vdg.framebuffer[0][3] == black, "expected black");

    /* All-on byte */
    ram[1] = 0xFF;
    vdg_render_scanline(&vdg, 0, 0x0000);
    TEST("RG6 byte $FF: all 8 pixels green");
    int all_on = 1;
    for (int x = 8; x < 16; x++) {
        if (vdg.framebuffer[0][x] != rg_green)
            all_on = 0;
    }
    CHECK(all_on, "expected all green");

    /* CSS=1 in RG6 -> white foreground */
    vdg_set_mode(&vdg, 0xF8);  /* A/G=1, GM=111, CSS=1 */
    vdg_render_scanline(&vdg, 0, 0x0000);
    uint32_t rg_white = 0x00B4B4B4;
    TEST("RG6 CSS=1: foreground is white");
    CHECK(vdg.framebuffer[0][0] == rg_white, "expected white");

    /* --- Test 9: CG6 mode rendering (128x192, 4-color) --- */
    printf("\nCG6 mode (128x192, 4-color):\n");
    memset(ram, 0, sizeof(ram));
    vdg_init(&vdg, ram);
    vdg_set_mode(&vdg, 0xE0);  /* A/G=1, GM=110, CSS=0 */

    /* 2 bits per pixel, 4 pixels per byte, each pixel doubled to 256 width
     * Byte $E4 = 11_10_01_00 -> colors 3,2,1,0 */
    ram[0] = 0xE4;
    vdg_render_scanline(&vdg, 0, 0x0000);

    /* CSS=0 colors: 0=green, 1=yellow, 2=blue, 3=red */
    uint32_t cg0_green  = 0x0000B400;
    uint32_t cg0_yellow = 0x00B4B400;
    uint32_t cg0_blue   = 0x000000B4;
    uint32_t cg0_red    = 0x00B40000;

    /* Each pixel is doubled horizontally: pixel 0 at columns 0-1, pixel 1 at 2-3, etc. */
    TEST("CG6 pixel 0 (color 3=red) at x=0,1");
    CHECK(vdg.framebuffer[0][0] == cg0_red && vdg.framebuffer[0][1] == cg0_red,
          "expected red");

    TEST("CG6 pixel 1 (color 2=blue) at x=2,3");
    CHECK(vdg.framebuffer[0][2] == cg0_blue && vdg.framebuffer[0][3] == cg0_blue,
          "expected blue");

    TEST("CG6 pixel 2 (color 1=yellow) at x=4,5");
    CHECK(vdg.framebuffer[0][4] == cg0_yellow && vdg.framebuffer[0][5] == cg0_yellow,
          "expected yellow");

    TEST("CG6 pixel 3 (color 0=green) at x=6,7");
    CHECK(vdg.framebuffer[0][6] == cg0_green && vdg.framebuffer[0][7] == cg0_green,
          "expected green");

    /* --- Test 10: RG1 mode (128x64, vertical repeat 3) --- */
    printf("\nRG1 mode (128x64, 2-color, vrepeat=3):\n");
    memset(ram, 0, sizeof(ram));
    vdg_init(&vdg, ram);
    vdg_set_mode(&vdg, 0x90);  /* A/G=1, GM=001, CSS=0 */

    ram[0] = 0xFF;  /* First byte of row 0 = all pixels on */
    vdg_render_scanline(&vdg, 0, 0x0000);
    vdg_render_scanline(&vdg, 1, 0x0000);
    vdg_render_scanline(&vdg, 2, 0x0000);
    vdg_render_scanline(&vdg, 3, 0x0000);

    TEST("RG1 lines 0,1,2 all read same data row (vrepeat=3)");
    int same_012 = 1;
    for (int x = 0; x < 16; x++) {
        if (vdg.framebuffer[0][x] != vdg.framebuffer[1][x] ||
            vdg.framebuffer[0][x] != vdg.framebuffer[2][x])
            same_012 = 0;
    }
    CHECK(same_012, "lines 0-2 should be identical");

    TEST("RG1 line 3 reads next data row (all zeros = black)");
    CHECK(vdg.framebuffer[3][0] == black, "expected black on line 3");

    /* RG1 horizontal: 128 pixels from 16 bytes, doubled to 256 */
    TEST("RG1 pixels are doubled horizontally");
    CHECK(vdg.framebuffer[0][0] == vdg.framebuffer[0][1],
          "adjacent pixels should match");

    /* --- Test 11: Timing / scanline counter --- */
    printf("\nTiming (scanline counter):\n");
    memset(ram, 0, sizeof(ram));
    vdg_init(&vdg, ram);

    TEST("Initial scanline is 0");
    CHECK(vdg.scanline == 0, "expected 0");

    /* Tick through active lines (0-191) */
    bool got_fs = false;
    for (int i = 0; i < 191; i++) {
        got_fs |= vdg_tick_scanline(&vdg);
    }
    TEST("No FS during active lines 0-191");
    CHECK(!got_fs, "FS should not trigger during active");
    TEST("Scanline is 191 after 191 ticks");
    CHECK(vdg.scanline == 191, "expected 191");

    /* Next tick should enter vblank and assert FS */
    bool fs_edge = vdg_tick_scanline(&vdg);
    TEST("FS triggers at scanline 192 (vblank start)");
    CHECK(fs_edge, "expected FS edge");
    CHECK(vdg.scanline == 192, "expected scanline 192");
    CHECK(vdg.fs == true, "expected FS active");

    /* Continue through vblank - no more FS edges */
    got_fs = false;
    for (int i = 193; i < VDG_SCANLINES_PER_FRAME; i++) {
        got_fs |= vdg_tick_scanline(&vdg);
    }
    TEST("No additional FS edges during vblank");
    CHECK(!got_fs, "FS should only trigger once");
    CHECK(vdg.scanline == VDG_SCANLINES_PER_FRAME - 1, "expected last scanline");

    /* Next tick wraps to 0, FS clears */
    vdg_tick_scanline(&vdg);
    TEST("Scanline wraps to 0 after full frame");
    CHECK(vdg.scanline == 0, "expected 0");
    TEST("FS clears after wrap");
    CHECK(vdg.fs == false, "expected FS inactive");

    /* --- Test 12: Full frame cycle --- */
    printf("\nFull frame cycle:\n");
    vdg_init(&vdg, ram);

    int fs_count = 0;
    for (int frame = 0; frame < 3; frame++) {
        for (int i = 0; i < VDG_SCANLINES_PER_FRAME; i++) {
            if (vdg_tick_scanline(&vdg))
                fs_count++;
        }
    }
    TEST("Exactly 3 FS edges in 3 frames");
    CHECK(fs_count == 3, "expected 3");

    /* --- Test 13: Render full frame --- */
    printf("\nFull frame render:\n");
    memset(ram, 0x20, 512);  /* Fill with spaces */
    vdg_init(&vdg, ram);
    vdg_set_mode(&vdg, 0x00);

    vdg_render_frame(&vdg, 0x0000);
    TEST("Full frame renders without crash");
    PASS();

    /* Last line should have same background as first (all spaces) */
    TEST("Line 0 and line 191 have same bg (all spaces)");
    CHECK(vdg.framebuffer[0][0] == vdg.framebuffer[191][0],
          "expected matching background");

    /* --- Test 14: Semigraphics 6 byte patterns --- */
    /* SG6 on a real MC6847 uses bit 7=1 with 6 block bits (2×3 grid)
     * and CSS-only color. The Dragon VDG implementation renders these
     * as SG4 (interpreting bits 6-4 as color and bits 3-0 as 2×2 blocks).
     * This test documents the actual rendering behavior. */
    printf("\nSemigraphics 6 byte patterns (rendered as SG4):\n");
    memset(ram, 0, 512);
    vdg_init(&vdg, ram);
    vdg_set_mode(&vdg, 0x00);  /* text/SG mode */

    /* Byte $BF = 1 011 1111: SG4 interprets as color 3 (red), all 4 blocks on */
    ram[0] = 0xBF;
    vdg_render_scanline(&vdg, 0, 0x0000);   /* top half */
    vdg_render_scanline(&vdg, 6, 0x0000);   /* bottom half */

    TEST("SG byte $BF: top-left block on (SG4 color 3)");
    CHECK((vdg.framebuffer[0][0] & 0xFF0000) > 0x80, "expected red");

    TEST("SG byte $BF: bottom-right block on");
    CHECK((vdg.framebuffer[6][4] & 0xFF0000) > 0x80, "expected red");

    /* Byte $FF = 1 111 1111: SG4 color 7 (orange), all blocks on */
    ram[0] = 0xFF;
    vdg_render_scanline(&vdg, 0, 0x0000);

    TEST("SG byte $FF: color 7 (orange), all blocks on");
    CHECK((vdg.framebuffer[0][0] & 0xFF0000) > 0x80, "expected orange component");
    CHECK(vdg.framebuffer[0][0] == vdg.framebuffer[0][4], "left == right");

    /* Byte $80 = 1 000 0000: SG4 color 0 (green), no blocks on */
    ram[0] = 0x80;
    vdg_render_scanline(&vdg, 0, 0x0000);

    TEST("SG byte $80: all blocks off (black)");
    CHECK(vdg.framebuffer[0][0] == 0x00000000, "expected black");

    /* Byte $88 = 1 000 1000: SG4 color 0 (green), top-left only */
    ram[0] = 0x88;
    vdg_render_scanline(&vdg, 0, 0x0000);   /* top half */
    vdg_render_scanline(&vdg, 6, 0x0000);   /* bottom half */

    TEST("SG byte $88: top-left green, top-right black");
    CHECK((vdg.framebuffer[0][0] & 0x00FF00) > 0x80, "expected green");
    CHECK(vdg.framebuffer[0][4] == 0x00000000, "expected black");

    TEST("SG byte $88: bottom half all black");
    CHECK(vdg.framebuffer[6][0] == 0x00000000, "expected black");

    /* --- Summary --- */
    printf("\n=== VDG (MC6847) Tests: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
