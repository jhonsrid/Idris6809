#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-50s ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)

/* Stub I/O handlers for testing */
static uint8_t pia0_last_write_addr;
static uint8_t pia0_last_write_val;

static uint8_t test_pia0_read(uint16_t addr)
{
    /* Return different values based on register to prove dispatch works */
    return 0xA0 + (uint8_t)(addr & 0x03);
}

static void test_pia0_write(uint16_t addr, uint8_t val)
{
    pia0_last_write_addr = (uint8_t)(addr & 0xFF);
    pia0_last_write_val = val;
}

int main(void)
{
    printf("=== Memory Subsystem Tests ===\n\n");

    /* --- Test 1: Basic RAM read/write in low 32K --- */
    printf("Low RAM ($0000-$7FFF):\n");
    mem_init();

    mem_write(0x0000, 0x42);
    TEST("Write/read at $0000");
    CHECK(mem_read(0x0000) == 0x42, "expected $42");

    mem_write(0x7FFF, 0xBE);
    TEST("Write/read at $7FFF");
    CHECK(mem_read(0x7FFF) == 0xBE, "expected $BE");

    mem_write(0x0400, 0x55);
    TEST("Write/read at $0400 (video RAM area)");
    CHECK(mem_read(0x0400) == 0x55, "expected $55");

    /* --- Test 2: ROM overlay at $8000-$FEFF --- */
    printf("\nROM overlay ($8000-$FEFF):\n");
    mem_init();

    /* Manually load some test ROM data */
    /* mem_load_rom loads into rom[] buffer at given offset */
    /* We'll write directly to RAM and ROM to test separation */

    /* Write test pattern to RAM at $8000 */
    uint8_t *ram = mem_get_ram();
    ram[0x8000] = 0xAA;
    ram[0xC000] = 0xBB;
    ram[0xFEFF] = 0xCC;

    /* In ROM mode (default), reads should come from ROM buffer (zeros) */
    TEST("ROM mode: $8000 reads from ROM (not RAM)");
    CHECK(mem_read(0x8000) == 0x00, "expected $00 from ROM");

    TEST("ROM mode: $C000 reads from ROM (not RAM)");
    CHECK(mem_read(0xC000) == 0x00, "expected $00 from ROM");

    TEST("ROM mode: $FEFF reads from ROM (not RAM)");
    CHECK(mem_read(0xFEFF) == 0x00, "expected $00 from ROM");

    /* Switch to all-RAM mode */
    mem_set_rom_mode(false);

    TEST("RAM mode: $8000 reads from RAM");
    CHECK(mem_read(0x8000) == 0xAA, "expected $AA from RAM");

    TEST("RAM mode: $C000 reads from RAM");
    CHECK(mem_read(0xC000) == 0xBB, "expected $BB from RAM");

    TEST("RAM mode: $FEFF reads from RAM");
    CHECK(mem_read(0xFEFF) == 0xCC, "expected $CC from RAM");

    /* Switch back to ROM mode */
    mem_set_rom_mode(true);

    TEST("Back to ROM mode: $8000 reads from ROM again");
    CHECK(mem_read(0x8000) == 0x00, "expected $00 from ROM");

    /* --- Test 3: Writes to $8000-$FEFF always go to RAM --- */
    printf("\nWrites through ROM overlay:\n");
    mem_init();

    mem_write(0x8000, 0xDD);
    TEST("Write $DD to $8000 in ROM mode goes to RAM");
    mem_set_rom_mode(false);
    CHECK(mem_read(0x8000) == 0xDD, "expected $DD in RAM");
    mem_set_rom_mode(true);

    mem_write(0xC000, 0xEE);
    TEST("Write $EE to $C000 in ROM mode goes to RAM");
    mem_set_rom_mode(false);
    CHECK(mem_read(0xC000) == 0xEE, "expected $EE in RAM");
    mem_set_rom_mode(true);

    /* --- Test 4: Interrupt vectors always from ROM --- */
    printf("\nInterrupt vectors ($FFE0-$FFFF):\n");
    mem_init();

    /* Put test data in RAM at vector locations */
    ram = mem_get_ram();
    ram[0xFFFE] = 0x12;
    ram[0xFFFF] = 0x34;

    TEST("Vector $FFFE reads from ROM (not RAM), ROM mode");
    CHECK(mem_read(0xFFFE) == 0x00, "expected $00 from ROM");

    mem_set_rom_mode(false);
    TEST("Vector $FFFE reads from ROM even in RAM mode");
    CHECK(mem_read(0xFFFE) == 0x00, "expected $00 from ROM, not $12 from RAM");
    mem_set_rom_mode(true);

    /* Write to vector area goes to RAM */
    mem_write(0xFFF0, 0x56);
    TEST("Write to $FFF0 goes to RAM underneath");
    CHECK(ram[0xFFF0] == 0x56, "expected $56 in RAM");

    TEST("Read from $FFF0 still comes from ROM");
    CHECK(mem_read(0xFFF0) == 0x00, "expected $00 from ROM");

    /* --- Test 5: I/O region dispatch --- */
    printf("\nI/O dispatch ($FF00-$FFBF):\n");
    mem_init();

    /* Register PIA0 handler at $FF00-$FF03 */
    mem_register_io(0xFF00, 4, test_pia0_read, test_pia0_write);

    TEST("PIA0 read $FF00 dispatches to handler");
    CHECK(mem_read(0xFF00) == 0xA0, "expected $A0");

    TEST("PIA0 read $FF01 dispatches to handler");
    CHECK(mem_read(0xFF01) == 0xA1, "expected $A1");

    TEST("PIA0 read $FF03 dispatches to handler");
    CHECK(mem_read(0xFF03) == 0xA3, "expected $A3");

    mem_write(0xFF02, 0x77);
    TEST("PIA0 write $FF02 dispatches to handler");
    CHECK(pia0_last_write_addr == 0x02 && pia0_last_write_val == 0x77,
          "expected addr=$02, val=$77");

    /* Unregistered I/O address returns $FF */
    TEST("Unregistered I/O $FF10 reads $FF");
    CHECK(mem_read(0xFF10) == 0xFF, "expected $FF");

    TEST("Unregistered I/O $FF20 reads $FF");
    CHECK(mem_read(0xFF20) == 0xFF, "expected $FF");

    /* --- Test 6: SAM register area --- */
    printf("\nSAM registers ($FFC0-$FFDF):\n");
    mem_init();

    TEST("SAM register $FFC0 reads $FF");
    CHECK(mem_read(0xFFC0) == 0xFF, "expected $FF");

    TEST("SAM register $FFDF reads $FF");
    CHECK(mem_read(0xFFDF) == 0xFF, "expected $FF");

    /* Writes to SAM should not crash (they're address-only) */
    mem_write(0xFFC0, 0x00);
    mem_write(0xFFDF, 0x00);
    TEST("SAM register writes don't crash");
    PASS();

    /* --- Test 7: ROM loading --- */
    printf("\nROM loading:\n");
    mem_init();

    /* Load d32.rom at offset $0000 (maps to $8000) and mirror */
    int rc = mem_load_rom("ROMS/d32.rom", 0x0000, 0x4000);
    TEST("Load d32.rom at offset $0000");
    CHECK(rc == 0, "load failed");
    mem_mirror_rom(0x4000, 0x0000, 0x4000);

    /* The reset vector should be at $FFFE/$FFFF in ROM */
    uint16_t reset_vec = ((uint16_t)mem_read(0xFFFE) << 8) | mem_read(0xFFFF);
    TEST("Reset vector from ROM is valid (non-zero)");
    CHECK(reset_vec != 0x0000, "expected non-zero reset vector");

    printf("  (Reset vector = $%04X)\n", reset_vec);

    /* Verify ROM data is readable in ROM area */
    TEST("ROM data at $C000 is non-zero after load");
    CHECK(mem_read(0xC000) != 0x00, "expected non-zero ROM data");

    /* --- Summary --- */
    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
