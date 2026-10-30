#define _POSIX_C_SOURCE 200809L
#include "plan.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "look.h"

void plan_single(lvl_plan *pl, int cls, int levels)
{
    int i;

    pl->n = levels;
    for (i = 0; i < levels && i < MAX_LEVELS; i++)
        pl->seq[i] = cls;
}

void plan_random(rules *ru, const chr *c, lvl_plan *pl, int first, int levels,
                 int max_classes, rng *g)
{
    int chosen[MAX_CLASSES], nc = 1, want, i, r;

    plan_single(pl, first, levels);
    chosen[0] = first;
    if (max_classes > MAX_CLASSES)
        max_classes = MAX_CLASSES;
    if (max_classes < 2 || levels < 2)
        return;
    want = 1 + (int)rng_below(g, (unsigned)(max_classes < levels ? max_classes : levels));
    while (nc < want) {
        int pool[64], np = 0, k;

        for (r = 0; r < rules_nclasses(ru) && np < 64; r++) {
            if (twoda_int(rules_classes(ru), r, "PlayerClass", 0) != 1 ||
                twoda_get(rules_classes(ru), r, "PreReqTable") ||
                !chr_align_ok(ru, r, c->good_evil, c->lawful_chaotic))
                continue;
            for (k = 0; k < nc; k++)
                if (chosen[k] == r)
                    break;
            if (k == nc)
                pool[np++] = r;
        }
        if (!np)
            break;
        chosen[nc++] = pool[rng_below(g, (unsigned)np)];
    }
    /* every extra class takes one distinct level after the first ... */
    for (i = 1; i < nc; i++) {
        int pos;
        do
            pos = 1 + (int)rng_below(g, (unsigned)(levels - 1));
        while (pl->seq[pos] != first);
        pl->seq[pos] = chosen[i];
    }
    /* ... and the remaining free levels go to a random class of the set */
    for (i = 1; i < levels; i++)
        if (pl->seq[i] == first)
            pl->seq[i] = chosen[rng_below(g, (unsigned)nc)];
}

int plan_run(rules *ru, chr *c, const lvl_plan *pl, rng *g, char *err, size_t n)
{
    int i, school_used = 0;

    for (i = 0; i < pl->n; i++) {
        pending p;
        int cls = pl->seq[i];

        if (i > 0 && pl->pick_class)
            cls = pl->pick_class(ru, c, i, cls, pl->ud);
        if (cls < 0) {
            snprintf(err, n, "stopped before level %d", i + 1);
            return -1;
        }
        if (lu_begin(ru, c, cls, i == 0 ? HP_MAX : pl->hm, g, &p, err, n) != 0)
            return -1;
        if (p.need_school && !school_used && pl->school != PLAN_SCHOOL_NONE) {
            int sc = pl->school;

            if (sc == PLAN_SCHOOL_RANDOM)
                sc = (int)rng_below(g, (unsigned)rules_tab(ru, "spellschools")->nrows);
            if (lu_pick_school(ru, c, &p, sc, err, n) != 0)
                return -1;
            school_used = 1;
        }
        if (pl->fill) {
            if (pl->fill(ru, c, &p, g, &pl->opts, pl->ud) != 0) {
                snprintf(err, n, "stopped during level %d", i + 1);
                return -1;
            }
        } else {
            lu_autofill(ru, c, &p, g, &pl->opts);
        }
        lu_end(c, &p);
    }
    return 0;
}

static int levels_of_label(rules *ru, const chr *c, const char *label)
{
    int k;

    for (k = 0; k < rules_nclasses(ru); k++) {
        const char *l = rules_class_str(ru, k, "Label");
        if (l && !strcasecmp(l, label))
            return chr_class_level(c, k);
    }
    return 0;
}

static int pick_row(const twoda *t, const char *col, rng *g)
{
    int valid[64], n = 0, r;

    for (r = 0; t && r < t->nrows && n < 64; r++)
        if (twoda_get(t, r, col))
            valid[n++] = r;
    return n ? valid[rng_below(g, (unsigned)n)] : 0;
}

void plan_finish(rules *ru, chr *c, rng *g)
{
    const twoda *pk = rules_tab(ru, "packages");
    int row = chr_package(ru, c, c->lv[0].cls), k, dr, rg;

    c->gold = pk && row >= 0 ? twoda_int(pk, row, "Gold", 0) : 0;
    for (k = 0; k < rules_nclasses(ru); k++)
        if (chr_class_level(c, k) > 0 && rules_class_int(ru, k, "Arcane", 0) == 1 &&
            rules_class_int(ru, k, "SpellbookRestricted", 0) == 1) {
            c->familiar = pick_row(rules_tab(ru, "hen_familiar"), "BASERESREF", g);
            familiar_name_random(g, c->familiar_name, sizeof c->familiar_name);
            break;
        }
    dr = levels_of_label(ru, c, "Druid");
    rg = levels_of_label(ru, c, "Ranger");
    if (dr > 0 || rg >= 4) {       /* druids have one from level 1, rangers from 4 */
        c->companion = pick_row(rules_tab(ru, "hen_companion"), "BASERESREF", g);
        familiar_name_random(g, c->companion_name, sizeof c->companion_name);
    } else if (rg > 0) {
        c->companion = 0;
        c->companion_name[0] = '\0';
    }
}
