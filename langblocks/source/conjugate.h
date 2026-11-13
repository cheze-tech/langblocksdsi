/* -------------------------------------------------------------------------
 * conjugate.h -- the grammar engine.
 *
 * Given an infinitive, a tense and a person, produce the correct form, the
 * ending that produced it, and a short English explanation of the rule.  The
 * quiz uses this for both the tense drills and the "which ending?" drills, so
 * every grammar question is generated from one source of truth instead of
 * hundreds of hand-typed multiple-choice items.
 *
 * Tenses: Spanish gets presente / pretérito / imperfecto / futuro, French gets
 * présent / passé composé / imparfait / futur -- the four the specification
 * asks for in each language.
 * ------------------------------------------------------------------------- */
#ifndef CONJUGATE_H
#define CONJUGATE_H

#include "data.h"

typedef enum {
    TENSE_PRESENT = 0,
    TENSE_PRETERITE = 1,   /* Spanish preterite / French passé composé */
    TENSE_IMPERFECT = 2,
    TENSE_FUTURE = 3,
    TENSE_COUNT = 4
} Tense;

#define PERSON_COUNT 6

/* Conjugated form only, e.g. "hablamos" or "avons parlé". */
void conjForm(Lang lang, const char *inf, Tense t, int person, char *out,
              int outSize);

/* Subject pronoun, e.g. "nosotros" / "nous". */
const char *conjPronoun(Lang lang, int person);

/* Pronoun + form, adding the French elision ("je" + "ai parlé" -> "j'ai parlé"). */
void conjPhrase(Lang lang, int person, const char *form, char *out, int size);

/* "Presente" / "Présent" -- the tense name as taught in the target language. */
const char *conjTenseName(Lang lang, Tense t);

/* "present" / "passé composé" -- the English label used in explanations. */
const char *conjTenseNameEn(Lang lang, Tense t);

int conjIsIrregular(Lang lang, const char *inf);

/* 0/1/2 = first/second/third conjugation, see VerbItem in data.h. */
int conjGroup(Lang lang, const char *inf);

/* Ending added to the stem for this person, with a leading '-'.  Writes "" when
 * the tense has no meaningful ending (the French passé composé). */
void conjEnding(Lang lang, const char *inf, Tense t, int person, char *out,
                int size);

/* "-o, -as, -a, -amos, -áis, -an" for the whole tense, or "" if not applicable. */
const char *conjEndingList(Lang lang, const char *inf, Tense t);

/* Full English explanation of why this form is what it is.  Returns a pointer
 * into a small ring of static buffers, so it is safe to hold a couple of
 * results at once but not to store them long-term. */
const char *conjRule(Lang lang, const char *inf, Tense t, int person);

/* Convenience: number of tenses that make sense for "which ending?" drills. */
int conjHasEndings(Lang lang, Tense t);

#endif /* CONJUGATE_H */
