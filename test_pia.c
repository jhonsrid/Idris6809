#include "pia.h"
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
    PIA pia;

    printf("=== PIA (MC6821) Tests ===\n\n");

    /* --- 1: Init / reset --- */
    printf("Initialization:\n");
    pia_init(&pia);

    TEST("DDR A defaults to $00 (all input)");
    CHECK(pia.ddra == 0x00, "expected $00");

    TEST("DDR B defaults to $00");
    CHECK(pia.ddrb == 0x00, "expected $00");

    TEST("CRA defaults to $00");
    CHECK(pia.cra == 0x00, "expected $00");

    TEST("CRB defaults to $00");
    CHECK(pia.crb == 0x00, "expected $00");

    TEST("Input A defaults to $FF (float high)");
    CHECK(pia.ira == 0xFF, "expected $FF");

    /* --- 2: DDR / Data register selection via CR bit 2 --- */
    printf("\nDDR/Data register selection:\n");
    pia_init(&pia);

    /* CR bit 2 = 0 -> access DDR */
    pia_write(&pia, 1, 0x00);    /* CRA: bit 2 = 0 -> DDR mode */
    pia_write(&pia, 0, 0xF0);    /* Write to DDR A */
    TEST("Write DDR A = $F0 when CR bit 2 = 0");
    CHECK(pia.ddra == 0xF0, "expected $F0");

    /* Read DDR back */
    TEST("Read DDR A returns $F0");
    CHECK(pia_read(&pia, 0) == 0xF0, "expected $F0");

    /* Switch to data register */
    pia_write(&pia, 1, 0x04);    /* CRA: bit 2 = 1 -> Data mode */
    pia_write(&pia, 0, 0xAA);    /* Write to Output Register A */
    TEST("Write ORA = $AA when CR bit 2 = 1");
    CHECK(pia.ora == 0xAA, "expected $AA");

    /* Reading data register: output bits from ORA, input bits from IRA */
    /* DDR=$F0: bits 7-4 output, bits 3-0 input */
    /* ORA=$AA = 10101010, IRA=$FF */
    /* Expected: (AA & F0) | (FF & 0F) = A0 | 0F = $AF */
    TEST("Read Data A: output bits from ORA, input from IRA");
    CHECK(pia_read(&pia, 0) == 0xAF, "expected $AF");

    /* --- 3: Same for Port B --- */
    printf("\nPort B DDR/Data:\n");
    pia_init(&pia);

    pia_write(&pia, 3, 0x00);    /* CRB bit 2 = 0 -> DDR */
    pia_write(&pia, 2, 0xFF);    /* DDR B = all output */
    TEST("Write DDR B = $FF");
    CHECK(pia.ddrb == 0xFF, "expected $FF");

    pia_write(&pia, 3, 0x04);    /* CRB bit 2 = 1 -> Data */
    pia_write(&pia, 2, 0x55);
    TEST("Write ORB = $55");
    CHECK(pia.orb == 0x55, "expected $55");

    /* Port B all output: read returns ORB */
    TEST("Read Data B (all output) returns ORB");
    CHECK(pia_read(&pia, 2) == 0x55, "expected $55");

    /* --- 4: Control register read/write --- */
    printf("\nControl register:\n");
    pia_init(&pia);

    pia_write(&pia, 1, 0x3F);    /* Write all writable bits */
    TEST("CRA write: bits 5-0 written, bits 7-6 preserved");
    CHECK(pia.cra == 0x3F, "expected $3F");

    /* Bits 7-6 should not be writable */
    pia.cra |= 0x80;  /* Simulate IRQ1 flag set */
    pia_write(&pia, 1, 0x04);
    TEST("CRA write preserves IRQ flag bits 7-6");
    CHECK((pia.cra & 0x80) != 0, "expected bit 7 preserved");
    CHECK((pia.cra & 0x3F) == 0x04, "expected bits 5-0 = $04");

    TEST("Read CRA returns full byte including flags");
    CHECK(pia_read(&pia, 1) == 0x84, "expected $84");

    /* --- 5: CA1 interrupt (FSYNC / HSYNC) --- */
    printf("\nCA1 interrupt:\n");
    pia_init(&pia);

    /* Configure CRA: falling edge (bit1=0), IRQ enabled (bit0=1), data mode (bit2=1) */
    pia_write(&pia, 1, 0x05);  /* 00000101 */

    TEST("CA1 falling edge: low transition sets IRQ1 flag");
    bool irq = pia_set_ca1(&pia, false);  /* falling edge */
    CHECK(pia.cra & PIA_CR_IRQ1_FLAG, "expected IRQ1 flag set");
    CHECK(irq, "expected IRQ signalled");

    /* Rising edge should NOT set flag when configured for falling */
    pia.cra &= ~PIA_CR_IRQ1_FLAG;  /* Clear flag */
    irq = pia_set_ca1(&pia, true);
    TEST("CA1 rising edge: no effect when configured for falling");
    CHECK(!(pia.cra & PIA_CR_IRQ1_FLAG), "expected no flag");
    CHECK(!irq, "expected no IRQ");

    /* Change to rising edge */
    pia_write(&pia, 1, 0x07);  /* bit1=1 (rising), bit0=1 (enable), bit2=1 */
    irq = pia_set_ca1(&pia, true);
    TEST("CA1 rising edge: high transition sets IRQ1 flag");
    CHECK(pia.cra & PIA_CR_IRQ1_FLAG, "expected IRQ1 flag set");
    CHECK(irq, "expected IRQ signalled");

    /* --- 6: CB1 interrupt (FSYNC on PIA0) --- */
    printf("\nCB1 interrupt:\n");
    pia_init(&pia);

    /* Configure CRB: falling edge, IRQ enabled, data mode */
    pia_write(&pia, 3, 0x05);

    bool firq = pia_set_cb1(&pia, false);
    TEST("CB1 falling edge: sets IRQ1 flag in CRB");
    CHECK(pia.crb & PIA_CR_IRQ1_FLAG, "expected IRQ1 flag");
    CHECK(firq, "expected IRQ");

    /* --- 7: IRQ flag cleared on data register read --- */
    printf("\nIRQ flag clearing:\n");
    pia_init(&pia);

    /* Set up Port A: DDR=$00 (all input), CR=data mode + falling + enable */
    pia_write(&pia, 1, 0x05);
    pia_set_ca1(&pia, false);  /* Set IRQ1 flag */
    TEST("IRQ1 flag set before read");
    CHECK(pia.cra & PIA_CR_IRQ1_FLAG, "expected flag set");

    /* Read data register A -> should clear both IRQ flags */
    pia_read(&pia, 0);
    TEST("IRQ1 flag cleared after reading data register A");
    CHECK(!(pia.cra & PIA_CR_IRQ1_FLAG), "expected flag cleared");

    /* Same for Port B */
    pia_write(&pia, 3, 0x05);
    pia_set_cb1(&pia, false);
    pia_read(&pia, 2);
    TEST("IRQ1 flag cleared after reading data register B");
    CHECK(!(pia.crb & PIA_CR_IRQ1_FLAG), "expected flag cleared");

    /* --- 8: IRQ disabled (bit 0 = 0) --- */
    printf("\nIRQ enable/disable:\n");
    pia_init(&pia);

    /* Configure: falling edge, IRQ DISABLED, data mode */
    pia_write(&pia, 1, 0x04);  /* bit0=0 (disabled) */
    irq = pia_set_ca1(&pia, false);
    TEST("CA1 with IRQ disabled: flag set but no IRQ output");
    CHECK(pia.cra & PIA_CR_IRQ1_FLAG, "expected flag set");
    CHECK(!irq, "expected no IRQ signalled");

    TEST("pia_irq_a() returns false when IRQ disabled");
    CHECK(!pia_irq_a(&pia), "expected false");

    /* Enable IRQ */
    pia_write(&pia, 1, 0x05);
    TEST("pia_irq_a() returns true after enabling IRQ");
    CHECK(pia_irq_a(&pia), "expected true");

    /* --- 9: IRQ output lines (pia_irq_a / pia_irq_b) --- */
    printf("\nIRQ output lines:\n");
    pia_init(&pia);

    TEST("No IRQ initially");
    CHECK(!pia_irq_a(&pia), "expected no IRQA");
    CHECK(!pia_irq_b(&pia), "expected no IRQB");

    /* Trigger CA1 with IRQ enabled */
    pia_write(&pia, 1, 0x05);
    pia_set_ca1(&pia, false);
    TEST("IRQA active after CA1 trigger");
    CHECK(pia_irq_a(&pia), "expected IRQA");
    TEST("IRQB not affected by CA1");
    CHECK(!pia_irq_b(&pia), "expected no IRQB");

    /* Trigger CB1 with IRQ enabled */
    pia_write(&pia, 3, 0x05);
    pia_set_cb1(&pia, false);
    TEST("IRQB active after CB1 trigger");
    CHECK(pia_irq_b(&pia), "expected IRQB");

    /* --- 10: get_output / set_input --- */
    printf("\nOutput/Input helpers:\n");
    pia_init(&pia);

    /* Set DDR A to all output, write $42 */
    pia_write(&pia, 1, 0x00);    /* DDR mode */
    pia_write(&pia, 0, 0xFF);    /* DDR = all output */
    pia_write(&pia, 1, 0x04);    /* Data mode */
    pia_write(&pia, 0, 0x42);    /* ORA = $42 */

    TEST("get_output_a returns driven output bits");
    CHECK(pia_get_output_a(&pia) == 0x42, "expected $42");

    /* With partial DDR: only output bits in result */
    pia_write(&pia, 1, 0x00);
    pia_write(&pia, 0, 0xF0);    /* DDR = upper 4 output */
    pia_write(&pia, 1, 0x04);
    pia_write(&pia, 0, 0xAB);    /* ORA = $AB */
    TEST("get_output_a with partial DDR: only output bits");
    CHECK(pia_get_output_a(&pia) == 0xA0, "expected $A0 (AB & F0)");

    /* Set input */
    pia_set_input_a(&pia, 0x55);
    TEST("set_input_a sets IRA");
    CHECK(pia.ira == 0x55, "expected $55");

    /* Read data: output bits from ORA, input bits from IRA */
    /* DDR=$F0, ORA=$AB, IRA=$55 -> (AB & F0) | (55 & 0F) = A0 | 05 = $A5 */
    TEST("Read data A: mixed output/input");
    CHECK(pia_read(&pia, 0) == 0xA5, "expected $A5");

    /* --- 11: Dragon keyboard scanning simulation --- */
    printf("\nDragon keyboard scan simulation:\n");
    pia_init(&pia);

    /* PIA0 setup like Dragon ROM does:
     * Port A: all input (DDR=0x00) - keyboard rows (active low)
     * Port B: all output (DDR=0xFF) - keyboard column select
     * CRA: data mode, falling CA1, IRQ enabled ($05)
     * CRB: data mode, falling CB1, IRQ enabled ($05)
     */

    /* Set DDR A = $00 (input) */
    pia_write(&pia, 1, 0x00);  /* CRA: DDR mode */
    pia_write(&pia, 0, 0x00);  /* DDR A = all input */
    pia_write(&pia, 1, 0x05);  /* CRA: data mode + falling + enable */

    /* Set DDR B = $FF (output) */
    pia_write(&pia, 3, 0x00);  /* CRB: DDR mode */
    pia_write(&pia, 2, 0xFF);  /* DDR B = all output */
    pia_write(&pia, 3, 0x05);  /* CRB: data mode + falling + enable */

    /* Select row 0: write $FE to port B (bit 0 low) */
    pia_write(&pia, 2, 0xFE);
    TEST("Keyboard row select: ORB = $FE");
    CHECK(pia_get_output_b(&pia) == 0xFE, "expected $FE");

    /* Simulate no keys pressed: all column inputs high */
    pia_set_input_a(&pia, 0xFF);
    TEST("No keys: column reads $FF");
    uint8_t cols = pia_read(&pia, 0);
    CHECK(cols == 0xFF, "expected $FF");

    /* Simulate key '3' pressed (row 0, col 3): bit 3 goes low */
    pia_set_input_a(&pia, 0xF7);  /* bit 3 = 0 */
    cols = pia_read(&pia, 0);
    TEST("Key '3' pressed: column reads $F7");
    CHECK(cols == 0xF7, "expected $F7");

    /* Select row 2, no key -> $FF */
    pia_write(&pia, 2, 0xFB);  /* bit 2 low */
    pia_set_input_a(&pia, 0xFF);
    cols = pia_read(&pia, 0);
    TEST("Row 2, no key: column reads $FF");
    CHECK(cols == 0xFF, "expected $FF");

    /* --- 12: FSYNC interrupt (CB1 on PIA0) --- */
    printf("\nFSYNC (60 Hz) interrupt simulation:\n");
    pia_init(&pia);

    /* Dragon sets up PIA0 CRB for falling-edge CB1 with IRQ enabled */
    pia_write(&pia, 3, 0x00);
    pia_write(&pia, 2, 0xFF);
    pia_write(&pia, 3, 0x05);   /* falling, enable, data mode */

    /* Simulate FSYNC pulse: goes low */
    bool triggered = pia_set_cb1(&pia, false);
    TEST("FSYNC falling: IRQ1B flag set, IRQ signalled");
    CHECK(triggered, "expected IRQ");
    CHECK(pia.crb & PIA_CR_IRQ1_FLAG, "expected flag set");
    CHECK(pia_irq_b(&pia), "expected IRQB active");

    /* CPU reads data register B to acknowledge interrupt */
    pia_read(&pia, 2);
    TEST("Read data B clears IRQ flag");
    CHECK(!(pia.crb & PIA_CR_IRQ1_FLAG), "expected flag cleared");
    CHECK(!pia_irq_b(&pia), "expected IRQB inactive");

    /* --- 13: Dragon interrupt routing --- */
    printf("\nDragon interrupt routing:\n");

    PIA pia0, pia1;
    pia_init(&pia0);
    pia_init(&pia1);

    /* Set up PIA0: CA1 falling+enable, CB1 falling+enable */
    pia_write(&pia0, 1, 0x05);
    pia_write(&pia0, 3, 0x05);

    /* Set up PIA1: CA1 falling+enable, CB1 falling+enable */
    pia_write(&pia1, 1, 0x05);
    pia_write(&pia1, 3, 0x05);

    /* CPU IRQ = PIA0_IRQA | PIA0_IRQB | PIA1_IRQA */
    /* CPU FIRQ = PIA1_IRQB */

    TEST("No interrupts initially");
    bool cpu_irq = pia_irq_a(&pia0) || pia_irq_b(&pia0) || pia_irq_a(&pia1);
    bool cpu_firq = pia_irq_b(&pia1);
    CHECK(!cpu_irq && !cpu_firq, "expected no IRQ/FIRQ");

    /* FSYNC triggers PIA0 CB1 -> IRQ */
    pia_set_cb1(&pia0, false);
    cpu_irq = pia_irq_a(&pia0) || pia_irq_b(&pia0) || pia_irq_a(&pia1);
    cpu_firq = pia_irq_b(&pia1);
    TEST("FSYNC -> PIA0 CB1 -> CPU IRQ (not FIRQ)");
    CHECK(cpu_irq, "expected IRQ");
    CHECK(!cpu_firq, "expected no FIRQ");

    /* Clear it */
    pia_read(&pia0, 2);

    /* CART triggers PIA1 CB1 -> FIRQ */
    pia_set_cb1(&pia1, false);
    cpu_irq = pia_irq_a(&pia0) || pia_irq_b(&pia0) || pia_irq_a(&pia1);
    cpu_firq = pia_irq_b(&pia1);
    TEST("CART -> PIA1 CB1 -> CPU FIRQ (not IRQ)");
    CHECK(!cpu_irq, "expected no IRQ");
    CHECK(cpu_firq, "expected FIRQ");

    /* HSYNC triggers PIA0 CA1 -> IRQ */
    pia_read(&pia1, 2);  /* clear FIRQ */
    pia_set_ca1(&pia0, false);
    cpu_irq = pia_irq_a(&pia0) || pia_irq_b(&pia0) || pia_irq_a(&pia1);
    cpu_firq = pia_irq_b(&pia1);
    TEST("HSYNC -> PIA0 CA1 -> CPU IRQ");
    CHECK(cpu_irq, "expected IRQ");
    CHECK(!cpu_firq, "expected no FIRQ");

    /* --- 14: VDG mode output from PIA1 port B --- */
    printf("\nPIA1 port B -> VDG mode:\n");
    pia_init(&pia1);

    /* DDR B = $F8 (bits 7-3 output, bits 2-0 input) */
    pia_write(&pia1, 3, 0x00);
    pia_write(&pia1, 2, 0xF8);
    pia_write(&pia1, 3, 0x04);  /* data mode */

    /* Write VDG mode: A/G=1, GM=111, CSS=1 -> $F8 */
    pia_write(&pia1, 2, 0xF8);
    TEST("PIA1 port B output $F8 for VDG mode");
    CHECK(pia_get_output_b(&pia1) == 0xF8, "expected $F8");

    /* With bit 2 input (RAM size): set input bit 2 = 0 (64K present) */
    pia_set_input_b(&pia1, 0xFB);  /* bit 2 = 0 */
    uint8_t pb = pia_read(&pia1, 2);
    /* Output bits (F8 & F8) | input bits (FB & 07) = F8 | 03 = $FB */
    TEST("PIA1 port B read: output + input bits combined");
    CHECK(pb == 0xFB, "expected $FB");

    /* --- Summary --- */
    printf("\n=== PIA (MC6821) Tests: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
