#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize memory subsystem (zeroes RAM, clears ROM buffers) */
void mem_init(void);

/* Load a ROM image from file into the ROM buffer at the given offset.
 * rom_offset is relative to the start of the ROM buffer (0 = $8000 equivalent). */
int mem_load_rom(const char *path, uint16_t rom_offset, uint16_t size);

/* Copy data within the ROM buffer (e.g. for mirroring).
 * Both offsets are relative to the start of the ROM buffer. */
void mem_mirror_rom(uint16_t dst_offset, uint16_t src_offset, uint16_t size);

/* Main read/write interface used by the CPU */
uint8_t mem_read(uint16_t addr);
void    mem_write(uint16_t addr, uint8_t val);

/* ROM/RAM mode control (called by SAM TY bit) */
void mem_set_rom_mode(bool rom_enabled);
bool mem_get_rom_mode(void);

/* I/O callback types — peripherals register themselves */
typedef uint8_t (*io_read_fn)(uint16_t addr);
typedef void    (*io_write_fn)(uint16_t addr, uint8_t val);

/* Register I/O handlers for address ranges within $FF00-$FFBF.
 * base: start address (e.g. 0xFF00)
 * size: number of bytes (e.g. 4)
 * read_fn / write_fn: handler functions (NULL for unhandled) */
void mem_register_io(uint16_t base, uint16_t size,
                     io_read_fn read_fn, io_write_fn write_fn);

/* Register a callback for SAM register writes ($FFC0-$FFDF).
 * The callback receives the address written to. */
typedef void (*sam_write_fn)(uint16_t addr);
void mem_register_sam(sam_write_fn fn);

/* Direct RAM access (for VDG rendering, DMA, etc.) */
uint8_t *mem_get_ram(void);

#endif
