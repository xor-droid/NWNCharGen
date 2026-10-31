#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../src/chr.h"
#include "../src/equip.h"
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

/* A deviation of an engine-written file from its own package table: shown but
 * not counted (tests treat any note on a generated file as a failure). */
static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void note(const char *fmt, ...)
{
    va_list ap;

    printf("  [%s] note: ", cur);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static int has(const int *v, int n, int x)
{
    int i;

    for (i = 0; i < n; i++)
        if (v[i] == x)
            return 1;
    return 0;
}


/* ---- starting equipment ---------------------------------------------------- */

static const char *const ITEM_IGNORE[] = { "ObjectId", "Repos_PosX", "Repos_Posy", "StackSize",
                                           "Cost", "TemplateResRef", "PaletteID", "Comment" };

static int ignored(const char *label)
{
    size_t i, n = strlen(label);

    /* The static premades predate the current robe templates (other colours,
     * no ArmorPart_Robe), so those fields cannot be compared. */
    if ((n > 5 && !strcmp(label + n - 5, "Color")) || !strcmp(label, "ArmorPart_Robe"))
        return 1;
    for (i = 0; i < sizeof ITEM_IGNORE / sizeof *ITEM_IGNORE; i++)
        if (!strcmp(label, ITEM_IGNORE[i]))
            return 1;
    return 0;
}

/* First difference between two item structs (ignoring the per-copy fields),
 * as text in out; 0 if they agree. */
static int item_diff(const gff_struct *a, const gff_struct *b, char *out, size_t n, int top)
{
    size_t i, j;
    const char *tag = top ? gff_get_str(a, "Tag") : NULL;
    /* the robes of the static premades are older models: compare identity only */
    int robe = tag && !strncasecmp(tag, "NW_CLOTH", 8);

    for (i = 0; i < a->n; i++) {
        const gff_field *fa = a->f[i], *fb;

        if (top && ignored(fa->label))
            continue;
        if (robe && strcmp(fa->label, "BaseItem") && strcmp(fa->label, "Tag") &&
            strcmp(fa->label, "LocalizedName"))
            continue;
        fb = gff_get(b, fa->label);
        if (!fb) {
            snprintf(out, n, "%s only in the game's item", fa->label);
            return 1;
        }
        if (fa->type != fb->type) {
            snprintf(out, n, "%s has another type", fa->label);
            return 1;
        }
        switch (fa->type) {
        case GFF_EXOSTR: case GFF_RESREF:
            if (strcmp(fa->v.s ? fa->v.s : "", fb->v.s ? fb->v.s : "")) {
                snprintf(out, n, "%s differs", fa->label);
                return 1;
            }
            break;
        case GFF_LOCSTR:
            if (fa->v.loc.strref != fb->v.loc.strref) {
                snprintf(out, n, "%s strref differs", fa->label);
                return 1;
            }
            break;
        case GFF_FLOAT: case GFF_DOUBLE:
            if (fa->v.d != fb->v.d) {
                snprintf(out, n, "%s differs", fa->label);
                return 1;
            }
            break;
        case GFF_LIST:
            if (!strcmp(fa->label, "PropertiesList")) {
                /* every property of the game's item must be in ours; the
                 * current templates carry more class limits on scrolls */
                for (j = 0; j < fa->v.list.n; j++) {
                    const gff_struct *pa = fa->v.list.v[j];
                    size_t q;
                    int hit = 0;

                    for (q = 0; q < fb->v.list.n && !hit; q++) {
                        const gff_struct *pb = fb->v.list.v[q];

                        if (gff_get_int(pa, "PropertyName", -1) == gff_get_int(pb, "PropertyName", -2) &&
                            gff_get_int(pa, "Subtype", -1) == gff_get_int(pb, "Subtype", -2)) {
                            hit = 1;
                            if (item_diff(pa, pb, out, n, 0))
                                return 1;
                        }
                    }
                    if (!hit) {
                        snprintf(out, n, "property %lld/%lld is missing from ours",
                                 (long long)gff_get_int(pa, "PropertyName", -1),
                                 (long long)gff_get_int(pa, "Subtype", -1));
                        return 1;
                    }
                }
                break;
            }
            if (fa->v.list.n != fb->v.list.n) {
                snprintf(out, n, "%s has %zu entries, ours %zu", fa->label, fa->v.list.n,
                         fb->v.list.n);
                return 1;
            }
            for (j = 0; j < fa->v.list.n; j++)
                if (item_diff(fa->v.list.v[j], fb->v.list.v[j], out, n, 0))
                    return 1;
            break;
        case GFF_STRUCT:
            if (item_diff(fa->v.st, fb->v.st, out, n, 0))
                return 1;
            break;
        case GFF_VOID: case GFF_ORIENT: case GFF_VECTOR:
            break;
        default:
            if (fa->v.u != fb->v.u) {
                snprintf(out, n, "%s: %llu, ours %llu", fa->label,
                         (unsigned long long)fa->v.u, (unsigned long long)fb->v.u);
                return 1;
            }
        }
    }
    for (i = 0; i < b->n; i++)
        if (!robe && !(top && ignored(b->f[i]->label)) && !gff_get(a, b->f[i]->label)) {
            snprintf(out, n, "%s only in ours", b->f[i]->label);
            return 1;
        }
    return 0;
}

static const gff_struct *list_at(const gff_struct *g, const char *label, size_t i)
{
    const gff_field *f = gff_get(g, label);

    return f && f->type == GFF_LIST && i < f->v.list.n ? f->v.list.v[i] : NULL;
}

static size_t list_len(const gff_struct *g, const char *label)
{
    const gff_field *f = gff_get(g, label);

    return f && f->type == GFF_LIST ? f->v.list.n : 0;
}

/* Item properties that move the stored saves and hit points: Ability Bonus
 * (scores, before the modifier), Improved Saving Throws (universal subtype
 * only; the elemental ones are conditional) and the per-save version. The
 * property, its subtype and its cost are all read from the 2DAs. */
static void apply_gear(rules *ru, const gff_struct *g, chr *c)
{
    const twoda *def = rules_tab(ru, "itempropdef");
    const twoda *ct = rules_tab(ru, "iprp_costtable");
    size_t it, k, np;

    if (!def || !ct)
        return;
    for (it = 0; it < list_len(g, "Equip_ItemList"); it++) {
        const gff_struct *item = list_at(g, "Equip_ItemList", it);

        np = list_len(item, "PropertiesList");
        for (k = 0; k < np; k++) {
            const gff_struct *p = list_at(item, "PropertiesList", k);
            int row = (int)gff_get_int(p, "PropertyName", -1);
            int sub = (int)gff_get_int(p, "Subtype", -1);
            int cost = (int)gff_get_int(p, "CostValue", -1);
            const char *label = row >= 0 && row < def->nrows ? twoda_get(def, row, "Label") : NULL;
            int tr = row >= 0 && row < def->nrows ? twoda_int(def, row, "CostTableResRef", -1) : -1;
            const char *tn = tr >= 0 && tr < ct->nrows ? twoda_get(ct, tr, "Name") : NULL;
            const twoda *vt = tn ? rules_tab(ru, tn) : NULL;
            int v = vt && cost >= 0 ? twoda_int(vt, cost, "Value", 0) : 0;

            if (!label || !v)
                continue;
            if (!strcmp(label, "Ability") && sub >= 0 && sub < NABIL) {
                static const int ord[NABIL] = { ABIL_STR, ABIL_DEX, ABIL_CON, ABIL_INT, ABIL_WIS, ABIL_CHA };
                c->gear_abil[ord[sub]] += v;
            } else if (!strcmp(label, "ImprovedSavingThrows") && sub == 0) {
                c->gear_save[0] += v;
                c->gear_save[1] += v;
                c->gear_save[2] += v;
            } else if (!strcmp(label, "ImprovedSavingThrowsSpecific")) {
                /* iprp_savingthrow: 1 Fortitude, 2 Will, 3 Reflex */
                int w = sub == 1 ? 0 : sub == 3 ? 1 : sub == 2 ? 2 : -1;
                if (w >= 0)
                    c->gear_save[w] += v;
            }
        }
    }
}

/* A level-1 character carries its package's items, built from the .uti files
 * the way the engine builds them, laid out in the pack, and wears one robe.
 * Layout and ObjectIds are compared only for premades numbered from 193 (the
 * older files use another numbering). */
static void check_items(rules *ru, const gff_struct *g, const chr *orig)
{
    char want[64][32], msg[160];
    chr own = *orig;                    /* a shallow copy: only pkg is changed */
    const chr *c = &own;
    const twoda *pk = rules_tab(ru, "packages");
    int nwant, i;

    /* the package the file itself names (StartingPackage) */
    int named = (int)gff_get_int(g, "StartingPackage", -1);

    if (pk && named >= 0 && named < pk->nrows &&
        twoda_int(pk, named, "ClassID", -1) == orig->lv[0].cls && orig->lv[0].cls < MAX_CLS)
        own.pkg[orig->lv[0].cls] = named;
    nwant = equip_package_resrefs(ru, c, want, 64);
    size_t k, np = list_len(g, "ItemList");
    gff_struct *ref = gff_new(0);
    rng rg;
    int same_numbering = 0, found[64] = { 0 };
    unsigned char grid[10 * 64] = { 0 };
    const gff_struct *worn = list_at(g, "Equip_ItemList", 0);

    if (list_len(g, "Equip_ItemList") != 1 || !worn || worn->type != 2 ||
        gff_get_int(worn, "BaseItem", -1) != 16) {
        bad(cur, "level 1 should wear exactly one robe in the chest slot");
    } else {
        gff_struct *mine = equip_item_from_uti(ru, gff_get_str(worn, "Tag"), 0);

        same_numbering = gff_get_int(worn, "ObjectId", 0) == 193;
        if (!mine)
            bad(cur, "worn robe %s has no template", gff_get_str(worn, "Tag"));
        else {
            if (item_diff(worn, mine, msg, sizeof msg, 1))
                bad(cur, "worn robe %s: %s", gff_get_str(worn, "Tag"), msg);
            gff_free(mine);
        }
    }
    rng_seed(&rg, 1);
    gff_list_init(ref, "Equip_ItemList");
    gff_list_init(ref, "ItemList");
    equip_fill(ru, c, ref, &rg);

    {   /* a premade whose pack differs from its package table (the file, not
         * the generator, deviates): the numbering and layout cannot match */
        int all = 1;

        for (k = 0; k < np; k++) {
            const char *t = gff_get_str(list_at(g, "ItemList", k), "Tag");

            for (i = 0; i < nwant && t; i++)
                if (!strcasecmp(t, want[i]))
                    break;
            all = all && t && i < nwant;
        }
        all = all && (int)np >= 0;
        for (i = 0; i < nwant; i++) {
            int hit = 0;

            for (k = 0; k < np; k++) {
                const char *t = gff_get_str(list_at(g, "ItemList", k), "Tag");

                hit |= t && !strcasecmp(t, want[i]);
            }
            all = all && hit;
        }
        if (!all)
            same_numbering = 0;
    }
    for (k = 0; k < np; k++) {
        const gff_struct *it = list_at(g, "ItemList", k), *mine = NULL;
        const char *tag = gff_get_str(it, "Tag");
        size_t m;
        int x = (int)gff_get_int(it, "Repos_PosX", 0), y = (int)gff_get_int(it, "Repos_Posy", 0);
        int base = (int)gff_get_int(it, "BaseItem", 0), a, b;
        const twoda *bi = rules_tab(ru, "baseitems");
        int w = twoda_int(bi, base, "InvSlotWidth", 1), h = twoda_int(bi, base, "InvSlotHeight", 1);

        for (i = 0; i < nwant; i++)
            if (tag && !strcasecmp(tag, want[i]))
                found[i] = 1;
        {   /* the k-th item of a tag matches the k-th of ours (two short swords) */
            size_t before = 0, seen = 0;

            for (m = 0; m < k; m++)
                before += !strcmp(gff_get_str(list_at(g, "ItemList", m), "Tag"), tag ? tag : "");
            for (m = 0; m < list_len(ref, "ItemList"); m++)
                if (!strcmp(gff_get_str(list_at(ref, "ItemList", m), "Tag"), tag ? tag : "") &&
                    seen++ == before)
                    mine = list_at(ref, "ItemList", m);
            if (!mine)      /* older files keep stackable items as separate singles */
                for (m = 0; m < list_len(ref, "ItemList") && !mine; m++)
                    if (!strcmp(gff_get_str(list_at(ref, "ItemList", m), "Tag"), tag ? tag : ""))
                        mine = list_at(ref, "ItemList", m);
            if (mine && gff_get_int(it, "StackSize", 1) != gff_get_int(mine, "StackSize", 1))
                same_numbering = 0;     /* another way of stacking: layout differs too */
        }
        if (!mine) {
            note("item %s is not in the character's package", tag ? tag : "?");
            continue;
        }
        if (item_diff(it, mine, msg, sizeof msg, 1))
            bad(cur, "item %s: %s", tag, msg);
        if (same_numbering) {
            if (gff_get_int(it, "ObjectId", 0) != gff_get_int(mine, "ObjectId", 0))
                bad(cur, "item %s ObjectId %lld, ours %lld", tag,
                    (long long)gff_get_int(it, "ObjectId", 0),
                    (long long)gff_get_int(mine, "ObjectId", 0));
            if (x != gff_get_int(mine, "Repos_PosX", 0) || y != gff_get_int(mine, "Repos_Posy", 0))
                bad(cur, "item %s at %d,%d, ours %d,%d", tag, x, y,
                    (int)gff_get_int(mine, "Repos_PosX", 0), (int)gff_get_int(mine, "Repos_Posy", 0));
        }
        for (b = 0; b < h; b++)
            for (a = 0; a < w; a++) {
                if (x + a >= 10 || y + b >= 64 || grid[(y + b) * 10 + x + a])
                    bad(cur, "item %s overlaps another or leaves the pack", tag);
                else
                    grid[(y + b) * 10 + x + a] = 1;
            }
    }
    for (i = 0; i < nwant; i++)
        if (!found[i])
            note("package item %s is missing", want[i]);
    gff_free(ref);
}

/* StartingPackage, memorized-list size and school rules for wizards. */
static void check_wizard(rules *ru, const gff_struct *g, const chr *c)
{
    const gff_field *cl = gff_get(g, "ClassList");
    size_t k;
    int sl, li, j, first_cls = c->lv[0].cls, prestige = 0, pkg = (int)gff_get_int(g, "StartingPackage", -1);
    const twoda *pk = rules_tab(ru, "packages");

    /* premades with prestige levels were built differently (package, school
     * choices), so only their slot counts are compared */
    for (li = 0; li < c->nlevels; li++)
        if (rules_class_str(ru, c->lv[li].cls, "PreReqTable"))
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

        if (rules_class_int(ru, cls, "PickSchool", 0) <= 0)
            continue;
        if (!prestige && gff_get_int(e, "School", 0) != (c->school > 0 ? c->school : 0))
            bad(cur, "School byte %d, model has %d", (int)gff_get_int(e, "School", 0), c->school);
        for (sl = 0; sl < NSPLVL; sl++) {
            char lab[32];
            const gff_field *m;
            int slots = chr_spell_slots(ru, c, cls, sl), school_seen = 0;

            snprintf(lab, sizeof lab, "MemorizedList%d", sl);
            m = gff_get(e, lab);
            if (slots > 0 && m && (int)m->v.list.n != slots)
                bad(cur, "spell level %d: %d memorized, %d slots", sl, (int)m->v.list.n, slots);
            for (li = 0; !prestige && li < c->nlevels; li++)
                for (j = 0; j < c->lv[li].nknown[sl]; j++) {
                    int sp = c->lv[li].known[sl][j];

                    if (c->school > 0 && sl >= 1 && chr_spell_banned(ru, c, sp))
                        bad(cur, "spell %d (level %d) is from the opposition school", sp, sl);
                    if (chr_spell_in_school(ru, sp, c->school))
                        school_seen = 1;
                }
            if (!prestige && c->school > 0 && sl >= 1 && slots > 0 && m && m->v.list.n &&
                !school_seen)
                bad(cur, "spell level %d: specialist knows no school spell", sl);
            if (!prestige && c->school > 0 && sl >= 1 && m && m->v.list.n &&
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

        if (twoda_get(rules_classes(ru), cls, "PreReqTable") &&
            !lu_prestige_supported(ru, cls))
            continue; /* this prestige class is not modelled yet */
        memcpy(&t, c, sizeof t);
        t.desc = NULL;
        t.nlevels = li;
        if (twoda_get(rules_classes(ru), cls, "PreReqTable") && chr_class_level(&t, cls) == 0) {
            char why[160];

            if (!lu_prestige_ok(ru, &t, cls, why, sizeof why))
                note("level %d: premade enters %s without meeting its requirements (%s)",
                     li + 1, rules_class_str(ru, cls, "Label"), why);
        }
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

        /* the game buys skills before feats, so this level's ranks count */
        memcpy(t.lv[li].ranks, lr->ranks, sizeof t.lv[li].ranks);

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
        if (nch != p.general + p.bonus &&
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
                    !feat_selectable(ru, &t, cls, chosen[i], 1))
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
    if (c->nlevels == 1)
        check_items(ru, g, c);
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
    lu_set_entry_checks(0);     /* the premades ignore prestige entry rules */
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
        apply_gear(ru, g, &c);
        check(ru, g, &c);
        if (problems == before)
            clean++;
        chr_free(&c);
        gff_free(g);
        free(d);
    }
    printf("%d files, %d clean, %d problems\n", files, clean, problems);
    rules_close(ru);
    res_close(r);
    return problems != 0;
}
