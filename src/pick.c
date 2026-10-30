#define _POSIX_C_SOURCE 200809L
#include "pick.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "levelup.h"

/* ---- weapon families --------------------------------------------------- */

/* baseitems.2da names, per weapon, the feats that go with it; rows sharing a
 * Weapon Focus feat (unarmed, creature weapons) are one family. */
enum { R_WF, R_EWF, R_WS, R_EWS, R_IC, R_OVER, R_DEVA, R_WOC, NROLE };

static const char *role_col[NROLE] = {
    "WeaponFocusFeat", "EpicWeaponFocusFeat", "WeaponSpecializationFeat",
    "EpicWeaponSpecializationFeat", "WeaponImprovedCriticalFeat",
    "EpicWeaponOverwhelmingCriticalFeat", "EpicWeaponDevastatingCriticalFeat",
    "WeaponOfChoiceFeat"
};

#define MAX_FAM 160

typedef struct {
    int role[NROLE];
    int ranged;
    int dmg;
    int req[5];
} wfam;

static struct {
    const rules *ru;
    int nfam, nfeats;
    wfam fam[MAX_FAM];
    int *feat_fam, *feat_role;
} wc;

static void wc_build(rules *ru)
{
    const twoda *b = rules_tab(ru, "baseitems");
    int r, k, f;

    if (wc.ru == ru)
        return;
    free(wc.feat_fam);
    free(wc.feat_role);
    wc.ru = ru;
    wc.nfam = 0;
    wc.nfeats = rules_nfeats(ru);
    wc.feat_fam = malloc((size_t)wc.nfeats * sizeof(int));
    wc.feat_role = malloc((size_t)wc.nfeats * sizeof(int));
    if (!wc.feat_fam || !wc.feat_role)
        abort();
    for (f = 0; f < wc.nfeats; f++)
        wc.feat_fam[f] = wc.feat_role[f] = -1;
    for (r = 0; b && r < b->nrows && wc.nfam < MAX_FAM; r++) {
        wfam *w = &wc.fam[wc.nfam];
        int wf = twoda_int(b, r, role_col[R_WF], -1), dup = 0;
        char col[16];

        for (k = 0; k < wc.nfam; k++)
            if (wc.fam[k].role[R_WF] == wf)
                dup = 1;
        if (wf < 0 || dup)
            continue;
        for (k = 0; k < NROLE; k++)
            w->role[k] = twoda_int(b, r, role_col[k], -1);
        w->ranged = twoda_get(b, r, "RangedWeapon") != NULL;
        w->dmg = twoda_int(b, r, "NumDice", 0) * twoda_int(b, r, "DieToRoll", 0);
        for (k = 0; k < 5; k++) {
            snprintf(col, sizeof col, "ReqFeat%d", k);
            w->req[k] = twoda_int(b, r, col, -1);
        }
        for (k = 0; k < NROLE; k++)
            if (w->role[k] >= 0 && w->role[k] < wc.nfeats) {
                wc.feat_fam[w->role[k]] = wc.nfam;
                wc.feat_role[w->role[k]] = k;
            }
        wc.nfam++;
    }
}

static int proficient(const chr *c, const wfam *w)
{
    int k, any = 0;

    for (k = 0; k < 5; k++)
        if (w->req[k] >= 0) {
            any = 1;
            if (chr_has_feat(c, w->req[k]))
                return 1;
        }
    return !any;
}

/* The family of the first weapon feat of that kind (melee/ranged) the
 * character already owns, or -1. */
static int committed(const chr *c, int ranged)
{
    int i, j;

    for (i = 0; i < c->nlevels; i++)
        for (j = 0; j < c->lv[i].nfeats; j++) {
            int f = c->lv[i].feats[j], fam = f < wc.nfeats ? wc.feat_fam[f] : -1;
            if (fam >= 0 && wc.fam[fam].ranged == ranged)
                return fam;
        }
    return -1;
}

/* Whether a weapon feat suits what the character already wields. */
static int weapon_ok(const chr *c, int f)
{
    int fam = f < wc.nfeats ? wc.feat_fam[f] : -1, cm;

    if (fam < 0)
        return 1;
    if (!proficient(c, &wc.fam[fam]))
        return 0;
    cm = committed(c, wc.fam[fam].ranged);
    return cm < 0 || cm == fam;
}

/* ---- fit to the character ---------------------------------------------- */

static int ends_with3(const char *label, const char *s)
{
    size_t n = label ? strlen(label) : 0;

    return n >= 3 && strlen(s) >= 3 && !strncasecmp(label + n - 3, s, 3);
}

/* Percent weight for how well the feat suits this character (100 = neutral). */
static int fit(rules *ru, const chr *c, int f)
{
    const twoda *t = rules_feats(ru);
    static const char *mincol[NABIL] = { "MINSTR", "MINDEX", "MINCON", "MININT",
                                         "MINWIS", "MINCHA" };
    const char *label = twoda_get(t, f, "LABEL");
    int pct = 100, a, m, master = twoda_int(t, f, "MASTERFEAT", -1);
    int sm = abil_mod(chr_eff_abil(ru, c, ABIL_STR));
    int dm = abil_mod(chr_eff_abil(ru, c, ABIL_DEX));

    for (a = 0; a < NABIL; a++) {   /* comfortably above a requirement */
        int v = twoda_int(t, f, mincol[a], 0);
        if (v > 0 && chr_eff_abil(ru, c, a) >= v + 3)
            pct = pct * 3 / 2;
    }
    if (f < wc.nfeats && wc.feat_fam[f] >= 0) {   /* weapons follow the better stat */
        int ranged = wc.fam[wc.feat_fam[f]].ranged;

        pct = pct * (ranged ? (dm >= sm ? 200 : 50) : (sm >= dm ? 150 : 60)) / 100;
    }
    if (label && !strcasecmp(label, "WeapFinesse"))
        pct = pct * (dm > sm ? 400 : 10) / 100;
    else if (label && !strcasecmp(label, "Toughness"))
        pct = pct * (abil_mod(chr_eff_abil(ru, c, ABIL_CON)) <= 1 ? 200 : 100) / 100;

    /* spell feats are wasted on non-casters, and spell focus follows the school */
    m = master == 3 || master == 6;
    if (m || (label && (!strcasecmp(label, "CmbtCast") || !strcasecmp(label, "SpellPen") ||
                        !strcasecmp(label, "GreatSpellPen") || !strcasecmp(label, "Empower") ||
                        !strcasecmp(label, "ExtSpell") || !strcasecmp(label, "Quicken") ||
                        !strcasecmp(label, "MaxSpell") || !strcasecmp(label, "StillSpell") ||
                        !strcasecmp(label, "Silent")))) {
        if (chr_max_spell_level(ru, c) < 1)
            return pct / 10 > 0 ? pct / 10 : 1;
        if (m && c->school > 0) {
            const twoda *ss = rules_tab(ru, "spellschools");
            int src = master == 6 ? twoda_int(t, f, "PREREQFEAT1", f) : f;
            const char *sl = ss ? twoda_get(ss, c->school, "Label") : NULL;

            if (sl && ends_with3(twoda_get(t, src, "LABEL"), sl))
                pct = pct * 4;
            else
                pct = pct * 3 / 10;
        }
    }
    return pct > 0 ? pct : 1;
}

/* ---- the pick ---------------------------------------------------------- */

static const int window[VARIETY_MAX + 1] = { 1, 3, 5, 8 };
static const int pos_w[8] = { 64, 40, 28, 20, 14, 10, 7, 5 };

int pick_window(int variety)
{
    return window[variety < 0 ? 0 : variety > VARIETY_MAX ? VARIETY_MAX : variety];
}

int pick_pos_weight(int pos)
{
    return pos_w[pos < 0 ? 0 : pos > 7 ? 7 : pos];
}

int pick_weighted(const int *w, int n, rng *g)
{
    int k, total = 0, roll;

    for (k = 0; k < n; k++)
        total += w[k];
    if (n <= 1 || total <= 0)
        return 0;
    roll = (int)rng_below(g, (unsigned)total);
    for (k = 0; k < n - 1 && roll >= w[k]; k++)
        roll -= w[k];
    return k;
}

int pick_avoided(const lu_opts *o, int feat)
{
    int i;

    for (i = 0; o && i < o->navoid; i++)
        if (o->avoid[i] == feat)
            return 1;
    return 0;
}

/* The feat to take now toward `f`: itself when legal, else the first missing
 * prerequisite feat that is. -1 if nothing can be taken yet. */
static int toward(rules *ru, const chr *c, int cls, int bonus, int f, int depth)
{
    const twoda *t = rules_feats(ru);
    static const char *orcol[5] = { "OrReqFeat0", "OrReqFeat1", "OrReqFeat2",
                                    "OrReqFeat3", "OrReqFeat4" };
    int i, g, r, anyor = 0, hasor = 0;

    if (f < 0 || f >= t->nrows)
        return -1;
    if (feat_selectable(ru, c, cls, f, bonus))
        return f;
    if (depth <= 0 || (chr_has_feat(c, f) && !twoda_int(t, f, "GAINMULTIPLE", 0)))
        return -1;
    for (i = 1; i <= 2; i++) {
        char col[16];

        snprintf(col, sizeof col, "PREREQFEAT%d", i);
        g = twoda_int(t, f, col, -1);
        if (g >= 0 && !chr_has_feat(c, g) && (r = toward(ru, c, cls, bonus, g, depth - 1)) >= 0)
            return r;
    }
    for (i = 0; i < 5; i++) {
        g = twoda_int(t, f, orcol[i], -1);
        if (g >= 0) {
            anyor = 1;
            hasor |= chr_has_feat(c, g);
        }
    }
    for (i = 0; anyor && !hasor && i < 5; i++) {
        g = twoda_int(t, f, orcol[i], -1);
        if (g >= 0 && (r = toward(ru, c, cls, bonus, g, depth - 1)) >= 0)
            return r;
    }
    return -1;
}

int pick_pinned(rules *ru, const chr *c, int cls, int bonus, const lu_opts *o)
{
    int i, r;

    for (i = 0; o && i < o->npins; i++) {
        if (c->nlevels < o->pin[i].from)
            continue;
        if ((r = toward(ru, c, cls, bonus, o->pin[i].feat, 4)) >= 0)
            return r;
    }
    return -1;
}
static const int swap_pct[VARIETY_MAX + 1] = { 0, 25, 40, 60 };

/* A fresh weapon: with some probability another proficient weapon of the
 * same kind takes the place of the one the list names. */
static int swap_weapon(rules *ru, const chr *c, int cls, int bonus, int f,
                       int variety, rng *g)
{
    int fam = wc.feat_fam[f], role = wc.feat_role[f], tries, pool[MAX_FAM], n = 0, k;

    if (committed(c, wc.fam[fam].ranged) >= 0 ||
        (int)rng_below(g, 100) >= swap_pct[variety])
        return f;
    for (k = 0; k < wc.nfam; k++)
        if (wc.fam[k].ranged == wc.fam[fam].ranged && wc.fam[k].dmg >= 6 &&
            proficient(c, &wc.fam[k]) && wc.fam[k].role[role] >= 0)
            pool[n++] = k;
    for (tries = 0; n && tries < 6; tries++) {
        int cand = wc.fam[pool[rng_below(g, (unsigned)n)]].role[role];

        if (cand < wc.nfeats && feat_selectable(ru, c, cls, cand, bonus))
            return cand;
    }
    return f;
}

/* Epic levels outlast every package list, so what is left is chosen from the
 * legal pool by fit, with weapon feats limited to the weapons already used. */
int pick_epic(rules *ru, const chr *c, const int *pool, int np, rng *g)
{
    int w[1100], k, any = 0;

    if (np > 1100)
        np = 1100;
    wc_build(ru);
    for (k = 0; k < np; k++) {
        w[k] = weapon_ok(c, pool[k]) ? fit(ru, c, pool[k]) : 0;
        any |= w[k] > 0;
    }
    if (!any)
        return pool[rng_below(g, (unsigned)np)];
    return pool[pick_weighted(w, np, g)];
}

int pick_feator(rules *ru, const chr *c, const int *opt, int n)
{
    int k, fam;

    wc_build(ru);
    for (k = 0; k < n; k++)
        if (chr_has_feat(c, opt[k]))
            return opt[k];
    for (k = 0; k < n; k++) {
        fam = opt[k] < wc.nfeats ? wc.feat_fam[opt[k]] : -1;
        if (fam >= 0 && committed(c, wc.fam[fam].ranged) == fam && proficient(c, &wc.fam[fam]))
            return opt[k];
    }
    for (k = 0; k < n; k++) {
        fam = opt[k] < wc.nfeats ? wc.feat_fam[opt[k]] : -1;
        if (fam >= 0 && !wc.fam[fam].ranged && wc.fam[fam].dmg >= 6 &&
            proficient(c, &wc.fam[fam]))
            return opt[k];
    }
    for (k = 0; k < n; k++) {
        fam = opt[k] < wc.nfeats ? wc.feat_fam[opt[k]] : -1;
        if (fam < 0 || proficient(c, &wc.fam[fam]))
            return opt[k];
    }
    return n ? opt[0] : -1;
}

int pick_feat(rules *ru, const chr *c, int cls, int bonus, const lu_opts *o, rng *g)
{
    int cand[8], w[8], nc = 0, k, r, f, win, variety = o ? o->variety : 0;

    if (variety < 0)
        variety = 0;
    if (variety > VARIETY_MAX)
        variety = VARIETY_MAX;
    win = window[variety];
    wc_build(ru);
    for (k = 0; k < 2 && !nc; k++) {
        const twoda *t = lu_pref_tab(ru, c, cls, "FeatPref2DA", k);

        for (r = 0; t && r < t->nrows && nc < win; r++) {
            int j, dup = 0;

            f = twoda_int(t, r, "FeatIndex", -1);
            for (j = 0; j < nc; j++)
                dup |= cand[j] == f;
            if (f < 0 || dup || pick_avoided(o, f) || !feat_selectable(ru, c, cls, f, bonus) ||
                !weapon_ok(c, f))
                continue;
            cand[nc++] = f;
        }
    }
    if (!nc)
        return -1;
    if (nc == 1 || variety == 0) {
        f = cand[0];
    } else {
        for (k = 0; k < nc; k++)
            w[k] = pos_w[k] * fit(ru, c, cand[k]);
        f = cand[pick_weighted(w, nc, g)];
    }
    if (f < wc.nfeats && wc.feat_fam[f] >= 0 && variety > 0)
        f = swap_weapon(ru, c, cls, bonus, f, variety, g);
    return f;
}
