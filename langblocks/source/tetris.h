/* -------------------------------------------------------------------------
 * tetris.h -- the reward mini-game.
 *
 * The game is one self-contained module: a 10x20 board, the seven tetrominoes
 * with four rotations each, gravity, a lock delay, line clearing and scoring.
 * It never touches the save file or the clock itself -- main.c owns the banked
 * time and simply stops calling tetrisStep() when the time is up.
 *
 * Nothing here allocates memory, and every board access goes through bounds
 * checks, so a bad rotate near the wall cannot corrupt memory on real hardware.
 * ------------------------------------------------------------------------- */
#ifndef TETRIS_H
#define TETRIS_H

#include "gfx.h"
#include "platform.h"

#define TETRIS_COLS 10
#define TETRIS_ROWS 20

typedef enum {
    TS_PLAYING = 0,
    TS_PAUSED = 1,
    TS_OVER = 2
} TetrisState;

typedef enum {
    TO_TOP_OUT = 0,   /* a new piece could not enter the board */
    TO_TIME_UP = 1    /* the banked time ran out              */
} TetrisOverReason;

typedef struct {
    u8 board[TETRIS_ROWS][TETRIS_COLS];   /* 0 = empty, 1..7 = piece colour */
    int px, py;                           /* top-left of the 4x4 piece box  */
    int type, rot, next;
    u32 score;
    int lines;
    int level;
    TetrisState state;
    int overReason;
    int gravityTimer;
    int lockTimer;
    int lockResets;
} Tetris;

/* Touch button ids used by tetrisHitTest(). */
enum {
    TB_ROTATE = 0,
    TB_LEFT,
    TB_RIGHT,
    TB_DOWN,
    TB_DROP,
    TB_PAUSE,
    TB_COUNT
};

void tetrisSeed(u32 seed);

/* Start a brand new game (clears the board, resets score and level). */
void tetrisNew(Tetris *t);

/* Advance one frame: gravity, lock delay, line clears. */
void tetrisStep(Tetris *t);

void tetrisMove(Tetris *t, int dx);
void tetrisRotate(Tetris *t);
void tetrisSoftDrop(Tetris *t);
void tetrisHardDrop(Tetris *t);
void tetrisTogglePause(Tetris *t);

/* Draw the board and the touch controls on the bottom screen, plus the score
 * panel on the top screen.  `timeLeftMs` is whatever main.c has left in the
 * time bank. */
void tetrisDraw(const Tetris *t, Surface *bottom, Surface *top, u32 timeLeftMs);

/* Draw the "game over" summary on both screens. */
void tetrisDrawGameOver(const Tetris *t, Surface *bottom, Surface *top);

/* Which touch button is at (x,y)?  Returns -1 when the touch misses them. */
int tetrisHitTest(int x, int y);

#endif /* TETRIS_H */
