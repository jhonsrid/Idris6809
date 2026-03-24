#ifndef CPU6809_H
#define CPU6809_H

#include <stdint.h>
#include <stdbool.h>

/* Condition code flag bits */
#define CC_C  0x01  /* Carry */
#define CC_V  0x02  /* Overflow */
#define CC_Z  0x04  /* Zero */
#define CC_N  0x08  /* Negative */
#define CC_I  0x10  /* IRQ mask */
#define CC_H  0x20  /* Half carry */
#define CC_F  0x40  /* FIRQ mask */
#define CC_E  0x80  /* Entire state saved */

/* Register identifiers for TFR/EXG and opcode table */
enum {
    REG_D  = 0,
    REG_X  = 1,
    REG_Y  = 2,
    REG_U  = 3,
    REG_S  = 4,
    REG_PC = 5,
    REG_A  = 8,
    REG_B  = 9,
    REG_CC = 10,
    REG_DP = 11
};

typedef struct {
    uint16_t pc;
    uint16_t x, y;
    uint16_t u, s;
    uint16_t d;         /* D register: A is high byte, B is low byte */
    uint8_t  dp;
    uint8_t  cc;

    int      cycles;        /* Cycles consumed by current instruction */
    int      total_cycles;

    bool     nmi_armed;
    bool     nmi_pending;
    bool     firq_pending;
    bool     irq_pending;
    bool     halted;        /* CPU halted by SYNC or CWAI */
    bool     cwai;          /* true if halted by CWAI (state already pushed) */
} CPU6809;

/* A/B accessor inlines */
static inline uint8_t cpu_get_a(const CPU6809 *cpu)
{
    return (uint8_t)(cpu->d >> 8);
}

static inline uint8_t cpu_get_b(const CPU6809 *cpu)
{
    return (uint8_t)(cpu->d & 0xFF);
}

static inline void cpu_set_a(CPU6809 *cpu, uint8_t v)
{
    cpu->d = (cpu->d & 0x00FF) | ((uint16_t)v << 8);
}

static inline void cpu_set_b(CPU6809 *cpu, uint8_t v)
{
    cpu->d = (cpu->d & 0xFF00) | v;
}

/* Public API */
void cpu_init(CPU6809 *cpu);
void cpu_reset(CPU6809 *cpu);
int  cpu_step(CPU6809 *cpu);
void cpu_set_nmi(CPU6809 *cpu, bool state);
void cpu_set_firq(CPU6809 *cpu, bool state);
void cpu_set_irq(CPU6809 *cpu, bool state);
void cpu_dump_state(const CPU6809 *cpu);

#endif
