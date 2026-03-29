#include "savestate.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int pass_count, fail_count;
static const char *current_test;

#define TEST(name) do { current_test = (name); printf("  %s", name); } while(0)
#define CHECK(cond, msg) do { \
    if (cond) { printf("%*sPASS\n", (int)(60 - strlen(current_test)), ""); pass_count++; } \
    else { printf("%*sFAIL: %s\n", (int)(60 - strlen(current_test)), "", msg); fail_count++; } \
} while(0)

#define TMPFILE "/tmp/idris6809_test.state"

int main(void)
{
    printf("=== Save State Tests ===\n");

    /* --- 1: Round-trip save/load --- */
    printf("\nRound-trip save/load:\n");

    Dragon d;
    dragon_init(&d);
    dragon_load_rom("ROMS/d32.rom");
    dragon_reset(&d);

    /* Run some frames to get non-trivial state */
    for (int f = 0; f < 50; f++)
        dragon_run_frame(&d);

    /* Press a key to modify keyboard state */
    dragon_key_press(&d, 2, 1, true);
    dragon_run_frame(&d);

    /* Capture state before save */
    uint16_t saved_pc = d.cpu.pc;
    uint16_t saved_s  = d.cpu.s;
    uint8_t  saved_cc = d.cpu.cc;
    int      saved_total = d.cpu.total_cycles;
    int      saved_debt  = d.cycle_debt;
    int      saved_frame = d.frame_count;
    uint8_t  saved_kb0   = d.keyboard[0];
    uint8_t  saved_pia0_crb = d.pia0.crb;
    uint8_t  saved_pia1_orb = d.pia1.orb;
    bool     saved_rom_mode = mem_get_rom_mode();
    uint8_t  saved_sam_ty = d.sam.ty;
    uint8_t  saved_vdg_gm = d.vdg.gm;
    uint8_t *ram = mem_get_ram();
    uint8_t  saved_ram_byte = ram[0x0400];

    /* Save */
    int rc = savestate_save(&d, TMPFILE);
    TEST("Save succeeds");
    CHECK(rc == 0, "save returned error");

    /* Trash the state */
    memset(&d.cpu, 0, sizeof(d.cpu));
    memset(&d.sam, 0, sizeof(d.sam));
    memset(d.keyboard, 0xFF, sizeof(d.keyboard));
    d.cycle_debt = 0;
    d.frame_count = 0;
    d.pia0.crb = 0;
    d.pia1.orb = 0;
    ram[0x0400] = 0xAA;

    /* Load */
    rc = savestate_load(&d, TMPFILE);
    TEST("Load succeeds");
    CHECK(rc == 0, "load returned error");

    /* Verify all state restored */
    TEST("CPU PC restored");
    CHECK(d.cpu.pc == saved_pc, "PC mismatch");

    TEST("CPU S restored");
    CHECK(d.cpu.s == saved_s, "S mismatch");

    TEST("CPU CC restored");
    CHECK(d.cpu.cc == saved_cc, "CC mismatch");

    TEST("CPU total_cycles restored");
    CHECK(d.cpu.total_cycles == saved_total, "total_cycles mismatch");

    TEST("cycle_debt restored");
    CHECK(d.cycle_debt == saved_debt, "cycle_debt mismatch");

    TEST("frame_count restored");
    CHECK(d.frame_count == saved_frame, "frame_count mismatch");

    TEST("Keyboard state restored");
    CHECK(d.keyboard[0] == saved_kb0, "keyboard mismatch");

    TEST("PIA0 CRB restored");
    CHECK(d.pia0.crb == saved_pia0_crb, "PIA0 CRB mismatch");

    TEST("PIA1 ORB restored");
    CHECK(d.pia1.orb == saved_pia1_orb, "PIA1 ORB mismatch");

    TEST("ROM mode restored");
    CHECK(mem_get_rom_mode() == saved_rom_mode, "rom_mode mismatch");

    TEST("SAM TY restored");
    CHECK(d.sam.ty == saved_sam_ty, "SAM TY mismatch");

    TEST("VDG GM restored");
    CHECK(d.vdg.gm == saved_vdg_gm, "VDG GM mismatch");

    TEST("RAM content restored");
    CHECK(ram[0x0400] == saved_ram_byte, "RAM mismatch");

    /* --- 2: Bad file handling --- */
    printf("\nError handling:\n");

    /* Suppress expected error messages during negative tests */
    FILE *saved_stderr = stderr;
    stderr = fopen("/dev/null", "w");

    TEST("Load non-existent file fails");
    CHECK(savestate_load(&d, "/tmp/no_such_file.state") != 0, "should fail");

    /* Write garbage to test magic validation */
    FILE *f = fopen(TMPFILE, "wb");
    fprintf(f, "GARBAGE");
    fclose(f);
    TEST("Load garbage file fails");
    CHECK(savestate_load(&d, TMPFILE) != 0, "should fail on bad magic");

    fclose(stderr);
    stderr = saved_stderr;

    /* --- 3: Filename generation --- */
    printf("\nFilename generation:\n");
    char fname[64];
    savestate_make_filename(fname, sizeof(fname));
    TEST("Filename ends with .state");
    CHECK(strstr(fname, ".state") != NULL, "missing .state extension");

    TEST("Filename has date format");
    CHECK(strlen(fname) == 21, "expected yyyymmdd-hhmmss.state (21 chars)");

    /* --- 4: Version mismatch --- */
    printf("\nVersion mismatch:\n");
    {
        FILE *saved_err = stderr;
        stderr = fopen("/dev/null", "w");

        /* Write file with correct magic but wrong version */
        f = fopen(TMPFILE, "wb");
        uint32_t magic = SAVESTATE_MAGIC;
        uint32_t bad_version = 0xFFFF;
        fwrite(&magic, 4, 1, f);
        fwrite(&bad_version, 4, 1, f);
        fclose(f);

        TEST("Load with wrong version fails");
        CHECK(savestate_load(&d, TMPFILE) != 0, "should fail on bad version");

        fclose(stderr);
        stderr = saved_err;
    }

    /* --- 5: Truncated file --- */
    printf("\nTruncated file:\n");
    {
        FILE *saved_err = stderr;
        stderr = fopen("/dev/null", "w");

        /* Write correct magic+version but truncate after that */
        f = fopen(TMPFILE, "wb");
        uint32_t magic = SAVESTATE_MAGIC;
        uint32_t version = SAVESTATE_VERSION;
        fwrite(&magic, 4, 1, f);
        fwrite(&version, 4, 1, f);
        /* Write only 100 bytes of "RAM" instead of full 64KB */
        uint8_t partial[100];
        memset(partial, 0, sizeof(partial));
        fwrite(partial, 1, sizeof(partial), f);
        fclose(f);

        TEST("Load truncated file fails");
        CHECK(savestate_load(&d, TMPFILE) != 0, "should fail on truncated data");

        fclose(stderr);
        stderr = saved_err;
    }

    /* --- 6: Cassette state save/load (position preserved) --- */
    printf("\nCassette state in savestate:\n");
    {
        /* Create a temporary cassette file */
        const char *cas_path = "/tmp/idris6809_test_cas.cas";
        f = fopen(cas_path, "wb");
        uint8_t cas_data[] = { 0x55, 0x55, 0x55, 0x3C, 0x01, 0x00, 0xFF, 0xFF };
        fwrite(cas_data, 1, sizeof(cas_data), f);
        fclose(f);

        /* Set up dragon with cassette */
        dragon_init(&d);
        dragon_load_rom("ROMS/d32.rom");
        dragon_reset(&d);

        /* Load cassette and advance playback */
        cassette_load(&d.cassette, cas_path);
        cassette_set_motor(&d.cassette, true);
        /* Run some updates to advance cassette position */
        for (int i = 0; i < 20; i++)
            cassette_update(&d.cassette, 218);

        size_t saved_byte_pos = d.cassette.byte_pos;
        int saved_bit_pos = d.cassette.bit_pos;
        bool saved_motor = d.cassette.motor_on;
        bool saved_playing = d.cassette.playing;

        /* Save state */
        rc = savestate_save(&d, TMPFILE);
        TEST("Save with cassette state succeeds");
        CHECK(rc == 0, "save failed");

        /* Trash cassette state but keep data loaded */
        d.cassette.byte_pos = 0;
        d.cassette.bit_pos = 0;
        d.cassette.motor_on = false;

        /* Load state */
        rc = savestate_load(&d, TMPFILE);
        TEST("Load restores cassette byte_pos");
        CHECK(d.cassette.byte_pos == saved_byte_pos, "byte_pos mismatch");

        TEST("Load restores cassette bit_pos");
        CHECK(d.cassette.bit_pos == saved_bit_pos, "bit_pos mismatch");

        TEST("Load restores cassette motor_on");
        CHECK(d.cassette.motor_on == saved_motor, "motor_on mismatch");

        TEST("Load restores cassette playing");
        CHECK(d.cassette.playing == saved_playing, "playing mismatch");

        /* Cassette data pointer is NOT saved — verify it's still valid */
        TEST("Cassette data pointer preserved (not saved/restored)");
        CHECK(d.cassette.data != NULL, "data should still be allocated");

        cassette_eject(&d.cassette);
        unlink(cas_path);
    }

    /* Cleanup */
    unlink(TMPFILE);

    printf("\n=== Save State Tests: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
