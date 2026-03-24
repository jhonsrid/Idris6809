#include "vdg.h"
#include <string.h>

/* ================================================================
 * MC6847 Internal Character ROM
 *
 * 64 characters (codes $00-$3F), each 8 pixels wide × 12 rows tall.
 * The actual glyph occupies a 5×7 area within the 8×12 cell.
 * Top 2 rows and bottom 3 rows are blank; glyph is in rows 2-8.
 * Only the top 5 bits of each byte are significant (left-justified).
 *
 * Character mapping:
 *   $00 = '@'  $01 = 'A'  ...  $1A = 'Z'  $1B = '['  $1C = '\'
 *   $1D = ']'  $1E = '^'  $1F = '_' (down-arrow on real VDG)
 *   $20 = ' '  $21 = '!'  ...  $3F = '?'
 *
 * This is the standard MC6847 font as documented.
 * Each row is stored MSB-first (bit 7 = leftmost pixel).
 * ================================================================ */
static const uint8_t mc6847_font[64][12] = {
    /* $00 @ */ { 0x00,0x00,0x38,0x44,0x5C,0x58,0x42,0x3C,0x00,0x00,0x00,0x00 },
    /* $01 A */ { 0x00,0x00,0x10,0x28,0x44,0x7C,0x44,0x44,0x00,0x00,0x00,0x00 },
    /* $02 B */ { 0x00,0x00,0x78,0x44,0x78,0x44,0x44,0x78,0x00,0x00,0x00,0x00 },
    /* $03 C */ { 0x00,0x00,0x38,0x44,0x40,0x40,0x44,0x38,0x00,0x00,0x00,0x00 },
    /* $04 D */ { 0x00,0x00,0x78,0x44,0x44,0x44,0x44,0x78,0x00,0x00,0x00,0x00 },
    /* $05 E */ { 0x00,0x00,0x7C,0x40,0x78,0x40,0x40,0x7C,0x00,0x00,0x00,0x00 },
    /* $06 F */ { 0x00,0x00,0x7C,0x40,0x78,0x40,0x40,0x40,0x00,0x00,0x00,0x00 },
    /* $07 G */ { 0x00,0x00,0x38,0x44,0x40,0x4C,0x44,0x38,0x00,0x00,0x00,0x00 },
    /* $08 H */ { 0x00,0x00,0x44,0x44,0x7C,0x44,0x44,0x44,0x00,0x00,0x00,0x00 },
    /* $09 I */ { 0x00,0x00,0x38,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00 },
    /* $0A J */ { 0x00,0x00,0x04,0x04,0x04,0x04,0x44,0x38,0x00,0x00,0x00,0x00 },
    /* $0B K */ { 0x00,0x00,0x44,0x48,0x70,0x48,0x44,0x44,0x00,0x00,0x00,0x00 },
    /* $0C L */ { 0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x7C,0x00,0x00,0x00,0x00 },
    /* $0D M */ { 0x00,0x00,0x44,0x6C,0x54,0x44,0x44,0x44,0x00,0x00,0x00,0x00 },
    /* $0E N */ { 0x00,0x00,0x44,0x64,0x54,0x4C,0x44,0x44,0x00,0x00,0x00,0x00 },
    /* $0F O */ { 0x00,0x00,0x38,0x44,0x44,0x44,0x44,0x38,0x00,0x00,0x00,0x00 },
    /* $10 P */ { 0x00,0x00,0x78,0x44,0x44,0x78,0x40,0x40,0x00,0x00,0x00,0x00 },
    /* $11 Q */ { 0x00,0x00,0x38,0x44,0x44,0x54,0x48,0x34,0x00,0x00,0x00,0x00 },
    /* $12 R */ { 0x00,0x00,0x78,0x44,0x44,0x78,0x48,0x44,0x00,0x00,0x00,0x00 },
    /* $13 S */ { 0x00,0x00,0x38,0x40,0x38,0x04,0x04,0x78,0x00,0x00,0x00,0x00 },
    /* $14 T */ { 0x00,0x00,0x7C,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00 },
    /* $15 U */ { 0x00,0x00,0x44,0x44,0x44,0x44,0x44,0x38,0x00,0x00,0x00,0x00 },
    /* $16 V */ { 0x00,0x00,0x44,0x44,0x44,0x28,0x28,0x10,0x00,0x00,0x00,0x00 },
    /* $17 W */ { 0x00,0x00,0x44,0x44,0x44,0x54,0x6C,0x44,0x00,0x00,0x00,0x00 },
    /* $18 X */ { 0x00,0x00,0x44,0x28,0x10,0x28,0x44,0x44,0x00,0x00,0x00,0x00 },
    /* $19 Y */ { 0x00,0x00,0x44,0x28,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00 },
    /* $1A Z */ { 0x00,0x00,0x7C,0x08,0x10,0x20,0x40,0x7C,0x00,0x00,0x00,0x00 },
    /* $1B [ */ { 0x00,0x00,0x38,0x20,0x20,0x20,0x20,0x38,0x00,0x00,0x00,0x00 },
    /* $1C \ */ { 0x00,0x00,0x40,0x20,0x10,0x08,0x04,0x04,0x00,0x00,0x00,0x00 },
    /* $1D ] */ { 0x00,0x00,0x38,0x08,0x08,0x08,0x08,0x38,0x00,0x00,0x00,0x00 },
    /* $1E ^ (up arrow) */ { 0x00,0x00,0x10,0x38,0x54,0x10,0x10,0x10,0x00,0x00,0x00,0x00 },
    /* $1F _ (down arrow) */ { 0x00,0x00,0x10,0x10,0x10,0x54,0x38,0x10,0x00,0x00,0x00,0x00 },
    /* $20 (space) */ { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    /* $21 ! */ { 0x00,0x00,0x10,0x10,0x10,0x10,0x00,0x10,0x00,0x00,0x00,0x00 },
    /* $22 " */ { 0x00,0x00,0x28,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    /* $23 # */ { 0x00,0x00,0x28,0x7C,0x28,0x28,0x7C,0x28,0x00,0x00,0x00,0x00 },
    /* $24 $ */ { 0x00,0x00,0x10,0x3C,0x50,0x38,0x14,0x78,0x10,0x00,0x00,0x00 },
    /* $25 % */ { 0x00,0x00,0x60,0x64,0x08,0x10,0x20,0x4C,0x0C,0x00,0x00,0x00 },
    /* $26 & */ { 0x00,0x00,0x20,0x50,0x20,0x54,0x48,0x34,0x00,0x00,0x00,0x00 },
    /* $27 ' */ { 0x00,0x00,0x10,0x10,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    /* $28 ( */ { 0x00,0x00,0x08,0x10,0x20,0x20,0x10,0x08,0x00,0x00,0x00,0x00 },
    /* $29 ) */ { 0x00,0x00,0x20,0x10,0x08,0x08,0x10,0x20,0x00,0x00,0x00,0x00 },
    /* $2A * */ { 0x00,0x00,0x00,0x28,0x10,0x7C,0x10,0x28,0x00,0x00,0x00,0x00 },
    /* $2B + */ { 0x00,0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00,0x00,0x00 },
    /* $2C , */ { 0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x20,0x00,0x00,0x00 },
    /* $2D - */ { 0x00,0x00,0x00,0x00,0x00,0x7C,0x00,0x00,0x00,0x00,0x00,0x00 },
    /* $2E . */ { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00 },
    /* $2F / */ { 0x00,0x00,0x04,0x08,0x10,0x20,0x40,0x40,0x00,0x00,0x00,0x00 },
    /* $30 0 */ { 0x00,0x00,0x38,0x44,0x4C,0x54,0x64,0x38,0x00,0x00,0x00,0x00 },
    /* $31 1 */ { 0x00,0x00,0x10,0x30,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00 },
    /* $32 2 */ { 0x00,0x00,0x38,0x44,0x08,0x10,0x20,0x7C,0x00,0x00,0x00,0x00 },
    /* $33 3 */ { 0x00,0x00,0x38,0x44,0x18,0x04,0x44,0x38,0x00,0x00,0x00,0x00 },
    /* $34 4 */ { 0x00,0x00,0x08,0x18,0x28,0x48,0x7C,0x08,0x00,0x00,0x00,0x00 },
    /* $35 5 */ { 0x00,0x00,0x7C,0x40,0x78,0x04,0x04,0x78,0x00,0x00,0x00,0x00 },
    /* $36 6 */ { 0x00,0x00,0x18,0x20,0x78,0x44,0x44,0x38,0x00,0x00,0x00,0x00 },
    /* $37 7 */ { 0x00,0x00,0x7C,0x04,0x08,0x10,0x10,0x10,0x00,0x00,0x00,0x00 },
    /* $38 8 */ { 0x00,0x00,0x38,0x44,0x38,0x44,0x44,0x38,0x00,0x00,0x00,0x00 },
    /* $39 9 */ { 0x00,0x00,0x38,0x44,0x44,0x3C,0x08,0x30,0x00,0x00,0x00,0x00 },
    /* $3A : */ { 0x00,0x00,0x00,0x10,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00 },
    /* $3B ; */ { 0x00,0x00,0x00,0x10,0x00,0x00,0x10,0x10,0x20,0x00,0x00,0x00 },
    /* $3C < */ { 0x00,0x00,0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x00,0x00,0x00 },
    /* $3D = */ { 0x00,0x00,0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00,0x00,0x00 },
    /* $3E > */ { 0x00,0x00,0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x00,0x00,0x00 },
    /* $3F ? */ { 0x00,0x00,0x38,0x44,0x08,0x10,0x00,0x10,0x00,0x00,0x00,0x00 },
};

/* ================================================================
 * Color palette
 *
 * MC6847 standard colors (approximated as RGB).
 * ================================================================ */
enum {
    COL_GREEN       = 0,
    COL_YELLOW      = 1,
    COL_BLUE        = 2,
    COL_RED         = 3,
    COL_WHITE       = 4,
    COL_CYAN        = 5,
    COL_MAGENTA     = 6,
    COL_ORANGE      = 7,
    COL_BLACK       = 8,
    COL_DARK_GREEN  = 9,
    COL_DARK_ORANGE = 10,
    COL_BRIGHT_GREEN= 11,
};

static const uint32_t vdg_palette[] = {
    [COL_GREEN]        = 0x0000B400,  /* Green */
    [COL_YELLOW]       = 0x00B4B400,  /* Yellow */
    [COL_BLUE]         = 0x000000B4,  /* Blue */
    [COL_RED]          = 0x00B40000,  /* Red */
    [COL_WHITE]        = 0x00B4B4B4,  /* White/buff */
    [COL_CYAN]         = 0x0000B4B4,  /* Cyan */
    [COL_MAGENTA]      = 0x00B400B4,  /* Magenta */
    [COL_ORANGE]       = 0x00B46400,  /* Orange */
    [COL_BLACK]        = 0x00000000,  /* Black */
    [COL_DARK_GREEN]   = 0x00005000,  /* Dark green (for text BG) */
    [COL_DARK_ORANGE]  = 0x00503000,  /* Dark orange (for text BG) */
    [COL_BRIGHT_GREEN] = 0x0000FF00,  /* Bright green (text FG) */
};

/* Color set tables for graphics modes */

/* CSS=0: green, yellow, blue, red */
/* CSS=1: white/buff, cyan, magenta, orange */
static const uint32_t cg_colors[2][4] = {
    { 0x0000B400, 0x00B4B400, 0x000000B4, 0x00B40000 },  /* CSS=0 */
    { 0x00B4B4B4, 0x0000B4B4, 0x00B400B4, 0x00B46400 },  /* CSS=1 */
};

/* Resolution graphics: CSS selects FG color; BG is always black */
static const uint32_t rg_fg_colors[2] = {
    0x0000B400,  /* CSS=0: green */
    0x00B4B4B4,  /* CSS=1: white/buff */
};

/* Semigraphics 4 colors (from bits in the character byte) */
static const uint32_t sg4_colors[8] = {
    0x0000B400,  /* 0 = green */
    0x00B4B400,  /* 1 = yellow */
    0x000000B4,  /* 2 = blue */
    0x00B40000,  /* 3 = red */
    0x00B4B4B4,  /* 4 = white/buff */
    0x0000B4B4,  /* 5 = cyan */
    0x00B400B4,  /* 6 = magenta */
    0x00B46400,  /* 7 = orange */
};

/* ================================================================
 * Bytes per row for each graphics mode (GM value when A/G=1)
 *
 * The SAM V register controls bytes-per-row addressing:
 *   V=0: 32 bytes/row (text, SG4, SG6, CG1, RG1)
 *   V=1,2: varies by mode
 *
 * Simpler: we compute bytes per row from the mode.
 * ================================================================ */

/*
 * Bytes of video RAM per data row for each GM mode:
 *   CG1(0): 16  RG1(1): 16  CG2(2): 32  RG2(3): 16
 *   CG3(4): 32  RG3(5): 16  CG6(6): 32  RG6(7): 32
 */

/* Vertical repeat factor: how many screen lines per data row */
static const int gm_vrepeat[8] = {
    3,  /* CG1: 64 rows -> 192/64 = 3 */
    3,  /* RG1: 64 rows -> 3 */
    3,  /* CG2: 64 rows -> 3 */
    2,  /* RG2: 96 rows -> 2 */
    2,  /* CG3: 96 rows -> 2 */
    1,  /* RG3: 192 rows -> 1 */
    1,  /* CG6: 192 rows -> 1 */
    1,  /* RG6: 192 rows -> 1 */
};

/* ================================================================
 * Rendering
 * ================================================================ */

void vdg_init(VDG *vdg, const uint8_t *ram)
{
    memset(vdg, 0, sizeof(*vdg));
    vdg->ram = ram;
    vdg->ag = false;
    vdg->gm = 0;
    vdg->css = false;
    vdg->scanline = 0;
    vdg->fs = false;

    /* Fill framebuffer with black */
    memset(vdg->framebuffer, 0, sizeof(vdg->framebuffer));
}

void vdg_set_mode(VDG *vdg, uint8_t pia1b)
{
    vdg->ag  = (pia1b & 0x80) != 0;
    vdg->gm  = (pia1b >> 4) & 0x07;
    vdg->css = (pia1b & 0x08) != 0;
}

/* Render one scanline of text/semigraphics mode (A/G=0) */
static void render_alpha_scanline(VDG *vdg, int line, uint16_t display_addr)
{
    /*
     * Text mode: 32 columns × 16 rows. Each character cell is 8×12 pixels.
     * 192 active lines / 12 rows per char = 16 rows.
     * 256 active pixels / 8 pixels per char = 32 columns.
     */
    int char_row = line / 12;
    int row_in_char = line % 12;
    uint16_t row_addr = display_addr + (uint16_t)(char_row * 32);

    for (int col = 0; col < 32; col++) {
        uint8_t byte = vdg->ram[(row_addr + col) & 0xFFFF];

        if (byte & 0x80) {
            /* Semigraphics 4 mode:
             * Bit 7 = 1 (SG flag)
             * Bits 6-4 = color (3 bits -> 8 colors)
             * Bits 3-0 = 2×2 block pattern:
             *   bit 3 = top-left, bit 2 = top-right
             *   bit 1 = bottom-left, bit 0 = bottom-right
             * Each character cell becomes a 2×2 grid of colored blocks.
             * "Top" = upper 6 pixel rows, "Bottom" = lower 6 pixel rows.
             */
            uint8_t color_idx = (byte >> 4) & 0x07;
            uint32_t fg = sg4_colors[color_idx];
            uint32_t bg = vdg_palette[COL_BLACK];

            bool top_half = (row_in_char < 6);
            bool left_on, right_on;
            if (top_half) {
                left_on  = (byte & 0x08) != 0;
                right_on = (byte & 0x04) != 0;
            } else {
                left_on  = (byte & 0x02) != 0;
                right_on = (byte & 0x01) != 0;
            }

            int px = col * 8;
            for (int x = 0; x < 4; x++)
                vdg->framebuffer[line][px + x] = left_on ? fg : bg;
            for (int x = 4; x < 8; x++)
                vdg->framebuffer[line][px + x] = right_on ? fg : bg;
        } else {
            /* Internal alphanumeric character */
            bool inverse = (byte & 0x40) != 0;
            uint8_t charcode = byte & 0x3F;

            uint8_t font_row = mc6847_font[charcode][row_in_char];

            /* Text colors: CSS=0 -> green on dark green,
             *              CSS=1 -> orange on dark orange */
            uint32_t fg, bg;
            if (vdg->css) {
                fg = vdg_palette[COL_ORANGE];
                bg = vdg_palette[COL_DARK_ORANGE];
            } else {
                fg = vdg_palette[COL_BRIGHT_GREEN];
                bg = vdg_palette[COL_DARK_GREEN];
            }

            if (inverse) {
                uint32_t tmp = fg;
                fg = bg;
                bg = tmp;
            }

            int px = col * 8;
            for (int bit = 7; bit >= 0; bit--) {
                vdg->framebuffer[line][px + (7 - bit)] =
                    (font_row & (1 << bit)) ? fg : bg;
            }
        }
    }
}

/* Render one scanline of a color graphics mode (2 bits per pixel) */
static void render_cg_scanline(VDG *vdg, int line, uint16_t display_addr,
                                int bytes_per_row, int vrepeat, int pixels_per_byte)
{
    int data_row = line / vrepeat;
    uint16_t row_addr = display_addr + (uint16_t)(data_row * bytes_per_row);
    int css = vdg->css ? 1 : 0;

    /* pixels_per_byte is always 4 for CG modes (2 bits per pixel) */
    /* Horizontal scale factor: 256 / (bytes_per_row * pixels_per_byte) */
    int native_width = bytes_per_row * pixels_per_byte;
    int hscale = 256 / native_width;

    int px = 0;
    for (int b = 0; b < bytes_per_row; b++) {
        uint8_t byte = vdg->ram[(row_addr + b) & 0xFFFF];
        for (int p = 0; p < pixels_per_byte; p++) {
            /* Extract 2-bit color from MSB first */
            int shift = (3 - p) * 2;
            uint8_t cidx = (byte >> shift) & 0x03;
            uint32_t color = cg_colors[css][cidx];
            for (int s = 0; s < hscale; s++) {
                if (px < 256)
                    vdg->framebuffer[line][px++] = color;
            }
        }
    }
}

/* Render one scanline of a resolution graphics mode (1 bit per pixel) */
static void render_rg_scanline(VDG *vdg, int line, uint16_t display_addr,
                                int bytes_per_row, int vrepeat)
{
    int data_row = line / vrepeat;
    uint16_t row_addr = display_addr + (uint16_t)(data_row * bytes_per_row);
    int css = vdg->css ? 1 : 0;

    uint32_t fg = rg_fg_colors[css];
    uint32_t bg = vdg_palette[COL_BLACK];

    /* native_width = bytes_per_row * 8 bits */
    int native_width = bytes_per_row * 8;
    int hscale = 256 / native_width;

    int px = 0;
    for (int b = 0; b < bytes_per_row; b++) {
        uint8_t byte = vdg->ram[(row_addr + b) & 0xFFFF];
        for (int bit = 7; bit >= 0; bit--) {
            uint32_t color = (byte & (1 << bit)) ? fg : bg;
            for (int s = 0; s < hscale; s++) {
                if (px < 256)
                    vdg->framebuffer[line][px++] = color;
            }
        }
    }
}

void vdg_render_scanline(VDG *vdg, int line, uint16_t display_addr)
{
    if (line < 0 || line >= VDG_HEIGHT)
        return;

    if (!vdg->ag) {
        /* Alpha / semigraphics mode */
        render_alpha_scanline(vdg, line, display_addr);
        return;
    }

    /* Graphics mode: A/G=1, decode GM2:GM1:GM0 */
    int vrepeat = gm_vrepeat[vdg->gm];

    switch (vdg->gm) {
    case 0: /* CG1: 64x64, 4-color, 2bpp */
        render_cg_scanline(vdg, line, display_addr, 16, vrepeat, 4);
        break;
    case 1: /* RG1: 128x64, 2-color, 1bpp */
        render_rg_scanline(vdg, line, display_addr, 16, vrepeat);
        break;
    case 2: /* CG2: 128x64, 4-color, 2bpp */
        render_cg_scanline(vdg, line, display_addr, 32, vrepeat, 4);
        break;
    case 3: /* RG2: 128x96, 2-color, 1bpp */
        render_rg_scanline(vdg, line, display_addr, 16, vrepeat);
        break;
    case 4: /* CG3: 128x96, 4-color, 2bpp */
        render_cg_scanline(vdg, line, display_addr, 32, vrepeat, 4);
        break;
    case 5: /* RG3: 128x192, 2-color, 1bpp */
        render_rg_scanline(vdg, line, display_addr, 16, vrepeat);
        break;
    case 6: /* CG6: 128x192, 4-color, 2bpp */
        render_cg_scanline(vdg, line, display_addr, 32, vrepeat, 4);
        break;
    case 7: /* RG6: 256x192, 2-color, 1bpp */
        render_rg_scanline(vdg, line, display_addr, 32, vrepeat);
        break;
    }

}

void vdg_render_frame(VDG *vdg, uint16_t display_addr)
{
    for (int line = 0; line < VDG_HEIGHT; line++)
        vdg_render_scanline(vdg, line, display_addr);
}

bool vdg_tick_scanline(VDG *vdg)
{
    vdg->scanline++;
    if (vdg->scanline >= VDG_SCANLINES_PER_FRAME)
        vdg->scanline = 0;

    bool prev_fs = vdg->fs;
    vdg->fs = (vdg->scanline >= VDG_ACTIVE_LINES);

    /* Return true on transition into vblank (FS rising edge) */
    return vdg->fs && !prev_fs;
}

const uint8_t *vdg_get_font(void)
{
    return &mc6847_font[0][0];
}
