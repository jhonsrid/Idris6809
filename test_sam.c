#include "sam.h"
#include "memory.h"
#include <stdio.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-55s ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)

/* Global SAM instance for the write callback */
static SAM g_sam;

static void sam_write_cb(uint16_t addr)
{
    sam_write(&g_sam, addr);
}

int main(void)
{
    printf("=== SAM (MC6883) Tests ===\n\n");

    /* --- Test 1: Init defaults --- */
    printf("Initialization:\n");
    mem_init();
    sam_init(&g_sam);
    mem_register_sam(sam_write_cb);

    TEST("V bits default to 0");
    CHECK(sam_get_vdg_mode(&g_sam) == 0, "expected 0");

    TEST("F bits default to 0");
    CHECK(sam_get_display_addr(&g_sam) == 0x0000, "expected $0000");

    TEST("CPU rate defaults to 0 (normal)");
    CHECK(sam_get_cpu_rate(&g_sam) == 0, "expected 0");

    TEST("TY defaults to 0 (ROM mode)");
    CHECK(sam_get_ty(&g_sam) == false, "expected false");

    TEST("Memory is in ROM mode after init");
    CHECK(mem_get_rom_mode() == true, "expected ROM mode");

    /* --- Test 2: Bit set/clear via address writes --- */
    printf("\nBit set/clear mechanism:\n");

    /* Set V0 by writing to $FFC1 (odd = set) */
    mem_write(0xFFC1, 0x00);  /* data ignored */
    TEST("Write $FFC1 sets V0");
    CHECK(g_sam.v == 0x01, "expected V0=1");

    /* Clear V0 by writing to $FFC0 (even = clear) */
    mem_write(0xFFC0, 0x00);
    TEST("Write $FFC0 clears V0");
    CHECK(g_sam.v == 0x00, "expected V0=0");

    /* Set V1 */
    mem_write(0xFFC3, 0x00);
    TEST("Write $FFC3 sets V1");
    CHECK(g_sam.v == 0x02, "expected V=010");

    /* Set V2 */
    mem_write(0xFFC5, 0x00);
    TEST("Write $FFC5 sets V2");
    CHECK(g_sam.v == 0x06, "expected V=110");

    /* Clear V1 */
    mem_write(0xFFC2, 0x00);
    TEST("Write $FFC2 clears V1");
    CHECK(g_sam.v == 0x04, "expected V=100");

    /* --- Test 3: F register (display page) --- */
    printf("\nDisplay page (F register):\n");
    mem_init();
    sam_init(&g_sam);
    mem_register_sam(sam_write_cb);

    /* Set F1 to get display at $0400 (standard Dragon text) */
    /* F1 is bit 4, address pair $FFC8/$FFC9 */
    mem_write(0xFFC9, 0x00);  /* Set F1 */
    TEST("Set F1 -> display at $0400");
    CHECK(sam_get_display_addr(&g_sam) == 0x0400, "expected $0400");

    /* Set F0 as well -> F=0000011 -> $0200 + $0400 = $0600? No: F=3, addr = 3<<9 = $0600 */
    mem_write(0xFFC7, 0x00);  /* Set F0 */
    TEST("Set F0+F1 -> display at $0600");
    CHECK(sam_get_display_addr(&g_sam) == 0x0600, "expected $0600");

    /* Clear F0, set F2 -> F=0000110 -> addr = 6<<9 = $0C00 */
    mem_write(0xFFC6, 0x00);  /* Clear F0 */
    mem_write(0xFFCB, 0x00);  /* Set F2 */
    TEST("F=0000110 -> display at $0C00");
    CHECK(sam_get_display_addr(&g_sam) == 0x0C00, "expected $0C00");

    /* All F bits set: F=1111111 -> 127 << 9 = $FE00 */
    mem_init();
    sam_init(&g_sam);
    mem_register_sam(sam_write_cb);
    mem_write(0xFFC7, 0x00);  /* F0 */
    mem_write(0xFFC9, 0x00);  /* F1 */
    mem_write(0xFFCB, 0x00);  /* F2 */
    mem_write(0xFFCD, 0x00);  /* F3 */
    mem_write(0xFFCF, 0x00);  /* F4 */
    mem_write(0xFFD1, 0x00);  /* F5 */
    mem_write(0xFFD3, 0x00);  /* F6 */
    TEST("All F bits set -> display at $FE00");
    CHECK(sam_get_display_addr(&g_sam) == 0xFE00, "expected $FE00");

    /* --- Test 4: CPU rate --- */
    printf("\nCPU rate (R register):\n");
    mem_init();
    sam_init(&g_sam);
    mem_register_sam(sam_write_cb);

    TEST("Default rate is 0");
    CHECK(sam_get_cpu_rate(&g_sam) == 0, "expected 0");

    mem_write(0xFFD5, 0x00);  /* Set R0 */
    TEST("Set R0 -> rate 1");
    CHECK(sam_get_cpu_rate(&g_sam) == 1, "expected 1");

    mem_write(0xFFD7, 0x00);  /* Set R1 */
    TEST("Set R0+R1 -> rate 3");
    CHECK(sam_get_cpu_rate(&g_sam) == 3, "expected 3");

    mem_write(0xFFD4, 0x00);  /* Clear R0 */
    TEST("Clear R0, keep R1 -> rate 2");
    CHECK(sam_get_cpu_rate(&g_sam) == 2, "expected 2");

    mem_write(0xFFD6, 0x00);  /* Clear R1 */
    TEST("Clear R1 -> rate 0");
    CHECK(sam_get_cpu_rate(&g_sam) == 0, "expected 0");

    /* --- Test 5: TY bit (ROM/RAM mode) --- */
    printf("\nTY bit (ROM/RAM mode):\n");
    mem_init();
    sam_init(&g_sam);
    mem_register_sam(sam_write_cb);

    /* Write test pattern to RAM at $8000 */
    uint8_t *ram = mem_get_ram();
    ram[0x8000] = 0xAA;

    TEST("Boot: TY=0, ROM mode, $8000 reads ROM");
    CHECK(mem_read(0x8000) == 0x00, "expected ROM data ($00)");

    /* Set TY -> all-RAM mode */
    mem_write(0xFFDF, 0x00);
    TEST("Set TY=1 via $FFDF");
    CHECK(sam_get_ty(&g_sam) == true, "expected TY=1");

    TEST("All-RAM mode: $8000 reads RAM");
    CHECK(mem_read(0x8000) == 0xAA, "expected $AA from RAM");

    TEST("Memory reports RAM mode");
    CHECK(mem_get_rom_mode() == false, "expected RAM mode");

    /* Clear TY -> back to ROM mode */
    mem_write(0xFFDE, 0x00);
    TEST("Clear TY=0 via $FFDE");
    CHECK(sam_get_ty(&g_sam) == false, "expected TY=0");

    TEST("ROM mode restored: $8000 reads ROM");
    CHECK(mem_read(0x8000) == 0x00, "expected ROM data ($00)");

    TEST("Memory reports ROM mode");
    CHECK(mem_get_rom_mode() == true, "expected ROM mode");

    /* --- Test 6: P1 bit --- */
    printf("\nP1 bit (memory size):\n");
    mem_init();
    sam_init(&g_sam);
    mem_register_sam(sam_write_cb);

    TEST("P1 defaults to 0");
    CHECK(g_sam.p == 0, "expected 0");

    mem_write(0xFFD9, 0x00);  /* Set P1 */
    TEST("Set P1 via $FFD9");
    CHECK(g_sam.p == 1, "expected 1");

    mem_write(0xFFD8, 0x00);  /* Clear P1 */
    TEST("Clear P1 via $FFD8");
    CHECK(g_sam.p == 0, "expected 0");

    /* --- Test 7: Unused bits don't affect other fields --- */
    printf("\nUnused bits (13-14):\n");
    mem_init();
    sam_init(&g_sam);
    mem_register_sam(sam_write_cb);

    /* Set some bits first */
    mem_write(0xFFC1, 0x00);  /* V0 */
    mem_write(0xFFC9, 0x00);  /* F1 */

    /* Write to unused addresses */
    mem_write(0xFFDB, 0x00);  /* bit 13 set */
    mem_write(0xFFDD, 0x00);  /* bit 14 set */

    TEST("Unused writes don't affect V");
    CHECK(g_sam.v == 0x01, "expected V=001");

    TEST("Unused writes don't affect F");
    CHECK(g_sam.f == 0x02, "expected F=0000010");

    /* --- Test 8: Data written is truly ignored --- */
    printf("\nData-ignored verification:\n");
    mem_init();
    sam_init(&g_sam);
    mem_register_sam(sam_write_cb);

    mem_write(0xFFC1, 0xFF);  /* Set V0, data=$FF */
    TEST("Write $FF to $FFC1 still just sets V0");
    CHECK(g_sam.v == 0x01, "expected V=001");

    mem_write(0xFFC0, 0xAA);  /* Clear V0, data=$AA */
    TEST("Write $AA to $FFC0 still just clears V0");
    CHECK(g_sam.v == 0x00, "expected V=000");

    /* --- Test 9: Dragon-typical initialization sequence --- */
    printf("\nDragon-typical SAM setup (display at $0400):\n");
    mem_init();
    sam_init(&g_sam);
    mem_register_sam(sam_write_cb);

    /* The Dragon ROM typically sets F1 to put video at $0400,
     * clears all V bits (text mode), and leaves TY=0 (ROM mode). */
    mem_write(0xFFC0, 0x00);  /* Clear V0 */
    mem_write(0xFFC2, 0x00);  /* Clear V1 */
    mem_write(0xFFC4, 0x00);  /* Clear V2 */
    mem_write(0xFFC6, 0x00);  /* Clear F0 */
    mem_write(0xFFC9, 0x00);  /* Set F1 */
    mem_write(0xFFCA, 0x00);  /* Clear F2 */
    mem_write(0xFFCC, 0x00);  /* Clear F3 */
    mem_write(0xFFCE, 0x00);  /* Clear F4 */
    mem_write(0xFFD0, 0x00);  /* Clear F5 */
    mem_write(0xFFD2, 0x00);  /* Clear F6 */

    TEST("Dragon default: V=000 (text mode)");
    CHECK(sam_get_vdg_mode(&g_sam) == 0, "expected 0");

    TEST("Dragon default: display at $0400");
    CHECK(sam_get_display_addr(&g_sam) == 0x0400, "expected $0400");

    TEST("Dragon default: TY=0 (ROM mode)");
    CHECK(sam_get_ty(&g_sam) == false, "expected false");

    /* --- Summary --- */
    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
