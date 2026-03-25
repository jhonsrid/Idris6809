#include "dragon.h"
#include <stdio.h>
#include <string.h>

static int pass_count, fail_count;
static const char *current_test;

#define TEST(name) do { current_test = (name); printf("  %s", name); } while(0)
#define CHECK(cond, msg) do { \
    if (cond) { printf("%*sPASS\n", (int)(55 - strlen(current_test)), ""); pass_count++; } \
    else { printf("%*sFAIL: %s\n", (int)(55 - strlen(current_test)), "", msg); fail_count++; } \
} while(0)

/* Set up a graphics mode and render directly (no CPU execution,
 * so the ROM can't override VDG mode). */
static void setup_gfx(Dragon *d, int ag, int gm, int css, uint16_t display_addr)
{
    /* Set VDG mode directly */
    d->vdg.ag  = ag;
    d->vdg.gm  = gm;
    d->vdg.css = css;

    /* Set SAM display address via F register (bits 15-9) */
    d->sam.f = (uint8_t)(display_addr >> 9);

    /* Render all 192 visible scanlines directly */
    for (int line = 0; line < VDG_HEIGHT; line++)
        vdg_render_scanline(&d->vdg, line, display_addr);
}

int main(void)
{
    printf("=== Graphics Mode Tests ===\n");
    Dragon d;
    uint8_t *ram;
    const uint32_t *fb;

    /* ---- Text mode (reference) ---- */
    printf("\nText mode baseline:\n");

    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);
    for (int f = 0; f < 100; f++) dragon_run_frame(&d);
    fb = dragon_get_framebuffer(&d);

    TEST("Text mode: ag=0 after boot");
    CHECK(d.vdg.ag == false, "expected ag=0");

    TEST("Text mode: has bright green pixels");
    int green_count = 0;
    for (int i = 0; i < 256*192; i++)
        if ((fb[i] & 0x00FF00) > 0x80) green_count++;
    CHECK(green_count > 100, "expected green text pixels");

    /* ---- RG6: 256x192, 2-color ---- */
    printf("\nRG6 mode (256x192, 2-color):\n");

    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);
    ram = mem_get_ram();

    /* Clear graphics RAM at $0E00 (page 7) */
    memset(&ram[0x0E00], 0x00, 6144);
    /* Draw: set first byte to $FF (8 pixels on) */
    ram[0x0E00] = 0xFF;
    /* Draw: set pixel at (128, 96) = byte at offset 96*32 + 128/8 = 3072+16 */
    ram[0x0E00 + 3072 + 16] = 0x80;  /* leftmost pixel of that byte */

    /* A/G=1, GM=7 (RG6), CSS=0, display at $0E00 */
    setup_gfx(&d, 1, 7, 0, 0x0E00);
    fb = dragon_get_framebuffer(&d);

    TEST("RG6: VDG mode set correctly");
    CHECK(d.vdg.ag == true && d.vdg.gm == 7, "wrong mode");

    TEST("RG6 CSS=0: first 8 pixels are green");
    int all_green = 1;
    for (int x = 0; x < 8; x++)
        if ((fb[x] & 0x00FF00) < 0x80) all_green = 0;
    CHECK(all_green, "expected green pixels");

    TEST("RG6 CSS=0: pixel (8,0) is black");
    CHECK((fb[8] & 0x00FFFF) < 0x20, "expected black");

    TEST("RG6 CSS=0: pixel at (128,96) is set");
    CHECK((fb[96*256+128] & 0x00FF00) > 0x80, "expected green pixel");

    TEST("RG6 CSS=0: pixel at (129,96) is black");
    CHECK((fb[96*256+129] & 0x00FFFF) < 0x20, "expected black");

    /* RG6 with CSS=1 */
    setup_gfx(&d, 1, 7, 1, 0x0E00);
    fb = dragon_get_framebuffer(&d);

    TEST("RG6 CSS=1: first pixels are white/buff");
    int is_bright = (fb[0] & 0x00FF00) > 0x80 || (fb[0] & 0xFF0000) > 0x80;
    CHECK(is_bright, "expected white/buff");

    /* ---- CG6: 128x192, 4-color ---- */
    printf("\nCG6 mode (128x192, 4-color):\n");

    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);
    ram = mem_get_ram();

    /* Clear then draw with all 4 colors in first byte */
    memset(&ram[0x0E00], 0x00, 6144);
    /* CG6: 2 bits per pixel, 4 pixels per byte. MSB first.
     * Color 0=00, 1=01, 2=10, 3=11
     * Byte $E4 = 11 10 01 00 = colors 3,2,1,0 */
    ram[0x0E00] = 0xE4;

    /* A/G=1, GM=6 (CG6), CSS=0, display at $0E00 */
    setup_gfx(&d, 1, 6, 0, 0x0E00);
    fb = dragon_get_framebuffer(&d);

    TEST("CG6: VDG mode set correctly");
    CHECK(d.vdg.ag == true && d.vdg.gm == 6, "wrong mode");

    /* CG6: each logical pixel = 2 physical pixels wide
     * Pixel 0 (color 3=red CSS0): fb[0],fb[1]
     * Pixel 1 (color 2=blue CSS0): fb[2],fb[3]
     * Pixel 2 (color 1=yellow CSS0): fb[4],fb[5]
     * Pixel 3 (color 0=green CSS0): fb[6],fb[7] */
    TEST("CG6 CSS=0: pixel 0 is red (color 3)");
    CHECK((fb[0] & 0xFF0000) > 0x80 && (fb[0] & 0x00FF00) < 0x20, "expected red");

    TEST("CG6 CSS=0: pixel 1 is blue (color 2)");
    CHECK((fb[2] & 0x0000FF) > 0x80, "expected blue");

    TEST("CG6 CSS=0: pixel 2 is yellow (color 1)");
    CHECK((fb[4] & 0xFF0000) > 0x80 && (fb[4] & 0x00FF00) > 0x80, "expected yellow");

    TEST("CG6 CSS=0: pixel 3 is green (color 0)");
    CHECK((fb[6] & 0x00FF00) > 0x80 && (fb[6] & 0xFF0000) < 0x20, "expected green");

    /* CG6 with CSS=1: buff, cyan, magenta, orange */
    setup_gfx(&d, 1, 6, 1, 0x0E00);
    fb = dragon_get_framebuffer(&d);

    TEST("CG6 CSS=1: pixel 0 is orange (color 3)");
    CHECK((fb[0] & 0xFF0000) > 0x80 && (fb[0] & 0x0000FF) < 0x20, "expected orange");

    TEST("CG6 CSS=1: pixel 3 is white/buff (color 0)");
    CHECK((fb[6] & 0xFF0000) > 0x80 && (fb[6] & 0x00FF00) > 0x80 &&
          (fb[6] & 0x0000FF) > 0x80, "expected buff");

    /* ---- RG1: 128x64, 2-color (vertical repeat 3x) ---- */
    printf("\nRG1 mode (128x64, 2-color):\n");

    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);
    ram = mem_get_ram();

    memset(&ram[0x0E00], 0x00, 2048);
    /* First byte: all pixels on */
    ram[0x0E00] = 0xFF;
    /* RG1: 16 bytes per row, 64 rows = 1024 bytes. But 192/64=3 lines per row. */

    /* A/G=1, GM=1 (RG1), CSS=0 */
    setup_gfx(&d, 1, 1, 0, 0x0E00);
    fb = dragon_get_framebuffer(&d);

    TEST("RG1: VDG mode set correctly");
    CHECK(d.vdg.ag == true && d.vdg.gm == 1, "wrong mode");

    /* RG1: 128x64, doubled to 256x192.
     * Each byte = 8 pixels. 16 bytes/row. Each row repeated 3 lines.
     * Pixels doubled horizontally: logical px 0 -> fb[0],fb[1] */
    TEST("RG1: first 2 fb pixels set (horizontal double)");
    CHECK((fb[0] & 0x00FF00) > 0x80 && (fb[1] & 0x00FF00) > 0x80, "expected doubled");

    TEST("RG1: line 0 and line 1 same (vertical repeat)");
    CHECK(fb[0] == fb[256], "lines should match");

    TEST("RG1: line 0 and line 2 same (vertical repeat)");
    CHECK(fb[0] == fb[512], "lines should match");

    TEST("RG1: line 3 is different (next data row)");
    /* Second data byte is $00, so line 3 pixel 0 should be black */
    CHECK(fb[3*256] != fb[0] || ram[0x0E00+16] == 0xFF, "line 3 should differ");

    /* ---- CG1: 64x64, 4-color ---- */
    printf("\nCG1 mode (64x64, 4-color):\n");

    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);
    ram = mem_get_ram();

    memset(&ram[0x0E00], 0x00, 2048);
    /* CG1: 2 bits/pixel, 4 pixels/byte, 16 bytes/row, 64 rows.
     * Each pixel = 4x3 physical pixels.
     * Set first byte to color 3 for all 4 pixels: $FF */
    ram[0x0E00] = 0xFF;

    setup_gfx(&d, 1, 0, 0, 0x0E00);
    fb = dragon_get_framebuffer(&d);

    TEST("CG1: VDG gm=0 with ag=1");
    CHECK(d.vdg.ag == true && d.vdg.gm == 0, "wrong mode");

    TEST("CG1: first pixel is red (color 3, CSS=0)");
    CHECK((fb[0] & 0xFF0000) > 0x80, "expected red");

    /* ---- Semigraphics 4 in text mode ---- */
    printf("\nSemigraphics 4:\n");

    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);
    ram = mem_get_ram();

    /* SG4: byte bit 7=1, bits 6-4=color, bits 3-0=quadrants
     * $9F = 1 001 1111 = green, all 4 quadrants on */
    ram[0x0400] = 0x9F;
    /* $C1 = 1 100 0001 = red, top-left only */
    ram[0x0401] = 0xC1;

    setup_gfx(&d, 0, 0, 0, 0x0400);  /* Text/SG mode */
    fb = dragon_get_framebuffer(&d);

    TEST("SG4: green block at (0,0)");
    CHECK((fb[0] & 0x00FF00) > 0x80, "expected green");

    TEST("SG4: green block fills full character cell");
    CHECK((fb[11*256+3] & 0x00FF00) > 0x80, "expected green at bottom-right");

    /* ---- Display address (SAM F register) ---- */
    printf("\nDisplay address:\n");

    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);
    ram = mem_get_ram();

    /* Put graphics at two different addresses, verify SAM switches */
    memset(&ram[0x0600], 0x00, 6144);
    memset(&ram[0x2000], 0x00, 6144);
    ram[0x0600] = 0xFF;  /* pixels at page $0600 */
    ram[0x2000] = 0x00;  /* blank at page $2000 */

    setup_gfx(&d, 1, 7, 0, 0x0600);
    fb = dragon_get_framebuffer(&d);
    TEST("Display at $0600: first pixels set");
    CHECK((fb[0] & 0x00FF00) > 0x80, "expected pixels");

    setup_gfx(&d, 1, 7, 0, 0x2000);
    fb = dragon_get_framebuffer(&d);
    TEST("Display at $2000: first pixels blank");
    CHECK((fb[0] & 0x00FFFF) < 0x20, "expected black");

    /* ---- Mixed: draw a pattern and verify ---- */
    printf("\nPattern drawing:\n");

    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);
    ram = mem_get_ram();

    /* RG6 at $0E00: draw a horizontal line at row 50 */
    memset(&ram[0x0E00], 0x00, 6144);
    memset(&ram[0x0E00 + 50*32], 0xFF, 32);  /* Full row of pixels */

    setup_gfx(&d, 1, 7, 0, 0x0E00);
    fb = dragon_get_framebuffer(&d);

    TEST("Horizontal line at y=50: pixel (0,50) set");
    CHECK((fb[50*256] & 0x00FF00) > 0x80, "expected set");

    TEST("Horizontal line at y=50: pixel (255,50) set");
    CHECK((fb[50*256+255] & 0x00FF00) > 0x80, "expected set");

    TEST("Above line at y=49: pixel (128,49) clear");
    CHECK((fb[49*256+128] & 0x00FFFF) < 0x20, "expected black");

    TEST("Below line at y=51: pixel (128,51) clear");
    CHECK((fb[51*256+128] & 0x00FFFF) < 0x20, "expected black");

    /* Vertical line: set bit 7 of each byte in column 0 */
    memset(&ram[0x0E00], 0x00, 6144);
    for (int row = 0; row < 192; row++)
        ram[0x0E00 + row * 32] = 0x80;  /* bit 7 = leftmost pixel */

    setup_gfx(&d, 1, 7, 0, 0x0E00);
    fb = dragon_get_framebuffer(&d);

    TEST("Vertical line at x=0: pixel (0,0) set");
    CHECK((fb[0] & 0x00FF00) > 0x80, "expected set");

    TEST("Vertical line at x=0: pixel (0,191) set");
    CHECK((fb[191*256] & 0x00FF00) > 0x80, "expected set");

    TEST("Vertical line: pixel (1,50) clear");
    CHECK((fb[50*256+1] & 0x00FFFF) < 0x20, "expected black");

    /* Checkerboard */
    memset(&ram[0x0E00], 0x00, 6144);
    for (int row = 0; row < 192; row++)
        for (int col = 0; col < 32; col++)
            ram[0x0E00 + row*32 + col] = (row & 1) ? 0x55 : 0xAA;

    setup_gfx(&d, 1, 7, 0, 0x0E00);
    fb = dragon_get_framebuffer(&d);

    TEST("Checkerboard: (0,0) and (1,0) differ");
    CHECK(fb[0] != fb[1], "adjacent pixels should differ");

    TEST("Checkerboard: (0,0) and (0,1) differ");
    CHECK(fb[0] != fb[256], "adjacent rows should differ");

    printf("\n=== Graphics Mode Tests: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
