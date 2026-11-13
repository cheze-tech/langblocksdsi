/* -------------------------------------------------------------------------
 * save.h -- persistent profile stored on the SD card.
 *
 * The file is a fixed-size struct with a magic number, a version and a
 * checksum, so a half-written or foreign file is detected and replaced with
 * defaults instead of loading garbage.  Writes go to a temporary file first
 * and are then renamed over the real one, which is the usual trick to avoid
 * corrupting a save when the console is switched off mid-write.
 *
 * Version 2 replaced the old "Tetris time" with a shared game-time bank and
 * added the chess high score and the drill-selection mask.
 * ------------------------------------------------------------------------- */
#ifndef SAVE_H
#define SAVE_H

#include "data.h"
#include "platform.h"
#include "quiz.h"

#define SAVE_MAGIC      0x4C424B53u   /* 'S' 'B' 'K' 'L' -> "LBKS" */
#define SAVE_VERSION    2
#define DAILY_GOAL      15            /* correct answers per session */
#define MAX_BANK_MS     (30u * 60u * 1000u)   /* 30 minutes of game time */

typedef struct {
    u32 magic;
    u16 version;
    u16 checksum;

    u32 xp;
    u32 gameMs;          /* banked game time, in ms, for Tetris and Chess */
    u32 drillMask;       /* QK_MASK(...) bits enabled in the setup screen */
    u32 streak;          /* consecutive sessions with the goal met */
    u32 bestStreak;
    u32 dailyGoal;
    u32 dailyCorrect;
    u32 dailyDone;       /* 1 once the goal was met this session */
    u32 totalAsked;
    u32 totalCorrect;
    u32 bestTetrisScore;
    u32 bestLines;
    u32 tetrisGames;
    u32 bestChessScore;
    u32 chessGames;
    u32 lang;            /* Lang last selected */
} SaveData;

extern SaveData save;

/* Initialise the profile.  With SAVE_TO_SD disabled in save.c this only
 * applies defaults; the profile then lives in RAM for the session. */
void saveInit(void);

/* 1 if a writable device was found.  The menu shows this state to the user. */
int saveAvailable(void);

/* "sd:/langblocks.sav" or an empty string. */
const char *savePath(void);

/* Reset everything to a fresh profile (used when no valid file exists). */
void saveDefaults(void);

/* Write the profile now (temporary file + rename). */
void saveNow(void);

/* Called once at boot: applies the "goal met last time?" streak rule and
 * clears the per-session counters. */
void saveNewSession(void);

#endif /* SAVE_H */
