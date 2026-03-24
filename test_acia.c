#include "acia.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-60s ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)

/* TX callback test state */
static uint8_t last_tx_byte;
static int tx_count;

static void test_tx_cb(uint8_t byte, void *ctx)
{
    (void)ctx;
    last_tx_byte = byte;
    tx_count++;
}

int main(void)
{
    ACIA acia;

    printf("=== ACIA (MC6551) Tests ===\n\n");

    /* --- 1: Init / reset state --- */
    printf("Initialization:\n");
    acia_init(&acia);

    TEST("Status: TX empty on reset");
    CHECK(acia_read(&acia, 1) & ACIA_SR_TDRE, "expected TDRE set");

    TEST("Status: RX not full on reset");
    CHECK(!(acia_read(&acia, 1) & ACIA_SR_RDRF), "expected RDRF clear");

    TEST("Status: no IRQ on reset");
    CHECK(!(acia_read(&acia, 1) & ACIA_SR_IRQ), "expected IRQ clear");

    TEST("Command register = $00 on reset");
    CHECK(acia_read(&acia, 2) == 0x00, "expected $00");

    TEST("Control register = $00 on reset");
    CHECK(acia_read(&acia, 3) == 0x00, "expected $00");

    TEST("No IRQ asserted on reset");
    CHECK(!acia_irq(&acia), "expected no IRQ");

    /* --- 2: Control/command register read/write --- */
    printf("\nRegister read/write:\n");
    acia_init(&acia);

    acia_write(&acia, 3, 0x1E);  /* Control: 9600 baud, 8N1 */
    TEST("Write control register $1E");
    CHECK(acia_read(&acia, 3) == 0x1E, "expected $1E");

    acia_write(&acia, 2, 0x0B);  /* Command: DTR on, RX IRQ enabled, TX off */
    TEST("Write command register $0B");
    CHECK(acia_read(&acia, 2) == 0x0B, "expected $0B");

    /* --- 3: TX data --- */
    printf("\nTX data:\n");
    acia_init(&acia);
    tx_count = 0;
    last_tx_byte = 0;
    acia_set_tx_callback(&acia, test_tx_cb, NULL);

    TEST("TX empty before write");
    CHECK(acia_read(&acia, 1) & ACIA_SR_TDRE, "expected TDRE");

    acia_write(&acia, 0, 0x41);  /* TX 'A' */
    TEST("TX callback called");
    CHECK(tx_count == 1, "expected 1 call");

    TEST("TX callback received correct byte");
    CHECK(last_tx_byte == 0x41, "expected $41");

    TEST("TX empty after write (instant completion)");
    CHECK(acia_read(&acia, 1) & ACIA_SR_TDRE, "expected TDRE");

    /* Multiple transmissions */
    acia_write(&acia, 0, 0x42);
    acia_write(&acia, 0, 0x43);
    TEST("Multiple TX: callback count = 3");
    CHECK(tx_count == 3, "expected 3");
    TEST("Last TX byte = $43");
    CHECK(last_tx_byte == 0x43, "expected $43");

    /* --- 4: RX data --- */
    printf("\nRX data:\n");
    acia_init(&acia);

    TEST("RX not full initially");
    CHECK(!(acia_read(&acia, 1) & ACIA_SR_RDRF), "expected RDRF clear");

    bool accepted = acia_rx_byte(&acia, 0x48);  /* Receive 'H' */
    TEST("RX byte accepted");
    CHECK(accepted, "expected true");

    TEST("RDRF set after receive");
    CHECK(acia_read(&acia, 1) & ACIA_SR_RDRF, "expected RDRF set");

    TEST("Read RX data returns $48");
    CHECK(acia_read(&acia, 0) == 0x48, "expected $48");

    TEST("RDRF cleared after reading data");
    CHECK(!(acia_read(&acia, 1) & ACIA_SR_RDRF), "expected RDRF clear");

    /* --- 5: RX overrun --- */
    printf("\nRX overrun:\n");
    acia_init(&acia);

    acia_rx_byte(&acia, 0x41);  /* First byte */
    accepted = acia_rx_byte(&acia, 0x42);  /* Second byte without reading first */
    TEST("Second RX byte rejected (overrun)");
    CHECK(!accepted, "expected false");

    TEST("Overrun flag set in status");
    CHECK(acia_read(&acia, 1) & ACIA_SR_OVRN, "expected OVRN set");

    TEST("Original data still $41");
    CHECK(acia_read(&acia, 0) == 0x41, "expected $41");

    TEST("Overrun cleared after reading data");
    CHECK(!(acia_read(&acia, 1) & ACIA_SR_OVRN), "expected OVRN clear");

    /* --- 6: RX IRQ --- */
    printf("\nRX IRQ:\n");
    acia_init(&acia);

    /* Enable RX IRQ: command bit 1 = 0 (active low enable) */
    acia_write(&acia, 2, 0x01);  /* DTR on, RX IRQ enabled (bit 1 = 0) */

    TEST("No IRQ before RX");
    CHECK(!acia_irq(&acia), "expected no IRQ");

    acia_rx_byte(&acia, 0x55);
    TEST("IRQ asserted after RX with IRQ enabled");
    CHECK(acia_irq(&acia), "expected IRQ");

    TEST("IRQ flag in status register");
    CHECK(acia_read(&acia, 1) & ACIA_SR_IRQ, "expected IRQ flag");

    /* Read data clears IRQ */
    acia_read(&acia, 0);
    TEST("IRQ cleared after reading RX data");
    CHECK(!acia_irq(&acia), "expected no IRQ");

    /* Disable RX IRQ: command bit 1 = 1 */
    acia_write(&acia, 2, 0x03);  /* DTR on, RX IRQ disabled */
    acia_rx_byte(&acia, 0x66);
    TEST("No IRQ when RX IRQ disabled");
    CHECK(!acia_irq(&acia), "expected no IRQ");

    /* --- 7: Programmed reset --- */
    printf("\nProgrammed reset:\n");
    acia_init(&acia);

    /* Set some state */
    acia_write(&acia, 2, 0x1F);
    acia_write(&acia, 3, 0xAB);
    acia_rx_byte(&acia, 0x99);

    /* Programmed reset: write any value to register 1 */
    acia_write(&acia, 1, 0x00);

    TEST("Command cleared after programmed reset");
    CHECK(acia_read(&acia, 2) == 0x00, "expected $00");

    TEST("Control cleared after programmed reset");
    CHECK(acia_read(&acia, 3) == 0x00, "expected $00");

    TEST("RDRF cleared after programmed reset");
    CHECK(!(acia_read(&acia, 1) & ACIA_SR_RDRF), "expected clear");

    TEST("TDRE set after programmed reset");
    CHECK(acia_read(&acia, 1) & ACIA_SR_TDRE, "expected set");

    /* --- 8: TX without callback (no crash) --- */
    printf("\nTX without callback:\n");
    acia_init(&acia);

    acia_write(&acia, 0, 0xFF);
    TEST("TX with no callback doesn't crash");
    PASS();

    TEST("TX still shows empty");
    CHECK(acia_read(&acia, 1) & ACIA_SR_TDRE, "expected TDRE");

    /* --- 9: Integration via memory-mapped I/O --- */
    printf("\nMemory-mapped I/O (via Dragon):\n");
    /* This requires dragon.c wiring — test that reading $FF05 returns
     * the status register with TDRE set. We test this indirectly
     * by verifying the ACIA struct state matches expectations. */
    acia_init(&acia);
    uint8_t sr = acia_read(&acia, 1);
    TEST("Status via acia_read(1) = $10 (TDRE only)");
    CHECK(sr == 0x10, "expected $10");

    /* --- Summary --- */
    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
