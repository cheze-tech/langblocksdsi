/* -------------------------------------------------------------------------
 * quiz.h -- builds the four kinds of question used by the game.
 *
 * All questions are generated at runtime from data.c and conjugate.c, so the
 * player never sees exactly the same drill twice and the distractors are
 * always related to the answer (other forms of the same verb, other endings of
 * the same tense, other words from the same topic).
 * ------------------------------------------------------------------------- */
#ifndef QUIZ_H
#define QUIZ_H

#include "data.h"

typedef enum {
    QK_FLASH = 0,   /* translate a word, 4 options                    */
    QK_BLANK = 1,   /* tap the word that completes the sentence      */
    QK_TENSE = 2,   /* conjugate a verb in the given tense           */
    QK_ENDING = 3,  /* pick the ending for this person/tense         */
    QK_COUNT = 4
} QuestionKind;

/* Drill selection bitmask: 1 << QK_*.  The setup screen toggles these and
 * quizMake() only ever produces the kinds that are enabled. */
#define QK_MASK(k)   (1u << (unsigned)(k))
#define QK_MASK_ALL  (QK_MASK(QK_FLASH) | QK_MASK(QK_BLANK) | \
                      QK_MASK(QK_TENSE) | QK_MASK(QK_ENDING))

typedef struct {
    QuestionKind kind;
    char label[32];        /* header strip, e.g. "FILL THE GAP"          */
    char prompt[192];      /* the word / sentence / infinitive to solve  */
    char hint[72];         /* second line: tense, pronoun, translation   */
    char options[4][56];
    char answer[56];
    int correct;           /* index into options[] of the right answer   */
    char explanation[320]; /* shown after a wrong answer or a timeout    */
    int big;               /* draw the prompt at size 2 (short words)    */
} Question;

/* Seed the internal xorshift generator (the DSi has no RTC, so the caller
 * mixes in a frame counter / key state). */
void quizSeed(u32 seed);

/* Build one question of one of the enabled kinds (mask of QK_MASK(...)).
 * A zero mask means "all four", so the generator can never return nothing. */
void quizMake(Lang lang, unsigned int kinds, Question *q);

#endif /* QUIZ_H */
