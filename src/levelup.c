#define _POSIX_C_SOURCE 200809L
#include "levelup.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pick.h"
#include "util.h"

static void seterr(char *err, size_t n, const char *fmt, ...)
{
    va_list ap;

    if (!err || !n)
        return;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

/* ---- alignment --------------------------------------------------------- */

int chr_align_ok(rules *ru, int cls, int ge, int lc)
{
    int mask = rules_class_int(ru, cls, "AlignRestrict", 0);
    int type = rules_class_int(ru, cls, "AlignRstrctType", 0);
    int inv = rules_class_int(ru, cls, "InvertRestrict", 0);
    int lcbit = lc >= 70 ? 0x02 : lc <= 30 ? 0x04 : 0x01;
    int gebit = ge >= 70 ? 0x08 : ge <= 30 ? 0x10 : 0x01;
    int hit = ((type & 1) && (lcbit & mask)) || ((type & 2) && (gebit & mask));

    if (!type)
        return 1;
    return inv ? hit : !hit;
}

/* ---- feat pools -------------------------------------------------------- */

/* The List value of feat in the class feat table, or -1 when absent. */
static int class_list_value(rules *ru, int cls, int feat)
{
    const twoda *t = rules_class_tab(ru, cls, "FeatsTable");
    int r;

    for (r = 0; t && r < t->nrows; r++)
        if (twoda_int(t, r, "FeatIndex", -1) == feat)
            return twoda_int(t, r, "List", -1);
    return -1;
}

static int class_max_sl(rules *ru, const chr *c, int cls)
{
    int lv = chr_caster_level(ru, c, cls), sl, m = 0, best = -1;
    const char *ab;

    if (!lv || !rules_class_tab(ru, cls, "SpellGainTable"))
        return -1;
    ab = rules_class_str(ru, cls, "SpellcastingAbil");
    if (ab) {
        int a = !strcasecmp(ab, "INT") ? ABIL_INT : !strcasecmp(ab, "WIS") ?
                ABIL_WIS : ABIL_CHA;
        m = abil_mod(chr_eff_abil(ru, c, a));
    }
    for (sl = 0; sl < NSPLVL; sl++) {
        int base = rules_spells_per_day(ru, cls, lv, sl);
        if (base >= 0 && (base > 0 || spell_bonus(m, sl) > 0) && sl > best)
            best = sl;
    }
    return best;
}

int lu_class_max_spell_level(rules *ru, const chr *c, int cls)
{
    return class_max_sl(ru, c, cls);
}

int chr_max_spell_level(rules *ru, const chr *c)
{
    int cls, best = -1;

    for (cls = 0; cls < rules_nclasses(ru); cls++) {
        int m = class_max_sl(ru, c, cls);
        if (m > best)
            best = m;
    }
    return best;
}

int chr_max_arcane_spell_level(rules *ru, const chr *c)
{
    int cls, best = -1;

    for (cls = 0; cls < rules_nclasses(ru); cls++) {
        int m;

        if (rules_class_int(ru, cls, "Arcane", 0) != 1)
            continue;
        m = class_max_sl(ru, c, cls);
        if (m > best)
            best = m;
    }
    return best;
}

static int skill_ranks(const chr *c, int sk)
{
    int i, n = 0;

    for (i = 0; i < c->nlevels; i++)
        n += c->lv[i].ranks[sk];
    return n;
}

static int feat_prereqs(rules *ru, const chr *c, int cls, int feat)
{
    const twoda *t = rules_feats(ru);
    static const char *mincol[NABIL] = { "MINSTR", "MINDEX", "MINCON", "MININT",
                                         "MINWIS", "MINCHA" };
    static const char *orcol[5] = { "OrReqFeat0", "OrReqFeat1", "OrReqFeat2",
                                    "OrReqFeat3", "OrReqFeat4" };
    int a, i, v, anyor = 0, okor = 0;
    int level = c->nlevels;

    for (a = 0; a < NABIL; a++) {
        v = twoda_int(t, feat, mincol[a], 0);
        if (v > 0 && chr_eff_abil(ru, c, a) < v)
            return 0;
    }
    v = twoda_int(t, feat, "MINATTACKBONUS", 0);
    if (v > 0 && chr_bab(ru, c) < v)
        return 0;
    v = twoda_int(t, feat, "MINSPELLLVL", 0);
    if (v > 0 && chr_max_spell_level(ru, c) < v)
        return 0;
    for (i = 1; i <= 2; i++) {
        char col[32];
        snprintf(col, sizeof col, "PREREQFEAT%d", i);
        v = twoda_int(t, feat, col, -1);
        if (v >= 0 && !chr_has_feat(c, v))
            return 0;
    }
    for (i = 0; i < 5; i++) {
        v = twoda_int(t, feat, orcol[i], -1);
        if (v >= 0) {
            anyor = 1;
            if (chr_has_feat(c, v))
                okor = 1;
        }
    }
    if (anyor && !okor)
        return 0;
    for (i = 0; i < 2; i++) {
        int sk = twoda_int(t, feat, i ? "REQSKILL2" : "REQSKILL", -1);
        int need = twoda_int(t, feat, i ? "ReqSkillMinRanks2" : "ReqSkillMinRanks", 0);
        if (sk >= 0 && sk < MAX_SK && skill_ranks(c, sk) < need)
            return 0;
    }
    v = twoda_int(t, feat, "MinLevel", 0);
    if (v > 0) {
        int mc = twoda_int(t, feat, "MinLevelClass", -1);
        if ((mc >= 0 ? chr_class_level(c, mc) : level) < v)
            return 0;
    }
    v = twoda_int(t, feat, "MaxLevel", 0);
    if (v > 0 && level > v)
        return 0;
    if (twoda_int(t, feat, "PreReqEpic", 0) && level < 21)
        return 0;
    (void)cls;
    return 1;
}

/* ---- prestige entry ---------------------------------------------------- */

static int entry_checks = 1;

void lu_set_entry_checks(int on)
{
    entry_checks = on;
}

int lu_prestige_supported(rules *ru, int cls)
{
    static const char *const ok[] = { "Champion_Torm", "Dwarven_Defender", "Pale_Master", "Arcane_Archer", "Shifter", "WeaponMaster", "Dragon_Disciple",
        "Shadowdancer", "Harper", "Assassin", "Blackguard", "Purple_Dragon_Knight" };
    const char *l = rules_class_str(ru, cls, "Label");
    size_t i;

    for (i = 0; l && i < sizeof ok / sizeof ok[0]; i++)
        if (!strcasecmp(l, ok[i]))
            return 1;
    return 0;
}

int lu_prestige_reqs(rules *ru, int cls, lu_req *out, int cap)
{
    const char *name = twoda_get(rules_classes(ru), cls, "PreReqTable");
    const twoda *t = name ? rules_tab(ru, name) : NULL;
    int r, n = 0;

    for (r = 0; t && r < t->nrows && n < cap; r++) {
        const char *ty = twoda_get(t, r, "ReqType");
        int a = twoda_int(t, r, "ReqParam1", -1), b = twoda_int(t, r, "ReqParam2", 0);
        char k;

        if (!ty || a < 0)
            continue;
        k = !strcasecmp(ty, "SKILL") ? 'S' : !strcasecmp(ty, "FEAT") ? 'F' :
            !strcasecmp(ty, "FEATOR") ? 'O' : !strcasecmp(ty, "RACE") ? 'R' :
            !strcasecmp(ty, "BAB") ? 'B' : !strcasecmp(ty, "ARCSPELL") ? 'A' :
            !strcasecmp(ty, "SPELL") ? 'P' : !strcasecmp(ty, "CLASSOR") ? 'C' : 0;
        if (!k)                         /* VAR: a module switch, always open */
            continue;
        out[n].type = k;
        out[n].a = a;
        out[n].b = b;
        n++;
    }
    return n;
}

static const char *label_of(const twoda *t, int row, const char *col)
{
    const char *s = t && row >= 0 && row < t->nrows ? twoda_get(t, row, col) : NULL;

    return s ? s : "?";
}

int lu_prestige_ok(rules *ru, const chr *c, int cls, char *why, size_t n)
{
    lu_req rq[LU_MAX_REQS];
    int nr = lu_prestige_reqs(ru, cls, rq, LU_MAX_REQS), i;
    int any[128] = { 0 }, hit[128] = { 0 };     /* OR groups by type letter */

    for (i = 0; i < nr; i++) {
        const lu_req *q = &rq[i];

        switch (q->type) {
        case 'S':
            if (q->a < MAX_SK && skill_ranks(c, q->a) < q->b) {
                seterr(why, n, "needs %d ranks of %s (has %d)", q->b,
                       label_of(rules_tab(ru, "skills"), q->a, "Label"),
                       skill_ranks(c, q->a));
                return 0;
            }
            break;
        case 'F':
            if (!chr_has_feat(c, q->a)) {
                char blk[100];

                lu_feat_block(ru, c, q->a, blk, sizeof blk);
                seterr(why, n, "needs the feat %s%s%s", label_of(rules_feats(ru), q->a, "LABEL"),
                       *blk ? ": " : "", blk);
                return 0;
            }
            break;
        case 'B':
            if (chr_bab(ru, c) < q->a) {
                seterr(why, n, "needs base attack bonus +%d (has +%d)", q->a,
                       chr_bab(ru, c));
                return 0;
            }
            break;
        case 'A':
            if (chr_max_arcane_spell_level(ru, c) < q->a) {
                seterr(why, n, "needs to cast level %d arcane spells", q->a);
                return 0;
            }
            break;
        case 'P':
            if (chr_max_spell_level(ru, c) < q->a) {
                seterr(why, n, "needs to cast level %d spells", q->a);
                return 0;
            }
            break;
        case 'O':
        case 'R':
        case 'C':
            any[(int)q->type] = 1;
            if (q->type == 'O' ? chr_has_feat(c, q->a) : q->type == 'R' ?
                c->race == q->a : chr_class_level(c, q->a) > 0)
                hit[(int)q->type] = 1;
            break;
        }
    }
    if (any['O'] && !hit['O']) {
        seterr(why, n, "needs a weapon feat (Weapon Focus in a weapon it allows)");
        return 0;
    }
    if (any['R'] && !hit['R']) {
        seterr(why, n, "not open to this race");
        return 0;
    }
    if (any['C'] && !hit['C']) {
        seterr(why, n, "needs levels in one of the classes it follows from");
        return 0;
    }
    return 1;
}

void lu_feat_abil_need(rules *ru, int feat, int *need)
{
    const twoda *t = rules_feats(ru);
    static const char *mincol[NABIL] = { "MINSTR", "MINDEX", "MINCON", "MININT",
                                         "MINWIS", "MINCHA" };
    int a, i, v, depth;

    for (depth = 0; depth < 6 && feat >= 0 && feat < t->nrows; depth++) {
        int next = -1;

        for (a = 0; a < NABIL; a++) {
            v = twoda_int(t, feat, mincol[a], 0);
            if (v > need[a])
                need[a] = v;
        }
        for (i = 1; i <= 2; i++) {
            char col[16];

            snprintf(col, sizeof col, "PREREQFEAT%d", i);
            v = twoda_int(t, feat, col, -1);
            if (v >= 0 && next >= 0)
                lu_feat_abil_need(ru, v, need);
            else if (v >= 0)
                next = v;
        }
        feat = next;
    }
}

void lu_feat_block(rules *ru, const chr *c, int feat, char *why, size_t n)
{
    const twoda *t = rules_feats(ru);
    static const char *mincol[NABIL] = { "MINSTR", "MINDEX", "MINCON", "MININT",
                                         "MINWIS", "MINCHA" };
    static const char *const aname[NABIL] = { "Strength", "Dexterity", "Constitution",
                                              "Intelligence", "Wisdom", "Charisma" };
    int a, i, v, depth;

    if (n)
        why[0] = '\0';
    for (depth = 0; depth < 6 && feat >= 0 && feat < t->nrows; depth++) {
        int next = -1;

        for (a = 0; a < NABIL; a++) {
            v = twoda_int(t, feat, mincol[a], 0);
            if (v > 0 && chr_eff_abil(ru, c, a) < v) {
                seterr(why, n, "%s needs %s %d (has %d)", label_of(t, feat, "LABEL"),
                       aname[a], v, chr_eff_abil(ru, c, a));
                return;
            }
        }
        v = twoda_int(t, feat, "MINATTACKBONUS", 0);
        if (v > 0 && chr_bab(ru, c) < v) {
            seterr(why, n, "%s needs base attack bonus +%d", label_of(t, feat, "LABEL"), v);
            return;
        }
        for (i = 1; i <= 2 && next < 0; i++) {
            char col[16];

            snprintf(col, sizeof col, "PREREQFEAT%d", i);
            v = twoda_int(t, feat, col, -1);
            if (v >= 0 && !chr_has_feat(c, v))
                next = v;
        }
        if (next < 0)
            return;
        feat = next;
    }
}

int feat_selectable(rules *ru, const chr *c, int cls, int feat, int bonus)
{
    const twoda *t = rules_feats(ru);
    int lst;

    if (feat < 0 || feat >= t->nrows || !twoda_get(t, feat, "FEAT"))
        return 0;
    if (chr_has_feat(c, feat) && !twoda_int(t, feat, "GAINMULTIPLE", 0))
        return 0;
    lst = class_list_value(ru, cls, feat);
    if (bonus) {
        if (lst != 1 && lst != 2)
            return 0;
    } else if (!(twoda_int(t, feat, "ALLCLASSESCANUSE", 0) == 1 || lst == 0 ||
                 lst == 2)) {
        return 0;
    }
    return feat_prereqs(ru, c, cls, feat);
}

int lu_feat_pool(rules *ru, const chr *c, int bonus, int *out, int cap)
{
    int cls = c->lv[c->nlevels - 1].cls, f, n = 0;

    for (f = 0; f < rules_nfeats(ru) && n < cap; f++)
        if (feat_selectable(ru, c, cls, f, bonus))
            out[n++] = f;
    return n;
}

/* ---- packages ---------------------------------------------------------- */

/* A class's first level takes one of its playable packages (packages.2da
 * rows with that ClassID); wizards get theirs from the school instead. */
static void pick_package(rules *ru, chr *c, int cls, rng *g)
{
    const twoda *pk = rules_tab(ru, "packages");
    int rows[64], n = 0, r;

    for (r = 0; pk && r < pk->nrows && n < 64; r++)
        if (twoda_int(pk, r, "ClassID", -1) == cls &&
            twoda_int(pk, r, "PlayerClass", 0) == 1)
            rows[n++] = r;
    if (n)
        c->pkg[cls] = rows[rng_below(g, (unsigned)n)];
}

/* The preference list a package column names, or NULL. which 0 is the
 * character's package, 1 the class's default package (a shorter or empty
 * list falls back to it). */
const twoda *lu_pref_tab(rules *ru, const chr *c, int cls, const char *col,
                         int which)
{
    const twoda *pk = rules_tab(ru, "packages");
    int row = which ? rules_class_int(ru, cls, "Package", -1) : chr_package(ru, c, cls);
    const char *name = pk && row >= 0 && row < pk->nrows ? twoda_get(pk, row, col) : NULL;

    if (which && row == chr_package(ru, c, cls))
        return NULL;
    return name ? rules_tab(ru, name) : NULL;
}

/* ---- spells ------------------------------------------------------------ */

const char *spell_list_column(rules *ru, int cls)
{
    const char *l = rules_class_str(ru, cls, "Label");

    if (!l)
        return NULL;
    if (!strcasecmp(l, "Bard") || !strcasecmp(l, "Cleric") ||
        !strcasecmp(l, "Druid") || !strcasecmp(l, "Paladin") ||
        !strcasecmp(l, "Ranger"))
        return l;
    if (!strcasecmp(l, "Wizard") || !strcasecmp(l, "Sorcerer"))
        return "Wiz_Sorc";
    return NULL;
}

static int spell_known(const chr *c, int cls, int sl, int spell)
{
    int i, j;

    for (i = 0; i < c->nlevels; i++) {
        if (c->lv[i].cls != cls)
            continue;
        for (j = 0; j < c->lv[i].nknown[sl]; j++)
            if (c->lv[i].known[sl][j] == spell)
                return 1;
    }
    return 0;
}

static int spell_level(rules *ru, int cls, int spell)
{
    const twoda *t = rules_tab(ru, "spells");
    const char *col = spell_list_column(ru, cls), *v;

    v = t && col && spell >= 0 && spell < t->nrows ? twoda_get(t, spell, col) : NULL;
    return v ? atoi(v) : -1;
}

static int spell_eligible(rules *ru, const chr *c, int cls, int sl, int r)
{
    const twoda *t = rules_tab(ru, "spells");

    return spell_level(ru, cls, r) == sl && twoda_get(t, r, "Name") &&
           twoda_int(t, r, "UserType", 1) == 1 && !spell_known(c, cls, sl, r) &&
           !chr_spell_banned(ru, c, r);
}

int lu_spell_pool(rules *ru, const chr *c, int cls, int sl, int *out, int cap)
{
    const twoda *t = rules_tab(ru, "spells");
    int r, n = 0;

    if (!t || !spell_list_column(ru, cls))
        return 0;
    for (r = 0; r < t->nrows && n < cap; r++)
        if (spell_eligible(ru, c, cls, sl, r))
            out[n++] = r;
    return n;
}

/* A spell from the package's list: the next few that the class may learn now,
 * weighed by list position (and, for a specialist, towards its school). With
 * sl >= 0 it must be of that level, otherwise of any level 1..top; school > 0
 * restricts it to that school. *lvl receives the spell's level; -1 if none. */
static int pref_spell(rules *ru, const chr *c, int cls, int sl, int top,
                      int school, int *lvl, const lu_opts *o, rng *g)
{
    int cand[8], clvl[8], w[8], nc = 0, win = pick_window(o ? o->variety : 0), r, k;

    for (k = 0; k < 2 && !nc; k++) {
        const twoda *t = lu_pref_tab(ru, c, cls, "SpellPref2DA", k);

        for (r = 0; t && r < t->nrows && nc < win; r++) {
            int sp = twoda_int(t, r, "SpellIndex", -1), l = spell_level(ru, cls, sp);

            if (sp < 0 || l < 0 || (sl >= 0 ? l != sl : l < 1 || l > top))
                continue;
            if (school > 0 && !chr_spell_in_school(ru, sp, school))
                continue;
            if (spell_eligible(ru, c, cls, l, sp)) {
                cand[nc] = sp;
                clvl[nc] = l;
                w[nc] = pick_pos_weight(nc) * (c->school > 0 &&
                        chr_spell_in_school(ru, sp, c->school) ? 3 : 1);
                nc++;
            }
        }
    }
    if (!nc)
        return -1;
    k = pick_weighted(w, nc, g);
    *lvl = clvl[k];
    return cand[k];
}

static int learn(chr *c, pending *p, int sl, int spell)
{
    lvl_rec *lr = &c->lv[c->nlevels - 1];

    if (lr->nknown[sl] >= MAX_KNOWN)
        return -1;
    lr->known[sl][lr->nknown[sl]++] = (uint16_t)spell;
    if (p->spells[sl] > 0)
        p->spells[sl]--;
    return 0;
}

/* ---- begin / end ------------------------------------------------------- */

static int is_wizard_like(rules *ru, int cls)
{
    return rules_class_int(ru, cls, "MemorizesSpells", 0) == 1 &&
           rules_class_int(ru, cls, "SpellbookRestricted", 0) == 1 &&
           rules_class_int(ru, cls, "Arcane", 0) == 1;
}

int lu_begin(rules *ru, chr *c, int cls, hp_mode hm, rng *g, pending *p,
             char *err, size_t errlen)
{
    lvl_rec *lr;
    int li = c->nlevels, clvl, hd, sl, i, n, f;
    int autof[64];

    if (li >= MAX_LEVELS) {
        seterr(err, errlen, "level cap reached");
        return -1;
    }
    if (cls < 0 || cls >= rules_nclasses(ru) ||
        !rules_class_int(ru, cls, "PlayerClass", 0)) {
        seterr(err, errlen, "class %d is not a player class", cls);
        return -1;
    }
    if (twoda_get(rules_classes(ru), cls, "PreReqTable")) {
        int max = rules_class_int(ru, cls, "MaxLevel", 0);
        char why[160];

        if (!lu_prestige_supported(ru, cls)) {
            seterr(err, errlen, "prestige class %d not supported yet", cls);
            return -1;
        }
        if (li == 0) {
            seterr(err, errlen, "a prestige class cannot be the first class");
            return -1;
        }
        if (max > 0 && chr_class_level(c, cls) >= max) {
            seterr(err, errlen, "class %d is limited to %d levels", cls, max);
            return -1;
        }
        if (rules_class_int(ru, cls, "ArcSpellLvlMod", 0) > 0) {
            int k;

            for (k = 0; k < rules_nclasses(ru); k++)
                if (chr_class_level(c, k) > 0 && rules_class_int(ru, k, "Arcane", 0) == 1 &&
                    rules_class_tab(ru, k, "SpellKnownTable")) {
                    seterr(err, errlen, "%s levels on top of sorcerer or bard levels are "
                           "not modelled yet", rules_class_str(ru, cls, "Label"));
                    return -1;
                }
        }
        if (entry_checks && chr_class_level(c, cls) == 0 &&
            !lu_prestige_ok(ru, c, cls, why, sizeof why)) {
            seterr(err, errlen, "cannot take %s: %s",
                   rules_class_str(ru, cls, "Label"), why);
            return -1;
        }
    }
    if (!chr_align_ok(ru, cls, c->good_evil, c->lawful_chaotic)) {
        seterr(err, errlen, "alignment not allowed for class %d", cls);
        return -1;
    }

    memset(p, 0, sizeof *p);
    lr = &c->lv[li];
    memset(lr, 0, sizeof *lr);
    lr->cls = cls;
    lr->abil = -1;
    lr->epic = li >= 20;
    c->nlevels++;
    clvl = chr_class_level(c, cls);
    for (i = 0; i < NABIL; i++)         /* the class's stat gains join the stored scores */
        c->abil[i] += chr_stat_gain(ru, cls, clvl, i);

    hd = rules_class_int(ru, cls, "HitDie", 4);
    if (li == 0 || hm == HP_MAX)
        lr->hp = hd;
    else if (hm == HP_AVG)
        lr->hp = hd / 2 + 1;
    else
        lr->hp = 1 + (int)rng_below(g, (unsigned)hd);

    n = chr_auto_feats(ru, c, li, autof, 64);
    for (i = 0; i < n && lr->nfeats < MAX_LFEATS; i++)
        lr->feats[lr->nfeats++] = (uint16_t)autof[i];

    p->skill_pts = chr_skillpts_for_level(ru, c, li) + c->unspent_skillpts;
    p->general = chr_general_feat_slots(ru, c, li);
    p->bonus = chr_bonus_feat_slots(ru, c, li);
    p->abil_due = ((li + 1) % 4 == 0);
    p->need_domains = clvl == 1 && rules_class_int(ru, cls, "PickDomains", 0) > 0;
    p->need_school = clvl == 1 && rules_class_int(ru, cls, "PickSchool", 0) > 0;
    if (clvl == 1 && cls < MAX_CLS && !p->need_school)
        pick_package(ru, c, cls, g);

    if (rules_class_tab(ru, cls, "SpellKnownTable")) {
        for (sl = 0; sl < NSPLVL; sl++) {
            int now = rules_spells_known(ru, cls, clvl, sl);
            int before = clvl > 1 ? rules_spells_known(ru, cls, clvl - 1, sl) : 0;
            if (now < 0)
                now = 0;
            if (before < 0)
                before = 0;
            p->spells[sl] = now > before ? now - before : 0;
        }
    } else if (is_wizard_like(ru, cls)) {
        /* A wizard knows every cantrip; the spellbook starts with 3 + Int
         * modifier first-level spells and grows by two each level. */
        int pool[64], np = lu_spell_pool(ru, c, cls, 0, pool, 64);

        for (i = 0; i < np; i++)
            learn(c, p, 0, pool[i]);
        if (clvl == 1) {
            f = 3 + abil_mod(chr_eff_abil(ru, c, ABIL_INT));
            p->spells[1] = f > 0 ? f : 0;
        } else {
            p->spells[NSPLVL - 1] = 0;
            p->wiz_any = 2;
        }
    }
    return 0;
}

void lu_end(chr *c, const pending *p)
{
    c->unspent_skillpts = p->skill_pts;
    c->lv[c->nlevels - 1].skillpts = p->skill_pts;
}

/* ---- picks ------------------------------------------------------------- */

int lu_pick_ability(rules *ru, chr *c, pending *p, int a, char *err, size_t n)
{
    int li = c->nlevels - 1, before;

    if (!p->abil_due) {
        seterr(err, n, "no ability increase is due");
        return -1;
    }
    if (a < 0 || a >= NABIL) {
        seterr(err, n, "bad ability");
        return -1;
    }
    /* the level's skill points use the Int modifier after the increase */
    before = chr_skillpts_for_level(ru, c, li);
    c->abil[a]++;
    c->lv[li].abil = a;
    p->skill_pts += chr_skillpts_for_level(ru, c, li) - before;
    p->abil_due = 0;
    return 0;
}

static int skill_max_ranks(int total_level, int class_skill)
{
    return class_skill ? total_level + 3 : (total_level + 3) / 2;
}

int lu_buy_skill(rules *ru, chr *c, pending *p, int sk, int ranks, char *err,
                 size_t n)
{
    lvl_rec *lr = &c->lv[c->nlevels - 1];
    int cost = sk >= 0 && sk < MAX_SK ? chr_skill_cost(ru, lr->cls, sk) : 0;

    if (cost == 0) {
        seterr(err, n, "skill %d unavailable to this class", sk);
        return -1;
    }
    if (ranks < 1 || cost * ranks > p->skill_pts) {
        seterr(err, n, "not enough skill points");
        return -1;
    }
    if (skill_ranks(c, sk) + ranks > skill_max_ranks(c->nlevels, cost == 1)) {
        seterr(err, n, "skill %d would exceed its rank cap", sk);
        return -1;
    }
    lr->ranks[sk] += ranks;
    p->skill_pts -= cost * ranks;
    return 0;
}

int lu_pick_feat(rules *ru, chr *c, pending *p, int feat, int bonus, char *err,
                 size_t n)
{
    lvl_rec *lr = &c->lv[c->nlevels - 1];

    if ((bonus ? p->bonus : p->general) <= 0) {
        seterr(err, n, "no %s feat slot left", bonus ? "bonus" : "general");
        return -1;
    }
    if (!feat_selectable(ru, c, lr->cls, feat, bonus) || lr->nfeats >= MAX_LFEATS) {
        seterr(err, n, "feat %d is not selectable now", feat);
        return -1;
    }
    lr->feats[lr->nfeats++] = (uint16_t)feat;
    if (bonus)
        p->bonus--;
    else
        p->general--;
    return 0;
}

int lu_pick_spell(rules *ru, chr *c, pending *p, int sl, int spell, char *err,
                  size_t n)
{
    int cls = c->lv[c->nlevels - 1].cls, pool[256], np, i, ok = 0;

    if (sl < 0 || sl >= NSPLVL) {
        seterr(err, n, "bad spell level");
        return -1;
    }
    if (p->spells[sl] <= 0 &&
        !(p->wiz_any > 0 && sl >= 1 && sl <= class_max_sl(ru, c, cls))) {
        seterr(err, n, "no spell of level %d is owed", sl);
        return -1;
    }
    np = lu_spell_pool(ru, c, cls, sl, pool, 256);
    for (i = 0; i < np; i++)
        if (pool[i] == spell)
            ok = 1;
    if (!ok) {
        seterr(err, n, "spell %d is not available at level %d", spell, sl);
        return -1;
    }
    if (p->spells[sl] <= 0)
        p->wiz_any--;
    return learn(c, p, sl, spell);
}

int lu_pick_domains(rules *ru, chr *c, pending *p, int d1, int d2, char *err,
                    size_t n)
{
    const twoda *d = rules_tab(ru, "domains");
    lvl_rec *lr = &c->lv[c->nlevels - 1];
    int i, f;
    int pick[2] = { d1, d2 };

    if (!p->need_domains || !d || d1 == d2 || d1 < 0 || d2 < 0 ||
        d1 >= d->nrows || d2 >= d->nrows || !twoda_get(d, d1, "Label") ||
        !twoda_get(d, d2, "Label")) {
        seterr(err, n, "bad domain choice");
        return -1;
    }
    c->domain1 = d1;
    c->domain2 = d2;
    p->need_domains = 0;
    for (i = 0; i < 2; i++) {
        f = twoda_int(d, pick[i], "GrantedFeat", -1);
        if (f >= 0 && !chr_has_feat(c, f) && lr->nfeats < MAX_LFEATS)
            lr->feats[lr->nfeats++] = (uint16_t)f;
    }
    return 0;
}

int lu_pick_school(rules *ru, chr *c, pending *p, int school, char *err, size_t n)
{
    const twoda *ss = rules_tab(ru, "spellschools");

    if (!p->need_school || school < 0 || !ss || school >= ss->nrows) {
        seterr(err, n, "bad school choice");
        return -1;
    }
    c->school = school;
    p->need_school = 0;
    return 0;
}

/* ---- autofill ---------------------------------------------------------- */

static int primary_ability(rules *ru, int cls)
{
    const char *s = rules_class_str(ru, cls, "PrimaryAbil");

    if (!s)
        return ABIL_STR;
    if (!strcasecmp(s, "DEX")) return ABIL_DEX;
    if (!strcasecmp(s, "CON")) return ABIL_CON;
    if (!strcasecmp(s, "INT")) return ABIL_INT;
    if (!strcasecmp(s, "WIS")) return ABIL_WIS;
    if (!strcasecmp(s, "CHA")) return ABIL_CHA;
    return ABIL_STR;
}

static int abil_by_name(const char *s)
{
    static const char *n[NABIL] = { "STR", "DEX", "CON", "INT", "WIS", "CHA" };
    int a;

    for (a = 0; s && a < NABIL; a++)
        if (!strcasecmp(s, n[a]))
            return a;
    return -1;
}

/* Percent weight of putting another rank into a skill: keeps going where
 * ranks already are and favours skills keyed to strong abilities. */
static int skill_fit(rules *ru, const chr *c, int sk)
{
    int a = abil_by_name(twoda_get(rules_skills(ru), sk, "KeyAbility")), pct = 100;

    if (skill_ranks(c, sk) > 0)
        pct *= 8;
    if (a >= 0) {
        int m = abil_mod(chr_eff_abil(ru, c, a));
        pct = pct * (100 + 20 * m) / 100;
    }
    return pct < 20 ? 20 : pct;
}

int lu_spell_level(rules *ru, int cls, int spell)
{
    return spell_level(ru, cls, spell);
}

void lu_auto_domains(rules *ru, chr *c, pending *p, rng *g)
{
    const twoda *d = rules_tab(ru, "domains");
    const twoda *pk = rules_tab(ru, "packages");
    int cls = c->lv[c->nlevels - 1].cls, row = chr_package(ru, c, cls);
    int valid[64], nv = 0, d1, d2, i;

    if (!p->need_domains)
        return;
    for (i = 0; i < d->nrows && nv < 64; i++)   /* skip blank rows */
        if (twoda_get(d, i, "Label"))
            valid[nv++] = i;
    d1 = pk ? twoda_int(pk, row, "Domain1", -1) : -1;   /* the package's pair */
    d2 = pk ? twoda_int(pk, row, "Domain2", -1) : -1;
    if (lu_pick_domains(ru, c, p, d1, d2, NULL, 0) != 0) {
        d1 = valid[rng_below(g, (unsigned)nv)];
        do
            d2 = valid[rng_below(g, (unsigned)nv)];
        while (d2 == d1);
        lu_pick_domains(ru, c, p, d1, d2, NULL, 0);
    }
}

void lu_auto_school(rules *ru, chr *c, pending *p)
{
    if (p->need_school)
        lu_pick_school(ru, c, p, 0, NULL, 0);
}

void lu_auto_ability(rules *ru, chr *c, pending *p)
{
    if (p->abil_due)
        lu_pick_ability(ru, c, p, primary_ability(ru, c->lv[c->nlevels - 1].cls),
                        NULL, 0);
}

int lu_auto_feat(rules *ru, chr *c, pending *p, int bonus, rng *g, const lu_opts *o)
{
    int cls = c->lv[c->nlevels - 1].cls, pool[1100], np, pick, j, k;

    if ((bonus ? p->bonus : p->general) <= 0 ||
        (np = lu_feat_pool(ru, c, bonus, pool, 1100)) <= 0)
        return -1;
    pick = pick_pinned(ru, c, cls, bonus, o);
    if (pick < 0)
        pick = pick_feat(ru, c, cls, bonus, o, g);
    if (pick < 0) {             /* the lists are spent: any legal feat */
        for (j = k = 0; j < np; j++)
            if (!pick_avoided(o, pool[j]))
                pool[k++] = pool[j];
        if (k)
            np = k;
        pick = c->nlevels > 20 ? pick_epic(ru, c, pool, np, g) :
                                 pool[rng_below(g, (unsigned)np)];
    }
    return lu_pick_feat(ru, c, p, pick, bonus, NULL, 0);
}

void lu_auto_feats(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o)
{
    int bonus;

    for (bonus = 1; bonus >= 0; bonus--)    /* bonus slots first, then general */
        while (lu_auto_feat(ru, c, p, bonus, g, o) == 0)
            ;
}

/* A specialist's extra slot needs a school spell at every castable level. */
int lu_school_due(rules *ru, const chr *c, const pending *p, int sl)
{
    int cls = c->lv[c->nlevels - 1].cls, i, j;

    if (c->school <= 0 || !is_wizard_like(ru, cls) || sl < 1 || sl >= NSPLVL)
        return 0;
    if (chr_spell_slots(ru, c, cls, sl) <= 0 || (p->spells[sl] <= 0 && p->wiz_any <= 0))
        return 0;
    for (i = 0; i < c->nlevels; i++)
        for (j = 0; j < c->lv[i].nknown[sl]; j++)
            if (chr_spell_in_school(ru, c->lv[i].known[sl][j], c->school))
                return 0;
    return 1;
}

int lu_auto_school_spell(rules *ru, chr *c, pending *p, int sl, rng *g,
                         const lu_opts *o)
{
    int cls = c->lv[c->nlevels - 1].cls, pool[1100], np, i, j;

    if (!lu_school_due(ru, c, p, sl))
        return -1;
    if ((i = pref_spell(ru, c, cls, sl, sl, c->school, &j, o, g)) >= 0)
        return lu_pick_spell(ru, c, p, sl, i, NULL, 0);
    np = lu_spell_pool(ru, c, cls, sl, pool, 1100);
    for (i = j = 0; i < np; i++)
        if (chr_spell_in_school(ru, pool[i], c->school))
            pool[j++] = pool[i];
    if (j <= 0)
        return -1;
    return lu_pick_spell(ru, c, p, sl, pool[rng_below(g, (unsigned)j)], NULL, 0);
}

int lu_auto_spell(rules *ru, chr *c, pending *p, int sl, rng *g, const lu_opts *o)
{
    int cls = c->lv[c->nlevels - 1].cls, pool[1100], np, lv, pick;

    if (sl < 0 || sl >= NSPLVL || p->spells[sl] <= 0 ||
        (np = lu_spell_pool(ru, c, cls, sl, pool, 1100)) <= 0)
        return -1;
    pick = pref_spell(ru, c, cls, sl, sl, 0, &lv, o, g);
    if (pick < 0)
        pick = pool[rng_below(g, (unsigned)np)];
    return lu_pick_spell(ru, c, p, sl, pick, NULL, 0);
}

int lu_auto_wiz_any(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o)
{
    int cls = c->lv[c->nlevels - 1].cls, pool[1100], np, top, sl2 = 0, pick;

    if (p->wiz_any <= 0 || (top = class_max_sl(ru, c, cls)) < 1)
        return -1;
    pick = pref_spell(ru, c, cls, -1, top, 0, &sl2, o, g);
    if (pick >= 0)
        return lu_pick_spell(ru, c, p, sl2, pick, NULL, 0);
    sl2 = 1 + (int)rng_below(g, (unsigned)top);
    np = lu_spell_pool(ru, c, cls, sl2, pool, 1100);
    if (np > 0)
        return lu_pick_spell(ru, c, p, sl2, pool[rng_below(g, (unsigned)np)], NULL, 0);
    p->wiz_any--;   /* nothing left at that level; give up one pick */
    return 0;
}

void lu_auto_spells(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o)
{
    int sl;

    for (sl = 1; sl < NSPLVL; sl++)
        lu_auto_school_spell(ru, c, p, sl, g, o);
    for (sl = 0; sl < NSPLVL; sl++)
        while (lu_auto_spell(ru, c, p, sl, g, o) == 0)
            ;
    while (lu_auto_wiz_any(ru, c, p, g, o) == 0)
        ;
}

/* One rank into a class skill: the next few skills of the package's list
 * that still take a rank, else any class skill. */
int lu_auto_skill(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o)
{
    int cls = c->lv[c->nlevels - 1].cls, nsk = rules_nskills(ru), i;
    int cand[MAX_SK], nc = 0, total = c->nlevels;
    int cs[8], sw[8], ncs = 0, w, win = pick_window(o ? o->variety : 0);

    /* ranks a prestige class asks for come first, at cross-class cost if need be */
    int saving = 0;

    for (i = 0; o && i < nsk && i < MAX_SK; i++) {
        int have = skill_ranks(c, i), cost = chr_skill_cost(ru, cls, i);

        if (o->skill_goal[i] > have && cost && have + 1 <= skill_max_ranks(total, cost == 1)) {
            if (cost <= p->skill_pts)
                return lu_buy_skill(ru, c, p, i, 1, NULL, 0);
            saving = 1;
        }
    }
    if (saving)                         /* keep the points: a cross-class rank costs two */
        return -1;

    for (w = 0; w < 2 && !ncs; w++) {
        const twoda *sp = lu_pref_tab(ru, c, cls, "SkillPref2DA", w);

        for (i = 0; sp && i < sp->nrows && ncs < win; i++) {
            int sk = twoda_int(sp, i, "SkillIndex", -1), cost;

            if (sk < 0 || sk >= nsk || sk >= MAX_SK)
                continue;
            cost = chr_skill_cost(ru, cls, sk);
            if (cost == 1 && cost <= p->skill_pts &&
                skill_ranks(c, sk) + 1 <= skill_max_ranks(total, 1)) {
                cs[ncs] = sk;
                sw[ncs] = pick_pos_weight(ncs) * pick_pos_weight(ncs) / 16 *
                             skill_fit(ru, c, sk);   /* squared: skills favour the top */
                ncs++;
            }
        }
    }
    if (ncs)
        return lu_buy_skill(ru, c, p, cs[pick_weighted(sw, ncs, g)], 1, NULL, 0);
    for (i = 0; i < nsk && i < MAX_SK; i++) {
        int cost = chr_skill_cost(ru, cls, i);
        if (cost && cost <= p->skill_pts &&
            skill_ranks(c, i) + 1 <= skill_max_ranks(total, cost == 1) &&
            cost == 1)
            cand[nc++] = i;
    }
    if (!nc)
        return -1;
    return lu_buy_skill(ru, c, p, cand[rng_below(g, (unsigned)nc)], 1, NULL, 0);
}

void lu_auto_skills(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o)
{
    while (lu_auto_skill(ru, c, p, g, o) == 0)
        ;
}

void lu_autofill(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o)
{
    lu_auto_domains(ru, c, p, g);
    lu_auto_school(ru, c, p);
    lu_auto_ability(ru, c, p);
    /* Epic feats ask for up to level+3 ranks, so from level 21 the ranks come
     * first, as in the game's level-up order. */
    if (c->nlevels > 20)
        lu_auto_skills(ru, c, p, g, o);
    lu_auto_feats(ru, c, p, g, o);
    lu_auto_spells(ru, c, p, g, o);
    if (c->nlevels <= 20)
        lu_auto_skills(ru, c, p, g, o);
}
