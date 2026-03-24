#include "cassette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set up waveform parameters for the current bit */
static void setup_current_bit(Cassette *cas)
{
    if (cas->byte_pos >= cas->data_size) {
        cas->playing = false;
        return;
    }

    int bit = (cas->data[cas->byte_pos] >> cas->bit_pos) & 1;

    if (bit) {
        /* Bit 1: 1 cycle at 2400 Hz = 2 half-cycles of 185 cycles */
        cas->halfcycle_len = CAS_HALFCYCLE_2400;
    } else {
        /* Bit 0: 1 cycle at 1200 Hz = 2 half-cycles of 370 cycles */
        cas->halfcycle_len = CAS_HALFCYCLE_1200;
    }
    cas->halfcycles_per_bit = 2;

    cas->halfcycle_idx = 0;
    /* Don't reset cycle_count here — leftover cycles carry forward */
}

/* Advance to the next bit in the stream */
static void advance_bit(Cassette *cas)
{
    cas->bit_pos++;
    if (cas->bit_pos > 7) {
        cas->bit_pos = 0;
        cas->byte_pos++;
    }
    setup_current_bit(cas);
}

void cassette_init(Cassette *cas)
{
    memset(cas, 0, sizeof(*cas));
    cas->bit_pos = 7;
}

int cassette_load(Cassette *cas, const char *path)
{
    cassette_eject(cas);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open cassette: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        fprintf(stderr, "Cassette file is empty: %s\n", path);
        return -1;
    }

    cas->data = malloc((size_t)size);
    if (!cas->data) {
        fclose(f);
        return -1;
    }

    size_t n = fread(cas->data, 1, (size_t)size, f);
    fclose(f);

    if ((long)n != size) {
        fprintf(stderr, "Cassette %s: expected %ld bytes, got %zu\n", path, size, n);
        free(cas->data);
        cas->data = NULL;
        return -1;
    }

    cas->data_size = (size_t)size;
    cassette_rewind(cas);
    return 0;
}

void cassette_eject(Cassette *cas)
{
    free(cas->data);
    memset(cas, 0, sizeof(*cas));
    cas->bit_pos = 7;
}

void cassette_rewind(Cassette *cas)
{
    if (!cas->data || cas->data_size == 0)
        return;

    cas->byte_pos = 0;
    cas->bit_pos = 0;  /* LSB first (matches Dragon ROM's BYTIN which uses RORA) */
    cas->halfcycle_idx = 0;
    cas->cycle_count = 0.0;
    cas->signal_level = true;  /* Start HIGH so ROM sees first LOW transition */
    cas->playing = true;
    setup_current_bit(cas);
}

void cassette_set_motor(Cassette *cas, bool on)
{
    cas->motor_on = on;
}

bool cassette_update(Cassette *cas, int cycles)
{
    if (!cas->playing || !cas->motor_on || !cas->data)
        return cas->signal_level;

    cas->cycle_count += cycles;

    while (cas->cycle_count >= cas->halfcycle_len) {
        cas->cycle_count -= cas->halfcycle_len;

        /* Toggle signal at each half-cycle boundary */
        cas->signal_level = !cas->signal_level;
        cas->halfcycle_idx++;

        /* Bit complete when all half-cycles done */
        if (cas->halfcycle_idx >= cas->halfcycles_per_bit) {
            advance_bit(cas);
            if (!cas->playing)
                break;
        }
    }

    return cas->signal_level;
}

bool cassette_is_playing(const Cassette *cas)
{
    return cas->playing && cas->motor_on;
}

size_t cassette_get_position(const Cassette *cas)
{
    return cas->byte_pos;
}

size_t cassette_get_size(const Cassette *cas)
{
    return cas->data_size;
}
