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

    printf("\n=== CPU 6809 Instruction Tests: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
