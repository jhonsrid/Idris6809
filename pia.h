#ifndef PIA_H
#define PIA_H

#include <stdint.h>
#include <stdbool.h>

/*
 * MC6821 Peripheral Interface Adapter (PIA)
 *
 * Each PIA has two ports (A and B), each with:
 *   - Data Direction Register (DDR): determines pin direction (0=input, 1=output)
 *   - Data Register (DR): reads pin state / writes output latch
 *   - Control Register (CR): configures interrupts and DDR/DR access
 *
 * Register addresses (relative to base):
 *   base+0: Port A DDR/DR (selected by CRA bit 2)
 *   base+1: Port A Control Register (CRA)
 *   base+2: Port B DDR/DR (selected by CRB bit 2)
 *   base+3: Port B Control Register (CRB)
 *
 * Control register bits:
 *   Bit 7: IRQ1 flag (set by CA1/CB1 transition, cleared on DR read)
 *   Bit 6: IRQ2 flag (set by CA2/CB2 transition if configured as input)
 *   Bits 5-3: CA2/CB2 control
 *   Bit 2: DDR/DR select (0=DDR, 1=DR)
 *   Bits 1-0: CA1/CB1 control
 *     Bit 1: active edge (0=falling, 1=rising)
 *     Bit 0: IRQ enable (1=enabled)
 */

/* Control register bit masks */
#define PIA_CR_IRQ1_FLAG   0x80  /* IRQ1 status (read-only) */
#define PIA_CR_IRQ2_FLAG   0x40  /* IRQ2 status (read-only) */
#define PIA_CR_C2_CTRL     0x38  /* CA2/CB2 control bits 5-3 */
#define PIA_CR_DDR_SELECT  0x04  /* 0=DDR, 1=Data Register */
#define PIA_CR_C1_EDGE     0x02  /* CA1/CB1 edge: 0=falling, 1=rising */
#define PIA_CR_C1_IRQ_EN   0x01  /* CA1/CB1 IRQ enable */

typedef struct {
    /* Port A */
    uint8_t  ddra;       /* Data Direction Register A */
    uint8_t  ora;        /* Output Register A (what CPU wrote) */
    uint8_t  ira;        /* Input Register A (external pin state) */
    uint8_t  cra;        /* Control Register A */

    /* Port B */
    uint8_t  ddrb;
    uint8_t  orb;
    uint8_t  irb;
    uint8_t  crb;
} PIA;

/* Initialize PIA to reset state (all registers zero) */
void pia_init(PIA *pia);

/* Read from PIA register (offset 0-3 from base address) */
uint8_t pia_read(PIA *pia, uint8_t reg);

/* Write to PIA register (offset 0-3 from base address) */
void pia_write(PIA *pia, uint8_t reg, uint8_t val);

/* Signal CA1/CB1 transition. The PIA checks the configured edge.
 * state: new pin level (true=high, false=low).
 * For CA1, pass the previous and new state; the PIA detects the edge.
 * Returns true if the IRQ flag was set. */
bool pia_set_ca1(PIA *pia, bool state);
bool pia_set_cb1(PIA *pia, bool state);

/* Set CA2/CB2 input pin state (only relevant when configured as input).
 * Returns true if the IRQ flag was set. */
bool pia_set_ca2(PIA *pia, bool state);
bool pia_set_cb2(PIA *pia, bool state);

/* Get the IRQ output state for each half:
 * IRQA = (IRQ1A flag && IRQ1A enable) || (IRQ2A flag && IRQ2A enable)
 * IRQB = (IRQ1B flag && IRQ1B enable) || (IRQ2B flag && IRQ2B enable) */
bool pia_irq_a(const PIA *pia);
bool pia_irq_b(const PIA *pia);

/* Read the output register for port A or B (for peripherals to read) */
uint8_t pia_get_output_a(const PIA *pia);
uint8_t pia_get_output_b(const PIA *pia);

/* Set the input pins for port A or B (external device drives these) */
void pia_set_input_a(PIA *pia, uint8_t val);
void pia_set_input_b(PIA *pia, uint8_t val);

/*
 * Dragon 64 specific: two PIA instances
 *
 * PIA0 ($FF00-$FF03):
 *   Port A: keyboard column inputs (active low) / joystick comparator
 *   Port B: keyboard row select outputs
 *   CA1: HSYNC from VDG
 *   CB1: FSYNC from VDG (60 Hz frame interrupt)
 *
 * PIA1 ($FF20-$FF23):
 *   Port A: DAC output (bits 0-5), cassette input (bit 7)
 *   Port B: VDG mode pins (bits 3-7), sound enable (bit 1), RAM size (bit 2)
 *   CB1: CART interrupt from cartridge port
 *
 * Interrupt routing:
 *   CPU IRQ  = PIA0_IRQA | PIA0_IRQB | PIA1_IRQA
 *   CPU FIRQ = PIA1_IRQB
 */

#endif
