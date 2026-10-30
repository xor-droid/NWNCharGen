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

/* ---- reaching a prestige class ------------------------------------------ */

/* Index of the first level of each prestige class in the plan. */
static int prestige_entries(rules *ru, const lvl_plan *pl, int *ip, int *cls, int cap)
{
    int i, j, n = 0;

    for (i = 1; i < pl->n && n < cap; i++) {
        if (!twoda_get(rules_classes(ru), pl->seq[i], "PreReqTable"))
            continue;
        for (j = 0; j < i && pl->seq[j] != pl->seq[i]; j++)
            ;
        if (j == i) {
            ip[n] = i;
            cls[n++] = pl->seq[i];
        }
    }
    return n;
}

static int plan_has_pin(const lu_opts *o, int feat)
{
    int k;

    for (k = 0; k < o->npins; k++)
        if (o->pin[k].feat == feat)
            return 1;
    return 0;
}

static void plan_pin(lu_opts *o, int feat)
{
    if (o->npins < MAX_PINS && !plan_has_pin(o, feat)) {
        o->pin[o->npins].feat = feat;
        o->pin[o->npins++].from = 1;
    }
}

/* Before each level that precedes a prestige class: pins for the feats it
 * wants, one member of every "any of these" feat group, and skill ranks. */
static void prestige_goals(rules *ru, const chr *c, const lvl_plan *pl, lu_opts *o, int li)
{
    int ip[MAX_CLASSES], cl[MAX_CLASSES], n = prestige_entries(ru, pl, ip, cl, MAX_CLASSES), e;

    for (e = 0; e < n; e++) {
        lu_req rq[LU_MAX_REQS];
        int nr, k, opt[LU_MAX_REQS], no = 0, held = 0;

        if (li >= ip[e])
            continue;
        nr = lu_prestige_reqs(ru, cl[e], rq, LU_MAX_REQS);
        for (k = 0; k < nr; k++) {
            if (rq[k].type == 'F') {
                plan_pin(o, rq[k].a);
            } else if (rq[k].type == 'S' && rq[k].a < MAX_SK) {
                if (rq[k].b > o->skill_goal[rq[k].a])
                    o->skill_goal[rq[k].a] = (short)rq[k].b;
            } else if (rq[k].type == 'O') {
                opt[no++] = rq[k].a;
                held |= chr_has_feat(c, rq[k].a) || plan_has_pin(o, rq[k].a);
            }
        }
        if (no && !held)
            plan_pin(o, pick_feator(ru, c, opt, no));
    }
}

/* What no pick can change is checked before the first level: the race and
 * alignment, the classes a prestige class follows from, and whether the
 * levels before it can hold the base attack bonus and skill ranks it asks
 * for. */
static int plan_check(rules *ru, const chr *c, const lvl_plan *pl, char *err, size_t n)
{
    int ip[MAX_CLASSES], cl[MAX_CLASSES], ne = prestige_entries(ru, pl, ip, cl, MAX_CLASSES), e;

    for (e = 0; e < ne; e++) {
        lu_req rq[LU_MAX_REQS];
        int nr = lu_prestige_reqs(ru, cl[e], rq, LU_MAX_REQS), k, j, bab = 0;
        int any_r = 0, ok_r = 0, any_c = 0, ok_c = 0;
        const char *nm = rules_class_str(ru, cl[e], "Label");

        for (j = 0; j < ip[e]; j++) {       /* base attack bonus at entry */
            int lv = 0, m, first = 1;

            for (m = 0; m < ip[e]; m++) {
                lv += pl->seq[m] == pl->seq[j];
                if (m < j && pl->seq[m] == pl->seq[j])
                    first = 0;
            }
            if (first)
                bab += rules_cls_bab(ru, pl->seq[j], lv);
        }
        for (k = 0; k < nr; k++) {
            if (rq[k].type == 'R') {
                any_r = 1;
                ok_r |= c->race == rq[k].a;
            } else if (rq[k].type == 'C') {
                any_c = 1;
                for (j = 0; j < ip[e]; j++)
                    ok_c |= pl->seq[j] == rq[k].a;
            } else if (rq[k].type == 'B' && bab < rq[k].a) {
                snprintf(err, n, "%s needs base attack bonus +%d by level %d; the "
                         "levels before it give +%d", nm, rq[k].a, ip[e], bab);
                return -1;
            } else if (rq[k].type == 'S' && rq[k].a < MAX_SK) {
                int classskill = 0, cap;

                for (j = 0; j < ip[e]; j++)
                    classskill |= chr_skill_cost(ru, pl->seq[j], rq[k].a) == 1;
                cap = classskill ? ip[e] + 3 : (ip[e] + 3) / 2;
                if (rq[k].b > cap) {
                    snprintf(err, n, "%s needs %d ranks of skill %d by level %d, but "
                             "only %d are possible by then (put more levels before it)",
                             nm, rq[k].b, rq[k].a, ip[e], cap);
                    return -1;
                }
            }
        }
        if (any_r && !ok_r) {
            snprintf(err, n, "%s is not open to this race", nm);
            return -1;
        }
        if (any_c && !ok_c) {
            snprintf(err, n, "%s needs earlier levels in one of the classes it follows from", nm);
            return -1;
        }
        if (!chr_align_ok(ru, cl[e], c->good_evil, c->lawful_chaotic)) {
            snprintf(err, n, "%s does not allow this alignment", nm);
            return -1;
        }
    }
    return 0;
}

int plan_run(rules *ru, chr *c, const lvl_plan *pl, rng *g, char *err, size_t n)
{
    int i, school_used = 0;
    lu_opts opts = pl->opts;

    if (!pl->pick_class && plan_check(ru, c, pl, err, n) != 0)
        return -1;
    for (i = 0; i < pl->n; i++) {
        pending p;
        int cls = pl->seq[i];

        prestige_goals(ru, c, pl, &opts, i);
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
            if (pl->fill(ru, c, &p, g, &opts, pl->ud) != 0) {
                snprintf(err, n, "stopped during level %d", i + 1);
                return -1;
            }
        } else {
            lu_autofill(ru, c, &p, g, &opts);
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
