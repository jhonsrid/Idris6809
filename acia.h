#ifndef ACIA_H
#define ACIA_H

#include <stdint.h>
#include <stdbool.h>

/*
 * MC6551 Asynchronous Communications Interface Adapter (ACIA)
 *
 * Dragon 64 specific: mapped at $FF04-$FF07.
 *
 * Registers (offset from base):
 *   +0  Read:  RX data register    Write: TX data register
 *   +1  Read:  Status register     Write: Programmed reset
 *   +2  Read:  Command register    Write: Command register
 *   +3  Read:  Control register    Write: Control register
 *
 * Status register bits:
 *   Bit 0: Parity error (PE)
 *   Bit 1: Framing error (FE)
 *   Bit 2: Overrun (OVRN)
 *   Bit 3: RX data register full (RDRF)
 *   Bit 4: TX data register empty (TDRE)
 *   Bit 5: DCD (Data Carrier Detect, active low)
 *   Bit 6: DSR (Data Set Ready, active low)
 *   Bit 7: IRQ (interrupt occurred)
 *
 * Command register bits:
 *   Bit 0:    DTR (Data Terminal Ready)
 *   Bit 1:    RX IRQ enable (0=enabled, active low)
 *   Bits 3-2: TX control (00=TX IRQ disabled, RTS low; etc.)
 *   Bit 4:    Echo mode (0=normal)
 *   Bits 7-5: Parity control
 *
 * Control register bits:
 *   Bits 3-0: Baud rate select
 *   Bit 4:    RX clock source (0=external, 1=baud rate generator)
 *   Bits 6-5: Word length (00=8, 01=7, 10=6, 11=5)
 *   Bit 7:    Stop bits (0=1 stop, 1=2 stop; or 1.5 for 5-bit)
 */

/* Status register bit masks */
#define ACIA_SR_PE    0x01
#define ACIA_SR_FE    0x02
#define ACIA_SR_OVRN  0x04
#define ACIA_SR_RDRF  0x08  /* RX data register full */
#define ACIA_SR_TDRE  0x10  /* TX data register empty */
#define ACIA_SR_DCD   0x20  /* DCD (active low on pin, but bit=1 means no carrier) */
#define ACIA_SR_DSR   0x40  /* DSR */
#define ACIA_SR_IRQ   0x80  /* IRQ flag */

typedef struct {
    uint8_t  rx_data;       /* Last received byte */
    uint8_t  tx_data;       /* Last transmitted byte */
    uint8_t  status;        /* Status register */
    uint8_t  command;       /* Command register */
    uint8_t  control;       /* Control register */

    bool     rx_full;       /* RX data register has unread data */
    bool     tx_empty;      /* TX data register is available for writing */

    /* TX output callback: called when a byte is transmitted.
     * Set to NULL if no serial device is attached. */
    void     (*tx_callback)(uint8_t byte, void *ctx);
    void     *tx_ctx;
} ACIA;

/* Initialize ACIA to reset state */
void acia_init(ACIA *acia);

/* Read from ACIA register (offset 0-3) */
uint8_t acia_read(ACIA *acia, uint8_t reg);

/* Write to ACIA register (offset 0-3) */
void acia_write(ACIA *acia, uint8_t reg, uint8_t val);

/* Feed a byte into the RX side (external device sending to Dragon).
 * Returns true if the byte was accepted (RDRF was clear). */
bool acia_rx_byte(ACIA *acia, uint8_t byte);

/* Check if an IRQ is being asserted */
bool acia_irq(const ACIA *acia);

/* Set TX callback (called when CPU writes TX data) */
void acia_set_tx_callback(ACIA *acia, void (*cb)(uint8_t, void*), void *ctx);

#endif
