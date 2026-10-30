#define _POSIX_C_SOURCE 200809L
#include "rules.h"

#include <stdio.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef struct {
    char *name;
    twoda *t;
} cached;

struct rules {
    res *r;
    cached *cache;
    size_t n, cap;
    const twoda *classes, *races, *feats, *skills, *exptable;
};

const twoda *rules_tab(rules *ru, const char *name)
{
    size_t i;
    char key[64];

    for (i = 0; name[i] && i < sizeof key - 1; i++)
        key[i] = (char)tolower((unsigned char)name[i]);
    key[i] = '\0';
    for (i = 0; i < ru->n; i++)
        if (strcmp(ru->cache[i].name, key) == 0)
            return ru->cache[i].t;
    if (ru->n == ru->cap) {
        ru->cap = ru->cap ? ru->cap * 2 : 32;
        ru->cache = xrealloc(ru->cache, ru->cap * sizeof *ru->cache);
    }
    ru->cache[ru->n].name = xstrdup(key);
    ru->cache[ru->n].t = res_2da(ru->r, key);
    return ru->cache[ru->n++].t;
}

rules *rules_open(res *r, char *err, size_t errlen)
{
    static const char *core[] = { "classes", "racialtypes", "feat", "skills",
                                  "exptable" };
    rules *ru = xcalloc(1, sizeof *ru);
    size_t i;

    ru->r = r;
    for (i = 0; i < sizeof core / sizeof *core; i++) {
        if (!rules_tab(ru, core[i])) {
            snprintf(err, errlen, "cannot load %s.2da", core[i]);
            rules_close(ru);
            return NULL;
        }
    }
    ru->classes = rules_tab(ru, "classes");
    ru->races = rules_tab(ru, "racialtypes");
    ru->feats = rules_tab(ru, "feat");
    ru->skills = rules_tab(ru, "skills");
    ru->exptable = rules_tab(ru, "exptable");
    return ru;
}

void rules_close(rules *ru)
{
    size_t i;

    if (!ru)
        return;
    for (i = 0; i < ru->n; i++) {
        free(ru->cache[i].name);
        twoda_free(ru->cache[i].t);
    }
    free(ru->cache);
    free(ru);
}

const twoda *rules_classes(rules *ru) { return ru->classes; }
const twoda *rules_races(rules *ru) { return ru->races; }
const twoda *rules_feats(rules *ru) { return ru->feats; }
const twoda *rules_skills(rules *ru) { return ru->skills; }
res *rules_res(rules *ru) { return ru->r; }
int rules_nskills(rules *ru) { return ru->skills->nrows; }
int rules_nfeats(rules *ru) { return ru->feats->nrows; }
int rules_nclasses(rules *ru) { return ru->classes->nrows; }

const twoda *rules_class_tab(rules *ru, int cls, const char *col)
{
    const char *n = twoda_get(ru->classes, cls, col);
    return n ? rules_tab(ru, n) : NULL;
}

const twoda *rules_race_tab(rules *ru, int race, const char *col)
{
    const char *n = twoda_get(ru->races, race, col);
    return n ? rules_tab(ru, n) : NULL;
}

int rules_class_int(rules *ru, int cls, const char *col, int def)
{
    return twoda_int(ru->classes, cls, col, def);
}

int rules_race_int(rules *ru, int race, const char *col, int def)
{
    return twoda_int(ru->races, race, col, def);
}

const char *rules_class_str(rules *ru, int cls, const char *col)
{
    return twoda_get(ru->classes, cls, col);
}

int abil_mod(int score)
{
    int d = score - 10;
    return d >= 0 ? d / 2 : -((1 - d) / 2);
}

int rules_race_adjust(rules *ru, int race, int a)
{
    static const char *col[NABIL] = { "StrAdjust", "DexAdjust", "ConAdjust",
                                      "IntAdjust", "WisAdjust", "ChaAdjust" };
    return rules_race_int(ru, race, col[a], 0);
}

/* Row index for a 1-based class level, clamped to the table. */
static int level_row(const twoda *t, int level)
{
    int row = level - 1;

    if (row >= t->nrows)
        row = t->nrows - 1;
    return row < 0 ? 0 : row;
}

int rules_cls_bab(rules *ru, int cls, int level)
{
    const twoda *t = rules_class_tab(ru, cls, "AttackBonusTable");

    return t ? twoda_int(t, level_row(t, level), "BAB", 0) : 0;
}

int rules_cls_save(rules *ru, int cls, int level, const char *col)
{
    const twoda *t = rules_class_tab(ru, cls, "SavingThrowTable");

    return t ? twoda_int(t, level_row(t, level), col, 0) : 0;
}

int rules_cls_bonus_feats(rules *ru, int cls, int level)
{
    const twoda *t = rules_class_tab(ru, cls, "BonusFeatsTable");

    return t ? twoda_int(t, level_row(t, level), "Bonus", 0) : 0;
}

long rules_xp_for_level(rules *ru, int level)
{
    const twoda *t = ru->exptable;
    int row = level - 1;
    const char *s;

    if (row < 0 || row >= t->nrows)
        return -1;
    s = twoda_get(t, row, "XP");
    return s ? strtol(s, NULL, 0) : -1;
}

static int spell_col(rules *ru, const char *tabcol, int cls, int level,
                     int spell_level)
{
    const twoda *t = rules_class_tab(ru, cls, tabcol);
    char col[32];
    const char *s;

    if (!t)
        return -1;
    snprintf(col, sizeof col, "SpellLevel%d", spell_level);
    s = twoda_get(t, level_row(t, level), col);
    return s ? atoi(s) : -1;
}

int rules_spells_per_day(rules *ru, int cls, int level, int spell_level)
{
    return spell_col(ru, "SpellGainTable", cls, level, spell_level);
}

int rules_spells_known(rules *ru, int cls, int level, int spell_level)
{
    return spell_col(ru, "SpellKnownTable", cls, level, spell_level);
}

int spell_bonus(int m, int spell_level)
{
    return m >= spell_level && spell_level > 0 ? (m - spell_level) / 4 + 1 : 0;
}
