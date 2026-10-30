#define _POSIX_C_SOURCE 200809L
#include "chr.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"

void chr_init(chr *c)
{
    memset(c, 0, sizeof *c);
    int i;

    c->domain1 = c->domain2 = c->school = c->familiar = c->companion = -1;
    for (i = 0; i < MAX_CLS; i++)
        c->pkg[i] = -1;
}

void chr_free(chr *c)
{
    free(c->desc);
    c->desc = NULL;
}

int chr_class_level_upto(const chr *c, int cls, int nlevels)
{
    int i, n = 0;

    for (i = 0; i < nlevels && i < c->nlevels; i++)
        if (c->lv[i].cls == cls)
            n++;
    return n;
}

int chr_class_level(const chr *c, int cls)
{
    return chr_class_level_upto(c, cls, c->nlevels);
}

int chr_has_feat(const chr *c, int feat)
{
    int i, j;

    for (i = 0; i < c->nlevels; i++)
        for (j = 0; j < c->lv[i].nfeats; j++)
            if (c->lv[i].feats[j] == feat)
                return 1;
    return 0;
}

int chr_abil_at(rules *ru, const chr *c, int a, int nlevels)
{
    int v = c->abil[a], i;

    for (i = nlevels; i < c->nlevels; i++)
        if (c->lv[i].abil == a)
            v--;
    return v + rules_race_adjust(ru, c->race, a);
}

int chr_eff_abil(rules *ru, const chr *c, int a)
{
    return chr_abil_at(ru, c, a, c->nlevels);
}

/* Feat index by its feat.2da label, or -1. Labels are stable across
 * game versions where numeric ids are not. */
int rules_feat_by_label(rules *ru, const char *label)
{
    const twoda *t = rules_feats(ru);
    int r;

    for (r = 0; r < t->nrows; r++) {
        const char *l = twoda_get(t, r, "LABEL");
        if (l && strcasecmp(l, label) == 0)
            return r;
    }
    return -1;
}

static int has_labeled(rules *ru, const chr *c, const char *label)
{
    int f = rules_feat_by_label(ru, label);

    return f >= 0 && chr_has_feat(c, f);
}

int chr_bab(rules *ru, const chr *c)
{
    const twoda *cl = rules_classes(ru);
    int cls, total = 0;

    for (cls = 0; cls < cl->nrows; cls++) {
        int lv = chr_class_level(c, cls);
        if (lv)
            total += rules_cls_bab(ru, cls, lv);
    }
    return total;
}

int chr_save(rules *ru, const chr *c, int which)
{
    static const char *col[3] = { "FortSave", "RefSave", "WillSave" };
    static const int abil[3] = { ABIL_CON, ABIL_DEX, ABIL_WIS };
    const twoda *cl = rules_classes(ru);
    int cls, total = 0;

    for (cls = 0; cls < cl->nrows; cls++) {
        int lv = chr_class_level(c, cls);
        if (lv)
            total += rules_cls_save(ru, cls, lv, col[which]);
    }
    total += abil_mod(chr_eff_abil(ru, c, abil[which]));
    /* Feat effects the game bakes into the stored value. */
    if (has_labeled(ru, c, which == 0 ? "GreatFort" :
                           which == 1 ? "LightngRef" : "IronWill"))
        total += 2;
    if (has_labeled(ru, c, "lucky"))
        total += 1;
    if (has_labeled(ru, c, "DivineGrace")) {
        int m = abil_mod(chr_eff_abil(ru, c, ABIL_CHA));
        total += m > 0 ? m : 0;
    }
    return total;
}

int chr_hp_rolled(const chr *c)
{
    int i, n = 0;

    for (i = 0; i < c->nlevels; i++)
        n += c->lv[i].hp;
    return n;
}

int chr_maxhp(rules *ru, const chr *c)
{
    int hp = chr_hp_rolled(c) + c->nlevels * abil_mod(chr_eff_abil(ru, c, ABIL_CON));

    if (has_labeled(ru, c, "Toughness"))
        hp += c->nlevels;
    return hp;
}

int chr_skillpts_for_level(rules *ru, const chr *c, int li)
{
    int cls = c->lv[li].cls;
    int base = rules_class_int(ru, cls, "SkillPointBase", 0);
    int extra = rules_race_int(ru, c->race, "ExtraSkillPointsPerLevel", 0);
    int m = abil_mod(chr_abil_at(ru, c, ABIL_INT, li + 1));
    int pts = base + extra + m;

    if (pts < 1)
        pts = 1;
    if (li == 0)
        pts *= rules_race_int(ru, c->race, "FirstLevelSkillPointsMultiplier", 4);
    return pts;
}

int chr_skill_cost(rules *ru, int cls, int sk)
{
    const twoda *t = rules_class_tab(ru, cls, "SkillsTable");
    int r;

    if (!t)
        return 0;
    for (r = 0; r < t->nrows; r++) {
        if (twoda_int(t, r, "SkillIndex", -1) == sk)
            return twoda_int(t, r, "ClassSkill", 0) ? 1 : 2;
    }
    return 0;
}

static int owned_before(const chr *c, int li, const int *out, int n, int f)
{
    int i, j;

    for (i = 0; i < li; i++)
        for (j = 0; j < c->lv[i].nfeats; j++)
            if (c->lv[i].feats[j] == f)
                return 1;
    for (i = 0; i < n; i++)
        if (out[i] == f)
            return 1;
    return 0;
}

int chr_auto_feats(rules *ru, const chr *c, int li, int *out, int cap)
{
    int n = 0, r, f;
    int cls = c->lv[li].cls;
    int clvl = chr_class_level_upto(c, cls, li + 1);
    const twoda *t;

    if (li == 0 && (t = rules_race_tab(ru, c->race, "FeatsTable")) != NULL) {
        for (r = 0; r < t->nrows; r++) {
            f = twoda_int(t, r, "FeatIndex", -1);
            if (f >= 0 && n < cap && !owned_before(c, li, out, n, f))
                out[n++] = f;
        }
    }
    /* Reaching level 21 grants the Epic Character feat; no table lists it. */
    if (li == 20 && n < cap && !owned_before(c, li, out, n, FEAT_EPIC_CHARACTER))
        out[n++] = FEAT_EPIC_CHARACTER;
    t = rules_class_tab(ru, cls, "FeatsTable");
    if (t) {
        for (r = 0; r < t->nrows; r++) {
            f = twoda_int(t, r, "FeatIndex", -1);
            if (f >= 0 && twoda_int(t, r, "List", -1) == 3 &&
                twoda_int(t, r, "GrantedOnLevel", 0) == clvl && n < cap &&
                !owned_before(c, li, out, n, f))
                out[n++] = f;
        }
    }
    /* Clerics receive the feat attached to each chosen domain at level 1. */
    if (clvl == 1 && rules_class_int(ru, cls, "PickDomains", 0) > 0) {
        const twoda *d = rules_tab(ru, "domains");
        int dom[2] = { c->domain1, c->domain2 }, i;

        for (i = 0; d && i < 2; i++) {
            f = dom[i] >= 0 ? twoda_int(d, dom[i], "GrantedFeat", -1) : -1;
            if (f >= 0 && n < cap && !owned_before(c, li, out, n, f))
                out[n++] = f;
        }
    }
    return n;
}

int chr_general_feat_slots(rules *ru, const chr *c, int li)
{
    int lvl = li + 1, n = 0;
    int every = rules_race_int(ru, c->race, "NormalFeatEveryNthLevel", 3);
    int per = rules_race_int(ru, c->race, "NumberNormalFeatsEveryNthLevel", 1);

    if (lvl == 1)
        n += 1 + rules_race_int(ru, c->race, "ExtraFeatsAtFirstLevel", 0);
    if (every > 0 && lvl % every == 0)
        n += per;
    return n;
}

int chr_bonus_feat_slots(rules *ru, const chr *c, int li)
{
    int cls = c->lv[li].cls;

    return rules_cls_bonus_feats(ru, cls, chr_class_level_upto(c, cls, li + 1));
}

/* ---- loading from GFF -------------------------------------------------- */

static void copy_locstr(const gff_struct *g, const char *label, char *out,
                        size_t cap)
{
    gff_field *f = gff_get(g, label);

    out[0] = '\0';
    if (f && f->type == GFF_LOCSTR && f->v.loc.n > 0)
        snprintf(out, cap, "%s", f->v.loc.e[0].text);
}

static void load_known(const gff_struct *s, lvl_rec *lr)
{
    int sl;
    char label[32];

    for (sl = 0; sl < NSPLVL; sl++) {
        gff_field *f;
        size_t i;

        snprintf(label, sizeof label, "KnownList%d", sl);
        f = gff_get(s, label);
        if (!f || f->type != GFF_LIST)
            continue;
        for (i = 0; i < f->v.list.n && i < MAX_KNOWN; i++)
            lr->known[sl][lr->nknown[sl]++] =
                (uint16_t)gff_get_int(f->v.list.v[i], "Spell", 0);
    }
}

int chr_from_gff(rules *ru, const gff_struct *g, chr *c, char *err, size_t errlen)
{
    static const char *an[NABIL] = { "Str", "Dex", "Con", "Int", "Wis", "Cha" };
    gff_field *ls = gff_get(g, "LvlStatList"), *cl = gff_get(g, "ClassList");
    gff_field *f;
    size_t i, j;
    int a;

    (void)ru;
    chr_init(c);
    if (!ls || ls->type != GFF_LIST || !cl || cl->type != GFF_LIST) {
        snprintf(err, errlen, "no LvlStatList/ClassList");
        return -1;
    }
    if (ls->v.list.n > MAX_LEVELS) {
        snprintf(err, errlen, "too many levels");
        return -1;
    }
    copy_locstr(g, "FirstName", c->first, sizeof c->first);
    copy_locstr(g, "LastName", c->last, sizeof c->last);
    c->race = (int)gff_get_int(g, "Race", 0);
    c->gender = (int)gff_get_int(g, "Gender", 0);
    c->good_evil = (int)gff_get_int(g, "GoodEvil", 50);
    c->lawful_chaotic = (int)gff_get_int(g, "LawfulChaotic", 50);
    for (a = 0; a < NABIL; a++)
        c->abil[a] = (int)gff_get_int(g, an[a], 10);
    c->xp = (long)gff_get_int(g, "Experience", 0);
    c->gold = (int)gff_get_int(g, "Gold", 0);
    c->unspent_skillpts = (int)gff_get_int(g, "SkillPoints", 0);

    for (i = 0; i < ls->v.list.n; i++) {
        const gff_struct *s = ls->v.list.v[i];
        lvl_rec *lr = &c->lv[c->nlevels++];

        lr->cls = (int)gff_get_int(s, "LvlStatClass", 0);
        lr->hp = (int)gff_get_int(s, "LvlStatHitDie", 0);
        lr->abil = (int)gff_get_int(s, "LvlStatAbility", -1);
        lr->skillpts = (int)gff_get_int(s, "SkillPoints", 0);
        lr->epic = (int)gff_get_int(s, "EpicLevel", 0);
        f = gff_get(s, "SkillList");
        if (f && f->type == GFF_LIST)
            for (j = 0; j < f->v.list.n && j < MAX_SK; j++)
                lr->ranks[j] = (int)gff_get_int(f->v.list.v[j], "Rank", 0);
        f = gff_get(s, "FeatList");
        if (f && f->type == GFF_LIST)
            for (j = 0; j < f->v.list.n && j < MAX_LFEATS; j++)
                lr->feats[lr->nfeats++] =
                    (uint16_t)gff_get_int(f->v.list.v[j], "Feat", 0);
        load_known(s, lr);
    }
    for (i = 0; i < cl->v.list.n; i++) {   /* the cleric or wizard may not be first */
        const gff_struct *e = cl->v.list.v[i];

        if (c->domain1 < 0) {
            c->domain1 = (int)gff_get_int(e, "Domain1", -1);
            c->domain2 = (int)gff_get_int(e, "Domain2", -1);
        }
        if (c->school < 0)
            c->school = (int)gff_get_int(e, "School", -1);
    }
    return 0;
}

int chr_spell_slots(rules *ru, const chr *c, int cls, int sl)
{
    const char *ab = rules_class_str(ru, cls, "SpellcastingAbil");
    int lv = chr_class_level(c, cls), base, a;

    if (!lv)
        return -1;
    base = rules_spells_per_day(ru, cls, lv, sl);
    if (base < 0)
        return -1;
    a = ab && !strcasecmp(ab, "INT") ? ABIL_INT : ab && !strcasecmp(ab, "WIS") ?
        ABIL_WIS : ABIL_CHA;
    base += spell_bonus(abil_mod(chr_eff_abil(ru, c, a)), sl);
    /* a specialist holds one extra spell per spell level, from sl 1 up */
    if (c->school > 0 && sl >= 1 && rules_class_int(ru, cls, "PickSchool", 0) > 0)
        base++;
    return base;
}

int chr_spell_in_school(rules *ru, int spell, int school)
{
    const twoda *sp = rules_tab(ru, "spells"), *ss = rules_tab(ru, "spellschools");
    const char *a, *b;

    if (!sp || !ss || school < 1 || school >= ss->nrows)
        return 0;
    a = twoda_get(sp, spell, "School");
    b = twoda_get(ss, school, "Letter");
    return a && b && toupper((unsigned char)a[0]) == toupper((unsigned char)b[0]);
}

int chr_spell_banned(rules *ru, const chr *c, int spell)
{
    const twoda *ss = rules_tab(ru, "spellschools");

    if (c->school < 1 || !ss || c->school >= ss->nrows)
        return 0;
    return chr_spell_in_school(ru, spell, twoda_int(ss, c->school, "Opposition", 0));
}

int chr_package(rules *ru, const chr *c, int cls)
{
    const twoda *pk = rules_tab(ru, "packages");
    int r;

    if (cls >= 0 && cls < MAX_CLS && c->pkg[cls] >= 0)
        return c->pkg[cls];
    if (c->school > 0 && pk && rules_class_int(ru, cls, "PickSchool", 0) > 0)
        for (r = 0; r < pk->nrows; r++)
            if (twoda_int(pk, r, "ClassID", -1) == cls &&
                twoda_int(pk, r, "School", -1) == c->school &&
                twoda_int(pk, r, "PlayerClass", 0) == 1)
                return r;
    return rules_class_int(ru, cls, "Package", 0);
}
