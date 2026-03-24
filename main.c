#include "dragon.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Audio ring buffer
 * ================================================================ */
#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_BUFFER_SIZE   4096

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

static void audio_push_sample(int16_t sample)
{
    int next = (audio_write_pos + 1) % AUDIO_BUFFER_SIZE;
    if (next != audio_read_pos) {  /* Drop if full */
        audio_ring[audio_write_pos] = sample;
        audio_write_pos = next;
    }
}

/* ================================================================
 * Keyboard mapping: SDL scancode -> Dragon matrix (row, col)
 *
 * Dragon 64 keyboard matrix:
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

    /* Shifted punctuation: colon via Shift+; is handled by SHIFT key above.
     * @ is at row 2 col 0 — map to a convenient key */
    { SDL_SCANCODE_GRAVE,     2, 0 },  /* ` -> @ */
    { SDL_SCANCODE_LEFTBRACKET,  1, 2 },  /* [ -> : (close enough) */
};

#define KEY_MAP_SIZE (sizeof(key_map) / sizeof(key_map[0]))

static void handle_key(Dragon *d, SDL_Scancode sc, bool pressed)
{
    for (int i = 0; i < (int)KEY_MAP_SIZE; i++) {
        if (key_map[i].scancode == sc) {
            dragon_key_press(d, key_map[i].row, key_map[i].col, pressed);
            return;
        }
    }
}

/* ================================================================
 * Usage and argument parsing
 * ================================================================ */
static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "  --rom PATH      Single ROM (Dragon 32: 16KB mirrored, or 32KB combined)\n");
    fprintf(stderr, "  --rom1 PATH     Dragon 64 ROM 1 (default: ROMS/d64_1.rom)\n");
    fprintf(stderr, "  --rom2 PATH     Dragon 64 ROM 2 (default: ROMS/d64_2.rom)\n");
    fprintf(stderr, "  --scale N       Window scale factor (default: 3)\n");
    fprintf(stderr, "  --nosound       Disable audio\n");
    fprintf(stderr, "  --headless N    Run N frames without display then exit\n");
    fprintf(stderr, "  --debug         Enable debug output\n");
}

/* ================================================================
 * Main
 * ================================================================ */
int main(int argc, char *argv[])
{
    const char *rom_path  = NULL;  /* single ROM (Dragon 32 / combined) */
    const char *rom1_path = NULL;
    const char *rom2_path = NULL;
    int scale = 3;
    bool enable_sound = true;
    bool debug = false;
    int headless_frames = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc)
            rom_path = argv[++i];
        else if (strcmp(argv[i], "--rom1") == 0 && i + 1 < argc)
            rom1_path = argv[++i];
        else if (strcmp(argv[i], "--rom2") == 0 && i + 1 < argc)
            rom2_path = argv[++i];
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--nosound") == 0)
            enable_sound = false;
        else if (strcmp(argv[i], "--headless") == 0 && i + 1 < argc) {
            headless_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--debug") == 0)
            debug = true;
        else {
            usage(argv[0]);
            return 1;
        }
    }

    /* Default to Dragon 64 ROMs if nothing specified */
    if (!rom_path && !rom1_path && !rom2_path) {
        rom1_path = "ROMS/d64_1.rom";
        rom2_path = "ROMS/d64_2.rom";
    }

    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;

    /* Initialize machine — single ROM implies Dragon 32 */
    DragonModel model = rom_path ? DRAGON_32 : DRAGON_64;
    Dragon dragon;
    dragon_init(&dragon, model);

    if (rom_path) {
        if (dragon_load_roms(&dragon, rom_path, NULL) != 0)
            return 1;
    } else {
        if (dragon_load_roms(&dragon, rom1_path, rom2_path) != 0)
            return 1;
    }

    dragon_reset(&dragon);

    if (debug)
        fprintf(stderr, "Reset vector: $%04X\n", dragon.cpu.pc);

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
        "Idris6809 — Dragon 64",
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
                if (!event.key.repeat)
                    handle_key(&dragon, event.key.keysym.scancode, true);
                break;
            case SDL_KEYUP:
                handle_key(&dragon, event.key.keysym.scancode, false);
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

                    audio_push_sample(sample);
                }
            }
        }
        dragon.frame_count++;

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
