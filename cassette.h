#ifndef CASSETTE_H
#define CASSETTE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Dragon cassette (.cas file) emulation via bit-level FSK simulation.
 *
 * .cas files contain the raw byte stream as written to tape:
 *   - Leader: $55 bytes (01010101 = steady 2400 Hz tone)
 *   - Sync byte: $3C
 *   - Block type: $00=namefile, $01=data, $FF=EOF
 *   - Block length: 1 byte
 *   - Data: <length> bytes
 *   - Checksum: 1 byte
 *   - Trailer: $55 bytes
 *
 * FSK encoding (per the Dragon's Kansas City Standard variant):
 *   Bit 0: 1 cycle at 1200 Hz = 2 half-cycles of ~370 CPU cycles = ~740 total
 *   Bit 1: 2 cycles at 2400 Hz = 4 half-cycles of ~185 CPU cycles = ~740 total
 *   Both bit types take the same duration (~1/1200 second).
 *
 * PIA1 PA7 (cassette comparator input) toggles at each half-cycle.
 */

/* CPU cycles per half-cycle derived from the Dragon ROM's actual CSAVE
 * tone generation timing (not theoretical 1200/2400 Hz).
 * The ROM outputs a 36-sample sine table per bit cycle:
 *   Bit 0: 36 samples at 22 cycles/sample + 29 overhead = 813 cycles/bit
 *   Bit 1: 18 samples at 23 cycles/sample + 29 overhead = 435 cycles/bit */
#define CAS_HALFCYCLE_1200  406.5   /* 813 / 2 */
#define CAS_HALFCYCLE_2400  217.5   /* 435 / 2 */

typedef struct {
    /* File data */
    uint8_t  *data;
    size_t    data_size;

    /* Byte/bit position in the stream */
    size_t    byte_pos;
    int       bit_pos;       /* 7=MSB down to 0=LSB */

    /* Waveform state for current bit */
    double    halfcycle_len;     /* CPU cycles per half-cycle (fractional) */
    int       halfcycles_per_bit; /* Always 2 (one full cycle per bit for both 1200/2400 Hz) */
    int       halfcycle_idx;     /* Which half-cycle we're on (0-based) */
    double    cycle_count;       /* Cycles accumulated (fractional) */
    bool      signal_level;  /* Current comparator output */

    /* Control */
    bool      motor_on;
    bool      playing;       /* Tape loaded and not at end */
} Cassette;

void cassette_init(Cassette *cas);
int  cassette_load(Cassette *cas, const char *path);
void cassette_eject(Cassette *cas);
void cassette_rewind(Cassette *cas);
void cassette_set_motor(Cassette *cas, bool on);

/* Advance cassette by the given CPU cycles.
 * Returns the current signal level (for updating PIA1 PA7). */
bool cassette_update(Cassette *cas, int cycles);

bool   cassette_is_playing(const Cassette *cas);
size_t cassette_get_position(const Cassette *cas);
size_t cassette_get_size(const Cassette *cas);

#endif
