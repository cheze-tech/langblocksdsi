/* -------------------------------------------------------------------------
 * chess.c -- rules, AI and touch UI for the chess mini-game.
 *
 * Layout of this file:
 *   1. board helpers
 *   2. move generation        (pseudo-legal)
 *   3. make / unmake          (the only place the board is mutated)
 *   4. legality filtering     (king safety, castling legality, check)
 *   5. evaluation + search    (negamax with alpha-beta)
 *   6. game wrapper for the UI (selection, score, result)
 *   7. drawing
 *
 * Nothing allocates: the largest arrays are 256 moves on the stack, and the
 * search depth is bounded, so stack use stays well under a few kilobytes.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess.h"

/* Piece values in centipawns; used by the evaluation and the AI's ordering. */
static const int pieceValue[7] = { 0, 100, 320, 330, 500, 900, 20000 };

static const int knightOffsets[8][2] = {
    { 1, 2 }, { 2, 1 }, { 2, -1 }, { 1, -2 },
    { -1, -2 }, { -2, -1 }, { -2, 1 }, { -1, 2 }
};
static const int kingOffsets[8][2] = {
    { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
    { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
};
static const int bishopDirs[4][2] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
static const int rookDirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

static int inBounds(int r, int f)
{
    return r >= 0 && r < 8 && f >= 0 && f < 8;
}

int chessSideOf(s8 piece)
{
    if (piece > 0) return CH_WHITE;
    if (piece < 0) return CH_BLACK;
    return 0;
}

int chessTypeOf(s8 piece)
{
    return piece > 0 ? piece : -piece;
}

/* ------------------------------------------------------- 1. board helpers */

void chessNewBoard(ChessBoard *b)
{
    memset(b, 0, sizeof(*b));

    static const s8 backRank[8] = {
        CH_ROOK, CH_KNIGHT, CH_BISHOP, CH_QUEEN,
        CH_KING, CH_BISHOP, CH_KNIGHT, CH_ROOK
    };

    for (int f = 0; f < 8; f++) {
        b->board[CH_SQ(0, f)] = (s8)-backRank[f];      /* black home rank */
        b->board[CH_SQ(1, f)] = -CH_PAWN;
        b->board[CH_SQ(6, f)] = CH_PAWN;
        b->board[CH_SQ(7, f)] = backRank[f];           /* white home rank */
    }
    b->side = CH_WHITE;
    b->castling = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ;
    b->ep = -1;
    b->halfmove = 0;
    b->fullmove = 1;
}

/* Minimal FEN reader.  It exists so the host tests can run the standard perft
 * positions, which are what prove the move generator is correct. */
int chessLoadFen(ChessBoard *b, const char *fen)
{
    memset(b, 0, sizeof(*b));
    b->ep = -1;

    int rank = 0, file = 0;
    const char *p = fen;

    for (; *p && *p != ' '; p++) {
        char c = *p;
        if (c == '/') {
            rank++;
            file = 0;
            if (rank > 7) return 0;
            continue;
        }
        if (c >= '1' && c <= '8') {
            file += c - '0';
            continue;
        }
        if (file > 7) return 0;

        s8 piece = 0;
        switch (c | 0x20) {           /* lower-case for the switch */
        case 'p': piece = CH_PAWN; break;
        case 'n': piece = CH_KNIGHT; break;
        case 'b': piece = CH_BISHOP; break;
        case 'r': piece = CH_ROOK; break;
        case 'q': piece = CH_QUEEN; break;
        case 'k': piece = CH_KING; break;
        default: return 0;
        }
        b->board[CH_SQ(rank, file)] = (c >= 'A' && c <= 'Z') ? piece : (s8)-piece;
        file++;
    }

    while (*p == ' ') p++;
    b->side = (*p == 'b') ? CH_BLACK : CH_WHITE;
    while (*p && *p != ' ') p++;

    while (*p == ' ') p++;
    for (; *p && *p != ' '; p++) {
        switch (*p) {
        case 'K': b->castling |= CASTLE_WK; break;
        case 'Q': b->castling |= CASTLE_WQ; break;
        case 'k': b->castling |= CASTLE_BK; break;
        case 'q': b->castling |= CASTLE_BQ; break;
        default: break;
        }
    }

    while (*p == ' ') p++;
    if (*p && *p != '-') {
        int f = p[0] - 'a';
        int r = p[1] - '1';
        if (inBounds(r, f)) b->ep = (s8)CH_SQ(r, f);
        while (*p && *p != ' ') p++;
    } else if (*p == '-') {
        p++;
    }

    while (*p == ' ') p++;
    b->halfmove = (u16)atoi(p);
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    b->fullmove = (u16)atoi(p);
    if (b->fullmove == 0) b->fullmove = 1;

    return 1;
}

/* -------------------------------------------------- 2. move generation */

static int addMove(ChessMove *out, int n, int max, int from, int to, int promo,
                   int flags)
{
    if (n >= max) return n;
    out[n].from = (u8)from;
    out[n].to = (u8)to;
    out[n].promo = (u8)promo;
    out[n].flags = (u8)flags;
    return n + 1;
}

static int addPawnMove(ChessMove *out, int n, int max, int from, int to,
                       int flags, int promoRank)
{
    if (CH_RANK(to) == promoRank) {
        /* All four promotions are generated: anything less would make the
         * move counts wrong (and the AI would never under-promote). */
        for (int promo = CH_KNIGHT; promo <= CH_QUEEN; promo++) {
            n = addMove(out, n, max, from, to, promo, flags | MF_PROMO);
        }
        return n;
    }
    return addMove(out, n, max, from, to, 0, flags);
}

int chessPseudoMoves(const ChessBoard *b, ChessMove *out, int max)
{
    int n = 0;
    const int side = b->side;

    for (int from = 0; from < 64; from++) {
        s8 piece = b->board[from];
        if (!piece || chessSideOf(piece) != side) continue;

        const int type = chessTypeOf(piece);
        const int r = CH_RANK(from);
        const int f = CH_FILE(from);

        switch (type) {
        case CH_PAWN: {
            const int dir = (side == CH_WHITE) ? -1 : 1;
            const int startRank = (side == CH_WHITE) ? 6 : 1;
            const int promoRank = (side == CH_WHITE) ? 0 : 7;

            int nr = r + dir;
            if (nr >= 0 && nr < 8 && b->board[CH_SQ(nr, f)] == 0) {
                n = addPawnMove(out, n, max, from, CH_SQ(nr, f), 0, promoRank);
                int nr2 = r + 2 * dir;
                if (r == startRank && b->board[CH_SQ(nr2, f)] == 0) {
                    n = addMove(out, n, max, from, CH_SQ(nr2, f), 0, MF_DOUBLE);
                }
            }
            for (int df = -1; df <= 1; df += 2) {
                int nf = f + df;
                if (!inBounds(nr, nf)) continue;
                int to = CH_SQ(nr, nf);
                s8 target = b->board[to];
                if (target && chessSideOf(target) != side) {
                    n = addPawnMove(out, n, max, from, to, MF_CAPTURE, promoRank);
                } else if (!target && to == b->ep) {
                    n = addPawnMove(out, n, max, from, to,
                                    MF_CAPTURE | MF_EP, promoRank);
                }
            }
            break;
        }

        case CH_KNIGHT:
            for (int k = 0; k < 8; k++) {
                int nr = r + knightOffsets[k][0];
                int nf = f + knightOffsets[k][1];
                if (!inBounds(nr, nf)) continue;
                int to = CH_SQ(nr, nf);
                s8 target = b->board[to];
                if (target && chessSideOf(target) == side) continue;
                n = addMove(out, n, max, from, to, 0, target ? MF_CAPTURE : 0);
            }
            break;

        case CH_BISHOP:
        case CH_ROOK:
        case CH_QUEEN: {
            const int (*dirs)[2] = (type == CH_BISHOP) ? bishopDirs : rookDirs;
            int dirCount = 4;

            for (int pass = 0; pass < (type == CH_QUEEN ? 2 : 1); pass++) {
                if (type == CH_QUEEN) {
                    dirs = pass == 0 ? rookDirs : bishopDirs;
                }
                for (int d = 0; d < dirCount; d++) {
                    int nr = r + dirs[d][0];
                    int nf = f + dirs[d][1];
                    while (inBounds(nr, nf)) {
                        int to = CH_SQ(nr, nf);
                        s8 target = b->board[to];
                        if (!target) {
                            n = addMove(out, n, max, from, to, 0, 0);
                        } else {
                            if (chessSideOf(target) != side) {
                                n = addMove(out, n, max, from, to, 0, MF_CAPTURE);
                            }
                            break;
                        }
                        nr += dirs[d][0];
                        nf += dirs[d][1];
                    }
                }
            }
            break;
        }

        case CH_KING: {
            for (int k = 0; k < 8; k++) {
                int nr = r + kingOffsets[k][0];
                int nf = f + kingOffsets[k][1];
                if (!inBounds(nr, nf)) continue;
                int to = CH_SQ(nr, nf);
                s8 target = b->board[to];
                if (target && chessSideOf(target) == side) continue;
                n = addMove(out, n, max, from, to, 0, target ? MF_CAPTURE : 0);
            }

            /* Castling.  The king must not be in check and must not pass
             * through an attacked square; the rook must still be home. */
            const int homeRank = (side == CH_WHITE) ? 7 : 0;
            const int enemy = -side;
            if (from == CH_SQ(homeRank, 4) &&
                !chessAttacked(b, from, enemy)) {
                if (side == CH_WHITE) {
                    if ((b->castling & CASTLE_WK) &&
                        b->board[CH_SQ(7, 7)] == CH_ROOK &&
                        b->board[CH_SQ(7, 5)] == 0 &&
                        b->board[CH_SQ(7, 6)] == 0 &&
                        !chessAttacked(b, CH_SQ(7, 5), enemy) &&
                        !chessAttacked(b, CH_SQ(7, 6), enemy)) {
                        n = addMove(out, n, max, from, CH_SQ(7, 6), 0, MF_CASTLE);
                    }
                    if ((b->castling & CASTLE_WQ) &&
                        b->board[CH_SQ(7, 0)] == CH_ROOK &&
                        b->board[CH_SQ(7, 1)] == 0 &&
                        b->board[CH_SQ(7, 2)] == 0 &&
                        b->board[CH_SQ(7, 3)] == 0 &&
                        !chessAttacked(b, CH_SQ(7, 3), enemy) &&
                        !chessAttacked(b, CH_SQ(7, 2), enemy)) {
                        n = addMove(out, n, max, from, CH_SQ(7, 2), 0, MF_CASTLE);
                    }
                } else {
                    if ((b->castling & CASTLE_BK) &&
                        b->board[CH_SQ(0, 7)] == -CH_ROOK &&
                        b->board[CH_SQ(0, 5)] == 0 &&
                        b->board[CH_SQ(0, 6)] == 0 &&
                        !chessAttacked(b, CH_SQ(0, 5), enemy) &&
                        !chessAttacked(b, CH_SQ(0, 6), enemy)) {
                        n = addMove(out, n, max, from, CH_SQ(0, 6), 0, MF_CASTLE);
                    }
                    if ((b->castling & CASTLE_BQ) &&
                        b->board[CH_SQ(0, 0)] == -CH_ROOK &&
                        b->board[CH_SQ(0, 1)] == 0 &&
                        b->board[CH_SQ(0, 2)] == 0 &&
                        b->board[CH_SQ(0, 3)] == 0 &&
                        !chessAttacked(b, CH_SQ(0, 3), enemy) &&
                        !chessAttacked(b, CH_SQ(0, 2), enemy)) {
                        n = addMove(out, n, max, from, CH_SQ(0, 2), 0, MF_CASTLE);
                    }
                }
            }
            break;
        }

        default:
            break;
        }
    }

    return n;
}

/* ---------------------------------------------------- 3. make / unmake */

void chessMakeMove(ChessBoard *b, const ChessMove *m, ChessUndo *u)
{
    u->castling = b->castling;
    u->ep = b->ep;
    u->halfmove = b->halfmove;
    u->captured = 0;
    u->capturedSq = -1;

    const s8 piece = b->board[m->from];
    const int side = b->side;

    if (m->flags & MF_EP) {
        /* The captured pawn sits beside the destination, not on it. */
        int capSq = m->to + (side == CH_WHITE ? 8 : -8);
        u->captured = b->board[capSq];
        u->capturedSq = (s8)capSq;
        b->board[capSq] = 0;
    } else if (b->board[m->to]) {
        u->captured = b->board[m->to];
        u->capturedSq = (s8)m->to;
    }

    b->board[m->from] = 0;
    if (m->flags & MF_PROMO) {
        b->board[m->to] = (s8)(side == CH_WHITE ? m->promo : -m->promo);
    } else {
        b->board[m->to] = piece;
    }

    if (m->flags & MF_CASTLE) {
        /* The rook hops over the king: h->f, or a->d. */
        int rank = CH_RANK(m->from);
        if (CH_FILE(m->to) == 6) {
            b->board[CH_SQ(rank, 5)] = b->board[CH_SQ(rank, 7)];
            b->board[CH_SQ(rank, 7)] = 0;
        } else {
            b->board[CH_SQ(rank, 3)] = b->board[CH_SQ(rank, 0)];
            b->board[CH_SQ(rank, 0)] = 0;
        }
    }

    /* Castling rights disappear when the king moves, when a rook moves, and
     * when a rook is captured on its home square. */
    if (chessTypeOf(piece) == CH_KING) {
        if (side == CH_WHITE) b->castling &= (u8)~(CASTLE_WK | CASTLE_WQ);
        else                  b->castling &= (u8)~(CASTLE_BK | CASTLE_BQ);
    }
    if (m->from == CH_SQ(7, 7) || m->to == CH_SQ(7, 7)) b->castling &= (u8)~CASTLE_WK;
    if (m->from == CH_SQ(7, 0) || m->to == CH_SQ(7, 0)) b->castling &= (u8)~CASTLE_WQ;
    if (m->from == CH_SQ(0, 7) || m->to == CH_SQ(0, 7)) b->castling &= (u8)~CASTLE_BK;
    if (m->from == CH_SQ(0, 0) || m->to == CH_SQ(0, 0)) b->castling &= (u8)~CASTLE_BQ;

    b->ep = (m->flags & MF_DOUBLE) ? (s8)((m->from + m->to) / 2) : (s8)-1;

    if (chessTypeOf(piece) == CH_PAWN || u->captured) b->halfmove = 0;
    else b->halfmove++;

    b->side = (s8)-side;
    if (b->side == CH_WHITE) b->fullmove++;
}

void chessUnmakeMove(ChessBoard *b, const ChessMove *m, const ChessUndo *u)
{
    b->side = (s8)-b->side;
    if (b->side == CH_WHITE) b->fullmove--;

    s8 piece = b->board[m->to];
    if (m->flags & MF_PROMO) {
        piece = (s8)(b->side == CH_WHITE ? CH_PAWN : -CH_PAWN);
    }
    b->board[m->from] = piece;
    b->board[m->to] = 0;

    if (u->capturedSq >= 0) b->board[u->capturedSq] = u->captured;

    if (m->flags & MF_CASTLE) {
        int rank = CH_RANK(m->from);
        if (CH_FILE(m->to) == 6) {
            b->board[CH_SQ(rank, 7)] = b->board[CH_SQ(rank, 5)];
            b->board[CH_SQ(rank, 5)] = 0;
        } else {
            b->board[CH_SQ(rank, 0)] = b->board[CH_SQ(rank, 3)];
            b->board[CH_SQ(rank, 3)] = 0;
        }
    }

    b->castling = u->castling;
    b->ep = u->ep;
    b->halfmove = u->halfmove;
}

/* ------------------------------------------------- 4. attacks / legality */

int chessAttacked(const ChessBoard *b, int sq, int bySide)
{
    const int r = CH_RANK(sq);
    const int f = CH_FILE(sq);

    /* Pawns: a white pawn attacks "upwards", so look one rank below the
     * square (rank + 1 in our top-down indexing). */
    const int pawnDirs = (bySide == CH_WHITE) ? 1 : -1;
    for (int df = -1; df <= 1; df += 2) {
        int pr = r + pawnDirs;
        int pf = f + df;
        if (!inBounds(pr, pf)) continue;
        s8 p = b->board[CH_SQ(pr, pf)];
        if (p == (s8)(bySide * CH_PAWN)) return 1;
    }

    for (int k = 0; k < 8; k++) {
        int nr = r + knightOffsets[k][0];
        int nf = f + knightOffsets[k][1];
        if (!inBounds(nr, nf)) continue;
        if (b->board[CH_SQ(nr, nf)] == (s8)(bySide * CH_KNIGHT)) return 1;
    }

    for (int k = 0; k < 8; k++) {
        int nr = r + kingOffsets[k][0];
        int nf = f + kingOffsets[k][1];
        if (!inBounds(nr, nf)) continue;
        if (b->board[CH_SQ(nr, nf)] == (s8)(bySide * CH_KING)) return 1;
    }

    for (int pass = 0; pass < 2; pass++) {
        const int (*dirs)[2] = pass == 0 ? rookDirs : bishopDirs;
        const int sliderType = pass == 0 ? CH_ROOK : CH_BISHOP;
        for (int d = 0; d < 4; d++) {
            int nr = r + dirs[d][0];
            int nf = f + dirs[d][1];
            while (inBounds(nr, nf)) {
                s8 p = b->board[CH_SQ(nr, nf)];
                if (p) {
                    if (chessSideOf(p) == bySide) {
                        int t = chessTypeOf(p);
                        if (t == sliderType || t == CH_QUEEN) return 1;
                    }
                    break;
                }
                nr += dirs[d][0];
                nf += dirs[d][1];
            }
        }
    }

    return 0;
}

static int findKing(const ChessBoard *b, int side)
{
    for (int sq = 0; sq < 64; sq++) {
        if (b->board[sq] == (s8)(side * CH_KING)) return sq;
    }
    return -1;
}

int chessInCheck(const ChessBoard *b, int side)
{
    int king = findKing(b, side);
    if (king < 0) return 0;          /* no king: nothing to attack */
    return chessAttacked(b, king, -side);
}

int chessLegalMoves(const ChessBoard *b, ChessMove *out, int max)
{
    ChessMove pseudo[256];
    int n = chessPseudoMoves(b, pseudo, 256);
    int count = 0;

    ChessBoard work = *b;

    for (int i = 0; i < n; i++) {
        ChessUndo u;
        chessMakeMove(&work, &pseudo[i], &u);
        /* After the move it is the other side's turn, so `b->side` is the side
         * that just moved: its king must not be attacked. */
        int legal = !chessInCheck(&work, b->side);
        chessUnmakeMove(&work, &pseudo[i], &u);
        if (legal && count < max) out[count++] = pseudo[i];
    }
    return count;
}

/* ------------------------------------------------- 5. evaluation + search */

static int evaluate(const ChessBoard *b)
{
    int score = 0;

    for (int sq = 0; sq < 64; sq++) {
        s8 p = b->board[sq];
        if (!p) continue;

        int type = chessTypeOf(p);
        int value = pieceValue[type];
        int r = CH_RANK(sq);
        int f = CH_FILE(sq);

        if (type == CH_KNIGHT || type == CH_BISHOP) {
            /* Reward central squares a little; it stops the AI shuffling
             * pieces along the edge for ever. */
            value += (7 - abs(r * 2 - 7)) + (7 - abs(f * 2 - 7));
        } else if (type == CH_PAWN) {
            value += (p > 0) ? (6 - r) * 6 : (r - 1) * 6;
        } else if (type == CH_KING) {
            /* keep the king tucked away in the corner region early on */
            value += (r == 0 || r == 7) ? 10 : 0;
        }

        score += (p > 0) ? value : -value;
    }

    return (b->side == CH_WHITE) ? score : -score;
}

/* Captures first, biggest victim first: this is what makes alpha-beta cheap. */
static void orderMoves(ChessMove *moves, int n, const ChessBoard *b)
{
    int score[256];
    for (int i = 0; i < n; i++) {
        s8 victim = b->board[moves[i].to];
        s8 attacker = b->board[moves[i].from];
        score[i] = 0;
        if (victim) score[i] = 1000 + pieceValue[chessTypeOf(victim)] * 10
                                   - pieceValue[chessTypeOf(attacker)];
        if (moves[i].flags & MF_PROMO) score[i] += 800;
    }

    for (int i = 1; i < n; i++) {
        ChessMove mv = moves[i];
        int s = score[i];
        int j = i - 1;
        while (j >= 0 && score[j] < s) {
            moves[j + 1] = moves[j];
            score[j + 1] = score[j];
            j--;
        }
        moves[j + 1] = mv;
        score[j + 1] = s;
    }
}

#define NODE_BUDGET 20000

static int negamax(ChessBoard *b, int depth, int alpha, int beta, int *nodes,
                   int ply)
{
    if (++(*nodes) > NODE_BUDGET) return evaluate(b);
    if (depth <= 0) return evaluate(b);

    ChessMove moves[256];
    int n = chessLegalMoves(b, moves, 256);

    if (n == 0) {
        /* Mate is scored by distance so the AI prefers the faster mate. */
        if (chessInCheck(b, b->side)) return -29000 + ply;
        return 0;
    }

    orderMoves(moves, n, b);

    int best = -30000;
    for (int i = 0; i < n; i++) {
        ChessUndo u;
        chessMakeMove(b, &moves[i], &u);
        int value = -negamax(b, depth - 1, -beta, -alpha, nodes, ply + 1);
        chessUnmakeMove(b, &moves[i], &u);

        if (value > best) best = value;
        if (value > alpha) alpha = value;
        if (alpha >= beta) break;          /* cutoff */
        if (*nodes > NODE_BUDGET) break;
    }
    return best;
}

static u32 aiRng = 0x1F123BB5u;

static u32 aiRand(void)
{
    aiRng ^= aiRng << 13;
    aiRng ^= aiRng >> 17;
    aiRng ^= aiRng << 5;
    return aiRng;
}

int chessBestMove(ChessBoard *b, ChessMove *out, int depth)
{
    ChessMove moves[256];
    int n = chessLegalMoves(b, moves, 256);
    if (n == 0) return 0;

    orderMoves(moves, n, b);

    /* Shuffle a little so equal moves are not always played the same way. */
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(aiRand() % (u32)(i + 1));
        ChessMove tmp = moves[i];
        moves[i] = moves[j];
        moves[j] = tmp;
    }

    int bestScore = -30000;
    int bestIndex = 0;
    int nodes = 0;

    for (int i = 0; i < n; i++) {
        ChessUndo u;
        chessMakeMove(b, &moves[i], &u);
        int value = -negamax(b, depth - 1, -30000, -bestScore, &nodes, 1);
        chessUnmakeMove(b, &moves[i], &u);

        if (value > bestScore) {
            bestScore = value;
            bestIndex = i;
        }
    }

    *out = moves[bestIndex];
    return 1;
}

/* ----------------------------------------------------- 6. game wrapper */

static int insufficientMaterial(const ChessBoard *b)
{
    int minors = 0, others = 0;
    for (int sq = 0; sq < 64; sq++) {
        int type = chessTypeOf(b->board[sq]);
        if (type == CH_EMPTY || type == CH_KING) continue;
        if (type == CH_KNIGHT || type == CH_BISHOP) minors++;
        else others++;
    }
    return others == 0 && minors <= 1;
}

void chessNewGame(ChessGame *g)
{
    memset(g, 0, sizeof(*g));
    chessNewBoard(&g->board);
    g->selected = -1;
    g->result = CHESS_ONGOING;
    g->lastFrom = -1;
    g->lastTo = -1;
}

int chessUpdateResult(ChessGame *g)
{
    ChessMove moves[256];
    int n = chessLegalMoves(&g->board, moves, 256);

    if (n == 0) {
        if (chessInCheck(&g->board, g->board.side)) {
            g->result = (g->board.side == CH_WHITE) ? CHESS_BLACK_WIN
                                                    : CHESS_WHITE_WIN;
        } else {
            g->result = CHESS_STALEMATE;
        }
    } else if (g->board.halfmove >= 100 || insufficientMaterial(&g->board)) {
        g->result = CHESS_DRAW;
    }
    return g->result;
}

/* Rebuild the list of legal destinations for the selected piece. */
static void collectTargets(ChessGame *g)
{
    g->targetCount = 0;
    if (g->selected < 0) return;

    ChessMove moves[256];
    int n = chessLegalMoves(&g->board, moves, 256);

    for (int i = 0; i < n; i++) {
        if (moves[i].from != g->selected) continue;

        /* All four promotions land on the same square.  The destination list
         * only stores squares, so the piece is chosen afterwards. */
        int already = 0;
        for (int t = 0; t < g->targetCount; t++) {
            if (g->targets[t] == moves[i].to) { already = 1; break; }
        }
        if (!already && g->targetCount < CHESS_TARGET_MAX) {
            g->targets[g->targetCount++] = moves[i].to;
        }
    }
}

int chessTap(ChessGame *g, int square)
{
    if (g->result != CHESS_ONGOING) return 0;
    if (square < 0 || square > 63) return 0;
    if (g->board.side != CH_WHITE) return 0;      /* the human plays white */

    /* 1. a destination for the current selection? */
    if (g->selected >= 0) {
        for (int t = 0; t < g->targetCount; t++) {
            if (g->targets[t] != square) continue;

            ChessMove moves[256];
            int n = chessLegalMoves(&g->board, moves, 256);
            for (int i = 0; i < n; i++) {
                if (moves[i].from != g->selected || moves[i].to != square) continue;

                if (moves[i].flags & MF_PROMO) {
                    /* Let the player pick queen, rook, bishop or knight. */
                    g->promoPending = 1;
                    g->promoFrom = moves[i].from;
                    g->promoTo = moves[i].to;
                    g->selected = -1;
                    g->targetCount = 0;
                    return 1;
                }

                ChessUndo u;
                chessMakeMove(&g->board, &moves[i], &u);
                if (u.captured) {
                    g->score += pieceValue[chessTypeOf(u.captured)] / 10;
                }
                g->lastFrom = moves[i].from;
                g->lastTo = moves[i].to;
                g->selected = -1;
                g->targetCount = 0;
                chessUpdateResult(g);
                return 1;
            }
        }
    }

    /* 2. select one of our own pieces */
    s8 piece = g->board.board[square];
    if (piece > 0) {
        g->selected = square;
        collectTargets(g);
        return 1;
    }

    /* 3. tapping an empty square or an enemy piece with nothing selected */
    g->selected = -1;
    g->targetCount = 0;
    return 1;
}

int chessChoosePromotion(ChessGame *g, int promoType)
{
    if (!g->promoPending) return 0;
    if (promoType != CH_QUEEN && promoType != CH_ROOK &&
        promoType != CH_BISHOP && promoType != CH_KNIGHT) return 0;

    ChessMove moves[256];
    int n = chessLegalMoves(&g->board, moves, 256);

    for (int i = 0; i < n; i++) {
        if (moves[i].from != (u8)g->promoFrom) continue;
        if (moves[i].to != (u8)g->promoTo) continue;
        if (!(moves[i].flags & MF_PROMO)) continue;
        if (moves[i].promo != promoType) continue;

        ChessUndo u;
        chessMakeMove(&g->board, &moves[i], &u);
        if (u.captured) g->score += pieceValue[chessTypeOf(u.captured)] / 10;
        g->lastFrom = moves[i].from;
        g->lastTo = moves[i].to;
        g->promoPending = 0;
        g->selected = -1;
        g->targetCount = 0;
        chessUpdateResult(g);
        return 1;
    }

    g->promoPending = 0;
    return 0;
}

void chessPlayAi(ChessGame *g)
{
    if (g->result != CHESS_ONGOING) return;
    if (g->board.side != CH_BLACK) return;

    ChessMove move;
    if (!chessBestMove(&g->board, &move, 3)) {
        chessUpdateResult(g);
        return;
    }

    ChessUndo u;
    chessMakeMove(&g->board, &move, &u);
    g->lastFrom = move.from;
    g->lastTo = move.to;
    chessUpdateResult(g);
}

/* ------------------------------------------------------------ 7. drawing */

#define CH_CELL     20
#define CH_BOARD_X  6
#define CH_BOARD_Y  26
#define CH_PANEL_X  170

static const u16 boardLight = RGB15(24, 23, 19);
static const u16 boardDark  = RGB15(8, 12, 15);

static void drawPiece(Surface *s, int x, int y, s8 piece)
{
    static const char letters[7] = { '?', 'P', 'N', 'B', 'R', 'Q', 'K' };
    int type = chessTypeOf(piece);
    if (type < 1 || type > 6) return;

    int white = piece > 0;
    int cx = x + CH_CELL / 2;
    int cy = y + CH_CELL / 2;

    /* A soft disc behind the letter keeps both colours readable on both
     * square colours. */
    for (int dy = -8; dy <= 8; dy++) {
        int width = 8;
        int d = dy < 0 ? -dy : dy;
        if (d > 5) width = 8 - (d - 5) * 2;
        if (width <= 0) continue;
        surfHLine(s, cx - width, cy + dy, width * 2, white ? COL_TEXT : COL_BG);
    }
    surfFrame(s, cx - 8, cy - 8, 17, 17, COL_BORDER, 1);

    char label[2] = { letters[type], '\0' };
    surfTextCentered(s, cx, cy - 7, label, white ? COL_BG : COL_TEXT, 2);
}

int chessTouchSquare(int x, int y)
{
    if (x < CH_BOARD_X || y < CH_BOARD_Y) return -1;
    int f = (x - CH_BOARD_X) / CH_CELL;
    int r = (y - CH_BOARD_Y) / CH_CELL;
    if (f > 7 || r > 7) return -1;
    return CH_SQ(r, f);
}

int chessTouchResign(int x, int y)
{
    return x >= CH_PANEL_X && x < CH_PANEL_X + 80 &&
           y >= 152 && y < 152 + 28;
}

/* Promotion picker buttons, drawn over the board while a pawn is waiting. */
static const short promoRect[4][4] = {
    { 16,  96, 68, 26 },
    { 92,  96, 68, 26 },
    { 16, 128, 68, 26 },
    { 92, 128, 68, 26 },
};

int chessTouchPromotion(int x, int y)
{
    static const int pieces[4] = { CH_QUEEN, CH_ROOK, CH_BISHOP, CH_KNIGHT };

    for (int i = 0; i < 4; i++) {
        if (x >= promoRect[i][0] && x < promoRect[i][0] + promoRect[i][2] &&
            y >= promoRect[i][1] && y < promoRect[i][1] + promoRect[i][3])
            return pieces[i];
    }
    return -1;
}

static void formatTime(u32 ms, char *buf, int size)
{
    u32 total = ms / 1000;
    if (total > 99 * 60) total = 99 * 60;
    snprintf(buf, (size_t)size, "%02u:%02u", (unsigned)(total / 60),
             (unsigned)(total % 60));
}

void chessDrawGame(const ChessGame *g, Surface *bottom, Surface *top,
                   u32 timeLeftMs, int inCheck)
{
    char buf[40];

    /* ------------------------------ bottom: board + controls ------------ */
    surfClear(bottom, COL_BG);

    for (int r = 0; r < 8; r++) {
        for (int f = 0; f < 8; f++) {
            int sq = CH_SQ(r, f);
            int x = CH_BOARD_X + f * CH_CELL;
            int y = CH_BOARD_Y + r * CH_CELL;
            surfRect(bottom, x, y, CH_CELL, CH_CELL,
                     ((r + f) & 1) ? boardDark : boardLight);
            (void)sq;
        }
    }

    /* last move highlight */
    if (g->lastFrom >= 0) {
        int x = CH_BOARD_X + CH_FILE(g->lastFrom) * CH_CELL;
        int y = CH_BOARD_Y + CH_RANK(g->lastFrom) * CH_CELL;
        surfFrame(bottom, x, y, CH_CELL, CH_CELL, COL_BLUE, 2);
        x = CH_BOARD_X + CH_FILE(g->lastTo) * CH_CELL;
        y = CH_BOARD_Y + CH_RANK(g->lastTo) * CH_CELL;
        surfFrame(bottom, x, y, CH_CELL, CH_CELL, COL_BLUE, 2);
    }

    for (int sq = 0; sq < 64; sq++) {
        if (!g->board.board[sq]) continue;
        drawPiece(bottom, CH_BOARD_X + CH_FILE(sq) * CH_CELL,
                  CH_BOARD_Y + CH_RANK(sq) * CH_CELL, g->board.board[sq]);
    }

    /* legal destinations of the selected piece */
    for (int t = 0; t < g->targetCount; t++) {
        int x = CH_BOARD_X + CH_FILE(g->targets[t]) * CH_CELL;
        int y = CH_BOARD_Y + CH_RANK(g->targets[t]) * CH_CELL;
        if (g->board.board[g->targets[t]]) {
            surfFrame(bottom, x, y, CH_CELL, CH_CELL, COL_GOLD, 2);
        } else {
            surfRect(bottom, x + 8, y + 8, 4, 4, COL_GOLD);
        }
    }

    /* selected square */
    if (g->selected >= 0) {
        int x = CH_BOARD_X + CH_FILE(g->selected) * CH_CELL;
        int y = CH_BOARD_Y + CH_RANK(g->selected) * CH_CELL;
        surfFrame(bottom, x, y, CH_CELL, CH_CELL, COL_GOLD, 2);
    }

    surfFrame(bottom, CH_BOARD_X - 1, CH_BOARD_Y - 1,
              CH_CELL * 8 + 2, CH_CELL * 8 + 2, COL_BORDER, 1);

    /* right-hand panel */
    formatTime(timeLeftMs, buf, sizeof(buf));
    surfText(bottom, CH_PANEL_X, 26, "TIME", COL_TEXT_DIM, 1);
    surfText(bottom, CH_PANEL_X, 36, buf, timeLeftMs < 10000 ? COL_RED : COL_CYAN, 1);

    surfText(bottom, CH_PANEL_X, 58, "SCORE", COL_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "%d", g->score);
    surfText(bottom, CH_PANEL_X, 68, buf, COL_GOLD, 1);

    if (g->result != CHESS_ONGOING) {
        surfTextCentered(bottom, CH_PANEL_X + 40, 92, "GAME", COL_TEXT_DIM, 1);
        surfTextCentered(bottom, CH_PANEL_X + 40, 102, "OVER", COL_TEXT_DIM, 1);
    } else if (g->board.side == CH_WHITE) {
        surfTextCentered(bottom, CH_PANEL_X + 40, 92, inCheck ? "CHECK!" : "YOUR",
                         inCheck ? COL_RED : COL_GREEN, 1);
        surfTextCentered(bottom, CH_PANEL_X + 40, 102, inCheck ? "" : "MOVE",
                         COL_GREEN, 1);
    } else {
        surfTextCentered(bottom, CH_PANEL_X + 40, 92, "BLACK", COL_TEXT_DIM, 1);
        surfTextCentered(bottom, CH_PANEL_X + 40, 102, "MOVES", COL_TEXT_DIM, 1);
    }

    surfCard(bottom, CH_PANEL_X, 152, 80, 28, COL_PANEL, COL_RED, 0);
    surfTextCentered(bottom, CH_PANEL_X + 40, 161, "RESIGN", COL_RED, 1);

    /* Promotion picker: covers the board until a piece is chosen. */
    if (g->promoPending) {
        static const char *names[4] = { "QUEEN", "ROOK", "BISHOP", "KNIGHT" };

        surfRect(bottom, 8, 58, 156, 108, COL_PANEL);
        surfFrame(bottom, 8, 58, 156, 108, COL_GOLD, 2);
        surfTextCentered(bottom, 86, 64, "PROMOTE TO", COL_GOLD, 1);

        for (int i = 0; i < 4; i++) {
            const short *r = promoRect[i];
            surfCard(bottom, r[0], r[1], r[2], r[3], COL_PANEL_HI, COL_GOLD, 0);
            surfTextCentered(bottom, r[0] + r[2] / 2, r[1] + 9, names[i],
                             COL_TEXT, 1);
        }
    }

    /* -------------------------------- top: clock, score, help ----------- */
    surfClear(top, COL_BG);
    surfRect(top, 0, 0, SCREEN_W, 18, COL_PANEL);
    surfText(top, 6, 5, "CHESS", COL_GOLD, 1);
    surfText(top, 200, 5, "REWARD MODE", COL_TEXT_DIM, 1);

    formatTime(timeLeftMs, buf, sizeof(buf));
    surfText(top, 8, 26, "TIME LEFT", COL_TEXT_DIM, 1);
    surfText(top, 8, 36, buf, timeLeftMs < 10000 ? COL_RED : COL_CYAN, 3);

    surfText(top, 150, 26, "SCORE", COL_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "%d", g->score);
    surfText(top, 150, 36, buf, COL_GOLD, 2);

    const char *status;
    u16 statusColor = COL_TEXT;
    if (g->promoPending) { status = "CHOOSE A PROMOTION"; statusColor = COL_GOLD; }
    else if (g->result == CHESS_BLACK_WIN) { status = "CHECKMATE - BLACK WINS"; statusColor = COL_RED; }
    else if (g->result == CHESS_WHITE_WIN) { status = "CHECKMATE - YOU WIN!"; statusColor = COL_GREEN; }
    else if (g->result == CHESS_STALEMATE) { status = "STALEMATE"; statusColor = COL_TEXT_DIM; }
    else if (g->result == CHESS_DRAW) { status = "DRAW"; statusColor = COL_TEXT_DIM; }
    else if (inCheck) { status = "YOU ARE IN CHECK"; statusColor = COL_RED; }
    else if (g->board.side == CH_WHITE) { status = "YOUR MOVE"; statusColor = COL_GREEN; }
    else { status = "BLACK IS THINKING"; statusColor = COL_TEXT_DIM; }

    surfRect(top, 0, 74, SCREEN_W, 1, COL_BORDER);
    surfText(top, 8, 82, status, statusColor, 1);

    surfText(top, 8, 100, "Tap a white piece, then tap a", COL_TEXT_DIM, 1);
    surfText(top, 8, 112, "gold square to move it.", COL_TEXT_DIM, 1);
    surfText(top, 8, 124, "Promote to queen, rook, bishop", COL_TEXT_DIM, 1);
    surfText(top, 8, 132, "or knight when a pawn reaches the end.", COL_TEXT_DIM, 1);
    surfText(top, 8, 144, "RESIGN returns to the menu.", COL_TEXT_DIM, 1);

    surfRect(top, 0, 168, SCREEN_W, 1, COL_BORDER);
    surfText(top, 8, 176, "SELECT Quit", COL_TEXT_DIM, 1);
}

void chessDrawResult(const ChessGame *g, Surface *bottom, Surface *top)
{
    char buf[48];

    surfClear(top, COL_BG);
    surfRect(top, 0, 0, SCREEN_W, 22, COL_PANEL);
    surfText(top, 8, 7, "CHESS", COL_GOLD, 1);
    surfText(top, 160, 7, "GAME OVER", COL_TEXT_DIM, 1);

    const char *title;
    u16 color;
    switch (g->result) {
    case CHESS_WHITE_WIN: title = "CHECKMATE"; color = COL_GREEN; break;
    case CHESS_BLACK_WIN: title = "CHECKMATE"; color = COL_RED; break;
    case CHESS_STALEMATE: title = "STALEMATE"; color = COL_TEXT_DIM; break;
    case CHESS_DRAW: title = "DRAW"; color = COL_TEXT_DIM; break;
    case CHESS_TIMEOUT: title = "OUT OF TIME"; color = COL_RED; break;
    case CHESS_RESIGNED: title = "RESIGNED"; color = COL_RED; break;
    default: title = "GAME OVER"; color = COL_TEXT; break;
    }

    surfTextCentered(top, 128, 36, title, color, 2);

    const char *subtitle = "";
    if (g->result == CHESS_WHITE_WIN) subtitle = "you beat the computer";
    else if (g->result == CHESS_BLACK_WIN) subtitle = "the computer wins this one";
    else if (g->result == CHESS_STALEMATE) subtitle = "no legal moves, but no check";
    else if (g->result == CHESS_DRAW) subtitle = "nothing left to fight with";
    else if (g->result == CHESS_TIMEOUT) subtitle = "the game time ran out";
    else if (g->result == CHESS_RESIGNED) subtitle = "you ended the game early";
    surfTextCentered(top, 128, 58, subtitle, COL_TEXT_DIM, 1);

    surfText(top, 24, 84, "SCORE", COL_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "%d", g->score);
    surfText(top, 24, 96, buf, COL_GOLD, 2);

    surfText(top, 148, 84, "XP EARNED", COL_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "%d", g->score / 10);
    surfText(top, 148, 96, buf, COL_TEXT, 2);

    surfTextCentered(top, 128, 140, "Answer questions to refill", COL_TEXT_DIM, 1);
    surfTextCentered(top, 128, 152, "the game time bank.", COL_TEXT_DIM, 1);

    surfClear(bottom, COL_BG);
    surfCard(bottom, 8, 72, 240, 48, COL_PANEL, COL_BORDER, 0);
    surfTextCentered(bottom, 128, 84, "GAME OVER", COL_RED, 1);
    surfTextCentered(bottom, 128, 100, "TAP TO RETURN", COL_TEXT, 1);
}
