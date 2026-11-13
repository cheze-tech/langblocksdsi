/* -------------------------------------------------------------------------
 * main.c -- Linguablocks: Spanish/French drills with a game-time bank that
 *           pays for the Tetris and Chess mini-games.
 *
 * Screen split
 * ------------
 *   Top screen    : the 10 second countdown, streak, XP, banked game time,
 *                   the question itself and the grammar explanation.
 *   Bottom screen : the four big touch targets, the menus, the drill setup
 *                   toggles, the Tetris board or the chess board.
 *
 * The app is a simple frame loop at 60 fps:
 *   1. read keys + touch,
 *   2. update the current state,
 *   3. draw,
 *   4. wait for vblank.
 * Everything is table driven, so there is no dynamic memory anywhere.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>

#include "chess.h"
#include "conjugate.h"
#include "data.h"
#include "gfx.h"
#include "quiz.h"
#include "save.h"
#include "sound.h"
#include "tetris.h"

/* ------------------------------------------------------------------ timing */

#define FPS               60
#define QUIZ_SECONDS      10
#define QUIZ_FRAMES       (FPS * QUIZ_SECONDS)
#define FEEDBACK_FRAMES   (FPS * 5)     /* auto-advance after 5 s          */
#define MIN_GAME_MS       5000u         /* need at least 5 s to start      */
#define GOAL_BONUS_MS     30000u        /* +30 s for hitting the daily goal*/
#define CHESS_WIN_BONUS   500           /* points added for checkmate      */
#define CHESS_DRAW_BONUS  150

/* ------------------------------------------------------------------- state */

typedef enum {
    ST_MENU = 0,
    ST_SETUP,
    ST_QUIZ,
    ST_TETRIS,
    ST_CHESS,
    ST_TETRIS_OVER,
    ST_CHESS_OVER
} AppState;

static AppState state = ST_MENU;
static Lang lang = LANG_ES;

static Question question;
static unsigned int quizKinds = QK_MASK_ALL;
static int quizPhase;                 /* 0 = asking, 1 = feedback          */
static int selectedOption;            /* -1 until the player answers       */
static int timeLeftFrames;
static int feedbackFrames;
static int answerWasCorrect;

static int streak;                    /* consecutive correct answers       */
static int cursorOption;              /* D-pad selection while answering   */
static int goalBannerFrames;
static int toastFrames;
static char toastText[64];

static Tetris tetris;
static ChessGame chess;
static u32 frameMsCarry;              /* keeps the ms countdown accurate   */

static u32 frameCounter;
static int screenDirty = 1;           /* force a full redraw                */

/* input snapshot ---------------------------------------------------------- */
static u32 keysPressed, keysHeldNow;
static int touchPressed, touchDownNow, touchX, touchY;
static int prevTouch;
static int holdButton;
static int holdTimer;

/* touch rectangles -------------------------------------------------------- */
static const short optionRect[4][4] = {
    {   4, 26, 120, 72 },
    { 132, 26, 120, 72 },
    {   4, 102, 120, 72 },
    { 132, 102, 120, 72 },
};

/* five main-menu rows */
static const short menuRect[5][4] = {
    {   8, 32, 240, 26 },
    {   8, 62, 240, 26 },
    {   8, 92, 240, 26 },
    {   8, 122, 240, 26 },
    {   8, 152, 240, 26 },
};

/* drill setup: four toggles, then start and back */
static const short toggleRect[4][4] = {
    {   8, 26, 116, 44 },
    { 132, 26, 116, 44 },
    {   8, 76, 116, 44 },
    { 132, 76, 116, 44 },
};
static const short setupStartRect[4] = { 8, 128, 240, 26 };
static const short setupBackRect[4]  = { 8, 160, 240, 24 };

/* --------------------------------------------------------------- utilities */

static void formatClock(u32 ms, char *buf, int size)
{
    u32 total = ms / 1000;
    if (total > 99 * 60) total = 99 * 60;
    snprintf(buf, (size_t)size, "%02u:%02u", (unsigned)(total / 60),
             (unsigned)(total % 60));
}

static int pointInRect(int x, int y, const short r[4])
{
    return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}

static void showToast(const char *text)
{
    snprintf(toastText, sizeof(toastText), "%s", text);
    toastFrames = FPS * 2;
}

/* Clamp the banked time and keep XP in a sane range. */
static void clampProgress(void)
{
    if (save.gameMs > MAX_BANK_MS) save.gameMs = MAX_BANK_MS;
    if (save.xp > 999999u) save.xp = 999999u;
}

/* ------------------------------------------------------------------- input */

static void pollInput(void)
{
    scanKeys();
    keysPressed = keysDown();
    keysHeldNow = keysHeld();

    touchPosition t;
    int down = touchRead(&t) ? 1 : 0;
    if (down && (t.px != 0 || t.py != 0)) {
        touchX = t.px;
        touchY = t.py;
        touchDownNow = 1;
        touchPressed = !prevTouch;
        prevTouch = 1;
    } else {
        touchDownNow = 0;
        touchPressed = 0;
        prevTouch = 0;
    }
}

/* ------------------------------------------------------------- quiz screens */

/* A 5x7 flame drawn pixel by pixel: the streak marker, with no image asset. */
static void drawFlame(Surface *s, int x, int y)
{
    static const u8 rows[7] = { 0x04, 0x04, 0x0A, 0x1F, 0x1F, 0x0E, 0x04 };
    for (int ry = 0; ry < 7; ry++) {
        for (int rx = 0; rx < 5; rx++) {
            if (!(rows[ry] & (1u << (4 - rx)))) continue;
            surfPixel(s, x + rx, y + ry, ry >= 5 ? COL_RED : COL_GOLD);
        }
    }
}

static void drawHud(void)
{
    Surface *s = &gfxTop;
    char buf[32];

    /* clear the HUD strip only -- the question below stays untouched */
    surfRect(s, 0, 0, SCREEN_W, 28, COL_PANEL);
    surfHLine(s, 0, 27, SCREEN_W, COL_BORDER);

    surfText(s, 6, 3, "TIME", COL_TEXT_DIM, 1);
    int barX = 40, barW = 128, barH = 7, barY = 4;
    surfRect(s, barX, barY, barW, barH, COL_BORDER);

    int left = timeLeftFrames;
    if (left < 0) left = 0;
    if (left > QUIZ_FRAMES) left = QUIZ_FRAMES;
    int filled = barW * left / QUIZ_FRAMES;
    u16 barColor = COL_CYAN;
    if (left <= QUIZ_FRAMES / 3) barColor = COL_RED;
    else if (left <= QUIZ_FRAMES / 2) barColor = COL_GOLD;
    if (filled > 0) surfRect(s, barX, barY, filled, barH, barColor);

    /* seconds with one decimal, e.g. "9.4s" */
    int tenths = (left * 10 + QUIZ_FRAMES - 1) / QUIZ_FRAMES;
    snprintf(buf, sizeof(buf), "%d.%ds", tenths / 10, tenths % 10);
    surfText(s, 176, 1, buf, left <= QUIZ_FRAMES / 3 ? COL_RED : COL_TEXT, 2);

    /* second row: streak, XP, banked game time (shared by both mini-games) */
    drawFlame(s, 5, 13);
    snprintf(buf, sizeof(buf), "STREAK x%d", streak);
    surfText(s, 13, 16, buf, streak > 0 ? COL_GOLD : COL_TEXT_DIM, 1);

    snprintf(buf, sizeof(buf), "XP %lu", (unsigned long)save.xp);
    surfText(s, 86, 16, buf, COL_TEXT, 1);

    char clock[12];
    formatClock(save.gameMs, clock, sizeof(clock));
    snprintf(buf, sizeof(buf), "GAME %s", clock);
    surfText(s, 152, 16, buf, COL_CYAN, 1);
}

/* Draw the four answer cards on the bottom screen. */
static void drawOptions(void)
{
    Surface *s = &gfxBottom;

    for (int i = 0; i < 4; i++) {
        const short *r = optionRect[i];
        u16 fill = COL_PANEL;
        u16 border = COL_BORDER;
        u16 textColor = COL_TEXT;

        if (quizPhase == 1) {
            if (i == question.correct) {
                fill = COL_DARKGREEN;
                border = COL_GREEN;
                textColor = COL_GREEN;
            } else if (i == selectedOption) {
                fill = COL_DARKRED;
                border = COL_RED;
                textColor = COL_RED;
            }
        } else if (i == cursorOption) {
            border = COL_GOLD;
        }

        surfCard(s, r[0], r[1], r[2], r[3], fill, border, 0);

        /* letter badge */
        static const char letters[5] = "ABCD";
        char label[2] = { letters[i], '\0' };
        surfRect(s, r[0] + 6, r[1] + 6, 14, 14, border);
        surfTextCentered(s, r[0] + 13, r[1] + 10, label, textColor, 1);

        /* option text, wrapped and centred in the card */
        int textW = r[2] - 14;
        int scale = (int)strlen(question.options[i]) <= 9 ? 2 : 1;
        int height = wrappedHeight(textW, question.options[i], scale);
        int ty = r[1] + 24 + (r[3] - 26 - height) / 2;
        surfTextWrapped(s, r[0] + 7, ty, textW, question.options[i], textColor,
                        scale);
    }

    if (quizPhase == 1) {
        surfTextCentered(&gfxBottom, 128, 178, "TAP TO CONTINUE", COL_TEXT_DIM,
                         1);
    } else {
        surfTextCentered(&gfxBottom, 128, 178, "TAP AN ANSWER  (or D-PAD + A)",
                         COL_TEXT_DIM, 1);
    }
}

/* Full top-screen redraw for the current question (and its feedback). */
static void drawQuestion(void)
{
    Surface *s = &gfxTop;

    surfClear(s, COL_BG);
    drawHud();

    /* kind label */
    surfText(s, 8, 33, question.label, COL_GOLD, 1);
    surfRect(s, 8, 43, SCREEN_W - 16, 1, COL_BORDER);

    /* The prompt: short words get the big font, sentences are wrapped. */
    int promptBottom;
    if (question.big) {
        surfTextCentered(s, 128, 58, question.prompt,
                         quizPhase == 1 ? COL_TEXT_DIM : COL_TEXT, 2);
        promptBottom = 58 + textLineHeight(2);
    } else {
        promptBottom = surfTextWrapped(s, 8, 50, SCREEN_W - 16, question.prompt,
                                       quizPhase == 1 ? COL_TEXT_DIM : COL_TEXT,
                                       1);
    }

    /* hint line under the prompt */
    int hintY = promptBottom + 6;
    if (hintY > 108) hintY = 108;
    surfTextWrapped(s, 8, hintY, SCREEN_W - 16, question.hint, COL_TEXT_DIM, 1);

    /* feedback panel */
    if (quizPhase == 1) {
        int top = 116;
        surfRect(s, 6, top, SCREEN_W - 12, SCREEN_H - top - 6,
                 answerWasCorrect ? COL_DARKGREEN : COL_DARKRED);
        surfFrame(s, 6, top, SCREEN_W - 12, SCREEN_H - top - 6,
                  answerWasCorrect ? COL_GREEN : COL_RED, 1);

        const char *title = answerWasCorrect ? "CORRECT"
                          : (selectedOption < 0 ? "TIME'S UP" : "INCORRECT");
        surfText(s, 12, top + 5, title,
                 answerWasCorrect ? COL_GREEN : COL_RED, 1);

        /* The explanation is the whole point of a wrong answer: it names the
         * rule and shows the correct form. */
        surfTextWrapped(s, 12, top + 16, SCREEN_W - 24, question.explanation,
                        COL_TEXT, 1);
    }
}

static void nextQuestion(void)
{
    quizMake(lang, quizKinds, &question);
    timeLeftFrames = QUIZ_FRAMES;
    quizPhase = 0;
    selectedOption = -1;
    cursorOption = 0;
    screenDirty = 1;
}

static void beginFeedback(int correct, int timedOut)
{
    quizPhase = 1;
    feedbackFrames = 0;
    answerWasCorrect = correct;

    save.totalAsked++;
    if (correct) {
        save.totalCorrect++;
        streak++;

        /* Reward: XP grows with the streak, and every correct answer adds one
         * second to the game-time bank (Tetris and Chess both spend it). */
        int bonus = 10 + (streak > 10 ? 20 : streak * 2);
        save.xp += (u32)bonus;
        save.gameMs += 1000;
        save.dailyCorrect++;
        clampProgress();
        sndCorrect();
    } else {
        /* Wrong answers and timeouts earn no XP and no game time. */
        streak = 0;
        if (timedOut) sndBuzzer();
        else          sndWrong();
    }

    /* Daily goal: pay the bonus exactly once per session. */
    if (!save.dailyDone && save.dailyCorrect >= save.dailyGoal) {
        save.dailyDone = 1;
        save.streak++;
        if (save.streak > save.bestStreak) save.bestStreak = save.streak;
        save.gameMs += GOAL_BONUS_MS;
        clampProgress();
        goalBannerFrames = FPS * 3;
        sndGoal();
        saveNow();
    }

    screenDirty = 1;
}

/* -------------------------------------------------------------- quiz state */

static void startQuiz(unsigned int kinds)
{
    quizKinds = (kinds & QK_MASK_ALL) ? (kinds & QK_MASK_ALL) : QK_MASK_ALL;
    quizSeed(0x9E3779B9u ^ (frameCounter * 2654435761u) ^ (u32)save.xp);
    nextQuestion();
    state = ST_QUIZ;
    screenDirty = 1;
}

static void updateQuiz(void)
{
    if (quizPhase == 0) {
        if (timeLeftFrames > 0) timeLeftFrames--;

        if (touchPressed) {
            for (int i = 0; i < 4; i++) {
                if (pointInRect(touchX, touchY, optionRect[i])) {
                    selectedOption = i;
                    beginFeedback(i == question.correct, 0);
                    sndClick();
                    return;
                }
            }
        }

        /* D-pad moves the cursor, A confirms -- a keyboard path for players who
         * do not want to use the stylus. */
        if (keysPressed & KEY_LEFT)  { cursorOption = 0; screenDirty = 1; }
        if (keysPressed & KEY_RIGHT) { cursorOption = 1; screenDirty = 1; }
        if (keysPressed & KEY_UP)    { cursorOption = 2; screenDirty = 1; }
        if (keysPressed & KEY_DOWN)  { cursorOption = 3; screenDirty = 1; }
        if (keysPressed & KEY_A) {
            selectedOption = cursorOption;
            beginFeedback(cursorOption == question.correct, 0);
            sndClick();
            return;
        }

        if (timeLeftFrames == 0) {
            /* Strict rule: hitting zero is a miss, buzzer, then the rule. */
            selectedOption = -1;
            beginFeedback(0, 1);
        }
    } else {
        feedbackFrames++;
        if (touchPressed || (keysPressed & (KEY_A | KEY_B)) ||
            feedbackFrames > FEEDBACK_FRAMES) {
            nextQuestion();
        }
    }
}

/* ------------------------------------------------------------- menu screens */

/* How many drill types are enabled?  Used all over the menus. */
static int enabledDrillCount(void)
{
    int n = 0;
    for (int k = 0; k < QK_COUNT; k++) {
        if (save.drillMask & QK_MASK(k)) n++;
    }
    return n;
}

static void drawMenu(void)
{
    Surface *top = &gfxTop, *bottom = &gfxBottom;
    const LangPack *p = &langPacks[lang];
    char buf[64], clock[12];

    surfClear(top, COL_BG);
    surfTextCentered(top, 128, 8, "LINGUABLOCKS", COL_GOLD, 2);
    snprintf(buf, sizeof(buf), "%s . %s", p->name, p->code);
    surfTextCentered(top, 128, 28, buf, COL_TEXT_DIM, 1);

    /* progress card */
    surfCard(top, 8, 42, 240, 94, COL_PANEL, COL_BORDER, 0);

    snprintf(buf, sizeof(buf), "XP %lu", (unsigned long)save.xp);
    surfText(top, 16, 50, buf, COL_TEXT, 1);

    formatClock(save.gameMs, clock, sizeof(clock));
    snprintf(buf, sizeof(buf), "GAME %s", clock);
    surfText(top, 128, 50, buf, COL_CYAN, 1);

    snprintf(buf, sizeof(buf), "STREAK %lu   BEST %lu",
             (unsigned long)save.streak, (unsigned long)save.bestStreak);
    surfText(top, 16, 64, buf, COL_GOLD, 1);

    snprintf(buf, sizeof(buf), "DAILY GOAL %lu/%lu",
             (unsigned long)(save.dailyCorrect > save.dailyGoal
                                 ? save.dailyGoal
                                 : save.dailyCorrect),
             (unsigned long)save.dailyGoal);
    surfText(top, 16, 78, buf, COL_TEXT, 1);

    surfRect(top, 16, 90, 224, 7, COL_BORDER);
    {
        u32 done = save.dailyCorrect;
        if (done > save.dailyGoal) done = save.dailyGoal;
        u32 goal = save.dailyGoal ? save.dailyGoal : 1;
        int w = (int)(224u * done / goal);
        if (w > 0) surfRect(top, 16, 90, w, 7, COL_GREEN);
    }

    surfText(top, 16, 101, save.dailyDone ? "goal met - streak extended"
                                          : "hit the goal to extend the streak",
             COL_TEXT_DIM, 1);

    snprintf(buf, sizeof(buf), "BEST TETRIS %lu   BEST CHESS %lu",
             (unsigned long)save.bestTetrisScore,
             (unsigned long)save.bestChessScore);
    surfText(top, 16, 114, buf, COL_TEXT_DIM, 1);

    snprintf(buf, sizeof(buf), "ACCURACY %lu/%lu   DRILLS %d/4",
             (unsigned long)save.totalCorrect, (unsigned long)save.totalAsked,
             enabledDrillCount());
    surfText(top, 16, 126, buf, COL_TEXT_DIM, 1);

    /* footer: where progress is being written */
    if (saveAvailable()) {
        snprintf(buf, sizeof(buf), "SAVED TO %s", savePath());
    } else {
        snprintf(buf, sizeof(buf), "NO SD CARD - PROGRESS IS NOT SAVED");
    }
    surfText(top, 8, 144, buf, saveAvailable() ? COL_GREEN : COL_RED, 1);

    surfText(top, 8, 162, "D-PAD move   A select   B back", COL_TEXT_DIM, 1);

    /* bottom: the menu proper */
    surfClear(bottom, COL_BG);
    surfText(bottom, 12, 12, "MAIN MENU", COL_GOLD, 1);

    for (int i = 0; i < 5; i++) {
        const short *r = menuRect[i];
        char text[48];
        char sub[48];
        u16 color = COL_TEXT;

        sub[0] = '\0';
        switch (i) {
        case 0:
            snprintf(text, sizeof(text), "PLAY SPANISH");
            snprintf(sub, sizeof(sub), "Edexcel IGCSE drills");
            color = COL_GREEN;
            break;
        case 1:
            snprintf(text, sizeof(text), "PLAY FRENCH");
            snprintf(sub, sizeof(sub), "AQA drills");
            color = COL_GREEN;
            break;
        case 2:
            snprintf(text, sizeof(text), "SETUP DRILLS");
            snprintf(sub, sizeof(sub), "%d of 4 question types on",
                     enabledDrillCount());
            color = COL_GOLD;
            break;
        case 3:
            snprintf(text, sizeof(text), "PLAY TETRIS");
            if (save.gameMs >= MIN_GAME_MS) {
                formatClock(save.gameMs, clock, sizeof(clock));
                snprintf(sub, sizeof(sub), "GAME %s banked", clock);
                color = COL_GOLD;
            } else {
                snprintf(sub, sizeof(sub), "earn %us more game time",
                         (unsigned)((MIN_GAME_MS - save.gameMs + 999) / 1000));
                color = COL_TEXT_DIM;
            }
            break;
        default:
            snprintf(text, sizeof(text), "PLAY CHESS");
            if (save.gameMs >= MIN_GAME_MS) {
                formatClock(save.gameMs, clock, sizeof(clock));
                snprintf(sub, sizeof(sub), "GAME %s banked", clock);
                color = COL_GOLD;
            } else {
                snprintf(sub, sizeof(sub), "earn %us more game time",
                         (unsigned)((MIN_GAME_MS - save.gameMs + 999) / 1000));
                color = COL_TEXT_DIM;
            }
            break;
        }

        surfCard(bottom, r[0], r[1], r[2], r[3], COL_PANEL, COL_BORDER, 0);
        surfText(bottom, r[0] + 10, r[1] + 5, text, color, 1);
        surfText(bottom, r[0] + 10, r[1] + 15, sub, COL_TEXT_DIM, 1);
    }

    if (goalBannerFrames > 0) {
        surfRect(bottom, 8, 182, 240, 8, COL_DARKGREEN);
        surfTextCentered(bottom, 128, 182, "DAILY GOAL MET! +30s GAME TIME",
                         COL_GREEN, 1);
    }
}

static void openMenu(void)
{
    state = ST_MENU;
    screenDirty = 1;
    holdButton = -1;
    saveNow();
}

/* ------------------------------------------------------------ setup screen */

static const char *drillName[4] = {
    "FLASHCARDS", "FILL THE GAP", "TENSE DRILLS", "VERB ENDINGS"
};
static const char *drillDesc[4] = {
    "translate a word", "complete a sentence", "conjugate a verb", "pick the ending"
};

static void drawSetup(void)
{
    Surface *top = &gfxTop, *bottom = &gfxBottom;
    char buf[48];

    surfClear(top, COL_BG);
    surfTextCentered(top, 128, 8, "DRILL SETUP", COL_GOLD, 2);
    surfTextCentered(top, 128, 28, "Turn question types on or off,", COL_TEXT, 1);
    surfTextCentered(top, 128, 40, "then start a session.", COL_TEXT, 1);

    for (int k = 0; k < 4; k++) {
        int on = (save.drillMask & QK_MASK(k)) != 0;
        int y = 62 + k * 24;
        snprintf(buf, sizeof(buf), "%s", drillName[k]);
        surfText(top, 12, y, buf, on ? COL_GREEN : COL_TEXT_DIM, 1);
        surfText(top, 12, y + 11, drillDesc[k], COL_TEXT_DIM, 1);
        surfText(top, 200, y, on ? "ON" : "OFF",
                 on ? COL_GREEN : COL_RED, 1);
    }

    snprintf(buf, sizeof(buf), "%d of 4 types enabled", enabledDrillCount());
    surfTextCentered(top, 128, 170, buf, COL_TEXT_DIM, 1);

    /* ---------------------------------------------------------- bottom */
    surfClear(bottom, COL_BG);
    surfText(bottom, 12, 8, "TAP TO ENABLE / DISABLE", COL_TEXT_DIM, 1);

    for (int k = 0; k < 4; k++) {
        const short *r = toggleRect[k];
        int on = (save.drillMask & QK_MASK(k)) != 0;

        surfCard(bottom, r[0], r[1], r[2], r[3],
                 on ? COL_DARKGREEN : COL_PANEL,
                 on ? COL_GREEN : COL_BORDER, 0);
        surfText(bottom, r[0] + 8, r[1] + 7, drillName[k],
                 on ? COL_GREEN : COL_TEXT_DIM, 1);
        surfText(bottom, r[0] + 8, r[1] + 21, on ? "ON" : "OFF",
                 on ? COL_GREEN : COL_RED, 2);
    }

    surfCard(bottom, setupStartRect[0], setupStartRect[1], setupStartRect[2],
             setupStartRect[3], COL_DARKGREEN, COL_GREEN, 0);
    surfTextCentered(bottom, 128, setupStartRect[1] + 9, "START DRILLS",
                     COL_GREEN, 1);

    surfCard(bottom, setupBackRect[0], setupBackRect[1], setupBackRect[2],
             setupBackRect[3], COL_PANEL, COL_BORDER, 0);
    surfTextCentered(bottom, 128, setupBackRect[1] + 8, "BACK", COL_TEXT, 1);
}

static void handleSetup(void)
{
    if (keysPressed & KEY_B) {
        openMenu();
        return;
    }
    if (!touchPressed) return;

    for (int k = 0; k < 4; k++) {
        if (!pointInRect(touchX, touchY, toggleRect[k])) continue;

        unsigned int bit = QK_MASK(k);
        if (save.drillMask & bit) {
            /* Never let the player disable every single drill type. */
            if ((save.drillMask & ~bit) == 0) {
                showToast("KEEP AT LEAST ONE TYPE ON");
                sndWrong();
                screenDirty = 1;
                return;
            }
            save.drillMask &= ~bit;
        } else {
            save.drillMask |= bit;
        }
        save.drillMask &= QK_MASK_ALL;
        saveNow();
        sndClick();
        screenDirty = 1;
        return;
    }

    if (pointInRect(touchX, touchY, setupStartRect)) {
        sndClick();
        startQuiz(save.drillMask);
        return;
    }
    if (pointInRect(touchX, touchY, setupBackRect)) {
        sndClick();
        openMenu();
    }
}

/* ------------------------------------------------------------- tetris state */

static void startTetris(void)
{
    if (save.gameMs < MIN_GAME_MS) {
        showToast("EARN MORE GAME TIME");
        sndWrong();
        screenDirty = 1;
        return;
    }
    tetrisSeed(0x9E3779B9u ^ (frameCounter * 40503u) ^ save.xp);
    tetrisNew(&tetris);
    frameMsCarry = 0;
    state = ST_TETRIS;
    screenDirty = 1;
    saveNow();
}

static void finishTetris(TetrisOverReason reason)
{
    tetris.state = TS_OVER;
    tetris.overReason = reason;

    /* A little XP for playing, so the reward loop pays out either way. */
    save.xp += tetris.score / 10u;
    clampProgress();

    if (tetris.score > save.bestTetrisScore) save.bestTetrisScore = tetris.score;
    if ((u32)tetris.lines > save.bestLines) save.bestLines = (u32)tetris.lines;
    save.tetrisGames++;

    state = ST_TETRIS_OVER;
    screenDirty = 1;
    saveNow();
}

/* One frame of Tetris: clock, gravity and input. */
static void updateTetris(void)
{
    /* Count down the banked time, 1/60 s at a time without drifting. */
    if (tetris.state == TS_PLAYING) {
        frameMsCarry += 1000;
        u32 ms = frameMsCarry / FPS;
        frameMsCarry -= ms * FPS;
        if (save.gameMs > ms) save.gameMs -= ms;
        else {
            save.gameMs = 0;
            finishTetris(TO_TIME_UP);
            return;
        }
        tetrisStep(&tetris);
    }

    /* --- D-pad, with our own auto-repeat so we control the feel ---------- */
    int move = 0;
    if (keysPressed & KEY_LEFT) move = -1;
    if (keysPressed & KEY_RIGHT) move = 1;
    if (keysHeldNow & KEY_LEFT) {
        if (holdButton != KEY_LEFT) { holdButton = KEY_LEFT; holdTimer = 0; }
        else if (++holdTimer >= 6) { holdTimer = 2; move = -1; }
    } else if (keysHeldNow & KEY_RIGHT) {
        if (holdButton != KEY_RIGHT) { holdButton = KEY_RIGHT; holdTimer = 0; }
        else if (++holdTimer >= 6) { holdTimer = 2; move = 1; }
    } else if (keysHeldNow & KEY_DOWN) {
        if (holdButton != KEY_DOWN) { holdButton = KEY_DOWN; holdTimer = 0; }
        else if (++holdTimer >= 4) { holdTimer = 1; tetrisSoftDrop(&tetris); }
    } else {
        holdButton = -1;
    }
    if (move) tetrisMove(&tetris, move);

    if (keysPressed & KEY_A) tetrisRotate(&tetris);
    if (keysPressed & KEY_B) tetrisHardDrop(&tetris);
    if (keysPressed & KEY_START) {
        tetrisTogglePause(&tetris);
        sndClick();
    }
    if (keysPressed & KEY_SELECT) {
        saveNow();
        openMenu();
        return;
    }

    /* --- touch controls ------------------------------------------------- */
    if (touchPressed || touchDownNow) {
        int hit = tetrisHitTest(touchX, touchY);
        if (hit >= 0) {
            if (touchPressed) {
                holdButton = -100 - hit;      /* separate namespace from keys */
                holdTimer = 0;
                switch (hit) {
                case TB_ROTATE: tetrisRotate(&tetris); break;
                case TB_DROP:   tetrisHardDrop(&tetris); break;
                case TB_PAUSE:  tetrisTogglePause(&tetris); sndClick(); break;
                case TB_LEFT:   tetrisMove(&tetris, -1); break;
                case TB_RIGHT:  tetrisMove(&tetris, 1); break;
                case TB_DOWN:   tetrisSoftDrop(&tetris); break;
                default: break;
                }
            } else if (holdButton == -100 - hit) {
                int rate = (hit == TB_DOWN) ? 4 : 6;
                if (++holdTimer >= rate) {
                    holdTimer = 0;
                    if (hit == TB_LEFT) tetrisMove(&tetris, -1);
                    else if (hit == TB_RIGHT) tetrisMove(&tetris, 1);
                    else if (hit == TB_DOWN) tetrisSoftDrop(&tetris);
                }
            }
        }
    }

    if (tetris.state == TS_OVER) {
        finishTetris(tetris.overReason == TO_TOP_OUT ? TO_TOP_OUT : TO_TIME_UP);
    }
}

/* -------------------------------------------------------------- chess state */

static void startChess(void)
{
    if (save.gameMs < MIN_GAME_MS) {
        showToast("EARN MORE GAME TIME");
        sndWrong();
        screenDirty = 1;
        return;
    }
    chessNewGame(&chess);
    frameMsCarry = 0;
    state = ST_CHESS;
    screenDirty = 1;
    saveNow();
}

static void endChess(int result)
{
    /* Points for a win or a draw, then the score becomes XP. */
    if (result == CHESS_WHITE_WIN) chess.score += CHESS_WIN_BONUS;
    else if (result == CHESS_DRAW || result == CHESS_STALEMATE)
        chess.score += CHESS_DRAW_BONUS;

    chess.result = result;

    save.xp += (u32)(chess.score / 10);
    clampProgress();

    if ((u32)chess.score > save.bestChessScore)
        save.bestChessScore = (u32)chess.score;
    save.chessGames++;

    state = ST_CHESS_OVER;
    screenDirty = 1;
    saveNow();
}

static void updateChess(void)
{
    /* The game clock runs while the player is in the chess screen, whichever
     * side is thinking. */
    if (chess.result == CHESS_ONGOING) {
        frameMsCarry += 1000;
        u32 ms = frameMsCarry / FPS;
        frameMsCarry -= ms * FPS;
        if (save.gameMs > ms) {
            save.gameMs -= ms;
        } else {
            save.gameMs = 0;
            endChess(CHESS_TIMEOUT);
            return;
        }
    }

    if (keysPressed & KEY_SELECT) {
        saveNow();
        openMenu();
        return;
    }

    /* Two-frame "thinking" state: one frame to draw the message, then the
     * search runs.  It keeps the AI from freezing the screen without warning. */
    if (chess.thinking > 0) {
        if (--chess.thinking == 0) {
            chessPlayAi(&chess);
            screenDirty = 1;
            if (chess.result != CHESS_ONGOING) {
                endChess(chess.result);
                return;
            }
        }
    }

    if (touchPressed) {
        if (chessTouchResign(touchX, touchY)) {
            endChess(CHESS_RESIGNED);
            return;
        }

        /* A pawn on the last rank waits here until a piece is picked. */
        if (chess.promoPending) {
            int piece = chessTouchPromotion(touchX, touchY);
            if (piece >= 0 && chessChoosePromotion(&chess, piece)) {
                sndClick();
                screenDirty = 1;
                if (chess.result != CHESS_ONGOING) {
                    endChess(chess.result);
                    return;
                }
                if (chess.board.side == CH_BLACK) chess.thinking = 2;
            }
            return;
        }

        int sq = chessTouchSquare(touchX, touchY);
        if (sq >= 0 && chessTap(&chess, sq)) {
            sndClick();
            screenDirty = 1;
            if (chess.result != CHESS_ONGOING) {
                endChess(chess.result);
                return;
            }
            if (chess.board.side == CH_BLACK) chess.thinking = 2;
        }
    }
}

/* ---------------------------------------------------------------- painting */

static void render(void)
{
    switch (state) {
    case ST_MENU:
        if (screenDirty) {
            drawMenu();
            screenDirty = 0;
        }
        break;

    case ST_SETUP:
        if (screenDirty) {
            drawSetup();
            screenDirty = 0;
        }
        break;

    case ST_QUIZ:
        if (screenDirty) {
            drawQuestion();
            drawOptions();
            screenDirty = 0;
        } else {
            drawHud();                 /* only the countdown keeps moving */
        }
        break;

    case ST_TETRIS:
        tetrisDraw(&tetris, &gfxBottom, &gfxTop, save.gameMs);
        break;

    case ST_CHESS:
        chessDrawGame(&chess, &gfxBottom, &gfxTop, save.gameMs,
                      chessInCheck(&chess.board, chess.board.side));
        break;

    case ST_TETRIS_OVER:
        if (screenDirty) {
            tetrisDrawGameOver(&tetris, &gfxBottom, &gfxTop);
            screenDirty = 0;
        }
        break;

    case ST_CHESS_OVER:
        if (screenDirty) {
            chessDrawResult(&chess, &gfxBottom, &gfxTop);
            screenDirty = 0;
        }
        break;
    }

    if (toastFrames > 0) {
        surfRect(&gfxBottom, 8, 168, 240, 16, COL_DARKRED);
        surfTextCentered(&gfxBottom, 128, 171, toastText, COL_TEXT, 1);
    }
}

/* ------------------------------------------------------------------- menus */

static void handleMenu(void)
{
    int choice = -1;

    if (touchPressed) {
        for (int i = 0; i < 5; i++) {
            if (pointInRect(touchX, touchY, menuRect[i])) {
                choice = i;
                break;
            }
        }
    }
    if (keysPressed & KEY_A)
        choice = (save.lang == (u32)LANG_FR) ? 1 : 0;   /* quick start */

    if (keysPressed & KEY_B) {
        openMenu();                    /* flushes the save and repaints */
        return;
    }

    switch (choice) {
    case 0:
        lang = LANG_ES;
        save.lang = (u32)lang;
        saveNow();
        startQuiz(save.drillMask);
        break;
    case 1:
        lang = LANG_FR;
        save.lang = (u32)lang;
        saveNow();
        startQuiz(save.drillMask);
        break;
    case 2: state = ST_SETUP; screenDirty = 1; break;
    case 3: startTetris(); break;
    case 4: startChess(); break;
    default: break;
    }
}

/* -------------------------------------------------------------------- main */

int main(void)
{
    gfxInit();
    sndInit();

    saveInit();          /* mounts the SD card and loads the profile */
    saveNewSession();    /* streak bookkeeping for this play session    */

    lang = (Lang)(save.lang < LANG_COUNT ? save.lang : 0);
    quizKinds = save.drillMask ? save.drillMask : QK_MASK_ALL;

    /* The menu itself offers Spanish and French, so there is no separate
     * first-run language gate. */
    state = ST_MENU;
    screenDirty = 1;

    while (1) {
        pollInput();

        if (toastFrames > 0) toastFrames--;
        if (goalBannerFrames > 0) goalBannerFrames--;

        switch (state) {
        case ST_MENU:
            handleMenu();
            break;

        case ST_SETUP:
            handleSetup();
            break;

        case ST_QUIZ:
            if (keysPressed & KEY_B) {
                saveNow();
                openMenu();
            } else {
                updateQuiz();
            }
            break;

        case ST_TETRIS:
            updateTetris();
            break;

        case ST_CHESS:
            updateChess();
            break;

        case ST_TETRIS_OVER:
        case ST_CHESS_OVER:
            if (touchPressed || (keysPressed & (KEY_A | KEY_B | KEY_START))) {
                sndClick();
                openMenu();
            }
            break;
        }

        render();
        gfxPresent();
        frameCounter++;

        /* Make sure progress survives the console being switched off while the
         * player is idle in the menu. */
        if ((frameCounter % (FPS * 20)) == 0 && state == ST_MENU) saveNow();
    }

    return 0;
}
