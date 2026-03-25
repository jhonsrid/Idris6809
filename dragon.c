#include "dragon.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * I/O callbacks — bridge memory I/O to PIA instances
 * ================================================================ */

/* We need access to the Dragon struct from the I/O callbacks.
 * Since the memory subsystem uses function pointers without context,
 * we use a module-level pointer to the active Dragon instance. */
static Dragon *g_dragon;

static void update_keyboard_columns(Dragon *d);

static uint8_t pia0_read_cb(uint16_t addr)
{
    return pia_read(&g_dragon->pia0, (uint8_t)(addr & 0x03));
}

static void pia0_write_cb(uint16_t addr, uint8_t val)
{
    uint8_t reg = (uint8_t)(addr & 0x03);
    pia_write(&g_dragon->pia0, reg, val);

    /* When port B data register is written (column select changes),
     * immediately update keyboard row data on port A. */
    if (reg == 2 && (g_dragon->pia0.crb & PIA_CR_DDR_SELECT))
        update_keyboard_columns(g_dragon);
}

static uint8_t pia1_read_cb(uint16_t addr)
{
    return pia_read(&g_dragon->pia1, (uint8_t)(addr & 0x03));
}

static void pia1_write_cb(uint16_t addr, uint8_t val)
{
    uint8_t reg = (uint8_t)(addr & 0x03);
    pia_write(&g_dragon->pia1, reg, val);

    /* CRA written (reg 1): check cassette motor control via CA2.
     * When CRA bit 5 = 1, CA2 is output; bit 3 = level. */
    if (reg == 1) {
        uint8_t cra = g_dragon->pia1.cra;
        if (cra & 0x20) {
            bool motor = (cra & 0x08) != 0;
            cassette_set_motor(&g_dragon->cassette, motor);
        }
    }

    /* Side effect: when PIA1 port B data register is written,
     * update VDG mode pins. Port B is at register offset 2. */
    if (reg == 2 && (g_dragon->pia1.crb & PIA_CR_DDR_SELECT)) {
        uint8_t pb_out = pia_get_output_b(&g_dragon->pia1);
        vdg_set_mode(&g_dragon->vdg, pb_out);
    }
}

static void sam_write_cb(uint16_t addr)
{
    sam_write(&g_dragon->sam, addr);
}

/* ================================================================
 * Keyboard matrix scanning
 *
 * PIA0 port B (output) drives columns (active low).
 * PIA0 port A (input) reads rows (active low = pressed).
 *
 * keyboard[col] holds row states: bit N = 0 means key at (row N, col) pressed.
 * When the ROM drives PB column C low, we return keyboard[C] on PA.
 * ================================================================ */
static void update_keyboard_columns(Dragon *d)
{
    uint8_t col_select = pia_get_output_b(&d->pia0);
    uint8_t rows = 0xFF;  /* All high = no keys pressed */

    for (int col = 0; col < 8; col++) {
        /* Column is selected when its bit is LOW */
        if (!(col_select & (1 << col))) {
            rows &= d->keyboard[col];
        }
    }

    pia_set_input_a(&d->pia0, rows);
}

/* ================================================================
 * Interrupt routing
 *
 * CPU IRQ  = PIA0_IRQA | PIA0_IRQB | PIA1_IRQA
 * CPU FIRQ = PIA1_IRQB
 * ================================================================ */
static void update_cpu_interrupts(Dragon *d)
{
    bool irq = pia_irq_a(&d->pia0) || pia_irq_b(&d->pia0) || pia_irq_a(&d->pia1);
    bool firq = pia_irq_b(&d->pia1);

    cpu_set_irq(&d->cpu, irq);
    cpu_set_firq(&d->cpu, firq);
}

/* ================================================================
 * Public API
 * ================================================================ */

void dragon_init(Dragon *d)
{
    memset(d, 0, sizeof(*d));
    g_dragon = d;

    /* Initialize all subsystems */
    mem_init();
    sam_init(&d->sam);
    vdg_init(&d->vdg, mem_get_ram());
    pia_init(&d->pia0);
    pia_init(&d->pia1);
    cassette_init(&d->cassette);
    cpu_init(&d->cpu);

    /* Register I/O handlers */
    mem_register_sam(sam_write_cb);
    mem_register_io(0xFF00, 4, pia0_read_cb, pia0_write_cb);
    mem_register_io(0xFF20, 4, pia1_read_cb, pia1_write_cb);

    /* Keyboard: all keys released (active low, so $FF = all up) */
    memset(d->keyboard, 0xFF, sizeof(d->keyboard));

    /* PIA1 port B input: bit 2 = 1 (32K RAM) → $FF */
    pia_set_input_b(&d->pia1, 0xFF);

    d->running = true;
    d->frame_count = 0;
}

int dragon_load_rom(const char *rom_path)
{
    /* Single-ROM mode: detect size */
    FILE *f = fopen(rom_path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open ROM: %s\n", rom_path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Failed to seek in ROM: %s\n", rom_path);
        fclose(f);
        return -1;
    }
    long rom_size = ftell(f);
    fclose(f);

    if (rom_size == 0x8000) {
        /* 32KB: fills entire ROM buffer ($8000-$FFFF) */
        if (mem_load_rom(rom_path, 0x0000, 0x8000) != 0)
            return -1;
    } else if (rom_size == 0x4000) {
        /* 16KB: load at $8000-$BFFF, mirror to $C000-$FFFF */
        if (mem_load_rom(rom_path, 0x0000, 0x4000) != 0)
            return -1;
        mem_mirror_rom(0x4000, 0x0000, 0x4000);
    } else {
        fprintf(stderr, "ROM %s: unexpected size %ld (expected 16384 or 32768)\n",
                rom_path, rom_size);
        return -1;
    }
    return 0;
}

void dragon_reset(Dragon *d)
{
    /* Reset peripherals */
    sam_init(&d->sam);
    pia_init(&d->pia0);
    pia_init(&d->pia1);
    pia_set_input_b(&d->pia1, 0xFF);
    vdg_init(&d->vdg, mem_get_ram());

    /* Reset timing state */
    d->cycle_debt = 0;

    /* Reset CPU — loads PC from reset vector */
    cpu_reset(&d->cpu);

    /* Cartridge auto-start: after BASIC boots, it checks $71==$55 and
     * if the byte at [$72-$73] == $12 (signature), it JMPs there.
     * Pre-set $72-$73 to $C000 so BASIC finds the cartridge. The ROM
     * also needs to start with $12 for auto-start to trigger.
     * If no $12 signature, assert CART (PIA1 CB1) after boot delay
     * to trigger FIRQ, which restarts BASIC with JMP $C000. */
    if (mem_has_cartridge()) {
        uint8_t *ram = mem_get_ram();
        ram[0x72] = 0xC0;
        ram[0x73] = 0x00;  /* EXEC address = $C000 */
        /* If cartridge lacks $12 signature, use FIRQ fallback */
        d->cart_firq_delay = (mem_read(0xC000) != 0x12) ? 120 : 0;
    } else {
        d->cart_firq_delay = 0;
    }
}

int dragon_run_scanline(Dragon *d)
{
    /* Start with any overshoot carried from the previous scanline */
    int cycles_this_line = d->cycle_debt;
    int cycles_executed = 0;

    /* Update keyboard columns before CPU runs (ROM may read mid-line) */
    update_keyboard_columns(d);

    /* Execute CPU cycles for one scanline */
    while (cycles_this_line < DRAGON_CYCLES_PER_SCANLINE) {
        update_cpu_interrupts(d);
        int c = cpu_step(&d->cpu);
        cycles_this_line += c;
        cycles_executed += c;

        /* Advance cassette and update PIA1 PA0 (comparator input).
         * DDRA bit 0 = 0 (input), so reads of port A return IRA bit 0. */
        bool cas_level = cassette_update(&d->cassette, c);
        if (cas_level)
            d->pia1.ira |= 0x01;
        else
            d->pia1.ira &= 0xFE;
    }

    /* Carry overshoot into next scanline */
    d->cycle_debt = cycles_this_line - DRAGON_CYCLES_PER_SCANLINE;

    /* VDG scanline processing */
    int vdg_line = d->vdg.scanline;

    /* Render if in active display area */
    if (vdg_line < VDG_HEIGHT) {
        uint16_t display_addr = sam_get_display_addr(&d->sam);
        /* Update VDG mode from PIA1 port B output */
        vdg_set_mode(&d->vdg, pia_get_output_b(&d->pia1));
        vdg_render_scanline(&d->vdg, vdg_line, display_addr);
    }

    /* Advance VDG scanline counter */
    bool fs_edge = vdg_tick_scanline(&d->vdg);

    /* FSYNC triggers PIA0 CB1 (falling edge) at start of vblank */
    if (fs_edge) {
        pia_set_cb1(&d->pia0, false);
        update_cpu_interrupts(d);
    }

    return cycles_executed;
}

void dragon_end_frame(Dragon *d)
{
    d->frame_count++;

    /* Delayed cartridge FIRQ: after BASIC has booted and enabled
     * PIA1 CB1 (rising edge, CRB bit1=1), pulse CART high to trigger. */
    if (d->cart_firq_delay > 0 && --d->cart_firq_delay == 0) {
        pia_set_cb1(&d->pia1, true);
        update_cpu_interrupts(d);
    }
}

int dragon_run_frame(Dragon *d)
{
    int total_cycles = 0;

    for (int sl = 0; sl < DRAGON_SCANLINES_PER_FRAME; sl++) {
        total_cycles += dragon_run_scanline(d);
    }

    dragon_end_frame(d);
    return total_cycles;
}

void dragon_key_press(Dragon *d, int row, int col, bool pressed)
{
    if (row < 0 || row > 7 || col < 0 || col > 7)
        return;

    /* keyboard[col] stores row bits: PB=col driven, PA=row read */
    if (pressed)
        d->keyboard[col] &= ~(1 << row);   /* Active low: clear bit */
    else
        d->keyboard[col] |= (1 << row);    /* Release: set bit */
}

const uint32_t *dragon_get_framebuffer(const Dragon *d)
{
    return &d->vdg.framebuffer[0][0];
}
