#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/chr.h"
#include "../src/levelup.h"

/* bicverify INSTALL file.bic...
 * Loads each character, re-derives what the 2DA rules say the game would
 * have recorded, and reports every disagreement with the file. */

static int problems;

static void bad(const char *who, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static const char *cur;

static void bad(const char *who, const char *fmt, ...)
{
    va_list ap;

    (void)who;
    printf("  [%s] ", cur);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    problems++;
}

static int has(const int *v, int n, int x)
{
    int i;

    for (i = 0; i < n; i++)
        if (v[i] == x)
            return 1;
    return 0;
}


/* StartingPackage, memorized-list size and school rules for wizards. */
static void check_wizard(rules *ru, const gff_struct *g, const chr *c)
{
    const gff_field *cl = gff_get(g, "ClassList");
    size_t k;
    int sl, li, j, first_cls = c->lv[0].cls, prestige = 0, pkg = (int)gff_get_int(g, "StartingPackage", -1);
    const twoda *pk = rules_tab(ru, "packages");

    /* prestige levels (not built by the engine yet) change caster levels */
    for (li = 0; li < c->nlevels; li++)
        if (rules_class_int(ru, c->lv[li].cls, "PreReqTable", 0) ||
            rules_class_str(ru, c->lv[li].cls, "PreReqTable"))
            prestige = 1;
    /* a premade may use any package of its first class; a specialist
     * wizard's package is fixed by the school */
    if (!prestige) {
        if (c->school > 0 && rules_class_int(ru, first_cls, "PickSchool", 0) > 0) {
            if (pkg != chr_package(ru, c, first_cls))
                bad(cur, "StartingPackage %d, rules say %d", pkg, chr_package(ru, c, first_cls));
        } else if (!pk || pkg < 0 || pkg >= pk->nrows ||
                   twoda_int(pk, pkg, "ClassID", -1) != first_cls) {
            bad(cur, "StartingPackage %d is not a package of class %d", pkg, first_cls);
        }
    }
    for (k = 0; cl && k < cl->v.list.n; k++) {
        const gff_struct *e = cl->v.list.v[k];
        int cls = (int)gff_get_int(e, "Class", -1);

        if (prestige || rules_class_int(ru, cls, "PickSchool", 0) <= 0)
            continue;
        if (gff_get_int(e, "School", 0) != (c->school > 0 ? c->school : 0))
            bad(cur, "School byte %d, model has %d", (int)gff_get_int(e, "School", 0), c->school);
        for (sl = 0; sl < NSPLVL; sl++) {
            char lab[32];
            const gff_field *m;
            int slots = chr_spell_slots(ru, c, cls, sl), school_seen = 0;

            snprintf(lab, sizeof lab, "MemorizedList%d", sl);
            m = gff_get(e, lab);
            if (slots > 0 && m && (int)m->v.list.n != slots)
                bad(cur, "spell level %d: %d memorized, %d slots", sl, (int)m->v.list.n, slots);
            for (li = 0; li < c->nlevels; li++)
                for (j = 0; j < c->lv[li].nknown[sl]; j++) {
                    int sp = c->lv[li].known[sl][j];

                    if (c->school > 0 && sl >= 1 && chr_spell_banned(ru, c, sp))
                        bad(cur, "spell %d (level %d) is from the opposition school", sp, sl);
                    if (chr_spell_in_school(ru, sp, c->school))
                        school_seen = 1;
                }
            if (c->school > 0 && sl >= 1 && slots > 0 && m && m->v.list.n &&
                !school_seen)
                bad(cur, "spell level %d: specialist knows no school spell", sl);
            if (c->school > 0 && sl >= 1 && m && m->v.list.n &&
                !chr_spell_in_school(ru, (int)gff_get_int(m->v.list.v[0], "Spell", -1),
                                     c->school))
                bad(cur, "spell level %d: first memorized spell is not a school spell", sl);
        }
    }
}

/* A bonus slot may stay empty when nothing is left to put in it; true when
 * the bonus pool is empty once level li's recorded feats are in. */
static int bonus_pool_empty(rules *ru, const chr *c, int li)
{
    static chr t;
    static int pool[1100];

    memcpy(&t, c, sizeof t);
    t.desc = NULL;
    t.nlevels = li + 1;
    return lu_feat_pool(ru, &t, 1, pool, 1100) == 0;
}

/* Replays each level against the engine: state before the level, what
 * lu_begin says is owed, and whether the recorded choices were legal. */
static void check_replay(rules *ru, const chr *c)
{
    static chr t;
    int li, i, sl, sk;
    rng g;
    char err[120];

    rng_seed(&g, 1);
    for (li = 0; li < c->nlevels; li++) {
        const lvl_rec *lr = &c->lv[li];
        pending p;
        int chosen[MAX_LFEATS], nch = 0, autof[64], na, nsp, wiz_any_n = 0;
        int abil_due;
        int cls = lr->cls;

        if (twoda_get(rules_classes(ru), cls, "PreReqTable"))
            continue; /* prestige entry rules are not modelled yet */
        memcpy(&t, c, sizeof t);
        t.desc = NULL;
        t.nlevels = li;
        for (i = li; i < c->nlevels; i++)
            if (c->lv[i].abil >= 0)
                t.abil[c->lv[i].abil]--;
        t.unspent_skillpts = 0;
        for (i = 0; i < li; i++) { /* carry-over from earlier levels */
            long pts = 0;
            (void)pts;
        }
        {
            long avail = 0, spent = 0;
            for (i = 0; i < li; i++) {
                avail += chr_skillpts_for_level(ru, &t, i);
                for (sk = 0; sk < MAX_SK; sk++)
                    spent += (long)c->lv[i].ranks[sk] * chr_skill_cost(ru, c->lv[i].cls, sk);
            }
            t.unspent_skillpts = (int)(avail - spent);
        }
        t.good_evil = c->good_evil;
        t.lawful_chaotic = c->lawful_chaotic;
        if (lu_begin(ru, &t, cls, HP_MAX, &g, &p, err, sizeof err) != 0) {
            bad(cur, "level %d: lu_begin refused: %s", li + 1, err);
            continue;
        }

        abil_due = p.abil_due;
        if (lr->abil >= 0) /* the stat point lands before feats are picked */
            lu_pick_ability(ru, &t, &p, lr->abil, err, sizeof err);

        /* chosen = recorded feats not handed out automatically */
        na = chr_auto_feats(ru, &t, li, autof, 64);
        for (i = 0; i < lr->nfeats; i++) {
            int j, is_auto = 0;
            for (j = 0; j < na; j++)
                if (autof[j] == lr->feats[i])
                    is_auto = 1;
            if (!is_auto)
                chosen[nch++] = lr->feats[i];
        }
        if (nch != p.general + p.bonus && lr->epic == 0 && li < 20 &&
            !(nch == p.general + p.bonus - 1 && p.bonus > 0 &&
              bonus_pool_empty(ru, c, li)))
            bad(cur, "level %d: %d chosen feats, lu_begin offers %d+%d", li + 1,
                nch, p.general, p.bonus);
        {
            /* A feat and its prerequisite may both be picked at one level, in
             * either order, so test each against the others already added. */
            int base = t.lv[li].nfeats, k;

            for (i = 0; i < nch; i++) {
                t.lv[li].nfeats = base;
                for (k = 0; k < nch; k++)
                    if (k != i)
                        t.lv[li].feats[t.lv[li].nfeats++] = (uint16_t)chosen[k];
                if (!feat_selectable(ru, &t, cls, chosen[i], 0) &&
                    !feat_selectable(ru, &t, cls, chosen[i], 1) && li < 20)
                    bad(cur, "level %d: chosen feat %d not in selectable pool",
                        li + 1, chosen[i]);
            }
            t.lv[li].nfeats = base;
            for (i = 0; i < nch; i++)
                t.lv[li].feats[t.lv[li].nfeats++] = (uint16_t)chosen[i];
        }

        if ((abil_due != 0) != (lr->abil >= 0))
            bad(cur, "level %d: ability increase mismatch", li + 1);

        /* spells */
        nsp = 0;
        for (sl = 0; sl < NSPLVL; sl++) {
            int cnt = lr->nknown[sl];
            if (sl == 0 && p.spells[0] == 0 && cnt > 0 &&
                rules_class_int(ru, cls, "MemorizesSpells", 0) == 1) {
                continue; /* wizard cantrips are learned automatically */
            }
            if (sl >= 1 && p.wiz_any > 0)
                wiz_any_n += cnt;
            else if (cnt != p.spells[sl])
                bad(cur, "level %d: learned %d spells of level %d, lu_begin owes %d",
                    li + 1, cnt, sl, p.spells[sl]);
            nsp += cnt;
        }
        if (p.wiz_any > 0 && wiz_any_n != p.wiz_any)
            bad(cur, "level %d: wizard learned %d free spells, expected %d", li + 1,
                wiz_any_n, p.wiz_any);
        for (sl = 0; sl < NSPLVL; sl++) {
            const char *col = spell_list_column(ru, cls);
            for (i = 0; col && i < lr->nknown[sl]; i++) {
                const twoda *sp = rules_tab(ru, "spells");
                int lev = twoda_int(sp, lr->known[sl][i], col, -1);
                if (lev != sl)
                    bad(cur, "level %d: spell %d filed under %d but list says %d",
                        li + 1, lr->known[sl][i], sl, lev);
            }
        }

        /* skills never exceed the rank caps */
        for (sk = 0; sk < rules_nskills(ru) && sk < MAX_SK; sk++) {
            int tot = 0, k, cost = chr_skill_cost(ru, cls, sk);
            for (k = 0; k <= li; k++)
                tot += c->lv[k].ranks[sk];
            if (lr->ranks[sk] && cost) {
                int cap = cost == 1 ? li + 1 + 3 : (li + 1 + 3) / 2;
                if (tot > cap)
                    bad(cur, "level %d: skill %d has %d ranks, cap %d", li + 1, sk,
                        tot, cap);
            }
        }
    }
}

static void check(rules *ru, const gff_struct *g, const chr *c)
{
    int li, i, sk, a;
    int nsk = rules_nskills(ru);
    int tot_ranks[MAX_SK];
    long spent = 0, avail = 0;
    int stored;

    /* HP */
    stored = (int)gff_get_int(g, "MaxHitPoints", -1);
    if (stored != chr_maxhp(ru, c))
        bad(cur, "MaxHitPoints file=%d derived=%d", stored, chr_maxhp(ru, c));
    stored = (int)gff_get_int(g, "HitPoints", -1);
    if (stored != chr_hp_rolled(c))
        bad(cur, "HitPoints file=%d sum(LvlStatHitDie)=%d", stored, chr_hp_rolled(c));

    /* Saves */
    {
        static const char *nm[3] = { "FortSaveThrow", "RefSaveThrow", "WillSaveThrow" };
        for (i = 0; i < 3; i++) {
            stored = (int)gff_get_int(g, nm[i], -99);
            if (stored != chr_save(ru, c, i))
                bad(cur, "%s file=%d derived=%d", nm[i], stored, chr_save(ru, c, i));
        }
    }

    /* XP versus level */
    {
        long lo = rules_xp_for_level(ru, c->nlevels);
        if (lo >= 0 && c->xp < lo)
            bad(cur, "XP %ld below %ld required for level %d", c->xp, lo, c->nlevels);
    }

    /* Skills: ranks, costs and points */
    memset(tot_ranks, 0, sizeof tot_ranks);
    for (li = 0; li < c->nlevels; li++) {
        avail += chr_skillpts_for_level(ru, c, li);
        for (sk = 0; sk < nsk && sk < MAX_SK; sk++) {
            int r = c->lv[li].ranks[sk];
            if (r) {
                int cost = chr_skill_cost(ru, c->lv[li].cls, sk);
                if (cost == 0)
                    bad(cur, "level %d buys unavailable skill %d", li + 1, sk);
                spent += (long)r * cost;
                tot_ranks[sk] += r;
            }
        }
    }
    {
        gff_field *sl = gff_get(g, "SkillList");
        for (sk = 0; sl && sk < (int)sl->v.list.n && sk < MAX_SK; sk++) {
            int r = (int)gff_get_int(sl->v.list.v[sk], "Rank", 0);
            if (r != tot_ranks[sk])
                bad(cur, "skill %d rank file=%d sum-of-levels=%d", sk, r, tot_ranks[sk]);
        }
    }
    if (avail - spent != c->unspent_skillpts)
        bad(cur, "unspent skill points file=%d derived=%ld (avail %ld spent %ld)",
            c->unspent_skillpts, avail - spent, avail, spent);

    /* Feats, level by level */
    for (li = 0; li < c->nlevels; li++) {
        int autof[64], na = chr_auto_feats(ru, c, li, autof, 64);
        int gen = chr_general_feat_slots(ru, c, li);
        int bon = chr_bonus_feat_slots(ru, c, li);
        int extra = 0;
        const lvl_rec *lr = &c->lv[li];

        for (i = 0; i < na; i++) {
                int j, found = 0;
                for (j = 0; j < lr->nfeats; j++)
                    if (lr->feats[j] == autof[i])
                        found = 1;
                /* HORSE_MENU and Scribe Scroll postdate the stock premades. */
                if (!found && autof[i] != 1089 && autof[i] != 945)
                    bad(cur, "level %d: expected granted feat %d missing", li + 1, autof[i]);
            }
        for (i = 0; i < lr->nfeats; i++)
            if (!has(autof, na, lr->feats[i]))
                extra++;
        if (extra != gen + bon &&
            !(extra == gen + bon - 1 && bon > 0 && bonus_pool_empty(ru, c, li)))
            bad(cur, "level %d: %d chosen feats, rules allow %d general + %d bonus",
                li + 1, extra, gen, bon);
    }

    /* Ability increases */
    for (li = 0; li < c->nlevels; li++) {
        int due = ((li + 1) % 4 == 0);
        if (due != (c->lv[li].abil >= 0))
            bad(cur, "level %d: ability increase %s", li + 1,
                c->lv[li].abil >= 0 ? "recorded but not due" : "due but missing");
    }
    (void)a;
    check_replay(ru, c);
    check_wizard(ru, g, c);
}

int main(int argc, char **argv)
{
    char err[200];
    res *r;
    rules *ru;
    int i, files = 0, clean = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: bicverify INSTALL file.bic...\n");
        return 2;
    }
    r = res_open(argv[1], NULL, err, sizeof err);
    if (!r || !(ru = rules_open(r, err, sizeof err))) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    for (i = 2; i < argc; i++) {
        size_t n;
        uint8_t *d = read_file(argv[i], &n);
        gff_struct *g;
        chr c;
        int before = problems;
        const char *slash = strrchr(argv[i], '/');

        cur = slash ? slash + 1 : argv[i];
        if (!d || !(g = gff_read(d, n, NULL, err, sizeof err))) {
            printf("%s: unreadable (%s)\n", cur, d ? err : "no file");
            problems++;
            continue;
        }
        if (chr_from_gff(ru, g, &c, err, sizeof err) != 0) {
            printf("%s: %s\n", cur, err);
            problems++;
            continue;
        }
        files++;
        check(ru, g, &c);
        if (problems == before)
            clean++;
        chr_free(&c);
        gff_free(g);
        free(d);
    }
    printf("%d files, %d clean, %d problems\n", files, clean, problems);
    return problems != 0;
}
