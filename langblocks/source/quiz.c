/* -------------------------------------------------------------------------
 * quiz.c -- question generation.
 *
 * Every question is built in four steps:
 *   1. pick the subject (a word, a sentence, or a verb+tense+person),
 *   2. compute the correct answer,
 *   3. gather three plausible wrong answers,
 *   4. shuffle, and keep a pointer to where the right answer ended up.
 * The explanation is generated at the same time, because that is the moment
 * when we still know exactly which rule was tested.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>

#include "quiz.h"
#include "conjugate.h"

static u32 rngState = 0x9E3779B9u;

void quizSeed(u32 seed)
{
    rngState = seed ? seed : 0x9E3779B9u;
}

static u32 rnd(void)
{
    /* xorshift32: cheap, no library, good enough for shuffling options. */
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

static int rndInt(int n)
{
    if (n <= 0) return 0;
    return (int)(rnd() % (u32)n);
}

static void setText(char *dst, int size, const char *src)
{
    /* snprintf truncates safely and always NUL-terminates. */
    snprintf(dst, (size_t)size, "%s", src);
}

static void setOption(Question *q, int i, const char *text)
{
    setText(q->options[i], sizeof(q->options[i]), text);
}

/* Shuffle the four options and remember where the right one landed. */
static void finishOptions(Question *q)
{
    for (int i = 3; i > 0; i--) {
        int j = rndInt(i + 1);
        char tmp[56];
        memcpy(tmp, q->options[i], sizeof(tmp));
        memcpy(q->options[i], q->options[j], sizeof(tmp));
        memcpy(q->options[j], tmp, sizeof(tmp));
    }
    q->correct = -1;
    for (int i = 0; i < 4; i++) {
        if (strcmp(q->options[i], q->answer) == 0) {
            q->correct = i;
            break;
        }
    }
    if (q->correct < 0) {
        /* Should never happen, but never leave the game without a valid index. */
        setOption(q, 0, q->answer);
        q->correct = 0;
    }
}

static int alreadyUsed(const Question *q, int count, const char *text)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(q->options[i], text) == 0) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------- flashcards */

static void makeFlashcard(Lang lang, Question *q)
{
    const LangPack *p = &langPacks[lang];
    int idx = rndInt(p->vocabCount);

    q->kind = QK_FLASH;
    snprintf(q->label, sizeof(q->label), "FLASHCARD  %s", p->code);
    setText(q->prompt, sizeof(q->prompt), p->vocab[idx].en);
    setText(q->hint, sizeof(q->hint),
            lang == LANG_ES ? "How do you say this in Spanish?"
                            : "How do you say this in French?");
    setText(q->answer, sizeof(q->answer), p->vocab[idx].tgt);
    /* The exam list has long headwords; only short prompts fit the big font. */
    q->big = strlen(p->vocab[idx].en) <= 18;

    setOption(q, 0, q->answer);
    int made = 1;
    int guard = 0;
    while (made < 4 && guard++ < 200) {
        int other = rndInt(p->vocabCount);
        const char *cand = p->vocab[other].tgt;
        if (alreadyUsed(q, made, cand)) continue;
        setOption(q, made, cand);
        made++;
    }
    /* Fill any leftovers (tiny vocabulary) so we always have 4 options. */
    while (made < 4) {
        setOption(q, made, "————");
        made++;
    }

    finishOptions(q);
    snprintf(q->explanation, sizeof(q->explanation),
             "\"%s\" is \"%s\" in %s. Copy the spelling exactly, accents "
             "included.",
             p->vocab[idx].en, p->vocab[idx].tgt,
             lang == LANG_ES ? "Spanish" : "French");
}

/* ---------------------------------------------------------- gap fillers */

static void makeBlank(Lang lang, Question *q)
{
    const LangPack *p = &langPacks[lang];
    int idx = rndInt(p->blankCount);
    const BlankItem *b = &p->blanks[idx];

    q->kind = QK_BLANK;
    snprintf(q->label, sizeof(q->label), "FILL THE GAP  %s", p->code);
    setText(q->prompt, sizeof(q->prompt), b->sentence);
    setText(q->hint, sizeof(q->hint), b->en);
    setText(q->answer, sizeof(q->answer), b->answer);
    q->big = 0;

    for (int i = 0; i < 4; i++) setOption(q, i, b->opts[i]);

    finishOptions(q);
    snprintf(q->explanation, sizeof(q->explanation),
             "\"%s\" fits here: %s", b->answer, b->en);
}

/* ------------------------------------------------------- conjugation drill */

/* Choose a verb, preferring regular ones so the endings pattern is visible. */
static const VerbItem *pickVerb(Lang lang)
{
    const LangPack *p = &langPacks[lang];
    for (int attempt = 0; attempt < 12; attempt++) {
        const VerbItem *v = &p->verbs[rndInt(p->verbCount)];
        /* ~30% of the time keep whatever came up, including irregulars. */
        if (!conjIsIrregular(lang, v->inf) || rndInt(100) < 30) return v;
    }
    return &p->verbs[rndInt(p->verbCount)];
}

/* Pick a tense that has a full set of forms in this language. */
static Tense pickTense(Lang lang)
{
    (void)lang;               /* both languages have the same four tenses */
    return (Tense)rndInt(TENSE_COUNT);
}

static void makeTenseDrill(Lang lang, Question *q)
{
    const VerbItem *v = pickVerb(lang);
    Tense t = pickTense(lang);
    int person = rndInt(PERSON_COUNT);

    q->kind = QK_TENSE;
    snprintf(q->label, sizeof(q->label), "TENSE DRILL  %s",
             langPacks[lang].code);
    setText(q->prompt, sizeof(q->prompt), v->inf);
    snprintf(q->hint, sizeof(q->hint), "%s . %s . %s", conjTenseName(lang, t),
             conjPronoun(lang, person), v->en);
    q->big = 1;

    conjForm(lang, v->inf, t, person, q->answer, sizeof(q->answer));
    setOption(q, 0, q->answer);

    /* Distractors: the same verb in the same tense but other persons.  Those
     * are the forms the player is most likely to confuse, and seeing them
     * together teaches the ending pattern. */
    int made = 1;
    for (int tries = 0; tries < 40 && made < 4; tries++) {
        int other = rndInt(PERSON_COUNT);
        char form[48];
        conjForm(lang, v->inf, t, other, form, sizeof(form));
        if (alreadyUsed(q, made, form)) continue;
        setOption(q, made, form);
        made++;
    }
    /* Still short?  Borrow this person's form from another tense: that catches
     * people who know the person but mix up the tenses. */
    for (int tries = 0; tries < 40 && made < 4; tries++) {
        Tense other = (Tense)rndInt(TENSE_COUNT);
        char form[48];
        conjForm(lang, v->inf, other, person, form, sizeof(form));
        if (alreadyUsed(q, made, form)) continue;
        setOption(q, made, form);
        made++;
    }
    while (made < 4) {
        setOption(q, made, "————");
        made++;
    }

    finishOptions(q);
    /* conjRule returns a pointer to one of its rotating static buffers. */
    setText(q->explanation, sizeof(q->explanation),
            conjRule(lang, v->inf, t, person));
}

/* ---------------------------------------------------------- ending drill */

static void makeEndingDrill(Lang lang, Question *q)
{
    /* Only regular verbs have a single ending worth drilling. */
    const LangPack *p = &langPacks[lang];
    const VerbItem *v = NULL;
    for (int attempt = 0; attempt < 40; attempt++) {
        const VerbItem *cand = &p->verbs[rndInt(p->verbCount)];
        if (conjIsIrregular(lang, cand->inf)) continue;

        /* Skip verbs with spelling changes: their ending is not the one the
         * tense table teaches, which would make a confusing drill. */
        int len = (int)strlen(cand->inf);
        const char *tail = (len >= 3) ? cand->inf + len - 3 : cand->inf;
        if (lang == LANG_ES && (strcmp(tail, "gar") == 0 ||
                                strcmp(tail, "car") == 0 ||
                                strcmp(tail, "zar") == 0)) continue;
        if (lang == LANG_FR && strcmp(tail, "ger") == 0) continue;

        v = cand;
        break;
    }
    if (!v) v = &p->verbs[0];

    /* Pick a tense that actually has endings (not the passé composé). */
    Tense t = TENSE_PRESENT;
    for (int attempt = 0; attempt < 12; attempt++) {
        Tense cand = pickTense(lang);
        if (conjHasEndings(lang, cand)) { t = cand; break; }
    }
    /* The -re present has no ending in the il/elle form; skip that person so
     * the drill always has a real ending to offer. */
    int person = rndInt(PERSON_COUNT);
    for (int attempt = 0; attempt < 8; attempt++) {
        char probe[24];
        conjEnding(lang, v->inf, t, person, probe, sizeof(probe));
        if (probe[0]) break;
        person = (person + 1) % PERSON_COUNT;
    }

    q->kind = QK_ENDING;
    snprintf(q->label, sizeof(q->label), "VERB ENDINGS  %s",
             langPacks[lang].code);
    setText(q->prompt, sizeof(q->prompt), "Which ending?");
    snprintf(q->hint, sizeof(q->hint), "%s . %s . %s", v->inf,
             conjTenseName(lang, t), conjPronoun(lang, person));
    q->big = 0;

    char ending[24];
    conjEnding(lang, v->inf, t, person, ending, sizeof(ending));
    if (ending[0] == '\0') setText(ending, sizeof(ending), "-");
    setText(q->answer, sizeof(q->answer), ending);
    setOption(q, 0, q->answer);

    int made = 1;
    for (int tries = 0; tries < 40 && made < 4; tries++) {
        int other = rndInt(PERSON_COUNT);
        char e[24];
        conjEnding(lang, v->inf, t, other, e, sizeof(e));
        if (e[0] == '\0') continue;
        if (alreadyUsed(q, made, e)) continue;
        setOption(q, made, e);
        made++;
    }
    while (made < 4) {
        setOption(q, made, "—");
        made++;
    }

    finishOptions(q);
    snprintf(q->explanation, sizeof(q->explanation),
             "%s (%s) . %s: the '%s' ending is %s, giving %s.", v->inf,
             conjTenseName(lang, t), conjEndingList(lang, v->inf, t),
             conjPronoun(lang, person), q->answer,
             (conjForm(lang, v->inf, t, person, ending, sizeof(ending)), ending));
}

/* ---------------------------------------------------------------- public */

void quizMake(Lang lang, unsigned int kinds, Question *q)
{
    if (lang < 0 || lang >= LANG_COUNT) lang = LANG_ES;
    memset(q, 0, sizeof(*q));

    /* Relative weights: vocabulary comes up most often, but the grammar
     * drills are always represented when they are enabled. */
    static const int weights[QK_COUNT] = { 40, 22, 24, 14 };

    if ((kinds & QK_MASK_ALL) == 0) kinds = QK_MASK_ALL;

    int enabled[QK_COUNT];
    int enabledCount = 0;
    int totalWeight = 0;
    for (int k = 0; k < QK_COUNT; k++) {
        if (!(kinds & QK_MASK(k))) continue;
        enabled[enabledCount++] = k;
        totalWeight += weights[k];
    }

    int kind = enabled[0];
    int roll = rndInt(totalWeight);
    int acc = 0;
    for (int i = 0; i < enabledCount; i++) {
        acc += weights[enabled[i]];
        if (roll < acc) {
            kind = enabled[i];
            break;
        }
    }

    switch (kind) {
    case QK_BLANK:  makeBlank(lang, q); break;
    case QK_TENSE:  makeTenseDrill(lang, q); break;
    case QK_ENDING: makeEndingDrill(lang, q); break;
    default:        makeFlashcard(lang, q); break;
    }
}
