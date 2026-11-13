/* -------------------------------------------------------------------------
 * tetris.c -- a small, careful Tetris implementation for the touch screen.
 *
 * Board model
 * -----------
 *   board[row][col], row 0 at the top, 20 rows, 10 columns.
 *   A falling piece lives in a 4x4 box at (px, py); py may be negative while
 *   the piece is still entering the playfield above row 0.
 *
 * Collision rules
 * ---------------
 *   * columns outside 0..9 collide
 *   * rows >= 20 collide
 *   * a cell above the board (py+y < 0) never collides
 *   That last rule is what lets a piece spawn half off-screen without reading
 *   out of bounds, which is the usual source of crashes in homebrew Tetris.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>

#include "tetris.h"

/* Screen layout ---------------------------------------------------------- */
#define CELL      8
#define BOARD_X   6
#define BOARD_Y   20

#define PANEL_X   92
#define NEXT_X    170

/* Touch button rectangles (x, y, w, h) -------------------------------- */
static const short btnRect[TB_COUNT][4] = {
    { PANEL_X,  86,  72, 28 },   /* rotate */
    { NEXT_X,   86,  80, 28 },   /* hard drop */
    { PANEL_X, 120,  36, 28 },   /* left */
    { PANEL_X + 38, 120, 34, 28 },/* right */
    { PANEL_X, 152,  72, 26 },   /* soft drop */
    { NEXT_X,  152,  80, 26 },   /* pause */
};

/* Piece definitions: four rotations of a 4x4 bit grid per tetromino.  A set
 * bit means "this cell of the 4x4 box is filled". */
static const u16 tetShapes[7][4] = {
    /* I */ { 0x0F00, 0x2222, 0x00F0, 0x4444 },
    /* O */ { 0x6600, 0x6600, 0x6600, 0x6600 },
    /* T */ { 0x4E00, 0x4640, 0x0E40, 0x4C40 },
    /* S */ { 0x6C00, 0x4620, 0x06C0, 0x8C40 },
    /* Z */ { 0xC600, 0x2640, 0x0C60, 0x4C80 },
    /* J */ { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
    /* L */ { 0x2E00, 0x4460, 0x0E80, 0xC440 },
};

static const u16 tetColors[7] = {
    RGB15(6, 26, 28),    /* I cyan   */
    RGB15(31, 24, 6),    /* O gold   */
    RGB15(18, 8, 28),    /* T purple */
    RGB15(6, 28, 12),    /* S green  */
    RGB15(28, 8, 10),    /* Z red    */
    RGB15(8, 18, 31),    /* J blue   */
    RGB15(31, 14, 4),    /* L orange */
};

/* ---------------------------------------------------------------- random */

static u32 tetRng = 0x2545F491u;

void tetrisSeed(u32 seed)
{
    tetRng = seed ? seed : 0x2545F491u;
}

static u32 tetRand(void)
{
    tetRng ^= tetRng << 13;
    tetRng ^= tetRng >> 17;
    tetRng ^= tetRng << 5;
    return tetRng;
}

/* A 7-bag randomiser: every piece appears once before any repeats, which is
 * what modern Tetris does and what players expect. */
static u8 bag[7];
static int bagPos = 7;

static int nextPiece(void)
{
    if (bagPos >= 7) {
        for (int i = 0; i < 7; i++) bag[i] = (u8)i;
        for (int i = 6; i > 0; i--) {
            int j = (int)(tetRand() % (u32)(i + 1));
            u8 tmp = bag[i];
            bag[i] = bag[j];
            bag[j] = tmp;
        }
        bagPos = 0;
    }
    return bag[bagPos++];
}

/* ------------------------------------------------------------ board maths */

static int shapeCell(int type, int rot, int x, int y)
{
    if (type < 0 || type > 6) return 0;
    u16 bits = tetShapes[type][rot & 3];
    int shift = (3 - y) * 4 + (3 - x);
    return (bits >> shift) & 1;
}

/* Does this piece position overlap the walls, the floor or a settled block? */
static int collides(const Tetris *t, int type, int rot, int px, int py)
{
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (!shapeCell(type, rot, x, y)) continue;

            int bx = px + x;
            int by = py + y;

            if (bx < 0 || bx >= TETRIS_COLS) return 1;
            if (by >= TETRIS_ROWS) return 1;
            if (by >= 0 && t->board[by][bx]) return 1;
        }
    }
    return 0;
}

static void spawnPiece(Tetris *t)
{
    t->type = t->next;
    t->next = nextPiece();
    t->rot = 0;
    t->px = 3;                      /* 4x4 box at column 3 centres 3..6 */
    t->py = 0;
    t->gravityTimer = 0;
    t->lockTimer = 0;
    t->lockResets = 0;

    if (collides(t, t->type, t->rot, t->px, t->py)) {
        /* No room at the top: the stack reached the ceiling. */
        t->state = TS_OVER;
        t->overReason = TO_TOP_OUT;
    }
}

/* Copy the falling piece into the board grid. */
static void lockPiece(Tetris *t)
{
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (!shapeCell(t->type, t->rot, x, y)) continue;
            int bx = t->px + x;
            int by = t->py + y;
            if (bx < 0 || bx >= TETRIS_COLS) continue;
            if (by < 0 || by >= TETRIS_ROWS) continue;
            t->board[by][bx] = (u8)(t->type + 1);
        }
    }
}

static void clearLines(Tetris *t)
{
    int cleared = 0;

    for (int y = TETRIS_ROWS - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < TETRIS_COLS; x++) {
            if (!t->board[y][x]) { full = 0; break; }
        }
        if (!full) continue;

        /* Move every row above this one down by one. */
        for (int row = y; row > 0; row--) {
            memcpy(t->board[row], t->board[row - 1],
                   sizeof(t->board[row]));
        }
        memset(t->board[0], 0, sizeof(t->board[0]));
        cleared++;
        y++;                        /* re-check the row that just moved down */
    }

    if (cleared) {
        /* 1/2/3/4 lines -> 100/300/500/800, scaled by the level. */
        static const u32 points[5] = { 0, 100, 300, 500, 800 };
        t->score += points[cleared] * (u32)t->level;
        t->lines += cleared;
        t->level = 1 + t->lines / 10;
    }
}

static void finishPiece(Tetris *t)
{
    lockPiece(t);
    clearLines(t);
    spawnPiece(t);
}

/* -------------------------------------------------------------- controls */

void tetrisMove(Tetris *t, int dx)
{
    if (t->state != TS_PLAYING) return;
    if (!collides(t, t->type, t->rot, t->px + dx, t->py)) {
        t->px += dx;
        /* Moving a resting piece buys a little more time, but not forever. */
        if (collides(t, t->type, t->rot, t->px, t->py + 1) && t->lockResets < 10) {
            t->lockTimer = 0;
            t->lockResets++;
        }
    }
}

void tetrisRotate(Tetris *t)
{
    if (t->state != TS_PLAYING) return;

    int rot = (t->rot + 1) & 3;
    /* Simple wall kicks: straight, then one and two cells to either side. */
    static const int kicks[5] = { 0, -1, 1, -2, 2 };
    for (int i = 0; i < 5; i++) {
        if (!collides(t, t->type, rot, t->px + kicks[i], t->py)) {
            t->rot = rot;
            t->px += kicks[i];
            if (collides(t, t->type, t->rot, t->px, t->py + 1) &&
                t->lockResets < 10) {
                t->lockTimer = 0;
                t->lockResets++;
            }
            return;
        }
    }
}

void tetrisSoftDrop(Tetris *t)
{
    if (t->state != TS_PLAYING) return;
    if (!collides(t, t->type, t->rot, t->px, t->py + 1)) {
        t->py++;
        t->score += 1;
        t->gravityTimer = 0;
        t->lockTimer = 0;
    }
}

void tetrisHardDrop(Tetris *t)
{
    if (t->state != TS_PLAYING) return;
    while (!collides(t, t->type, t->rot, t->px, t->py + 1)) {
        t->py++;
        t->score += 2;
    }
    finishPiece(t);
}

void tetrisTogglePause(Tetris *t)
{
    if (t->state == TS_PLAYING)       t->state = TS_PAUSED;
    else if (t->state == TS_PAUSED)   t->state = TS_PLAYING;
}

/* ---------------------------------------------------------------- update */

void tetrisStep(Tetris *t)
{
    if (t->state != TS_PLAYING) return;

    /* Gravity gets faster with the level: 48 frames per row at level 1 down to
     * 6 frames at level 11 and above. */
    int interval = 48 - (t->level - 1) * 4;
    if (interval < 6) interval = 6;

    if (++t->gravityTimer >= interval) {
        t->gravityTimer = 0;
        if (!collides(t, t->type, t->rot, t->px, t->py + 1)) {
            t->py++;
        }
    }

    /* Lock delay: once the piece is resting the player gets 15 frames to slide
     * it into place.  Without this the game feels unfair on the touch screen. */
    if (collides(t, t->type, t->rot, t->px, t->py + 1)) {
        if (++t->lockTimer >= 15) finishPiece(t);
    } else {
        t->lockTimer = 0;
    }
}

void tetrisNew(Tetris *t)
{
    memset(t, 0, sizeof(*t));
    t->level = 1;
    t->state = TS_PLAYING;
    t->next = nextPiece();
    spawnPiece(t);
}

/* ---------------------------------------------------------------- drawing */

static u16 lighten(u16 c)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r += (31 - r) / 2;
    g += (31 - g) / 2;
    b += (31 - b) / 2;
    return RGB15(r, g, b);
}

static void drawBlock(Surface *s, int x, int y, int cell, u16 color)
{
    surfRect(s, x, y, cell, cell, color);
    surfRect(s, x + 1, y + 1, cell - 2, cell - 2, lighten(color));
    surfRect(s, x + 2, y + 2, cell - 4, cell - 4, color);
}

static void formatTime(u32 ms, char *buf, int size)
{
    u32 total = ms / 1000;
    if (total > 99 * 60) total = 99 * 60;
    snprintf(buf, (size_t)size, "%02u:%02u", (unsigned)(total / 60),
             (unsigned)(total % 60));
}

static void drawBoard(const Tetris *t, Surface *s)
{
    /* Playfield frame + background */
    surfRect(s, BOARD_X - 2, BOARD_Y - 2, TETRIS_COLS * CELL + 4,
             TETRIS_ROWS * CELL + 4, COL_BORDER);
    surfRect(s, BOARD_X, BOARD_Y, TETRIS_COLS * CELL, TETRIS_ROWS * CELL,
             COL_PANEL);

    /* Faint grid so empty cells still read as cells. */
    for (int y = 0; y < TETRIS_ROWS; y++) {
        for (int x = 0; x < TETRIS_COLS; x++) {
            surfPixel(s, BOARD_X + x * CELL, BOARD_Y + y * CELL, COL_BORDER);
        }
    }

    /* Settled blocks */
    for (int y = 0; y < TETRIS_ROWS; y++) {
        for (int x = 0; x < TETRIS_COLS; x++) {
            u8 v = t->board[y][x];
            if (!v) continue;
            drawBlock(s, BOARD_X + x * CELL, BOARD_Y + y * CELL, CELL,
                      tetColors[v - 1]);
        }
    }

    if (t->state == TS_OVER) return;

    /* Ghost piece: where the current piece would land.  Drawn as an outline so
     * it never hides the real piece. */
    int ghostY = t->py;
    while (!collides(t, t->type, t->rot, t->px, ghostY + 1)) ghostY++;

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (!shapeCell(t->type, t->rot, x, y)) continue;
            int by = ghostY + y;
            if (by < 0 || by >= TETRIS_ROWS) continue;
            if (t->board[by][t->px + x]) continue;
            surfFrame(s, BOARD_X + (t->px + x) * CELL,
                      BOARD_Y + by * CELL, CELL, CELL, COL_BORDER, 1);
        }
    }

    /* The falling piece itself */
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (!shapeCell(t->type, t->rot, x, y)) continue;
            int by = t->py + y;
            if (by < 0 || by >= TETRIS_ROWS) continue;
            drawBlock(s, BOARD_X + (t->px + x) * CELL, BOARD_Y + by * CELL,
                      CELL, tetColors[t->type]);
        }
    }
}

static void drawNextBox(const Tetris *t, Surface *s)
{
    surfCard(s, NEXT_X, 20, 80, 56, COL_PANEL, COL_BORDER, 0);
    surfText(s, NEXT_X + 6, 24, "NEXT", COL_TEXT_DIM, 1);

    /* Centre the preview inside the box using a 9px cell. */
    int minX = 4, maxX = -1, minY = 4, maxY = -1;
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (!shapeCell(t->next, 0, x, y)) continue;
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
    }
    int cellsW = maxX - minX + 1;
    int cellsH = maxY - minY + 1;
    int cell = 9;
    int ox = NEXT_X + (80 - cellsW * cell) / 2;
    int oy = 38 + (34 - cellsH * cell) / 2;

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (!shapeCell(t->next, 0, x, y)) continue;
            drawBlock(s, ox + (x - minX) * cell, oy + (y - minY) * cell, cell,
                      tetColors[t->next]);
        }
    }
}

void tetrisDraw(const Tetris *t, Surface *bottom, Surface *top, u32 timeLeftMs)
{
    char buf[24];

    /* ---------------- bottom screen: board + touch controls -------------- */
    surfClear(bottom, COL_BG);
    drawBoard(t, bottom);
    drawNextBox(t, bottom);

    formatTime(timeLeftMs, buf, sizeof(buf));
    surfText(bottom, PANEL_X, 22, "TIME", COL_TEXT_DIM, 1);
    surfText(bottom, PANEL_X, 34, buf, timeLeftMs < 10000 ? COL_RED : COL_CYAN, 2);

    surfCard(bottom, btnRect[TB_ROTATE][0], btnRect[TB_ROTATE][1],
             btnRect[TB_ROTATE][2], btnRect[TB_ROTATE][3], COL_PANEL,
             COL_BORDER, 0);
    surfTextCentered(bottom, btnRect[TB_ROTATE][0] + 36,
                     btnRect[TB_ROTATE][1] + 9, "ROTATE", COL_TEXT, 1);

    surfCard(bottom, btnRect[TB_DROP][0], btnRect[TB_DROP][1],
             btnRect[TB_DROP][2], btnRect[TB_DROP][3], COL_PANEL,
             COL_BORDER, 0);
    surfTextCentered(bottom, btnRect[TB_DROP][0] + 40,
                     btnRect[TB_DROP][1] + 9, "DROP", COL_GOLD, 1);

    surfCard(bottom, btnRect[TB_LEFT][0], btnRect[TB_LEFT][1],
             btnRect[TB_LEFT][2], btnRect[TB_LEFT][3], COL_PANEL,
             COL_BORDER, 0);
    surfTextCentered(bottom, btnRect[TB_LEFT][0] + 18,
                     btnRect[TB_LEFT][1] + 8, "<", COL_TEXT, 2);

    surfCard(bottom, btnRect[TB_RIGHT][0], btnRect[TB_RIGHT][1],
             btnRect[TB_RIGHT][2], btnRect[TB_RIGHT][3], COL_PANEL,
             COL_BORDER, 0);
    surfTextCentered(bottom, btnRect[TB_RIGHT][0] + 17,
                     btnRect[TB_RIGHT][1] + 8, ">", COL_TEXT, 2);

    surfCard(bottom, btnRect[TB_DOWN][0], btnRect[TB_DOWN][1],
             btnRect[TB_DOWN][2], btnRect[TB_DOWN][3], COL_PANEL,
             COL_BORDER, 0);
    surfTextCentered(bottom, btnRect[TB_DOWN][0] + 36,
                     btnRect[TB_DOWN][1] + 9, "DOWN", COL_TEXT, 1);

    surfCard(bottom, btnRect[TB_PAUSE][0], btnRect[TB_PAUSE][1],
             btnRect[TB_PAUSE][2], btnRect[TB_PAUSE][3], COL_PANEL,
             COL_BORDER, 0);
    surfTextCentered(bottom, btnRect[TB_PAUSE][0] + 40,
                     btnRect[TB_PAUSE][1] + 9,
                     t->state == TS_PAUSED ? "RESUME" : "PAUSE", COL_TEXT, 1);

    /* -------------------------- top screen: score ------------------------ */
    surfClear(top, COL_BG);
    surfRect(top, 0, 0, SCREEN_W, 18, COL_PANEL);
    surfText(top, 6, 5, "TETRIS", COL_GOLD, 1);
    surfText(top, 190, 5, "REWARD MODE", COL_TEXT_DIM, 1);

    formatTime(timeLeftMs, buf, sizeof(buf));
    surfText(top, 8, 24, "TIME LEFT", COL_TEXT_DIM, 1);
    surfText(top, 8, 34, buf, timeLeftMs < 10000 ? COL_RED : COL_CYAN, 3);

    surfText(top, 8, 66, "SCORE", COL_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)t->score);
    surfText(top, 8, 76, buf, COL_TEXT, 2);

    surfText(top, 140, 66, "LINES", COL_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "%d", t->lines);
    surfText(top, 140, 76, buf, COL_TEXT, 2);

    surfText(top, 200, 66, "LEVEL", COL_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "%d", t->level);
    surfText(top, 200, 76, buf, COL_TEXT, 2);

    surfRect(top, 0, 104, SCREEN_W, 1, COL_BORDER);
    surfText(top, 8, 112, "D-PAD Move   A Rotate   B Drop", COL_TEXT_DIM, 1);
    surfText(top, 8, 124, "START Pause   SELECT Quit", COL_TEXT_DIM, 1);
    surfText(top, 8, 140, "Touch the buttons on the lower screen.", COL_TEXT_DIM, 1);

    if (t->state == TS_PAUSED) {
        surfRect(top, 48, 154, 160, 26, COL_PANEL_HI);
        surfFrame(top, 48, 154, 160, 26, COL_GOLD, 1);
        surfTextCentered(top, 128, 163, "PAUSED", COL_GOLD, 1);
    }
}

void tetrisDrawGameOver(const Tetris *t, Surface *bottom, Surface *top)
{
    char buf[48];

    surfClear(top, COL_BG);
    surfRect(top, 0, 0, SCREEN_W, 22, COL_PANEL);
    surfText(top, 8, 7, "GAME OVER", COL_RED, 1);
    surfText(top, 180, 7, "TETRIS", COL_TEXT_DIM, 1);

    surfTextCentered(top, 128, 40,
                     t->overReason == TO_TIME_UP ? "OUT OF TIME" : "TOP OUT",
                     COL_GOLD, 2);

    surfText(top, 24, 76, "SCORE", COL_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)t->score);
    surfText(top, 24, 88, buf, COL_TEXT, 2);

    surfText(top, 148, 76, "LINES", COL_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "%d", t->lines);
    surfText(top, 148, 88, buf, COL_TEXT, 2);

    surfTextCentered(top, 128, 130, "The game time bank is empty.", COL_TEXT_DIM, 1);
    surfTextCentered(top, 128, 146, "Answer questions to earn more.",
                     COL_TEXT_DIM, 1);

    surfClear(bottom, COL_BG);
    surfCard(bottom, 8, 72, 240, 48, COL_PANEL, COL_BORDER, 0);
    surfTextCentered(bottom, 128, 84, "GAME OVER", COL_RED, 1);
    surfTextCentered(bottom, 128, 100, "TAP TO RETURN", COL_TEXT, 1);
}

/* ------------------------------------------------------------ touch input */

int tetrisHitTest(int x, int y)
{
    for (int i = 0; i < TB_COUNT; i++) {
        if (x >= btnRect[i][0] && x < btnRect[i][0] + btnRect[i][2] &&
            y >= btnRect[i][1] && y < btnRect[i][1] + btnRect[i][3]) {
            return i;
        }
    }
    return -1;
}
