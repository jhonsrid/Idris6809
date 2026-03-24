#include "cpu6809.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Addressing mode enum
 * ================================================================ */
typedef enum {
    AM_INHERENT,
    AM_IMMEDIATE8,
    AM_IMMEDIATE16,
    AM_DIRECT,
    AM_EXTENDED,
    AM_INDEXED,
    AM_RELATIVE8,
    AM_RELATIVE16
} addr_mode_t;

/* Forward declarations for opcode handlers */
typedef void (*opcode_handler)(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode);

typedef struct {
    opcode_handler handler;
    addr_mode_t    mode;
    uint8_t        base_cycles;
} opcode_entry;

/* ================================================================
 * Internal helpers: fetch, push, pull
 * ================================================================ */
static inline uint8_t fetch_byte(CPU6809 *cpu)
{
    uint8_t val = mem_read(cpu->pc);
    cpu->pc++;
    return val;
}

static inline uint16_t fetch_word(CPU6809 *cpu)
{
    uint8_t hi = mem_read(cpu->pc);
    uint8_t lo = mem_read(cpu->pc + 1);
    cpu->pc += 2;
    return ((uint16_t)hi << 8) | lo;
}

static inline uint16_t read_word(uint16_t addr)
{
    uint8_t hi = mem_read(addr);
    uint8_t lo = mem_read(addr + 1);
    return ((uint16_t)hi << 8) | lo;
}

static inline void write_word(uint16_t addr, uint16_t val)
{
    mem_write(addr, (uint8_t)(val >> 8));
    mem_write(addr + 1, (uint8_t)(val & 0xFF));
}

static inline void push_byte(CPU6809 *cpu, uint16_t *sp, uint8_t val)
{
    (void)cpu;
    (*sp)--;
    mem_write(*sp, val);
}

static inline void push_word(CPU6809 *cpu, uint16_t *sp, uint16_t val)
{
    (void)cpu;
    (*sp)--;
    mem_write(*sp, (uint8_t)(val & 0xFF));
    (*sp)--;
    mem_write(*sp, (uint8_t)(val >> 8));
}

static inline uint8_t pull_byte(CPU6809 *cpu, uint16_t *sp)
{
    (void)cpu;
    uint8_t val = mem_read(*sp);
    (*sp)++;
    return val;
}

static inline uint16_t pull_word(CPU6809 *cpu, uint16_t *sp)
{
    (void)cpu;
    uint8_t hi = mem_read(*sp);
    (*sp)++;
    uint8_t lo = mem_read(*sp);
    (*sp)++;
    return ((uint16_t)hi << 8) | lo;
}

/* ================================================================
 * Flag helpers
 * ================================================================ */
static inline void set_flag(CPU6809 *cpu, uint8_t flag, bool cond)
{
    if (cond)
        cpu->cc |= flag;
    else
        cpu->cc &= ~flag;
}

static inline void set_nz8(CPU6809 *cpu, uint8_t val)
{
    set_flag(cpu, CC_N, val & 0x80);
    set_flag(cpu, CC_Z, val == 0);
}

static inline void set_nz16(CPU6809 *cpu, uint16_t val)
{
    set_flag(cpu, CC_N, val & 0x8000);
    set_flag(cpu, CC_Z, val == 0);
}

/* ================================================================
 * Addressing mode resolution
 * ================================================================ */
static uint16_t *get_index_reg(CPU6809 *cpu, uint8_t rr)
{
    switch (rr) {
    case 0: return &cpu->x;
    case 1: return &cpu->y;
    case 2: return &cpu->u;
    case 3: return &cpu->s;
    }
    return &cpu->x; /* should not happen */
}

static uint16_t resolve_indexed(CPU6809 *cpu)
{
    uint8_t postbyte = fetch_byte(cpu);
    uint16_t ea;
    bool indirect = false;

    if (!(postbyte & 0x80)) {
        /* 5-bit signed offset, no indirect */
        uint16_t *reg = get_index_reg(cpu, (postbyte >> 5) & 0x03);
        int8_t offset = (int8_t)((postbyte & 0x1F) | ((postbyte & 0x10) ? 0xE0 : 0));
        ea = *reg + (int16_t)offset;
        cpu->cycles += 1;
        return ea;
    }

    uint16_t *reg = get_index_reg(cpu, (postbyte >> 5) & 0x03);
    uint8_t mode = postbyte & 0x1F;

    /* Check indirect bit: for modes with bit 4 set */
    if (mode & 0x10) {
        indirect = true;
        mode &= 0x0F; /* strip indirect bit for switch */
    }

    switch (mode) {
    case 0x00: /* ,R+ (no indirect form) */
        ea = *reg;
        (*reg)++;
        cpu->cycles += 2;
        indirect = false; /* no indirect for ,R+ */
        break;
    case 0x01: /* ,R++ */
        ea = *reg;
        (*reg) += 2;
        cpu->cycles += indirect ? 6 : 3;
        break;
    case 0x02: /* ,-R (no indirect form) */
        (*reg)--;
        ea = *reg;
        cpu->cycles += 2;
        indirect = false; /* no indirect for ,-R */
        break;
    case 0x03: /* ,--R */
        (*reg) -= 2;
        ea = *reg;
        cpu->cycles += indirect ? 6 : 3;
        break;
    case 0x04: /* ,R (zero offset) */
        ea = *reg;
        cpu->cycles += indirect ? 3 : 0;
        break;
    case 0x05: /* B,R */
        ea = *reg + (int16_t)(int8_t)cpu_get_b(cpu);
        cpu->cycles += indirect ? 4 : 1;
        break;
    case 0x06: /* A,R */
        ea = *reg + (int16_t)(int8_t)cpu_get_a(cpu);
        cpu->cycles += indirect ? 4 : 1;
        break;
    case 0x08: /* 8-bit offset,R */
        ea = *reg + (int16_t)(int8_t)fetch_byte(cpu);
        cpu->cycles += indirect ? 4 : 1;
        break;
    case 0x09: /* 16-bit offset,R */
        ea = *reg + (int16_t)fetch_word(cpu);
        cpu->cycles += indirect ? 7 : 4;
        break;
    case 0x0B: /* D,R */
        ea = *reg + cpu->d;
        cpu->cycles += indirect ? 7 : 4;
        break;
    case 0x0C: { /* 8-bit offset,PC */
        int8_t off8 = (int8_t)fetch_byte(cpu);
        ea = cpu->pc + (int16_t)off8;
        cpu->cycles += indirect ? 4 : 1;
        break;
    }
    case 0x0D: { /* 16-bit offset,PC */
        int16_t off16 = (int16_t)fetch_word(cpu);
        ea = cpu->pc + off16;
        cpu->cycles += indirect ? 8 : 5;
        break;
    }
    case 0x0F: /* extended indirect [addr] - only valid with indirect bit */
        ea = fetch_word(cpu);
        cpu->cycles += 5;
        indirect = true; /* always indirect */
        break;
    default:
        /* Illegal indexed mode - treat as zero offset */
        ea = *reg;
        break;
    }

    if (indirect) {
        ea = read_word(ea);
    }

    return ea;
}

static uint16_t resolve_ea(CPU6809 *cpu, addr_mode_t mode)
{
    switch (mode) {
    case AM_DIRECT: {
        uint8_t lo = fetch_byte(cpu);
        return ((uint16_t)cpu->dp << 8) | lo;
    }
    case AM_EXTENDED:
        return fetch_word(cpu);
    case AM_INDEXED:
        return resolve_indexed(cpu);
    default:
        return 0; /* Should not be called for other modes */
    }
}

/* ================================================================
 * Instruction handlers
 * ================================================================ */

/* --- Illegal/unimplemented opcode --- */
static void op_illegal(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode;
    fprintf(stderr, "Illegal opcode: $%02X at PC=$%04X\n", opcode, cpu->pc - 1);
}

/* --- NOP --- */
static void op_nop(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)cpu; (void)mode; (void)opcode;
}

/* --- SYNC --- */
static void op_sync(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->halted = true;
    cpu->cwai = false;
}

/* --- DAA --- */
static void op_daa(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t a = cpu_get_a(cpu);
    uint8_t correction = 0;
    bool carry = cpu->cc & CC_C;

    if ((cpu->cc & CC_H) || (a & 0x0F) > 9)
        correction |= 0x06;
    if (carry || a > 0x99 || (a > 0x89 && (a & 0x0F) > 9))
        correction |= 0x60;

    uint16_t result = (uint16_t)a + correction;
    cpu_set_a(cpu, (uint8_t)result);
    set_nz8(cpu, (uint8_t)result);
    if (correction & 0x60)
        cpu->cc |= CC_C;
    /* V is undefined after DAA */
}

/* --- SEX --- */
static void op_sex(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t b = cpu_get_b(cpu);
    cpu_set_a(cpu, (b & 0x80) ? 0xFF : 0x00);
    set_nz16(cpu, cpu->d);
    /* V is cleared per some references, but most say it's not affected.
       The 6809 datasheet: V is not affected. */
}

/* --- ABX --- */
static void op_abx(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->x += cpu_get_b(cpu); /* unsigned addition */
}

/* --- MUL --- */
static void op_mul(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint16_t result = (uint16_t)cpu_get_a(cpu) * (uint16_t)cpu_get_b(cpu);
    cpu->d = result;
    set_flag(cpu, CC_Z, result == 0);
    set_flag(cpu, CC_C, result & 0x0080); /* C = bit 7 of result (B bit 7) */
}

/* --- Branch helpers --- */
static bool eval_branch_cond(CPU6809 *cpu, uint8_t opcode)
{
    /* Branch condition is encoded in bits 3-0 of the opcode */
    uint8_t cond = opcode & 0x0F;
    bool n = (cpu->cc & CC_N) != 0;
    bool z = (cpu->cc & CC_Z) != 0;
    bool v = (cpu->cc & CC_V) != 0;
    bool c = (cpu->cc & CC_C) != 0;

    switch (cond) {
    case 0x0: return true;              /* BRA / LBRA */
    case 0x1: return false;             /* BRN / LBRN */
    case 0x2: return !c && !z;          /* BHI */
    case 0x3: return c || z;            /* BLS */
    case 0x4: return !c;               /* BCC / BHS */
    case 0x5: return c;                /* BCS / BLO */
    case 0x6: return !z;               /* BNE */
    case 0x7: return z;                /* BEQ */
    case 0x8: return !v;               /* BVC */
    case 0x9: return v;                /* BVS */
    case 0xA: return !n;               /* BPL */
    case 0xB: return n;                /* BMI */
    case 0xC: return n == v;            /* BGE */
    case 0xD: return n != v;            /* BLT */
    case 0xE: return !z && (n == v);    /* BGT */
    case 0xF: return z || (n != v);     /* BLE */
    }
    return false;
}

/* --- Short branch (8-bit relative) --- */
static void op_bra8(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode;
    int8_t offset = (int8_t)fetch_byte(cpu);
    if (eval_branch_cond(cpu, opcode)) {
        cpu->pc += (int16_t)offset;
    }
}

/* --- Long branch (16-bit relative, page 2) --- */
static void op_bra16(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode;
    int16_t offset = (int16_t)fetch_word(cpu);
    if (eval_branch_cond(cpu, opcode)) {
        cpu->pc += offset;
        cpu->cycles += 1; /* extra cycle when taken */
    }
}

/* --- LBRA (page 0, $16) --- */
static void op_lbra(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    int16_t offset = (int16_t)fetch_word(cpu);
    cpu->pc += offset;
}

/* --- BSR (page 0, $8D) --- */
static void op_bsr(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    int8_t offset = (int8_t)fetch_byte(cpu);
    push_word(cpu, &cpu->s, cpu->pc);
    cpu->pc += (int16_t)offset;
}

/* --- LBSR (page 0, $17) --- */
static void op_lbsr(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    int16_t offset = (int16_t)fetch_word(cpu);
    push_word(cpu, &cpu->s, cpu->pc);
    cpu->pc += offset;
}

/* --- 8-bit loads --- */
static void op_lda(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t val;
    if (mode == AM_IMMEDIATE8)
        val = fetch_byte(cpu);
    else
        val = mem_read(resolve_ea(cpu, mode));
    cpu_set_a(cpu, val);
    set_nz8(cpu, val);
    cpu->cc &= ~CC_V;
}

static void op_ldb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t val;
    if (mode == AM_IMMEDIATE8)
        val = fetch_byte(cpu);
    else
        val = mem_read(resolve_ea(cpu, mode));
    cpu_set_b(cpu, val);
    set_nz8(cpu, val);
    cpu->cc &= ~CC_V;
}

/* --- 8-bit stores --- */
static void op_sta(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = cpu_get_a(cpu);
    mem_write(ea, val);
    set_nz8(cpu, val);
    cpu->cc &= ~CC_V;
}

static void op_stb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = cpu_get_b(cpu);
    mem_write(ea, val);
    set_nz8(cpu, val);
    cpu->cc &= ~CC_V;
}

/* --- 16-bit loads --- */
static void op_ldd(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t val;
    if (mode == AM_IMMEDIATE16)
        val = fetch_word(cpu);
    else
        val = read_word(resolve_ea(cpu, mode));
    cpu->d = val;
    set_nz16(cpu, val);
    cpu->cc &= ~CC_V;
}

static void op_ldx(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t val;
    if (mode == AM_IMMEDIATE16)
        val = fetch_word(cpu);
    else
        val = read_word(resolve_ea(cpu, mode));
    cpu->x = val;
    set_nz16(cpu, val);
    cpu->cc &= ~CC_V;
}

static void op_ldy(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t val;
    if (mode == AM_IMMEDIATE16)
        val = fetch_word(cpu);
    else
        val = read_word(resolve_ea(cpu, mode));
    cpu->y = val;
    set_nz16(cpu, val);
    cpu->cc &= ~CC_V;
}

static void op_ldu(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t val;
    if (mode == AM_IMMEDIATE16)
        val = fetch_word(cpu);
    else
        val = read_word(resolve_ea(cpu, mode));
    cpu->u = val;
    set_nz16(cpu, val);
    cpu->cc &= ~CC_V;
}

static void op_lds(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t val;
    if (mode == AM_IMMEDIATE16)
        val = fetch_word(cpu);
    else
        val = read_word(resolve_ea(cpu, mode));
    cpu->s = val;
    set_nz16(cpu, val);
    cpu->cc &= ~CC_V;
    cpu->nmi_armed = true; /* Loading S arms NMI */
}

/* --- 16-bit stores --- */
static void op_std(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    write_word(ea, cpu->d);
    set_nz16(cpu, cpu->d);
    cpu->cc &= ~CC_V;
}

static void op_stx(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    write_word(ea, cpu->x);
    set_nz16(cpu, cpu->x);
    cpu->cc &= ~CC_V;
}

static void op_sty(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    write_word(ea, cpu->y);
    set_nz16(cpu, cpu->y);
    cpu->cc &= ~CC_V;
}

static void op_stu(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    write_word(ea, cpu->u);
    set_nz16(cpu, cpu->u);
    cpu->cc &= ~CC_V;
}

static void op_sts(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    write_word(ea, cpu->s);
    set_nz16(cpu, cpu->s);
    cpu->cc &= ~CC_V;
}

/* --- 8-bit ADD --- */
static void op_adda(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t a = cpu_get_a(cpu);
    uint16_t result = (uint16_t)a + operand;
    set_flag(cpu, CC_H, ((a ^ operand ^ result) & 0x10) != 0);
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((a ^ result) & (operand ^ result) & 0x80) != 0);
    cpu_set_a(cpu, (uint8_t)result);
    set_nz8(cpu, (uint8_t)result);
}

static void op_addb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t b = cpu_get_b(cpu);
    uint16_t result = (uint16_t)b + operand;
    set_flag(cpu, CC_H, ((b ^ operand ^ result) & 0x10) != 0);
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((b ^ result) & (operand ^ result) & 0x80) != 0);
    cpu_set_b(cpu, (uint8_t)result);
    set_nz8(cpu, (uint8_t)result);
}

/* --- 8-bit ADC --- */
static void op_adca(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t a = cpu_get_a(cpu);
    uint8_t carry = (cpu->cc & CC_C) ? 1 : 0;
    uint16_t result = (uint16_t)a + operand + carry;
    set_flag(cpu, CC_H, ((a ^ operand ^ result) & 0x10) != 0);
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((a ^ result) & (operand ^ result) & 0x80) != 0);
    cpu_set_a(cpu, (uint8_t)result);
    set_nz8(cpu, (uint8_t)result);
}

static void op_adcb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t b = cpu_get_b(cpu);
    uint8_t carry = (cpu->cc & CC_C) ? 1 : 0;
    uint16_t result = (uint16_t)b + operand + carry;
    set_flag(cpu, CC_H, ((b ^ operand ^ result) & 0x10) != 0);
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((b ^ result) & (operand ^ result) & 0x80) != 0);
    cpu_set_b(cpu, (uint8_t)result);
    set_nz8(cpu, (uint8_t)result);
}

/* --- 8-bit SUB --- */
static void op_suba(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t a = cpu_get_a(cpu);
    uint16_t result = (uint16_t)a - operand;
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((a ^ operand) & (a ^ result) & 0x80) != 0);
    cpu_set_a(cpu, (uint8_t)result);
    set_nz8(cpu, (uint8_t)result);
}

static void op_subb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t b = cpu_get_b(cpu);
    uint16_t result = (uint16_t)b - operand;
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((b ^ operand) & (b ^ result) & 0x80) != 0);
    cpu_set_b(cpu, (uint8_t)result);
    set_nz8(cpu, (uint8_t)result);
}

/* --- 8-bit SBC --- */
static void op_sbca(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t a = cpu_get_a(cpu);
    uint8_t carry = (cpu->cc & CC_C) ? 1 : 0;
    uint16_t result = (uint16_t)a - operand - carry;
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((a ^ operand) & (a ^ result) & 0x80) != 0);
    cpu_set_a(cpu, (uint8_t)result);
    set_nz8(cpu, (uint8_t)result);
}

static void op_sbcb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t b = cpu_get_b(cpu);
    uint8_t carry = (cpu->cc & CC_C) ? 1 : 0;
    uint16_t result = (uint16_t)b - operand - carry;
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((b ^ operand) & (b ^ result) & 0x80) != 0);
    cpu_set_b(cpu, (uint8_t)result);
    set_nz8(cpu, (uint8_t)result);
}

/* --- 8-bit AND --- */
static void op_anda(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t result = cpu_get_a(cpu) & operand;
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
}

static void op_andb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t result = cpu_get_b(cpu) & operand;
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
}

/* --- 8-bit OR --- */
static void op_ora(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t result = cpu_get_a(cpu) | operand;
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
}

static void op_orb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t result = cpu_get_b(cpu) | operand;
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
}

/* --- 8-bit EOR --- */
static void op_eora(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t result = cpu_get_a(cpu) ^ operand;
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
}

static void op_eorb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t result = cpu_get_b(cpu) ^ operand;
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
}

/* --- 8-bit CMP --- */
static void op_cmpa(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t a = cpu_get_a(cpu);
    uint16_t result = (uint16_t)a - operand;
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((a ^ operand) & (a ^ result) & 0x80) != 0);
    set_nz8(cpu, (uint8_t)result);
}

static void op_cmpb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t b = cpu_get_b(cpu);
    uint16_t result = (uint16_t)b - operand;
    set_flag(cpu, CC_C, result & 0x100);
    set_flag(cpu, CC_V, ((b ^ operand) & (b ^ result) & 0x80) != 0);
    set_nz8(cpu, (uint8_t)result);
}

/* --- 8-bit BIT test --- */
static void op_bita(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t result = cpu_get_a(cpu) & operand;
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
}

static void op_bitb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint8_t operand;
    if (mode == AM_IMMEDIATE8)
        operand = fetch_byte(cpu);
    else
        operand = mem_read(resolve_ea(cpu, mode));
    uint8_t result = cpu_get_b(cpu) & operand;
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
}

/* --- 16-bit ADD (ADDD) --- */
static void op_addd(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t operand;
    if (mode == AM_IMMEDIATE16)
        operand = fetch_word(cpu);
    else
        operand = read_word(resolve_ea(cpu, mode));
    uint16_t d = cpu->d;
    uint32_t result = (uint32_t)d + operand;
    set_flag(cpu, CC_C, result & 0x10000);
    set_flag(cpu, CC_V, ((d ^ result) & (operand ^ result) & 0x8000) != 0);
    cpu->d = (uint16_t)result;
    set_nz16(cpu, (uint16_t)result);
}

/* --- 16-bit SUB (SUBD) --- */
static void op_subd(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t operand;
    if (mode == AM_IMMEDIATE16)
        operand = fetch_word(cpu);
    else
        operand = read_word(resolve_ea(cpu, mode));
    uint16_t d = cpu->d;
    uint32_t result = (uint32_t)d - operand;
    set_flag(cpu, CC_C, result & 0x10000);
    set_flag(cpu, CC_V, ((d ^ operand) & (d ^ result) & 0x8000) != 0);
    cpu->d = (uint16_t)result;
    set_nz16(cpu, (uint16_t)result);
}

/* --- 16-bit CMP --- */
static void op_cmpd(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t operand;
    if (mode == AM_IMMEDIATE16)
        operand = fetch_word(cpu);
    else
        operand = read_word(resolve_ea(cpu, mode));
    uint16_t d = cpu->d;
    uint32_t result = (uint32_t)d - operand;
    set_flag(cpu, CC_C, result & 0x10000);
    set_flag(cpu, CC_V, ((d ^ operand) & (d ^ result) & 0x8000) != 0);
    set_nz16(cpu, (uint16_t)result);
}

static void op_cmpx(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t operand;
    if (mode == AM_IMMEDIATE16)
        operand = fetch_word(cpu);
    else
        operand = read_word(resolve_ea(cpu, mode));
    uint16_t x = cpu->x;
    uint32_t result = (uint32_t)x - operand;
    set_flag(cpu, CC_C, result & 0x10000);
    set_flag(cpu, CC_V, ((x ^ operand) & (x ^ result) & 0x8000) != 0);
    set_nz16(cpu, (uint16_t)result);
}

static void op_cmpy(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t operand;
    if (mode == AM_IMMEDIATE16)
        operand = fetch_word(cpu);
    else
        operand = read_word(resolve_ea(cpu, mode));
    uint16_t y = cpu->y;
    uint32_t result = (uint32_t)y - operand;
    set_flag(cpu, CC_C, result & 0x10000);
    set_flag(cpu, CC_V, ((y ^ operand) & (y ^ result) & 0x8000) != 0);
    set_nz16(cpu, (uint16_t)result);
}

static void op_cmpu(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t operand;
    if (mode == AM_IMMEDIATE16)
        operand = fetch_word(cpu);
    else
        operand = read_word(resolve_ea(cpu, mode));
    uint16_t u = cpu->u;
    uint32_t result = (uint32_t)u - operand;
    set_flag(cpu, CC_C, result & 0x10000);
    set_flag(cpu, CC_V, ((u ^ operand) & (u ^ result) & 0x8000) != 0);
    set_nz16(cpu, (uint16_t)result);
}

static void op_cmps(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t operand;
    if (mode == AM_IMMEDIATE16)
        operand = fetch_word(cpu);
    else
        operand = read_word(resolve_ea(cpu, mode));
    uint16_t s = cpu->s;
    uint32_t result = (uint32_t)s - operand;
    set_flag(cpu, CC_C, result & 0x10000);
    set_flag(cpu, CC_V, ((s ^ operand) & (s ^ result) & 0x8000) != 0);
    set_nz16(cpu, (uint16_t)result);
}

/* --- Read-modify-write: NEG --- */
static void op_nega(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_a(cpu);
    uint8_t result = (uint8_t)(0 - val);
    set_flag(cpu, CC_C, val != 0);
    set_flag(cpu, CC_V, val == 0x80);
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
}

static void op_negb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_b(cpu);
    uint8_t result = (uint8_t)(0 - val);
    set_flag(cpu, CC_C, val != 0);
    set_flag(cpu, CC_V, val == 0x80);
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
}

static void op_neg_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = mem_read(ea);
    uint8_t result = (uint8_t)(0 - val);
    set_flag(cpu, CC_C, val != 0);
    set_flag(cpu, CC_V, val == 0x80);
    mem_write(ea, result);
    set_nz8(cpu, result);
}

/* --- COM --- */
static void op_coma(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t result = ~cpu_get_a(cpu);
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
    cpu->cc |= CC_C;
}

static void op_comb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t result = ~cpu_get_b(cpu);
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
    cpu->cc |= CC_C;
}

static void op_com_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t result = ~mem_read(ea);
    mem_write(ea, result);
    set_nz8(cpu, result);
    cpu->cc &= ~CC_V;
    cpu->cc |= CC_C;
}

/* --- INC --- */
static void op_inca(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_a(cpu);
    set_flag(cpu, CC_V, val == 0x7F);
    val++;
    cpu_set_a(cpu, val);
    set_nz8(cpu, val);
}

static void op_incb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_b(cpu);
    set_flag(cpu, CC_V, val == 0x7F);
    val++;
    cpu_set_b(cpu, val);
    set_nz8(cpu, val);
}

static void op_inc_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = mem_read(ea);
    set_flag(cpu, CC_V, val == 0x7F);
    val++;
    mem_write(ea, val);
    set_nz8(cpu, val);
}

/* --- DEC --- */
static void op_deca(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_a(cpu);
    set_flag(cpu, CC_V, val == 0x80);
    val--;
    cpu_set_a(cpu, val);
    set_nz8(cpu, val);
}

static void op_decb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_b(cpu);
    set_flag(cpu, CC_V, val == 0x80);
    val--;
    cpu_set_b(cpu, val);
    set_nz8(cpu, val);
}

static void op_dec_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = mem_read(ea);
    set_flag(cpu, CC_V, val == 0x80);
    val--;
    mem_write(ea, val);
    set_nz8(cpu, val);
}

/* --- CLR --- */
static void op_clra(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu_set_a(cpu, 0);
    cpu->cc &= ~(CC_N | CC_V | CC_C);
    cpu->cc |= CC_Z;
}

static void op_clrb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu_set_b(cpu, 0);
    cpu->cc &= ~(CC_N | CC_V | CC_C);
    cpu->cc |= CC_Z;
}

static void op_clr_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    mem_write(ea, 0);
    cpu->cc &= ~(CC_N | CC_V | CC_C);
    cpu->cc |= CC_Z;
}

/* --- TST --- */
static void op_tsta(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_a(cpu);
    set_nz8(cpu, val);
    cpu->cc &= ~CC_V;
}

static void op_tstb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_b(cpu);
    set_nz8(cpu, val);
    cpu->cc &= ~CC_V;
}

static void op_tst_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = mem_read(ea);
    set_nz8(cpu, val);
    cpu->cc &= ~CC_V;
}

/* --- ASL/LSL --- */
static void op_asla(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_a(cpu);
    set_flag(cpu, CC_C, val & 0x80);
    uint8_t result = val << 1;
    set_flag(cpu, CC_V, ((val ^ result) & 0x80) != 0);
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
}

static void op_aslb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_b(cpu);
    set_flag(cpu, CC_C, val & 0x80);
    uint8_t result = val << 1;
    set_flag(cpu, CC_V, ((val ^ result) & 0x80) != 0);
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
}

static void op_asl_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = mem_read(ea);
    set_flag(cpu, CC_C, val & 0x80);
    uint8_t result = val << 1;
    set_flag(cpu, CC_V, ((val ^ result) & 0x80) != 0);
    mem_write(ea, result);
    set_nz8(cpu, result);
}

/* --- ASR --- */
static void op_asra(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_a(cpu);
    set_flag(cpu, CC_C, val & 0x01);
    uint8_t result = (val >> 1) | (val & 0x80);
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
}

static void op_asrb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_b(cpu);
    set_flag(cpu, CC_C, val & 0x01);
    uint8_t result = (val >> 1) | (val & 0x80);
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
}

static void op_asr_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = mem_read(ea);
    set_flag(cpu, CC_C, val & 0x01);
    uint8_t result = (val >> 1) | (val & 0x80);
    mem_write(ea, result);
    set_nz8(cpu, result);
}

/* --- LSR --- */
static void op_lsra(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_a(cpu);
    set_flag(cpu, CC_C, val & 0x01);
    uint8_t result = val >> 1;
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
}

static void op_lsrb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_b(cpu);
    set_flag(cpu, CC_C, val & 0x01);
    uint8_t result = val >> 1;
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
}

static void op_lsr_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = mem_read(ea);
    set_flag(cpu, CC_C, val & 0x01);
    uint8_t result = val >> 1;
    mem_write(ea, result);
    set_nz8(cpu, result);
}

/* --- ROL --- */
static void op_rola(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_a(cpu);
    uint8_t old_c = (cpu->cc & CC_C) ? 1 : 0;
    set_flag(cpu, CC_C, val & 0x80);
    uint8_t result = (val << 1) | old_c;
    set_flag(cpu, CC_V, ((val ^ result) & 0x80) != 0);
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
}

static void op_rolb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_b(cpu);
    uint8_t old_c = (cpu->cc & CC_C) ? 1 : 0;
    set_flag(cpu, CC_C, val & 0x80);
    uint8_t result = (val << 1) | old_c;
    set_flag(cpu, CC_V, ((val ^ result) & 0x80) != 0);
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
}

static void op_rol_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = mem_read(ea);
    uint8_t old_c = (cpu->cc & CC_C) ? 1 : 0;
    set_flag(cpu, CC_C, val & 0x80);
    uint8_t result = (val << 1) | old_c;
    set_flag(cpu, CC_V, ((val ^ result) & 0x80) != 0);
    mem_write(ea, result);
    set_nz8(cpu, result);
}

/* --- ROR --- */
static void op_rora(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_a(cpu);
    uint8_t old_c = (cpu->cc & CC_C) ? 0x80 : 0;
    set_flag(cpu, CC_C, val & 0x01);
    uint8_t result = (val >> 1) | old_c;
    cpu_set_a(cpu, result);
    set_nz8(cpu, result);
}

static void op_rorb(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t val = cpu_get_b(cpu);
    uint8_t old_c = (cpu->cc & CC_C) ? 0x80 : 0;
    set_flag(cpu, CC_C, val & 0x01);
    uint8_t result = (val >> 1) | old_c;
    cpu_set_b(cpu, result);
    set_nz8(cpu, result);
}

static void op_ror_mem(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    uint8_t val = mem_read(ea);
    uint8_t old_c = (cpu->cc & CC_C) ? 0x80 : 0;
    set_flag(cpu, CC_C, val & 0x01);
    uint8_t result = (val >> 1) | old_c;
    mem_write(ea, result);
    set_nz8(cpu, result);
}

/* --- JMP --- */
static void op_jmp(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    cpu->pc = resolve_ea(cpu, mode);
}

/* --- JSR --- */
static void op_jsr(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    uint16_t ea = resolve_ea(cpu, mode);
    push_word(cpu, &cpu->s, cpu->pc);
    cpu->pc = ea;
}

/* --- RTS --- */
static void op_rts(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->pc = pull_word(cpu, &cpu->s);
}

/* --- LEA instructions --- */
static void op_leax(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    cpu->x = resolve_ea(cpu, mode);
    set_flag(cpu, CC_Z, cpu->x == 0);
}

static void op_leay(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    cpu->y = resolve_ea(cpu, mode);
    set_flag(cpu, CC_Z, cpu->y == 0);
}

static void op_leas(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    cpu->s = resolve_ea(cpu, mode);
}

static void op_leau(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)opcode;
    cpu->u = resolve_ea(cpu, mode);
}

/* --- TFR --- */
static uint16_t read_tfr_reg(CPU6809 *cpu, uint8_t code)
{
    switch (code) {
    case 0x0: return cpu->d;
    case 0x1: return cpu->x;
    case 0x2: return cpu->y;
    case 0x3: return cpu->u;
    case 0x4: return cpu->s;
    case 0x5: return cpu->pc;
    case 0x8: return cpu_get_a(cpu);
    case 0x9: return cpu_get_b(cpu);
    case 0xA: return cpu->cc;
    case 0xB: return cpu->dp;
    default:  return 0xFF; /* undefined */
    }
}

static void write_tfr_reg(CPU6809 *cpu, uint8_t code, uint16_t val)
{
    switch (code) {
    case 0x0: cpu->d = val; break;
    case 0x1: cpu->x = val; break;
    case 0x2: cpu->y = val; break;
    case 0x3: cpu->u = val; break;
    case 0x4: cpu->s = val; break;
    case 0x5: cpu->pc = val; break;
    case 0x8: cpu_set_a(cpu, (uint8_t)val); break;
    case 0x9: cpu_set_b(cpu, (uint8_t)val); break;
    case 0xA: cpu->cc = (uint8_t)val; break;
    case 0xB: cpu->dp = (uint8_t)val; break;
    }
}

static void op_tfr(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t postbyte = fetch_byte(cpu);
    uint8_t src = (postbyte >> 4) & 0x0F;
    uint8_t dst = postbyte & 0x0F;
    uint16_t val = read_tfr_reg(cpu, src);

    /* If transferring 8-bit to 16-bit, the 6809 puts the 8-bit value
       in both the high and low bytes */
    if (src >= 8 && dst < 8)
        val = (val & 0xFF) | ((val & 0xFF) << 8);
    /* If transferring 16-bit to 8-bit, take the low byte */
    if (src < 8 && dst >= 8)
        val = val & 0xFF;

    write_tfr_reg(cpu, dst, val);
}

/* --- EXG --- */
static void op_exg(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t postbyte = fetch_byte(cpu);
    uint8_t r1 = (postbyte >> 4) & 0x0F;
    uint8_t r2 = postbyte & 0x0F;
    uint16_t v1 = read_tfr_reg(cpu, r1);
    uint16_t v2 = read_tfr_reg(cpu, r2);

    /* Handle size mismatch */
    if (r1 >= 8 && r2 < 8) {
        v1 = (v1 & 0xFF) | ((v1 & 0xFF) << 8);
        v2 = v2 & 0xFF;
    } else if (r1 < 8 && r2 >= 8) {
        v2 = (v2 & 0xFF) | ((v2 & 0xFF) << 8);
        v1 = v1 & 0xFF;
    }

    write_tfr_reg(cpu, r1, v2);
    write_tfr_reg(cpu, r2, v1);
}

/* --- PSH / PUL --- */
/*
 * Postbyte bitmask:
 * Bit 0: CC    Bit 4: X
 * Bit 1: A     Bit 5: Y
 * Bit 2: B     Bit 6: S or U
 * Bit 3: DP    Bit 7: PC
 *
 * PSHS pushes to S stack, register bit 6 = U
 * PSHU pushes to U stack, register bit 6 = S
 * Push order: PC, S/U, Y, X, DP, B, A, CC (highest bit first)
 * Pull order: CC, A, B, DP, X, Y, S/U, PC (lowest bit first)
 */
static int count_postbyte_regs(uint8_t postbyte)
{
    int bytes = 0;
    if (postbyte & 0x01) bytes += 1; /* CC */
    if (postbyte & 0x02) bytes += 1; /* A */
    if (postbyte & 0x04) bytes += 1; /* B */
    if (postbyte & 0x08) bytes += 1; /* DP */
    if (postbyte & 0x10) bytes += 2; /* X */
    if (postbyte & 0x20) bytes += 2; /* Y */
    if (postbyte & 0x40) bytes += 2; /* S/U */
    if (postbyte & 0x80) bytes += 2; /* PC */
    return bytes;
}

static void op_pshs(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t postbyte = fetch_byte(cpu);
    cpu->cycles += count_postbyte_regs(postbyte);

    /* Push in order: PC, U, Y, X, DP, B, A, CC */
    if (postbyte & 0x80) push_word(cpu, &cpu->s, cpu->pc);
    if (postbyte & 0x40) push_word(cpu, &cpu->s, cpu->u);
    if (postbyte & 0x20) push_word(cpu, &cpu->s, cpu->y);
    if (postbyte & 0x10) push_word(cpu, &cpu->s, cpu->x);
    if (postbyte & 0x08) push_byte(cpu, &cpu->s, cpu->dp);
    if (postbyte & 0x04) push_byte(cpu, &cpu->s, cpu_get_b(cpu));
    if (postbyte & 0x02) push_byte(cpu, &cpu->s, cpu_get_a(cpu));
    if (postbyte & 0x01) push_byte(cpu, &cpu->s, cpu->cc);
}

static void op_puls(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t postbyte = fetch_byte(cpu);
    cpu->cycles += count_postbyte_regs(postbyte);

    /* Pull in order: CC, A, B, DP, X, Y, U, PC */
    if (postbyte & 0x01) cpu->cc = pull_byte(cpu, &cpu->s);
    if (postbyte & 0x02) cpu_set_a(cpu, pull_byte(cpu, &cpu->s));
    if (postbyte & 0x04) cpu_set_b(cpu, pull_byte(cpu, &cpu->s));
    if (postbyte & 0x08) cpu->dp = pull_byte(cpu, &cpu->s);
    if (postbyte & 0x10) cpu->x = pull_word(cpu, &cpu->s);
    if (postbyte & 0x20) cpu->y = pull_word(cpu, &cpu->s);
    if (postbyte & 0x40) cpu->u = pull_word(cpu, &cpu->s);
    if (postbyte & 0x80) cpu->pc = pull_word(cpu, &cpu->s);
}

static void op_pshu(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t postbyte = fetch_byte(cpu);
    cpu->cycles += count_postbyte_regs(postbyte);

    if (postbyte & 0x80) push_word(cpu, &cpu->u, cpu->pc);
    if (postbyte & 0x40) push_word(cpu, &cpu->u, cpu->s);
    if (postbyte & 0x20) push_word(cpu, &cpu->u, cpu->y);
    if (postbyte & 0x10) push_word(cpu, &cpu->u, cpu->x);
    if (postbyte & 0x08) push_byte(cpu, &cpu->u, cpu->dp);
    if (postbyte & 0x04) push_byte(cpu, &cpu->u, cpu_get_b(cpu));
    if (postbyte & 0x02) push_byte(cpu, &cpu->u, cpu_get_a(cpu));
    if (postbyte & 0x01) push_byte(cpu, &cpu->u, cpu->cc);
}

static void op_pulu(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    uint8_t postbyte = fetch_byte(cpu);
    cpu->cycles += count_postbyte_regs(postbyte);

    if (postbyte & 0x01) cpu->cc = pull_byte(cpu, &cpu->u);
    if (postbyte & 0x02) cpu_set_a(cpu, pull_byte(cpu, &cpu->u));
    if (postbyte & 0x04) cpu_set_b(cpu, pull_byte(cpu, &cpu->u));
    if (postbyte & 0x08) cpu->dp = pull_byte(cpu, &cpu->u);
    if (postbyte & 0x10) cpu->x = pull_word(cpu, &cpu->u);
    if (postbyte & 0x20) cpu->y = pull_word(cpu, &cpu->u);
    if (postbyte & 0x40) cpu->s = pull_word(cpu, &cpu->u);
    if (postbyte & 0x80) cpu->pc = pull_word(cpu, &cpu->u);
}

/* --- ANDCC / ORCC --- */
static void op_andcc(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->cc &= fetch_byte(cpu);
}

static void op_orcc(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->cc |= fetch_byte(cpu);
}

/* ================================================================
 * Push/pull entire state helpers (for interrupts)
 * ================================================================ */
static void push_entire_state(CPU6809 *cpu)
{
    push_word(cpu, &cpu->s, cpu->pc);
    push_word(cpu, &cpu->s, cpu->u);
    push_word(cpu, &cpu->s, cpu->y);
    push_word(cpu, &cpu->s, cpu->x);
    push_byte(cpu, &cpu->s, cpu->dp);
    push_byte(cpu, &cpu->s, cpu_get_b(cpu));
    push_byte(cpu, &cpu->s, cpu_get_a(cpu));
    push_byte(cpu, &cpu->s, cpu->cc);
}

/* --- SWI --- */
static void op_swi(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->cc |= CC_E;
    push_entire_state(cpu);
    cpu->cc |= CC_I | CC_F;
    cpu->pc = read_word(0xFFFA);
}

/* --- SWI2 --- */
static void op_swi2(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->cc |= CC_E;
    push_entire_state(cpu);
    cpu->pc = read_word(0xFFF4);
}

/* --- SWI3 --- */
static void op_swi3(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->cc |= CC_E;
    push_entire_state(cpu);
    cpu->pc = read_word(0xFFF2);
}

/* --- RTI --- */
static void op_rti(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->cc = pull_byte(cpu, &cpu->s);
    if (cpu->cc & CC_E) {
        /* Entire state was saved */
        cpu_set_a(cpu, pull_byte(cpu, &cpu->s));
        cpu_set_b(cpu, pull_byte(cpu, &cpu->s));
        cpu->dp = pull_byte(cpu, &cpu->s);
        cpu->x = pull_word(cpu, &cpu->s);
        cpu->y = pull_word(cpu, &cpu->s);
        cpu->u = pull_word(cpu, &cpu->s);
        cpu->cycles += 9; /* extra cycles for full pull */
    }
    cpu->pc = pull_word(cpu, &cpu->s);
}

/* --- CWAI --- */
static void op_cwai(CPU6809 *cpu, addr_mode_t mode, uint8_t opcode)
{
    (void)mode; (void)opcode;
    cpu->cc &= fetch_byte(cpu);
    cpu->cc |= CC_E;
    push_entire_state(cpu);
    cpu->halted = true;
    cpu->cwai = true;
}

/* ================================================================
 * Opcode tables
 *
 * Page 0: $00–$FF (main opcodes)
 * Page 2: $10 xx  (long branches, LDY/STY, LDS/STS, CMPD, CMPY, SWI2)
 * Page 3: $11 xx  (CMPU, CMPS, SWI3)
 * ================================================================ */

#define OP(handler, am, cycles) { handler, am, cycles }
#define ILLEGAL { op_illegal, AM_INHERENT, 1 }

static const opcode_entry page0_opcodes[256] = {
    /* $00-$0F: Direct RMW / misc */
    OP(op_neg_mem,  AM_DIRECT,    6),  /* 00 NEG direct */
    ILLEGAL,                            /* 01 */
    ILLEGAL,                            /* 02 */
    OP(op_com_mem,  AM_DIRECT,    6),  /* 03 COM direct */
    OP(op_lsr_mem,  AM_DIRECT,    6),  /* 04 LSR direct */
    ILLEGAL,                            /* 05 */
    OP(op_ror_mem,  AM_DIRECT,    6),  /* 06 ROR direct */
    OP(op_asr_mem,  AM_DIRECT,    6),  /* 07 ASR direct */
    OP(op_asl_mem,  AM_DIRECT,    6),  /* 08 ASL/LSL direct */
    OP(op_rol_mem,  AM_DIRECT,    6),  /* 09 ROL direct */
    OP(op_dec_mem,  AM_DIRECT,    6),  /* 0A DEC direct */
    ILLEGAL,                            /* 0B */
    OP(op_inc_mem,  AM_DIRECT,    6),  /* 0C INC direct */
    OP(op_tst_mem,  AM_DIRECT,    6),  /* 0D TST direct */
    OP(op_jmp,      AM_DIRECT,    3),  /* 0E JMP direct */
    OP(op_clr_mem,  AM_DIRECT,    6),  /* 0F CLR direct */

    /* $10-$1F: Page 2 prefix, misc */
    ILLEGAL,                            /* 10 (page 2 prefix, handled in step) */
    ILLEGAL,                            /* 11 (page 3 prefix, handled in step) */
    OP(op_nop,      AM_INHERENT,  2),  /* 12 NOP */
    OP(op_sync,     AM_INHERENT,  4),  /* 13 SYNC */
    ILLEGAL,                            /* 14 */
    ILLEGAL,                            /* 15 */
    OP(op_lbra,     AM_RELATIVE16,5),  /* 16 LBRA */
    OP(op_lbsr,     AM_RELATIVE16,9),  /* 17 LBSR */
    ILLEGAL,                            /* 18 */
    OP(op_daa,      AM_INHERENT,  2),  /* 19 DAA */
    OP(op_orcc,     AM_IMMEDIATE8,3),  /* 1A ORCC immediate */
    ILLEGAL,                            /* 1B */
    OP(op_andcc,    AM_IMMEDIATE8,3),  /* 1C ANDCC immediate */
    OP(op_sex,      AM_INHERENT,  2),  /* 1D SEX */
    OP(op_exg,      AM_INHERENT,  8),  /* 1E EXG */
    OP(op_tfr,      AM_INHERENT,  6),  /* 1F TFR */

    /* $20-$2F: Short branches */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 20 BRA */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 21 BRN */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 22 BHI */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 23 BLS */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 24 BCC/BHS */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 25 BCS/BLO */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 26 BNE */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 27 BEQ */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 28 BVC */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 29 BVS */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 2A BPL */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 2B BMI */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 2C BGE */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 2D BLT */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 2E BGT */
    OP(op_bra8,     AM_RELATIVE8, 3),  /* 2F BLE */

    /* $30-$3F: LEA, PSH, PUL, misc */
    OP(op_leax,     AM_INDEXED,   4),  /* 30 LEAX */
    OP(op_leay,     AM_INDEXED,   4),  /* 31 LEAY */
    OP(op_leas,     AM_INDEXED,   4),  /* 32 LEAS */
    OP(op_leau,     AM_INDEXED,   4),  /* 33 LEAU */
    OP(op_pshs,     AM_INHERENT,  5),  /* 34 PSHS */
    OP(op_puls,     AM_INHERENT,  5),  /* 35 PULS */
    OP(op_pshu,     AM_INHERENT,  5),  /* 36 PSHU */
    OP(op_pulu,     AM_INHERENT,  5),  /* 37 PULU */
    ILLEGAL,                            /* 38 */
    OP(op_rts,      AM_INHERENT,  5),  /* 39 RTS */
    OP(op_abx,      AM_INHERENT,  3),  /* 3A ABX */
    OP(op_rti,      AM_INHERENT,  6),  /* 3B RTI (6 or 15) */
    OP(op_cwai,     AM_INHERENT, 20),  /* 3C CWAI */
    OP(op_mul,      AM_INHERENT, 11),  /* 3D MUL */
    ILLEGAL,                            /* 3E */
    OP(op_swi,      AM_INHERENT, 19),  /* 3F SWI */

    /* $40-$4F: Inherent A register */
    OP(op_nega,     AM_INHERENT,  2),  /* 40 NEGA */
    ILLEGAL,                            /* 41 */
    ILLEGAL,                            /* 42 */
    OP(op_coma,     AM_INHERENT,  2),  /* 43 COMA */
    OP(op_lsra,     AM_INHERENT,  2),  /* 44 LSRA */
    ILLEGAL,                            /* 45 */
    OP(op_rora,     AM_INHERENT,  2),  /* 46 RORA */
    OP(op_asra,     AM_INHERENT,  2),  /* 47 ASRA */
    OP(op_asla,     AM_INHERENT,  2),  /* 48 ASLA/LSLA */
    OP(op_rola,     AM_INHERENT,  2),  /* 49 ROLA */
    OP(op_deca,     AM_INHERENT,  2),  /* 4A DECA */
    ILLEGAL,                            /* 4B */
    OP(op_inca,     AM_INHERENT,  2),  /* 4C INCA */
    OP(op_tsta,     AM_INHERENT,  2),  /* 4D TSTA */
    ILLEGAL,                            /* 4E */
    OP(op_clra,     AM_INHERENT,  2),  /* 4F CLRA */

    /* $50-$5F: Inherent B register */
    OP(op_negb,     AM_INHERENT,  2),  /* 50 NEGB */
    ILLEGAL,                            /* 51 */
    ILLEGAL,                            /* 52 */
    OP(op_comb,     AM_INHERENT,  2),  /* 53 COMB */
    OP(op_lsrb,     AM_INHERENT,  2),  /* 54 LSRB */
    ILLEGAL,                            /* 55 */
    OP(op_rorb,     AM_INHERENT,  2),  /* 56 RORB */
    OP(op_asrb,     AM_INHERENT,  2),  /* 57 ASRB */
    OP(op_aslb,     AM_INHERENT,  2),  /* 58 ASLB/LSLB */
    OP(op_rolb,     AM_INHERENT,  2),  /* 59 ROLB */
    OP(op_decb,     AM_INHERENT,  2),  /* 5A DECB */
    ILLEGAL,                            /* 5B */
    OP(op_incb,     AM_INHERENT,  2),  /* 5C INCB */
    OP(op_tstb,     AM_INHERENT,  2),  /* 5D TSTB */
    ILLEGAL,                            /* 5E */
    OP(op_clrb,     AM_INHERENT,  2),  /* 5F CLRB */

    /* $60-$6F: Indexed RMW */
    OP(op_neg_mem,  AM_INDEXED,   6),  /* 60 NEG indexed */
    ILLEGAL,                            /* 61 */
    ILLEGAL,                            /* 62 */
    OP(op_com_mem,  AM_INDEXED,   6),  /* 63 COM indexed */
    OP(op_lsr_mem,  AM_INDEXED,   6),  /* 64 LSR indexed */
    ILLEGAL,                            /* 65 */
    OP(op_ror_mem,  AM_INDEXED,   6),  /* 66 ROR indexed */
    OP(op_asr_mem,  AM_INDEXED,   6),  /* 67 ASR indexed */
    OP(op_asl_mem,  AM_INDEXED,   6),  /* 68 ASL/LSL indexed */
    OP(op_rol_mem,  AM_INDEXED,   6),  /* 69 ROL indexed */
    OP(op_dec_mem,  AM_INDEXED,   6),  /* 6A DEC indexed */
    ILLEGAL,                            /* 6B */
    OP(op_inc_mem,  AM_INDEXED,   6),  /* 6C INC indexed */
    OP(op_tst_mem,  AM_INDEXED,   6),  /* 6D TST indexed */
    OP(op_jmp,      AM_INDEXED,   3),  /* 6E JMP indexed */
    OP(op_clr_mem,  AM_INDEXED,   6),  /* 6F CLR indexed */

    /* $70-$7F: Extended RMW */
    OP(op_neg_mem,  AM_EXTENDED,  7),  /* 70 NEG extended */
    ILLEGAL,                            /* 71 */
    ILLEGAL,                            /* 72 */
    OP(op_com_mem,  AM_EXTENDED,  7),  /* 73 COM extended */
    OP(op_lsr_mem,  AM_EXTENDED,  7),  /* 74 LSR extended */
    ILLEGAL,                            /* 75 */
    OP(op_ror_mem,  AM_EXTENDED,  7),  /* 76 ROR extended */
    OP(op_asr_mem,  AM_EXTENDED,  7),  /* 77 ASR extended */
    OP(op_asl_mem,  AM_EXTENDED,  7),  /* 78 ASL/LSL extended */
    OP(op_rol_mem,  AM_EXTENDED,  7),  /* 79 ROL extended */
    OP(op_dec_mem,  AM_EXTENDED,  7),  /* 7A DEC extended */
    ILLEGAL,                            /* 7B */
    OP(op_inc_mem,  AM_EXTENDED,  7),  /* 7C INC extended */
    OP(op_tst_mem,  AM_EXTENDED,  7),  /* 7D TST extended */
    OP(op_jmp,      AM_EXTENDED,  4),  /* 7E JMP extended */
    OP(op_clr_mem,  AM_EXTENDED,  7),  /* 7F CLR extended */

    /* $80-$8F: Immediate A / 16-bit */
    OP(op_suba,     AM_IMMEDIATE8, 2),  /* 80 SUBA imm */
    OP(op_cmpa,     AM_IMMEDIATE8, 2),  /* 81 CMPA imm */
    OP(op_sbca,     AM_IMMEDIATE8, 2),  /* 82 SBCA imm */
    OP(op_subd,     AM_IMMEDIATE16,4),  /* 83 SUBD imm */
    OP(op_anda,     AM_IMMEDIATE8, 2),  /* 84 ANDA imm */
    OP(op_bita,     AM_IMMEDIATE8, 2),  /* 85 BITA imm */
    OP(op_lda,      AM_IMMEDIATE8, 2),  /* 86 LDA imm */
    ILLEGAL,                             /* 87 (STA imm - illegal) */
    OP(op_eora,     AM_IMMEDIATE8, 2),  /* 88 EORA imm */
    OP(op_adca,     AM_IMMEDIATE8, 2),  /* 89 ADCA imm */
    OP(op_ora,      AM_IMMEDIATE8, 2),  /* 8A ORA imm */
    OP(op_adda,     AM_IMMEDIATE8, 2),  /* 8B ADDA imm */
    OP(op_cmpx,     AM_IMMEDIATE16,4),  /* 8C CMPX imm */
    OP(op_bsr,      AM_RELATIVE8,  7),  /* 8D BSR */
    OP(op_ldx,      AM_IMMEDIATE16,3),  /* 8E LDX imm */
    ILLEGAL,                             /* 8F (STX imm - illegal) */

    /* $90-$9F: Direct A / 16-bit */
    OP(op_suba,     AM_DIRECT,    4),  /* 90 SUBA direct */
    OP(op_cmpa,     AM_DIRECT,    4),  /* 91 CMPA direct */
    OP(op_sbca,     AM_DIRECT,    4),  /* 92 SBCA direct */
    OP(op_subd,     AM_DIRECT,    6),  /* 93 SUBD direct */
    OP(op_anda,     AM_DIRECT,    4),  /* 94 ANDA direct */
    OP(op_bita,     AM_DIRECT,    4),  /* 95 BITA direct */
    OP(op_lda,      AM_DIRECT,    4),  /* 96 LDA direct */
    OP(op_sta,      AM_DIRECT,    4),  /* 97 STA direct */
    OP(op_eora,     AM_DIRECT,    4),  /* 98 EORA direct */
    OP(op_adca,     AM_DIRECT,    4),  /* 99 ADCA direct */
    OP(op_ora,      AM_DIRECT,    4),  /* 9A ORA direct */
    OP(op_adda,     AM_DIRECT,    4),  /* 9B ADDA direct */
    OP(op_cmpx,     AM_DIRECT,    6),  /* 9C CMPX direct */
    OP(op_jsr,      AM_DIRECT,    7),  /* 9D JSR direct */
    OP(op_ldx,      AM_DIRECT,    5),  /* 9E LDX direct */
    OP(op_stx,      AM_DIRECT,    5),  /* 9F STX direct */

    /* $A0-$AF: Indexed A / 16-bit */
    OP(op_suba,     AM_INDEXED,   4),  /* A0 SUBA indexed */
    OP(op_cmpa,     AM_INDEXED,   4),  /* A1 CMPA indexed */
    OP(op_sbca,     AM_INDEXED,   4),  /* A2 SBCA indexed */
    OP(op_subd,     AM_INDEXED,   6),  /* A3 SUBD indexed */
    OP(op_anda,     AM_INDEXED,   4),  /* A4 ANDA indexed */
    OP(op_bita,     AM_INDEXED,   4),  /* A5 BITA indexed */
    OP(op_lda,      AM_INDEXED,   4),  /* A6 LDA indexed */
    OP(op_sta,      AM_INDEXED,   4),  /* A7 STA indexed */
    OP(op_eora,     AM_INDEXED,   4),  /* A8 EORA indexed */
    OP(op_adca,     AM_INDEXED,   4),  /* A9 ADCA indexed */
    OP(op_ora,      AM_INDEXED,   4),  /* AA ORA indexed */
    OP(op_adda,     AM_INDEXED,   4),  /* AB ADDA indexed */
    OP(op_cmpx,     AM_INDEXED,   6),  /* AC CMPX indexed */
    OP(op_jsr,      AM_INDEXED,   7),  /* AD JSR indexed */
    OP(op_ldx,      AM_INDEXED,   5),  /* AE LDX indexed */
    OP(op_stx,      AM_INDEXED,   5),  /* AF STX indexed */

    /* $B0-$BF: Extended A / 16-bit */
    OP(op_suba,     AM_EXTENDED,  5),  /* B0 SUBA extended */
    OP(op_cmpa,     AM_EXTENDED,  5),  /* B1 CMPA extended */
    OP(op_sbca,     AM_EXTENDED,  5),  /* B2 SBCA extended */
    OP(op_subd,     AM_EXTENDED,  7),  /* B3 SUBD extended */
    OP(op_anda,     AM_EXTENDED,  5),  /* B4 ANDA extended */
    OP(op_bita,     AM_EXTENDED,  5),  /* B5 BITA extended */
    OP(op_lda,      AM_EXTENDED,  5),  /* B6 LDA extended */
    OP(op_sta,      AM_EXTENDED,  5),  /* B7 STA extended */
    OP(op_eora,     AM_EXTENDED,  5),  /* B8 EORA extended */
    OP(op_adca,     AM_EXTENDED,  5),  /* B9 ADCA extended */
    OP(op_ora,      AM_EXTENDED,  5),  /* BA ORA extended */
    OP(op_adda,     AM_EXTENDED,  5),  /* BB ADDA extended */
    OP(op_cmpx,     AM_EXTENDED,  7),  /* BC CMPX extended */
    OP(op_jsr,      AM_EXTENDED,  8),  /* BD JSR extended */
    OP(op_ldx,      AM_EXTENDED,  6),  /* BE LDX extended */
    OP(op_stx,      AM_EXTENDED,  6),  /* BF STX extended */

    /* $C0-$CF: Immediate B / 16-bit */
    OP(op_subb,     AM_IMMEDIATE8, 2),  /* C0 SUBB imm */
    OP(op_cmpb,     AM_IMMEDIATE8, 2),  /* C1 CMPB imm */
    OP(op_sbcb,     AM_IMMEDIATE8, 2),  /* C2 SBCB imm */
    OP(op_addd,     AM_IMMEDIATE16,4),  /* C3 ADDD imm */
    OP(op_andb,     AM_IMMEDIATE8, 2),  /* C4 ANDB imm */
    OP(op_bitb,     AM_IMMEDIATE8, 2),  /* C5 BITB imm */
    OP(op_ldb,      AM_IMMEDIATE8, 2),  /* C6 LDB imm */
    ILLEGAL,                             /* C7 (STB imm - illegal) */
    OP(op_eorb,     AM_IMMEDIATE8, 2),  /* C8 EORB imm */
    OP(op_adcb,     AM_IMMEDIATE8, 2),  /* C9 ADCB imm */
    OP(op_orb,      AM_IMMEDIATE8, 2),  /* CA ORB imm */
    OP(op_addb,     AM_IMMEDIATE8, 2),  /* CB ADDB imm */
    OP(op_ldd,      AM_IMMEDIATE16,3),  /* CC LDD imm */
    ILLEGAL,                             /* CD (STD imm - illegal) */
    OP(op_ldu,      AM_IMMEDIATE16,3),  /* CE LDU imm */
    ILLEGAL,                             /* CF (STU imm - illegal) */

    /* $D0-$DF: Direct B / 16-bit */
    OP(op_subb,     AM_DIRECT,    4),  /* D0 SUBB direct */
    OP(op_cmpb,     AM_DIRECT,    4),  /* D1 CMPB direct */
    OP(op_sbcb,     AM_DIRECT,    4),  /* D2 SBCB direct */
    OP(op_addd,     AM_DIRECT,    6),  /* D3 ADDD direct */
    OP(op_andb,     AM_DIRECT,    4),  /* D4 ANDB direct */
    OP(op_bitb,     AM_DIRECT,    4),  /* D5 BITB direct */
    OP(op_ldb,      AM_DIRECT,    4),  /* D6 LDB direct */
    OP(op_stb,      AM_DIRECT,    4),  /* D7 STB direct */
    OP(op_eorb,     AM_DIRECT,    4),  /* D8 EORB direct */
    OP(op_adcb,     AM_DIRECT,    4),  /* D9 ADCB direct */
    OP(op_orb,      AM_DIRECT,    4),  /* DA ORB direct */
    OP(op_addb,     AM_DIRECT,    4),  /* DB ADDB direct */
    OP(op_ldd,      AM_DIRECT,    5),  /* DC LDD direct */
    OP(op_std,      AM_DIRECT,    5),  /* DD STD direct */
    OP(op_ldu,      AM_DIRECT,    5),  /* DE LDU direct */
    OP(op_stu,      AM_DIRECT,    5),  /* DF STU direct */

    /* $E0-$EF: Indexed B / 16-bit */
    OP(op_subb,     AM_INDEXED,   4),  /* E0 SUBB indexed */
    OP(op_cmpb,     AM_INDEXED,   4),  /* E1 CMPB indexed */
    OP(op_sbcb,     AM_INDEXED,   4),  /* E2 SBCB indexed */
    OP(op_addd,     AM_INDEXED,   6),  /* E3 ADDD indexed */
    OP(op_andb,     AM_INDEXED,   4),  /* E4 ANDB indexed */
    OP(op_bitb,     AM_INDEXED,   4),  /* E5 BITB indexed */
    OP(op_ldb,      AM_INDEXED,   4),  /* E6 LDB indexed */
    OP(op_stb,      AM_INDEXED,   4),  /* E7 STB indexed */
    OP(op_eorb,     AM_INDEXED,   4),  /* E8 EORB indexed */
    OP(op_adcb,     AM_INDEXED,   4),  /* E9 ADCB indexed */
    OP(op_orb,      AM_INDEXED,   4),  /* EA ORB indexed */
    OP(op_addb,     AM_INDEXED,   4),  /* EB ADDB indexed */
    OP(op_ldd,      AM_INDEXED,   5),  /* EC LDD indexed */
    OP(op_std,      AM_INDEXED,   5),  /* ED STD indexed */
    OP(op_ldu,      AM_INDEXED,   5),  /* EE LDU indexed */
    OP(op_stu,      AM_INDEXED,   5),  /* EF STU indexed */

    /* $F0-$FF: Extended B / 16-bit */
    OP(op_subb,     AM_EXTENDED,  5),  /* F0 SUBB extended */
    OP(op_cmpb,     AM_EXTENDED,  5),  /* F1 CMPB extended */
    OP(op_sbcb,     AM_EXTENDED,  5),  /* F2 SBCB extended */
    OP(op_addd,     AM_EXTENDED,  7),  /* F3 ADDD extended */
    OP(op_andb,     AM_EXTENDED,  5),  /* F4 ANDB extended */
    OP(op_bitb,     AM_EXTENDED,  5),  /* F5 BITB extended */
    OP(op_ldb,      AM_EXTENDED,  5),  /* F6 LDB extended */
    OP(op_stb,      AM_EXTENDED,  5),  /* F7 STB extended */
    OP(op_eorb,     AM_EXTENDED,  5),  /* F8 EORB extended */
    OP(op_adcb,     AM_EXTENDED,  5),  /* F9 ADCB extended */
    OP(op_orb,      AM_EXTENDED,  5),  /* FA ORB extended */
    OP(op_addb,     AM_EXTENDED,  5),  /* FB ADDB extended */
    OP(op_ldd,      AM_EXTENDED,  6),  /* FC LDD extended */
    OP(op_std,      AM_EXTENDED,  6),  /* FD STD extended */
    OP(op_ldu,      AM_EXTENDED,  6),  /* FE LDU extended */
    OP(op_stu,      AM_EXTENDED,  6),  /* FF STU extended */
};

/* Page 2 opcodes (prefix $10) */
static const opcode_entry page2_opcodes[256] = {
    /* $00-$1F: all illegal */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,

    /* $20-$2F: Long branches */
    ILLEGAL,                                 /* 10 20 (LBRA is on page 0) */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 21 LBRN */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 22 LBHI */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 23 LBLS */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 24 LBCC/LBHS */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 25 LBCS/LBLO */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 26 LBNE */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 27 LBEQ */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 28 LBVC */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 29 LBVS */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 2A LBPL */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 2B LBMI */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 2C LBGE */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 2D LBLT */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 2E LBGT */
    OP(op_bra16,    AM_RELATIVE16, 5),      /* 10 2F LBLE */

    /* $30-$3F */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    OP(op_swi2,     AM_INHERENT,  20),      /* 10 3F SWI2 */

    /* $40-$7F: all illegal */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,

    /* $80-$8F */
    ILLEGAL,                                 /* 10 80 */
    ILLEGAL,                                 /* 10 81 */
    ILLEGAL,                                 /* 10 82 */
    OP(op_cmpd,     AM_IMMEDIATE16, 5),     /* 10 83 CMPD imm */
    ILLEGAL,                                 /* 10 84 */
    ILLEGAL,                                 /* 10 85 */
    ILLEGAL,                                 /* 10 86 */
    ILLEGAL,                                 /* 10 87 */
    ILLEGAL,                                 /* 10 88 */
    ILLEGAL,                                 /* 10 89 */
    ILLEGAL,                                 /* 10 8A */
    ILLEGAL,                                 /* 10 8B */
    OP(op_cmpy,     AM_IMMEDIATE16, 5),     /* 10 8C CMPY imm */
    ILLEGAL,                                 /* 10 8D */
    OP(op_ldy,      AM_IMMEDIATE16, 4),     /* 10 8E LDY imm */
    ILLEGAL,                                 /* 10 8F */

    /* $90-$9F */
    ILLEGAL,                                 /* 10 90 */
    ILLEGAL,                                 /* 10 91 */
    ILLEGAL,                                 /* 10 92 */
    OP(op_cmpd,     AM_DIRECT,     7),      /* 10 93 CMPD direct */
    ILLEGAL,                                 /* 10 94 */
    ILLEGAL,                                 /* 10 95 */
    ILLEGAL,                                 /* 10 96 */
    ILLEGAL,                                 /* 10 97 */
    ILLEGAL,                                 /* 10 98 */
    ILLEGAL,                                 /* 10 99 */
    ILLEGAL,                                 /* 10 9A */
    ILLEGAL,                                 /* 10 9B */
    OP(op_cmpy,     AM_DIRECT,     7),      /* 10 9C CMPY direct */
    ILLEGAL,                                 /* 10 9D */
    OP(op_ldy,      AM_DIRECT,     6),      /* 10 9E LDY direct */
    OP(op_sty,      AM_DIRECT,     6),      /* 10 9F STY direct */

    /* $A0-$AF */
    ILLEGAL,                                 /* 10 A0 */
    ILLEGAL,                                 /* 10 A1 */
    ILLEGAL,                                 /* 10 A2 */
    OP(op_cmpd,     AM_INDEXED,    7),      /* 10 A3 CMPD indexed */
    ILLEGAL,                                 /* 10 A4 */
    ILLEGAL,                                 /* 10 A5 */
    ILLEGAL,                                 /* 10 A6 */
    ILLEGAL,                                 /* 10 A7 */
    ILLEGAL,                                 /* 10 A8 */
    ILLEGAL,                                 /* 10 A9 */
    ILLEGAL,                                 /* 10 AA */
    ILLEGAL,                                 /* 10 AB */
    OP(op_cmpy,     AM_INDEXED,    7),      /* 10 AC CMPY indexed */
    ILLEGAL,                                 /* 10 AD */
    OP(op_ldy,      AM_INDEXED,    6),      /* 10 AE LDY indexed */
    OP(op_sty,      AM_INDEXED,    6),      /* 10 AF STY indexed */

    /* $B0-$BF */
    ILLEGAL,                                 /* 10 B0 */
    ILLEGAL,                                 /* 10 B1 */
    ILLEGAL,                                 /* 10 B2 */
    OP(op_cmpd,     AM_EXTENDED,   8),      /* 10 B3 CMPD extended */
    ILLEGAL,                                 /* 10 B4 */
    ILLEGAL,                                 /* 10 B5 */
    ILLEGAL,                                 /* 10 B6 */
    ILLEGAL,                                 /* 10 B7 */
    ILLEGAL,                                 /* 10 B8 */
    ILLEGAL,                                 /* 10 B9 */
    ILLEGAL,                                 /* 10 BA */
    ILLEGAL,                                 /* 10 BB */
    OP(op_cmpy,     AM_EXTENDED,   8),      /* 10 BC CMPY extended */
    ILLEGAL,                                 /* 10 BD */
    OP(op_ldy,      AM_EXTENDED,   7),      /* 10 BE LDY extended */
    OP(op_sty,      AM_EXTENDED,   7),      /* 10 BF STY extended */

    /* $C0-$CF */
    ILLEGAL,                                 /* 10 C0 */
    ILLEGAL,                                 /* 10 C1 */
    ILLEGAL,                                 /* 10 C2 */
    ILLEGAL,                                 /* 10 C3 */
    ILLEGAL,                                 /* 10 C4 */
    ILLEGAL,                                 /* 10 C5 */
    ILLEGAL,                                 /* 10 C6 */
    ILLEGAL,                                 /* 10 C7 */
    ILLEGAL,                                 /* 10 C8 */
    ILLEGAL,                                 /* 10 C9 */
    ILLEGAL,                                 /* 10 CA */
    ILLEGAL,                                 /* 10 CB */
    ILLEGAL,                                 /* 10 CC */
    ILLEGAL,                                 /* 10 CD */
    OP(op_lds,      AM_IMMEDIATE16, 4),     /* 10 CE LDS imm */
    ILLEGAL,                                 /* 10 CF */

    /* $D0-$DF */
    ILLEGAL,                                 /* 10 D0 */
    ILLEGAL,                                 /* 10 D1 */
    ILLEGAL,                                 /* 10 D2 */
    ILLEGAL,                                 /* 10 D3 */
    ILLEGAL,                                 /* 10 D4 */
    ILLEGAL,                                 /* 10 D5 */
    ILLEGAL,                                 /* 10 D6 */
    ILLEGAL,                                 /* 10 D7 */
    ILLEGAL,                                 /* 10 D8 */
    ILLEGAL,                                 /* 10 D9 */
    ILLEGAL,                                 /* 10 DA */
    ILLEGAL,                                 /* 10 DB */
    ILLEGAL,                                 /* 10 DC */
    ILLEGAL,                                 /* 10 DD */
    OP(op_lds,      AM_DIRECT,     6),      /* 10 DE LDS direct */
    OP(op_sts,      AM_DIRECT,     6),      /* 10 DF STS direct */

    /* $E0-$EF */
    ILLEGAL,                                 /* 10 E0 */
    ILLEGAL,                                 /* 10 E1 */
    ILLEGAL,                                 /* 10 E2 */
    ILLEGAL,                                 /* 10 E3 */
    ILLEGAL,                                 /* 10 E4 */
    ILLEGAL,                                 /* 10 E5 */
    ILLEGAL,                                 /* 10 E6 */
    ILLEGAL,                                 /* 10 E7 */
    ILLEGAL,                                 /* 10 E8 */
    ILLEGAL,                                 /* 10 E9 */
    ILLEGAL,                                 /* 10 EA */
    ILLEGAL,                                 /* 10 EB */
    ILLEGAL,                                 /* 10 EC */
    ILLEGAL,                                 /* 10 ED */
    OP(op_lds,      AM_INDEXED,    6),      /* 10 EE LDS indexed */
    OP(op_sts,      AM_INDEXED,    6),      /* 10 EF STS indexed */

    /* $F0-$FF */
    ILLEGAL,                                 /* 10 F0 */
    ILLEGAL,                                 /* 10 F1 */
    ILLEGAL,                                 /* 10 F2 */
    ILLEGAL,                                 /* 10 F3 */
    ILLEGAL,                                 /* 10 F4 */
    ILLEGAL,                                 /* 10 F5 */
    ILLEGAL,                                 /* 10 F6 */
    ILLEGAL,                                 /* 10 F7 */
    ILLEGAL,                                 /* 10 F8 */
    ILLEGAL,                                 /* 10 F9 */
    ILLEGAL,                                 /* 10 FA */
    ILLEGAL,                                 /* 10 FB */
    ILLEGAL,                                 /* 10 FC */
    ILLEGAL,                                 /* 10 FD */
    OP(op_lds,      AM_EXTENDED,   7),      /* 10 FE LDS extended */
    OP(op_sts,      AM_EXTENDED,   7),      /* 10 FF STS extended */
};

/* Page 3 opcodes (prefix $11) */
static const opcode_entry page3_opcodes[256] = {
    /* $00-$3F: all illegal */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    OP(op_swi3,     AM_INHERENT,  20),      /* 11 3F SWI3 */

    /* $40-$7F: all illegal */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,

    /* $80-$8F */
    ILLEGAL,                                 /* 11 80 */
    ILLEGAL,                                 /* 11 81 */
    ILLEGAL,                                 /* 11 82 */
    OP(op_cmpu,     AM_IMMEDIATE16, 5),     /* 11 83 CMPU imm */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    OP(op_cmps,     AM_IMMEDIATE16, 5),     /* 11 8C CMPS imm */
    ILLEGAL, ILLEGAL, ILLEGAL,

    /* $90-$9F */
    ILLEGAL,                                 /* 11 90 */
    ILLEGAL,                                 /* 11 91 */
    ILLEGAL,                                 /* 11 92 */
    OP(op_cmpu,     AM_DIRECT,     7),      /* 11 93 CMPU direct */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    OP(op_cmps,     AM_DIRECT,     7),      /* 11 9C CMPS direct */
    ILLEGAL, ILLEGAL, ILLEGAL,

    /* $A0-$AF */
    ILLEGAL,                                 /* 11 A0 */
    ILLEGAL,                                 /* 11 A1 */
    ILLEGAL,                                 /* 11 A2 */
    OP(op_cmpu,     AM_INDEXED,    7),      /* 11 A3 CMPU indexed */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    OP(op_cmps,     AM_INDEXED,    7),      /* 11 AC CMPS indexed */
    ILLEGAL, ILLEGAL, ILLEGAL,

    /* $B0-$BF */
    ILLEGAL,                                 /* 11 B0 */
    ILLEGAL,                                 /* 11 B1 */
    ILLEGAL,                                 /* 11 B2 */
    OP(op_cmpu,     AM_EXTENDED,   8),      /* 11 B3 CMPU extended */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    OP(op_cmps,     AM_EXTENDED,   8),      /* 11 BC CMPS extended */
    ILLEGAL, ILLEGAL, ILLEGAL,

    /* $C0-$FF: all illegal */
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
    ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL, ILLEGAL,
};

#undef OP
#undef ILLEGAL

/* ================================================================
 * Interrupt handling
 * ================================================================ */
static void handle_nmi(CPU6809 *cpu)
{
    if (!cpu->cwai) {
        cpu->cc |= CC_E;
        push_entire_state(cpu);
        cpu->cycles += 19;
    } else {
        cpu->halted = false;
        cpu->cwai = false;
        cpu->cycles += 7;
    }
    cpu->cc |= CC_I | CC_F;
    cpu->pc = read_word(0xFFFC);
    cpu->nmi_pending = false;
}

static void handle_firq(CPU6809 *cpu)
{
    if (!cpu->cwai) {
        cpu->cc &= ~CC_E; /* Only CC and PC saved */
        push_word(cpu, &cpu->s, cpu->pc);
        push_byte(cpu, &cpu->s, cpu->cc);
        cpu->cycles += 10;
    } else {
        cpu->halted = false;
        cpu->cwai = false;
        cpu->cycles += 7;
    }
    cpu->cc |= CC_I | CC_F;
    cpu->pc = read_word(0xFFF6);
}

static void handle_irq(CPU6809 *cpu)
{
    if (!cpu->cwai) {
        cpu->cc |= CC_E;
        push_entire_state(cpu);
        cpu->cycles += 19;
    } else {
        cpu->halted = false;
        cpu->cwai = false;
        cpu->cycles += 7;
    }
    cpu->cc |= CC_I;
    cpu->pc = read_word(0xFFF8);
}

/* ================================================================
 * Main step function
 * ================================================================ */
int cpu_step(CPU6809 *cpu)
{
    cpu->cycles = 0;

    /* Check interrupts */
    if (cpu->nmi_pending && cpu->nmi_armed) {
        handle_nmi(cpu);
        cpu->total_cycles += cpu->cycles;
        return cpu->cycles;
    }
    if (cpu->firq_pending && !(cpu->cc & CC_F)) {
        handle_firq(cpu);
        cpu->total_cycles += cpu->cycles;
        return cpu->cycles;
    }
    if (cpu->irq_pending && !(cpu->cc & CC_I)) {
        handle_irq(cpu);
        cpu->total_cycles += cpu->cycles;
        return cpu->cycles;
    }

    /* If halted (SYNC/CWAI), consume 1 cycle */
    if (cpu->halted) {
        cpu->cycles = 1;
        cpu->total_cycles += 1;
        return 1;
    }

    /* Fetch and decode */
    uint8_t opcode = fetch_byte(cpu);
    const opcode_entry *entry;

    if (opcode == 0x10) {
        opcode = fetch_byte(cpu);
        entry = &page2_opcodes[opcode];
    } else if (opcode == 0x11) {
        opcode = fetch_byte(cpu);
        entry = &page3_opcodes[opcode];
    } else {
        entry = &page0_opcodes[opcode];
    }

    cpu->cycles += entry->base_cycles;
    entry->handler(cpu, entry->mode, opcode);

    cpu->total_cycles += cpu->cycles;
    return cpu->cycles;
}

/* ================================================================
 * Public API
 * ================================================================ */
void cpu_init(CPU6809 *cpu)
{
    memset(cpu, 0, sizeof(*cpu));
    /* S and U default to top of low RAM so JSR before LDS doesn't
     * wrap into the ROM vector area (Dragon 64 ROM does this). */
    cpu->s = 0x7F00;
    cpu->u = 0x7F00;
}

void cpu_reset(CPU6809 *cpu)
{
    /* MC6809 RESET only defines DP, CC, and PC.
     * All other registers are undefined. Set them to safe defaults
     * rather than 0 (S=0 would wrap into the vector area on first JSR). */
    cpu->dp = 0x00;
    cpu->cc = CC_F | CC_I; /* Interrupts masked */
    cpu->halted = false;
    cpu->cwai = false;
    cpu->nmi_armed = false;
    cpu->nmi_pending = false;
    cpu->firq_pending = false;
    cpu->irq_pending = false;
    cpu->cycles = 0;
    cpu->total_cycles = 0;
    cpu->pc = read_word(0xFFFE);
}

void cpu_set_nmi(CPU6809 *cpu, bool state)
{
    /* NMI is edge-triggered: only set pending on transition to active */
    if (state && cpu->nmi_armed)
        cpu->nmi_pending = true;
}

void cpu_set_firq(CPU6809 *cpu, bool state)
{
    cpu->firq_pending = state;
}

void cpu_set_irq(CPU6809 *cpu, bool state)
{
    cpu->irq_pending = state;
}

void cpu_dump_state(const CPU6809 *cpu)
{
    fprintf(stderr,
        "PC=%04X A=%02X B=%02X D=%04X X=%04X Y=%04X U=%04X S=%04X DP=%02X "
        "CC=%c%c%c%c%c%c%c%c  cycles=%d\n",
        cpu->pc,
        cpu_get_a(cpu), cpu_get_b(cpu), cpu->d,
        cpu->x, cpu->y, cpu->u, cpu->s, cpu->dp,
        (cpu->cc & CC_E) ? 'E' : '.',
        (cpu->cc & CC_F) ? 'F' : '.',
        (cpu->cc & CC_H) ? 'H' : '.',
        (cpu->cc & CC_I) ? 'I' : '.',
        (cpu->cc & CC_N) ? 'N' : '.',
        (cpu->cc & CC_Z) ? 'Z' : '.',
        (cpu->cc & CC_V) ? 'V' : '.',
        (cpu->cc & CC_C) ? 'C' : '.',
        cpu->total_cycles);
}
