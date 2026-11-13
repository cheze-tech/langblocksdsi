/* -------------------------------------------------------------------------
 * gfx.c -- see gfx.h.  All drawing is plain C on top of a 16bpp framebuffer.
 *
 * Coordinate system: (0,0) is the top-left of each 256x192 screen.  Every
 * primitive clips to the screen, so nothing can ever write outside VRAM.
 * ------------------------------------------------------------------------- */
#include "gfx.h"
#include "font5x7.h"

Surface gfxTop;
Surface gfxBottom;

#ifndef HOST_TEST
/* Only used when this file is compiled for the PC (see tools/). */
#else
static u16 hostTop[SCREEN_W * SCREEN_H];
static u16 hostBottom[SCREEN_W * SCREEN_H];
#endif

void gfxInit(void)
{
#ifndef HOST_TEST
    /* Bank A feeds the main engine's background, bank C the sub engine's.  In
     * 16bpp bitmap mode a full screen needs 96 KiB, which is exactly one bank,
     * so a single mapping is enough. */
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    /* Mode 5 = 256x192 16-bit bitmap on BG2, for both engines. */
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);

    gfxTop.fb = BG_GFX;
    gfxBottom.fb = BG_GFX_SUB;
#else
    gfxTop.fb = hostTop;
    gfxBottom.fb = hostBottom;
#endif
}

void gfxPresent(void)
{
#ifndef HOST_TEST
    /* Drawing goes straight to VRAM.  Waiting for vblank here paces the whole
     * app at 60 fps and makes sure the frame we just finished is on screen
     * before we start overwriting it. */
    swiWaitForVBlank();
#endif
}

/* ------------------------------------------------------------------ pixels */

void surfPixel(Surface *s, int x, int y, u16 color)
{
    if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H) return;
    s->fb[y * SCREEN_W + x] = color;
}

void surfHLine(Surface *s, int x, int y, int w, u16 color)
{
    if ((unsigned)y >= SCREEN_H) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (w <= 0) return;
    u16 *row = s->fb + y * SCREEN_W + x;
    while (w-- > 0) *row++ = color;
}

void surfVLine(Surface *s, int x, int y, int h, u16 color)
{
    if ((unsigned)x >= SCREEN_W) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (h <= 0) return;
    u16 *p = s->fb + y * SCREEN_W + x;
    while (h-- > 0) { *p = color; p += SCREEN_W; }
}

void surfRect(Surface *s, int x, int y, int w, int h, u16 color)
{
    if (w <= 0 || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    for (int i = 0; i < h; i++) surfHLine(s, x, y + i, w, color);
}

void surfFrame(Surface *s, int x, int y, int w, int h, u16 color, int thickness)
{
    if (thickness <= 0) thickness = 1;
    surfRect(s, x, y, w, thickness, color);                    /* top    */
    surfRect(s, x, y + h - thickness, w, thickness, color);    /* bottom */
    surfRect(s, x, y, thickness, h, color);                    /* left   */
    surfRect(s, x + w - thickness, y, thickness, h, color);    /* right  */
}

void surfClear(Surface *s, u16 color)
{
    surfRect(s, 0, 0, SCREEN_W, SCREEN_H, color);
}

void surfCard(Surface *s, int x, int y, int w, int h, u16 fill, u16 border,
              int pressed)
{
    surfRect(s, x, y, w, h, pressed ? border : fill);
    surfFrame(s, x, y, w, h, border, 1);
    if (!pressed) {
        /* A lighter 1px line along the top edge fakes a subtle bevel. */
        surfHLine(s, x + 1, y + 1, w - 2, COL_PANEL_HI);
    }
}

/* -------------------------------------------------------------------- text */

int utf8Decode(const char *text, u16 *codepoint)
{
    const unsigned char *p = (const unsigned char *)text;
    if (p[0] < 0x80) {
        *codepoint = p[0];
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0) {
        *codepoint = (u16)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
        return 2;
    }
    if ((p[0] & 0xF0) == 0xE0) {
        *codepoint = (u16)(((p[0] & 0x0F) << 12) |
                           ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
        return 3;
    }
    *codepoint = '?';
    return 1;
}

static const u8 *glyphFor(u16 cp)
{
    if (cp >= 0x20 && cp <= 0x7E) return fontAscii[cp - 0x20];
    for (unsigned int i = 0; i < fontExtraCount; i++) {
        if (fontExtra[i].cp == cp) return fontExtra[i].rows;
    }
    /* Unknown character (an emoji, say) shows as '?' rather than nothing. */
    return fontAscii['?' - 0x20];
}

int textLineHeight(int scale)
{
    return FONT_LINE * scale;
}

int textWidthN(const char *text, int bytes, int scale)
{
    if (bytes <= 0) return 0;
    int count = 0;
    const char *p = text;
    const char *end = text + bytes;
    while (p < end && *p) {
        u16 cp;
        p += utf8Decode(p, &cp);
        count++;
    }
    return count * FONT_ADVANCE * scale - scale;   /* no gap after last glyph */
}

int textWidth(const char *text, int scale)
{
    int n = 0;
    while (text[n]) n++;
    return textWidthN(text, n, scale);
}

void surfTextN(Surface *s, int x, int y, const char *text, int bytes,
               u16 color, int scale)
{
    const char *p = text;
    const char *end = text + bytes;

    while (p < end && *p) {
        u16 cp;
        p += utf8Decode(p, &cp);
        const u8 *rows = glyphFor(cp);

        for (int ry = 0; ry < FONT_H; ry++) {
            u8 bits = rows[ry];
            if (!bits) continue;
            for (int rx = 0; rx < FONT_W; rx++) {
                if (!(bits & (1u << (FONT_W - 1 - rx)))) continue;
                int px = x + rx * scale;
                int py = y + ry * scale;
                for (int sy = 0; sy < scale; sy++) {
                    surfHLine(s, px, py + sy, scale, color);
                }
            }
        }
        x += FONT_ADVANCE * scale;
    }
}

void surfText(Surface *s, int x, int y, const char *text, u16 color, int scale)
{
    int n = 0;
    while (text[n]) n++;
    surfTextN(s, x, y, text, n, color, scale);
}

void surfTextCentered(Surface *s, int cx, int y, const char *text, u16 color,
                      int scale)
{
    surfText(s, cx - textWidth(text, scale) / 2, y, text, color, scale);
}

/* Shared word-wrap walker.  When `s` is NULL it only measures. */
static int wrapRun(Surface *s, int x, int y, int maxWidth, const char *text,
                   u16 color, int scale, int draw)
{
    const int lineH = textLineHeight(scale);
    const int x0 = x;
    int cy = y;
    const char *p = text;

    while (*p) {
        if (*p == '\n') {              /* hard line break in the source text */
            cy += lineH;
            x = x0;
            p++;
            continue;
        }
        if (*p == ' ') {               /* spaces only separate words        */
            p++;
            continue;
        }

        const char *word = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        int bytes = (int)(p - word);
        int wordW = textWidthN(word, bytes, scale);

        if (x > x0 && x + wordW > x0 + maxWidth) {
            cy += lineH;
            x = x0;
        }
        if (draw) surfTextN(s, x, cy, word, bytes, color, scale);
        x += wordW + FONT_ADVANCE * scale;
    }
    return cy + lineH;
}

int surfTextWrapped(Surface *s, int x, int y, int maxWidth, const char *text,
                    u16 color, int scale)
{
    return wrapRun(s, x, y, maxWidth, text, color, scale, 1);
}

int wrappedHeight(int maxWidth, const char *text, int scale)
{
    /* Measure relative to y=0 so the return value is a height. */
    return wrapRun(NULL, 0, 0, maxWidth, text, 0, scale, 0);
}
