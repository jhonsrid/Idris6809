#include "dragon.h"
#include "savestate.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Audio ring buffer and filtering
 * ================================================================ */
#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_BUFFER_SIZE   16384

static int16_t audio_ring[AUDIO_BUFFER_SIZE];
static volatile int audio_write_pos = 0;
static volatile int audio_read_pos = 0;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int samples = len / (int)sizeof(int16_t);

    for (int i = 0; i < samples; i++) {
        if (audio_read_pos != audio_write_pos) {
            out[i] = audio_ring[audio_read_pos];
            audio_read_pos = (audio_read_pos + 1) % AUDIO_BUFFER_SIZE;
        } else {
            out[i] = 0;  /* Underrun: silence */
        }
    }
}

/* Simple single-pole low-pass filter + DC blocker */
static int32_t lpf_acc = 0;     /* Low-pass accumulator (fixed-point) */
static int32_t dc_acc = 0;      /* DC-blocking accumulator */

static void audio_push_sample(int16_t raw)
{
    /* Low-pass filter: smooths DAC staircase and cassette square waves.
     * Cutoff ~4.5 kHz at 44100 Hz (alpha ≈ 0.5) */
    lpf_acc += ((int32_t)raw * 256 - lpf_acc) >> 1;
    int16_t sample = (int16_t)(lpf_acc >> 8);

    /* DC-blocking high-pass filter: removes constant offsets.
     * Time constant ~50ms (alpha ≈ 1/2205 at 44100 Hz) */
    dc_acc += sample - (dc_acc >> 11);
    sample -= (int16_t)(dc_acc >> 11);

    int next = (audio_write_pos + 1) % AUDIO_BUFFER_SIZE;
    if (next != audio_read_pos)  {
        audio_ring[audio_write_pos] = sample;
        audio_write_pos = next;
    }
}

/* ================================================================
 * Keyboard mapping: SDL scancode -> Dragon matrix (row, col)
 *
 * Dragon 32 keyboard matrix:
 *   Row 0: 0 1 2 3 4 5 6 7
 *   Row 1: 8 9 : ; , - . /
 *   Row 2: @ A B C D E F G
 *   Row 3: H I J K L M N O
 *   Row 4: P Q R S T U V W
 *   Row 5: X Y Z UP DOWN LEFT RIGHT SPACE
 *   Row 6: ENTER CLEAR BREAK - - - - SHIFT
 *   Row 7: (unused)
 * ================================================================ */
typedef struct {
    SDL_Scancode scancode;
    int row;
    int col;
} KeyMapping;

static const KeyMapping key_map[] = {
    /* Row 0: digits 0-7 */
    { SDL_SCANCODE_0, 0, 0 },
    { SDL_SCANCODE_1, 0, 1 },
    { SDL_SCANCODE_2, 0, 2 },
    { SDL_SCANCODE_3, 0, 3 },
    { SDL_SCANCODE_4, 0, 4 },
    { SDL_SCANCODE_5, 0, 5 },
    { SDL_SCANCODE_6, 0, 6 },
    { SDL_SCANCODE_7, 0, 7 },

    /* Row 1: 8 9 : ; , - . / */
    { SDL_SCANCODE_8,         1, 0 },
    { SDL_SCANCODE_9,         1, 1 },
    { SDL_SCANCODE_SEMICOLON, 1, 3 },  /* ; */
    { SDL_SCANCODE_COMMA,     1, 4 },  /* , */
    { SDL_SCANCODE_MINUS,     1, 5 },  /* - */
    { SDL_SCANCODE_PERIOD,    1, 6 },  /* . */
    { SDL_SCANCODE_SLASH,     1, 7 },  /* / */

    /* Row 2: @ A B C D E F G */
    { SDL_SCANCODE_A, 2, 1 },
    { SDL_SCANCODE_B, 2, 2 },
    { SDL_SCANCODE_C, 2, 3 },
    { SDL_SCANCODE_D, 2, 4 },
    { SDL_SCANCODE_E, 2, 5 },
    { SDL_SCANCODE_F, 2, 6 },
    { SDL_SCANCODE_G, 2, 7 },

    /* Row 3: H I J K L M N O */
    { SDL_SCANCODE_H, 3, 0 },
    { SDL_SCANCODE_I, 3, 1 },
    { SDL_SCANCODE_J, 3, 2 },
    { SDL_SCANCODE_K, 3, 3 },
    { SDL_SCANCODE_L, 3, 4 },
    { SDL_SCANCODE_M, 3, 5 },
    { SDL_SCANCODE_N, 3, 6 },
    { SDL_SCANCODE_O, 3, 7 },

    /* Row 4: P Q R S T U V W */
    { SDL_SCANCODE_P, 4, 0 },
    { SDL_SCANCODE_Q, 4, 1 },
    { SDL_SCANCODE_R, 4, 2 },
    { SDL_SCANCODE_S, 4, 3 },
    { SDL_SCANCODE_T, 4, 4 },
    { SDL_SCANCODE_U, 4, 5 },
    { SDL_SCANCODE_V, 4, 6 },
    { SDL_SCANCODE_W, 4, 7 },

    /* Row 5: X Y Z UP DOWN LEFT RIGHT SPACE */
    { SDL_SCANCODE_X,      5, 0 },
    { SDL_SCANCODE_Y,      5, 1 },
    { SDL_SCANCODE_Z,      5, 2 },
    { SDL_SCANCODE_UP,     5, 3 },
    { SDL_SCANCODE_DOWN,   5, 4 },
    { SDL_SCANCODE_LEFT,   5, 5 },
    { SDL_SCANCODE_RIGHT,  5, 6 },
    { SDL_SCANCODE_SPACE,  5, 7 },

    /* Row 6: ENTER CLEAR BREAK - - - - SHIFT */
    { SDL_SCANCODE_RETURN,    6, 0 },  /* ENTER */
    { SDL_SCANCODE_BACKSPACE, 5, 5 },  /* Backspace -> LEFT */
    { SDL_SCANCODE_ESCAPE,    6, 2 },  /* Escape -> BREAK */
    { SDL_SCANCODE_TAB,       6, 1 },  /* Tab -> CLEAR */
    { SDL_SCANCODE_LSHIFT,    6, 7 },  /* Left Shift */
    { SDL_SCANCODE_RSHIFT,    6, 7 },  /* Right Shift */

    /* Extra mappings for convenience */
    { SDL_SCANCODE_GRAVE,        2, 0 },  /* ` -> @ */
    { SDL_SCANCODE_LEFTBRACKET,  1, 2 },  /* [ -> : */
    { SDL_SCANCODE_APOSTROPHE,   0, 7 },  /* ' -> 7 (Dragon SHIFT+7=') */
    /* Note: = key is NOT in the unshifted table.
     * Unshifted = is handled specially in handle_key. */
};

#define KEY_MAP_SIZE (sizeof(key_map) / sizeof(key_map[0]))

/* Shifted key remapping: modern US keyboard → Dragon matrix.
 * When the host SHIFT is held, some keys need to target a different
 * Dragon matrix position so the Dragon produces the expected character.
 *
 * Dragon shifted punctuation:
 *   SHIFT+1=!  +2="  +3=#  +4=$  +5=%  +6=&  +7='
 *   SHIFT+8=(  +9=)  +:=*  +;=+  +,=<  +-==  +.=>  +/=?
 *
 * Modern US shifted keys that differ:
 *   Shift+2=@ Shift+6=^ Shift+7=& Shift+8=* Shift+9=( Shift+0=)
 *   Shift+-=_ Shift+=+
 */
typedef struct {
    SDL_Scancode scancode;  /* Host key (when shift is held) */
    int row, col;           /* Dragon matrix target */
    bool need_shift;        /* Whether Dragon SHIFT should be pressed */
} ShiftRemap;

static const ShiftRemap shift_remap[] = {
    { SDL_SCANCODE_2, 2, 0, false },  /* Shift+2 → @ (unshifted) */
    { SDL_SCANCODE_6, 0, 6, true  },  /* Shift+6(^) → Dragon has no ^, send & (SHIFT+6) */
    { SDL_SCANCODE_7, 0, 6, true  },  /* Shift+7(&) → Dragon SHIFT+6=& */
    { SDL_SCANCODE_8, 1, 2, true  },  /* Shift+8(*) → Dragon SHIFT+:=* */
    { SDL_SCANCODE_9, 1, 0, true  },  /* Shift+9(() → Dragon SHIFT+8=( */
    { SDL_SCANCODE_0, 1, 1, true  },  /* Shift+0()) → Dragon SHIFT+9=) */
    { SDL_SCANCODE_EQUALS,     1, 3, true },  /* Shift+=(+) → Dragon SHIFT+;=+ */
    { SDL_SCANCODE_APOSTROPHE, 0, 2, true },  /* Shift+'(") → Dragon SHIFT+2=" */
    { SDL_SCANCODE_SEMICOLON,  1, 2, false }, /* Shift+;(:) → Dragon : (unshifted) */
};

#define SHIFT_REMAP_SIZE (sizeof(shift_remap) / sizeof(shift_remap[0]))

/* Track which Dragon key each host scancode was mapped to on press,
 * so the release always goes to the correct key regardless of
 * whether shift state has changed in between. */
static int8_t active_row[SDL_NUM_SCANCODES];
static int8_t active_col[SDL_NUM_SCANCODES];
/* Track keys that override Dragon SHIFT while held:
 *  +1 = force SHIFT on,  -1 = suppress SHIFT,  0 = no override */
static int8_t active_shift_override[SDL_NUM_SCANCODES];

static void init_key_tracking(void)
{
    memset(active_row, -1, sizeof(active_row));
    memset(active_col, -1, sizeof(active_col));
    memset(active_shift_override, 0, sizeof(active_shift_override));
}

static void handle_key(Dragon *d, SDL_Scancode sc, bool pressed, bool shift)
{
    /* SHIFT keys always map directly to Dragon SHIFT */
    if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) {
        dragon_key_press(d, 6, 7, pressed);
        return;
    }

    if (!pressed) {
        /* Key release: use the mapping recorded at press time */
        if (sc < SDL_NUM_SCANCODES && active_row[sc] >= 0) {
            dragon_key_press(d, active_row[sc], active_col[sc], false);
            /* Restore Dragon SHIFT if we were overriding it */
            if (active_shift_override[sc] != 0) {
                /* Restore SHIFT to match current host shift state */
                bool host_shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                dragon_key_press(d, 6, 7, host_shift);
                active_shift_override[sc] = 0;
            }
            active_row[sc] = -1;
            active_col[sc] = -1;
        }
        return;
    }

    /* Key press: determine mapping */
    int row = -1, col = -1;
    bool force_shift = false;    /* Add Dragon SHIFT (host has no shift) */
    bool suppress_shift = false; /* Remove Dragon SHIFT (host has shift) */

    if (shift) {
        /* Check shifted remap table first */
        for (int i = 0; i < (int)SHIFT_REMAP_SIZE; i++) {
            if (shift_remap[i].scancode == sc) {
                row = shift_remap[i].row;
                col = shift_remap[i].col;
                suppress_shift = !shift_remap[i].need_shift;
                break;
            }
        }
    }

    /* Unshifted keys that need Dragon SHIFT injected:
     * Modern = (unshifted) → Dragon SHIFT+- to produce = */
    if (row < 0 && !shift && sc == SDL_SCANCODE_EQUALS) {
        row = 1; col = 5;  /* Dragon - key */
        force_shift = true;
    }

    /* Fall through to normal mapping if no remap found */
    if (row < 0) {
        for (int i = 0; i < (int)KEY_MAP_SIZE; i++) {
            if (key_map[i].scancode == sc) {
                row = key_map[i].row;
                col = key_map[i].col;
                break;
            }
        }
    }

    if (row < 0)
        return;  /* Unknown key */

    /* Adjust Dragon SHIFT state for remapped keys.
     * Override persists until the key is released. */
    if (suppress_shift)
        dragon_key_press(d, 6, 7, false);
    if (force_shift)
        dragon_key_press(d, 6, 7, true);

    dragon_key_press(d, row, col, true);

    /* Record mapping for release */
    if (sc < SDL_NUM_SCANCODES) {
        active_row[sc] = (int8_t)row;
        active_col[sc] = (int8_t)col;
        active_shift_override[sc] = suppress_shift ? -1 : (force_shift ? 1 : 0);
    }
}

/* ================================================================
 * Usage and argument parsing
 * ================================================================ */
static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "  --rom PATH      ROM file (default: ROMS/d32.rom)\n");
    fprintf(stderr, "  --cas PATH      Load cassette .cas file\n");
    fprintf(stderr, "  --cartridge PATH  Load cartridge ROM (8KB/16KB, maps at $C000)\n");
    fprintf(stderr, "  --load PATH     Load saved state from file\n");
    fprintf(stderr, "  --scale N       Window scale factor (default: 3)\n");
    fprintf(stderr, "  --nosound       Disable audio\n");
    fprintf(stderr, "  --noloadsound   Disable cassette loading sounds\n");
    fprintf(stderr, "  --headless N    Run N frames without display then exit\n");
    fprintf(stderr, "  --debug         Enable debug output\n");
}

/* ================================================================
 * Main
 * ================================================================ */
int main(int argc, char *argv[])
{
    const char *rom_path  = NULL;
    const char *cas_path  = NULL;
    const char *cart_path = NULL;
    const char *load_path = NULL;
    int scale = 3;
    bool enable_sound = true;
    bool enable_cas_sound = true;
    bool debug = false;
    int headless_frames = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc)
            rom_path = argv[++i];
        else if (strcmp(argv[i], "--cas") == 0 && i + 1 < argc)
            cas_path = argv[++i];
        else if (strcmp(argv[i], "--cartridge") == 0 && i + 1 < argc)
            cart_path = argv[++i];
        else if (strcmp(argv[i], "--load") == 0 && i + 1 < argc)
            load_path = argv[++i];
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--nosound") == 0)
            enable_sound = false;
        else if (strcmp(argv[i], "--noloadsound") == 0)
            enable_cas_sound = false;
        else if (strcmp(argv[i], "--headless") == 0 && i + 1 < argc) {
            headless_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--debug") == 0)
            debug = true;
        else {
            usage(argv[0]);
            return 1;
        }
    }

    /* Default ROM */
    if (!rom_path)
        rom_path = "ROMS/d32.rom";

    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;

    /* Initialize machine */
    Dragon dragon;
    dragon_init(&dragon);

    if (dragon_load_rom(rom_path) != 0)
        return 1;

    if (cart_path) {
        if (mem_load_cartridge(cart_path) != 0)
            return 1;
        if (debug)
            fprintf(stderr, "Cartridge loaded: %s\n", cart_path);
    }

    dragon_reset(&dragon);
    init_key_tracking();

    if (cas_path) {
        if (cassette_load(&dragon.cassette, cas_path) != 0)
            return 1;
        if (debug)
            fprintf(stderr, "Cassette loaded: %s (%zu bytes)\n",
                    cas_path, cassette_get_size(&dragon.cassette));
    }

    if (load_path) {
        if (savestate_load(&dragon, load_path) != 0)
            return 1;
        if (debug)
            fprintf(stderr, "State loaded: %s\n", load_path);
    }

    if (debug)
        fprintf(stderr, "PC: $%04X\n", dragon.cpu.pc);

    /* Headless mode: run N frames and exit (for testing) */
    if (headless_frames > 0) {
        for (int f = 0; f < headless_frames; f++)
            dragon_run_frame(&dragon);
        printf("Ran %d frames, %d total cycles\n",
               headless_frames, dragon.cpu.total_cycles);
        cpu_dump_state(&dragon.cpu);
        return 0;
    }

    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int win_w = VDG_WIDTH * scale;
    int win_h = VDG_HEIGHT * scale;

    SDL_Window *window = SDL_CreateWindow(
        "Idris6809 — Dragon 32",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Set integer scaling for crisp pixels */
    SDL_RenderSetLogicalSize(renderer, VDG_WIDTH, VDG_HEIGHT);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);

    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        VDG_WIDTH, VDG_HEIGHT
    );
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Audio setup */
    SDL_AudioDeviceID audio_dev = 0;
    if (enable_sound) {
        SDL_AudioSpec want, have;
        SDL_memset(&want, 0, sizeof(want));
        want.freq = AUDIO_SAMPLE_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        want.callback = audio_callback;

        audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audio_dev == 0) {
            fprintf(stderr, "SDL audio failed: %s (continuing without sound)\n",
                    SDL_GetError());
            enable_sound = false;
        } else {
            SDL_PauseAudioDevice(audio_dev, 0);  /* Start playback */
        }
    }

    /* Audio timing: how many CPU cycles per audio sample */
    double cycles_per_sample = (double)DRAGON_CPU_HZ / AUDIO_SAMPLE_RATE;
    double audio_cycle_accum = 0.0;

    /* Main loop */
    Uint32 frame_start;
    while (dragon.running) {
        frame_start = SDL_GetTicks();

        /* Handle events */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                dragon.running = false;
                break;
            case SDL_KEYDOWN:
                if (!event.key.repeat) {
                    if (event.key.keysym.scancode == SDL_SCANCODE_F5) {
                        char savefile[64];
                        savestate_make_filename(savefile, sizeof(savefile));
                        if (savestate_save(&dragon, savefile) == 0)
                            fprintf(stderr, "State saved: %s\n", savefile);
                        else
                            fprintf(stderr, "Save state failed!\n");
                    } else {
                        bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
                        handle_key(&dragon, event.key.keysym.scancode, true, shift);
                    }
                }
                break;
            case SDL_KEYUP:
                handle_key(&dragon, event.key.keysym.scancode, false, false);
                break;
            }
        }

        /* Run one frame, generating audio samples as we go */
        for (int sl = 0; sl < DRAGON_SCANLINES_PER_FRAME; sl++) {
            int cycles = dragon_run_scanline(&dragon);

            /* Generate audio samples for this scanline's worth of cycles */
            if (enable_sound) {
                audio_cycle_accum += cycles;
                while (audio_cycle_accum >= cycles_per_sample) {
                    audio_cycle_accum -= cycles_per_sample;

                    /* Read DAC value from PIA1 port A bits 0-5 */
                    uint8_t dac = dragon.pia1.ora & 0x3F;
                    /* Single-bit sound from PIA1 port B bit 1 */
                    bool sbs = (dragon.pia1.orb & 0x02) != 0;

                    /* Mix: DAC is 6-bit (0-63), scale to 16-bit range.
                     * Single-bit sound adds a pulse. */
                    int16_t sample = (int16_t)((int)dac - 32) * 512;
                    if (sbs)
                        sample += 4096;

                    /* Mix in cassette audio when playing */
                    if (enable_cas_sound && cassette_is_playing(&dragon.cassette))
                        sample += dragon.cassette.signal_level ? 4096 : -4096;

                    audio_push_sample(sample);
                }
            }
        }
        dragon_end_frame(&dragon);

        /* Update texture from framebuffer */
        const uint32_t *fb = dragon_get_framebuffer(&dragon);
        SDL_UpdateTexture(texture, NULL, fb, VDG_WIDTH * (int)sizeof(uint32_t));

        /* Render */
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        /* Frame throttling (if vsync isn't active): ~20ms for PAL 50Hz */
        Uint32 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < 20)
            SDL_Delay(20 - elapsed);
    }

    /* Cleanup */
    if (audio_dev)
        SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (debug) {
        fprintf(stderr, "Final state after %d frames:\n", dragon.frame_count);
        cpu_dump_state(&dragon.cpu);
    }

    return 0;
}
