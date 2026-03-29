#include "dragon.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-60s ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)

int main(void)
{
    Dragon d;

    printf("=== Dragon 32 Machine Tests ===\n\n");

    /* --- 1: Init --- */
    printf("Initialization:\n");
    dragon_init(&d);

    TEST("CPU initialized (PC=0 before reset)");
    CHECK(d.cpu.pc == 0, "expected 0 before ROM load/reset");

    TEST("SAM in ROM mode");
    CHECK(mem_get_rom_mode(), "expected ROM mode");

    TEST("Keyboard all released ($FF)");
    int all_released = 1;
    for (int r = 0; r < 8; r++) {
        if (d.keyboard[r] != 0xFF) all_released = 0;
    }
    CHECK(all_released, "expected all $FF");

    TEST("VDG initialized");
    CHECK(d.vdg.scanline == 0, "expected scanline 0");

    TEST("PIA0 initialized");
    CHECK(d.pia0.cra == 0x00, "expected CRA=$00");

    TEST("PIA1 port B input (32K: bit 2=1)");
    CHECK(d.pia1.irb == 0xFF, "expected $FF");

    TEST("Running flag set");
    CHECK(d.running, "expected true");

    /* --- 2: ROM loading and reset --- */
    printf("\nROM loading and reset:\n");
    dragon_init(&d);
    int rc = dragon_load_rom("ROMS/d32.rom");
    TEST("ROMs load successfully");
    CHECK(rc == 0, "load failed");

    dragon_reset(&d);
    TEST("Reset vector loaded into PC");
    uint16_t reset_vec = (mem_read(0xFFFE) << 8) | mem_read(0xFFFF);
    CHECK(d.cpu.pc == reset_vec, "PC != reset vector");
    printf("  (PC=$%04X)\n", d.cpu.pc);

    TEST("CC has F and I set after reset");
    CHECK((d.cpu.cc & (CC_F | CC_I)) == (CC_F | CC_I), "expected F|I");

    TEST("DP is $00 after reset");
    CHECK(d.cpu.dp == 0x00, "expected $00");

    /* --- 3: I/O dispatch wired correctly --- */
    printf("\nI/O dispatch:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Write to PIA0 CRA via memory, then read it back */
    mem_write(0xFF01, 0x04);  /* CRA: data mode */
    TEST("PIA0 CRA accessible at $FF01");
    CHECK(mem_read(0xFF01) == 0x04, "expected $04");

    /* Write to PIA1 CRB via memory */
    mem_write(0xFF23, 0x04);
    TEST("PIA1 CRB accessible at $FF23");
    CHECK(mem_read(0xFF23) == 0x04, "expected $04");

    /* SAM write: set TY -> all-RAM mode */
    mem_write(0xFFDF, 0x00);
    TEST("SAM TY write at $FFDF switches to RAM mode");
    CHECK(!mem_get_rom_mode(), "expected RAM mode");
    mem_write(0xFFDE, 0x00);  /* restore ROM mode */

    /* --- 4: Single scanline execution --- */
    printf("\nScanline execution:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    int cycles = dragon_run_scanline(&d);
    TEST("Single scanline executes >= 57 cycles");
    CHECK(cycles >= DRAGON_CYCLES_PER_SCANLINE, "too few cycles");

    TEST("VDG scanline advanced to 1");
    CHECK(d.vdg.scanline == 1, "expected 1");

    /* --- 5: Full frame execution --- */
    printf("\nFull frame execution:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    int frame_cycles = dragon_run_frame(&d);
    TEST("Full frame executes ~17784 cycles (PAL)");
    CHECK(frame_cycles >= DRAGON_CYCLES_PER_FRAME &&
          frame_cycles < DRAGON_CYCLES_PER_FRAME + DRAGON_SCANLINES_PER_FRAME * 20,
          "cycle count out of range");
    printf("  (actual: %d cycles)\n", frame_cycles);

    TEST("Frame counter incremented to 1");
    CHECK(d.frame_count == 1, "expected 1");

    TEST("VDG back to scanline 0 after full frame");
    CHECK(d.vdg.scanline == 0, "expected 0");

    /* --- 6: Multiple frames --- */
    printf("\nMultiple frames:\n");
    for (int i = 0; i < 4; i++)
        dragon_run_frame(&d);
    TEST("5 frames total, frame_count=5");
    CHECK(d.frame_count == 5, "expected 5");

    TEST("CPU has run significant cycles (>50K)");
    CHECK(d.cpu.total_cycles > 50000, "too few total cycles");
    printf("  (total: %d cycles after 5 frames)\n", d.cpu.total_cycles);

    /* --- 7: FSYNC interrupt fires --- */
    printf("\nFSYNC interrupt:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Run 192 scanlines (active display) */
    for (int sl = 0; sl < 192; sl++)
        dragon_run_scanline(&d);

    /* The 192nd tick should have triggered FSYNC -> PIA0 CB1 */
    TEST("PIA0 CRB IRQ1 flag set after scanline 192");
    CHECK(d.pia0.crb & PIA_CR_IRQ1_FLAG, "expected IRQ1 flag");

    /* --- 8: Keyboard matrix --- */
    printf("\nKeyboard matrix:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Set up PIA0 for keyboard: port A input, port B output */
    mem_write(0xFF01, 0x00);  /* CRA: DDR mode */
    mem_write(0xFF00, 0x00);  /* DDR A: all input */
    mem_write(0xFF01, 0x04);  /* CRA: data mode */

    mem_write(0xFF03, 0x00);  /* CRB: DDR mode */
    mem_write(0xFF02, 0xFF);  /* DDR B: all output */
    mem_write(0xFF03, 0x04);  /* CRB: data mode */

    /* Select col 0 (PB bit 0 low) */
    mem_write(0xFF02, 0xFE);

    /* No keys pressed -> all rows high */
    /* Need to pump the keyboard update */
    dragon_run_scanline(&d);
    uint8_t pa = mem_read(0xFF00);
    TEST("No keys: col 0 rows read $FF");
    CHECK(pa == 0xFF, "expected $FF");

    /* Press key at row 0, col 3 (key '3') — stored as keyboard[3] bit 0 */
    dragon_key_press(&d, 0, 3, true);
    mem_write(0xFF02, 0xF7);  /* Select col 3 (PB bit 3 low) */
    dragon_run_scanline(&d);
    pa = mem_read(0xFF00);
    TEST("Key '3' pressed (row 0, col 3): PA bit 0 low on col 3");
    CHECK((pa & 0x01) == 0, "expected bit 0 clear");
    /* Other bits should be high */
    CHECK((pa | 0x01) == 0xFF, "expected other bits high");

    /* Release key */
    dragon_key_press(&d, 0, 3, false);
    dragon_run_scanline(&d);
    pa = mem_read(0xFF00);
    TEST("Key '3' released: all rows high again");
    CHECK(pa == 0xFF, "expected $FF");

    /* Test scanning different column */
    dragon_key_press(&d, 2, 1, true);  /* 'A' at row 2, col 1 */
    mem_write(0xFF02, 0xFD);  /* Select col 1 (PB bit 1 low) */
    dragon_run_scanline(&d);
    pa = mem_read(0xFF00);
    TEST("Key 'A' pressed (row 2, col 1): PA bit 2 low on col 1");
    CHECK((pa & 0x04) == 0, "expected bit 2 clear");

    /* Wrong column should not see it */
    mem_write(0xFF02, 0xFE);  /* Select col 0 */
    dragon_run_scanline(&d);
    pa = mem_read(0xFF00);
    TEST("Key 'A' not visible on col 0");
    CHECK(pa == 0xFF, "expected $FF");

    dragon_key_press(&d, 2, 1, false);

    /* --- 9: VDG mode from PIA1 port B --- */
    printf("\nVDG mode via PIA1:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Set PIA1 port B: DDR = $F8 (bits 7-3 output), data mode */
    mem_write(0xFF23, 0x00);  /* CRB: DDR mode */
    mem_write(0xFF22, 0xF8);  /* DDR B: bits 7-3 output */
    mem_write(0xFF23, 0x04);  /* CRB: data mode */

    /* Write A/G=1, GM=111, CSS=1 -> $F8 */
    mem_write(0xFF22, 0xF8);
    dragon_run_scanline(&d);  /* trigger mode update */

    TEST("VDG A/G set from PIA1 port B");
    CHECK(d.vdg.ag == true, "expected A/G=1");

    TEST("VDG GM=7 from PIA1 port B");
    CHECK(d.vdg.gm == 7, "expected GM=7");

    TEST("VDG CSS set from PIA1 port B");
    CHECK(d.vdg.css == true, "expected CSS=1");

    /* --- 10: Framebuffer access --- */
    printf("\nFramebuffer:\n");
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    const uint32_t *fb = dragon_get_framebuffer(&d);
    TEST("Framebuffer pointer is non-NULL");
    CHECK(fb != NULL, "expected non-NULL");

    /* Run a frame to populate it */
    dragon_run_frame(&d);
    TEST("Framebuffer has non-zero data after a frame");
    int has_nonzero = 0;
    for (int i = 0; i < VDG_WIDTH * VDG_HEIGHT; i++) {
        if (fb[i] != 0) { has_nonzero = 1; break; }
    }
    CHECK(has_nonzero, "expected some non-zero pixels");

    /* --- 11: Timing constants sanity --- */
    printf("\nTiming constants:\n");
    TEST("CYCLES_PER_FRAME = 57 * 312 = 17784 (PAL)");
    CHECK(DRAGON_CYCLES_PER_FRAME == 17784, "constant mismatch");

    TEST("CPU_HZ is ~889 kHz (PAL)");
    CHECK(DRAGON_CPU_HZ > 885000 && DRAGON_CPU_HZ < 892000, "out of range");

    /* --- 12: Cartridge auto-start (FIRQ delay) --- */
    printf("\nCartridge auto-start:\n");
    {
        /* Create a cartridge without $12 signature at $C000 (triggers FIRQ path) */
        const char *cart_path = "/tmp/idris6809_test_cart_firq.rom";
        FILE *cf = fopen(cart_path, "wb");
        uint8_t cart[0x2000];
        memset(cart, 0x00, sizeof(cart));
        cart[0] = 0x7E;  /* JMP instruction, NOT $12 signature */
        cart[1] = 0xC0;
        cart[2] = 0x03;  /* JMP $C003 -> loop */
        cart[3] = 0x7E;
        cart[4] = 0xC0;
        cart[5] = 0x03;
        fwrite(cart, 1, sizeof(cart), cf);
        fclose(cf);

        dragon_init(&d);
        dragon_load_rom("ROMS/d32.rom");
        mem_load_cartridge(cart_path);
        dragon_reset(&d);

        TEST("Cartridge without $12: cart_firq_delay = 120");
        CHECK(d.cart_firq_delay == 120, "expected 120");

        TEST("EXEC address set to $C000");
        uint8_t *cram = mem_get_ram();
        CHECK(cram[0x72] == 0xC0 && cram[0x73] == 0x00, "expected $C000");

        /* Run 119 frames — delay should not have fired yet */
        for (int i = 0; i < 119; i++)
            dragon_run_frame(&d);

        TEST("After 119 frames: cart_firq_delay = 1");
        CHECK(d.cart_firq_delay == 1, "expected 1");

        /* PIA1 CB1 IRQ1 flag should NOT be set yet */
        TEST("After 119 frames: PIA1 CB1 not yet triggered");
        CHECK(!(d.pia1.crb & PIA_CR_IRQ1_FLAG), "expected no flag yet");

        /* Frame 120: FIRQ should fire */
        dragon_run_frame(&d);

        TEST("After 120 frames: cart_firq_delay = 0");
        CHECK(d.cart_firq_delay == 0, "expected 0");

        TEST("After 120 frames: PIA1 CB1 triggered (CART FIRQ)");
        CHECK(d.pia1.crb & PIA_CR_IRQ1_FLAG, "expected IRQ1 flag in PIA1 CRB");

        /* Extra frame should not re-trigger */
        d.pia1.crb &= ~PIA_CR_IRQ1_FLAG;  /* clear manually */
        dragon_run_frame(&d);
        TEST("Frame 121: no re-trigger");
        CHECK(!(d.pia1.crb & PIA_CR_IRQ1_FLAG), "should not re-fire");

        mem_eject_cartridge();
        unlink(cart_path);
    }

    /* Cartridge WITH $12 signature: no FIRQ delay */
    {
        const char *cart_path = "/tmp/idris6809_test_cart_sig.rom";
        FILE *cf = fopen(cart_path, "wb");
        uint8_t cart[0x2000];
        memset(cart, 0x00, sizeof(cart));
        cart[0] = 0x12;  /* Signature byte — BASIC auto-starts via JMP */
        fwrite(cart, 1, sizeof(cart), cf);
        fclose(cf);

        dragon_init(&d);
        dragon_load_rom("ROMS/d32.rom");
        mem_load_cartridge(cart_path);
        dragon_reset(&d);

        TEST("Cartridge with $12 signature: cart_firq_delay = 0");
        CHECK(d.cart_firq_delay == 0, "expected 0");

        mem_eject_cartridge();
        unlink(cart_path);
    }

    /* No cartridge: no delay */
    {
        dragon_init(&d);
        dragon_load_rom("ROMS/d32.rom");
        dragon_reset(&d);

        TEST("No cartridge: cart_firq_delay = 0");
        CHECK(d.cart_firq_delay == 0, "expected 0");
    }

    /* --- 13: dragon_end_frame() directly --- */
    printf("\ndragon_end_frame() direct:\n");
    {
        dragon_init(&d);
        dragon_load_rom("ROMS/d32.rom");
        dragon_reset(&d);

        int fc_before = d.frame_count;
        dragon_end_frame(&d);
        TEST("dragon_end_frame increments frame_count");
        CHECK(d.frame_count == fc_before + 1, "expected increment");

        dragon_end_frame(&d);
        dragon_end_frame(&d);
        TEST("Multiple calls increment correctly");
        CHECK(d.frame_count == fc_before + 3, "expected +3");
    }

    /* --- Summary --- */
    printf("\n=== Dragon 32 Machine Tests: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
