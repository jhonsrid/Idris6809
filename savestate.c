#include "savestate.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Helper macros for writing/reading with error checking */
#define WRITE(ptr, size, f) do { if (fwrite(ptr, size, 1, f) != 1) goto fail; } while(0)
#define READ(ptr, size, f)  do { if (fread(ptr, size, 1, f) != 1) goto fail; } while(0)

static void write_u8(FILE *f, uint8_t v)  { fwrite(&v, 1, 1, f); }
static void write_u16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void write_i32(FILE *f, int32_t v)  { fwrite(&v, 4, 1, f); }
static void write_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void write_f64(FILE *f, double v)   { fwrite(&v, 8, 1, f); }

static uint8_t  read_u8(FILE *f)  { uint8_t v;  fread(&v, 1, 1, f); return v; }
static uint16_t read_u16(FILE *f) { uint16_t v; fread(&v, 2, 1, f); return v; }
static int32_t  read_i32(FILE *f) { int32_t v;  fread(&v, 4, 1, f); return v; }
static uint32_t read_u32(FILE *f) { uint32_t v; fread(&v, 4, 1, f); return v; }
static double   read_f64(FILE *f) { double v;   fread(&v, 8, 1, f); return v; }

static void save_cpu(FILE *f, const CPU6809 *cpu)
{
    write_u16(f, cpu->pc);
    write_u16(f, cpu->x);
    write_u16(f, cpu->y);
    write_u16(f, cpu->u);
    write_u16(f, cpu->s);
    write_u16(f, cpu->d);
    write_u8(f, cpu->dp);
    write_u8(f, cpu->cc);
    write_i32(f, cpu->cycles);
    write_i32(f, cpu->total_cycles);
    write_u8(f, cpu->nmi_armed);
    write_u8(f, cpu->nmi_pending);
    write_u8(f, cpu->firq_pending);
    write_u8(f, cpu->irq_pending);
    write_u8(f, cpu->halted);
    write_u8(f, cpu->cwai);
}

static void load_cpu(FILE *f, CPU6809 *cpu)
{
    cpu->pc = read_u16(f);
    cpu->x  = read_u16(f);
    cpu->y  = read_u16(f);
    cpu->u  = read_u16(f);
    cpu->s  = read_u16(f);
    cpu->d  = read_u16(f);
    cpu->dp = read_u8(f);
    cpu->cc = read_u8(f);
    cpu->cycles = read_i32(f);
    cpu->total_cycles = read_i32(f);
    cpu->nmi_armed    = read_u8(f);
    cpu->nmi_pending  = read_u8(f);
    cpu->firq_pending = read_u8(f);
    cpu->irq_pending  = read_u8(f);
    cpu->halted       = read_u8(f);
    cpu->cwai         = read_u8(f);
}

static void save_sam(FILE *f, const SAM *sam)
{
    write_u8(f, sam->v);
    write_u8(f, sam->f);
    write_u8(f, sam->r);
    write_u8(f, sam->p);
    write_u8(f, sam->ty);
}

static void load_sam(FILE *f, SAM *sam)
{
    sam->v  = read_u8(f);
    sam->f  = read_u8(f);
    sam->r  = read_u8(f);
    sam->p  = read_u8(f);
    sam->ty = read_u8(f);
}

static void save_vdg(FILE *f, const VDG *vdg)
{
    write_u8(f, vdg->ag);
    write_u8(f, vdg->gm);
    write_u8(f, vdg->css);
    write_i32(f, vdg->scanline);
    write_u8(f, vdg->fs);
}

static void load_vdg(FILE *f, VDG *vdg)
{
    vdg->ag       = read_u8(f);
    vdg->gm       = read_u8(f);
    vdg->css       = read_u8(f);
    vdg->scanline  = read_i32(f);
    vdg->fs        = read_u8(f);
    /* ram pointer and framebuffer are NOT saved — already set by dragon_init */
}

static void save_pia(FILE *f, const PIA *pia)
{
    write_u8(f, pia->ddra);
    write_u8(f, pia->ora);
    write_u8(f, pia->ira);
    write_u8(f, pia->cra);
    write_u8(f, pia->ddrb);
    write_u8(f, pia->orb);
    write_u8(f, pia->irb);
    write_u8(f, pia->crb);
}

static void load_pia(FILE *f, PIA *pia)
{
    pia->ddra = read_u8(f);
    pia->ora  = read_u8(f);
    pia->ira  = read_u8(f);
    pia->cra  = read_u8(f);
    pia->ddrb = read_u8(f);
    pia->orb  = read_u8(f);
    pia->irb  = read_u8(f);
    pia->crb  = read_u8(f);
}

static void save_cassette(FILE *f, const Cassette *cas)
{
    /* Save playback position and waveform state, NOT the data buffer */
    write_u32(f, (uint32_t)cas->byte_pos);
    write_i32(f, cas->bit_pos);
    write_f64(f, cas->halfcycle_len);
    write_i32(f, cas->halfcycles_per_bit);
    write_i32(f, cas->halfcycle_idx);
    write_f64(f, cas->cycle_count);
    write_u8(f, cas->signal_level);
    write_u8(f, cas->motor_on);
    write_u8(f, cas->playing);
}

static void load_cassette(FILE *f, Cassette *cas)
{
    cas->byte_pos        = read_u32(f);
    cas->bit_pos         = read_i32(f);
    cas->halfcycle_len   = read_f64(f);
    cas->halfcycles_per_bit = read_i32(f);
    cas->halfcycle_idx   = read_i32(f);
    cas->cycle_count     = read_f64(f);
    cas->signal_level    = read_u8(f);
    cas->motor_on        = read_u8(f);
    cas->playing         = read_u8(f);
    /* data pointer and data_size are NOT restored — must re-load via --cas */
}

int savestate_save(const Dragon *d, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to create save state: %s\n", path);
        return -1;
    }

    /* Header */
    write_u32(f, SAVESTATE_MAGIC);
    write_u32(f, SAVESTATE_VERSION);

    /* RAM */
    WRITE(mem_get_ram(), 0x10000, f);

    /* Components */
    save_cpu(f, &d->cpu);
    save_sam(f, &d->sam);
    save_vdg(f, &d->vdg);
    save_pia(f, &d->pia0);
    save_pia(f, &d->pia1);
    save_cassette(f, &d->cassette);

    /* Memory mode */
    write_u8(f, mem_get_rom_mode() ? 1 : 0);

    /* Machine state */
    WRITE(d->keyboard, 8, f);
    write_i32(f, d->cycle_debt);
    write_i32(f, d->frame_count);

    fclose(f);
    return 0;

fail:
    fprintf(stderr, "Write error saving state: %s\n", path);
    fclose(f);
    return -1;
}

int savestate_load(Dragon *d, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open save state: %s\n", path);
        return -1;
    }

    /* Validate header */
    uint32_t magic = read_u32(f);
    uint32_t version = read_u32(f);
    if (magic != SAVESTATE_MAGIC) {
        fprintf(stderr, "Invalid save state (bad magic): %s\n", path);
        fclose(f);
        return -1;
    }
    if (version != SAVESTATE_VERSION) {
        fprintf(stderr, "Unsupported save state version %u: %s\n", version, path);
        fclose(f);
        return -1;
    }

    /* RAM */
    READ(mem_get_ram(), 0x10000, f);

    /* Components */
    load_cpu(f, &d->cpu);
    load_sam(f, &d->sam);
    load_vdg(f, &d->vdg);
    load_pia(f, &d->pia0);
    load_pia(f, &d->pia1);
    load_cassette(f, &d->cassette);

    /* Memory mode */
    mem_set_rom_mode(read_u8(f) != 0);

    /* Machine state */
    READ(d->keyboard, 8, f);
    d->cycle_debt  = read_i32(f);
    d->frame_count = read_i32(f);

    fclose(f);
    return 0;

fail:
    fprintf(stderr, "Read error loading state: %s\n", path);
    fclose(f);
    return -1;
}

void savestate_make_filename(char *buf, size_t bufsize)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, bufsize, "%Y%m%d-%H%M%S.state", t);
}
