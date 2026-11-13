/* -------------------------------------------------------------------------
 * test_logic.c -- host-side checks for the parts of the game that are pure
 * logic: the conjugation engine, the question builder, the Tetris engine and
 * the chess engine.
 *
 * Build and run (no devkitPro needed):
 *     gcc -DHOST_TEST -Isource -Wall -Wextra -fsanitize=address,undefined \
 *         tools/test_logic.c tools/host_stubs.c \
 *         source/conjugate.c source/quiz.c source/data.c source/tetris.c \
 *         source/chess.c -o /tmp/logic_test && /tmp/logic_test
 *
 * The conjugation dump is meant to be read: it is the table a teacher would
 * check.  The chess perft numbers come from the standard "Chess Programming
 * Wiki" positions, which is how you prove a move generator has no holes.
 * ------------------------------------------------------------------------- */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess.h"
#include "conjugate.h"
#include "data.h"
#include "quiz.h"
#include "tetris.h"

static void dumpVerb(Lang lang, const char *inf)
{
    printf("\n%s (%s) - %s\n", inf, lang == LANG_ES ? "ES" : "FR",
           conjIsIrregular(lang, inf) ? "irregular" : "regular");

    for (int t = 0; t < TENSE_COUNT; t++) {
        printf("  %-14s", conjTenseName(lang, (Tense)t));
        for (int p = 0; p < PERSON_COUNT; p++) {
            char f[48];
            conjForm(lang, inf, (Tense)t, p, f, sizeof(f));
            printf(" %-14s", f);
        }
        printf("| %s\n", conjEndingList(lang, inf, (Tense)t));
    }

    printf("  rule (preterite nosotros): %s\n", conjRule(lang, inf, TENSE_PRETERITE, 3));
    printf("  rule (present nous)      : %s\n", conjRule(lang, inf, TENSE_PRESENT, 3));
    printf("  rule (present je)        : %s\n", conjRule(lang, inf, TENSE_PRESENT, 0));
    printf("  rule (preterite yo)      : %s\n", conjRule(lang, inf, TENSE_PRETERITE, 0));
}

static void testConjugation(void)
{
    static const char *es[] = {
        "hablar", "comer", "vivir", "llegar", "tener", "ir", "ser",
        "estar", "hacer", "poder", "querer", "venir"
    };
    static const char *fr[] = {
        "parler", "étudier", "manger", "finir", "choisir", "vendre", "attendre",
        "être", "avoir", "aller", "faire", "prendre", "venir", "pouvoir"
    };

    for (unsigned i = 0; i < sizeof(es) / sizeof(es[0]); i++)
        dumpVerb(LANG_ES, es[i]);
    for (unsigned i = 0; i < sizeof(fr) / sizeof(fr[0]); i++)
        dumpVerb(LANG_FR, fr[i]);
}

/* ---------------------------------------------------------------- quizzes */

static void checkQuestion(const Question *q)
{
    assert(q->correct >= 0 && q->correct < 4);
    assert(strcmp(q->options[q->correct], q->answer) == 0);
    assert(q->prompt[0] != '\0');
    assert(q->explanation[0] != '\0');

    for (int a = 0; a < 4; a++) {
        for (int b = a + 1; b < 4; b++) {
            if (strcmp(q->options[a], q->options[b]) == 0) {
                fprintf(stderr, "duplicate option '%s' in %s\n",
                        q->options[a], q->prompt);
                abort();
            }
        }
    }
}

static void testQuizGeneration(void)
{
    int questions = 0;

    for (int i = 0; i < 30000; i++) {
        Lang lang = (i & 1) ? LANG_FR : LANG_ES;
        Question q;
        quizMake(lang, QK_MASK_ALL, &q);
        checkQuestion(&q);
        questions++;
    }
    printf("\nquiz: %d generated questions, all with 4 distinct options\n",
           questions);
}

static void testDrillMask(void)
{
    /* Only tense + ending drills enabled: nothing else may come out. */
    unsigned int onlyGrammar = QK_MASK(QK_TENSE) | QK_MASK(QK_ENDING);
    for (int i = 0; i < 4000; i++) {
        Question q;
        quizMake(LANG_ES, onlyGrammar, &q);
        checkQuestion(&q);
        assert(q.kind == QK_TENSE || q.kind == QK_ENDING);
    }

    /* A single type on its own must be honoured too. */
    for (int k = 0; k < QK_COUNT; k++) {
        for (int i = 0; i < 500; i++) {
            Question q;
            quizMake(LANG_FR, QK_MASK(k), &q);
            checkQuestion(&q);
            assert((int)q.kind == k);
        }
    }

    /* An empty mask falls back to all four instead of crashing. */
    for (int i = 0; i < 200; i++) {
        Question q;
        quizMake(LANG_ES, 0, &q);
        checkQuestion(&q);
    }
    printf("quiz: drill mask filtering verified (single types and empty mask)\n");
}

/* ----------------------------------------------------------------- tetris */

static void testTetris(void)
{
    srand(12345);
    Tetris t;
    tetrisNew(&t);

    long pieces = 0;
    int overCount = 0;

    for (int frame = 0; frame < 400000; frame++) {
        int action = rand() % 12;
        switch (action) {
        case 0: tetrisMove(&t, -1); break;
        case 1: tetrisMove(&t, 1); break;
        case 2: tetrisRotate(&t); break;
        case 3: tetrisSoftDrop(&t); break;
        case 4: tetrisHardDrop(&t); pieces++; break;
        default: tetrisStep(&t); break;
        }

        for (int y = 0; y < TETRIS_ROWS; y++) {
            for (int x = 0; x < TETRIS_COLS; x++) {
                assert(t.board[y][x] <= 7);
            }
        }
        assert(t.score < 100000000u);

        if (t.state == TS_OVER) {
            overCount++;
            tetrisNew(&t);
        }
    }

    printf("tetris: 400000 random frames, %ld hard drops, %d games ended\n",
           pieces, overCount);
}

/* ------------------------------------------------------------------ chess */

/* Count the leaf nodes of the legal move tree.  Matching the published
 * numbers proves the generator produces exactly the legal moves: no more,
 * no fewer -- castling, en passant and promotions included. */
static long perft(ChessBoard *b, int depth)
{
    if (depth == 0) return 1;

    ChessMove moves[256];
    int n = chessLegalMoves(b, moves, 256);
    if (depth == 1) return n;

    long total = 0;
    for (int i = 0; i < n; i++) {
        ChessUndo u;
        chessMakeMove(b, &moves[i], &u);
        total += perft(b, depth - 1);
        chessUnmakeMove(b, &moves[i], &u);
    }
    return total;
}

static void perftCheck(const char *name, const char *fen,
                       const long *expected, int count)
{
    ChessBoard b;
    assert(chessLoadFen(&b, fen));

    for (int depth = 1; depth <= count; depth++) {
        long got = perft(&b, depth);
        if (got != expected[depth - 1]) {
            fprintf(stderr, "perft %s depth %d: got %ld expected %ld\n",
                    name, depth, got, expected[depth - 1]);
            abort();
        }
    }
    printf("perft %-10s depth 1-%d ok\n", name, count);
}

static void testChessRules(void)
{
    static const long startExpected[4] = { 20, 400, 8902, 197281 };
    static const long kiwipete[3] = { 48, 2039, 97862 };
    static const long pos3[3] = { 14, 191, 2812 };
    static const long pos4[3] = { 6, 264, 9467 };

    perftCheck("start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
               startExpected, 4);
    perftCheck("kiwipete",
               "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
               kiwipete, 3);
    perftCheck("position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
               pos3, 3);
    perftCheck("position4",
               "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
               pos4, 3);

    /* Checkmate and stalemate detection. */
    ChessBoard b;
    ChessGame g;

    assert(chessLoadFen(&b, "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1"));
    g.board = b;
    g.result = CHESS_ONGOING;
    assert(chessUpdateResult(&g) == CHESS_WHITE_WIN);

    assert(chessLoadFen(&b, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    g.board = b;
    g.result = CHESS_ONGOING;
    assert(chessUpdateResult(&g) == CHESS_STALEMATE);

    /* The AI must return a legal move, and none when the game is over. */
    ChessBoard ai;
    chessNewBoard(&ai);
    ChessMove move;
    assert(chessBestMove(&ai, &move, 3) == 1);
    ChessMove legal[256];
    int n = chessLegalMoves(&ai, legal, 256);
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (legal[i].from == move.from && legal[i].to == move.to) found = 1;
    }
    assert(found);

    assert(chessLoadFen(&ai, "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1"));
    assert(chessBestMove(&ai, &move, 3) == 0);

    /* Tapping behaves: select a pawn, then move it. */
    ChessGame ui;
    chessNewGame(&ui);
    assert(chessTap(&ui, CH_SQ(6, 4)) == 1);        /* e2 pawn */
    assert(ui.selected == CH_SQ(6, 4));
    assert(ui.targetCount == 2);                    /* e3 and e4 */
    assert(chessTap(&ui, CH_SQ(4, 4)) == 1);        /* e4 */
    assert(ui.board.board[CH_SQ(4, 4)] == CH_PAWN);
    assert(ui.board.side == CH_BLACK);

    /* It is black's turn now, so a white tap must change nothing. */
    assert(chessTap(&ui, CH_SQ(3, 0)) == 0);        /* black's turn: ignored */
    assert(ui.board.board[CH_SQ(4, 4)] == CH_PAWN);

    /* Promotion picker: all four pieces can be chosen and the move lands. */
    {
        static const int promoTypes[4] = {
            CH_QUEEN, CH_ROOK, CH_BISHOP, CH_KNIGHT
        };
        for (int i = 0; i < 4; i++) {
            ChessGame pg;
            chessNewGame(&pg);
            assert(chessLoadFen(&pg.board, "7k/P7/8/8/8/8/8/7K w - - 0 1"));
            pg.result = CHESS_ONGOING;
            assert(chessTap(&pg, CH_SQ(1, 0)) == 1);   /* a7 pawn */
            assert(chessTap(&pg, CH_SQ(0, 0)) == 1);   /* a8 */
            assert(pg.promoPending == 1);
            assert(pg.board.board[CH_SQ(0, 0)] == 0);   /* nothing played yet */
            assert(chessChoosePromotion(&pg, promoTypes[i]) == 1);
            assert(pg.promoPending == 0);
            assert(pg.board.board[CH_SQ(0, 0)] == promoTypes[i]);
            assert(pg.board.side == CH_BLACK);
        }
    }

    printf("chess: rules, mate/stalemate, promotion picker, AI legality and "
           "touch selection ok\n");
}

static void testChessFuzz(void)
{
    srand(777);
    long plies = 0;
    int games = 0;

    for (int game = 0; game < 150; game++) {
        ChessGame g;
        chessNewGame(&g);

        for (int ply = 0; ply < 400; ply++) {
            ChessMove moves[256];
            int n = chessLegalMoves(&g.board, moves, 256);
            if (n == 0) break;

            ChessMove m = moves[rand() % n];
            ChessUndo u;
            chessMakeMove(&g.board, &m, &u);
            plies++;

            /* Invariants: piece codes stay in range and both kings exist. */
            int whiteKings = 0, blackKings = 0, whitePieces = 0, blackPieces = 0;
            for (int sq = 0; sq < 64; sq++) {
                s8 p = g.board.board[sq];
                int type = p > 0 ? p : -p;
                assert(type >= 0 && type <= 6);
                if (type == CH_KING) { if (p > 0) whiteKings++; else blackKings++; }
                if (p > 0) whitePieces++;
                if (p < 0) blackPieces++;
            }
            assert(whiteKings == 1 && blackKings == 1);
            assert(whitePieces <= 16 && blackPieces <= 16);

            chessUpdateResult(&g);
            chessUnmakeMove(&g.board, &m, &u);
            if (g.result != CHESS_ONGOING) break;
        }
        games++;
    }
    printf("chess: %d random games, %ld legal plies, no invariant broken\n",
           games, plies);
}

int main(void)
{
    testConjugation();
    testQuizGeneration();
    testDrillMask();
    testTetris();
    testChessRules();
    testChessFuzz();
    printf("\nall logic tests passed\n");
    return 0;
}
