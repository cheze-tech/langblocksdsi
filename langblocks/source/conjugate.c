/* -------------------------------------------------------------------------
 * conjugate.c -- Spanish and French conjugation, plus the rule text shown
 * after a wrong answer.
 *
 * The tables below are the whole grammar syllabus of the app:
 *
 *   Spanish  presente, pretérito, imperfecto, futuro
 *   French   présent, passé composé, imparfait, futur
 *
 * Regular verbs are derived from endings; the irregular ones the two players
 * meet most often are stored as explicit six-form tables.  Every string is
 * UTF-8, matching the glyphs baked into fonts/font5x7.h.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>

#include "conjugate.h"

/* ------------------------------------------------------------- pronouns */

static const char *esPronouns[PERSON_COUNT] = {
    "yo", "tú", "él / ella", "nosotros", "vosotros", "ellos / ellas"
};
static const char *frPronouns[PERSON_COUNT] = {
    "je", "tu", "il / elle", "nous", "vous", "ils / elles"
};

/* --------------------------------------------------- Spanish endings */

/* [group][tense][person]; group 0 = -ar, 1 = -er, 2 = -ir.
 * The future tense appends its endings to the whole infinitive, so its "stem"
 * is one character longer (handled in conjForm). */
static const char *esEndings[3][TENSE_COUNT][PERSON_COUNT] = {
    {   /* -ar: hablar */
        {"o", "as", "a", "amos", "áis", "an"},
        {"é", "aste", "ó", "amos", "asteis", "aron"},
        {"aba", "abas", "aba", "ábamos", "abais", "aban"},
        {"é", "ás", "á", "emos", "éis", "án"},
    },
    {   /* -er: comer */
        {"o", "es", "e", "emos", "éis", "en"},
        {"í", "iste", "ió", "imos", "isteis", "ieron"},
        {"ía", "ías", "ía", "íamos", "íais", "ían"},
        {"é", "ás", "á", "emos", "éis", "án"},
    },
    {   /* -ir: vivir */
        {"o", "es", "e", "imos", "ís", "en"},
        {"í", "iste", "ió", "imos", "isteis", "ieron"},
        {"ía", "ías", "ía", "íamos", "íais", "ían"},
        {"é", "ás", "á", "emos", "éis", "án"},
    },
};

/* ---------------------------------------------------- French endings */

/* group 0 = -er, 1 = -ir (finir type), 2 = -re.
 * The imperfect uses the "nous" stem, the future the infinitive, and the
 * passé composé is built from an auxiliary + past participle, so only the
 * present and imperfect have a plain ending table here. */
static const char *frPresentEnd[3][PERSON_COUNT] = {
    {"e", "es", "e", "ons", "ez", "ent"},
    {"is", "is", "it", "issons", "issez", "issent"},
    {"s", "s", "", "ons", "ez", "ent"},
};
static const char *frImparfaitEnd[PERSON_COUNT] = {
    "ais", "ais", "ait", "ions", "iez", "aient"
};
static const char *frFuturEnd[PERSON_COUNT] = {
    "ai", "as", "a", "ons", "ez", "ont"
};

/* --------------------------------------------------- irregular verbs */

typedef struct {
    const char *inf;
    const char *pres[PERSON_COUNT];
    const char *pret[PERSON_COUNT];
    const char *imp[PERSON_COUNT];
    const char *fut[PERSON_COUNT];
} IrrVerb;

static const IrrVerb esIrregular[] = {
    {"ser", {"soy", "eres", "es", "somos", "sois", "son"},
            {"fui", "fuiste", "fue", "fuimos", "fuisteis", "fueron"},
            {"era", "eras", "era", "éramos", "erais", "eran"},
            {"seré", "serás", "será", "seremos", "seréis", "serán"}},
    {"estar", {"estoy", "estás", "está", "estamos", "estáis", "están"},
              {"estuve", "estuviste", "estuvo", "estuvimos", "estuvisteis", "estuvieron"},
              {"estaba", "estabas", "estaba", "estábamos", "estabais", "estaban"},
              {"estaré", "estarás", "estará", "estaremos", "estaréis", "estarán"}},
    {"ir", {"voy", "vas", "va", "vamos", "vais", "van"},
           {"fui", "fuiste", "fue", "fuimos", "fuisteis", "fueron"},
           {"iba", "ibas", "iba", "íbamos", "ibais", "iban"},
           {"iré", "irás", "irá", "iremos", "iréis", "irán"}},
    {"tener", {"tengo", "tienes", "tiene", "tenemos", "tenéis", "tienen"},
              {"tuve", "tuviste", "tuvo", "tuvimos", "tuvisteis", "tuvieron"},
              {"tenía", "tenías", "tenía", "teníamos", "teníais", "tenían"},
              {"tendré", "tendrás", "tendrá", "tendremos", "tendréis", "tendrán"}},
    {"hacer", {"hago", "haces", "hace", "hacemos", "hacéis", "hacen"},
              {"hice", "hiciste", "hizo", "hicimos", "hicisteis", "hicieron"},
              {"hacía", "hacías", "hacía", "hacíamos", "hacíais", "hacían"},
              {"haré", "harás", "hará", "haremos", "haréis", "harán"}},
    {"poder", {"puedo", "puedes", "puede", "podemos", "podéis", "pueden"},
              {"pude", "pudiste", "pudo", "pudimos", "pudisteis", "pudieron"},
              {"podía", "podías", "podía", "podíamos", "podíais", "podían"},
              {"podré", "podrás", "podrá", "podremos", "podréis", "podrán"}},
    {"querer", {"quiero", "quieres", "quiere", "queremos", "queréis", "quieren"},
               {"quise", "quisiste", "quiso", "quisimos", "quisisteis", "quisieron"},
               {"quería", "querías", "quería", "queríamos", "queríais", "querían"},
               {"querré", "querrás", "querrá", "querremos", "querréis", "querrán"}},
    {"venir", {"vengo", "vienes", "viene", "venimos", "venís", "vienen"},
              {"vine", "viniste", "vino", "vinimos", "vinisteis", "vinieron"},
              {"venía", "venías", "venía", "veníamos", "veníais", "venían"},
              {"vendré", "vendrás", "vendrá", "vendremos", "vendréis", "vendrán"}},
};

static const IrrVerb frIrregular[] = {
    {"être", {"suis", "es", "est", "sommes", "êtes", "sont"},
             {"ai été", "as été", "a été", "avons été", "avez été", "ont été"},
             {"étais", "étais", "était", "étions", "étiez", "étaient"},
             {"serai", "seras", "sera", "serons", "serez", "seront"}},
    {"avoir", {"ai", "as", "a", "avons", "avez", "ont"},
              {"ai eu", "as eu", "a eu", "avons eu", "avez eu", "ont eu"},
              {"avais", "avais", "avait", "avions", "aviez", "avaient"},
              {"aurai", "auras", "aura", "aurons", "aurez", "auront"}},
    {"aller", {"vais", "vas", "va", "allons", "allez", "vont"},
              {"suis allé", "es allé", "est allé", "sommes allés", "êtes allés", "sont allés"},
              {"allais", "allais", "allait", "allions", "alliez", "allaient"},
              {"irai", "iras", "ira", "irons", "irez", "iront"}},
    {"faire", {"fais", "fais", "fait", "faisons", "faites", "font"},
              {"ai fait", "as fait", "a fait", "avons fait", "avez fait", "ont fait"},
              {"faisais", "faisais", "faisait", "faisions", "faisiez", "faisaient"},
              {"ferai", "feras", "fera", "ferons", "ferez", "feront"}},
    {"prendre", {"prends", "prends", "prend", "prenons", "prenez", "prennent"},
                {"ai pris", "as pris", "a pris", "avons pris", "avez pris", "ont pris"},
                {"prenais", "prenais", "prenait", "prenions", "preniez", "prenaient"},
                {"prendrai", "prendras", "prendra", "prendrons", "prendrez", "prendront"}},
    {"venir", {"viens", "viens", "vient", "venons", "venez", "viennent"},
              {"suis venu", "es venu", "est venu", "sommes venus", "êtes venus", "sont venus"},
              {"venais", "venais", "venait", "venions", "veniez", "venaient"},
              {"viendrai", "viendras", "viendra", "viendrons", "viendrez", "viendront"}},
    {"pouvoir", {"peux", "peux", "peut", "pouvons", "pouvez", "peuvent"},
                {"ai pu", "as pu", "a pu", "avons pu", "avez pu", "ont pu"},
                {"pouvais", "pouvais", "pouvait", "pouvions", "pouviez", "pouvaient"},
                {"pourrai", "pourras", "pourra", "pourrons", "pourrez", "pourront"}},
    {"vouloir", {"veux", "veux", "veut", "voulons", "voulez", "veulent"},
                {"ai voulu", "as voulu", "a voulu", "avons voulu", "avez voulu", "ont voulu"},
                {"voulais", "voulais", "voulait", "voulions", "vouliez", "voulaient"},
                {"voudrai", "voudras", "voudra", "voudrons", "voudrez", "voudront"}},
};

/* French verbs that take être in the passé composé: their participle agrees
 * with the subject, so "nous sommes allés" (masculine/plural). */
static const char *frEtreVerbs[] = {
    "aller", "venir", "arriver", "descendre", "entrer", "sortir", "rester",
    "monter", "tomber", "partir", "devenir", "revenir", "naître", "mourir",
    "rentrer", "retourner",
};

/* ------------------------------------------------------------- helpers */

static const IrrVerb *findIrregular(Lang lang, const char *inf)
{
    const IrrVerb *table = (lang == LANG_ES) ? esIrregular : frIrregular;
    int count = (lang == LANG_ES)
                    ? (int)(sizeof(esIrregular) / sizeof(esIrregular[0]))
                    : (int)(sizeof(frIrregular) / sizeof(frIrregular[0]));
    for (int i = 0; i < count; i++) {
        if (strcmp(table[i].inf, inf) == 0) return &table[i];
    }
    return NULL;
}

static int isEtreVerb(const char *inf)
{
    for (unsigned int i = 0; i < sizeof(frEtreVerbs) / sizeof(frEtreVerbs[0]); i++) {
        if (strcmp(frEtreVerbs[i], inf) == 0) return 1;
    }
    return 0;
}

int conjIsIrregular(Lang lang, const char *inf)
{
    return findIrregular(lang, inf) != NULL;
}

int conjGroup(Lang lang, const char *inf)
{
    int len = (int)strlen(inf);
    if (len < 2) return 0;
    if (lang == LANG_ES) {
        if (strcmp(inf + len - 2, "ar") == 0) return 0;
        if (strcmp(inf + len - 2, "er") == 0) return 1;
        return 2;                                  /* -ir */
    }
    if (strcmp(inf + len - 2, "er") == 0) return 0;
    if (strcmp(inf + len - 2, "ir") == 0) return 1;
    return 2;                                      /* -re */
}

/* Length of the stem: every infinitive loses its two-letter ending.  The
 * Spanish future keeps the whole infinitive, which conjForm handles. */
static int stemLen(const char *inf)
{
    int len = (int)strlen(inf);
    return (len >= 2) ? len - 2 : 0;
}

static void stemCopy(const char *inf, char *out, int outSize)
{
    int n = stemLen(inf);
    if (n > outSize - 1) n = outSize - 1;
    memcpy(out, inf, (size_t)n);
    out[n] = '\0';
}

static const char *groupName(Lang lang, int group)
{
    if (lang == LANG_ES) {
        return (group == 0) ? "regular -ar verb"
             : (group == 1) ? "regular -er verb"
                            : "regular -ir verb";
    }
    return (group == 0) ? "regular -er verb"
         : (group == 1) ? "regular -ir verb"
                        : "regular -re verb";
}

/* Append `suffix` to the string already in `out`, never overflowing. */
static void append(char *out, int outSize, const char *suffix)
{
    int used = (int)strlen(out);
    int add = (int)strlen(suffix);
    if (used + add > outSize - 1) add = outSize - 1 - used;
    if (add > 0) memcpy(out + used, suffix, (size_t)add);
    out[used + add] = '\0';
}

/* --------------------------------------------------------- French forms */

/* Past participle: regular rules, or the verb's irregular table. */
static void frParticiple(const char *inf, int group, char *out, int outSize)
{
    char stem[48];
    stemCopy(inf, stem, sizeof(stem));

    out[0] = '\0';
    switch (group) {
    case 0: append(out, outSize, stem); append(out, outSize, "é"); break;
    case 1: append(out, outSize, stem); append(out, outSize, "i"); break;
    default:
        /* -re verbs drop the -re and take -u (vendre -> vendu). */
        append(out, outSize, stem);
        append(out, outSize, "u");
        break;
    }
}

/* -ger verbs keep a soft g before a and o: mangeons, mangeais (but mangions). */
static int gerNeedsE(const char *inf, const char *ending)
{
    int len = (int)strlen(inf);
    if (len < 3) return 0;
    if (strcmp(inf + len - 3, "ger") != 0) return 0;
    return (ending[0] == 'a' || ending[0] == 'o');
}

static void frConjugate(const char *inf, Tense t, int person, char *out,
                        int outSize)
{
    const int group = conjGroup(LANG_FR, inf);
    char stem[48];
    stemCopy(inf, stem, sizeof(stem));            /* -er/-ir/-re stripped */

    out[0] = '\0';

    switch (t) {
    case TENSE_PRESENT: {
        /* finir: -is/-is/-it/-issons/-issez/-issent (the endings already carry
         * the -iss- infix, so the stem stays "fin"). */
        const char *end = frPresentEnd[group][person];
        append(out, outSize, stem);
        if (group == 0 && gerNeedsE(inf, end)) append(out, outSize, "e");
        append(out, outSize, end);
        break;
    }

    case TENSE_IMPERFECT: {
        /* The imperfect is built on the nous-stem: parlons -> parl-. */
        const char *end = frImparfaitEnd[person];
        append(out, outSize, stem);
        if (group == 1) append(out, outSize, "iss");
        if (group == 0 && gerNeedsE(inf, end)) append(out, outSize, "e");
        append(out, outSize, end);
        break;
    }

    case TENSE_FUTURE:
        /* The future is the infinitive plus -ai/-as/... For -re verbs the
         * final e disappears: vendre -> vendr + ai = vendrai. */
        if (group == 2) {
            append(out, outSize, inf);
            out[strlen(out) - 1] = '\0';      /* drop the final -e */
        } else {
            append(out, outSize, inf);
        }
        append(out, outSize, frFuturEnd[person]);
        break;

    case TENSE_PRETERITE: {
        /* passé composé = auxiliary (present) + past participle */
        char part[48];
        frParticiple(inf, group, part, sizeof(part));

        static const char *avoir[PERSON_COUNT] = {
            "ai", "as", "a", "avons", "avez", "ont"
        };
        static const char *etre[PERSON_COUNT] = {
            "suis", "es", "est", "sommes", "êtes", "sont"
        };

        append(out, outSize, isEtreVerb(inf) ? etre[person] : avoir[person]);
        append(out, outSize, " ");
        append(out, outSize, part);
        if (isEtreVerb(inf) && person >= 3) append(out, outSize, "s");
        break;
    }

    default:
        append(out, outSize, inf);
        break;
    }
}

/* -------------------------------------------------------- Spanish forms */

static void esConjugate(const char *inf, Tense t, int person, char *out,
                        int outSize)
{
    const int group = conjGroup(LANG_ES, inf);
    char stem[48];
    stemCopy(inf, stem, sizeof(stem));

    out[0] = '\0';
    if (t == TENSE_FUTURE) {
        /* futuro: infinitive + endings, no stem change for regulars */
        append(out, outSize, inf);
    } else {
        append(out, outSize, stem);
    }

    /* Spelling changes in the preterite yo form keep the sound the same:
     * llegar -> llegué, buscar -> busqué, empezar -> empecé.  The stem is
     * already in `out`, so drop its last letter (the c/g/z) first. */
    if (t == TENSE_PRETERITE && person == 0 && group == 0) {
        int len = (int)strlen(inf);
        if (len >= 3) {
            const char *tail = inf + len - 3;
            const char *replacement = NULL;
            if (strcmp(tail, "gar") == 0)      replacement = "gué";
            else if (strcmp(tail, "car") == 0) replacement = "qué";
            else if (strcmp(tail, "zar") == 0) replacement = "cé";

            if (replacement) {
                int used = (int)strlen(out);
                if (used > 0) out[used - 1] = '\0';
                append(out, outSize, replacement);
                return;
            }
        }
    }

    append(out, outSize, esEndings[group][t][person]);
}

/* ------------------------------------------------------------- public */

void conjForm(Lang lang, const char *inf, Tense t, int person, char *out,
              int outSize)
{
    if (person < 0 || person >= PERSON_COUNT) person = 0;
    const IrrVerb *irr = findIrregular(lang, inf);

    if (irr) {
        const char *form;
        switch (t) {
        case TENSE_PRESENT:   form = irr->pres[person]; break;
        case TENSE_PRETERITE: form = irr->pret[person]; break;
        case TENSE_IMPERFECT: form = irr->imp[person];  break;
        default:              form = irr->fut[person];  break;
        }
        strncpy(out, form, (size_t)(outSize - 1));
        out[outSize - 1] = '\0';
        return;
    }

    if (lang == LANG_ES) esConjugate(inf, t, person, out, outSize);
    else                 frConjugate(inf, t, person, out, outSize);
}

const char *conjPronoun(Lang lang, int person)
{
    if (person < 0 || person >= PERSON_COUNT) person = 0;
    return (lang == LANG_ES) ? esPronouns[person] : frPronouns[person];
}

void conjPhrase(Lang lang, int person, const char *form, char *out, int size)
{
    const char *p = conjPronoun(lang, person);
    if (lang == LANG_FR && p[0] == 'j' && p[1] == 'e') {
        /* je -> j' before a vowel or a silent h: j'ai parlé, j'étais, j'étudie.
         * Accented vowels are multi-byte in UTF-8, so look at the lead byte;
         * 0xC3 covers á à â é è ê ï î ô û ù and 0xC5 covers œ. */
        unsigned char c = (unsigned char)form[0];
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
            c == 'h' || c == 0xC3 || c == 0xC5) {
            snprintf(out, (size_t)size, "j'%s", form);
            return;
        }
    }
    snprintf(out, (size_t)size, "%s %s", p, form);
}

const char *conjTenseName(Lang lang, Tense t)
{
    static const char *es[TENSE_COUNT] = {
        "Presente", "Pretérito", "Imperfecto", "Futuro"
    };
    static const char *fr[TENSE_COUNT] = {
        "Présent", "Passé composé", "Imparfait", "Futur"
    };
    if ((int)t < 0 || (int)t >= TENSE_COUNT) t = TENSE_PRESENT;
    return (lang == LANG_ES) ? es[t] : fr[t];
}

const char *conjTenseNameEn(Lang lang, Tense t)
{
    static const char *es[TENSE_COUNT] = {
        "present", "preterite", "imperfect", "future"
    };
    static const char *fr[TENSE_COUNT] = {
        "present", "passé composé", "imperfect", "future"
    };
    if ((int)t < 0 || (int)t >= TENSE_COUNT) t = TENSE_PRESENT;
    return (lang == LANG_ES) ? es[t] : fr[t];
}

int conjHasEndings(Lang lang, Tense t)
{
    /* The French passé composé is auxiliary + participle: there is no single
     * ending to drill, so it only appears in the full-form drills. */
    if (lang == LANG_FR && t == TENSE_PRETERITE) return 0;
    return 1;
}

void conjEnding(Lang lang, const char *inf, Tense t, int person, char *out,
                int size)
{
    out[0] = '\0';
    if (!conjHasEndings(lang, t) || conjIsIrregular(lang, inf)) return;

    /* Derive the ending from the finished form rather than from the table, so
     * spelling changes (-ger, -gar, -car, -zar) are reflected correctly. */
    char form[48];
    conjForm(lang, inf, t, person, form, sizeof(form));

    int stem;
    if (t == TENSE_FUTURE) {
        stem = (int)strlen(inf);
        if (lang == LANG_FR && conjGroup(lang, inf) == 2) stem -= 1;  /* -e dropped */
    } else {
        stem = stemLen(inf);
    }
    if (stem < 0) stem = 0;
    if ((int)strlen(form) <= stem) return;     /* no ending (il vend) */

    snprintf(out, (size_t)size, "-%s", form + stem);
}

const char *conjEndingList(Lang lang, const char *inf, Tense t)
{
    static char buf[96];
    buf[0] = '\0';
    if (!conjHasEndings(lang, t) || conjIsIrregular(lang, inf)) return buf;

    for (int p = 0; p < PERSON_COUNT; p++) {
        char e[24];
        conjEnding(lang, inf, t, p, e, sizeof(e));
        if (e[0] == '\0') continue;      /* -re verbs have no il/elle ending */
        if (buf[0]) append(buf, sizeof(buf), ", ");
        append(buf, sizeof(buf), e);
    }
    return buf;
}

/* ------------------------------------------------------------ the rules */

static char ruleBufs[4][320];
static int ruleNext = 0;

const char *conjRule(Lang lang, const char *inf, Tense t, int person)
{
    char *buf = ruleBufs[ruleNext];
    ruleNext = (ruleNext + 1) & 3;

    char form[48];
    char phrase[96];
    char ending[24];
    conjForm(lang, inf, t, person, form, sizeof(form));
    conjPhrase(lang, person, form, phrase, sizeof(phrase));
    conjEnding(lang, inf, t, person, ending, sizeof(ending));

    const char *pronoun = conjPronoun(lang, person);
    const int group = conjGroup(lang, inf);
    const int irr = conjIsIrregular(lang, inf);

    if (irr) {
        /* Irregular verbs get a "learn this one by heart" explanation that
         * still names the tense so the feedback teaches something. */
        if (lang == LANG_FR && t == TENSE_PRETERITE) {
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s is irregular. The passé composé uses the present of "
                     "%s plus a past participle you have to memorise: %s.",
                     inf, isEtreVerb(inf) ? "être" : "avoir", phrase);
        } else {
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s is irregular in the %s, so it does not follow the %s "
                     "endings. Learn it by heart: %s.",
                     inf, conjTenseNameEn(lang, t), groupName(lang, group),
                     phrase);
        }
        return buf;
    }

    if (lang == LANG_ES) {
        const char *list = conjEndingList(lang, inf, t);
        switch (t) {
        case TENSE_FUTURE:
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s. The future keeps the whole infinitive and adds %s. "
                     "For '%s' the ending is %s, so %s -> %s.",
                     groupName(lang, group), list, pronoun, ending, inf, form);
            break;
        case TENSE_PRETERITE:
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s. Preterite endings are %s. For '%s' use %s: %s -> %s.",
                     groupName(lang, group), list, pronoun, ending, inf, form);
            break;
        case TENSE_IMPERFECT:
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s. The imperfect endings are %s. For '%s' use %s: "
                     "%s -> %s.",
                     groupName(lang, group), list, pronoun, ending, inf, form);
            break;
        default:
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s. In the present tense drop the -%s and add %s. For "
                     "'%s' the ending is %s, so %s -> %s.",
                     groupName(lang, group),
                     inf + strlen(inf) - 2, list, pronoun, ending, inf, form);
            break;
        }
        return buf;
    }

    /* French, regular verbs */
    /* Some verbs change their spelling to keep the sound (-ger keeps an e
     * before a/o, Spanish -gar/-car/-zar change in the yo preterite).  When
     * that happens the derived ending differs from the table ending, and the
     * explanation says so instead of pretending nothing happened. */
    {
        char tableEnding[24] = "";
        const char *raw = NULL;
        if (lang == LANG_ES) {
            raw = esEndings[group][t][person];
        } else {
            switch (t) {
            case TENSE_PRESENT:   raw = frPresentEnd[group][person]; break;
            case TENSE_IMPERFECT: raw = frImparfaitEnd[person];      break;
            case TENSE_FUTURE:    raw = frFuturEnd[person];          break;
            default: break;
            }
        }
        if (raw && raw[0]) snprintf(tableEnding, sizeof(tableEnding), "-%s", raw);

        if (tableEnding[0] && ending[0] && strcmp(tableEnding, ending) != 0) {
            int used;
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s. Endings are %s. For '%s' the ending is %s, so %s -> "
                     "%s.",
                     groupName(lang, group), conjEndingList(lang, inf, t),
                     pronoun, ending, inf, form);
            used = (int)strlen(buf);
            snprintf(buf + used, sizeof(ruleBufs[0]) - (size_t)used,
                     " Spelling change: the extra e keeps the g soft before a "
                     "and o.");
            return buf;
        }
    }

    switch (t) {
    case TENSE_PRESENT:
        if (ending[0] == '\0') {
            /* -re verbs: the il/elle form has no ending at all. */
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s. In the present tense the other forms add %s, but the "
                     "'%s' form has no ending: %s -> %s.",
                     groupName(lang, group), conjEndingList(lang, inf, t),
                     pronoun, inf, form);
        } else {
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s. In the present tense drop the -%s and add %s. For "
                     "'%s' the ending is %s, so %s -> %s.",
                     groupName(lang, group), inf + strlen(inf) - 2,
                     conjEndingList(lang, inf, t), pronoun, ending, inf, form);
        }
        break;
    case TENSE_IMPERFECT:
        snprintf(buf, sizeof(ruleBufs[0]),
                 "%s. Take the nous form, drop -ons, then add %s. For '%s' use "
                 "%s: %s -> %s.",
                 groupName(lang, group), conjEndingList(lang, inf, t), pronoun,
                 ending, inf, form);
        break;
    case TENSE_FUTURE:
        if (group == 2) {
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s. Drop the final -e of the infinitive, then add %s. "
                     "For '%s' use %s: %s -> %s.",
                     groupName(lang, group), conjEndingList(lang, inf, t),
                     pronoun, ending, inf, form);
        } else {
            snprintf(buf, sizeof(ruleBufs[0]),
                     "%s. The future keeps the infinitive and adds %s. For "
                     "'%s' use %s: %s -> %s.",
                     groupName(lang, group), conjEndingList(lang, inf, t),
                     pronoun, ending, inf, form);
        }
        break;
    default:
        snprintf(buf, sizeof(ruleBufs[0]),
                 "The passé composé is the present of %s plus the past "
                 "participle. %s is a %s, so %s.",
                 isEtreVerb(inf) ? "être (and the participle agrees)" : "avoir",
                 inf, groupName(lang, group), phrase);
        break;
    }
    return buf;
}
