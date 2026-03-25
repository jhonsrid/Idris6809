#include "memory.h"
#include <stdio.h>
#include <string.h>

/*
 * Dragon 32 Memory Map:
 *
 * $0000-$7FFF  32KB  RAM (always)
 * $8000-$BFFF  16KB  ROM (d32.rom) or RAM (all-RAM mode)
 * $C000-$FEFF  ~16KB ROM (mirror) or RAM (all-RAM mode)
 * $FF00-$FF03        PIA0
 * $FF04-$FF1F        mirrors / unused
 * $FF20-$FF23        PIA1
 * $FF24-$FFBF        mirrors / unused
 * $FFC0-$FFDF        SAM registers
 * $FFE0-$FFEF        reserved
 * $FFF0-$FFFF        interrupt vectors (always from ROM)
 *
 * Writes to $8000-$FEFF always go to RAM (even in ROM mode).
 * Reads from $FFE0-$FFFF always come from ROM (interrupt vectors).
 */

static uint8_t ram[0x10000];   /* Full 64KB RAM */
static uint8_t rom[0x8000];    /* 32KB ROM buffer ($8000-$FFFF image) */
static bool    rom_mode;       /* true = ROM overlays $8000-$FEFF (boot default) */

/* Cartridge ROM: maps at $C000-$FEFF, overlaying the system ROM mirror */
static uint8_t cart_rom[0x4000];  /* 16KB max */
static uint16_t cart_size;         /* Actual size (0 = no cartridge) */

/* I/O dispatch table for $FF00-$FFBF (192 bytes) */
#define IO_BASE  0xFF00
#define IO_SIZE  0x00C0  /* $FF00 to $FFBF inclusive */

static io_read_fn  io_read_handlers[IO_SIZE];
static io_write_fn io_write_handlers[IO_SIZE];

/* SAM write callback */
static sam_write_fn sam_handler;

void mem_init(void)
{
    memset(ram, 0, sizeof(ram));
    memset(rom, 0, sizeof(rom));
    memset(io_read_handlers, 0, sizeof(io_read_handlers));
    memset(io_write_handlers, 0, sizeof(io_write_handlers));
    sam_handler = NULL;
    rom_mode = true;  /* Boot with ROM enabled */
    memset(cart_rom, 0, sizeof(cart_rom));
    cart_size = 0;
}

int mem_load_rom(const char *path, uint16_t rom_offset, uint16_t size)
{
    if (rom_offset + size > sizeof(rom)) {
        fprintf(stderr, "ROM offset $%04X + size $%04X exceeds ROM buffer\n",
                rom_offset, size);
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open ROM: %s\n", path);
        return -1;
    }
    size_t n = fread(&rom[rom_offset], 1, size, f);
    fclose(f);
    if (n != size) {
        fprintf(stderr, "ROM %s: expected %u bytes, got %zu\n", path, size, n);
        return -1;
    }
    return 0;
}

void mem_mirror_rom(uint16_t dst_offset, uint16_t src_offset, uint16_t size)
{
    if (dst_offset + size <= sizeof(rom) && src_offset + size <= sizeof(rom))
        memcpy(&rom[dst_offset], &rom[src_offset], size);
}

void mem_set_rom_mode(bool rom_enabled)
{
    rom_mode = rom_enabled;
}

bool mem_get_rom_mode(void)
{
    return rom_mode;
}

uint8_t mem_read(uint16_t addr)
{
    /* $0000-$7FFF: always RAM */
    if (addr < 0x8000)
        return ram[addr];

    /* $FF00-$FFBF: I/O region */
    if (addr >= IO_BASE && addr < IO_BASE + IO_SIZE) {
        uint16_t offset = addr - IO_BASE;
        if (io_read_handlers[offset])
            return io_read_handlers[offset](addr);
        return 0xFF; /* Unmapped I/O reads as $FF */
    }

    /* $FFC0-$FFDF: SAM registers (no readable data, return $FF) */
    if (addr >= 0xFFC0 && addr <= 0xFFDF)
        return 0xFF;

    /* $FFE0-$FFFF: Always reads from ROM (interrupt vectors) */
    if (addr >= 0xFFE0)
        return rom[addr - 0x8000];

    /* $C000-$FEFF: cartridge ROM overlays this range if present */
    if (cart_size > 0 && addr >= 0xC000 && addr < 0xFF00) {
        uint16_t cart_offset = addr - 0xC000;
        if (cart_offset < cart_size)
            return cart_rom[cart_offset];
        /* 8KB carts mirror: $E000-$FEFF reads from $C000-$DFFF */
        if (cart_size == 0x2000)
            return cart_rom[cart_offset & 0x1FFF];
    }

    /* $8000-$FEFF: ROM or RAM depending on mode */
    if (rom_mode)
        return rom[addr - 0x8000];
    else
        return ram[addr];
}

void mem_write(uint16_t addr, uint8_t val)
{
    /* $0000-$7FFF: always RAM */
    if (addr < 0x8000) {
        ram[addr] = val;
        return;
    }

    /* $FF00-$FFBF: I/O region */
    if (addr >= IO_BASE && addr < IO_BASE + IO_SIZE) {
        uint16_t offset = addr - IO_BASE;
        if (io_write_handlers[offset])
            io_write_handlers[offset](addr, val);
        return;
    }

    /* $FFC0-$FFDF: SAM registers (address-only writes, data is ignored) */
    if (addr >= 0xFFC0 && addr <= 0xFFDF) {
        if (sam_handler)
            sam_handler(addr);
        return;
    }

    /* $FFE0-$FFFF: writes still go to RAM underneath */
    if (addr >= 0xFFE0) {
        ram[addr] = val;
        return;
    }

    /* $8000-$FEFF: writes always go to RAM (even when ROM is overlaid) */
    ram[addr] = val;
}

void mem_register_io(uint16_t base, uint16_t size,
                     io_read_fn read_fn, io_write_fn write_fn)
{
    for (uint16_t i = 0; i < size; i++) {
        uint16_t addr = base + i;
        if (addr >= IO_BASE && addr < IO_BASE + IO_SIZE) {
            uint16_t offset = addr - IO_BASE;
            io_read_handlers[offset] = read_fn;
            io_write_handlers[offset] = write_fn;
        }
    }
}

void mem_register_sam(sam_write_fn fn)
{
    sam_handler = fn;
}

uint8_t *mem_get_ram(void)
{
    return ram;
}

int mem_load_cartridge(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open cartridge: %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Failed to seek cartridge: %s\n", path);
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to seek cartridge: %s\n", path);
        fclose(f);
        return -1;
    }

    if (size <= 0 || size > 0x4000) {
        fprintf(stderr, "Cartridge %s: invalid size %ld (max 16384)\n", path, size);
        fclose(f);
        return -1;
    }

    memset(cart_rom, 0xFF, sizeof(cart_rom));
    size_t n = fread(cart_rom, 1, (size_t)size, f);
    fclose(f);

    if ((long)n != size) {
        fprintf(stderr, "Cartridge %s: expected %ld bytes, got %zu\n", path, size, n);
        return -1;
    }

    cart_size = (uint16_t)size;
    return 0;
}

void mem_eject_cartridge(void)
{
    memset(cart_rom, 0, sizeof(cart_rom));
    cart_size = 0;
}

bool mem_has_cartridge(void)
{
    return cart_size > 0;
}
