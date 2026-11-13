/* -------------------------------------------------------------------------
 * gfx.h -- tiny software renderer for the two 256x192 screens.
 *
 * Why a software renderer?  The DSi has no font or UI toolkit built in, and
 * shipping bitmap assets would add a grit/asset pipeline to the build.  Both
 * screens are instead put into 16bpp bitmap mode (MODE_5_2D) and every pixel
 * is written by hand.  That keeps the whole app in plain C with no external
 * data files, while still giving us colour, panels and a readable font.
 *
 * Everything is drawn into a `Surface` (a raw framebuffer pointer).  Writes
 * outside 0..255 x 0..191 are clipped, so a layout bug can never scribble
 * over VRAM that belongs to something else.
 * ------------------------------------------------------------------------- */
#ifndef GFX_H
#define GFX_H

#include "platform.h"

#define SCREEN_W 256
#define SCREEN_H 192

/* 15-bit BGR555 colour, the native format of the DS LCDs.  libnds already
 * defines RGB15, so only define it for the host build. */
#ifndef RGB15
#define RGB15(r, g, b) ((u16)(((b) << 10) | ((g) << 5) | (r)))
#endif

/* Palette used by the whole app ------------------------------------------ */
#define COL_BG        RGB15(2, 3, 7)      /* deep navy background        */
#define COL_PANEL     RGB15(4, 6, 12)     /* card background             */
#define COL_PANEL_HI  RGB15(7, 10, 18)    /* raised / hovered card       */
#define COL_BORDER    RGB15(11, 16, 26)   /* card outline                */
#define COL_TEXT      RGB15(29, 30, 31)   /* near-white body text        */
#define COL_TEXT_DIM  RGB15(13, 16, 21)   /* secondary text              */
#define COL_GOLD      RGB15(31, 24, 6)    /* XP / streak accent          */
#define COL_GREEN     RGB15(6, 28, 14)    /* correct                     */
#define COL_DARKGREEN RGB15(3, 14, 8)     /* correct panel fill          */
#define COL_RED       RGB15(31, 8, 10)    /* wrong                       */
#define COL_DARKRED   RGB15(15, 4, 6)     /* wrong panel fill            */
#define COL_BLUE      RGB15(8, 20, 31)    /* info accent                 */
#define COL_CYAN      RGB15(6, 26, 28)    /* timer accent                */

/* One framebuffer.  `fb` points either at VRAM (main/sub engine) or, when
 * running on the host, at a scratch array. */
typedef struct {
    u16 *fb;
} Surface;

extern Surface gfxTop;      /* main engine   (the upper screen) */
extern Surface gfxBottom;   /* sub engine    (the touch screen) */

/* Set both screens to 16bpp bitmap mode and point the surfaces at VRAM. */
void gfxInit(void);

/* Wait for the vertical blank so the frame we just drew is shown whole.
 * Drawing happens straight into VRAM, so this is also the frame pacer. */
void gfxPresent(void);

/* ---------------------------------------------------------------- drawing */
void surfClear(Surface *s, u16 color);
void surfRect(Surface *s, int x, int y, int w, int h, u16 color);
void surfFrame(Surface *s, int x, int y, int w, int h, u16 color, int thickness);
void surfHLine(Surface *s, int x, int y, int w, u16 color);
void surfVLine(Surface *s, int x, int y, int h, u16 color);
void surfPixel(Surface *s, int x, int y, u16 color);

/* A filled card with a border and a 1px top highlight -- the app's standard
 * "button" look.  `pressed` swaps the colours so touch feedback is obvious. */
void surfCard(Surface *s, int x, int y, int w, int h, u16 fill, u16 border,
              int pressed);

/* ------------------------------------------------------------------- text */
/* Monospaced 5x7 glyphs, 6px advance, 9px line height (see fonts/font5x7.h). */
void surfText(Surface *s, int x, int y, const char *text, u16 color, int scale);
void surfTextN(Surface *s, int x, int y, const char *text, int bytes,
               u16 color, int scale);
int  textWidth(const char *text, int scale);
int  textWidthN(const char *text, int bytes, int scale);
int  textLineHeight(int scale);
void surfTextCentered(Surface *s, int cx, int y, const char *text, u16 color,
                      int scale);

/* Word-wrapped text.  Returns the y coordinate just below the last line, so
 * callers can stack blocks of copy. */
int  surfTextWrapped(Surface *s, int x, int y, int maxWidth, const char *text,
                     u16 color, int scale);
int  wrappedHeight(int maxWidth, const char *text, int scale);

/* Draw a single UTF-8 code point at the pen position (used by the helpers). */
int  utf8Decode(const char *text, u16 *codepoint);

#endif /* GFX_H */
