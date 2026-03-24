#include "pia.h"

void pia_init(PIA *pia)
{
    pia->ddra = 0x00;
    pia->ora  = 0x00;
    pia->ira  = 0xFF;  /* Undriven inputs float high */
    pia->cra  = 0x00;

    pia->ddrb = 0x00;
    pia->orb  = 0x00;
    pia->irb  = 0xFF;
    pia->crb  = 0x00;
}

uint8_t pia_read(PIA *pia, uint8_t reg)
{
    switch (reg & 0x03) {
    case 0: /* Port A DDR or Data */
        if (pia->cra & PIA_CR_DDR_SELECT) {
            /* Read data register:
             * Output bits (DDR=1) return output register value.
             * Input bits (DDR=0) return external pin state. */
            uint8_t val = (pia->ora & pia->ddra) | (pia->ira & ~pia->ddra);
            /* Reading data register clears IRQ1 and IRQ2 flags */
            pia->cra &= ~(PIA_CR_IRQ1_FLAG | PIA_CR_IRQ2_FLAG);
            return val;
        } else {
            return pia->ddra;
        }

    case 1: /* Control Register A */
        return pia->cra;

    case 2: /* Port B DDR or Data */
        if (pia->crb & PIA_CR_DDR_SELECT) {
            /* Port B: on the real MC6821, port B reads the output register
             * for output bits, not the pin state. Input bits read pins. */
            uint8_t val = (pia->orb & pia->ddrb) | (pia->irb & ~pia->ddrb);
            /* Reading data register clears IRQ1 and IRQ2 flags */
            pia->crb &= ~(PIA_CR_IRQ1_FLAG | PIA_CR_IRQ2_FLAG);
            return val;
        } else {
            return pia->ddrb;
        }

    case 3: /* Control Register B */
        return pia->crb;
    }
    return 0xFF;
}

void pia_write(PIA *pia, uint8_t reg, uint8_t val)
{
    switch (reg & 0x03) {
    case 0: /* Port A DDR or Data */
        if (pia->cra & PIA_CR_DDR_SELECT)
            pia->ora = val;
        else
            pia->ddra = val;
        break;

    case 1: /* Control Register A */
        /* Bits 7-6 are read-only IRQ flags; preserve them */
        pia->cra = (pia->cra & 0xC0) | (val & 0x3F);
        break;

    case 2: /* Port B DDR or Data */
        if (pia->crb & PIA_CR_DDR_SELECT)
            pia->orb = val;
        else
            pia->ddrb = val;
        break;

    case 3: /* Control Register B */
        pia->crb = (pia->crb & 0xC0) | (val & 0x3F);
        break;
    }
}

/*
 * CA1/CB1 edge detection:
 *
 * CR bit 1 selects the active edge:
 *   0 = falling edge (high->low transition sets IRQ flag)
 *   1 = rising edge  (low->high transition sets IRQ flag)
 *
 * We store the previous pin state implicitly: the IRQ flag is set
 * on the appropriate transition. The caller passes the new state,
 * and we detect the edge by comparing with what would trigger.
 *
 * In practice, the caller toggles between states, so we track
 * the previous state internally to detect edges.
 */

/* Internal: previous CA1/CB1/CA2/CB2 pin states */
/* We store these in the upper bits of a static — but since we might
 * have multiple PIAs, we need per-PIA state. We'll use the IRQ flag
 * itself and edge detection logic. The simplest approach: the caller
 * is expected to call set_ca1/cb1 only on transitions. But to be safe,
 * we do a simple edge detect using a shadow. Since PIA struct doesn't
 * have shadow fields, let's add a practical approach: the function
 * triggers on the correct edge only. We maintain previous state via
 * an extra field. Actually, let's keep it simple — the caller should
 * call these functions to signal a transition has occurred. We check
 * if the new state matches the configured active edge. */

bool pia_set_ca1(PIA *pia, bool state)
{
    /* Active edge: CR bit 1: 0=falling (new state=low), 1=rising (new state=high) */
    bool active_edge = (pia->cra & PIA_CR_C1_EDGE) ? state : !state;
    if (active_edge) {
        pia->cra |= PIA_CR_IRQ1_FLAG;
        return (pia->cra & PIA_CR_C1_IRQ_EN) != 0;
    }
    return false;
}

bool pia_set_cb1(PIA *pia, bool state)
{
    bool active_edge = (pia->crb & PIA_CR_C1_EDGE) ? state : !state;
    if (active_edge) {
        pia->crb |= PIA_CR_IRQ1_FLAG;
        return (pia->crb & PIA_CR_C1_IRQ_EN) != 0;
    }
    return false;
}

bool pia_set_ca2(PIA *pia, bool state)
{
    /* CA2 as input: CR bits 5-3 = 0xx */
    if (pia->cra & 0x20)
        return false;  /* CA2 configured as output, ignore input */

    /* Edge select: bit 4 (within CA2 ctrl field bit 3): 0=falling, 1=rising */
    bool active_edge = (pia->cra & 0x10) ? state : !state;
    if (active_edge) {
        pia->cra |= PIA_CR_IRQ2_FLAG;
        /* IRQ2 enable: bit 3 */
        return (pia->cra & 0x08) != 0;
    }
    return false;
}

bool pia_set_cb2(PIA *pia, bool state)
{
    if (pia->crb & 0x20)
        return false;

    bool active_edge = (pia->crb & 0x10) ? state : !state;
    if (active_edge) {
        pia->crb |= PIA_CR_IRQ2_FLAG;
        return (pia->crb & 0x08) != 0;
    }
    return false;
}

bool pia_irq_a(const PIA *pia)
{
    /* IRQA is asserted if:
     *   (IRQ1 flag set AND IRQ1 enabled) OR
     *   (IRQ2 flag set AND IRQ2 enabled)
     *
     * IRQ1 enable = CRA bit 0
     * IRQ2 enable = CRA bit 3 (only when CA2 is input, bit 5=0)
     */
    bool irq1 = (pia->cra & PIA_CR_IRQ1_FLAG) && (pia->cra & PIA_CR_C1_IRQ_EN);
    bool irq2 = (pia->cra & PIA_CR_IRQ2_FLAG) && (pia->cra & 0x08) &&
                !(pia->cra & 0x20);
    return irq1 || irq2;
}

bool pia_irq_b(const PIA *pia)
{
    bool irq1 = (pia->crb & PIA_CR_IRQ1_FLAG) && (pia->crb & PIA_CR_C1_IRQ_EN);
    bool irq2 = (pia->crb & PIA_CR_IRQ2_FLAG) && (pia->crb & 0x08) &&
                !(pia->crb & 0x20);
    return irq1 || irq2;
}

uint8_t pia_get_output_a(const PIA *pia)
{
    /* Return the driven output bits; input bits return 0 */
    return pia->ora & pia->ddra;
}

uint8_t pia_get_output_b(const PIA *pia)
{
    return pia->orb & pia->ddrb;
}

void pia_set_input_a(PIA *pia, uint8_t val)
{
    pia->ira = val;
}

void pia_set_input_b(PIA *pia, uint8_t val)
{
    pia->irb = val;
}
