/* -------------------------------------------------------------------------
 * host_stubs.c -- no-op drawing routines so the pure logic modules can be
 * compiled and tested with a desktop compiler (see test_logic.c).
 *
 * Nothing here is ever built into the DS ROM.
 * ------------------------------------------------------------------------- */
#include "gfx.h"

Surface gfxTop;
Surface gfxBottom;

void gfxInit(void) {}
void gfxPresent(void) {}

void surfClear(Surface *s, u16 c) { (void)s; (void)c; }
void surfRect(Surface *s, int x, int y, int w, int h, u16 c)
{
    (void)s; (void)x; (void)y; (void)w; (void)h; (void)c;
}
void surfFrame(Surface *s, int x, int y, int w, int h, u16 c, int t)
{
    (void)s; (void)x; (void)y; (void)w; (void)h; (void)c; (void)t;
}
void surfHLine(Surface *s, int x, int y, int w, u16 c)
{
    (void)s; (void)x; (void)y; (void)w; (void)c;
}
void surfVLine(Surface *s, int x, int y, int h, u16 c)
{
    (void)s; (void)x; (void)y; (void)h; (void)c;
}
void surfPixel(Surface *s, int x, int y, u16 c) { (void)s; (void)x; (void)y; (void)c; }
void surfCard(Surface *s, int x, int y, int w, int h, u16 f, u16 b, int p)
{
    (void)s; (void)x; (void)y; (void)w; (void)h; (void)f; (void)b; (void)p;
}
void surfText(Surface *s, int x, int y, const char *t, u16 c, int sc)
{
    (void)s; (void)x; (void)y; (void)t; (void)c; (void)sc;
}
void surfTextN(Surface *s, int x, int y, const char *t, int n, u16 c, int sc)
{
    (void)s; (void)x; (void)y; (void)t; (void)n; (void)c; (void)sc;
}
int textWidth(const char *t, int sc) { (void)t; return (sc > 0 ? sc : 1) * 6; }
int textWidthN(const char *t, int n, int sc) { (void)t; (void)n; return (sc > 0 ? sc : 1) * 6; }
int textLineHeight(int sc) { return 9 * (sc > 0 ? sc : 1); }
void surfTextCentered(Surface *s, int cx, int y, const char *t, u16 c, int sc)
{
    (void)s; (void)cx; (void)y; (void)t; (void)c; (void)sc;
}
int surfTextWrapped(Surface *s, int x, int y, int w, const char *t, u16 c, int sc)
{
    (void)s; (void)x; (void)w; (void)t; (void)c; (void)sc;
    return y + 9 * (sc > 0 ? sc : 1);
}
int wrappedHeight(int w, const char *t, int sc)
{
    (void)w; (void)t;
    return 9 * (sc > 0 ? sc : 1);
}
int utf8Decode(const char *t, u16 *cp) { *cp = (unsigned char)t[0]; return 1; }
