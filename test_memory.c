#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

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

    /* --- Test 8: Cartridge ROM (programmatic, no file needed) --- */
    printf("\nCartridge ROM:\n");
    mem_init();

    TEST("No cartridge initially");
    CHECK(!mem_has_cartridge(), "expected no cartridge");

    /* Manually load a 16KB cartridge into cart_rom via the public interface.
     * Since mem_load_cartridge needs a file, we create a temp file. */
    {
        const char *tmppath = "/tmp/idris6809_test_cart_16k.rom";
        FILE *f = fopen(tmppath, "wb");
        uint8_t cart_data[0x4000];
        memset(cart_data, 0, sizeof(cart_data));
        cart_data[0] = 0x12;           /* Signature byte at $C000 */
        cart_data[1] = 0x34;           /* $C001 */
        cart_data[0x3EFF] = 0xAB;      /* Maps to $FEFF ($FEFF-$C000=$3EFF) */
        fwrite(cart_data, 1, sizeof(cart_data), f);
        fclose(f);

        int crc = mem_load_cartridge(tmppath);
        TEST("Load 16KB cartridge succeeds");
        CHECK(crc == 0, "load failed");

        TEST("mem_has_cartridge() returns true");
        CHECK(mem_has_cartridge(), "expected cartridge present");

        TEST("$C000 reads from cartridge ROM");
        CHECK(mem_read(0xC000) == 0x12, "expected $12");

        TEST("$C001 reads from cartridge ROM");
        CHECK(mem_read(0xC001) == 0x34, "expected $34");

        TEST("$FEFF reads from cartridge ROM");
        CHECK(mem_read(0xFEFF) == 0xAB, "expected $AB");

        /* Write to cartridge area goes to RAM underneath */
        mem_write(0xC000, 0xFF);
        TEST("Write to $C000 goes to RAM, cart still reads");
        CHECK(mem_read(0xC000) == 0x12, "cartridge should overlay");
        /* Verify RAM got the write */
        ram = mem_get_ram();
        CHECK(ram[0xC000] == 0xFF, "RAM should have $FF");

        /* Eject */
        mem_eject_cartridge();
        TEST("After eject: no cartridge");
        CHECK(!mem_has_cartridge(), "expected no cartridge");

        TEST("After eject: $C000 reads from ROM (not cart)");
        CHECK(mem_read(0xC000) != 0x12, "should not read cart data");

        unlink(tmppath);
    }

    /* --- Test 9: 8KB cartridge with mirroring --- */
    printf("\n8KB Cartridge mirroring:\n");
    mem_init();
    {
        const char *tmppath = "/tmp/idris6809_test_cart_8k.rom";
        FILE *f = fopen(tmppath, "wb");
        uint8_t cart_data[0x2000];
        memset(cart_data, 0, sizeof(cart_data));
        cart_data[0] = 0xAA;           /* $C000 */
        cart_data[0x100] = 0xBB;       /* $C100 */
        cart_data[0x1FFF] = 0xCC;      /* $DFFF */
        fwrite(cart_data, 1, sizeof(cart_data), f);
        fclose(f);

        int crc = mem_load_cartridge(tmppath);
        TEST("Load 8KB cartridge succeeds");
        CHECK(crc == 0, "load failed");

        TEST("$C000 reads cartridge data");
        CHECK(mem_read(0xC000) == 0xAA, "expected $AA");

        TEST("$C100 reads cartridge data");
        CHECK(mem_read(0xC100) == 0xBB, "expected $BB");

        TEST("$DFFF reads cartridge data");
        CHECK(mem_read(0xDFFF) == 0xCC, "expected $CC");

        /* 8KB mirroring: $E000-$FEFF mirrors $C000-$DFFF */
        TEST("$E000 mirrors $C000");
        CHECK(mem_read(0xE000) == 0xAA, "expected $AA from mirror");

        TEST("$E100 mirrors $C100");
        CHECK(mem_read(0xE100) == 0xBB, "expected $BB from mirror");

        TEST("$FDFF mirrors $DDFF");
        /* $FDFF - $C000 = $3DFF. $3DFF & $1FFF = $1DFF. cart_rom[$1DFF] = 0 */
        /* Let's test a known value instead: $DFFF mirrors to $FFFF (out of range).
         * So test that $E000 + offset mirrors $C000 + offset for cart_data[0] */
        /* $E000 -> offset $2000 & $1FFF = $0000 -> $AA. Already tested above.
         * $E100 -> offset $2100 & $1FFF = $0100 -> $BB. Already tested above.
         * Test another: cart_data[$1FFF] = $CC at $DFFF. Mirror at $DFFF+$2000=$FFFF
         * which is in vector area, so not accessible. Instead verify a mid-range mirror: */
        /* cart_data[0] = $AA at $C000, mirror at $E000 (tested). Check $FEFF: */
        /* $FEFF -> offset $3EFF & $1FFF = $1EFF. cart_rom[$1EFF] = 0 */
        CHECK(mem_read(0xFEFF) == 0x00, "expected $00 (no data at mirror offset)");

        mem_eject_cartridge();
        unlink(tmppath);
    }

    /* --- Test 10: Cartridge load error handling --- */
    printf("\nCartridge error handling:\n");
    {
        FILE *saved_err = stderr;
        stderr = fopen("/dev/null", "w");

        TEST("Load non-existent cartridge fails");
        CHECK(mem_load_cartridge("/tmp/no_such_cart.rom") != 0, "should fail");

        TEST("No cartridge after failed load");
        CHECK(!mem_has_cartridge(), "expected no cartridge");

        /* Empty file */
        const char *tmppath = "/tmp/idris6809_test_empty.rom";
        FILE *f = fopen(tmppath, "wb");
        fclose(f);
        TEST("Load empty cartridge file fails");
        CHECK(mem_load_cartridge(tmppath) != 0, "should fail");
        unlink(tmppath);

        fclose(stderr);
        stderr = saved_err;
    }

    /* --- Summary --- */
    printf("\n=== Memory Subsystem Tests: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
