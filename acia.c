#include "acia.h"
#include <stddef.h>

void acia_init(ACIA *acia)
{
    acia->rx_data   = 0x00;
    acia->tx_data   = 0x00;
    acia->status    = ACIA_SR_TDRE;  /* TX empty on reset */
    acia->command   = 0x00;
    acia->control   = 0x00;
    acia->rx_full   = false;
    acia->tx_empty  = true;
    acia->tx_callback = NULL;
    acia->tx_ctx    = NULL;
}

uint8_t acia_read(ACIA *acia, uint8_t reg)
{
    switch (reg & 0x03) {
    case 0: {
        /* RX data register: read received byte, clear RDRF */
        uint8_t val = acia->rx_data;
        acia->rx_full = false;
        acia->status &= ~(ACIA_SR_RDRF | ACIA_SR_OVRN | ACIA_SR_PE | ACIA_SR_FE);
        /* Clear IRQ if RX was the source */
        acia->status &= ~ACIA_SR_IRQ;
        return val;
    }
    case 1:
        /* Status register: rebuild from internal state and return */
        {
            uint8_t sr = acia->status & ~(ACIA_SR_RDRF | ACIA_SR_TDRE);
            if (acia->rx_full)
                sr |= ACIA_SR_RDRF;
            if (acia->tx_empty)
                sr |= ACIA_SR_TDRE;
            return sr;
        }
    case 2:
        /* Command register */
        return acia->command;
    case 3:
        /* Control register */
        return acia->control;
    }
    return 0x00;
}

void acia_write(ACIA *acia, uint8_t reg, uint8_t val)
{
    switch (reg & 0x03) {
    case 0:
        /* TX data register: transmit byte */
        acia->tx_data = val;
        acia->tx_empty = false;

        /* Call TX callback if attached */
        if (acia->tx_callback)
            acia->tx_callback(val, acia->tx_ctx);

        /* Transmission completes immediately in our emulation */
        acia->tx_empty = true;
        break;

    case 1:
        /* Programmed reset: writing any value resets the ACIA */
        acia->status   = ACIA_SR_TDRE;
        acia->command  = 0x00;
        acia->control  = 0x00;
        acia->rx_full  = false;
        acia->tx_empty = true;
        break;

    case 2:
        /* Command register */
        acia->command = val;
        break;

    case 3:
        /* Control register */
        acia->control = val;
        break;
    }
}

bool acia_rx_byte(ACIA *acia, uint8_t byte)
{
    if (acia->rx_full) {
        /* Overrun: data register already full */
        acia->status |= ACIA_SR_OVRN;
        return false;
    }

    acia->rx_data = byte;
    acia->rx_full = true;

    /* Generate IRQ if RX IRQ is enabled (command bit 1 = 0 = enabled) */
    if (!(acia->command & 0x02)) {
        acia->status |= ACIA_SR_IRQ;
    }

    return true;
}

bool acia_irq(const ACIA *acia)
{
    return (acia->status & ACIA_SR_IRQ) != 0;
}

void acia_set_tx_callback(ACIA *acia, void (*cb)(uint8_t, void*), void *ctx)
{
    acia->tx_callback = cb;
    acia->tx_ctx = ctx;
}
