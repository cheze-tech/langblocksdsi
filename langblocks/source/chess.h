/* -------------------------------------------------------------------------
 * chess.h -- the chess mini-game.
 *
 * Engine
 * ------
 * A 64-square board in a plain array, pseudo-legal move generation, then a
 * make/check/unmake filter so only fully legal moves ever reach the board.
 * Castling, en passant and promotion are all implemented, because a drill app
 * that let you play an illegal move would be worse than no chess at all.
 *
 * The AI is negamax with alpha-beta, a material + piece-activity evaluation,
 * capture-first move ordering and a hard node budget, so it can never stall
 * the DSi for long.  It plays black; the player is white.
 * ------------------------------------------------------------------------- */
#ifndef CHESS_H
#define CHESS_H

#include "gfx.h"
#include "platform.h"

/* Piece types (sign carries the colour: + white, - black) */
#define CH_EMPTY   0
#define CH_PAWN    1
#define CH_KNIGHT  2
#define CH_BISHOP  3
#define CH_ROOK    4
#define CH_QUEEN   5
#define CH_KING    6

#define CH_WHITE  1
#define CH_BLACK  (-1)

/* Castling rights */
#define CASTLE_WK 0x01
#define CASTLE_WQ 0x02
#define CASTLE_BK 0x04
#define CASTLE_BQ 0x08

/* Move flags */
#define MF_CAPTURE 0x01
#define MF_EP      0x02
#define MF_DOUBLE  0x04
#define MF_CASTLE  0x08
#define MF_PROMO   0x10

/* Square index: 0 = a8 (top left), 63 = h1 (bottom right).
 * White therefore moves towards lower indices. */
#define CH_SQ(rank, file) (((rank) << 3) | (file))
#define CH_RANK(sq) ((sq) >> 3)
#define CH_FILE(sq) ((sq) & 7)

typedef struct {
    s8 board[64];
    s8 side;        /* CH_WHITE or CH_BLACK to move */
    u8 castling;    /* CASTLE_* bits                  */
    s8 ep;          /* en-passant target square or -1 */
    u16 halfmove;   /* for the fifty-move rule        */
    u16 fullmove;
} ChessBoard;

typedef struct {
    u8 from;
    u8 to;
    u8 promo;       /* piece type when MF_PROMO is set */
    u8 flags;
} ChessMove;

/* Everything make/unmake needs to restore the position. */
typedef struct {
    u8 castling;
    s8 ep;
    u16 halfmove;
    s8 captured;
    s8 capturedSq;
} ChessUndo;

/* ------------------------------------------------------------- engine API */
void chessNewBoard(ChessBoard *b);
int  chessLoadFen(ChessBoard *b, const char *fen);   /* used by the tests */
int  chessSideOf(s8 piece);
int  chessTypeOf(s8 piece);

/* Pseudo-legal moves (may leave the king in check). */
int  chessPseudoMoves(const ChessBoard *b, ChessMove *out, int max);
/* Fully legal moves; this is the one the game and the AI use. */
int  chessLegalMoves(const ChessBoard *b, ChessMove *out, int max);

void chessMakeMove(ChessBoard *b, const ChessMove *m, ChessUndo *u);
void chessUnmakeMove(ChessBoard *b, const ChessMove *m, const ChessUndo *u);

int  chessInCheck(const ChessBoard *b, int side);
int  chessAttacked(const ChessBoard *b, int sq, int bySide);

/* Pick a move for the side to move.  Returns 0 when there is none. */
int  chessBestMove(ChessBoard *b, ChessMove *out, int depth);

/* ------------------------------------------------------------ game state */
typedef enum {
    CHESS_ONGOING = 0,
    CHESS_WHITE_WIN,
    CHESS_BLACK_WIN,
    CHESS_STALEMATE,
    CHESS_DRAW,
    CHESS_TIMEOUT,
    CHESS_RESIGNED
} ChessResult;

#define CHESS_TARGET_MAX 32

typedef struct {
    ChessBoard board;
    int selected;                   /* selected square, -1 when none      */
    int targets[CHESS_TARGET_MAX];  /* legal destinations for `selected`  */
    int targetCount;
    int score;                      /* the player's capture points        */
    int result;
    int thinking;                   /* show "thinking" for one frame      */
    int lastFrom, lastTo;           /* highlight the last move            */
    int promoPending;               /* a pawn reached the last rank       */
    int promoFrom, promoTo;
} ChessGame;

void chessNewGame(ChessGame *g);

/* Handle a tap on a board square.  Returns 1 if the selection changed or a
 * move was played (the caller then checks the result and lets the AI reply). */
int chessTap(ChessGame *g, int square);

/* Finish a pending promotion.  `promoType` is CH_QUEEN, CH_ROOK, CH_BISHOP or
 * CH_KNIGHT; all four are legal choices.  Returns 1 when the move was played. */
int chessChoosePromotion(ChessGame *g, int promoType);

/* Which promotion button is at (x, y)?  Returns a piece type or -1. */
int chessTouchPromotion(int x, int y);

/* Let the AI play black.  Call when board.side == CH_BLACK. */
void chessPlayAi(ChessGame *g);

/* Recompute g->result (mate / stalemate / draw). */
int chessUpdateResult(ChessGame *g);

/* ---------------------------------------------------------------- drawing */
void chessDrawGame(const ChessGame *g, Surface *bottom, Surface *top,
                   u32 timeLeftMs, int inCheck);
void chessDrawResult(const ChessGame *g, Surface *bottom, Surface *top);

/* Hit tests.  chessTouchSquare returns -1 when the touch misses the board. */
int chessTouchSquare(int x, int y);
int chessTouchResign(int x, int y);

#endif /* CHESS_H */
