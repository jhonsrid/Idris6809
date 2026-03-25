#include "cassette.h"
#include "pia.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass_count, fail_count;
static const char *current_test;

#define TEST(name) do { current_test = (name); printf("  %s", name); } while(0)
#define CHECK(cond, msg) do { \
    if (cond) { printf("%*sPASS\n", (int)(60 - strlen(current_test)), ""); pass_count++; } \
    else { printf("%*sFAIL: %s\n", (int)(60 - strlen(current_test)), "", msg); fail_count++; } \
} while(0)

/* Helper: load test data directly into cassette struct */
static void load_test_data(Cassette *cas, const uint8_t *data, size_t size)
{
    cassette_eject(cas);
    cas->data = malloc(size);
    memcpy(cas->data, data, size);
    cas->data_size = size;
    cassette_rewind(cas);
}

int main(void)
{
    printf("=== Cassette Tests ===\n");
    Cassette cas;
    PIA pia;

    /* --- 1: Initialization --- */
    printf("\nInitialization:\n");
    cassette_init(&cas);

    TEST("Not playing after init");
    CHECK(!cas.playing, "expected not playing");

    TEST("Motor off after init");
    CHECK(!cas.motor_on, "expected motor off");

    TEST("No data after init");
    CHECK(cas.data == NULL, "expected NULL");

    TEST("Signal low after init");
    CHECK(!cas.signal_level, "expected low");

    /* --- 2: Load and rewind --- */
    printf("\nLoad and rewind:\n");
    uint8_t test_data[] = { 0x55, 0x3C, 0x00 };
    load_test_data(&cas, test_data, sizeof(test_data));

    TEST("Data loaded");
    CHECK(cas.data != NULL, "expected non-NULL");

    TEST("Size correct");
    CHECK(cassette_get_size(&cas) == 3, "expected 3");

    TEST("Position at start");
    CHECK(cassette_get_position(&cas) == 0, "expected 0");

    TEST("Playing after load");
    CHECK(cas.playing, "expected playing");

    TEST("Bit pos at LSB");
    CHECK(cas.bit_pos == 0, "expected 0");

    /* --- 3: Motor control --- */
    printf("\nMotor control:\n");
    cassette_set_motor(&cas, true);
    TEST("Motor on");
    CHECK(cas.motor_on, "expected on");

    cassette_set_motor(&cas, false);
    TEST("Motor off");
    CHECK(!cas.motor_on, "expected off");

    /* No advancement when motor off */
    bool level = cassette_update(&cas, 10000);
    TEST("No advance with motor off");
    CHECK(cassette_get_position(&cas) == 0, "expected pos 0");
    CHECK(level, "expected high signal (initial state)");

    /* --- 4: Bit 1 waveform (0xFF = all ones) --- */
    printf("\nBit 1 waveform (0xFF byte):\n");
    uint8_t all_ones[] = { 0xFF };
    load_test_data(&cas, all_ones, 1);
    cassette_set_motor(&cas, true);

    /* Bit 1: 1 cycle at 2400 Hz = 2 half-cycles of 185 CPU cycles */
    TEST("Signal starts high");
    CHECK(cas.signal_level, "expected high");

    cassette_update(&cas, 218);
    TEST("Toggles low after first half-cycle");
    CHECK(!cas.signal_level, "expected low");

    cassette_update(&cas, 218);
    TEST("Toggles high and advances after second half-cycle");
    CHECK(cas.signal_level, "expected high");
    CHECK(cas.bit_pos == 1, "expected bit_pos 1");

    /* --- 5: Bit 0 waveform (0x00 = all zeros) --- */
    printf("\nBit 0 waveform (0x00 byte):\n");
    uint8_t all_zeros[] = { 0x00 };
    load_test_data(&cas, all_zeros, 1);
    cassette_set_motor(&cas, true);

    /* Bit 0: 1200 Hz, 2 half-cycles of 370 CPU cycles each */
    cassette_update(&cas, 407);
    TEST("Toggles after 1200 Hz half-cycle");
    CHECK(!cas.signal_level, "expected low");

    cassette_update(&cas, 407);
    TEST("Completes one bit-0 cycle");
    CHECK(cas.signal_level, "expected high");
    CHECK(cas.bit_pos == 1, "expected bit_pos 1");

    /* --- 6: Full byte 0xFF timing --- */
    printf("\nFull byte timing:\n");
    load_test_data(&cas, all_ones, 1);
    cassette_set_motor(&cas, true);

    /* 8 bits * 2 half-cycles * 185 = 2960 cycles for a full 0xFF byte */
    for (int i = 0; i < 8; i++) {
        cassette_update(&cas, 218);
        cassette_update(&cas, 218);
    }
    TEST("End of tape after full byte");
    CHECK(!cas.playing, "expected not playing");

    /* --- 7: Leader byte $55 (01010101) --- */
    printf("\nLeader byte $55:\n");
    uint8_t leader[] = { 0x55 };
    load_test_data(&cas, leader, 1);
    cassette_set_motor(&cas, true);

    /* $55 = 01010101: alternating bit0/bit1. Signal starts HIGH. */
    /* Bit 7 = 0: 1200 Hz half-cycles */
    cassette_update(&cas, 407);
    TEST("Bit 7 (0): toggles low at 1200 Hz rate");
    CHECK(!cas.signal_level, "expected low");

    cassette_update(&cas, 407);
    /* Bit 6 = 1: 2400 Hz half-cycles */
    cassette_update(&cas, 218);
    TEST("Bit 6 (1): toggles low at 2400 Hz rate");
    CHECK(!cas.signal_level, "expected low");

    /* --- 8: PIA1 PA7 update --- */
    printf("\nPIA1 PA7 integration:\n");
    load_test_data(&cas, all_ones, 1);
    cassette_set_motor(&cas, true);
    pia_init(&pia);
    pia.ira = 0x55;  /* Set some bits to verify isolation */

    /* Manually update PA0 like dragon.c does */
    bool sig = cassette_update(&cas, 218);
    if (sig) pia.ira |= 0x01; else pia.ira &= 0xFE;

    TEST("PA0 cleared after first half-cycle");
    CHECK(pia.ira == 0x54, "expected $54 (0x55 & ~0x01)");

    sig = cassette_update(&cas, 218);
    if (sig) pia.ira |= 0x01; else pia.ira &= 0xFE;

    TEST("PA0 set after second half-cycle");
    CHECK(pia.ira == 0x55, "expected $55");

    /* --- 9: Eject --- */
    printf("\nEject:\n");
    cassette_eject(&cas);

    TEST("Data freed after eject");
    CHECK(cas.data == NULL, "expected NULL");

    TEST("Not playing after eject");
    CHECK(!cas.playing, "expected not playing");

    TEST("Size zero after eject");
    CHECK(cassette_get_size(&cas) == 0, "expected 0");

    /* --- 10: Rewind --- */
    printf("\nRewind:\n");
    load_test_data(&cas, test_data, sizeof(test_data));
    cassette_set_motor(&cas, true);

    /* Advance partway — test_data starts with $55 which has mixed bit types.
     * Run enough cycles to guarantee at least one full byte advances.
     * Worst case: all bit-0s at 1200 Hz = 8 bits * 2 * 370 = 5920 cycles. */
    cassette_update(&cas, 10000);

    TEST("Position advanced");
    CHECK(cassette_get_position(&cas) > 0, "expected > 0");

    cassette_rewind(&cas);
    TEST("Position reset after rewind");
    CHECK(cassette_get_position(&cas) == 0, "expected 0");

    TEST("Playing after rewind");
    CHECK(cas.playing, "expected playing");

    cassette_eject(&cas);

    printf("\n=== Cassette Tests: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
