/* -------------------------------------------------------------------------
 * boottest -- the smallest possible libnds v2/calico ROM.
 *
 * No app code, no data tables, no sound, no FAT, no gfx.c: just VRAM setup,
 * one bitmap background per screen and "HELLO WORLD".  If this ROM stays
 * black then the problem is the toolchain/loader, not the game; if it shows
 * text, the game's boot path can be compared against this file.
 * ------------------------------------------------------------------------- */
#include <nds.h>

#define W 256
#define H 192

static u16 *top;
static u16 *bot;

/* 5x7 glyphs for the letters used below. */
static const unsigned char glyphs[7][7] = {
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* H */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* E */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* L */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* O */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }, /* W */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* R */
    { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, /* D */
};

static int glyphIndex(char c)
{
    switch (c) {
    case 'H': return 0;
    case 'E': return 1;
    case 'L': return 2;
    case 'O': return 3;
    case 'W': return 4;
    case 'R': return 5;
    case 'D': return 6;
    default:  return -1;      /* anything else (including space) is blank */
    }
}

static void fill(u16 *s, u16 color)
{
    for (int i = 0; i < W * H; i++) s[i] = color;
}

static void drawText(u16 *s, int x, int y, const char *text, int scale, u16 color)
{
    for (; *text; text++, x += 6 * scale) {
        int g = glyphIndex(*text);
        if (g < 0) continue;
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (!(glyphs[g][row] & (1u << (4 - col)))) continue;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        int px = x + col * scale + dx;
                        int py = y + row * scale + dy;
                        if (px >= 0 && px < W && py >= 0 && py < H)
                            s[py * W + px] = color;
                    }
                }
            }
        }
    }
}

static int textWidth(const char *text, int scale)
{
    int n = 0;
    for (; *text; text++) n += 6 * scale;
    return n;
}

int main(void)
{
    /* Exactly the official 16-bit bitmap example's sequence. */
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);

    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    int mainBg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int subBg  = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);

    top = (u16 *)bgGetGfxPtr(mainBg);
    bot = (u16 *)bgGetGfxPtr(subBg);

    fill(top, RGB15(2, 3, 8));
    fill(bot, RGB15(2, 7, 4));

    drawText(top, (W - textWidth("HELLO WORLD", 2)) / 2, 76,
             "HELLO WORLD", 2, RGB15(31, 24, 6));
    drawText(top, (W - textWidth("BOOT TEST", 1)) / 2, 116,
             "BOOT TEST", 1, RGB15(20, 24, 28));

    drawText(bot, (W - textWidth("BOOT OK", 2)) / 2, 80,
             "BOOT OK", 2, RGB15(6, 28, 12));

    while (pmMainLoop()) {
        swiWaitForVBlank();
    }

    return 0;
}
