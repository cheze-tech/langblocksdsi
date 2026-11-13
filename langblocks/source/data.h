/* -------------------------------------------------------------------------
 * data.h -- the vocabulary, sentence and verb content for both languages.
 *
 * The Spanish list follows the Pearson Edexcel GCSE Spanish vocabulary
 * (core greetings/numbers/time/colours plus the school, work and opinion
 * words needed for these drills).  The French list follows the AQA GCSE
 * French "Education and work" booklet, which is the theme both players are
 * studying.  Everything is plain C string data so it can live in the ROM and
 * needs no file loading at runtime.
 * ------------------------------------------------------------------------- */
#ifndef DATA_H
#define DATA_H

#include "platform.h"

typedef enum { LANG_ES = 0, LANG_FR = 1, LANG_COUNT = 2 } Lang;

/* A flashcard pair: English prompt -> target-language answer. */
typedef struct {
    const char *en;
    const char *tgt;
} VocabItem;

/* A fill-in-the-blank sentence.  `sentence` contains "___" where the answer
 * goes.  opts[0] is always the correct answer and the four options are unique,
 * so the quiz can shuffle them freely. */
typedef struct {
    const char *sentence;
    const char *answer;
    const char *en;
    const char *opts[4];
} BlankItem;

/* A verb the drill can conjugate.
 * `group` means:  0 = -ar / -er / -er      (first conjugation)
 *                 1 = -er / -ir / -ir      (second conjugation)
 *                 2 = -ir / -ir / -re      (third conjugation)
 * i.e. its meaning depends on the language, see conjugate.c. */
typedef struct {
    const char *inf;
    const char *en;
    unsigned char group;
} VerbItem;

typedef struct {
    const char *name;          /* shown in menus, e.g. "ESPAÑOL"     */
    const char *code;          /* two letters, e.g. "ES"             */
    const char *flagLine;      /* small subtitle under the name      */
    const VocabItem *vocab;
    int vocabCount;
    const BlankItem *blanks;
    int blankCount;
    const VerbItem *verbs;
    int verbCount;
} LangPack;

extern const LangPack langPacks[LANG_COUNT];

#endif /* DATA_H */
