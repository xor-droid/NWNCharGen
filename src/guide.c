#define _POSIX_C_SOURCE 200809L
#include "guide.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cli.h"
#include "levelup.h"

#define POOL_MAX 1100

/* ---- input and output ---------------------------------------------------- */

static void say(guide *g, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(g->out, fmt, ap);
    va_end(ap);
}

static int all_digits(const char *s)
{
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s))
            return 0;
    return 1;
}

/* Prints the prompt and reads one answer into line (trimmed). In an automatic
 * stretch nothing is asked and the answer is empty ("Enter"). The words all
 * and level switch to automatic for the rest of the build or of this level,
 * q quits; end of input quits too. Returns -1 once the person has quit. */
static int ask(guide *g, const char *prompt, char *line, size_t n)
{
    line[0] = '\0';
    if (g->quit)
        return -1;
    if (g->auto_all || g->auto_level || g->step_auto)
        return 0;
    for (;;) {
        char raw[512], *s, *e;

        say(g, "%s", prompt);
        fflush(g->out);
        if (!fgets(raw, sizeof raw, g->in)) {
            say(g, "\n");
            g->quit = 1;
            return -1;
        }
        for (s = raw; isspace((unsigned char)*s); s++)
            ;
        e = s + strlen(s);
        while (e > s && isspace((unsigned char)e[-1]))
            *--e = '\0';
        if (*s == '#')                      /* comment lines in a replay file */
            continue;
        buf_put(&g->log, s, strlen(s));
        buf_u8(&g->log, '\n');
        if (!strcasecmp(s, "q") || !strcasecmp(s, "quit")) {
            g->quit = 1;
            return -1;
        }
        if (!strcasecmp(s, "all")) {
            g->auto_all = 1;
            s[0] = '\0';
        } else if (!strcasecmp(s, "level")) {
            g->auto_level = 1;
            s[0] = '\0';
        }
        snprintf(line, n, "%s", s);
        return 0;
    }
}

/* Whether the current stretch runs without asking. */
static int silent(const guide *g)
{
    return g->auto_all || g->auto_level || g->step_auto;
}

/* ---- choosing from a list ------------------------------------------------- */

/* How to print the name of an id: a 2DA column through the TLK, or a fixed
 * table of words. */
typedef struct {
    const twoda *tab;
    const char *col;
    const char *const *words;
    const char *const *alias;   /* optional short forms, indexed by id (e.g. NG) */
} namer;

static const char *nm(const guide *g, const namer *n, int id, char *b, size_t bn)
{
    if (n->words) {
        snprintf(b, bn, "%s", n->words[id]);
        return b;
    }
    return cli_tname(g->t, n->tab, id, n->col, b, bn);
}

enum { PK_AUTO = -1, PK_QUIT = -2, PK_EXTRA = -3 };

static void list_items(guide *g, const int *pool, const int *show, int nshow,
                       const namer *n, const char *marks)
{
    int i;
    char b[160];

    for (i = 0; i < nshow; i++) {
        int k = show ? show[i] : i;

        say(g, "%3d%c %-34.34s%s", k + 1, marks && marks[k] ? '*' : ' ',
            nm(g, n, pool[k], b, sizeof b), i % 2 ? "\n" : " ");
    }
    if (nshow % 2)
        say(g, "\n");
}

/* Asks until the person picks an entry of pool (returns its index), presses
 * Enter or types auto (PK_AUTO), types the extra word (PK_EXTRA) or quits. An
 * entry can be typed as its number in the list, its name or part of it; ? lists
 * everything. Entries marked in marks are drawn from the class's package. */
static int choose(guide *g, const char *prompt, const int *pool, int np,
                  const namer *n, const char *marks, const char *extra)
{
    char line[256], b[160];
    int show[POOL_MAX];

    for (;;) {
        int i, ns = 0;

        if (ask(g, prompt, line, sizeof line) != 0)
            return PK_QUIT;
        if (!*line)
            return PK_AUTO;
        if (!strcasecmp(line, "auto")) {
            g->step_auto = 1;
            return PK_AUTO;
        }
        if (extra && !strcasecmp(line, extra))
            return PK_EXTRA;
        if (!strcmp(line, "?") || !strcasecmp(line, "list")) {
            for (i = 0; i < np; i++)
                show[ns++] = i;
            list_items(g, pool, show, ns, n, marks);
            if (marks)
                say(g, "  (* = on the class's package list)\n");
            continue;
        }
        if (all_digits(line)) {
            int k = atoi(line);

            if (k >= 1 && k <= np)
                return k - 1;
            say(g, "  there is no entry %d (1 to %d)\n", k, np);
            continue;
        }
        for (i = 0; i < np; i++)
            if (cli_alnum_eq(nm(g, n, pool[i], b, sizeof b), line) ||
                (n->alias && cli_alnum_eq(n->alias[pool[i]], line)))
                return i;
        for (i = 0; i < np; i++)
            if (cli_alnum_contains(nm(g, n, pool[i], b, sizeof b), line))
                show[ns++] = i;
        if (ns == 1)
            return show[0];
        if (ns == 0) {
            say(g, "  nothing here matches '%s' (? lists the choices)\n", line);
            continue;
        }
        say(g, "  %d match '%s':\n", ns, line);
        list_items(g, pool, show, ns, n, marks);
    }
}

/* ---- setup ----------------------------------------------------------------- */

static const char *const ABIL_WORDS[NABIL] = { "Strength", "Dexterity", "Constitution",
                                               "Intelligence", "Wisdom", "Charisma" };

static const char *const ALIGN_CODE[9] = { "LG", "NG", "CG", "LN", "N", "CN", "LE", "NE", "CE" };
static const char *const ALIGN_WORDS[9] = { "Lawful Good", "Neutral Good", "Chaotic Good",
                                            "Lawful Neutral", "True Neutral",
                                            "Chaotic Neutral", "Lawful Evil",
                                            "Neutral Evil", "Chaotic Evil" };

static int base_class(rules *ru, int r)
{
    const twoda *t = rules_classes(ru);

    return twoda_int(t, r, "PlayerClass", 0) == 1 && !twoda_get(t, r, "PreReqTable");
}

static int class_pool(rules *ru, int *pool, int cap)
{
    int r, n = 0;

    for (r = 0; r < rules_nclasses(ru) && n < cap; r++)
        if (base_class(ru, r))
            pool[n++] = r;
    return n;
}

static const char *class_name(guide *g, int cls, char *b, size_t bn)
{
    return cli_tname(g->t, rules_classes(g->ru), cls, "Name", b, bn);
}

static void ask_race(guide *g)
{
    const twoda *rt = rules_races(g->ru);
    namer n = { rt, "Name", NULL, NULL };
    int pool[32], np = 0, r, k;

    for (r = 0; r < rt->nrows && np < 32; r++)
        if (twoda_int(rt, r, "PlayerRace", 0))
            pool[np++] = r;
    say(g, "\nRace:\n");
    list_items(g, pool, NULL, np, &n, NULL);
    k = choose(g, "Race (number or name, Enter = random): ", pool, np, &n, NULL, NULL);
    if (k >= 0)
        snprintf(g->race, sizeof g->race, "%d", pool[k]);
}

static void ask_level(guide *g, const guide_given *gv)
{
    char line[64];

    if (gv->level)
        return;     /* the caller preloaded g->level */
    for (;;) {
        if (ask(g, "\nCharacter level, 1 to 20 (Enter = 1): ", line, sizeof line) != 0)
            return;
        if (!*line) {
            g->level = 1;
            return;
        }
        if (all_digits(line) && atoi(line) >= 1 && atoi(line) <= 20) {
            g->level = atoi(line);
            return;
        }
        say(g, "  a level from 1 to 20 (epic levels are not supported yet)\n");
    }
}

/* The first class (unless the caller preloaded cls or ncl) and, above level
 * 1, how the levels are split. */
static void ask_class(guide *g)
{
    char line[200], b[96];
    namer n = { rules_classes(g->ru), "Name", NULL, NULL };
    int pool[64], np = class_pool(g->ru, pool, 64), k, first;

    first = g->ncl ? g->cl[0] : g->cls[0] ? atoi(g->cls) : -1;
    if (first < 0) {
        say(g, "\nClass:\n");
        list_items(g, pool, NULL, np, &n, NULL);
        k = choose(g, "First class (number or name, Enter = random): ", pool, np, &n,
                   NULL, NULL);
        if (k >= 0) {
            first = pool[k];
            snprintf(g->cls, sizeof g->cls, "%d", first);
        }
    }
    if (g->quit || g->ncl || g->level < 2)
        return;
    for (;;) {
        if (first >= 0)
            snprintf(b, sizeof b, "all %d levels in one class", g->level);
        say(g, "\nSplitting %d levels: Enter = %s;\n"
               "  a split such as fighter:5,wizard:3 (the first class takes level 1);\n"
               "  2 or 3 = a random mix of up to that many classes;\n"
               "  each = choose the class at every level-up.\n",
            g->level, first >= 0 ? b : "one random class");
        if (ask(g, "Levels: ", line, sizeof line) != 0)
            return;
        if (!*line)
            return;
        if (!strcasecmp(line, "each")) {
            g->per_level = 1;
            return;
        }
        if (all_digits(line) && atoi(line) >= 1 && atoi(line) <= MAX_CLASSES) {
            g->max_classes = atoi(line);
            return;
        }
        if (strchr(line, ':')) {
            int cl[MAX_CLASSES], cn[MAX_CLASSES], nc, i, sum = 0;

            if (cli_parse_classes(g->ru, line, cl, cn, &nc) != 0) {
                say(g, "  not understood (name:levels,... with at most %d different "
                       "base classes)\n", MAX_CLASSES);
                continue;
            }
            for (i = 0; i < nc; i++)
                sum += cn[i];
            if (sum != g->level) {
                say(g, "  that adds up to %d levels, not %d\n", sum, g->level);
                continue;
            }
            if (first >= 0 && cl[0] != first) {
                say(g, "  the first class is %s\n", class_name(g, first, b, sizeof b));
                continue;
            }
            g->ncl = nc;
            memcpy(g->cl, cl, sizeof cl);
            memcpy(g->cn, cn, sizeof cn);
            return;
        }
        say(g, "  not understood\n");
    }
}

/* Whether the classes already fixed allow an alignment. */
static int align_fits(guide *g, int ge, int lc)
{
    int i, cls;

    if (g->ncl) {
        for (i = 0; i < g->ncl; i++)
            if (!chr_align_ok(g->ru, g->cl[i], ge, lc))
                return 0;
        return 1;
    }
    if (g->cls[0]) {
        cls = atoi(g->cls);
        return chr_align_ok(g->ru, cls, ge, lc);
    }
    return 1;
}

static void ask_align(guide *g)
{
    namer n = { NULL, NULL, ALIGN_WORDS, ALIGN_CODE };
    int pool[9], np = 0, i, k;

    for (i = 0; i < 9; i++) {
        int ge, lc;

        cli_parse_align(ALIGN_CODE[i], &ge, &lc);
        if (align_fits(g, ge, lc))
            pool[np++] = i;
    }
    say(g, "\nAlignment (only those the chosen class allows):\n");
    list_items(g, pool, NULL, np, &n, NULL);
    k = choose(g, "Alignment (number or name, Enter = random): ", pool, np, &n, NULL, NULL);
    if (k >= 0)
        snprintf(g->align, sizeof g->align, "%s", ALIGN_CODE[pool[k]]);
}

static void ask_gender(guide *g)
{
    char line[32];

    if (ask(g, "\nGender, m or f (Enter = random): ", line, sizeof line) != 0)
        return;
    if (tolower((unsigned char)line[0]) == 'm' || tolower((unsigned char)line[0]) == 'f')
        snprintf(g->gender, sizeof g->gender, "%c", tolower((unsigned char)line[0]));
}

static int abil_by_word(const char *s)
{
    static const char *w[NABIL] = { "str", "dex", "con", "int", "wis", "cha" };
    int a;

    for (a = 0; a < NABIL; a++)
        if (!strncasecmp(s, w[a], 3))
            return a;
    return -1;
}

/* Scores such as "str 16 dex 14", or rec / Enter. */
static void ask_abilities(guide *g, const guide_given *gv)
{
    int race = g->race[0] ? atoi(g->race) : -1;
    int cls = g->ncl ? g->cl[0] : g->cls[0] ? atoi(g->cls) : -1;
    int budget = race >= 0 ? rules_race_int(g->ru, race, "AbilitiesPointBuyNumber", 30) : 30;
    char line[200];
    int a, any_given = 0;

    for (a = 0; a < NABIL; a++)
        any_given |= gv->abil[a];
    if (any_given)
        return;
    say(g, "\nAbility scores: point buy, %d points, each score 8 to 18 before the race's "
           "adjustment.\n", budget);
    for (;;) {
        char *tok, *save = NULL;
        int fixed[NABIL], ok = 1, cost = 0, out[NABIL];

        if (ask(g, "Scores (Enter = automatic, rec = class recommended, or e.g. "
                   "'str 16 dex 14': the rest follow): ", line, sizeof line) != 0)
            return;
        if (!*line)
            return;
        for (a = 0; a < NABIL; a++)
            fixed[a] = -1;
        if (!strcasecmp(line, "rec")) {
            if (cls < 0 || race < 0) {
                say(g, "  choose a race and a class first\n");
                continue;
            }
            if (cli_allocate(g->ru, cls, race, fixed, 0, NULL, out) != 0)
                continue;
            memcpy(fixed, out, sizeof fixed);
        } else {
            int cur = -1;

            for (tok = strtok_r(line, " ,=:", &save); tok; tok = strtok_r(NULL, " ,=:", &save)) {
                if (all_digits(tok) && cur >= 0) {
                    fixed[cur] = atoi(tok);
                    cur = -1;
                } else if ((cur = abil_by_word(tok)) < 0) {
                    ok = 0;
                    break;
                }
            }
            if (!ok || cur >= 0) {
                say(g, "  not understood: write an ability and a score, e.g. str 16\n");
                continue;
            }
        }
        for (a = 0; a < NABIL; a++) {
            if (fixed[a] != -1 && (fixed[a] < 8 || fixed[a] > 18)) {
                say(g, "  %s must be between 8 and 18\n", ABIL_WORDS[a]);
                ok = 0;
            }
            cost += cli_pb_cost(fixed[a] < 8 ? 8 : fixed[a] > 18 ? 18 : fixed[a]);
        }
        if (!ok)
            continue;
        if (cost > budget) {
            say(g, "  that costs %d points; the budget is %d\n", cost, budget);
            continue;
        }
        memcpy(g->fixed, fixed, sizeof fixed);
        if (cls >= 0 && race >= 0 && cli_allocate(g->ru, cls, race, fixed, 0, NULL, out) == 0) {
            say(g, "  scores:");
            for (a = 0; a < NABIL; a++)
                say(g, " %.3s %d", ABIL_WORDS[a], out[a]);
            say(g, "  (%d of %d points)\n", cli_pb_total(out), budget);
        }
        return;
    }
}

static void ask_name(guide *g)
{
    char line[128];

    if (ask(g, "\nName (Enter = random): ", line, sizeof line) == 0 && *line)
        snprintf(g->name, sizeof g->name, "%s", line);
}

static void ask_settings(guide *g, const guide_given *gv, int level)
{
    char line[64];

    if (!gv->hp && level > 1) {
        for (;;) {
            if (ask(g, "\nHit points after level 1: max, avg or roll (Enter = roll): ", line,
                    sizeof line) != 0)
                return;
            if (!*line)
                break;
            if (!strcasecmp(line, "max") || !strcasecmp(line, "avg") ||
                !strcasecmp(line, "roll")) {
                g->hm = !strcasecmp(line, "max") ? HP_MAX : !strcasecmp(line, "avg") ?
                        HP_AVG : HP_ROLL;
                g->have_hm = 1;
                break;
            }
            say(g, "  max, avg or roll\n");
        }
    }
    if (!gv->variety) {
        for (;;) {
            if (ask(g, "\nHow closely automatic picks follow the class's package, 0 (exactly) "
                       "to 3 (loosely) (Enter = 2): ", line, sizeof line) != 0)
                return;
            if (!*line)
                break;
            if (all_digits(line) && atoi(line) <= VARIETY_MAX) {
                g->variety = atoi(line);
                g->have_variety = 1;
                break;
            }
            say(g, "  0 to %d\n", VARIETY_MAX);
        }
    }
}

void guide_open(guide *g, rules *ru, const tlk *t, uint64_t seed, FILE *in, FILE *out)
{
    int a;

    memset(g, 0, sizeof *g);
    g->ru = ru;
    g->t = t;
    g->seed = seed;
    g->in = in;
    g->out = out;
    for (a = 0; a < NABIL; a++)
        g->fixed[a] = -1;
}

void guide_close(guide *g)
{
    free(g->log.p);
    g->log.p = NULL;
}

int guide_setup(guide *g, const guide_given *gv)
{
    say(g, "NWN character builder\n"
           "Seed %llu (repeat this build with --seed %llu and the same answers).\n"
           "At any prompt: Enter = pick automatically, ? = list the choices, "
           "auto = finish this step automatically,\n"
           "level = finish this level automatically, all = finish the whole "
           "character automatically, q = quit.\n",
        (unsigned long long)g->seed, (unsigned long long)g->seed);
    if (!gv->race)
        ask_race(g);
    ask_level(g, gv);
    if (!g->quit)
        ask_class(g);
    if (!g->quit && !gv->align)
        ask_align(g);
    if (!g->quit && !gv->gender)
        ask_gender(g);
    if (!g->quit)
        ask_abilities(g, gv);
    if (!g->quit && !gv->name)
        ask_name(g);
    if (!g->quit)
        ask_settings(g, gv, g->level);
    return g->quit ? -1 : 0;
}

/* ---- the level-up dialogue --------------------------------------------------- */

static int in_pref(rules *ru, const chr *c, int cls, const char *col, const char *idx,
                   int id)
{
    int w;

    for (w = 0; w < 2; w++) {
        const twoda *t = lu_pref_tab(ru, c, cls, col, w);
        int r;

        for (r = 0; t && r < t->nrows; r++)
            if (twoda_int(t, r, idx, -1) == id)
                return 1;
    }
    return 0;
}

typedef struct {
    guide *g;
    const lu_opts *o;
} fillctx;

static int pick_class_cb(rules *ru, const chr *c, int li, int dflt, void *ud)
{
    guide *g = ud;
    namer n = { rules_classes(ru), "Name", NULL, NULL };
    int pool[64], np = 0, r, k, last = c->lv[li - 1].cls;
    char b[96], prompt[160];

    (void)dflt;
    g->auto_level = 0;
    g->step_auto = 0;
    for (r = 0; r < rules_nclasses(ru) && np < 64; r++) {
        int have = chr_class_level(c, r) > 0, nclasses = 0, i;

        if (!base_class(ru, r) || !chr_align_ok(ru, r, c->good_evil, c->lawful_chaotic))
            continue;
        for (i = 0; i < rules_nclasses(ru); i++)
            nclasses += chr_class_level(c, i) > 0;
        if (have || nclasses < MAX_CLASSES)
            pool[np++] = r;
    }
    snprintf(prompt, sizeof prompt, "\nLevel %d, class (number or name, ? lists, Enter = %s): ",
             li + 1, class_name(g, last, b, sizeof b));
    k = choose(g, prompt, pool, np, &n, NULL, NULL);
    if (k == PK_QUIT)
        return -1;
    return k >= 0 ? pool[k] : last;
}

static void show_owed(guide *g, const pending *p)
{
    int sl, spells = 0;

    for (sl = 0; sl < NSPLVL; sl++)
        spells += p->spells[sl];
    say(g, "  to choose:");
    if (p->need_domains)
        say(g, " domains;");
    if (p->need_school)
        say(g, " school;");
    if (p->abil_due)
        say(g, " ability increase;");
    if (p->bonus)
        say(g, " %d bonus feat%s;", p->bonus, p->bonus > 1 ? "s" : "");
    if (p->general)
        say(g, " %d feat%s;", p->general, p->general > 1 ? "s" : "");
    if (spells)
        say(g, " %d spell%s;", spells, spells > 1 ? "s" : "");
    if (p->wiz_any)
        say(g, " %d spellbook spell%s;", p->wiz_any, p->wiz_any > 1 ? "s" : "");
    say(g, " %d skill point%s\n", p->skill_pts, p->skill_pts == 1 ? "" : "s");
}

static void step_package(guide *g, rules *ru, chr *c, const pending *p)
{
    const twoda *pk = rules_tab(ru, "packages");
    int cls = c->lv[c->nlevels - 1].cls, pool[64], np = 0, r, k, cur;
    namer n = { pk, "Name", NULL, NULL };
    char b[160], prompt[200];

    if (!pk || cls >= MAX_CLS || c->pkg[cls] < 0 || p->need_school ||
        chr_class_level(c, cls) != 1)
        return;
    for (r = 0; r < pk->nrows && np < 64; r++)
        if (twoda_int(pk, r, "ClassID", -1) == cls && twoda_int(pk, r, "PlayerClass", 0) == 1)
            pool[np++] = r;
    if (np < 2)
        return;
    if (silent(g))
        return;
    cur = c->pkg[cls];
    say(g, "\nStarting package (its preferences guide the automatic picks, "
           "and its gold and equipment):\n");
    list_items(g, pool, NULL, np, &n, NULL);
    snprintf(prompt, sizeof prompt, "Package (Enter = %s): ", cli_tname(g->t, pk, cur, "Name", b, sizeof b));
    k = choose(g, prompt, pool, np, &n, NULL, NULL);
    if (k >= 0)
        c->pkg[cls] = pool[k];
}

static void step_domains(guide *g, rules *ru, chr *c, pending *p, rng *r)
{
    const twoda *d = rules_tab(ru, "domains");
    namer n = { d, "Name", NULL, NULL };
    int pool[64], np = 0, i, k1, k2, d1, d2;
    char err[120];

    if (!p->need_domains)
        return;
    for (i = 0; i < d->nrows && np < 64; i++)
        if (twoda_get(d, i, "Label"))
            pool[np++] = i;
    if (!silent(g)) {
        say(g, "\nDomains (two):\n");
        list_items(g, pool, NULL, np, &n, NULL);
    }
    for (;;) {
        k1 = choose(g, "First domain (Enter = the package's pair): ", pool, np, &n, NULL, NULL);
        if (k1 < 0) {
            if (k1 == PK_AUTO)
                lu_auto_domains(ru, c, p, r);
            return;
        }
        k2 = choose(g, "Second domain: ", pool, np, &n, NULL, NULL);
        if (k2 < 0) {
            if (k2 == PK_AUTO)
                lu_auto_domains(ru, c, p, r);
            return;
        }
        d1 = pool[k1];
        d2 = pool[k2];
        if (lu_pick_domains(ru, c, p, d1, d2, err, sizeof err) == 0)
            return;
        say(g, "  %s (pick two different domains)\n", err);
    }
}

static void step_school(guide *g, rules *ru, chr *c, pending *p)
{
    const twoda *ss = rules_tab(ru, "spellschools");
    namer n = { ss, "StringRef", NULL, NULL };
    int pool[16], np = 0, i, k;
    char err[120];

    if (!p->need_school)
        return;
    for (i = 0; i < ss->nrows && np < 16; i++)
        pool[np++] = i;
    if (!silent(g)) {
        say(g, "\nWizard specialty (a specialist gains a spell slot per level but gives up "
               "an opposition school):\n");
        list_items(g, pool, NULL, np, &n, NULL);
    }
    for (;;) {
        k = choose(g, "School (Enter = generalist): ", pool, np, &n, NULL, NULL);
        if (k < 0) {
            if (k == PK_AUTO)
                lu_auto_school(ru, c, p);
            return;
        }
        if (lu_pick_school(ru, c, p, pool[k], err, sizeof err) == 0) {
            if (pool[k] > 0) {
                int opp = twoda_int(ss, pool[k], "Opposition", 0);
                char b[100];

                say(g, "  opposition school: %s\n", cli_tname(g->t, ss, opp, "StringRef", b, sizeof b));
            }
            return;
        }
        say(g, "  %s\n", err);
    }
}

static void step_ability(guide *g, rules *ru, chr *c, pending *p)
{
    namer n = { NULL, NULL, ABIL_WORDS, NULL };
    int pool[NABIL], a, k;
    char err[120];

    if (!p->abil_due)
        return;
    for (a = 0; a < NABIL; a++)
        pool[a] = a;
    if (!silent(g)) {
        say(g, "\nAbility increase (+1):\n ");
        for (a = 0; a < NABIL; a++)
            say(g, " %.3s %d", ABIL_WORDS[a], c->abil[a]);
        say(g, "\n");
    }
    for (;;) {
        k = choose(g, "Raise (number or name, Enter = the class's primary): ", pool, NABIL,
                   &n, NULL, NULL);
        if (k < 0) {
            if (k == PK_AUTO)
                lu_auto_ability(ru, c, p);
            return;
        }
        if (lu_pick_ability(ru, c, p, pool[k], err, sizeof err) == 0)
            return;
        say(g, "  %s\n", err);
    }
}

static void step_skills(guide *g, rules *ru, chr *c, pending *p, rng *r, const lu_opts *o);

static void step_feats(guide *g, rules *ru, chr *c, pending *p, rng *r, const lu_opts *o)
{
    namer n = { rules_feats(ru), "FEAT", NULL, NULL };
    int cls = c->lv[c->nlevels - 1].cls, bonus;

    g->step_auto = 0;
    for (bonus = 1; bonus >= 0; bonus--) {
        for (;;) {
            int pool[POOL_MAX], np, k, i;
            char marks[POOL_MAX], prompt[200], b[160], err[120];

            if ((bonus ? p->bonus : p->general) <= 0 ||
                (np = lu_feat_pool(ru, c, bonus, pool, POOL_MAX)) <= 0)
                break;
            for (i = 0; i < np; i++)
                marks[i] = (char)in_pref(ru, c, cls, "FeatPref2DA", "FeatIndex", pool[i]);
            snprintf(prompt, sizeof prompt,
                     "\n%s, %d left, %d to choose from (Enter = automatic, ? list%s): ",
                     bonus ? "Bonus feat" : "Feat", bonus ? p->bonus : p->general, np,
                     g->skills_done || p->skill_pts <= 0 ? "" : ", skills = buy skills first");
            k = choose(g, prompt, pool, np, &n, marks,
                       g->skills_done || p->skill_pts <= 0 ? NULL : "skills");
            if (k == PK_QUIT)
                return;
            if (k == PK_EXTRA) {
                step_skills(g, ru, c, p, r, o);
                g->step_auto = 0;
                continue;
            }
            if (k == PK_AUTO) {
                if (lu_auto_feat(ru, c, p, bonus, r, o) != 0)
                    break;
                if (!silent(g)) {
                    const lvl_rec *lr = &c->lv[c->nlevels - 1];

                    say(g, "  + %s\n", cli_tname(g->t, rules_feats(ru), lr->feats[lr->nfeats - 1],
                                                 "FEAT", b, sizeof b));
                }
                continue;
            }
            if (lu_pick_feat(ru, c, p, pool[k], bonus, err, sizeof err) != 0)
                say(g, "  %s\n", err);
            else
                say(g, "  + %s\n", cli_tname(g->t, rules_feats(ru), pool[k], "FEAT", b, sizeof b));
        }
    }
}

/* One pick of spell level sl (or, with sl < 0, a spellbook pick of any level up
 * to top) from the given pool. 0 picked, 1 stop asking (nothing more can be
 * picked), -1 quit. */
static int spell_prompt(guide *g, rules *ru, chr *c, pending *p, rng *r, const lu_opts *o,
                        int sl, int top, const char *what, const int *pool, int np, int forced)
{
    namer n = { rules_tab(ru, "spells"), "Name", NULL, NULL };
    int cls = c->lv[c->nlevels - 1].cls, k, i, lvl;
    char marks[POOL_MAX], prompt[200], err[120], b[160];

    (void)top;
    for (i = 0; i < np; i++)
        marks[i] = (char)in_pref(ru, c, cls, "SpellPref2DA", "SpellIndex", pool[i]);
    snprintf(prompt, sizeof prompt, "\n%s, %d to choose from (Enter = automatic, ? list): ",
             what, np);
    k = choose(g, prompt, pool, np, &n, marks, NULL);
    if (k == PK_QUIT)
        return -1;
    if (k == PK_AUTO) {
        int rc;

        if (forced)
            rc = lu_auto_school_spell(ru, c, p, sl, r, o);
        else if (sl < 0)
            rc = lu_auto_wiz_any(ru, c, p, r, o);
        else
            rc = lu_auto_spell(ru, c, p, sl, r, o);
        return rc == 0 ? 0 : 1;
    }
    lvl = sl >= 0 ? sl : lu_spell_level(ru, cls, pool[k]);
    if (lu_pick_spell(ru, c, p, lvl, pool[k], err, sizeof err) != 0) {
        say(g, "  %s\n", err);
        return 0;
    }
    say(g, "  + %s\n", cli_tname(g->t, rules_tab(ru, "spells"), pool[k], "Name", b, sizeof b));
    return 0;
}

static void step_spells(guide *g, rules *ru, chr *c, pending *p, rng *r, const lu_opts *o)
{
    int cls = c->lv[c->nlevels - 1].cls, sl, pool[POOL_MAX], np, i, j, rc;
    char what[160], b[100];

    g->step_auto = 0;
    /* a specialist needs a school spell at every castable level */
    for (sl = 1; sl < NSPLVL && !g->quit; sl++) {
        if (!lu_school_due(ru, c, p, sl))
            continue;
        np = lu_spell_pool(ru, c, cls, sl, pool, POOL_MAX);
        for (i = j = 0; i < np; i++)
            if (chr_spell_in_school(ru, pool[i], c->school))
                pool[j++] = pool[i];
        if (j <= 0)
            continue;
        snprintf(what, sizeof what, "Level-%d %s spell (a specialist needs one)", sl,
                 cli_tname(g->t, rules_tab(ru, "spellschools"), c->school, "StringRef", b, sizeof b));
        rc = spell_prompt(g, ru, c, p, r, o, sl, sl, what, pool, j, 1);
        if (rc < 0)
            return;
        if (rc == 0 && lu_school_due(ru, c, p, sl))      /* a refused pick: ask again */
            sl--;
    }
    for (sl = 0; sl < NSPLVL && !g->quit; sl++) {
        while (p->spells[sl] > 0 && !g->quit &&
               (np = lu_spell_pool(ru, c, cls, sl, pool, POOL_MAX)) > 0) {
            snprintf(what, sizeof what, "Level-%d spell, %d left", sl, p->spells[sl]);
            rc = spell_prompt(g, ru, c, p, r, o, sl, sl, what, pool, np, 0);
            if (rc != 0)
                break;
        }
    }
    while (p->wiz_any > 0 && !g->quit) {
        int top = lu_class_max_spell_level(ru, c, cls), s2;

        if (top < 1)
            break;
        for (np = 0, s2 = 1; s2 <= top; s2++)
            np += lu_spell_pool(ru, c, cls, s2, pool + np, POOL_MAX - np);
        if (np <= 0) {
            p->wiz_any = 0;
            break;
        }
        snprintf(what, sizeof what, "Spellbook spell of any level up to %d, %d left", top,
                 p->wiz_any);
        rc = spell_prompt(g, ru, c, p, r, o, -1, top, what, pool, np, 0);
        if (rc != 0)
            break;
    }
}

static int skill_cap(const chr *c, int cost)
{
    return cost == 1 ? c->nlevels + 3 : (c->nlevels + 3) / 2;
}

static int skill_total(const chr *c, int sk)
{
    int i, n = 0;

    for (i = 0; i < c->nlevels; i++)
        n += c->lv[i].ranks[sk];
    return n;
}

static void skill_table(guide *g, rules *ru, const chr *c)
{
    int cls = c->lv[c->nlevels - 1].cls, sk, col = 0;
    char b[96];

    for (sk = 0; sk < rules_nskills(ru) && sk < MAX_SK; sk++) {
        int cost = chr_skill_cost(ru, cls, sk);

        if (!cost)
            continue;
        say(g, "%3d %-20.20s %s %2d/%-2d%s", sk,
            cli_tname(g->t, rules_skills(ru), sk, "Name", b, sizeof b),
            cost == 1 ? "class" : "cross", skill_total(c, sk), skill_cap(c, cost),
            col++ % 2 ? "\n" : "  ");
    }
    if (col % 2)
        say(g, "\n");
}

/* "concentration 3", "1 3", "tumble": a skill and the ranks to buy. */
static int parse_skill(guide *g, rules *ru, const chr *c, char *line, int *sk, int *ranks)
{
    int cls = c->lv[c->nlevels - 1].cls, i, hit = -1, nhit = 0;
    char *sp = strrchr(line, ' '), b[96];

    *ranks = 1;
    if (sp && all_digits(sp + 1)) {
        *ranks = atoi(sp + 1);
        while (sp > line && isspace((unsigned char)sp[-1]))
            sp--;
        *sp = '\0';
    }
    if (all_digits(line)) {
        *sk = atoi(line);
        return *sk < rules_nskills(ru) && chr_skill_cost(ru, cls, *sk) ? 0 : -1;
    }
    for (i = 0; i < rules_nskills(ru) && i < MAX_SK; i++) {
        const char *name;

        if (!chr_skill_cost(ru, cls, i))
            continue;
        name = cli_tname(g->t, rules_skills(ru), i, "Name", b, sizeof b);
        if (cli_alnum_eq(name, line)) {
            *sk = i;
            return 0;
        }
        if (cli_alnum_contains(name, line)) {
            hit = i;
            nhit++;
        }
    }
    *sk = hit;
    return nhit == 1 ? 0 : -1;
}

static void step_skills(guide *g, rules *ru, chr *c, pending *p, rng *r, const lu_opts *o)
{
    char line[160], prompt[200], err[120];
    int shown = 0;

    if (g->skills_done || p->skill_pts <= 0)
        return;
    g->skills_done = 1;
    g->step_auto = 0;
    while (p->skill_pts > 0 && !g->quit) {
        int sk, ranks;

        if (!shown && !(g->auto_all || g->auto_level)) {
            say(g, "\nSkills (cost 1 per rank for class skills, 2 for cross-class; "
                   "ranks now/most):\n");
            skill_table(g, ru, c);
            shown = 1;
        }
        snprintf(prompt, sizeof prompt, "\n%d skill point%s. Enter = automatic, "
                 "'name N' buys N ranks, ? = table, done = keep the points: ",
                 p->skill_pts, p->skill_pts == 1 ? "" : "s");
        if (ask(g, prompt, line, sizeof line) != 0)
            return;
        if (!*line || !strcasecmp(line, "auto")) {
            lu_auto_skills(ru, c, p, r, o);
            return;
        }
        if (!strcasecmp(line, "done"))
            return;
        if (!strcmp(line, "?")) {
            skill_table(g, ru, c);
            continue;
        }
        if (parse_skill(g, ru, c, line, &sk, &ranks) != 0) {
            say(g, "  no single class or cross-class skill matches that (? lists them)\n");
            continue;
        }
        if (lu_buy_skill(ru, c, p, sk, ranks, err, sizeof err) != 0)
            say(g, "  %s\n", err);
    }
}

static int fill_cb(rules *ru, chr *c, pending *p, rng *r, const lu_opts *o, void *ud)
{
    guide *g = ud;
    const lvl_rec *lr = &c->lv[c->nlevels - 1];
    char b[96];

    g->auto_level = g->auto_all ? 1 : 0;
    g->step_auto = 0;
    g->skills_done = 0;
    say(g, "\n== Level %d: %s (class level %d), +%d HP ==\n", c->nlevels,
        class_name(g, lr->cls, b, sizeof b), chr_class_level(c, lr->cls), lr->hp);
    if (!g->auto_all) {
        show_owed(g, p);
        step_package(g, ru, c, p);
    }
    step_domains(g, ru, c, p, r);
    step_school(g, ru, c, p);
    step_ability(g, ru, c, p);
    step_feats(g, ru, c, p, r, o);
    step_spells(g, ru, c, p, r, o);
    step_skills(g, ru, c, p, r, o);
    /* a stretch that was cut short still has to leave nothing owed that the
     * engine can fill */
    return g->quit ? -1 : 0;
}

void guide_attach(guide *g, lvl_plan *pl)
{
    pl->fill = fill_cb;
    pl->pick_class = g->per_level ? pick_class_cb : NULL;
    pl->ud = g;
}

/* ---- familiar, review and the answer log -------------------------------------- */

static void pick_creature(guide *g, const char *table, const char *what, int *type,
                          char *name, size_t nn)
{
    const twoda *t = rules_tab(g->ru, table);
    namer n = { t, "STRREF", NULL, NULL };
    int pool[64], np = 0, r, k, cur = -1;
    char line[96], prompt[160], b[96];

    if (!t)
        return;
    for (r = 0; r < t->nrows && np < 64; r++) {
        if (!twoda_get(t, r, "NAME"))
            continue;
        if (r == *type)
            cur = np;
        pool[np++] = r;
    }
    if (cur < 0)
        return;
    if (silent(g))
        return;
    say(g, "\n%s:\n", what);
    list_items(g, pool, NULL, np, &n, NULL);
    snprintf(prompt, sizeof prompt, "%s (Enter = %s): ", what,
             cli_tname(g->t, t, *type, "STRREF", b, sizeof b));
    k = choose(g, prompt, pool, np, &n, NULL, NULL);
    if (k >= 0)
        *type = pool[k];
    snprintf(prompt, sizeof prompt, "Its name (Enter = %s): ", name);
    if (ask(g, prompt, line, sizeof line) == 0 && *line)
        snprintf(name, nn, "%s", line);
}

int guide_finish(guide *g, chr *c)
{
    g->step_auto = 0;
    g->auto_level = 0;
    if (c->familiar >= 0)
        pick_creature(g, "hen_familiar", "Familiar", &c->familiar, c->familiar_name,
                      sizeof c->familiar_name);
    if (c->companion >= 0 && c->companion_name[0])
        pick_creature(g, "hen_companion", "Animal companion", &c->companion,
                      c->companion_name, sizeof c->companion_name);
    return g->quit ? -1 : 0;
}

int guide_review(guide *g, const chr *c)
{
    char line[32];

    g->auto_level = g->auto_all = g->step_auto = 0;
    say(g, "\n");
    cli_sheet(g->ru, g->t, c);
    for (;;) {
        if (ask(g, "\nWrite this character? [Y/n]: ", line, sizeof line) != 0)
            return -1;
        if (!*line || tolower((unsigned char)line[0]) == 'y')
            return 1;
        if (tolower((unsigned char)line[0]) == 'n')
            return 0;
    }
}

int guide_save(const guide *g, const char *path, const char *command)
{
    buf out = { 0 };
    int rc;
    char head[1200];

    snprintf(head, sizeof head,
             "# Answers of a guided session (seed %llu). Replay with:\n#   %s < this-file\n",
             (unsigned long long)g->seed, command);
    buf_put(&out, head, strlen(head));
    buf_put(&out, g->log.p, g->log.n);
    rc = write_file(path, out.p, out.n);
    free(out.p);
    return rc;
}
