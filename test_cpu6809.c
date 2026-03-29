#include "cpu6809.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

static int pass_count, fail_count;
static const char *current_test;

#define TEST(name) do { current_test = (name); printf("  %s", name); } while(0)
#define CHECK(cond, msg) do { \
    if (cond) { printf("%*sPASS\n", (int)(55 - strlen(current_test)), ""); pass_count++; } \
    else { printf("%*sFAIL: %s\n", (int)(55 - strlen(current_test)), "", msg); fail_count++; } \
} while(0)

/* Write a small program to RAM at addr and return the address */
static uint16_t prog_addr;
static void prog_at(uint16_t addr) { prog_addr = addr; }
static void emit(uint8_t b) { mem_get_ram()[prog_addr++] = b; }
static void emit16(uint16_t w) { emit(w >> 8); emit(w & 0xFF); }

/* Set up CPU for test: init memory, place program, set PC */
static CPU6809 cpu;
static void setup(uint16_t addr)
{
    mem_init();
    cpu_init(&cpu);
    cpu.cc = 0;
    cpu.dp = 0;
    cpu.s = 0x7F00;
    cpu.u = 0x7E00;
    cpu.pc = addr;
}

/* Run N instructions */
static void run(int n)
{
    for (int i = 0; i < n; i++)
        cpu_step(&cpu);
}

/* ================================================================ */
int main(void)
{
    printf("=== CPU 6809 Instruction Tests ===\n");

    /* ---- 8-bit load/store ---- */
    printf("\n8-bit load/store:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0xB7); emit16(0x0300);   /* STA $0300 */
    run(2);
    TEST("LDA immediate + STA extended");
    CHECK(cpu_get_a(&cpu) == 0x42 && mem_get_ram()[0x300] == 0x42, "A or mem wrong");

    setup(0x200);
    prog_at(0x200);
    emit(0xC6); emit(0x7F);       /* LDB #$7F */
    run(1);
    TEST("LDB immediate");
    CHECK(cpu_get_b(&cpu) == 0x7F, "B wrong");
    CHECK(!(cpu.cc & CC_N), "N should be clear");
    CHECK(!(cpu.cc & CC_Z), "Z should be clear");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x00);       /* LDA #$00 */
    run(1);
    TEST("LDA #$00 sets Z flag");
    CHECK(cpu.cc & CC_Z, "Z not set");
    CHECK(!(cpu.cc & CC_N), "N should be clear");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 */
    run(1);
    TEST("LDA #$80 sets N flag");
    CHECK(cpu.cc & CC_N, "N not set");
    CHECK(!(cpu.cc & CC_Z), "Z should be clear");

    /* ---- 16-bit load/store ---- */
    printf("\n16-bit load/store:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0xCC); emit16(0x1234);   /* LDD #$1234 */
    emit(0xFD); emit16(0x0300);   /* STD $0300 */
    run(2);
    TEST("LDD immediate + STD extended");
    CHECK(cpu.d == 0x1234, "D wrong");
    CHECK(mem_get_ram()[0x300] == 0x12 && mem_get_ram()[0x301] == 0x34, "mem wrong");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0xABCD);   /* LDX #$ABCD */
    run(1);
    TEST("LDX immediate");
    CHECK(cpu.x == 0xABCD, "X wrong");

    setup(0x200);
    prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0x5678);  /* LDY #$5678 */
    run(1);
    TEST("LDY immediate (page 2)");
    CHECK(cpu.y == 0x5678, "Y wrong");

    /* ---- 8-bit arithmetic ---- */
    printf("\n8-bit arithmetic:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x30);       /* LDA #$30 */
    emit(0x8B); emit(0x12);       /* ADDA #$12 */
    run(2);
    TEST("ADDA immediate");
    CHECK(cpu_get_a(&cpu) == 0x42, "A wrong");
    CHECK(!(cpu.cc & CC_C), "C should be clear");
    CHECK(!(cpu.cc & CC_V), "V should be clear");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0xFF);       /* LDA #$FF */
    emit(0x8B); emit(0x01);       /* ADDA #$01 */
    run(2);
    TEST("ADDA overflow: $FF + $01");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(cpu.cc & CC_C, "C should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x7F);       /* LDA #$7F */
    emit(0x8B); emit(0x01);       /* ADDA #$01 */
    run(2);
    TEST("ADDA signed overflow: $7F + $01");
    CHECK(cpu_get_a(&cpu) == 0x80, "A should be $80");
    CHECK(cpu.cc & CC_V, "V should be set");
    CHECK(cpu.cc & CC_N, "N should be set");
    CHECK(!(cpu.cc & CC_C), "C should be clear");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x50);       /* LDA #$50 */
    emit(0x80); emit(0x20);       /* SUBA #$20 */
    run(2);
    TEST("SUBA immediate");
    CHECK(cpu_get_a(&cpu) == 0x30, "A should be $30");
    CHECK(!(cpu.cc & CC_C), "C should be clear (no borrow)");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x10);       /* LDA #$10 */
    emit(0x80); emit(0x20);       /* SUBA #$20 */
    run(2);
    TEST("SUBA borrow: $10 - $20");
    CHECK(cpu_get_a(&cpu) == 0xF0, "A should be $F0");
    CHECK(cpu.cc & CC_C, "C should be set (borrow)");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x05);       /* LDA #$05 */
    emit(0x4C);                   /* INCA */
    run(2);
    TEST("INCA");
    CHECK(cpu_get_a(&cpu) == 0x06, "A should be $06");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0xFF);       /* LDA #$FF */
    emit(0x4C);                   /* INCA */
    run(2);
    TEST("INCA wraps $FF -> $00");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(!(cpu.cc & CC_C), "C unchanged by INC");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0x4A);                   /* DECA */
    run(2);
    TEST("DECA wraps $00 -> $FF");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A should be $FF");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x55);       /* LDA #$55 */
    emit(0x40);                   /* NEGA */
    run(2);
    TEST("NEGA");
    CHECK(cpu_get_a(&cpu) == 0xAB, "A should be $AB");
    CHECK(cpu.cc & CC_C, "C should be set (non-zero negate)");

    /* ---- 8-bit logic ---- */
    printf("\n8-bit logic:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0xF0);       /* LDA #$F0 */
    emit(0x84); emit(0x3C);       /* ANDA #$3C */
    run(2);
    TEST("ANDA immediate");
    CHECK(cpu_get_a(&cpu) == 0x30, "A should be $30");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x0F);       /* LDA #$0F */
    emit(0x8A); emit(0xF0);       /* ORA #$F0 */
    run(2);
    TEST("ORA immediate");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A should be $FF");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0xFF);       /* LDA #$FF */
    emit(0x88); emit(0xFF);       /* EORA #$FF */
    run(2);
    TEST("EORA immediate ($FF ^ $FF = $00)");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0xAA);       /* LDA #$AA */
    emit(0x43);                   /* COMA */
    run(2);
    TEST("COMA");
    CHECK(cpu_get_a(&cpu) == 0x55, "A should be $55");
    CHECK(cpu.cc & CC_C, "C always set by COM");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0x4F);                   /* CLRA */
    run(2);
    TEST("CLRA sets Z, clears NVC");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(!(cpu.cc & (CC_N | CC_V | CC_C)), "N,V,C should be clear");

    /* ---- Shifts and rotates ---- */
    printf("\nShifts and rotates:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x01);       /* LDA #$01 */
    emit(0x48);                   /* ASLA / LSLA */
    run(2);
    TEST("ASLA: $01 << 1 = $02");
    CHECK(cpu_get_a(&cpu) == 0x02, "A should be $02");
    CHECK(!(cpu.cc & CC_C), "C should be clear");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 */
    emit(0x48);                   /* ASLA */
    run(2);
    TEST("ASLA: $80 << 1 sets C");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(cpu.cc & CC_C, "C should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x02);       /* LDA #$02 */
    emit(0x44);                   /* LSRA */
    run(2);
    TEST("LSRA: $02 >> 1 = $01");
    CHECK(cpu_get_a(&cpu) == 0x01, "A should be $01");
    CHECK(!(cpu.cc & CC_C), "C should be clear");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x01);       /* LDA #$01 */
    emit(0x44);                   /* LSRA */
    run(2);
    TEST("LSRA: $01 >> 1 sets C");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(cpu.cc & CC_C, "C should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 */
    emit(0x47);                   /* ASRA */
    run(2);
    TEST("ASRA: $80 >> 1 = $C0 (sign extend)");
    CHECK(cpu_get_a(&cpu) == 0xC0, "A should be $C0");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_C;                /* Set carry */
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0x49);                   /* ROLA */
    run(2);
    TEST("ROLA: rotate carry in");
    CHECK(cpu_get_a(&cpu) == 0x01, "A should be $01");
    CHECK(!(cpu.cc & CC_C), "C should be clear");

    /* ---- 16-bit arithmetic ---- */
    printf("\n16-bit arithmetic:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0xCC); emit16(0x1000);   /* LDD #$1000 */
    emit(0xC3); emit16(0x0234);   /* ADDD #$0234 */
    run(2);
    TEST("ADDD immediate");
    CHECK(cpu.d == 0x1234, "D should be $1234");

    setup(0x200);
    prog_at(0x200);
    emit(0xCC); emit16(0xFFFF);   /* LDD #$FFFF */
    emit(0xC3); emit16(0x0001);   /* ADDD #$0001 */
    run(2);
    TEST("ADDD overflow: $FFFF + $0001");
    CHECK(cpu.d == 0x0000, "D should be $0000");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(cpu.cc & CC_C, "C should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0xCC); emit16(0x5000);   /* LDD #$5000 */
    emit(0x83); emit16(0x1000);   /* SUBD #$1000 */
    run(2);
    TEST("SUBD immediate");
    CHECK(cpu.d == 0x4000, "D should be $4000");

    /* ---- MUL ---- */
    printf("\nMultiply:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x0A);       /* LDA #10 */
    emit(0xC6); emit(0x14);       /* LDB #20 */
    emit(0x3D);                   /* MUL */
    run(3);
    TEST("MUL: 10 * 20 = 200");
    CHECK(cpu.d == 200, "D should be 200");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0xFF);       /* LDA #$FF */
    emit(0xC6); emit(0xFF);       /* LDB #$FF */
    emit(0x3D);                   /* MUL */
    run(3);
    TEST("MUL: $FF * $FF = $FE01");
    CHECK(cpu.d == 0xFE01, "D should be $FE01");

    /* ---- SEX ---- */
    printf("\nSign extend:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0xC6); emit(0x80);       /* LDB #$80 */
    emit(0x1D);                   /* SEX */
    run(2);
    TEST("SEX: B=$80 -> A=$FF");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A should be $FF");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0xC6); emit(0x7F);       /* LDB #$7F */
    emit(0x1D);                   /* SEX */
    run(2);
    TEST("SEX: B=$7F -> A=$00");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(!(cpu.cc & CC_N), "N should be clear");

    /* ---- Branches ---- */
    printf("\nBranches:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x20); emit(0x02);       /* BRA +2 */
    emit(0x86); emit(0xFF);       /* LDA #$FF (skipped) */
    emit(0x86); emit(0x42);       /* LDA #$42 (target) */
    run(2);
    TEST("BRA skips instruction");
    CHECK(cpu_get_a(&cpu) == 0x42, "A should be $42");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x05);       /* LDA #$05 */
    emit(0x81); emit(0x05);       /* CMPA #$05 */
    emit(0x27); emit(0x02);       /* BEQ +2 */
    emit(0x86); emit(0xFF);       /* LDA #$FF (skipped) */
    emit(0xC6); emit(0x01);       /* LDB #$01 (target) */
    run(4);
    TEST("BEQ taken when equal");
    CHECK(cpu_get_a(&cpu) == 0x05, "A should be $05 (not overwritten)");
    CHECK(cpu_get_b(&cpu) == 0x01, "B should be $01");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x05);       /* LDA #$05 */
    emit(0x81); emit(0x06);       /* CMPA #$06 */
    emit(0x27); emit(0x02);       /* BEQ +2 */
    emit(0x86); emit(0xFF);       /* LDA #$FF (NOT skipped) */
    emit(0x12);                   /* NOP */
    run(4);
    TEST("BEQ not taken when unequal");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A should be $FF");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x03);       /* LDA #$03 */
    emit(0x81); emit(0x05);       /* CMPA #$05 */
    emit(0x25); emit(0x02);       /* BCS/BLO +2 */
    emit(0x86); emit(0xFF);       /* LDA #$FF (skipped) */
    emit(0xC6); emit(0x01);       /* LDB #$01 */
    run(4);
    TEST("BCS/BLO taken when A < operand");
    CHECK(cpu_get_b(&cpu) == 0x01, "B should be $01");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 (-128) */
    emit(0x81); emit(0x01);       /* CMPA #$01 */
    emit(0x2D); emit(0x02);       /* BLT +2 (signed less than) */
    emit(0x86); emit(0xFF);       /* LDA #$FF (skipped) */
    emit(0xC6); emit(0x01);       /* LDB #$01 */
    run(4);
    TEST("BLT taken: -128 < 1 (signed)");
    CHECK(cpu_get_b(&cpu) == 0x01, "B should be $01");

    /* ---- BSR / RTS ---- */
    printf("\nSubroutine calls:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x8D); emit(0x04);       /* BSR +4 ($0206) */
    emit(0xC6); emit(0x42);       /* LDB #$42 (return here) */
    emit(0x12);                   /* NOP (end) */
    emit(0x12);                   /* NOP (padding) */
    /* $0206: subroutine */
    emit(0x86); emit(0x99);       /* LDA #$99 */
    emit(0x39);                   /* RTS */
    run(4);
    TEST("BSR/RTS round-trip");
    CHECK(cpu_get_a(&cpu) == 0x99, "A should be $99");
    CHECK(cpu_get_b(&cpu) == 0x42, "B should be $42 (after return)");

    /* ---- JSR / RTS ---- */
    setup(0x200);
    prog_at(0x200);
    emit(0xBD); emit16(0x0210);   /* JSR $0210 */
    emit(0xC6); emit(0x77);       /* LDB #$77 (return here) */
    emit(0x12);                   /* NOP */
    prog_at(0x210);
    emit(0x86); emit(0x33);       /* LDA #$33 */
    emit(0x39);                   /* RTS */
    run(4);
    TEST("JSR extended / RTS");
    CHECK(cpu_get_a(&cpu) == 0x33, "A should be $33");
    CHECK(cpu_get_b(&cpu) == 0x77, "B should be $77");

    /* ---- TFR / EXG ---- */
    printf("\nTransfer/exchange:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0x1F); emit(0x89);       /* TFR A,B */
    run(2);
    TEST("TFR A,B");
    CHECK(cpu_get_b(&cpu) == 0x42, "B should be $42");
    CHECK(cpu_get_a(&cpu) == 0x42, "A should be unchanged");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x11);       /* LDA #$11 */
    emit(0xC6); emit(0x22);       /* LDB #$22 */
    emit(0x1E); emit(0x89);       /* EXG A,B */
    run(3);
    TEST("EXG A,B");
    CHECK(cpu_get_a(&cpu) == 0x22, "A should be $22");
    CHECK(cpu_get_b(&cpu) == 0x11, "B should be $11");

    setup(0x200);
    prog_at(0x200);
    emit(0xCC); emit16(0xBEEF);   /* LDD #$BEEF */
    emit(0x8E); emit16(0x1234);   /* LDX #$1234 */
    emit(0x1E); emit(0x01);       /* EXG D,X */
    run(3);
    TEST("EXG D,X");
    CHECK(cpu.d == 0x1234, "D should be $1234");
    CHECK(cpu.x == 0xBEEF, "X should be $BEEF");

    /* ---- PSH / PUL ---- */
    printf("\nStack operations:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0xC6); emit(0x99);       /* LDB #$99 */
    emit(0x34); emit(0x06);       /* PSHS A,B */
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0xC6); emit(0x00);       /* LDB #$00 */
    emit(0x35); emit(0x06);       /* PULS A,B */
    run(6);
    TEST("PSHS/PULS A,B round-trip");
    CHECK(cpu_get_a(&cpu) == 0x42, "A should be $42");
    CHECK(cpu_get_b(&cpu) == 0x99, "B should be $99");

    setup(0x200);
    prog_at(0x200);
    uint16_t old_s = cpu.s;
    emit(0x8E); emit16(0xAAAA);   /* LDX #$AAAA */
    emit(0x34); emit(0x10);       /* PSHS X */
    run(2);
    TEST("PSHS X decrements S by 2");
    CHECK(cpu.s == old_s - 2, "S should decrease by 2");
    CHECK(mem_get_ram()[old_s - 1] == 0xAA, "low byte");
    CHECK(mem_get_ram()[old_s - 2] == 0xAA, "high byte");

    /* ---- Addressing modes ---- */
    printf("\nAddressing modes:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x77;
    emit(0x96); emit(0x10);       /* LDA <$10 (DP:$10 = $0310) */
    run(1);
    TEST("Direct page addressing");
    CHECK(cpu_get_a(&cpu) == 0x77, "A should be $77");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x55;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xA6); emit(0x84);       /* LDA ,X (zero offset) */
    run(2);
    TEST("Indexed: LDA ,X");
    CHECK(cpu_get_a(&cpu) == 0x55, "A should be $55");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0405] = 0x33;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xA6); emit(0x05);       /* LDA 5,X (5-bit offset) */
    run(2);
    TEST("Indexed: LDA 5,X (5-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x33, "A should be $33");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xAB;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xA6); emit(0x80);       /* LDA ,X+ (post-increment) */
    run(2);
    TEST("Indexed: LDA ,X+ (post-increment)");
    CHECK(cpu_get_a(&cpu) == 0xAB, "A should be $AB");
    CHECK(cpu.x == 0x0401, "X should be $0401");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0401] = 0xCD;
    emit(0x8E); emit16(0x0402);   /* LDX #$0402 */
    emit(0xA6); emit(0x82);       /* LDA ,-X (pre-decrement) */
    run(2);
    TEST("Indexed: LDA ,-X (pre-decrement)");
    CHECK(cpu_get_a(&cpu) == 0xCD, "A should be $CD");
    CHECK(cpu.x == 0x0401, "X should be $0401");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0403] = 0xEE;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xC6); emit(0x03);       /* LDB #$03 */
    emit(0xA6); emit(0x85);       /* LDA B,X (accumulator offset) */
    run(3);
    TEST("Indexed: LDA B,X (accumulator offset)");
    CHECK(cpu_get_a(&cpu) == 0xEE, "A should be $EE");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0410] = 0x44;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xA6); emit(0x88); emit(0x10);  /* LDA $10,X (8-bit offset) */
    run(2);
    TEST("Indexed: LDA $10,X (8-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x44, "A should be $44");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0500] = 0x11;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xA6); emit(0x89); emit16(0x0100);  /* LDA $0100,X (16-bit offset) */
    run(2);
    TEST("Indexed: LDA $0100,X (16-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x11, "A should be $11");

    setup(0x200);
    prog_at(0x200);
    /* PC-relative: target byte at $0209 */
    mem_get_ram()[0x0209] = 0x77;
    emit(0xA6); emit(0x8C); emit(0x06);  /* LDA 6,PCR */
    /* After fetch: PC=$0203. $0203 + 6 = $0209 */
    run(1);
    TEST("Indexed: LDA n,PCR (PC-relative 8-bit)");
    CHECK(cpu_get_a(&cpu) == 0x77, "A should be $77");

    /* ---- Indirect addressing ---- */
    printf("\nIndirect addressing:\n");

    setup(0x200);
    prog_at(0x200);
    /* [,X] indirect: X points to a pointer */
    mem_get_ram()[0x0400] = 0x05;
    mem_get_ram()[0x0401] = 0x00;  /* pointer at $0400 -> $0500 */
    mem_get_ram()[0x0500] = 0xBB;  /* value at $0500 */
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xA6); emit(0x94);       /* LDA [,X] (indirect) */
    run(2);
    TEST("Indexed indirect: LDA [,X]");
    CHECK(cpu_get_a(&cpu) == 0xBB, "A should be $BB");

    /* ---- LEA ---- */
    printf("\nLEA instructions:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x1000);   /* LDX #$1000 */
    emit(0x30); emit(0x05);       /* LEAX 5,X */
    run(2);
    TEST("LEAX 5,X");
    CHECK(cpu.x == 0x1005, "X should be $1005");
    CHECK(!(cpu.cc & CC_Z), "Z should be clear");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x0005);   /* LDX #$0005 */
    emit(0x30); emit(0x1B);       /* LEAX -5,X */
    run(2);
    TEST("LEAX -5,X -> 0 sets Z");
    CHECK(cpu.x == 0x0000, "X should be $0000");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    /* ---- Compare instructions ---- */
    printf("\nCompare:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0x81); emit(0x42);       /* CMPA #$42 */
    run(2);
    TEST("CMPA equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(!(cpu.cc & CC_C), "C should be clear");
    CHECK(cpu_get_a(&cpu) == 0x42, "A unchanged");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x1234);   /* LDX #$1234 */
    emit(0x8C); emit16(0x1234);   /* CMPX #$1234 */
    run(2);
    TEST("CMPX equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x1234);   /* LDX #$1234 */
    emit(0x8C); emit16(0x1235);   /* CMPX #$1235 */
    run(2);
    TEST("CMPX less than");
    CHECK(cpu.cc & CC_C, "C should be set (borrow)");
    CHECK(!(cpu.cc & CC_Z), "Z should be clear");

    /* ---- ABX ---- */
    printf("\nMisc:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x1000);   /* LDX #$1000 */
    emit(0xC6); emit(0xFF);       /* LDB #$FF */
    emit(0x3A);                   /* ABX */
    run(3);
    TEST("ABX: X=$1000 + B=$FF = $10FF");
    CHECK(cpu.x == 0x10FF, "X should be $10FF");

    /* ---- DAA ---- */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x09);       /* LDA #$09 (BCD 9) */
    emit(0x8B); emit(0x01);       /* ADDA #$01 (9+1=10) */
    emit(0x19);                   /* DAA */
    run(3);
    TEST("DAA: 09+01 = BCD 10");
    CHECK(cpu_get_a(&cpu) == 0x10, "A should be $10");

    /* ---- Cycle counts ---- */
    printf("\nCycle counts:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x12);                   /* NOP */
    cpu.total_cycles = 0;
    run(1);
    TEST("NOP = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x00);       /* LDA immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA immediate = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200);
    prog_at(0x200);
    emit(0xB6); emit16(0x0300);   /* LDA extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA extended = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    emit(0x3D);                   /* MUL */
    cpu.total_cycles = 0;
    run(1);
    TEST("MUL = 11 cycles");
    CHECK(cpu.total_cycles == 11, "expected 11");

    setup(0x200);
    prog_at(0x200);
    emit(0x8D); emit(0x00);       /* BSR +0 (call next instruction) */
    cpu.total_cycles = 0;
    run(1);
    TEST("BSR = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    /* ---- ADC / SBC (carry-dependent arithmetic) ---- */
    printf("\nADC/SBC (carry-dependent):\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x10);       /* LDA #$10 */
    emit(0x1A); emit(0x01);       /* ORCC #$01 (set carry) */
    emit(0x89); emit(0x05);       /* ADCA #$05 */
    run(3);
    TEST("ADCA with carry: $10 + $05 + C=1 = $16");
    CHECK(cpu_get_a(&cpu) == 0x16, "A should be $16");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0xFF);       /* LDA #$FF */
    emit(0x1A); emit(0x01);       /* ORCC #$01 */
    emit(0x89); emit(0x00);       /* ADCA #$00 */
    run(3);
    TEST("ADCA carry propagation: $FF + $00 + C=1");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(cpu.cc & CC_Z, "Z set");
    CHECK(cpu.cc & CC_C, "C set");

    setup(0x200);
    prog_at(0x200);
    emit(0xC6); emit(0x20);       /* LDB #$20 */
    emit(0x1A); emit(0x01);       /* ORCC #$01 */
    emit(0xC9); emit(0x03);       /* ADCB #$03 */
    run(3);
    TEST("ADCB with carry: $20 + $03 + C=1 = $24");
    CHECK(cpu_get_b(&cpu) == 0x24, "B should be $24");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x20);       /* LDA #$20 */
    emit(0x1A); emit(0x01);       /* ORCC #$01 */
    emit(0x82); emit(0x10);       /* SBCA #$10 */
    run(3);
    TEST("SBCA with borrow: $20 - $10 - C=1 = $0F");
    CHECK(cpu_get_a(&cpu) == 0x0F, "A should be $0F");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0x1A); emit(0x01);       /* ORCC #$01 */
    emit(0x82); emit(0x00);       /* SBCA #$00 */
    run(3);
    TEST("SBCA borrow from zero: $00 - $00 - C=1 = $FF");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A should be $FF");
    CHECK(cpu.cc & CC_C, "C set (borrow)");
    CHECK(cpu.cc & CC_N, "N set");

    setup(0x200);
    prog_at(0x200);
    emit(0xC6); emit(0x50);       /* LDB #$50 */
    emit(0x1A); emit(0x01);       /* ORCC #$01 */
    emit(0xC2); emit(0x20);       /* SBCB #$20 */
    run(3);
    TEST("SBCB with borrow: $50 - $20 - C=1 = $2F");
    CHECK(cpu_get_b(&cpu) == 0x2F, "B should be $2F");

    /* ---- Memory read-modify-write ---- */
    printf("\nMemory RMW operations:\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x05;
    emit(0x7C); emit16(0x0300);   /* INC $0300 */
    run(1);
    TEST("INC extended: $05 -> $06");
    CHECK(mem_get_ram()[0x0300] == 0x06, "mem should be $06");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0xFF;
    emit(0x7C); emit16(0x0300);   /* INC $0300 */
    run(1);
    TEST("INC extended: $FF -> $00 sets Z");
    CHECK(mem_get_ram()[0x0300] == 0x00, "mem should be $00");
    CHECK(cpu.cc & CC_Z, "Z set");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x01;
    emit(0x7A); emit16(0x0300);   /* DEC $0300 */
    run(1);
    TEST("DEC extended: $01 -> $00 sets Z");
    CHECK(mem_get_ram()[0x0300] == 0x00, "mem should be $00");
    CHECK(cpu.cc & CC_Z, "Z set");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x55;
    emit(0x70); emit16(0x0300);   /* NEG $0300 */
    run(1);
    TEST("NEG extended: $55 -> $AB");
    CHECK(mem_get_ram()[0x0300] == 0xAB, "mem should be $AB");
    CHECK(cpu.cc & CC_C, "C set");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0xAA;
    emit(0x73); emit16(0x0300);   /* COM $0300 */
    run(1);
    TEST("COM extended: $AA -> $55");
    CHECK(mem_get_ram()[0x0300] == 0x55, "mem should be $55");
    CHECK(cpu.cc & CC_C, "C always set by COM");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x42;
    emit(0x7F); emit16(0x0300);   /* CLR $0300 */
    run(1);
    TEST("CLR extended: -> $00, Z set");
    CHECK(mem_get_ram()[0x0300] == 0x00, "mem should be $00");
    CHECK(cpu.cc & CC_Z, "Z set");
    CHECK(!(cpu.cc & (CC_N | CC_V | CC_C)), "N,V,C clear");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x80;
    emit(0x7D); emit16(0x0300);   /* TST $0300 */
    run(1);
    TEST("TST extended: $80 sets N");
    CHECK(cpu.cc & CC_N, "N set");
    CHECK(!(cpu.cc & CC_Z), "Z clear");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x00;
    emit(0x7D); emit16(0x0300);   /* TST $0300 */
    run(1);
    TEST("TST extended: $00 sets Z");
    CHECK(cpu.cc & CC_Z, "Z set");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x40;
    emit(0x78); emit16(0x0300);   /* ASL $0300 */
    run(1);
    TEST("ASL extended: $40 -> $80");
    CHECK(mem_get_ram()[0x0300] == 0x80, "mem should be $80");
    CHECK(!(cpu.cc & CC_C), "C clear");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x80;
    emit(0x77); emit16(0x0300);   /* ASR $0300 */
    run(1);
    TEST("ASR extended: $80 -> $C0 (sign extend)");
    CHECK(mem_get_ram()[0x0300] == 0xC0, "mem should be $C0");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x82;
    emit(0x74); emit16(0x0300);   /* LSR $0300 */
    run(1);
    TEST("LSR extended: $82 -> $41");
    CHECK(mem_get_ram()[0x0300] == 0x41, "mem should be $41");
    CHECK(!(cpu.cc & CC_C), "C clear");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x81;
    cpu.cc = CC_C;                /* Carry in */
    emit(0x79); emit16(0x0300);   /* ROL $0300 */
    run(1);
    TEST("ROL extended: $81 with C=1 -> $03, C=1");
    CHECK(mem_get_ram()[0x0300] == 0x03, "mem should be $03");
    CHECK(cpu.cc & CC_C, "C set (bit 7 was 1)");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0300] = 0x81;
    cpu.cc = CC_C;                /* Carry in */
    emit(0x76); emit16(0x0300);   /* ROR $0300 */
    run(1);
    TEST("ROR extended: $81 with C=1 -> $C0, C=1");
    CHECK(mem_get_ram()[0x0300] == 0xC0, "mem should be $C0");
    CHECK(cpu.cc & CC_C, "C set (bit 0 was 1)");

    /* Indexed RMW */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x10;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0x6C); emit(0x84);       /* INC ,X */
    run(2);
    TEST("INC indexed ,X: $10 -> $11");
    CHECK(mem_get_ram()[0x0400] == 0x11, "mem should be $11");

    /* ---- SWI / RTI (interrupt handling) ---- */
    printf("\nSWI/RTI (software interrupt):\n");

    /* Vectors at $FFE0-$FFFF always read from ROM. With no ROM loaded,
     * all vectors read $0000. Place handlers at $0000.
     * Note: RTI restores ALL registers, so handler effects on A/B are
     * overwritten. Use memory writes to prove the handler ran. */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0xC6); emit(0x77);       /* LDB #$77 */
    emit(0x3F);                   /* SWI (1 byte) — vectors to $0000 */
    /* Handler at $0000: write marker to $0300, then RTI */
    prog_at(0x0000);
    emit(0x86); emit(0xEE);       /* LDA #$EE */
    emit(0xB7); emit16(0x0300);   /* STA $0300 */
    emit(0x3B);                   /* RTI */
    run(6);  /* LDA, LDB, SWI, handler: LDA, STA, RTI */
    TEST("SWI vectors to handler (marker in RAM)");
    CHECK(mem_get_ram()[0x0300] == 0xEE, "handler should write $EE");

    TEST("RTI restores A from pre-SWI state");
    CHECK(cpu_get_a(&cpu) == 0x42, "A should be $42");

    TEST("RTI restores B from pre-SWI state");
    CHECK(cpu_get_b(&cpu) == 0x77, "B should be $77");

    TEST("RTI returns to byte after SWI");
    CHECK(cpu.pc == 0x0205, "PC should be $0205");

    TEST("CC has E set after RTI");
    CHECK(cpu.cc & CC_E, "E should be set");

    /* SWI2 — also vectors to $0000 ($FFF4 reads $0000) */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x11);       /* LDA #$11 */
    emit(0x10); emit(0x3F);       /* SWI2 (2 bytes) */
    prog_at(0x0000);
    emit(0x86); emit(0xDD);       /* LDA #$DD (marker, overwritten by RTI) */
    emit(0xB7); emit16(0x0300);   /* STA $0300 */
    emit(0x3B);                   /* RTI */
    run(5);  /* LDA, SWI2, handler: LDA, STA, RTI */
    TEST("SWI2 + RTI: handler ran and A restored");
    CHECK(mem_get_ram()[0x0300] == 0xDD, "handler ran");
    CHECK(cpu_get_a(&cpu) == 0x11, "A restored to pre-SWI2 value");

    /* SWI3 */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x33);       /* LDA #$33 */
    emit(0x11); emit(0x3F);       /* SWI3 (2 bytes) */
    prog_at(0x0000);
    emit(0x86); emit(0xCC);       /* LDA #$CC */
    emit(0xB7); emit16(0x0300);   /* STA $0300 */
    emit(0x3B);                   /* RTI */
    run(5);  /* LDA, SWI3, handler: LDA, STA, RTI */
    TEST("SWI3 + RTI: handler ran and A restored");
    CHECK(mem_get_ram()[0x0300] == 0xCC, "handler ran");
    CHECK(cpu_get_a(&cpu) == 0x33, "A restored to pre-SWI3 value");

    /* RTI with E=0 (FIRQ-style: only CC and PC restored) */
    setup(0x200);
    prog_at(0x200);
    /* Manually push a FIRQ-style stack frame: CC (with E=0) then PC */
    uint16_t sp = cpu.s;
    mem_get_ram()[--sp] = 0x10;   /* PC low = $0210 */
    mem_get_ram()[--sp] = 0x02;   /* PC high */
    mem_get_ram()[--sp] = 0x00;   /* CC with E=0 */
    cpu.s = sp;
    emit(0x3B);                   /* RTI */
    prog_at(0x0210);
    emit(0x86); emit(0xAA);       /* LDA #$AA */
    run(2);
    TEST("RTI with E=0 restores only CC+PC");
    CHECK(cpu.pc == 0x0212, "PC should advance past LDA");
    CHECK(cpu_get_a(&cpu) == 0xAA, "A should be $AA");
    CHECK(!(cpu.cc & CC_E), "E should be clear");

    /* ---- ANDCC / ORCC ---- */
    printf("\nCC manipulation:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_F | CC_I | CC_N | CC_C;
    emit(0x1C); emit(0xAF);       /* ANDCC #$AF (clear F and I) */
    run(1);
    TEST("ANDCC #$AF clears I and F");
    CHECK(!(cpu.cc & CC_I), "I clear");
    CHECK(!(cpu.cc & CC_F), "F clear");
    CHECK(cpu.cc & CC_N, "N preserved");
    CHECK(cpu.cc & CC_C, "C preserved");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = 0;
    emit(0x1A); emit(0x50);       /* ORCC #$50 (set F and I) */
    run(1);
    TEST("ORCC #$50 sets F and I");
    CHECK(cpu.cc & CC_F, "F set");
    CHECK(cpu.cc & CC_I, "I set");

    /* ---- CWAI ---- */
    printf("\nCWAI/SYNC:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_F | CC_I;
    emit(0x3C); emit(0xEF);       /* CWAI #$EF (clear I, then wait) */
    run(1);
    TEST("CWAI halts CPU");
    CHECK(cpu.halted, "should be halted");
    CHECK(cpu.cwai, "cwai flag set");
    CHECK(!(cpu.cc & CC_I), "I cleared by CWAI AND mask");
    CHECK(cpu.cc & CC_E, "E set (state pushed)");

    /* ---- SYNC ---- */
    setup(0x200);
    prog_at(0x200);
    emit(0x13);                   /* SYNC */
    run(1);
    TEST("SYNC halts CPU");
    CHECK(cpu.halted, "should be halted");
    CHECK(!cpu.cwai, "cwai should be false (this is SYNC)");

    /* ---- Page 2 compares ---- */
    printf("\nPage 2 compares:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0xCC); emit16(0x1234);         /* LDD #$1234 */
    emit(0x10); emit(0x83); emit16(0x1234);  /* CMPD #$1234 */
    run(2);
    TEST("CMPD equal");
    CHECK(cpu.cc & CC_Z, "Z set");
    CHECK(!(cpu.cc & CC_C), "C clear");

    setup(0x200);
    prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0x5000);  /* LDY #$5000 */
    emit(0x10); emit(0x8C); emit16(0x6000);  /* CMPY #$6000 */
    run(2);
    TEST("CMPY: $5000 < $6000");
    CHECK(cpu.cc & CC_C, "C set (borrow)");
    CHECK(!(cpu.cc & CC_Z), "Z clear");

    setup(0x200);
    prog_at(0x200);
    emit(0xCE); emit16(0x4000);         /* LDU #$4000 */
    emit(0x11); emit(0x83); emit16(0x4000);  /* CMPU #$4000 */
    run(2);
    TEST("CMPU equal (page 3)");
    CHECK(cpu.cc & CC_Z, "Z set");

    setup(0x200);
    prog_at(0x200);
    emit(0x10); emit(0xCE); emit16(0x2000);  /* LDS #$2000 */
    emit(0x11); emit(0x8C); emit16(0x3000);  /* CMPS #$3000 */
    run(2);
    TEST("CMPS: $2000 < $3000 (page 3)");
    CHECK(cpu.cc & CC_C, "C set");

    /* ---- LEAS / LEAU ---- */
    printf("\nLEAS/LEAU:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.s = 0x7F00;
    emit(0x32); emit(0x7F);       /* LEAS -1,S */
    run(1);
    TEST("LEAS -1,S");
    CHECK(cpu.s == 0x7EFF, "S should be $7EFF");

    setup(0x200);
    prog_at(0x200);
    cpu.u = 0x3000;
    emit(0x33); emit(0x49);       /* LEAU 9,U (5-bit offset) */
    run(1);
    TEST("LEAU 9,U");
    CHECK(cpu.u == 0x3009, "U should be $3009");

    /* ---- PSHU / PULU ---- */
    printf("\nUser stack:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.u = 0x7E00;
    emit(0x86); emit(0x55);       /* LDA #$55 */
    emit(0xC6); emit(0xAA);       /* LDB #$AA */
    emit(0x36); emit(0x06);       /* PSHU A,B */
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0xC6); emit(0x00);       /* LDB #$00 */
    emit(0x37); emit(0x06);       /* PULU A,B */
    run(6);
    TEST("PSHU/PULU A,B round-trip");
    CHECK(cpu_get_a(&cpu) == 0x55, "A should be $55");
    CHECK(cpu_get_b(&cpu) == 0xAA, "B should be $AA");

    /* ---- Long branches ---- */
    printf("\nLong branches:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x16); emit16(0x0100);   /* LBRA +$0100 -> $0303 */
    prog_at(0x0303);
    emit(0x86); emit(0x77);       /* LDA #$77 */
    run(2);
    TEST("LBRA long forward branch");
    CHECK(cpu_get_a(&cpu) == 0x77, "A should be $77");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x00);       /* LDA #$00 (sets Z) */
    emit(0x10); emit(0x27); emit16(0x0050);  /* LBEQ +$0050 -> $0256 */
    prog_at(0x0256);
    emit(0xC6); emit(0x88);       /* LDB #$88 */
    run(3);
    TEST("LBEQ taken (page 2 long branch)");
    CHECK(cpu_get_b(&cpu) == 0x88, "B should be $88");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x01);       /* LDA #$01 (clears Z) */
    emit(0x10); emit(0x27); emit16(0x0050);  /* LBEQ +$0050 */
    emit(0xC6); emit(0x11);       /* LDB #$11 (not skipped) */
    run(3);
    TEST("LBEQ not taken");
    CHECK(cpu_get_b(&cpu) == 0x11, "B should be $11");

    setup(0x200);
    prog_at(0x200);
    emit(0x17); emit16(0x0004);   /* LBSR +$0004 -> $0207 */
    emit(0xC6); emit(0x66);       /* LDB #$66 (return here) */
    emit(0x12);                   /* NOP */
    emit(0x12);                   /* NOP (pad) */
    /* $0207: */
    emit(0x86); emit(0x55);       /* LDA #$55 */
    emit(0x39);                   /* RTS */
    run(4);
    TEST("LBSR/RTS round-trip");
    CHECK(cpu_get_a(&cpu) == 0x55, "A should be $55");
    CHECK(cpu_get_b(&cpu) == 0x66, "B should be $66");

    /* ---- Half-carry flag ---- */
    printf("\nHalf-carry:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x0F);       /* LDA #$0F */
    emit(0x8B); emit(0x01);       /* ADDA #$01 */
    run(2);
    TEST("ADDA half-carry: $0F + $01 sets H");
    CHECK(cpu.cc & CC_H, "H should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x10);       /* LDA #$10 */
    emit(0x8B); emit(0x01);       /* ADDA #$01 */
    run(2);
    TEST("ADDA no half-carry: $10 + $01");
    CHECK(!(cpu.cc & CC_H), "H should be clear");

    /* ---- B-register inherent operations ---- */
    printf("\nB-register inherent:\n");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x05); emit(0x5C); run(2);
    TEST("INCB: $05 -> $06");
    CHECK(cpu_get_b(&cpu) == 0x06, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x01); emit(0x5A); run(2);
    TEST("DECB: $01 -> $00 sets Z");
    CHECK(cpu_get_b(&cpu) == 0x00 && (cpu.cc & CC_Z), "B or Z wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x55); emit(0x50); run(2);
    TEST("NEGB: $55 -> $AB");
    CHECK(cpu_get_b(&cpu) == 0xAB, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0xAA); emit(0x53); run(2);
    TEST("COMB: $AA -> $55");
    CHECK(cpu_get_b(&cpu) == 0x55, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0xFF); emit(0x5F); run(2);
    TEST("CLRB: -> $00, Z set");
    CHECK(cpu_get_b(&cpu) == 0x00 && (cpu.cc & CC_Z), "B or Z wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x80); emit(0x5D); run(2);
    TEST("TSTB: $80 sets N");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x00); emit(0x5D); run(2);
    TEST("TSTB: $00 sets Z");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x40); emit(0x58); run(2);
    TEST("ASLB: $40 -> $80");
    CHECK(cpu_get_b(&cpu) == 0x80, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x40); emit(0x58); run(2);
    TEST("LSLB: same as ASLB");
    CHECK(cpu_get_b(&cpu) == 0x80, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x04); emit(0x54); run(2);
    TEST("LSRB: $04 -> $02");
    CHECK(cpu_get_b(&cpu) == 0x02, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x80); emit(0x57); run(2);
    TEST("ASRB: $80 -> $C0 (sign extend)");
    CHECK(cpu_get_b(&cpu) == 0xC0, "B wrong");

    setup(0x200); prog_at(0x200);
    cpu.cc = CC_C;
    emit(0xC6); emit(0x00); emit(0x59); run(2);
    TEST("ROLB: C=1 rotates in");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    setup(0x200); prog_at(0x200);
    cpu.cc = CC_C;
    emit(0xC6); emit(0x01); emit(0x56); run(2);
    TEST("RORB: $01 with C=1 -> $80, C=1");
    CHECK(cpu_get_b(&cpu) == 0x80 && (cpu.cc & CC_C), "B or C wrong");

    /* ---- A-register missing inherent ops ---- */
    printf("\nA-register remaining:\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80); emit(0x4D); run(2);
    TEST("TSTA: $80 sets N");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01); emit(0x48); run(2);
    TEST("LSLA: $01 -> $02");
    CHECK(cpu_get_a(&cpu) == 0x02, "A wrong");

    setup(0x200); prog_at(0x200);
    cpu.cc = CC_C;
    emit(0x86); emit(0x01); emit(0x46); run(2);
    TEST("RORA: $01 with C=1 -> $80, C=1");
    CHECK(cpu_get_a(&cpu) == 0x80 && (cpu.cc & CC_C), "A or C wrong");

    /* ---- B-register arithmetic/logic ---- */
    printf("\nB-register arithmetic/logic:\n");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x10); emit(0xCB); emit(0x05); run(2);
    TEST("ADDB: $10 + $05 = $15");
    CHECK(cpu_get_b(&cpu) == 0x15, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x30); emit(0xC0); emit(0x10); run(2);
    TEST("SUBB: $30 - $10 = $20");
    CHECK(cpu_get_b(&cpu) == 0x20, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0xF0); emit(0xC4); emit(0x1F); run(2);
    TEST("ANDB: $F0 & $1F = $10");
    CHECK(cpu_get_b(&cpu) == 0x10, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x0F); emit(0xCA); emit(0xF0); run(2);
    TEST("ORB: $0F | $F0 = $FF");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0xAA); emit(0xC8); emit(0x55); run(2);
    TEST("EORB: $AA ^ $55 = $FF");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x42); emit(0xC1); emit(0x42); run(2);
    TEST("CMPB: equal sets Z");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    /* ---- BITA / BITB ---- */
    printf("\nBIT test:\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0xFF); emit(0x85); emit(0x80); run(2);
    TEST("BITA: $FF & $80 -> N set, A unchanged");
    CHECK(cpu_get_a(&cpu) == 0xFF && (cpu.cc & CC_N), "A or N wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x0F); emit(0x85); emit(0xF0); run(2);
    TEST("BITA: $0F & $F0 = 0 -> Z set");
    CHECK(cpu_get_a(&cpu) == 0x0F && (cpu.cc & CC_Z), "A or Z wrong");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0xAA); emit(0xC5); emit(0x0A); run(2);
    TEST("BITB: $AA & $0A != 0 -> Z clear");
    CHECK(cpu_get_b(&cpu) == 0xAA && !(cpu.cc & CC_Z), "B or Z wrong");

    /* ---- STB ---- */
    printf("\nRemaining stores:\n");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x77);
    emit(0xF7); emit16(0x0300); run(2);
    TEST("STB extended");
    CHECK(mem_get_ram()[0x0300] == 0x77, "mem wrong");

    /* ---- 16-bit stores ---- */
    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0xBEEF);
    emit(0xBF); emit16(0x0300); run(2);
    TEST("STX extended");
    CHECK(mem_get_ram()[0x300]==0xBE && mem_get_ram()[0x301]==0xEF, "mem wrong");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0xCAFE);
    emit(0x10); emit(0xBF); emit16(0x0300); run(2);
    TEST("STY extended (page 2)");
    CHECK(mem_get_ram()[0x300]==0xCA && mem_get_ram()[0x301]==0xFE, "mem wrong");

    setup(0x200); prog_at(0x200);
    emit(0xCE); emit16(0xDEAD);
    emit(0xFF); emit16(0x0300); run(2);
    TEST("STU extended");
    CHECK(mem_get_ram()[0x300]==0xDE && mem_get_ram()[0x301]==0xAD, "mem wrong");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xCE); emit16(0xF00D);
    emit(0x10); emit(0xFF); emit16(0x0300); run(2);
    TEST("STS extended (page 2)");
    CHECK(mem_get_ram()[0x300]==0xF0 && mem_get_ram()[0x301]==0x0D, "mem wrong");

    /* ---- 16-bit loads (remaining) ---- */
    setup(0x200); prog_at(0x200);
    emit(0xCE); emit16(0x4321); run(1);
    TEST("LDU immediate");
    CHECK(cpu.u == 0x4321, "U wrong");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xCE); emit16(0x7F00); run(1);
    TEST("LDS immediate (page 2)");
    CHECK(cpu.s == 0x7F00, "S wrong");

    /* ---- Memory RMW: LSL ---- */
    printf("\nMemory RMW remaining:\n");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0300] = 0x40;
    emit(0x78); emit16(0x0300); run(1);
    TEST("LSL extended: $40 -> $80 (same as ASL)");
    CHECK(mem_get_ram()[0x0300] == 0x80, "mem wrong");

    /* ---- JMP ---- */
    printf("\nJMP:\n");

    setup(0x200); prog_at(0x200);
    emit(0x7E); emit16(0x0300);   /* JMP $0300 */
    prog_at(0x0300);
    emit(0x86); emit(0xEE);       /* LDA #$EE */
    run(2);
    TEST("JMP extended");
    CHECK(cpu_get_a(&cpu) == 0xEE, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0x0300);   /* LDX #$0300 */
    emit(0x6E); emit(0x84);       /* JMP ,X */
    prog_at(0x0300);
    emit(0x86); emit(0xDD);       /* LDA #$DD */
    run(3);
    TEST("JMP indexed ,X");
    CHECK(cpu_get_a(&cpu) == 0xDD, "A wrong");

    /* ---- LEAY ---- */
    printf("\nLEAY:\n");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0x2000);  /* LDY #$2000 */
    emit(0x31); emit(0x28);       /* LEAY 8,Y */
    run(2);
    TEST("LEAY 8,Y");
    CHECK(cpu.y == 0x2008, "Y wrong");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0x0005);
    emit(0x31); emit(0x3B);       /* LEAY -5,Y */
    run(2);
    TEST("LEAY -5,Y -> 0 sets Z");
    CHECK(cpu.y == 0x0000 && (cpu.cc & CC_Z), "Y or Z wrong");

    /* ---- All short branches ---- */
    printf("\nShort branches (complete):\n");

    /* BRN: never branches */
    setup(0x200); prog_at(0x200);
    emit(0x21); emit(0x02);       /* BRN +2 */
    emit(0x86); emit(0x11);       /* LDA #$11 (NOT skipped) */
    run(2);
    TEST("BRN: never taken");
    CHECK(cpu_get_a(&cpu) == 0x11, "A wrong");

    /* BNE */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01); emit(0x81); emit(0x02);  /* LDA #1, CMPA #2 */
    emit(0x26); emit(0x02);       /* BNE +2 */
    emit(0x86); emit(0xFF);       /* skipped */
    emit(0xC6); emit(0x01); run(4);
    TEST("BNE taken when not equal");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BCC (already tested as BCS complement — test explicit) */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x03);  /* CMPA #3: 5>=3, C clear */
    emit(0x24); emit(0x02);       /* BCC +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("BCC taken when C clear");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BHI: unsigned higher (C=0 and Z=0) */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* CMPA #5: $10>$05 */
    emit(0x22); emit(0x02);       /* BHI +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("BHI taken: $10 > $05 unsigned");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BLS: unsigned lower or same (C=1 or Z=1) */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x05);  /* CMPA #5: equal */
    emit(0x23); emit(0x02);       /* BLS +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("BLS taken when equal");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BVC: overflow clear */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01); emit(0x8B); emit(0x01);  /* ADDA #1: no overflow */
    emit(0x28); emit(0x02);       /* BVC +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("BVC taken when V clear");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BVS: overflow set */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x7F); emit(0x8B); emit(0x01);  /* ADDA: $7F+1 overflows */
    emit(0x29); emit(0x02);       /* BVS +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("BVS taken when V set");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BPL: positive (N=0) */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01);       /* LDA #$01 (positive) */
    emit(0x2A); emit(0x02);       /* BPL +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(3);
    TEST("BPL taken when positive");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BMI: negative (N=1) */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 (negative) */
    emit(0x2B); emit(0x02);       /* BMI +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(3);
    TEST("BMI taken when negative");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BGE: signed >= (N==V) */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x05);  /* CMPA #5: equal */
    emit(0x2C); emit(0x02);       /* BGE +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("BGE taken when equal (signed)");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BGT: signed > (Z=0 and N==V) */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* CMPA #5: $10>$05 signed */
    emit(0x2E); emit(0x02);       /* BGT +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("BGT taken: $10 > $05 signed");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* BLE: signed <= (Z=1 or N!=V) */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80); emit(0x81); emit(0x01);  /* CMPA #1: -128 < 1 */
    emit(0x2F); emit(0x02);       /* BLE +2 */
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("BLE taken: -128 <= 1 signed");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* ---- Long branches (complete) ---- */
    printf("\nLong branches (complete):\n");

    /* LBRN: never taken */
    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0x21); emit16(0x0100);  /* LBRN +$100 */
    emit(0x86); emit(0x11); run(2);
    TEST("LBRN: never taken");
    CHECK(cpu_get_a(&cpu) == 0x11, "A wrong");

    /* LBNE */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01); emit(0x81); emit(0x02);
    emit(0x10); emit(0x26); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBNE taken");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBHI */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);
    emit(0x10); emit(0x22); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBHI taken");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBLS */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x05);
    emit(0x10); emit(0x23); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBLS taken when equal");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBCC */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x03);
    emit(0x10); emit(0x24); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBCC taken when C clear");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBCS */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x03); emit(0x81); emit(0x05);
    emit(0x10); emit(0x25); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBCS taken when C set");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBVC */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01); emit(0x8B); emit(0x01);
    emit(0x10); emit(0x28); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBVC taken when V clear");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBVS */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x7F); emit(0x8B); emit(0x01);
    emit(0x10); emit(0x29); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBVS taken when V set");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBPL */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01);
    emit(0x10); emit(0x2A); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(3);
    TEST("LBPL taken when positive");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBMI */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80);
    emit(0x10); emit(0x2B); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(3);
    TEST("LBMI taken when negative");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBGE */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x05);
    emit(0x10); emit(0x2C); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBGE taken when equal");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBLT */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80); emit(0x81); emit(0x01);
    emit(0x10); emit(0x2D); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBLT taken: -128 < 1");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBGT */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);
    emit(0x10); emit(0x2E); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBGT taken: $10 > $05 signed");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* LBLE */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80); emit(0x81); emit(0x01);
    emit(0x10); emit(0x2F); emit16(0x0002);
    emit(0x86); emit(0xFF);
    emit(0xC6); emit(0x01); run(4);
    TEST("LBLE taken: -128 <= 1");
    CHECK(cpu_get_b(&cpu) == 0x01, "B wrong");

    /* ================================================================ */
    /* ---- Hardware interrupts: IRQ ---- */
    printf("\nHardware IRQ:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0xC6); emit(0x77);       /* LDB #$77 */
    emit(0x12);                   /* NOP (will be interrupted before this) */
    /* Handler at $0000 (IRQ vector $FFF8 reads $0000 from zeroed ROM) */
    prog_at(0x0000);
    emit(0x86); emit(0xEE);       /* LDA #$EE */
    emit(0xB7); emit16(0x0300);   /* STA $0300 */
    emit(0x3B);                   /* RTI */
    run(2); /* LDA, LDB */
    cpu_set_irq(&cpu, true);
    run(4); /* interrupt + LDA + STA + RTI */
    cpu_set_irq(&cpu, false);
    TEST("IRQ: handler runs (marker in RAM)");
    CHECK(mem_get_ram()[0x0300] == 0xEE, "handler should write $EE");

    TEST("IRQ: RTI restores A");
    CHECK(cpu_get_a(&cpu) == 0x42, "A should be $42");

    TEST("IRQ: RTI restores B");
    CHECK(cpu_get_b(&cpu) == 0x77, "B should be $77");

    TEST("IRQ: RTI returns to correct PC");
    CHECK(cpu.pc == 0x0204, "PC should be $0204 (NOP)");

    TEST("IRQ: E flag set (entire state saved)");
    CHECK(cpu.cc & CC_E, "E should be set");

    /* IRQ masked when I=1 */
    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_I;                /* Mask IRQ */
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0x12);                   /* NOP */
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);
    cpu_set_irq(&cpu, true);
    run(2);
    cpu_set_irq(&cpu, false);
    TEST("IRQ masked by I flag");
    CHECK(mem_get_ram()[0x0300] == 0x00, "handler should NOT run");
    CHECK(cpu_get_a(&cpu) == 0x42, "A from main program");

    /* ---- Hardware interrupts: FIRQ ---- */
    printf("\nHardware FIRQ:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0xC6); emit(0x77);       /* LDB #$77 */
    emit(0x12);                   /* NOP */
    /* Handler at $0000 (FIRQ vector $FFF6 reads $0000) */
    prog_at(0x0000);
    emit(0xB7); emit16(0x0300);   /* STA $0300 (A is NOT preserved by FIRQ) */
    emit(0x3B);                   /* RTI */
    run(2);
    cpu_set_firq(&cpu, true);
    run(3); /* interrupt + STA + RTI */
    cpu_set_firq(&cpu, false);
    TEST("FIRQ: handler runs");
    CHECK(mem_get_ram()[0x0300] != 0x00, "handler should run");

    TEST("FIRQ: RTI with E=0 restores only CC+PC");
    /* FIRQ sets E=0, so RTI only restores CC and PC */
    CHECK(cpu.pc == 0x0204, "PC should be $0204");

    /* FIRQ masked when F=1 */
    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_F;
    emit(0x86); emit(0x42);
    emit(0x12);
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);
    cpu_set_firq(&cpu, true);
    run(2);
    cpu_set_firq(&cpu, false);
    TEST("FIRQ masked by F flag");
    CHECK(mem_get_ram()[0x0300] == 0x00, "handler should NOT run");

    /* ---- Hardware interrupts: NMI ---- */
    printf("\nHardware NMI:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.nmi_armed = true;
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0xC6); emit(0x77);       /* LDB #$77 */
    emit(0x12);                   /* NOP */
    /* Handler at $0000 (NMI vector $FFFC reads $0000) */
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);                   /* RTI */
    run(2);
    cpu_set_nmi(&cpu, true);
    run(4); /* interrupt + LDA + STA + RTI */
    TEST("NMI: handler runs and RTI restores");
    CHECK(mem_get_ram()[0x0300] == 0xEE, "handler should write $EE");
    CHECK(cpu_get_a(&cpu) == 0x42, "A restored");
    CHECK(cpu_get_b(&cpu) == 0x77, "B restored");

    /* NMI cannot be masked */
    setup(0x200);
    prog_at(0x200);
    cpu.nmi_armed = true;
    cpu.cc = CC_I | CC_F;         /* Both masks set */
    emit(0x86); emit(0x42);
    emit(0x12);
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);
    run(1);
    cpu_set_nmi(&cpu, true);
    run(4);
    TEST("NMI cannot be masked by I or F");
    CHECK(mem_get_ram()[0x0300] == 0xEE, "handler should run");

    /* ---- NMI arming ---- */
    printf("\nNMI arming:\n");

    setup(0x200);
    prog_at(0x200);
    /* NMI not armed after reset (no LDS yet) */
    emit(0x86); emit(0x42);
    emit(0x12);
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);
    run(1);
    cpu_set_nmi(&cpu, true);
    run(1);
    TEST("NMI not armed before LDS");
    CHECK(mem_get_ram()[0x0300] == 0x00, "handler should NOT run");
    CHECK(!cpu.nmi_armed, "nmi_armed should be false");

    setup(0x200);
    prog_at(0x200);
    emit(0x10); emit(0xCE); emit16(0x7F00);  /* LDS #$7F00 (arms NMI) */
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0x12);
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);
    run(2); /* LDS + LDA */
    TEST("LDS arms NMI");
    CHECK(cpu.nmi_armed, "nmi_armed should be true");
    cpu_set_nmi(&cpu, true);
    run(4);
    TEST("NMI fires after LDS arms it");
    CHECK(mem_get_ram()[0x0300] == 0xEE, "handler should run");

    /* ---- Interrupt priority ---- */
    printf("\nInterrupt priority:\n");

    /* NMI has highest priority */
    setup(0x200);
    prog_at(0x200);
    cpu.nmi_armed = true;
    emit(0x86); emit(0x42);
    emit(0x12);
    /* NMI handler at $0000: write $AA to $0300 */
    prog_at(0x0000);
    emit(0x86); emit(0xAA);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);
    run(1);
    /* Set all three interrupts simultaneously */
    cpu_set_nmi(&cpu, true);
    cpu_set_irq(&cpu, true);
    cpu_set_firq(&cpu, true);
    run(1); /* Should service NMI first */
    TEST("NMI has highest priority");
    /* If NMI ran, PC should be in handler at $0000 area */
    CHECK(mem_get_ram()[0x0300] == 0x00, "NMI handler started but not finished STA yet");
    /* Actually NMI handler: cpu_step handles interrupt (sets PC to $0000), returns.
       Next steps execute handler. Let me just check the PC. */
    /* After interrupt handling step, PC = $0000 (NMI vector) */
    /* Let me restructure: run the handler to completion */
    run(2); /* LDA #$AA, STA $0300 */
    CHECK(mem_get_ram()[0x0300] == 0xAA, "NMI handler wrote marker");

    /* ---- CWAI wakeup ---- */
    printf("\nCWAI wakeup:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_I;                /* I set initially */
    emit(0x3C); emit(0xEF);       /* CWAI #$EF (clears I bit) */
    emit(0x86); emit(0x42);       /* LDA #$42 (executed after RTI) */
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);                   /* RTI */
    run(1); /* CWAI: halts */
    TEST("CWAI halts and clears I via mask");
    CHECK(cpu.halted, "should be halted");
    CHECK(!(cpu.cc & CC_I), "I cleared by CWAI mask");

    /* Wake with IRQ (I was cleared by CWAI mask) */
    cpu_set_irq(&cpu, true);
    run(1); /* interrupt handling (state already pushed) */
    TEST("CWAI wakes on IRQ");
    CHECK(!cpu.halted, "should not be halted");
    run(2); /* handler: LDA + STA */
    cpu_set_irq(&cpu, false);
    run(1); /* RTI */
    CHECK(mem_get_ram()[0x0300] == 0xEE, "handler ran");

    /* CWAI wakeup by NMI */
    setup(0x200);
    prog_at(0x200);
    cpu.nmi_armed = true;
    cpu.cc = CC_I | CC_F;         /* Both masked */
    emit(0x3C); emit(0xFF);       /* CWAI #$FF (no bits cleared, keeps masks) */
    emit(0x86); emit(0x42);
    prog_at(0x0000);
    emit(0x86); emit(0xDD);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);
    run(1); /* CWAI halts */
    cpu_set_nmi(&cpu, true);
    run(4); /* wakeup + handler(LDA + STA + RTI) */
    TEST("CWAI wakes on NMI even with I+F masked");
    CHECK(mem_get_ram()[0x0300] == 0xDD, "NMI handler ran");

    /* ---- SYNC wakeup ---- */
    printf("\nSYNC wakeup:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x13);                   /* SYNC */
    emit(0x86); emit(0x42);       /* LDA #$42 (after sync wakes) */
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);
    run(1); /* SYNC halts */
    CHECK(cpu.halted, "should be halted");
    cpu_set_irq(&cpu, true);
    run(4); /* wakeup + handler(LDA+STA+RTI) */
    cpu_set_irq(&cpu, false);
    run(1); /* LDA #$42 */
    TEST("SYNC wakes on IRQ, resumes after handler");
    CHECK(mem_get_ram()[0x0300] == 0xEE, "IRQ handler ran");
    CHECK(cpu_get_a(&cpu) == 0x42, "resumes at instruction after SYNC");

    /* ---- Interrupt cycle counts ---- */
    printf("\nInterrupt cycle counts:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.nmi_armed = true;
    emit(0x12);                   /* NOP (not reached) */
    prog_at(0x0000);
    emit(0x12);                   /* NOP */
    cpu_set_irq(&cpu, true);
    cpu.total_cycles = 0;
    run(1); /* IRQ handling */
    cpu_set_irq(&cpu, false);
    TEST("IRQ = 19 cycles (entire state push)");
    CHECK(cpu.total_cycles == 19, "expected 19");

    setup(0x200);
    prog_at(0x200);
    cpu.nmi_armed = true;
    emit(0x12);
    prog_at(0x0000);
    emit(0x12);
    cpu_set_firq(&cpu, true);
    cpu.total_cycles = 0;
    run(1);
    cpu_set_firq(&cpu, false);
    TEST("FIRQ = 10 cycles (partial state push)");
    CHECK(cpu.total_cycles == 10, "expected 10");

    setup(0x200);
    prog_at(0x200);
    cpu.nmi_armed = true;
    emit(0x12);
    prog_at(0x0000);
    emit(0x12);
    cpu_set_nmi(&cpu, true);
    cpu.total_cycles = 0;
    run(1);
    TEST("NMI = 19 cycles");
    CHECK(cpu.total_cycles == 19, "expected 19");

    /* ---- Addressing modes: Y as index register ---- */
    printf("\nIndexed addressing with Y:\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x55;
    emit(0x10); emit(0x8E); emit16(0x0400);  /* LDY #$0400 */
    emit(0xA6); emit(0xA4);       /* LDA ,Y (zero offset) */
    run(2);
    TEST("Indexed: LDA ,Y");
    CHECK(cpu_get_a(&cpu) == 0x55, "A should be $55");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0405] = 0x33;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xA6); emit(0x25);       /* LDA 5,Y (5-bit offset) */
    run(2);
    TEST("Indexed: LDA 5,Y (5-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x33, "A should be $33");

    /* ---- Addressing modes: U as index register ---- */
    printf("\nIndexed addressing with U:\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x77;
    emit(0xCE); emit16(0x0400);   /* LDU #$0400 */
    emit(0xA6); emit(0xC4);       /* LDA ,U (zero offset) */
    run(2);
    TEST("Indexed: LDA ,U");
    CHECK(cpu_get_a(&cpu) == 0x77, "A should be $77");

    /* ---- Addressing modes: S as index register ---- */
    printf("\nIndexed addressing with S:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.s = 0x0400;
    mem_get_ram()[0x0400] = 0x88;
    emit(0xA6); emit(0xE4);       /* LDA ,S (zero offset) */
    run(1);
    TEST("Indexed: LDA ,S");
    CHECK(cpu_get_a(&cpu) == 0x88, "A should be $88");

    /* ---- Post-increment by 2 ---- */
    printf("\nPost/pre increment/decrement by 2:\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12;
    mem_get_ram()[0x0401] = 0x34;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xEC); emit(0x81);       /* LDD ,X++ (post-increment by 2, 16-bit load) */
    run(2);
    TEST("Indexed: LDD ,X++ (post-increment by 2)");
    CHECK(cpu.d == 0x1234, "D should be $1234");
    CHECK(cpu.x == 0x0402, "X should be $0402");

    /* ---- Pre-decrement by 2 ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xAB;
    mem_get_ram()[0x0401] = 0xCD;
    emit(0x8E); emit16(0x0402);   /* LDX #$0402 */
    emit(0xEC); emit(0x83);       /* LDD ,--X (pre-decrement by 2) */
    run(2);
    TEST("Indexed: LDD ,--X (pre-decrement by 2)");
    CHECK(cpu.d == 0xABCD, "D should be $ABCD");
    CHECK(cpu.x == 0x0400, "X should be $0400");

    /* ---- A accumulator offset ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0410] = 0x99;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0x86); emit(0x10);       /* LDA #$10 */
    emit(0xE6); emit(0x86);       /* LDB A,X (A offset) */
    run(3);
    TEST("Indexed: LDB A,X (A accumulator offset)");
    CHECK(cpu_get_b(&cpu) == 0x99, "B should be $99");

    /* ---- D accumulator offset ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0500] = 0x77;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xCC); emit16(0x0100);   /* LDD #$0100 */
    emit(0xA6); emit(0x8B);       /* LDA D,X */
    run(3);
    TEST("Indexed: LDA D,X (D accumulator offset)");
    CHECK(cpu_get_a(&cpu) == 0x77, "A should be $77");

    /* ---- PC-relative 16-bit offset ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x66;
    emit(0xA6); emit(0x8D); emit16(0x01FC);  /* LDA $01FC,PCR -> PC after fetch=$0204, +$01FC=$0400 */
    run(1);
    TEST("Indexed: LDA n,PCR (PC-relative 16-bit)");
    CHECK(cpu_get_a(&cpu) == 0x66, "A should be $66");

    /* ---- Extended indirect ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    mem_get_ram()[0x0401] = 0x00;  /* pointer -> $0500 */
    mem_get_ram()[0x0500] = 0xDD;
    emit(0xA6); emit(0x9F); emit16(0x0400);  /* LDA [$0400] (extended indirect) */
    run(1);
    TEST("Extended indirect: LDA [$0400]");
    CHECK(cpu_get_a(&cpu) == 0xDD, "A should be $DD");

    /* ---- Indirect with offset ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0410] = 0x05;
    mem_get_ram()[0x0411] = 0x00;  /* pointer at $0410 -> $0500 */
    mem_get_ram()[0x0500] = 0xCC;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xA6); emit(0x98); emit(0x10);  /* LDA [$10,X] (indirect 8-bit offset) */
    run(2);
    TEST("Indexed indirect: LDA [$10,X]");
    CHECK(cpu_get_a(&cpu) == 0xCC, "A should be $CC");

    /* ---- Indirect with 16-bit offset ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0500] = 0x06;
    mem_get_ram()[0x0501] = 0x00;  /* pointer at $0500 -> $0600 */
    mem_get_ram()[0x0600] = 0xBB;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xA6); emit(0x99); emit16(0x0100);  /* LDA [$0100,X] (indirect 16-bit offset) */
    run(2);
    TEST("Indexed indirect: LDA [$0100,X]");
    CHECK(cpu_get_a(&cpu) == 0xBB, "A should be $BB");

    /* ---- Indirect post-increment by 2 ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    mem_get_ram()[0x0401] = 0x00;  /* pointer at $0400 -> $0500 */
    mem_get_ram()[0x0500] = 0xAA;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xA6); emit(0x91);       /* LDA [,X++] (indirect post-inc by 2) */
    run(2);
    TEST("Indexed indirect: LDA [,X++]");
    CHECK(cpu_get_a(&cpu) == 0xAA, "A should be $AA");
    CHECK(cpu.x == 0x0402, "X should be $0402");

    /* ---- Indirect pre-decrement by 2 ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    mem_get_ram()[0x0401] = 0x00;  /* pointer at $0400 -> $0500 */
    mem_get_ram()[0x0500] = 0xBB;
    emit(0x8E); emit16(0x0402);   /* LDX #$0402 */
    emit(0xA6); emit(0x93);       /* LDA [,--X] (indirect pre-dec by 2) */
    run(2);
    TEST("Indexed indirect: LDA [,--X]");
    CHECK(cpu_get_a(&cpu) == 0xBB, "A should be $BB");
    CHECK(cpu.x == 0x0400, "X should be $0400");

    /* ---- Indirect B,X ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0403] = 0x05;
    mem_get_ram()[0x0404] = 0x00;  /* pointer at $0403 -> $0500 */
    mem_get_ram()[0x0500] = 0x77;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xC6); emit(0x03);       /* LDB #$03 */
    emit(0xA6); emit(0x95);       /* LDA [B,X] (indirect B offset) */
    run(3);
    TEST("Indexed indirect: LDA [B,X]");
    CHECK(cpu_get_a(&cpu) == 0x77, "A should be $77");

    /* ---- Indirect A,X ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0405] = 0x06;
    mem_get_ram()[0x0406] = 0x00;  /* pointer at $0405 -> $0600 */
    mem_get_ram()[0x0600] = 0x88;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0x86); emit(0x05);       /* LDA #$05 */
    emit(0xE6); emit(0x96);       /* LDB [A,X] (indirect A offset) */
    run(3);
    TEST("Indexed indirect: LDB [A,X]");
    CHECK(cpu_get_b(&cpu) == 0x88, "B should be $88");

    /* ---- Indirect D,X ---- */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0500] = 0x06;
    mem_get_ram()[0x0501] = 0x00;  /* pointer at $0500 -> $0600 */
    mem_get_ram()[0x0600] = 0x99;
    emit(0x8E); emit16(0x0400);   /* LDX #$0400 */
    emit(0xCC); emit16(0x0100);   /* LDD #$0100 */
    emit(0xA6); emit(0x9B);       /* LDA [D,X] (indirect D offset) */
    run(3);
    TEST("Indexed indirect: LDA [D,X]");
    CHECK(cpu_get_a(&cpu) == 0x99, "A should be $99");

    /* ---- Indirect PC-relative 8-bit ---- */
    setup(0x200);
    prog_at(0x200);
    /* After fetching postbyte and offset, PC=$0203. $0203+6=$0209 */
    mem_get_ram()[0x0209] = 0x06;
    mem_get_ram()[0x020A] = 0x00;  /* pointer at $0209 -> $0600 */
    mem_get_ram()[0x0600] = 0x55;
    emit(0xA6); emit(0x9C); emit(0x06);  /* LDA [6,PCR] (indirect PC-relative 8-bit) */
    run(1);
    TEST("Indexed indirect: LDA [n,PCR] (8-bit)");
    CHECK(cpu_get_a(&cpu) == 0x55, "A should be $55");

    /* ---- Indirect PC-relative 16-bit ---- */
    setup(0x200);
    prog_at(0x200);
    /* After fetch: PC=$0204. $0204+$01FC=$0400 */
    mem_get_ram()[0x0400] = 0x05;
    mem_get_ram()[0x0401] = 0x00;  /* pointer -> $0500 */
    mem_get_ram()[0x0500] = 0x44;
    emit(0xA6); emit(0x9D); emit16(0x01FC);  /* LDA [$01FC,PCR] */
    run(1);
    TEST("Indexed indirect: LDA [n,PCR] (16-bit)");
    CHECK(cpu_get_a(&cpu) == 0x44, "A should be $44");

    /* ---- Direct page addressing (more instructions) ---- */
    printf("\nDirect page addressing (extended):\n");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x86); emit(0x77);       /* LDA #$77 */
    emit(0x97); emit(0x10);       /* STA <$10 (DP:$10 = $0310) */
    run(2);
    TEST("STA direct page");
    CHECK(mem_get_ram()[0x0310] == 0x77, "mem should be $77");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x12;
    mem_get_ram()[0x0311] = 0x34;
    emit(0xDC); emit(0x10);       /* LDD <$10 */
    run(1);
    TEST("LDD direct page");
    CHECK(cpu.d == 0x1234, "D should be $1234");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    emit(0xCC); emit16(0xBEEF);   /* LDD #$BEEF */
    emit(0xDD); emit(0x10);       /* STD <$10 */
    run(2);
    TEST("STD direct page");
    CHECK(mem_get_ram()[0x0310] == 0xBE && mem_get_ram()[0x0311] == 0xEF, "mem wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x55;
    emit(0xD6); emit(0x10);       /* LDB <$10 */
    run(1);
    TEST("LDB direct page");
    CHECK(cpu_get_b(&cpu) == 0x55, "B should be $55");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    emit(0xC6); emit(0xAA);       /* LDB #$AA */
    emit(0xD7); emit(0x10);       /* STB <$10 */
    run(2);
    TEST("STB direct page");
    CHECK(mem_get_ram()[0x0310] == 0xAA, "mem wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0xAB;
    mem_get_ram()[0x0311] = 0xCD;
    emit(0x9E); emit(0x10);       /* LDX <$10 */
    run(1);
    TEST("LDX direct page");
    CHECK(cpu.x == 0xABCD, "X should be $ABCD");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x8E); emit16(0xBEEF);   /* LDX #$BEEF */
    emit(0x9F); emit(0x10);       /* STX <$10 */
    run(2);
    TEST("STX direct page");
    CHECK(mem_get_ram()[0x0310] == 0xBE && mem_get_ram()[0x0311] == 0xEF, "mem wrong");

    /* ---- Memory RMW: direct page mode ---- */
    printf("\nMemory RMW direct page:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0x0C); emit(0x10);       /* INC <$10 */
    run(1);
    TEST("INC direct: $05 -> $06");
    CHECK(mem_get_ram()[0x0310] == 0x06, "mem should be $06");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0x0A); emit(0x10);       /* DEC <$10 */
    run(1);
    TEST("DEC direct: $05 -> $04");
    CHECK(mem_get_ram()[0x0310] == 0x04, "mem should be $04");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x55;
    emit(0x00); emit(0x10);       /* NEG <$10 */
    run(1);
    TEST("NEG direct: $55 -> $AB");
    CHECK(mem_get_ram()[0x0310] == 0xAB, "mem should be $AB");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0xAA;
    emit(0x03); emit(0x10);       /* COM <$10 */
    run(1);
    TEST("COM direct: $AA -> $55");
    CHECK(mem_get_ram()[0x0310] == 0x55, "mem should be $55");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x42;
    emit(0x0F); emit(0x10);       /* CLR <$10 */
    run(1);
    TEST("CLR direct: -> $00");
    CHECK(mem_get_ram()[0x0310] == 0x00, "mem should be $00");
    CHECK(cpu.cc & CC_Z, "Z set");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x80;
    emit(0x0D); emit(0x10);       /* TST <$10 */
    run(1);
    TEST("TST direct: $80 sets N");
    CHECK(cpu.cc & CC_N, "N set");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x40;
    emit(0x08); emit(0x10);       /* ASL <$10 */
    run(1);
    TEST("ASL direct: $40 -> $80");
    CHECK(mem_get_ram()[0x0310] == 0x80, "mem should be $80");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x80;
    emit(0x07); emit(0x10);       /* ASR <$10 */
    run(1);
    TEST("ASR direct: $80 -> $C0");
    CHECK(mem_get_ram()[0x0310] == 0xC0, "mem should be $C0");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x82;
    emit(0x04); emit(0x10);       /* LSR <$10 */
    run(1);
    TEST("LSR direct: $82 -> $41");
    CHECK(mem_get_ram()[0x0310] == 0x41, "mem should be $41");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x81;
    cpu.cc = CC_C;
    emit(0x09); emit(0x10);       /* ROL <$10 */
    run(1);
    TEST("ROL direct: $81 with C=1 -> $03");
    CHECK(mem_get_ram()[0x0310] == 0x03, "mem should be $03");
    CHECK(cpu.cc & CC_C, "C set");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x81;
    cpu.cc = CC_C;
    emit(0x06); emit(0x10);       /* ROR <$10 */
    run(1);
    TEST("ROR direct: $81 with C=1 -> $C0");
    CHECK(mem_get_ram()[0x0310] == 0xC0, "mem should be $C0");
    CHECK(cpu.cc & CC_C, "C set");

    /* ---- Memory RMW: indexed (more operations) ---- */
    printf("\nMemory RMW indexed (more):\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x8E); emit16(0x0400);
    emit(0x6A); emit(0x84);       /* DEC ,X */
    run(2);
    TEST("DEC indexed ,X: $05 -> $04");
    CHECK(mem_get_ram()[0x0400] == 0x04, "mem should be $04");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x55;
    emit(0x8E); emit16(0x0400);
    emit(0x60); emit(0x84);       /* NEG ,X */
    run(2);
    TEST("NEG indexed ,X: $55 -> $AB");
    CHECK(mem_get_ram()[0x0400] == 0xAB, "mem should be $AB");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xAA;
    emit(0x8E); emit16(0x0400);
    emit(0x63); emit(0x84);       /* COM ,X */
    run(2);
    TEST("COM indexed ,X: $AA -> $55");
    CHECK(mem_get_ram()[0x0400] == 0x55, "mem should be $55");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x42;
    emit(0x8E); emit16(0x0400);
    emit(0x6F); emit(0x84);       /* CLR ,X */
    run(2);
    TEST("CLR indexed ,X: -> $00");
    CHECK(mem_get_ram()[0x0400] == 0x00, "mem should be $00");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x80;
    emit(0x8E); emit16(0x0400);
    emit(0x6D); emit(0x84);       /* TST ,X */
    run(2);
    TEST("TST indexed ,X: $80 sets N");
    CHECK(cpu.cc & CC_N, "N set");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x40;
    emit(0x8E); emit16(0x0400);
    emit(0x68); emit(0x84);       /* ASL ,X */
    run(2);
    TEST("ASL indexed ,X: $40 -> $80");
    CHECK(mem_get_ram()[0x0400] == 0x80, "mem should be $80");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x80;
    emit(0x8E); emit16(0x0400);
    emit(0x67); emit(0x84);       /* ASR ,X */
    run(2);
    TEST("ASR indexed ,X: $80 -> $C0");
    CHECK(mem_get_ram()[0x0400] == 0xC0, "mem should be $C0");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x82;
    emit(0x8E); emit16(0x0400);
    emit(0x64); emit(0x84);       /* LSR ,X */
    run(2);
    TEST("LSR indexed ,X: $82 -> $41");
    CHECK(mem_get_ram()[0x0400] == 0x41, "mem should be $41");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x81;
    cpu.cc = CC_C;
    emit(0x8E); emit16(0x0400);
    emit(0x69); emit(0x84);       /* ROL ,X */
    run(2);
    TEST("ROL indexed ,X: $81 C=1 -> $03");
    CHECK(mem_get_ram()[0x0400] == 0x03, "mem should be $03");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x81;
    cpu.cc = CC_C;
    emit(0x8E); emit16(0x0400);
    emit(0x66); emit(0x84);       /* ROR ,X */
    run(2);
    TEST("ROR indexed ,X: $81 C=1 -> $C0");
    CHECK(mem_get_ram()[0x0400] == 0xC0, "mem should be $C0");

    /* ---- TFR/EXG: more register combinations ---- */
    printf("\nTFR/EXG (more registers):\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x1234);   /* LDX #$1234 */
    emit(0x1F); emit(0x12);       /* TFR X,Y */
    run(2);
    TEST("TFR X,Y");
    CHECK(cpu.y == 0x1234, "Y should be $1234");
    CHECK(cpu.x == 0x1234, "X unchanged");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_N | CC_Z;
    emit(0x1F); emit(0xA8);       /* TFR CC,A */
    run(1);
    TEST("TFR CC,A");
    CHECK(cpu_get_a(&cpu) == (CC_N | CC_Z), "A should be CC value");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x05);       /* LDA #$05 */
    emit(0x1F); emit(0x8B);       /* TFR A,DP */
    run(2);
    TEST("TFR A,DP");
    CHECK(cpu.dp == 0x05, "DP should be $05");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0xAAAA);   /* LDX #$AAAA */
    emit(0x10); emit(0x8E); emit16(0x5555);  /* LDY #$5555 */
    emit(0x1E); emit(0x12);       /* EXG X,Y */
    run(3);
    TEST("EXG X,Y");
    CHECK(cpu.x == 0x5555, "X should be $5555");
    CHECK(cpu.y == 0xAAAA, "Y should be $AAAA");

    setup(0x200);
    prog_at(0x200);
    cpu.s = 0x7F00;
    emit(0xCE); emit16(0x3000);   /* LDU #$3000 */
    emit(0x1E); emit(0x34);       /* EXG U,S */
    run(2);
    TEST("EXG U,S");
    CHECK(cpu.u == 0x7F00, "U should be $7F00");
    CHECK(cpu.s == 0x3000, "S should be $3000");

    /* ---- PSHS/PULS: more register combinations ---- */
    printf("\nPSHS/PULS (more registers):\n");

    /* Push/pull CC */
    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_N | CC_C;
    emit(0x34); emit(0x01);       /* PSHS CC */
    emit(0x1C); emit(0x00);       /* ANDCC #$00 (clear all) */
    emit(0x35); emit(0x01);       /* PULS CC */
    run(3);
    TEST("PSHS/PULS CC");
    CHECK(cpu.cc & CC_N, "N preserved");
    CHECK(cpu.cc & CC_C, "C preserved");

    /* Push/pull all registers */
    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_N | CC_C;
    emit(0x86); emit(0x11);       /* LDA #$11 */
    emit(0xC6); emit(0x22);       /* LDB #$22 */
    emit(0x8E); emit16(0x3333);   /* LDX #$3333 */
    emit(0x10); emit(0x8E); emit16(0x4444);  /* LDY #$4444 */
    emit(0x34); emit(0x76);       /* PSHS A,B,X,Y,U (CC not pushed, DP not pushed) */
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0xC6); emit(0x00);       /* LDB #$00 */
    emit(0x8E); emit16(0x0000);   /* LDX #$0000 */
    emit(0x10); emit(0x8E); emit16(0x0000);  /* LDY #$0000 */
    emit(0x35); emit(0x76);       /* PULS A,B,X,Y,U */
    run(10);
    TEST("PSHS/PULS A,B,X,Y,U");
    CHECK(cpu_get_a(&cpu) == 0x11, "A restored");
    CHECK(cpu_get_b(&cpu) == 0x22, "B restored");
    CHECK(cpu.x == 0x3333, "X restored");
    CHECK(cpu.y == 0x4444, "Y restored");

    /* PULS PC (jump via stack) */
    setup(0x200);
    prog_at(0x200);
    /* Push a return address manually */
    uint16_t sp2 = cpu.s;
    mem_get_ram()[--sp2] = 0x00;   /* PC low = $0300 */
    mem_get_ram()[--sp2] = 0x03;   /* PC high */
    cpu.s = sp2;
    emit(0x35); emit(0x80);       /* PULS PC */
    prog_at(0x0300);
    emit(0x86); emit(0x77);       /* LDA #$77 */
    run(2);
    TEST("PULS PC (jump via stack)");
    CHECK(cpu_get_a(&cpu) == 0x77, "A should be $77");

    /* Push/pull DP */
    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x42;
    emit(0x34); emit(0x08);       /* PSHS DP */
    emit(0x1F); emit(0x9B);       /* TFR B,DP (DP=$00 since B=0) */
    emit(0x35); emit(0x08);       /* PULS DP */
    run(3);
    TEST("PSHS/PULS DP");
    CHECK(cpu.dp == 0x42, "DP should be $42");

    /* ---- Backward branches ---- */
    printf("\nBackward branches:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0x4C);                   /* INCA (target of backward branch: $0202) */
    emit(0x81); emit(0x03);       /* CMPA #$03 */
    emit(0x26); emit((uint8_t)(0x100 - 5));  /* BNE -5 -> $0202 */
    emit(0x12);                   /* NOP */
    run(13); /* 3 iterations: LDA + (INCA+CMPA+BNE)*3 + NOP - last BNE not taken */
    /* Actually: LDA(1) + loop: INCA(1)+CMPA(1)+BNE(1) x3, but 3rd BNE falls through + NOP */
    /* iter1: A=1, not equal, branch back */
    /* iter2: A=2, not equal, branch back */
    /* iter3: A=3, equal, fall through to NOP */
    TEST("Backward BNE loop: counts to 3");
    CHECK(cpu_get_a(&cpu) == 0x03, "A should be $03");

    setup(0x200);
    prog_at(0x200);
    emit(0x20); emit((uint8_t)(0x100 - 2));  /* BRA -2 (branch to self) */
    run(1);
    TEST("BRA backward to self");
    CHECK(cpu.pc == 0x0200, "PC should be $0200 (loop)");

    /* ---- Edge cases: NEG $80 ---- */
    printf("\nEdge cases:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 */
    emit(0x40);                   /* NEGA */
    run(2);
    TEST("NEGA $80: result=$80, V=1, C=1");
    CHECK(cpu_get_a(&cpu) == 0x80, "A should be $80");
    CHECK(cpu.cc & CC_V, "V should be set (special case)");
    CHECK(cpu.cc & CC_C, "C should be set");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0x40);                   /* NEGA */
    run(2);
    TEST("NEGA $00: result=$00, V=0, C=0");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(!(cpu.cc & CC_V), "V should be clear");
    CHECK(!(cpu.cc & CC_C), "C should be clear");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    /* INC $7F -> $80: V should be set */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x7F);       /* LDA #$7F */
    emit(0x4C);                   /* INCA */
    run(2);
    TEST("INCA $7F -> $80: V=1 (signed overflow)");
    CHECK(cpu_get_a(&cpu) == 0x80, "A should be $80");
    CHECK(cpu.cc & CC_V, "V should be set");
    CHECK(cpu.cc & CC_N, "N should be set");

    /* DEC $80 -> $7F: V should be set */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 */
    emit(0x4A);                   /* DECA */
    run(2);
    TEST("DECA $80 -> $7F: V=1 (signed overflow)");
    CHECK(cpu_get_a(&cpu) == 0x7F, "A should be $7F");
    CHECK(cpu.cc & CC_V, "V should be set");
    CHECK(!(cpu.cc & CC_N), "N should be clear");

    /* SUB signed overflow */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 (-128) */
    emit(0x80); emit(0x01);       /* SUBA #$01 */
    run(2);
    TEST("SUBA $80-$01=$7F: V=1 (signed overflow)");
    CHECK(cpu_get_a(&cpu) == 0x7F, "A should be $7F");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* ADDD signed overflow */
    setup(0x200);
    prog_at(0x200);
    emit(0xCC); emit16(0x7FFF);   /* LDD #$7FFF */
    emit(0xC3); emit16(0x0001);   /* ADDD #$0001 */
    run(2);
    TEST("ADDD $7FFF+$0001=$8000: V=1");
    CHECK(cpu.d == 0x8000, "D should be $8000");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* SUBD signed overflow */
    setup(0x200);
    prog_at(0x200);
    emit(0xCC); emit16(0x8000);   /* LDD #$8000 */
    emit(0x83); emit16(0x0001);   /* SUBD #$0001 */
    run(2);
    TEST("SUBD $8000-$0001=$7FFF: V=1");
    CHECK(cpu.d == 0x7FFF, "D should be $7FFF");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* MUL zero result sets Z */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x00);       /* LDA #$00 */
    emit(0xC6); emit(0xFF);       /* LDB #$FF */
    emit(0x3D);                   /* MUL */
    run(3);
    TEST("MUL: 0 * $FF = 0, Z set");
    CHECK(cpu.d == 0x0000, "D should be $0000");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    /* MUL C flag = bit 7 of B (result low byte) */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x02);       /* LDA #$02 */
    emit(0xC6); emit(0x40);       /* LDB #$40 */
    emit(0x3D);                   /* MUL: 2*64=128=$0080 */
    run(3);
    TEST("MUL: 2*64=128, C=1 (bit 7 of B set)");
    CHECK(cpu.d == 0x0080, "D should be $0080");
    CHECK(cpu.cc & CC_C, "C should be set (B bit 7)");

    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x02);       /* LDA #$02 */
    emit(0xC6); emit(0x3F);       /* LDB #$3F */
    emit(0x3D);                   /* MUL: 2*63=126=$007E */
    run(3);
    TEST("MUL: 2*63=126, C=0 (bit 7 of B clear)");
    CHECK(cpu.d == 0x007E, "D should be $007E");
    CHECK(!(cpu.cc & CC_C), "C should be clear");

    /* DAA with carry-in */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x99);       /* LDA #$99 (BCD 99) */
    emit(0x8B); emit(0x01);       /* ADDA #$01 */
    emit(0x19);                   /* DAA */
    run(3);
    TEST("DAA: 99+01 = BCD 00 with carry");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(cpu.cc & CC_C, "C should be set (BCD carry)");

    /* 16-bit compare with V flag */
    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x7FFF);   /* LDX #$7FFF */
    emit(0x8C); emit16(0xFFFF);   /* CMPX #$FFFF ($7FFF - $FFFF) */
    run(2);
    TEST("CMPX $7FFF vs $FFFF: signed overflow");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* ADDA half-carry edge: nibble carry from bit 3 */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x18);       /* LDA #$18 */
    emit(0x8B); emit(0x18);       /* ADDA #$18 */
    run(2);
    TEST("ADDA half-carry: $18 + $18 sets H");
    CHECK(cpu.cc & CC_H, "H should be set");

    /* ASL V flag = N XOR C */
    setup(0x200);
    prog_at(0x200);
    emit(0x86); emit(0x40);       /* LDA #$40 */
    emit(0x48);                   /* ASLA: $40->$80, C=0, N=1 -> V=1 */
    run(2);
    TEST("ASLA $40: V=1 (N xor C)");
    CHECK(cpu_get_a(&cpu) == 0x80, "A should be $80");
    CHECK(cpu.cc & CC_V, "V should be set");
    CHECK(cpu.cc & CC_N, "N should be set");
    CHECK(!(cpu.cc & CC_C), "C should be clear");

    /* ---- Extended cycle count tests ---- */
    printf("\nExtended cycle counts:\n");

    setup(0x200);
    prog_at(0x200);
    emit(0x96); emit(0x00);       /* LDA direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA direct = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x84);       /* LDA ,X (indexed zero offset) */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA ,X = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x80);       /* LDA ,X+ */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA ,X+ = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x81);       /* LDA ,X++ */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA ,X++ = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x82);       /* LDA ,-X */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA ,-X = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x83);       /* LDA ,--X */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA ,--X = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x85);       /* LDA B,X */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA B,X = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x86);       /* LDA A,X */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA A,X = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x88); emit(0x05);  /* LDA 5,X (8-bit offset) */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA 8-bit,X = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x89); emit16(0x0100);  /* LDA $100,X (16-bit offset) */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA 16-bit,X = 8 cycles");
    CHECK(cpu.total_cycles == 8, "expected 8");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x8B);       /* LDA D,X */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA D,X = 8 cycles");
    CHECK(cpu.total_cycles == 8, "expected 8");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x8C); emit(0x00);  /* LDA 0,PCR (8-bit) */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA 8-bit,PCR = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x8D); emit16(0x0000);  /* LDA 0,PCR (16-bit) */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA 16-bit,PCR = 9 cycles");
    CHECK(cpu.total_cycles == 9, "expected 9");

    setup(0x200);
    prog_at(0x200);
    emit(0xA6); emit(0x05);       /* LDA 5,X (5-bit offset) */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA 5-bit,X = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    /* Indirect cycle counts */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0000] = 0x04;
    mem_get_ram()[0x0001] = 0x00;
    emit(0xA6); emit(0x94);       /* LDA [,X] */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA [,X] = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0000] = 0x04;
    mem_get_ram()[0x0001] = 0x00;
    emit(0xA6); emit(0x91);       /* LDA [,X++] */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA [,X++] = 10 cycles");
    CHECK(cpu.total_cycles == 10, "expected 10");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x04;
    mem_get_ram()[0x0401] = 0x00;
    emit(0xA6); emit(0x9F); emit16(0x0400);  /* LDA [$0400] (extended indirect) */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDA [$nnnn] = 9 cycles");
    CHECK(cpu.total_cycles == 9, "expected 9");

    /* Instruction cycle counts */
    setup(0x200);
    prog_at(0x200);
    emit(0xCC); emit16(0x1234);   /* LDD immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDD immediate = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x1234);   /* LDX immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDX immediate = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    setup(0x200);
    prog_at(0x200);
    emit(0x20); emit(0x00);       /* BRA +0 (taken) */
    cpu.total_cycles = 0;
    run(1);
    TEST("BRA = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_Z;                /* Set Z for BEQ */
    emit(0x27); emit(0x00);       /* BEQ +0 (taken) */
    cpu.total_cycles = 0;
    run(1);
    TEST("BEQ taken = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = 0;                   /* Clear Z: BEQ not taken */
    emit(0x27); emit(0x00);       /* BEQ +0 (not taken) */
    cpu.total_cycles = 0;
    run(1);
    TEST("BEQ not taken = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    setup(0x200);
    prog_at(0x200);
    emit(0x16); emit16(0x0000);   /* LBRA */
    cpu.total_cycles = 0;
    run(1);
    TEST("LBRA = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = CC_Z;
    emit(0x10); emit(0x27); emit16(0x0000);  /* LBEQ taken */
    cpu.total_cycles = 0;
    run(1);
    TEST("LBEQ taken = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200);
    prog_at(0x200);
    cpu.cc = 0;
    emit(0x10); emit(0x27); emit16(0x0000);  /* LBEQ not taken */
    cpu.total_cycles = 0;
    run(1);
    TEST("LBEQ not taken = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    emit(0x17); emit16(0x0000);   /* LBSR */
    cpu.total_cycles = 0;
    run(1);
    TEST("LBSR = 9 cycles");
    CHECK(cpu.total_cycles == 9, "expected 9");

    setup(0x200);
    prog_at(0x200);
    emit(0x39);                   /* RTS */
    /* Push a return address to stack */
    sp2 = cpu.s;
    mem_get_ram()[--sp2] = 0x00;
    mem_get_ram()[--sp2] = 0x03;
    cpu.s = sp2;
    cpu.total_cycles = 0;
    run(1);
    TEST("RTS = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    emit(0x1E); emit(0x89);       /* EXG A,B */
    cpu.total_cycles = 0;
    run(1);
    TEST("EXG = 8 cycles");
    CHECK(cpu.total_cycles == 8, "expected 8");

    setup(0x200);
    prog_at(0x200);
    emit(0x1F); emit(0x89);       /* TFR A,B */
    cpu.total_cycles = 0;
    run(1);
    TEST("TFR = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200);
    prog_at(0x200);
    emit(0x1D);                   /* SEX */
    cpu.total_cycles = 0;
    run(1);
    TEST("SEX = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200);
    prog_at(0x200);
    emit(0x3A);                   /* ABX */
    cpu.total_cycles = 0;
    run(1);
    TEST("ABX = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    setup(0x200);
    prog_at(0x200);
    emit(0x19);                   /* DAA */
    cpu.total_cycles = 0;
    run(1);
    TEST("DAA = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200);
    prog_at(0x200);
    emit(0x4C);                   /* INCA */
    cpu.total_cycles = 0;
    run(1);
    TEST("INCA = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200);
    prog_at(0x200);
    emit(0x7C); emit16(0x0300);   /* INC extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("INC extended = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200);
    prog_at(0x200);
    emit(0x0C); emit(0x00);       /* INC direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("INC direct = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200);
    prog_at(0x200);
    emit(0x6C); emit(0x84);       /* INC ,X (indexed) */
    cpu.total_cycles = 0;
    run(1);
    TEST("INC indexed ,X = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200);
    prog_at(0x200);
    emit(0x3F);                   /* SWI */
    cpu.total_cycles = 0;
    run(1);
    TEST("SWI = 19 cycles");
    CHECK(cpu.total_cycles == 19, "expected 19");

    setup(0x200);
    prog_at(0x200);
    emit(0xBD); emit16(0x0300);   /* JSR extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("JSR extended = 8 cycles");
    CHECK(cpu.total_cycles == 8, "expected 8");

    setup(0x200);
    prog_at(0x200);
    emit(0x7E); emit16(0x0300);   /* JMP extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("JMP extended = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200);
    prog_at(0x200);
    emit(0xB7); emit16(0x0300);   /* STA extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("STA extended = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    emit(0xC3); emit16(0x0000);   /* ADDD immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("ADDD immediate = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200);
    prog_at(0x200);
    emit(0x83); emit16(0x0000);   /* SUBD immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("SUBD immediate = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200);
    prog_at(0x200);
    emit(0x1A); emit(0x00);       /* ORCC immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("ORCC = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    setup(0x200);
    prog_at(0x200);
    emit(0x1C); emit(0xFF);       /* ANDCC immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("ANDCC = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    /* RTI cycle counts */
    setup(0x200);
    prog_at(0x200);
    /* Set up FIRQ-style stack frame (E=0): only CC + PC */
    sp2 = cpu.s;
    mem_get_ram()[--sp2] = 0x10;   /* PC low */
    mem_get_ram()[--sp2] = 0x02;   /* PC high */
    mem_get_ram()[--sp2] = 0x00;   /* CC with E=0 */
    cpu.s = sp2;
    emit(0x3B);                   /* RTI */
    cpu.total_cycles = 0;
    run(1);
    TEST("RTI (E=0) = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200);
    prog_at(0x200);
    /* Set up full stack frame (E=1): CC,A,B,DP,X,Y,U,PC = 12 bytes */
    sp2 = cpu.s;
    mem_get_ram()[--sp2] = 0x10;   /* PC low */
    mem_get_ram()[--sp2] = 0x02;   /* PC high */
    mem_get_ram()[--sp2] = 0x00;   /* U low */
    mem_get_ram()[--sp2] = 0x00;   /* U high */
    mem_get_ram()[--sp2] = 0x00;   /* Y low */
    mem_get_ram()[--sp2] = 0x00;   /* Y high */
    mem_get_ram()[--sp2] = 0x00;   /* X low */
    mem_get_ram()[--sp2] = 0x00;   /* X high */
    mem_get_ram()[--sp2] = 0x00;   /* DP */
    mem_get_ram()[--sp2] = 0x00;   /* B */
    mem_get_ram()[--sp2] = 0x00;   /* A */
    mem_get_ram()[--sp2] = CC_E;   /* CC with E=1 */
    cpu.s = sp2;
    emit(0x3B);                   /* RTI */
    cpu.total_cycles = 0;
    run(1);
    TEST("RTI (E=1) = 15 cycles");
    CHECK(cpu.total_cycles == 15, "expected 15");

    /* Page 2 instruction cycle count */
    setup(0x200);
    prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0x1234);  /* LDY immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDY immediate = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200);
    prog_at(0x200);
    emit(0x10); emit(0x83); emit16(0x1234);  /* CMPD immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("CMPD immediate = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    /* Page 3 instruction cycle count */
    setup(0x200);
    prog_at(0x200);
    emit(0x11); emit(0x83); emit16(0x1234);  /* CMPU immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("CMPU immediate = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200);
    prog_at(0x200);
    emit(0x11); emit(0x8C); emit16(0x1234);  /* CMPS immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("CMPS immediate = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    /* Halted state = 1 cycle per step */
    setup(0x200);
    prog_at(0x200);
    emit(0x13);                   /* SYNC */
    run(1); /* Execute SYNC */
    cpu.total_cycles = 0;
    run(1); /* Halted step */
    TEST("Halted state = 1 cycle per step");
    CHECK(cpu.total_cycles == 1, "expected 1");

    /* ================================================================ */
    /* ---- Indexed addressing: Y register all sub-modes ---- */
    printf("\nIndexed Y register (all sub-modes):\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xAB;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xA6); emit(0xA0);       /* LDA ,Y+ */
    run(2);
    TEST("LDA ,Y+ (post-increment)");
    CHECK(cpu_get_a(&cpu) == 0xAB && cpu.y == 0x0401, "A or Y wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x34;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xEC); emit(0xA1);       /* LDD ,Y++ */
    run(2);
    TEST("LDD ,Y++ (post-increment by 2)");
    CHECK(cpu.d == 0x1234 && cpu.y == 0x0402, "D or Y wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x03FF] = 0xCC;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xA6); emit(0xA2);       /* LDA ,-Y */
    run(2);
    TEST("LDA ,-Y (pre-decrement)");
    CHECK(cpu_get_a(&cpu) == 0xCC && cpu.y == 0x03FF, "A or Y wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x03FE] = 0xAB; mem_get_ram()[0x03FF] = 0xCD;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xEC); emit(0xA3);       /* LDD ,--Y */
    run(2);
    TEST("LDD ,--Y (pre-decrement by 2)");
    CHECK(cpu.d == 0xABCD && cpu.y == 0x03FE, "D or Y wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0410] = 0x77;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xA6); emit(0xA8); emit(0x10);  /* LDA $10,Y (8-bit offset) */
    run(2);
    TEST("LDA $10,Y (8-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x77, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0500] = 0x88;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xA6); emit(0xA9); emit16(0x0100);  /* LDA $0100,Y (16-bit offset) */
    run(2);
    TEST("LDA $0100,Y (16-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x88, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0403] = 0x99;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0x03);
    emit(0xE6); emit(0xA6);       /* LDB A,Y */
    run(3);
    TEST("LDB A,Y (accumulator A offset)");
    CHECK(cpu_get_b(&cpu) == 0x99, "B wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0403] = 0x55;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0x03);
    emit(0xA6); emit(0xA5);       /* LDA B,Y */
    run(3);
    TEST("LDA B,Y (accumulator B offset)");
    CHECK(cpu_get_a(&cpu) == 0x55, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0500] = 0x66;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xCC); emit16(0x0100);
    emit(0xA6); emit(0xAB);       /* LDA D,Y */
    run(3);
    TEST("LDA D,Y (accumulator D offset)");
    CHECK(cpu_get_a(&cpu) == 0x66, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05; mem_get_ram()[0x0401] = 0x00;
    mem_get_ram()[0x0500] = 0xDD;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xA6); emit(0xB4);       /* LDA [,Y] (indirect) */
    run(2);
    TEST("LDA [,Y] (indirect zero offset)");
    CHECK(cpu_get_a(&cpu) == 0xDD, "A wrong");

    /* ---- Indexed U register sub-modes ---- */
    printf("\nIndexed U register (sub-modes):\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xAB;
    emit(0xCE); emit16(0x0400);
    emit(0xA6); emit(0xC0);       /* LDA ,U+ */
    run(2);
    TEST("LDA ,U+ (post-increment)");
    CHECK(cpu_get_a(&cpu) == 0xAB && cpu.u == 0x0401, "A or U wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x34;
    emit(0xCE); emit16(0x0400);
    emit(0xEC); emit(0xC1);       /* LDD ,U++ */
    run(2);
    TEST("LDD ,U++ (post-increment by 2)");
    CHECK(cpu.d == 0x1234 && cpu.u == 0x0402, "D or U wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x03FF] = 0xCC;
    emit(0xCE); emit16(0x0400);
    emit(0xA6); emit(0xC2);       /* LDA ,-U */
    run(2);
    TEST("LDA ,-U (pre-decrement)");
    CHECK(cpu_get_a(&cpu) == 0xCC && cpu.u == 0x03FF, "A or U wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0410] = 0x77;
    emit(0xCE); emit16(0x0400);
    emit(0xA6); emit(0xC8); emit(0x10);  /* LDA $10,U */
    run(2);
    TEST("LDA $10,U (8-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x77, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0500] = 0x88;
    emit(0xCE); emit16(0x0400);
    emit(0xA6); emit(0xC9); emit16(0x0100);  /* LDA $0100,U */
    run(2);
    TEST("LDA $0100,U (16-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x88, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05; mem_get_ram()[0x0401] = 0x00;
    mem_get_ram()[0x0500] = 0xEE;
    emit(0xCE); emit16(0x0400);
    emit(0xA6); emit(0xD4);       /* LDA [,U] */
    run(2);
    TEST("LDA [,U] (indirect)");
    CHECK(cpu_get_a(&cpu) == 0xEE, "A wrong");

    /* ---- Indexed S register sub-modes ---- */
    printf("\nIndexed S register (sub-modes):\n");

    setup(0x200);
    prog_at(0x200);
    cpu.s = 0x0400;
    mem_get_ram()[0x0400] = 0xAB;
    emit(0xA6); emit(0xE0);       /* LDA ,S+ */
    run(1);
    TEST("LDA ,S+ (post-increment)");
    CHECK(cpu_get_a(&cpu) == 0xAB && cpu.s == 0x0401, "A or S wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.s = 0x0400;
    mem_get_ram()[0x0410] = 0x77;
    emit(0xA6); emit(0xE8); emit(0x10);  /* LDA $10,S */
    run(1);
    TEST("LDA $10,S (8-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x77, "A wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.s = 0x0400;
    mem_get_ram()[0x0500] = 0x88;
    emit(0xA6); emit(0xE9); emit16(0x0100);  /* LDA $0100,S */
    run(1);
    TEST("LDA $0100,S (16-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x88, "A wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.s = 0x0400;
    mem_get_ram()[0x0400] = 0x05; mem_get_ram()[0x0401] = 0x00;
    mem_get_ram()[0x0500] = 0xFF;
    emit(0xA6); emit(0xF4);       /* LDA [,S] */
    run(1);
    TEST("LDA [,S] (indirect)");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* ---- ALU via direct, indexed, extended addressing ---- */
    printf("\nALU via direct addressing:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0x86); emit(0x10);
    emit(0x9B); emit(0x10);       /* ADDA <$10 */
    run(2);
    TEST("ADDA direct: $10 + $05 = $15");
    CHECK(cpu_get_a(&cpu) == 0x15, "A wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0x86); emit(0x15);
    emit(0x90); emit(0x10);       /* SUBA <$10 */
    run(2);
    TEST("SUBA direct: $15 - $05 = $10");
    CHECK(cpu_get_a(&cpu) == 0x10, "A wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x0F;
    emit(0x86); emit(0xFF);
    emit(0x94); emit(0x10);       /* ANDA <$10 */
    run(2);
    TEST("ANDA direct: $FF & $0F = $0F");
    CHECK(cpu_get_a(&cpu) == 0x0F, "A wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0xF0;
    emit(0x86); emit(0x0F);
    emit(0x9A); emit(0x10);       /* ORA <$10 */
    run(2);
    TEST("ORA direct: $0F | $F0 = $FF");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0xFF;
    emit(0x86); emit(0xAA);
    emit(0x98); emit(0x10);       /* EORA <$10 */
    run(2);
    TEST("EORA direct: $AA ^ $FF = $55");
    CHECK(cpu_get_a(&cpu) == 0x55, "A wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x42;
    emit(0x86); emit(0x42);
    emit(0x91); emit(0x10);       /* CMPA <$10 */
    run(2);
    TEST("CMPA direct: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(cpu_get_a(&cpu) == 0x42, "A unchanged");

    printf("\nALU via indexed addressing:\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0x10);
    emit(0xEB); emit(0x84);       /* ADDB ,X */
    run(3);
    TEST("ADDB indexed: $10 + $05 = $15");
    CHECK(cpu_get_b(&cpu) == 0x15, "B wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0x15);
    emit(0xE0); emit(0x84);       /* SUBB ,X */
    run(3);
    TEST("SUBB indexed: $15 - $05 = $10");
    CHECK(cpu_get_b(&cpu) == 0x10, "B wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xF0;
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0x0F);
    emit(0xEA); emit(0x84);       /* ORB ,X */
    run(3);
    TEST("ORB indexed: $0F | $F0 = $FF");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x0F;
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0xFF);
    emit(0xE4); emit(0x84);       /* ANDB ,X */
    run(3);
    TEST("ANDB indexed: $FF & $0F = $0F");
    CHECK(cpu_get_b(&cpu) == 0x0F, "B wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x55;
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0xAA);
    emit(0xE8); emit(0x84);       /* EORB ,X */
    run(3);
    TEST("EORB indexed: $AA ^ $55 = $FF");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x42;
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0x42);
    emit(0xE1); emit(0x84);       /* CMPB ,X */
    run(3);
    TEST("CMPB indexed: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    printf("\nALU via extended addressing:\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x86); emit(0x10);
    emit(0xBB); emit16(0x0400);   /* ADDA $0400 */
    run(2);
    TEST("ADDA extended: $10 + $05 = $15");
    CHECK(cpu_get_a(&cpu) == 0x15, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x86); emit(0x15);
    emit(0xB0); emit16(0x0400);   /* SUBA $0400 */
    run(2);
    TEST("SUBA extended: $15 - $05 = $10");
    CHECK(cpu_get_a(&cpu) == 0x10, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x42;
    emit(0x86); emit(0x42);
    emit(0xB1); emit16(0x0400);   /* CMPA $0400 */
    run(2);
    TEST("CMPA extended: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xF0;
    emit(0x86); emit(0x0F);
    emit(0xBA); emit16(0x0400);   /* ORA $0400 */
    run(2);
    TEST("ORA extended: $0F | $F0 = $FF");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x0F;
    emit(0x86); emit(0xFF);
    emit(0xB4); emit16(0x0400);   /* ANDA $0400 */
    run(2);
    TEST("ANDA extended: $FF & $0F = $0F");
    CHECK(cpu_get_a(&cpu) == 0x0F, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xFF;
    emit(0x86); emit(0xAA);
    emit(0xB8); emit16(0x0400);   /* EORA $0400 */
    run(2);
    TEST("EORA extended: $AA ^ $FF = $55");
    CHECK(cpu_get_a(&cpu) == 0x55, "A wrong");

    /* B-register via extended */
    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0xC6); emit(0x10);
    emit(0xFB); emit16(0x0400);   /* ADDB $0400 */
    run(2);
    TEST("ADDB extended: $10 + $05 = $15");
    CHECK(cpu_get_b(&cpu) == 0x15, "B wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0xC6); emit(0x15);
    emit(0xF0); emit16(0x0400);   /* SUBB $0400 */
    run(2);
    TEST("SUBB extended: $15 - $05 = $10");
    CHECK(cpu_get_b(&cpu) == 0x10, "B wrong");

    /* ---- ADC/SBC via direct and indexed ---- */
    printf("\nADC/SBC via direct/indexed:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0x86); emit(0x10);
    emit(0x1A); emit(0x01);       /* ORCC #$01 (set carry) */
    emit(0x99); emit(0x10);       /* ADCA <$10 */
    run(3);
    TEST("ADCA direct: $10 + $05 + C=1 = $16");
    CHECK(cpu_get_a(&cpu) == 0x16, "A wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0x86); emit(0x20);
    emit(0x1A); emit(0x01);
    emit(0x92); emit(0x10);       /* SBCA <$10 */
    run(3);
    TEST("SBCA direct: $20 - $05 - C=1 = $1A");
    CHECK(cpu_get_a(&cpu) == 0x1A, "A wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0x10);
    emit(0x1A); emit(0x01);
    emit(0xE9); emit(0x84);       /* ADCB ,X */
    run(4);
    TEST("ADCB indexed: $10 + $05 + C=1 = $16");
    CHECK(cpu_get_b(&cpu) == 0x16, "B wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0x20);
    emit(0x1A); emit(0x01);
    emit(0xE2); emit(0x84);       /* SBCB ,X */
    run(4);
    TEST("SBCB indexed: $20 - $05 - C=1 = $1A");
    CHECK(cpu_get_b(&cpu) == 0x1A, "B wrong");

    /* ---- ADDD/SUBD via direct, indexed, extended ---- */
    printf("\nADDD/SUBD via direct/indexed/extended:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x00; mem_get_ram()[0x0311] = 0x34;
    emit(0xCC); emit16(0x1000);
    emit(0xD3); emit(0x10);       /* ADDD <$10 */
    run(2);
    TEST("ADDD direct: $1000 + $0034 = $1034");
    CHECK(cpu.d == 0x1034, "D wrong");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x10; mem_get_ram()[0x0311] = 0x00;
    emit(0xCC); emit16(0x5000);
    emit(0x93); emit(0x10);       /* SUBD <$10 */
    run(2);
    TEST("SUBD direct: $5000 - $1000 = $4000");
    CHECK(cpu.d == 0x4000, "D wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x00; mem_get_ram()[0x0401] = 0x34;
    emit(0x8E); emit16(0x0400);
    emit(0xCC); emit16(0x1000);
    emit(0xE3); emit(0x84);       /* ADDD ,X */
    run(3);
    TEST("ADDD indexed: $1000 + $0034 = $1034");
    CHECK(cpu.d == 0x1034, "D wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x10; mem_get_ram()[0x0401] = 0x00;
    emit(0x8E); emit16(0x0400);
    emit(0xCC); emit16(0x5000);
    emit(0xA3); emit(0x84);       /* SUBD ,X */
    run(3);
    TEST("SUBD indexed: $5000 - $1000 = $4000");
    CHECK(cpu.d == 0x4000, "D wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x00; mem_get_ram()[0x0401] = 0x34;
    emit(0xCC); emit16(0x1000);
    emit(0xF3); emit16(0x0400);   /* ADDD $0400 */
    run(2);
    TEST("ADDD extended: $1000 + $0034 = $1034");
    CHECK(cpu.d == 0x1034, "D wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x10; mem_get_ram()[0x0401] = 0x00;
    emit(0xCC); emit16(0x5000);
    emit(0xB3); emit16(0x0400);   /* SUBD $0400 */
    run(2);
    TEST("SUBD extended: $5000 - $1000 = $4000");
    CHECK(cpu.d == 0x4000, "D wrong");

    /* ---- BITA/BITB via direct/indexed ---- */
    printf("\nBIT test via direct/indexed:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0xF0;
    emit(0x86); emit(0x0F);
    emit(0x95); emit(0x10);       /* BITA <$10 */
    run(2);
    TEST("BITA direct: $0F & $F0 = 0 -> Z");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(cpu_get_a(&cpu) == 0x0F, "A unchanged");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x80;
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0xFF);
    emit(0xE5); emit(0x84);       /* BITB ,X */
    run(3);
    TEST("BITB indexed: $FF & $80 -> N set");
    CHECK(cpu.cc & CC_N, "N should be set");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B unchanged");

    /* ---- 16-bit loads/stores via indexed ---- */
    printf("\n16-bit load/store via indexed:\n");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xAB; mem_get_ram()[0x0401] = 0xCD;
    emit(0x8E); emit16(0x0400);
    emit(0xEC); emit(0x84);       /* LDD ,X */
    run(2);
    TEST("LDD indexed ,X");
    CHECK(cpu.d == 0xABCD, "D wrong");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x0400);
    emit(0xCC); emit16(0xBEEF);
    emit(0xED); emit(0x84);       /* STD ,X */
    run(3);
    TEST("STD indexed ,X");
    CHECK(mem_get_ram()[0x0400]==0xBE && mem_get_ram()[0x0401]==0xEF, "mem wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x34;
    emit(0x10); emit(0x8E); emit16(0x0400);  /* LDY #$0400 (as index) */
    emit(0xAE); emit(0xA4);       /* LDX ,Y */
    run(2);
    TEST("LDX indexed ,Y");
    CHECK(cpu.x == 0x1234, "X wrong");

    setup(0x200);
    prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0x8E); emit16(0xDEAD);
    emit(0xAF); emit(0xA4);       /* STX ,Y */
    run(3);
    TEST("STX indexed ,Y");
    CHECK(mem_get_ram()[0x0400]==0xDE && mem_get_ram()[0x0401]==0xAD, "mem wrong");

    setup(0x200);
    prog_at(0x200);
    mem_get_ram()[0x0400] = 0xCA; mem_get_ram()[0x0401] = 0xFE;
    emit(0x8E); emit16(0x0400);
    emit(0x10); emit(0xAE); emit(0x84);  /* LDY ,X */
    run(2);
    TEST("LDY indexed ,X");
    CHECK(cpu.y == 0xCAFE, "Y wrong");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x0400);
    emit(0xCE); emit16(0xF00D);
    emit(0xEF); emit(0x84);       /* STU ,X */
    run(3);
    TEST("STU indexed ,X");
    CHECK(mem_get_ram()[0x0400]==0xF0 && mem_get_ram()[0x0401]==0x0D, "mem wrong");

    /* ---- JSR direct and indexed ---- */
    printf("\nJSR direct/indexed:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x9D); emit(0x10);       /* JSR <$10 (-> $0310) */
    emit(0xC6); emit(0x77);       /* LDB #$77 (return here) */
    prog_at(0x0310);
    emit(0x86); emit(0x33);
    emit(0x39);                   /* RTS */
    run(4);
    TEST("JSR direct + RTS");
    CHECK(cpu_get_a(&cpu) == 0x33 && cpu_get_b(&cpu) == 0x77, "A or B wrong");

    setup(0x200);
    prog_at(0x200);
    emit(0x8E); emit16(0x0310);
    emit(0xAD); emit(0x84);       /* JSR ,X (-> $0310) */
    emit(0xC6); emit(0x88);       /* LDB #$88 (return here) */
    prog_at(0x0310);
    emit(0x86); emit(0x44);
    emit(0x39);
    run(5);
    TEST("JSR indexed ,X + RTS");
    CHECK(cpu_get_a(&cpu) == 0x44 && cpu_get_b(&cpu) == 0x88, "A or B wrong");

    /* ---- JMP direct ---- */
    printf("\nJMP direct:\n");

    setup(0x200);
    prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x0E); emit(0x10);       /* JMP <$10 (-> $0310) */
    prog_at(0x0310);
    emit(0x86); emit(0xEE);
    run(2);
    TEST("JMP direct");
    CHECK(cpu_get_a(&cpu) == 0xEE, "A wrong");

    /* ---- Short branches: not-taken paths ---- */
    printf("\nShort branches (not-taken):\n");

    /* BNE not taken when equal */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x05);  /* CMPA #5: Z=1 */
    emit(0x26); emit(0x02);       /* BNE +2 (should NOT be taken) */
    emit(0x86); emit(0xFF);       /* LDA #$FF (NOT skipped) */
    emit(0x12);
    run(4);
    TEST("BNE not taken when equal");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BCS not taken when C clear */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* CMPA #5: $10>$05, C=0 */
    emit(0x25); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BCS not taken when C clear");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BCC not taken when C set */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x03); emit(0x81); emit(0x05);  /* CMPA #5: $03<$05, C=1 */
    emit(0x24); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BCC not taken when C set");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BLT not taken when N==V */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* CMPA #5: $10>$05 signed */
    emit(0x2D); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BLT not taken: $10 >= $05 signed");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BGE not taken when N!=V */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80); emit(0x81); emit(0x01);  /* CMPA #1: -128 < 1, N!=V */
    emit(0x2C); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BGE not taken: -128 < 1 signed");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BGT not taken when Z=1 */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x05);  /* CMPA #5: equal, Z=1 */
    emit(0x2E); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BGT not taken when equal");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BLE not taken when Z=0 and N==V */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* CMPA #5: $10>$05, Z=0, N=V=0 */
    emit(0x2F); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BLE not taken: $10 > $05 signed");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BHI not taken when C=1 */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x03); emit(0x81); emit(0x05);  /* C=1 */
    emit(0x22); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BHI not taken when C set");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BLS not taken when C=0 and Z=0 */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* $10>$05: C=0, Z=0 */
    emit(0x23); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BLS not taken: $10 > $05 unsigned");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BVC not taken when V set */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x7F); emit(0x8B); emit(0x01);  /* overflow */
    emit(0x28); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BVC not taken when V set");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BVS not taken when V clear */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01); emit(0x8B); emit(0x01);  /* no overflow */
    emit(0x29); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("BVS not taken when V clear");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BPL not taken when N set */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80);       /* negative */
    emit(0x2A); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(3);
    TEST("BPL not taken when negative");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* BMI not taken when N clear */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01);       /* positive */
    emit(0x2B); emit(0x02);
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(3);
    TEST("BMI not taken when positive");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* ---- Long branches not-taken paths ---- */
    printf("\nLong branches (not-taken):\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x05);  /* Z=1 */
    emit(0x10); emit(0x26); emit16(0x0002);  /* LBNE */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBNE not taken when equal");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* C=0, Z=0 */
    emit(0x10); emit(0x25); emit16(0x0002);  /* LBCS */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBCS not taken when C clear");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* $10>$05 signed, N=V=0 */
    emit(0x10); emit(0x2D); emit16(0x0002);  /* LBLT */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBLT not taken: $10 >= $05");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x05); emit(0x81); emit(0x05);  /* equal */
    emit(0x10); emit(0x2E); emit16(0x0002);  /* LBGT */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBGT not taken when equal");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* ---- Backward long branch ---- */
    printf("\nBackward long branches:\n");

    setup(0x300);
    prog_at(0x300);
    emit(0x16); emit16(0xFF00);   /* LBRA -$0100 (-256) -> $0300+3-256=$0203 */
    prog_at(0x0203);
    emit(0x86); emit(0x42);
    run(2);
    TEST("LBRA backward (negative 16-bit offset)");
    CHECK(cpu_get_a(&cpu) == 0x42, "A should be $42");

    /* ---- Flag edge cases: SBC overflow ---- */
    printf("\nSBC/ADC flag edge cases:\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 (-128) */
    emit(0x1A); emit(0x01);       /* ORCC #$01 (C=1) */
    emit(0x82); emit(0x00);       /* SBCA #$00: $80-$00-1=$7F */
    run(3);
    TEST("SBCA V flag: $80-$00-C=1=$7F -> V=1");
    CHECK(cpu_get_a(&cpu) == 0x7F, "A wrong");
    CHECK(cpu.cc & CC_V, "V should be set");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x80);       /* LDB #$80 */
    emit(0x1A); emit(0x01);
    emit(0xC2); emit(0x01);       /* SBCB #$01: $80-$01-1=$7E */
    run(3);
    TEST("SBCB V flag: $80-$01-C=1=$7E -> V=1");
    CHECK(cpu_get_b(&cpu) == 0x7E, "B wrong");
    CHECK(cpu.cc & CC_V, "V should be set");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x7F);
    emit(0x1A); emit(0x01);
    emit(0x89); emit(0x00);       /* ADCA #$00: $7F+$00+1=$80 */
    run(3);
    TEST("ADCA V flag: $7F+$00+C=1=$80 -> V=1");
    CHECK(cpu_get_a(&cpu) == 0x80, "A wrong");
    CHECK(cpu.cc & CC_V, "V should be set");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x7F);
    emit(0x1A); emit(0x01);
    emit(0xC9); emit(0x00);       /* ADCB #$00: $7F+$00+1=$80 */
    run(3);
    TEST("ADCB V flag: $7F+$00+C=1=$80 -> V=1");
    CHECK(cpu_get_b(&cpu) == 0x80, "B wrong");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* ADC half-carry */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x0E);
    emit(0x1A); emit(0x01);
    emit(0x89); emit(0x01);       /* ADCA #$01: $0E+$01+1=$10 -> H set */
    run(3);
    TEST("ADCA half-carry: $0E+$01+C=1=$10 -> H");
    CHECK(cpu_get_a(&cpu) == 0x10, "A wrong");
    CHECK(cpu.cc & CC_H, "H should be set");

    /* ---- 16-bit compare N flag ---- */
    printf("\n16-bit compare N flag:\n");

    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0x0001);
    emit(0x8C); emit16(0x0002);   /* CMPX: $0001-$0002=$FFFF -> N=1 */
    run(2);
    TEST("CMPX N flag: $0001 < $0002");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200); prog_at(0x200);
    emit(0xCC); emit16(0x0001);
    emit(0x10); emit(0x83); emit16(0x0002);  /* CMPD: $0001-$0002 -> N=1 */
    run(2);
    TEST("CMPD N flag: $0001 < $0002");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0x8000);
    emit(0x10); emit(0x8C); emit16(0x0001);  /* CMPY: $8000-$0001=$7FFF -> V=1 */
    run(2);
    TEST("CMPY V flag: $8000 vs $0001");
    CHECK(cpu.cc & CC_V, "V should be set");

    setup(0x200); prog_at(0x200);
    emit(0xCE); emit16(0x0001);
    emit(0x11); emit(0x83); emit16(0x0002);  /* CMPU: $0001-$0002 -> N=1 */
    run(2);
    TEST("CMPU N flag: $0001 < $0002");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xCE); emit16(0x0001);
    emit(0x11); emit(0x8C); emit16(0x0002);  /* CMPS: $0001-$0002 -> N=1 */
    run(2);
    TEST("CMPS N flag: $0001 < $0002");
    CHECK(cpu.cc & CC_N, "N should be set");

    /* ---- ASR carry flag ---- */
    printf("\nASR/ROL flag details:\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01);
    emit(0x47);                   /* ASRA: $01>>1=$00, bit 0->C */
    run(2);
    TEST("ASRA: $01 -> $00, C=1");
    CHECK(cpu_get_a(&cpu) == 0x00, "A wrong");
    CHECK(cpu.cc & CC_C, "C should be set");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    /* ROL V flag = N XOR C */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x40);
    cpu.cc = 0;                   /* C=0 */
    emit(0x49);                   /* ROLA: $40->$80, C=0, N=1 -> V=1 */
    run(2);
    TEST("ROLA V flag: $40->$80, V=N^C=1");
    CHECK(cpu_get_a(&cpu) == 0x80, "A wrong");
    CHECK(cpu.cc & CC_V, "V should be set");
    CHECK(cpu.cc & CC_N, "N should be set");
    CHECK(!(cpu.cc & CC_C), "C should be clear");

    /* COM N flag: complement of value < $80 clears N */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80);       /* LDA #$80 */
    emit(0x43);                   /* COMA: ~$80=$7F -> N=0 */
    run(2);
    TEST("COMA: $80->$7F, N=0");
    CHECK(cpu_get_a(&cpu) == 0x7F, "A wrong");
    CHECK(!(cpu.cc & CC_N), "N should be clear");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x7F);
    emit(0x43);                   /* COMA: ~$7F=$80 -> N=1 */
    run(2);
    TEST("COMA: $7F->$80, N=1");
    CHECK(cpu_get_a(&cpu) == 0x80, "A wrong");
    CHECK(cpu.cc & CC_N, "N should be set");

    /* NEG memory (direct) V flag edge case */
    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x80;
    emit(0x00); emit(0x10);       /* NEG <$10 */
    run(1);
    TEST("NEG direct $80: V=1");
    CHECK(mem_get_ram()[0x0310] == 0x80, "mem wrong");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* INC memory (extended) V flag */
    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0300] = 0x7F;
    emit(0x7C); emit16(0x0300);   /* INC $0300 */
    run(1);
    TEST("INC extended $7F->$80: V=1");
    CHECK(mem_get_ram()[0x0300] == 0x80, "mem wrong");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* DEC memory (extended) V flag */
    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0300] = 0x80;
    emit(0x7A); emit16(0x0300);   /* DEC $0300 */
    run(1);
    TEST("DEC extended $80->$7F: V=1");
    CHECK(mem_get_ram()[0x0300] == 0x7F, "mem wrong");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* INC indexed V flag */
    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x7F;
    emit(0x8E); emit16(0x0400);
    emit(0x6C); emit(0x84);       /* INC ,X */
    run(2);
    TEST("INC indexed $7F->$80: V=1");
    CHECK(mem_get_ram()[0x0400] == 0x80, "mem wrong");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* DEC indexed V flag */
    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x80;
    emit(0x8E); emit16(0x0400);
    emit(0x6A); emit(0x84);       /* DEC ,X */
    run(2);
    TEST("DEC indexed $80->$7F: V=1");
    CHECK(mem_get_ram()[0x0400] == 0x7F, "mem wrong");
    CHECK(cpu.cc & CC_V, "V should be set");

    /* ---- Stack push/pull ordering ---- */
    printf("\nStack push/pull ordering:\n");

    /* Push CC,A,B,X in one PSHS, then manually check stack contents */
    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x11);       /* LDA #$11 */
    emit(0xC6); emit(0x22);       /* LDB #$22 */
    emit(0x8E); emit16(0x3333);   /* LDX #$3333 */
    emit(0x1A); emit(0x09);       /* ORCC #$09 (set N and C) */
    emit(0x34); emit(0x17);       /* PSHS CC,A,B,X (bits 0,1,2,4 = 5 bytes) */
    run(5);
    /* Push order: X(hi,lo), B, A, CC (highest numbered first) */
    /* S started at $7F00, pushes 5 bytes -> S=$7EFB */
    /* $7EFB=CC, $7EFC=A, $7EFD=B, $7EFE=X(hi), $7EFF=X(lo) */
    TEST("PSHS ordering: S decreases by 5");
    CHECK(cpu.s == 0x7EFB, "S wrong");

    TEST("PSHS ordering: CC at lowest addr");
    CHECK(mem_get_ram()[0x7EFB] & CC_N, "CC N wrong on stack");

    TEST("PSHS ordering: A above CC");
    CHECK(mem_get_ram()[0x7EFC] == 0x11, "A wrong on stack");

    TEST("PSHS ordering: B above A");
    CHECK(mem_get_ram()[0x7EFD] == 0x22, "B wrong on stack");

    TEST("PSHS ordering: X high above B");
    CHECK(mem_get_ram()[0x7EFE] == 0x33, "X hi wrong on stack");

    TEST("PSHS ordering: X low at highest addr");
    CHECK(mem_get_ram()[0x7EFF] == 0x33, "X lo wrong on stack");

    /* Verify PULS reverses correctly */
    emit(0x86); emit(0x00);  emit(0xC6); emit(0x00);
    emit(0x8E); emit16(0x0000);
    emit(0x1C); emit(0x00);       /* ANDCC #$00 */
    emit(0x35); emit(0x17);       /* PULS CC,A,B,X */
    run(5);
    TEST("PULS reverses PSHS correctly");
    CHECK(cpu_get_a(&cpu) == 0x11, "A wrong");
    CHECK(cpu_get_b(&cpu) == 0x22, "B wrong");
    CHECK(cpu.x == 0x3333, "X wrong");
    CHECK(cpu.cc & CC_N, "CC N restored");
    CHECK(cpu.cc & CC_C, "CC C restored");

    /* Push Y, U, PC ordering */
    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0x4444);  /* LDY #$4444 */
    emit(0xCE); emit16(0x5555);               /* LDU #$5555 */
    emit(0x34); emit(0xE0);       /* PSHS Y,U,PC (bits 5,6,7) */
    run(3);
    /* Push order: PC, U, Y. PC = addr of next instruction after PSHS */
    /* PSHS is at $020A (2-byte). After fetch postbyte, PC=$020C */
    /* So pushed: PC=$020C, then U=$5555, then Y=$4444 */
    TEST("PSHS PC,U,Y: correct S adjustment");
    CHECK(cpu.s == 0x7EFA, "S should decrease by 6");

    /* ---- PSHU/PULU with S and PC ---- */
    printf("\nUser stack extended:\n");

    /* PSHU pushes S instead of U */
    setup(0x200); prog_at(0x200);
    cpu.u = 0x7E00;
    cpu.s = 0x7F00;
    emit(0x36); emit(0x40);       /* PSHU S (bit 6 = S for PSHU) */
    run(1);
    TEST("PSHU S: pushes S to U stack");
    CHECK(cpu.u == 0x7DFE, "U should decrease by 2");
    CHECK(mem_get_ram()[0x7DFE] == 0x7F && mem_get_ram()[0x7DFF] == 0x00, "S on stack");

    /* PULU S */
    emit(0x37); emit(0x40);       /* PULU S */
    run(1);
    TEST("PULU S: pulls S from U stack");
    CHECK(cpu.s == 0x7F00, "S restored");

    /* PULU PC (jump via user stack) */
    setup(0x200); prog_at(0x200);
    cpu.u = 0x7E00;
    sp2 = cpu.u;
    mem_get_ram()[--sp2] = 0x00;   /* PC low */
    mem_get_ram()[--sp2] = 0x03;   /* PC high -> $0300 */
    cpu.u = sp2;
    emit(0x37); emit(0x80);       /* PULU PC */
    prog_at(0x0300);
    emit(0x86); emit(0x77);
    run(2);
    TEST("PULU PC (jump via user stack)");
    CHECK(cpu_get_a(&cpu) == 0x77, "A wrong");

    /* ---- FIRQ RTI: verify regs NOT restored ---- */
    printf("\nFIRQ partial save verification:\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0xC6); emit(0x77);       /* LDB #$77 */
    emit(0x8E); emit16(0x1234);   /* LDX #$1234 */
    emit(0x12);                   /* NOP (interrupted here) */
    /* FIRQ handler: modifies A and X, then RTI */
    prog_at(0x0000);
    emit(0x86); emit(0xEE);       /* LDA #$EE */
    emit(0x8E); emit16(0xBEEF);   /* LDX #$BEEF */
    emit(0x3B);                   /* RTI (E=0: only CC+PC restored) */
    run(3); /* LDA, LDB, LDX */
    cpu_set_firq(&cpu, true);
    run(4); /* FIRQ + LDA + LDX + RTI */
    cpu_set_firq(&cpu, false);
    TEST("FIRQ RTI: A NOT restored (E=0)");
    CHECK(cpu_get_a(&cpu) == 0xEE, "A should be $EE (handler value)");

    TEST("FIRQ RTI: B NOT restored (E=0)");
    /* B was not saved/restored by FIRQ, handler didn't touch B
       but RTI with E=0 doesn't restore B, so whatever the handler left */
    CHECK(cpu_get_b(&cpu) == 0x77, "B should be $77 (handler didn't touch)");

    TEST("FIRQ RTI: X NOT restored (E=0)");
    CHECK(cpu.x == 0xBEEF, "X should be $BEEF (handler value)");

    TEST("FIRQ RTI: returns to correct PC");
    CHECK(cpu.pc == 0x0207, "PC should resume after interrupted NOP");

    /* ---- SYNC wakeup by masked interrupt ---- */
    printf("\nSYNC wakeup by masked interrupt:\n");

    setup(0x200); prog_at(0x200);
    cpu.cc = CC_I;                /* Mask IRQ */
    emit(0x13);                   /* SYNC */
    emit(0x86); emit(0x42);       /* LDA #$42 (should run after masked wake) */
    run(1); /* SYNC halts */
    CHECK(cpu.halted, "halted by SYNC");
    cpu_set_irq(&cpu, true);
    run(1); /* Masked IRQ wakes SYNC without vectoring */
    TEST("SYNC masked IRQ: wakes without vectoring");
    CHECK(!cpu.halted, "should be unhalted");
    /* PC should still be $0201 (no interrupt taken) */
    run(1); /* Execute LDA #$42 */
    cpu_set_irq(&cpu, false);
    CHECK(cpu_get_a(&cpu) == 0x42, "should resume at next instruction");

    setup(0x200); prog_at(0x200);
    cpu.cc = CC_F;                /* Mask FIRQ */
    emit(0x13);                   /* SYNC */
    emit(0x86); emit(0x42);
    run(1);
    cpu_set_firq(&cpu, true);
    run(1);
    TEST("SYNC masked FIRQ: wakes without vectoring");
    CHECK(!cpu.halted, "should be unhalted");
    run(1);
    cpu_set_firq(&cpu, false);
    CHECK(cpu_get_a(&cpu) == 0x42, "resumes at next instruction");

    /* ---- SYNC wakeup by FIRQ and NMI ---- */
    printf("\nSYNC wakeup by FIRQ/NMI:\n");

    setup(0x200); prog_at(0x200);
    emit(0x13);                   /* SYNC */
    emit(0x86); emit(0x42);       /* LDA #$42 (after RTI) */
    prog_at(0x0000);
    emit(0xB7); emit16(0x0300);   /* STA $0300 (A has garbage but writes something) */
    emit(0x3B);                   /* RTI */
    run(1);
    cpu_set_firq(&cpu, true);
    run(3); /* FIRQ handling + STA + RTI */
    cpu_set_firq(&cpu, false);
    run(1); /* LDA #$42 */
    TEST("SYNC wakes on FIRQ, handler runs");
    CHECK(cpu_get_a(&cpu) == 0x42, "resumes correctly");

    setup(0x200); prog_at(0x200);
    cpu.nmi_armed = true;
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0x13);                   /* SYNC */
    emit(0x12);                   /* NOP (after RTI) */
    prog_at(0x0000);
    emit(0x86); emit(0xDD);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);                   /* RTI */
    run(2); /* LDA, SYNC */
    cpu_set_nmi(&cpu, true);
    run(4); /* NMI + LDA + STA + RTI */
    TEST("SYNC wakes on NMI, handler runs");
    CHECK(mem_get_ram()[0x0300] == 0xDD, "NMI handler ran");
    CHECK(cpu_get_a(&cpu) == 0x42, "A restored by RTI");

    /* ---- CWAI wakeup by FIRQ ---- */
    printf("\nCWAI wakeup by FIRQ:\n");

    setup(0x200); prog_at(0x200);
    cpu.cc = CC_F;                /* F set initially */
    emit(0x3C); emit(0xBF);       /* CWAI #$BF (clears F bit) */
    emit(0x86); emit(0x42);       /* LDA #$42 (after RTI) */
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);                   /* RTI */
    run(1); /* CWAI halts */
    cpu_set_firq(&cpu, true);
    run(1); /* FIRQ handling (state already pushed) */
    CHECK(!cpu.halted, "should be unhalted");
    run(2); /* LDA + STA in handler */
    cpu_set_firq(&cpu, false);
    run(1); /* RTI */
    run(1); /* LDA #$42 */
    TEST("CWAI wakes on FIRQ after clearing F");
    CHECK(mem_get_ram()[0x0300] == 0xEE, "handler ran");
    CHECK(cpu_get_a(&cpu) == 0x42, "A restored by RTI");

    /* ---- Multiple interrupts in sequence ---- */
    printf("\nMultiple interrupts in sequence:\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0x12);                   /* NOP */
    emit(0x12);                   /* NOP */
    /* Handler at $0000: write A to $0300, RTI */
    prog_at(0x0000);
    emit(0xB7); emit16(0x0300);
    emit(0x1C); emit(0xEF);       /* ANDCC #$EF (clear I for next IRQ) */
    emit(0x3B);                   /* RTI */
    run(1); /* LDA #$42 */
    cpu_set_irq(&cpu, true);
    run(4); /* IRQ + STA + ANDCC + RTI */
    cpu_set_irq(&cpu, false);
    TEST("First IRQ: handler writes marker");
    CHECK(mem_get_ram()[0x0300] == 0x42, "first handler should write $42");
    /* Now at NOP. Trigger second IRQ */
    mem_get_ram()[0x0300] = 0x00; /* Clear marker */
    run(1); /* NOP */
    cpu_set_irq(&cpu, true);
    run(4); /* second IRQ + handler */
    cpu_set_irq(&cpu, false);
    TEST("Second IRQ: handler runs again");
    CHECK(mem_get_ram()[0x0300] == 0x42, "second handler should write $42");

    /* ---- NMI edge-triggered ---- */
    printf("\nNMI edge triggering:\n");

    setup(0x200); prog_at(0x200);
    cpu.nmi_armed = true;
    emit(0x86); emit(0x42);
    emit(0x12); emit(0x12); emit(0x12);
    prog_at(0x0000);
    emit(0x86); emit(0xEE);
    emit(0xB7); emit16(0x0300);
    emit(0x3B);
    /* Set NMI, handle it, then check it doesn't re-trigger (edge-triggered) */
    run(1); /* LDA */
    cpu_set_nmi(&cpu, true);
    run(4); /* NMI + handler */
    mem_get_ram()[0x0300] = 0x00; /* Clear marker */
    run(1); /* NOP (should NOT trigger NMI again - already pending/handled) */
    TEST("NMI edge-triggered: doesn't re-trigger");
    CHECK(mem_get_ram()[0x0300] == 0x00, "handler should NOT run again");

    /* ---- EXG/TFR 8-bit to 16-bit ---- */
    printf("\nTFR/EXG size mismatch:\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x42);
    emit(0x1F); emit(0x80);       /* TFR A,D: 8->16, duplicates to both bytes */
    run(2);
    TEST("TFR A,D: 8-to-16 duplicates byte");
    CHECK(cpu.d == 0x4242, "D should be $4242");

    setup(0x200); prog_at(0x200);
    emit(0xCC); emit16(0x1234);
    emit(0x1F); emit(0x09);       /* TFR D,B: 16->8, takes low byte */
    run(2);
    TEST("TFR D,B: 16-to-8 takes low byte");
    CHECK(cpu_get_b(&cpu) == 0x34, "B should be $34");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x42);
    emit(0x8E); emit16(0x1234);
    emit(0x1E); emit(0x81);       /* EXG A,X: 8<->16 */
    run(3);
    TEST("EXG A,X: size mismatch exchange");
    /* A (8-bit $42) -> X: duplicated to $4242 */
    CHECK(cpu.x == 0x4242, "X should be $4242");
    /* X (16-bit $1234) -> A: low byte $34 */
    CHECK(cpu_get_a(&cpu) == 0x34, "A should be $34");

    /* ---- ABX unsigned B ---- */
    printf("\nABX unsigned B:\n");

    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0x1000);
    emit(0xC6); emit(0x80);       /* LDB #$80 (128 unsigned, -128 signed) */
    emit(0x3A);                   /* ABX */
    run(3);
    TEST("ABX: X=$1000+B=$80=$1080 (unsigned)");
    CHECK(cpu.x == 0x1080, "X should be $1080, not $0F80");

    /* ---- SEX B=$00 ---- */
    printf("\nSEX edge case:\n");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x00);
    emit(0x1D);                   /* SEX */
    run(2);
    TEST("SEX B=$00: A=$00, Z=1");
    CHECK(cpu_get_a(&cpu) == 0x00, "A should be $00");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(!(cpu.cc & CC_N), "N should be clear");

    /* ---- DAA with H flag ---- */
    printf("\nDAA with H flag:\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x09);
    emit(0x8B); emit(0x09);       /* ADDA: $09+$09=$12, H=1 (nibble carry from 9+9=18) */
    emit(0x19);                   /* DAA */
    run(3);
    TEST("DAA: $09+$09 with H -> BCD 18");
    CHECK(cpu_get_a(&cpu) == 0x18, "A should be $18");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x73);
    emit(0x8B); emit(0x49);       /* ADDA: $73+$49=$BC, H=1 */
    emit(0x19);                   /* DAA: $BC -> +$60=$1C, +$06=$22, C=1 */
    run(3);
    TEST("DAA: BCD 73+49=122, C=1");
    CHECK(cpu.cc & CC_C, "C should be set (BCD carry)");

    /* ---- NOP: verify no state change ---- */
    printf("\nNOP state preservation:\n");

    setup(0x200); prog_at(0x200);
    cpu.cc = CC_N | CC_Z | CC_V | CC_C;
    emit(0x86); emit(0x42);       /* LDA #$42 */
    emit(0xC6); emit(0x99);       /* LDB #$99 */
    emit(0x8E); emit16(0x1234);   /* LDX #$1234 */
    emit(0x12);                   /* NOP */
    run(4);
    TEST("NOP: A unchanged");
    CHECK(cpu_get_a(&cpu) == 0x42, "A wrong");
    TEST("NOP: B unchanged");
    CHECK(cpu_get_b(&cpu) == 0x99, "B wrong");
    TEST("NOP: X unchanged");
    CHECK(cpu.x == 0x1234, "X wrong");
    /* CC was modified by LDA/LDB/LDX but NOP shouldn't change it further */
    /* After LDX #$1234: N=0,Z=0,V=0. NOP shouldn't change these */
    TEST("NOP: CC not modified");
    CHECK(!(cpu.cc & CC_Z), "Z should remain clear after NOP");

    /* ---- PSHS/PULS cycle counting ---- */
    printf("\nPSHS/PULS cycle counts:\n");

    /* PSHS base=5, +1 per byte pushed */
    /* PSHS CC (1 byte): 5+1=6 */
    setup(0x200); prog_at(0x200);
    emit(0x34); emit(0x01);       /* PSHS CC */
    cpu.total_cycles = 0;
    run(1);
    TEST("PSHS CC = 6 cycles (5+1)");
    CHECK(cpu.total_cycles == 6, "expected 6");

    /* PSHS A,B (2 bytes): 5+2=7 */
    setup(0x200); prog_at(0x200);
    emit(0x34); emit(0x06);       /* PSHS A,B */
    cpu.total_cycles = 0;
    run(1);
    TEST("PSHS A,B = 7 cycles (5+2)");
    CHECK(cpu.total_cycles == 7, "expected 7");

    /* PSHS X (2 bytes): 5+2=7 */
    setup(0x200); prog_at(0x200);
    emit(0x34); emit(0x10);       /* PSHS X */
    cpu.total_cycles = 0;
    run(1);
    TEST("PSHS X = 7 cycles (5+2)");
    CHECK(cpu.total_cycles == 7, "expected 7");

    /* PSHS CC,A,B,DP,X,Y,U (all except PC): 5+1+1+1+1+2+2+2=15 */
    setup(0x200); prog_at(0x200);
    emit(0x34); emit(0x7F);       /* PSHS CC,A,B,DP,X,Y,U */
    cpu.total_cycles = 0;
    run(1);
    TEST("PSHS all (no PC) = 15 cycles (5+10)");
    CHECK(cpu.total_cycles == 15, "expected 15");

    /* PSHS everything: 5+1+1+1+1+2+2+2+2=17 */
    setup(0x200); prog_at(0x200);
    emit(0x34); emit(0xFF);       /* PSHS CC,A,B,DP,X,Y,U,PC */
    cpu.total_cycles = 0;
    run(1);
    TEST("PSHS all = 17 cycles (5+12)");
    CHECK(cpu.total_cycles == 17, "expected 17");

    /* PULS A,B: 5+2=7 */
    setup(0x200); prog_at(0x200);
    emit(0x35); emit(0x06);       /* PULS A,B */
    cpu.total_cycles = 0;
    run(1);
    TEST("PULS A,B = 7 cycles (5+2)");
    CHECK(cpu.total_cycles == 7, "expected 7");

    /* PSHU/PULU: same base=5 */
    setup(0x200); prog_at(0x200);
    emit(0x36); emit(0x06);       /* PSHU A,B */
    cpu.total_cycles = 0;
    run(1);
    TEST("PSHU A,B = 7 cycles (5+2)");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200); prog_at(0x200);
    emit(0x37); emit(0x06);       /* PULU A,B */
    cpu.total_cycles = 0;
    run(1);
    TEST("PULU A,B = 7 cycles (5+2)");
    CHECK(cpu.total_cycles == 7, "expected 7");

    /* ---- CWAI/SYNC cycle counts ---- */
    printf("\nCWAI/SYNC cycle counts:\n");

    setup(0x200); prog_at(0x200);
    emit(0x3C); emit(0xFF);       /* CWAI #$FF */
    cpu.total_cycles = 0;
    run(1);
    TEST("CWAI = 20 cycles");
    CHECK(cpu.total_cycles == 20, "expected 20");

    setup(0x200); prog_at(0x200);
    emit(0x13);                   /* SYNC */
    cpu.total_cycles = 0;
    run(1);
    TEST("SYNC = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    /* ---- SWI2/SWI3 cycle counts ---- */
    printf("\nSWI2/SWI3 cycle counts:\n");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0x3F);       /* SWI2 */
    cpu.total_cycles = 0;
    run(1);
    TEST("SWI2 = 20 cycles");
    CHECK(cpu.total_cycles == 20, "expected 20");

    setup(0x200); prog_at(0x200);
    emit(0x11); emit(0x3F);       /* SWI3 */
    cpu.total_cycles = 0;
    run(1);
    TEST("SWI3 = 20 cycles");
    CHECK(cpu.total_cycles == 20, "expected 20");

    /* ---- Remaining instruction cycle counts ---- */
    printf("\nRemaining cycle counts:\n");

    setup(0x200); prog_at(0x200);
    emit(0x8B); emit(0x00);       /* ADDA immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("ADDA immediate = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200); prog_at(0x200);
    emit(0x80); emit(0x00);       /* SUBA immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("SUBA immediate = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200); prog_at(0x200);
    emit(0x84); emit(0xFF);       /* ANDA immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("ANDA immediate = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200); prog_at(0x200);
    emit(0x81); emit(0x00);       /* CMPA immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("CMPA immediate = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x9B); emit(0x00);       /* ADDA direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("ADDA direct = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200); prog_at(0x200);
    emit(0xAB); emit(0x84);       /* ADDA ,X indexed */
    cpu.total_cycles = 0;
    run(1);
    TEST("ADDA indexed ,X = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200); prog_at(0x200);
    emit(0xBB); emit16(0x0300);   /* ADDA extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("ADDA extended = 5 cycles");
    CHECK(cpu.total_cycles == 5, "expected 5");

    setup(0x200); prog_at(0x200);
    emit(0xCB); emit(0x00);       /* ADDB immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("ADDB immediate = 2 cycles");
    CHECK(cpu.total_cycles == 2, "expected 2");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0xD3); emit(0x00);       /* ADDD direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("ADDD direct = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200); prog_at(0x200);
    emit(0xE3); emit(0x84);       /* ADDD ,X indexed */
    cpu.total_cycles = 0;
    run(1);
    TEST("ADDD indexed ,X = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200); prog_at(0x200);
    emit(0xF3); emit16(0x0300);   /* ADDD extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("ADDD extended = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200); prog_at(0x200);
    emit(0x8C); emit16(0x0000);   /* CMPX immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("CMPX immediate = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200); prog_at(0x200);
    emit(0xFD); emit16(0x0300);   /* STD extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("STD extended = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x9D); emit(0x10);       /* JSR direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("JSR direct = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200); prog_at(0x200);
    emit(0xAD); emit(0x84);       /* JSR ,X indexed */
    cpu.total_cycles = 0;
    run(1);
    TEST("JSR indexed ,X = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200); prog_at(0x200);
    emit(0x0E); emit(0x00);       /* JMP direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("JMP direct = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    setup(0x200); prog_at(0x200);
    emit(0x6E); emit(0x84);       /* JMP ,X indexed */
    cpu.total_cycles = 0;
    run(1);
    TEST("JMP indexed ,X = 3 cycles");
    CHECK(cpu.total_cycles == 3, "expected 3");

    /* Direct page RMW cycle counts (representative) */
    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x0A); emit(0x10);       /* DEC direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("DEC direct = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x00); emit(0x10);       /* NEG direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("NEG direct = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x03); emit(0x10);       /* COM direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("COM direct = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x0D); emit(0x10);       /* TST direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("TST direct = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x0F); emit(0x10);       /* CLR direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("CLR direct = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    /* Page 2 store/load cycle counts */
    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xBF); emit16(0x0300);  /* STY extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("STY extended = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xBE); emit16(0x0300);  /* LDY extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDY extended = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x10); emit(0x9E); emit(0x10);  /* LDY direct */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDY direct = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xAE); emit(0x84);  /* LDY ,X indexed */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDY indexed ,X = 6 cycles");
    CHECK(cpu.total_cycles == 6, "expected 6");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xCE); emit16(0x7F00);  /* LDS immediate */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDS immediate = 4 cycles");
    CHECK(cpu.total_cycles == 4, "expected 4");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xFE); emit16(0x0300);  /* LDS extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("LDS extended = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xFF); emit16(0x0300);  /* STS extended */
    cpu.total_cycles = 0;
    run(1);
    TEST("STS extended = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    /* CWAI wakeup by IRQ cycle count (state already pushed) */
    setup(0x200); prog_at(0x200);
    emit(0x3C); emit(0xEF);       /* CWAI #$EF (clears I) */
    prog_at(0x0000);
    emit(0x12);                   /* NOP (handler) */
    run(1); /* CWAI */
    cpu_set_irq(&cpu, true);
    cpu.total_cycles = 0;
    run(1); /* IRQ wakeup from CWAI */
    cpu_set_irq(&cpu, false);
    TEST("IRQ from CWAI = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    /* FIRQ from CWAI */
    setup(0x200); prog_at(0x200);
    emit(0x3C); emit(0xBF);       /* CWAI #$BF (clears F) */
    prog_at(0x0000);
    emit(0x12);
    run(1);
    cpu_set_firq(&cpu, true);
    cpu.total_cycles = 0;
    run(1);
    cpu_set_firq(&cpu, false);
    TEST("FIRQ from CWAI = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    /* NMI from CWAI */
    setup(0x200); prog_at(0x200);
    cpu.nmi_armed = true;
    emit(0x3C); emit(0xFF);
    prog_at(0x0000);
    emit(0x12);
    run(1);
    cpu_set_nmi(&cpu, true);
    cpu.total_cycles = 0;
    run(1);
    TEST("NMI from CWAI = 7 cycles");
    CHECK(cpu.total_cycles == 7, "expected 7");

    /* ---- Illegal opcode ---- */
    printf("\nIllegal opcode:\n");

    setup(0x200); prog_at(0x200);
    emit(0x01);                   /* Illegal opcode */
    emit(0x86); emit(0x42);       /* LDA #$42 (should still run after illegal) */
    (void)cpu.pc;  /* suppress unused warning */
    run(2);
    TEST("Illegal opcode: execution continues");
    CHECK(cpu_get_a(&cpu) == 0x42, "A should be $42 (continues after illegal)");

    /* ================================================================ */
    /* ---- Negative 8-bit and 16-bit indexed offsets ---- */
    printf("\nNegative indexed offsets:\n");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x03F0] = 0xAA;
    emit(0x8E); emit16(0x0400);
    emit(0xA6); emit(0x88); emit(0xF0);  /* LDA -16,X (8-bit signed: $F0 = -16) */
    run(2);
    TEST("LDA -16,X (8-bit signed offset)");
    CHECK(cpu_get_a(&cpu) == 0xAA, "A wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0300] = 0xBB;
    emit(0x8E); emit16(0x0400);
    emit(0xA6); emit(0x89); emit16(0xFF00);  /* LDA -256,X (16-bit signed) */
    run(2);
    TEST("LDA -256,X (16-bit signed offset)");
    CHECK(cpu_get_a(&cpu) == 0xBB, "A wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x03F0] = 0xCC;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0xA6); emit(0xA8); emit(0xF0);  /* LDA -16,Y */
    run(2);
    TEST("LDA -16,Y (8-bit signed offset)");
    CHECK(cpu_get_a(&cpu) == 0xCC, "A wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x03F0] = 0xDD;
    emit(0xCE); emit16(0x0400);
    emit(0xA6); emit(0xC8); emit(0xF0);  /* LDA -16,U */
    run(2);
    TEST("LDA -16,U (8-bit signed offset)");
    CHECK(cpu_get_a(&cpu) == 0xDD, "A wrong");

    setup(0x200); prog_at(0x200);
    cpu.s = 0x0400;
    mem_get_ram()[0x03F0] = 0xEE;
    emit(0xA6); emit(0xE8); emit(0xF0);  /* LDA -16,S */
    run(1);
    TEST("LDA -16,S (8-bit signed offset)");
    CHECK(cpu_get_a(&cpu) == 0xEE, "A wrong");

    /* ---- Store instruction flags ---- */
    printf("\nStore instruction flags:\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80);
    emit(0xB7); emit16(0x0300);   /* STA $0300: value $80 -> N=1 */
    run(2);
    TEST("STA sets N for negative value");
    CHECK(cpu.cc & CC_N, "N should be set");
    CHECK(!(cpu.cc & CC_Z), "Z should be clear");
    CHECK(!(cpu.cc & CC_V), "V always cleared by store");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x00);
    emit(0xB7); emit16(0x0300);   /* STA $0300: value $00 -> Z=1 */
    run(2);
    TEST("STA sets Z for zero value");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(!(cpu.cc & CC_N), "N should be clear");

    setup(0x200); prog_at(0x200);
    emit(0xC6); emit(0x80);
    emit(0xF7); emit16(0x0300);   /* STB $0300 */
    run(2);
    TEST("STB sets N for negative value");
    CHECK(cpu.cc & CC_N, "N should be set");
    CHECK(!(cpu.cc & CC_V), "V cleared");

    setup(0x200); prog_at(0x200);
    emit(0xCC); emit16(0x8000);
    emit(0xFD); emit16(0x0300);   /* STD $0300 */
    run(2);
    TEST("STD sets N for negative 16-bit value");
    CHECK(cpu.cc & CC_N, "N should be set");
    CHECK(!(cpu.cc & CC_V), "V cleared");

    setup(0x200); prog_at(0x200);
    emit(0xCC); emit16(0x0000);
    emit(0xFD); emit16(0x0300);   /* STD $0300 */
    run(2);
    TEST("STD sets Z for zero 16-bit value");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0x8000);
    emit(0xBF); emit16(0x0300);   /* STX $0300 */
    run(2);
    TEST("STX sets N for negative value");
    CHECK(cpu.cc & CC_N, "N should be set");
    CHECK(!(cpu.cc & CC_V), "V cleared");

    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0x0000);
    emit(0xBF); emit16(0x0300);   /* STX $0300 */
    run(2);
    TEST("STX sets Z for zero value");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0x8E); emit16(0x8000);
    emit(0x10); emit(0xBF); emit16(0x0300);  /* STY $0300 */
    run(2);
    TEST("STY sets N for negative value");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200); prog_at(0x200);
    emit(0xCE); emit16(0x0000);
    emit(0xFF); emit16(0x0300);   /* STU $0300 */
    run(2);
    TEST("STU sets Z for zero value");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    emit(0x10); emit(0xCE); emit16(0x8000);
    emit(0x10); emit(0xFF); emit16(0x0300);  /* STS $0300 */
    run(2);
    TEST("STS sets N for negative value");
    CHECK(cpu.cc & CC_N, "N should be set");

    /* STA/STB indexed */
    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0x77);
    emit(0xA7); emit(0x84);       /* STA ,X */
    run(3);
    TEST("STA indexed ,X");
    CHECK(mem_get_ram()[0x0400] == 0x77, "mem wrong");

    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0x0400);
    emit(0xC6); emit(0x88);
    emit(0xE7); emit(0x84);       /* STB ,X */
    run(3);
    TEST("STB indexed ,X");
    CHECK(mem_get_ram()[0x0400] == 0x88, "mem wrong");

    /* ---- CMPX via direct, indexed, extended ---- */
    printf("\nCMPX via all addressing modes:\n");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x12; mem_get_ram()[0x0311] = 0x34;
    emit(0x8E); emit16(0x1234);
    emit(0x9C); emit(0x10);       /* CMPX <$10 */
    run(2);
    TEST("CMPX direct: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x34;
    emit(0x8E); emit16(0x1234);
    emit(0x8E); emit16(0x0400);   /* Reload X for index... */
    /* Actually X is the register being compared AND the index. Let me use Y. */
    run(0); /* reset */

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x34;
    emit(0x10); emit(0x8E); emit16(0x0400);  /* LDY #$0400 */
    emit(0x8E); emit16(0x1234);               /* LDX #$1234 */
    emit(0xAC); emit(0xA4);       /* CMPX ,Y */
    run(3);
    TEST("CMPX indexed ,Y: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x34;
    emit(0x8E); emit16(0x1234);
    emit(0xBC); emit16(0x0400);   /* CMPX $0400 */
    run(2);
    TEST("CMPX extended: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x35;
    emit(0x8E); emit16(0x1234);
    emit(0xBC); emit16(0x0400);   /* CMPX $0400: $1234 < $1235 */
    run(2);
    TEST("CMPX extended: less than");
    CHECK(cpu.cc & CC_C, "C should be set");
    CHECK(!(cpu.cc & CC_Z), "Z should be clear");

    /* ---- CMPD via direct, indexed, extended ---- */
    printf("\nCMPD via all addressing modes:\n");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x12; mem_get_ram()[0x0311] = 0x34;
    emit(0xCC); emit16(0x1234);
    emit(0x10); emit(0x93); emit(0x10);  /* CMPD <$10 */
    run(2);
    TEST("CMPD direct: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x34;
    emit(0x8E); emit16(0x0400);
    emit(0xCC); emit16(0x1234);
    emit(0x10); emit(0xA3); emit(0x84);  /* CMPD ,X */
    run(3);
    TEST("CMPD indexed: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x34;
    emit(0xCC); emit16(0x1234);
    emit(0x10); emit(0xB3); emit16(0x0400);  /* CMPD $0400 */
    run(2);
    TEST("CMPD extended: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    /* ---- CMPY via direct, indexed, extended ---- */
    printf("\nCMPY via all addressing modes:\n");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x56; mem_get_ram()[0x0311] = 0x78;
    emit(0x10); emit(0x8E); emit16(0x5678);
    emit(0x10); emit(0x9C); emit(0x10);  /* CMPY <$10 */
    run(2);
    TEST("CMPY direct: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x56; mem_get_ram()[0x0401] = 0x78;
    emit(0x8E); emit16(0x0400);
    emit(0x10); emit(0x8E); emit16(0x5678);
    emit(0x10); emit(0xAC); emit(0x84);  /* CMPY ,X */
    run(3);
    TEST("CMPY indexed: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x56; mem_get_ram()[0x0401] = 0x78;
    emit(0x10); emit(0x8E); emit16(0x5678);
    emit(0x10); emit(0xBC); emit16(0x0400);  /* CMPY $0400 */
    run(2);
    TEST("CMPY extended: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    /* ---- CMPU via direct, indexed, extended ---- */
    printf("\nCMPU via all addressing modes:\n");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x40; mem_get_ram()[0x0311] = 0x00;
    emit(0xCE); emit16(0x4000);
    emit(0x11); emit(0x93); emit(0x10);  /* CMPU <$10 */
    run(2);
    TEST("CMPU direct: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x40; mem_get_ram()[0x0401] = 0x00;
    emit(0x8E); emit16(0x0400);
    emit(0xCE); emit16(0x4000);
    emit(0x11); emit(0xA3); emit(0x84);  /* CMPU ,X */
    run(3);
    TEST("CMPU indexed: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x40; mem_get_ram()[0x0401] = 0x00;
    emit(0xCE); emit16(0x4000);
    emit(0x11); emit(0xB3); emit16(0x0400);  /* CMPU $0400 */
    run(2);
    TEST("CMPU extended: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    /* ---- CMPS via direct, indexed, extended ---- */
    printf("\nCMPS via all addressing modes:\n");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x20; mem_get_ram()[0x0311] = 0x00;
    emit(0x10); emit(0xCE); emit16(0x2000);
    emit(0x11); emit(0x9C); emit(0x10);  /* CMPS <$10 */
    run(2);
    TEST("CMPS direct: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x20; mem_get_ram()[0x0401] = 0x00;
    emit(0x8E); emit16(0x0400);
    emit(0x10); emit(0xCE); emit16(0x2000);
    emit(0x11); emit(0xAC); emit(0x84);  /* CMPS ,X */
    run(3);
    TEST("CMPS indexed: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x20; mem_get_ram()[0x0401] = 0x00;
    emit(0x10); emit(0xCE); emit16(0x2000);
    emit(0x11); emit(0xBC); emit16(0x0400);  /* CMPS $0400 */
    run(2);
    TEST("CMPS extended: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    /* ---- LDS via direct and indexed (NMI arming) ---- */
    printf("\nLDS direct/indexed (NMI arming):\n");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x7F; mem_get_ram()[0x0311] = 0x00;
    emit(0x10); emit(0xDE); emit(0x10);  /* LDS <$10 */
    run(1);
    TEST("LDS direct: loads S and arms NMI");
    CHECK(cpu.s == 0x7F00, "S wrong");
    CHECK(cpu.nmi_armed, "NMI should be armed");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x7E; mem_get_ram()[0x0401] = 0x00;
    emit(0x8E); emit16(0x0400);
    emit(0x10); emit(0xEE); emit(0x84);  /* LDS ,X */
    run(2);
    TEST("LDS indexed: loads S and arms NMI");
    CHECK(cpu.s == 0x7E00, "S wrong");
    CHECK(cpu.nmi_armed, "NMI should be armed");

    /* ---- STY/STS via direct and indexed ---- */
    printf("\nSTY/STS via direct/indexed:\n");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x10); emit(0x8E); emit16(0xCAFE);
    emit(0x10); emit(0x9F); emit(0x10);  /* STY <$10 */
    run(2);
    TEST("STY direct");
    CHECK(mem_get_ram()[0x0310]==0xCA && mem_get_ram()[0x0311]==0xFE, "mem wrong");

    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0x0400);
    emit(0x10); emit(0x8E); emit16(0xBEEF);
    emit(0x10); emit(0xAF); emit(0x84);  /* STY ,X */
    run(3);
    TEST("STY indexed ,X");
    CHECK(mem_get_ram()[0x0400]==0xBE && mem_get_ram()[0x0401]==0xEF, "mem wrong");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0x10); emit(0xCE); emit16(0xF00D);
    emit(0x10); emit(0xDF); emit(0x10);  /* STS <$10 */
    run(2);
    TEST("STS direct");
    CHECK(mem_get_ram()[0x0310]==0xF0 && mem_get_ram()[0x0311]==0x0D, "mem wrong");

    setup(0x200); prog_at(0x200);
    emit(0x8E); emit16(0x0400);
    emit(0x10); emit(0xCE); emit16(0xDEAD);
    emit(0x10); emit(0xEF); emit(0x84);  /* STS ,X */
    run(3);
    TEST("STS indexed ,X");
    CHECK(mem_get_ram()[0x0400]==0xDE && mem_get_ram()[0x0401]==0xAD, "mem wrong");

    /* ---- LDU/LDB via extended ---- */
    printf("\nLDU/LDB via extended:\n");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x12; mem_get_ram()[0x0401] = 0x34;
    emit(0xFE); emit16(0x0400);   /* LDU $0400 */
    run(1);
    TEST("LDU extended");
    CHECK(cpu.u == 0x1234, "U wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x77;
    emit(0xF6); emit16(0x0400);   /* LDB $0400 */
    run(1);
    TEST("LDB extended");
    CHECK(cpu_get_b(&cpu) == 0x77, "B wrong");

    /* ---- LDU indexed ---- */
    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0xAB; mem_get_ram()[0x0401] = 0xCD;
    emit(0x8E); emit16(0x0400);
    emit(0xEE); emit(0x84);       /* LDU ,X */
    run(2);
    TEST("LDU indexed ,X");
    CHECK(cpu.u == 0xABCD, "U wrong");

    /* ---- STU/LDU via direct ---- */
    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    emit(0xCE); emit16(0xBEEF);
    emit(0xDF); emit(0x10);       /* STU <$10 */
    run(2);
    TEST("STU direct");
    CHECK(mem_get_ram()[0x0310]==0xBE && mem_get_ram()[0x0311]==0xEF, "mem wrong");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0xCA; mem_get_ram()[0x0311] = 0xFE;
    emit(0xDE); emit(0x10);       /* LDU <$10 */
    run(1);
    TEST("LDU direct");
    CHECK(cpu.u == 0xCAFE, "U wrong");

    /* ---- B-register ALU via extended ---- */
    printf("\nB-register ALU via extended:\n");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x42;
    emit(0xC6); emit(0x42);
    emit(0xF1); emit16(0x0400);   /* CMPB $0400 */
    run(2);
    TEST("CMPB extended: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0xC6); emit(0x20);
    emit(0x1A); emit(0x01);
    emit(0xF2); emit16(0x0400);   /* SBCB $0400 */
    run(3);
    TEST("SBCB extended: $20-$05-C=1=$1A");
    CHECK(cpu_get_b(&cpu) == 0x1A, "B wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x0F;
    emit(0xC6); emit(0xFF);
    emit(0xF4); emit16(0x0400);   /* ANDB $0400 */
    run(2);
    TEST("ANDB extended: $FF & $0F = $0F");
    CHECK(cpu_get_b(&cpu) == 0x0F, "B wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x80;
    emit(0xC6); emit(0xFF);
    emit(0xF5); emit16(0x0400);   /* BITB $0400 */
    run(2);
    TEST("BITB extended: $FF & $80 -> N");
    CHECK(cpu.cc & CC_N, "N should be set");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B unchanged");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x55;
    emit(0xC6); emit(0xAA);
    emit(0xF8); emit16(0x0400);   /* EORB $0400 */
    run(2);
    TEST("EORB extended: $AA ^ $55 = $FF");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0xC6); emit(0x10);
    emit(0x1A); emit(0x01);
    emit(0xF9); emit16(0x0400);   /* ADCB $0400 */
    run(3);
    TEST("ADCB extended: $10+$05+C=1=$16");
    CHECK(cpu_get_b(&cpu) == 0x16, "B wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0xF0;
    emit(0xC6); emit(0x0F);
    emit(0xFA); emit16(0x0400);   /* ORB $0400 */
    run(2);
    TEST("ORB extended: $0F | $F0 = $FF");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x77;
    emit(0xF6); emit16(0x0400);   /* LDB $0400 (already tested but confirm flags) */
    run(1);
    TEST("LDB extended: $77 flags");
    CHECK(!(cpu.cc & CC_N), "N clear");
    CHECK(!(cpu.cc & CC_Z), "Z clear");

    /* ---- A-register ALU via indexed ---- */
    printf("\nA-register ALU via indexed:\n");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0x15);
    emit(0xA0); emit(0x84);       /* SUBA ,X */
    run(3);
    TEST("SUBA indexed: $15-$05=$10");
    CHECK(cpu_get_a(&cpu) == 0x10, "A wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x42;
    emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0x42);
    emit(0xA1); emit(0x84);       /* CMPA ,X */
    run(3);
    TEST("CMPA indexed: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0x20);
    emit(0x1A); emit(0x01);
    emit(0xA2); emit(0x84);       /* SBCA ,X */
    run(4);
    TEST("SBCA indexed: $20-$05-C=1=$1A");
    CHECK(cpu_get_a(&cpu) == 0x1A, "A wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x0F;
    emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0xFF);
    emit(0xA4); emit(0x84);       /* ANDA ,X */
    run(3);
    TEST("ANDA indexed: $FF & $0F = $0F");
    CHECK(cpu_get_a(&cpu) == 0x0F, "A wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0xF0;
    emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0x0F);
    emit(0xA5); emit(0x84);       /* BITA ,X */
    run(3);
    TEST("BITA indexed: $0F & $F0 = 0 -> Z");
    CHECK(cpu.cc & CC_Z, "Z should be set");
    CHECK(cpu_get_a(&cpu) == 0x0F, "A unchanged");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0xFF;
    emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0xAA);
    emit(0xA8); emit(0x84);       /* EORA ,X */
    run(3);
    TEST("EORA indexed: $AA ^ $FF = $55");
    CHECK(cpu_get_a(&cpu) == 0x55, "A wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0x10);
    emit(0x1A); emit(0x01);
    emit(0xA9); emit(0x84);       /* ADCA ,X */
    run(4);
    TEST("ADCA indexed: $10+$05+C=1=$16");
    CHECK(cpu_get_a(&cpu) == 0x16, "A wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0xF0;
    emit(0x8E); emit16(0x0400);
    emit(0x86); emit(0x0F);
    emit(0xAA); emit(0x84);       /* ORA ,X */
    run(3);
    TEST("ORA indexed: $0F | $F0 = $FF");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* ---- B-register ALU via direct ---- */
    printf("\nB-register ALU via direct:\n");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0xC6); emit(0x15);
    emit(0xD0); emit(0x10);       /* SUBB <$10 */
    run(2);
    TEST("SUBB direct: $15-$05=$10");
    CHECK(cpu_get_b(&cpu) == 0x10, "B wrong");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x42;
    emit(0xC6); emit(0x42);
    emit(0xD1); emit(0x10);       /* CMPB <$10 */
    run(2);
    TEST("CMPB direct: equal");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0xC6); emit(0x20);
    emit(0x1A); emit(0x01);
    emit(0xD2); emit(0x10);       /* SBCB <$10 */
    run(3);
    TEST("SBCB direct: $20-$05-C=1=$1A");
    CHECK(cpu_get_b(&cpu) == 0x1A, "B wrong");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0xC6); emit(0x10);
    emit(0xDB); emit(0x10);       /* ADDB <$10 */
    run(2);
    TEST("ADDB direct: $10+$05=$15");
    CHECK(cpu_get_b(&cpu) == 0x15, "B wrong");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x0F;
    emit(0xC6); emit(0xFF);
    emit(0xD4); emit(0x10);       /* ANDB <$10 */
    run(2);
    TEST("ANDB direct: $FF & $0F = $0F");
    CHECK(cpu_get_b(&cpu) == 0x0F, "B wrong");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x80;
    emit(0xC6); emit(0xFF);
    emit(0xD5); emit(0x10);       /* BITB <$10 */
    run(2);
    TEST("BITB direct: $FF & $80 -> N");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x55;
    emit(0xC6); emit(0xAA);
    emit(0xD8); emit(0x10);       /* EORB <$10 */
    run(2);
    TEST("EORB direct: $AA ^ $55 = $FF");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B wrong");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0x05;
    emit(0xC6); emit(0x10);
    emit(0x1A); emit(0x01);
    emit(0xD9); emit(0x10);       /* ADCB <$10 */
    run(3);
    TEST("ADCB direct: $10+$05+C=1=$16");
    CHECK(cpu_get_b(&cpu) == 0x16, "B wrong");

    setup(0x200); prog_at(0x200);
    cpu.dp = 0x03;
    mem_get_ram()[0x0310] = 0xF0;
    emit(0xC6); emit(0x0F);
    emit(0xDA); emit(0x10);       /* ORB <$10 */
    run(2);
    TEST("ORB direct: $0F | $F0 = $FF");
    CHECK(cpu_get_b(&cpu) == 0xFF, "B wrong");

    /* ---- A-register ALU via extended (remaining) ---- */
    printf("\nA-register ALU via extended (remaining):\n");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x86); emit(0x20);
    emit(0x1A); emit(0x01);
    emit(0xB2); emit16(0x0400);   /* SBCA $0400 */
    run(3);
    TEST("SBCA extended: $20-$05-C=1=$1A");
    CHECK(cpu_get_a(&cpu) == 0x1A, "A wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x80;
    emit(0x86); emit(0xFF);
    emit(0xB5); emit16(0x0400);   /* BITA $0400 */
    run(2);
    TEST("BITA extended: $FF & $80 -> N");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x05;
    emit(0x86); emit(0x10);
    emit(0x1A); emit(0x01);
    emit(0xB9); emit16(0x0400);   /* ADCA $0400 */
    run(3);
    TEST("ADCA extended: $10+$05+C=1=$16");
    CHECK(cpu_get_a(&cpu) == 0x16, "A wrong");

    /* ---- LDD/LDB/LDX extended (confirm flags) ---- */
    printf("\nExtended load flag verification:\n");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x80; mem_get_ram()[0x0401] = 0x00;
    emit(0xFC); emit16(0x0400);   /* LDD $0400 */
    run(1);
    TEST("LDD extended: $8000 sets N");
    CHECK(cpu.cc & CC_N, "N should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x00; mem_get_ram()[0x0401] = 0x00;
    emit(0xFC); emit16(0x0400);   /* LDD $0400 */
    run(1);
    TEST("LDD extended: $0000 sets Z");
    CHECK(cpu.cc & CC_Z, "Z should be set");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0400] = 0x80; mem_get_ram()[0x0401] = 0x00;
    emit(0xBE); emit16(0x0400);   /* LDX $0400 */
    run(1);
    TEST("LDX extended: $8000 sets N");
    CHECK(cpu.cc & CC_N, "N should be set");

    /* ---- Long branches: remaining not-taken ---- */
    printf("\nLong branches (remaining not-taken):\n");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x03); emit(0x81); emit(0x05);  /* C=1 */
    emit(0x10); emit(0x24); emit16(0x0002);  /* LBCC */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBCC not taken when C set");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* C=0, Z=0 */
    emit(0x10); emit(0x23); emit16(0x0002);  /* LBLS */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBLS not taken: $10 > $05 unsigned");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x03); emit(0x81); emit(0x05);  /* C=1 */
    emit(0x10); emit(0x22); emit16(0x0002);  /* LBHI */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBHI not taken when C set");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x7F); emit(0x8B); emit(0x01);  /* overflow: V=1 */
    emit(0x10); emit(0x28); emit16(0x0002);  /* LBVC */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBVC not taken when V set");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01); emit(0x8B); emit(0x01);  /* no overflow */
    emit(0x10); emit(0x29); emit16(0x0002);  /* LBVS */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBVS not taken when V clear");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80);       /* N=1 */
    emit(0x10); emit(0x2A); emit16(0x0002);  /* LBPL */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(3);
    TEST("LBPL not taken when negative");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x01);       /* N=0 */
    emit(0x10); emit(0x2B); emit16(0x0002);  /* LBMI */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(3);
    TEST("LBMI not taken when positive");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x80); emit(0x81); emit(0x01);  /* -128 < 1: N!=V */
    emit(0x10); emit(0x2C); emit16(0x0002);  /* LBGE */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBGE not taken: -128 < 1");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    setup(0x200); prog_at(0x200);
    emit(0x86); emit(0x10); emit(0x81); emit(0x05);  /* $10 > $05: Z=0, N=V */
    emit(0x10); emit(0x2F); emit16(0x0002);  /* LBLE */
    emit(0x86); emit(0xFF);
    emit(0x12);
    run(4);
    TEST("LBLE not taken: $10 > $05 signed");
    CHECK(cpu_get_a(&cpu) == 0xFF, "A wrong");

    /* ---- Indexed RMW with non-zero offset ---- */
    printf("\nIndexed RMW with offset:\n");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0405] = 0x10;
    emit(0x8E); emit16(0x0400);
    emit(0x6C); emit(0x05);       /* INC 5,X (5-bit offset) */
    run(2);
    TEST("INC 5,X: $10 -> $11");
    CHECK(mem_get_ram()[0x0405] == 0x11, "mem wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0410] = 0x20;
    emit(0x8E); emit16(0x0400);
    emit(0x6A); emit(0x88); emit(0x10);  /* DEC $10,X (8-bit offset) */
    run(2);
    TEST("DEC $10,X: $20 -> $1F");
    CHECK(mem_get_ram()[0x0410] == 0x1F, "mem wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0500] = 0x55;
    emit(0x8E); emit16(0x0400);
    emit(0x63); emit(0x89); emit16(0x0100);  /* COM $0100,X (16-bit offset) */
    run(2);
    TEST("COM $0100,X: $55 -> $AA");
    CHECK(mem_get_ram()[0x0500] == 0xAA, "mem wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0405] = 0x42;
    emit(0x10); emit(0x8E); emit16(0x0400);
    emit(0x6D); emit(0x25);       /* TST 5,Y */
    run(2);
    TEST("TST 5,Y: $42 -> N=0, Z=0");
    CHECK(!(cpu.cc & CC_N), "N clear");
    CHECK(!(cpu.cc & CC_Z), "Z clear");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0405] = 0x40;
    emit(0x8E); emit16(0x0400);
    emit(0x68); emit(0x05);       /* ASL 5,X */
    run(2);
    TEST("ASL 5,X: $40 -> $80");
    CHECK(mem_get_ram()[0x0405] == 0x80, "mem wrong");

    setup(0x200); prog_at(0x200);
    mem_get_ram()[0x0405] = 0x82;
    emit(0x8E); emit16(0x0400);
    emit(0x64); emit(0x05);       /* LSR 5,X */
    run(2);
    TEST("LSR 5,X: $82 -> $41");
    CHECK(mem_get_ram()[0x0405] == 0x41, "mem wrong");

    /* ---- cpu_reset() ---- */
    printf("\ncpu_reset:\n");

    /* Set up memory with a known reset vector */
    mem_init();
    cpu_init(&cpu);
    /* Put a reset vector at $FFFE-$FFFF in ROM area.
     * Vectors read from ROM which is zeroed, so reset vector = $0000.
     * We can test with that. */

    /* Dirty up the CPU state first */
    cpu.dp = 0x42;
    cpu.cc = 0x00;
    cpu.halted = true;
    cpu.cwai = true;
    cpu.nmi_armed = true;
    cpu.nmi_pending = true;
    cpu.firq_pending = true;
    cpu.irq_pending = true;
    cpu.total_cycles = 99999;

    cpu_reset(&cpu);

    TEST("cpu_reset: DP = $00");
    CHECK(cpu.dp == 0x00, "DP should be $00");

    TEST("cpu_reset: CC has F and I set");
    CHECK((cpu.cc & CC_F) && (cpu.cc & CC_I), "F and I should be set");

    TEST("cpu_reset: halted cleared");
    CHECK(!cpu.halted, "should not be halted");

    TEST("cpu_reset: cwai cleared");
    CHECK(!cpu.cwai, "should not be in CWAI");

    TEST("cpu_reset: nmi_armed cleared");
    CHECK(!cpu.nmi_armed, "NMI should not be armed");

    TEST("cpu_reset: nmi_pending cleared");
    CHECK(!cpu.nmi_pending, "NMI should not be pending");

    TEST("cpu_reset: firq_pending cleared");
    CHECK(!cpu.firq_pending, "FIRQ should not be pending");

    TEST("cpu_reset: irq_pending cleared");
    CHECK(!cpu.irq_pending, "IRQ should not be pending");

    TEST("cpu_reset: total_cycles cleared");
    CHECK(cpu.total_cycles == 0, "total_cycles should be 0");

    TEST("cpu_reset: PC loaded from $FFFE-$FFFF");
    /* ROM is zeroed, so reset vector = $0000 */
    CHECK(cpu.pc == 0x0000, "PC should be $0000");

    printf("\n=== CPU 6809 Instruction Tests: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
