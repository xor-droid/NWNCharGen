#define _POSIX_C_SOURCE 200809L
#include "cli.h"
#include "look.h"
#include "plan.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int cli_alnum_eq(const char *a, const char *b)
{
    for (;;) {
        while (*a && !isalnum((unsigned char)*a))
            a++;
        while (*b && !isalnum((unsigned char)*b))
            b++;
        if (!*a || !*b)
            return !*a && !*b;
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return 0;
    }
}


int cli_find_row(const twoda *t, const char *s, int playable_col)
{
    char *e;
    long v = strtol(s, &e, 10);
    int r;

    if (*s && !*e && v >= 0 && v < t->nrows)
        return (int)v;
    for (r = 0; r < t->nrows; r++) {
        const char *l = twoda_get(t, r, "Label");
        if (playable_col >= 0 && twoda_int(t, r, "PlayerClass", 1) != 1)
            continue;
        if (l && cli_alnum_eq(l, s))
            return r;
    }
    /* the game's name for the class whose label is Champion_Torm */
    if (cli_alnum_eq(s, "divinechampion") || cli_alnum_eq(s, "championoftorm"))
        return cli_find_row(t, "Champion_Torm", playable_col);
    return -1;
}


int cli_parse_align(const char *s, int *ge, int *lc)
{
    char a[4] = { 0 };
    size_t i, n = 0;

    for (i = 0; s[i] && n < 3; i++)
        if (isalpha((unsigned char)s[i]))
            a[n++] = (char)toupper((unsigned char)s[i]);
    if (n == 1 && a[0] == 'N') {
        *ge = *lc = 50;
        return 0;
    }
    if (n != 2)
        return -1;
    *lc = a[0] == 'L' ? 85 : a[0] == 'C' ? 15 : a[0] == 'N' ? 50 : -1;
    *ge = a[1] == 'G' ? 85 : a[1] == 'E' ? 15 : a[1] == 'N' ? 50 : -1;
    return (*lc < 0 || *ge < 0) ? -1 : 0;
}


const char *cli_align_name(int ge, int lc)
{
    const char *n[3][3] = {
        { "Lawful Good", "Lawful Neutral", "Lawful Evil" },
        { "Neutral Good", "True Neutral", "Neutral Evil" },
        { "Chaotic Good", "Chaotic Neutral", "Chaotic Evil" } };
    int l = lc >= 70 ? 0 : lc <= 30 ? 2 : 1;
    int g = ge >= 70 ? 0 : ge <= 30 ? 2 : 1;

    return n[l][g];
}


int cli_pb_cost(int s)
{
    return s <= 14 ? s - 8 : s == 15 ? 8 : s == 16 ? 10 : s == 17 ? 13 : 16;
}


int cli_pb_total(const int *a)
{
    int i, t = 0;

    for (i = 0; i < NABIL; i++)
        t += cli_pb_cost(a[i]);
    return t;
}


/* Class-recommended array (classes.2da Str..Cha columns). */
void cli_recommended(rules *ru, int cls, int *out)
{
    const char *col[NABIL] = { "Str", "Dex", "Con", "Int", "Wis", "Cha" };
    int a;

    for (a = 0; a < NABIL; a++)
        out[a] = rules_class_int(ru, cls, col[a], 10);
}


int cli_allocate(rules *ru, int cls, int race, const int *fixed, int randomize,
                    rng *g, int *out)
{
    int budget = rules_race_int(ru, race, "AbilitiesPointBuyNumber", 30);
    int rec[NABIL], a, b, i, any_fixed = 0, moves;

    cli_recommended(ru, cls, rec);
    for (a = 0; a < NABIL; a++) {
        if (fixed[a] != -1 && (fixed[a] < 8 || fixed[a] > 18))
            return -2;      /* point buy only covers 8..18 */
        out[a] = fixed[a] >= 0 ? fixed[a] : 8;
        any_fixed |= fixed[a] >= 0;
    }
    if (cli_pb_total(out) > budget)
        return -1;
    for (;;) {
        int best = -1;
        for (a = 0; a < NABIL; a++) {
            if (fixed[a] >= 0 || out[a] >= 18)
                continue;
            if (cli_pb_total(out) - cli_pb_cost(out[a]) + cli_pb_cost(out[a] + 1) > budget)
                continue;
            /* prefer the ability furthest below its recommended value */
            if (best < 0 || rec[a] - out[a] > rec[best] - out[best])
                best = a;
        }
        if (best < 0)
            break;
        out[best]++;
    }
    if (randomize && !any_fixed) {
        for (moves = 0; moves < 40; moves++) {
            a = (int)rng_below(g, NABIL);
            b = (int)rng_below(g, NABIL);
            if (a == b || out[a] <= 8 || out[b] >= 18)
                continue;
            out[a]--;
            out[b]++;
            if (cli_pb_total(out) > budget) {
                out[a]++;
                out[b]--;
            }
        }
        for (i = 0; i < NABIL; i++) {
            while (out[i] < 18 && cli_pb_total(out) - cli_pb_cost(out[i]) +
                   cli_pb_cost(out[i] + 1) <= budget)
                out[i]++;
        }
    }
    return 0;
}


void cli_file_name(const chr *c, char *out, size_t n)
{
    char tmp[128];
    size_t i, k = 0;

    snprintf(tmp, sizeof tmp, "%s%s", c->first, c->last);
    for (i = 0; tmp[i] && k < 16 && k + 1 < n; i++)
        if (isalnum((unsigned char)tmp[i]))
            out[k++] = (char)tolower((unsigned char)tmp[i]);
    if (!k)
        out[k++] = 'x';
    out[k] = '\0';
}


int cli_exists(const char *p)
{
    struct stat st;

    return stat(p, &st) == 0;
}


const char *cli_tname(const tlk *t, const twoda *tab, int row, const char *col,
                         char *buf, size_t n)
{
    const char *s = twoda_get(tab, row, col);
    const char *lab = twoda_get(tab, row, "Label");

    buf[0] = '\0';
    if (t && s && tlk_copy(t, (uint32_t)atoi(s), buf, n) > 0 && buf[0])
        return buf;
    snprintf(buf, n, "%s", lab ? lab : "?");
    return buf;
}


void cli_sheet(rules *ru, const tlk *t, const chr *c)
{
    const char *an[NABIL] = { "STR", "DEX", "CON", "INT", "WIS", "CHA" };
    char b[160];
    int a, i, j, sk;
    const twoda *sks = rules_skills(ru), *ft = rules_feats(ru);

    printf("%s %s  --  %s %s", c->first, c->last,
           cli_tname(t, rules_races(ru), c->race, "Name", b, sizeof b),
           c->gender ? "female" : "male");
    for (i = 0; i < rules_nclasses(ru); i++)
        if (chr_class_level(c, i))
            printf("  %s %d", cli_tname(t, rules_classes(ru), i, "Name", b, sizeof b),
                   chr_class_level(c, i));
    printf("  (%s)\n", cli_align_name(c->good_evil, c->lawful_chaotic));
    for (a = 0; a < NABIL; a++)
        printf("  %s %2d (%d)", an[a], chr_eff_abil(ru, c, a),
               abil_mod(chr_eff_abil(ru, c, a)));
    printf("\n  HP %d  BAB %d  Fort %d Ref %d Will %d  XP %ld  Gold %d\n",
           chr_maxhp(ru, c), chr_bab(ru, c), chr_save(ru, c, 0), chr_save(ru, c, 1),
           chr_save(ru, c, 2), c->xp, c->gold);
    printf("  Skills:");
    for (sk = 0; sk < rules_nskills(ru) && sk < MAX_SK; sk++) {
        int tot = 0;
        for (i = 0; i < c->nlevels; i++)
            tot += c->lv[i].ranks[sk];
        if (tot)
            printf(" %s %d,", cli_tname(t, sks, sk, "Name", b, sizeof b), tot);
    }
    printf("\n  Feats:");
    for (i = 0; i < c->nlevels; i++)
        for (j = 0; j < c->lv[i].nfeats; j++)
            printf(" %s;", cli_tname(t, ft, c->lv[i].feats[j], "FEAT", b, sizeof b));
    printf("\n");
    for (i = 0; i < c->nlevels; i++) {
        int sl;
        const twoda *sp = rules_tab(ru, "spells");
        for (sl = 0; sl < NSPLVL; sl++)
            if (c->lv[i].nknown[sl]) {
                printf("  Level-%d spells learned at level %d:", sl, i + 1);
                for (j = 0; j < c->lv[i].nknown[sl]; j++)
                    printf(" %s;", cli_tname(t, sp, c->lv[i].known[sl][j], "Name", b, sizeof b));
                printf("\n");
            }
    }
    if (c->school > 0)
        printf("  School: %s (opposed: %s)\n",
               twoda_get(rules_tab(ru, "spellschools"), c->school, "Label"),
               twoda_get(rules_tab(ru, "spellschools"),
                         twoda_int(rules_tab(ru, "spellschools"), c->school, "Opposition", 0),
                         "Label"));
    if (c->domain1 >= 0) {
        const twoda *d = rules_tab(ru, "domains");
        char b2[160];   /* separate buffers: both are used in one printf */
        printf("  Domains: %s, %s\n", cli_tname(t, d, c->domain1, "Name", b, sizeof b),
               cli_tname(t, d, c->domain2, "Name", b2, sizeof b2));
    }
    look_show(ru, t, stdout, c);
}


int cli_alnum_contains(const char *hay, const char *needle)
{
    char h[160], n[160];
    size_t i, j;

    for (i = j = 0; hay[i] && j < sizeof h - 1; i++)
        if (isalnum((unsigned char)hay[i]))
            h[j++] = (char)tolower((unsigned char)hay[i]);
    h[j] = '\0';
    for (i = j = 0; needle[i] && j < sizeof n - 1; i++)
        if (isalnum((unsigned char)needle[i]))
            n[j++] = (char)tolower((unsigned char)needle[i]);
    n[j] = '\0';
    return *n && strstr(h, n) != NULL;
}


/* A feat by number, label or in-game name; -1 if unknown. */
int cli_find_feat(rules *ru, const tlk *t, const char *s)
{
    const twoda *f = rules_feats(ru);
    char nm[160];
    int r;

    if (isdigit((unsigned char)s[0]) && atoi(s) < f->nrows && twoda_get(f, atoi(s), "FEAT"))
        return atoi(s);
    for (r = 0; r < f->nrows; r++) {
        const char *lab = twoda_get(f, r, "LABEL");

        if (!twoda_get(f, r, "FEAT"))
            continue;
        if ((lab && cli_alnum_eq(lab, s)) || cli_alnum_eq(cli_tname(t, f, r, "FEAT", nm, sizeof nm), s))
            return r;
    }
    return -1;
}


void cli_list_feats(rules *ru, const tlk *t, const char *text)
{
    const twoda *f = rules_feats(ru);
    char nm[160];
    int r, n = 0;

    for (r = 0; r < f->nrows; r++) {
        const char *lab = twoda_get(f, r, "LABEL");

        if (!twoda_get(f, r, "FEAT"))
            continue;
        cli_tname(t, f, r, "FEAT", nm, sizeof nm);
        if (cli_alnum_contains(nm, text) || (lab && cli_alnum_contains(lab, text))) {
            printf("  %4d  %s  [%s]\n", r, nm, lab ? lab : "");
            n++;
        }
    }
    if (!n)
        printf("  no feat matches '%s'\n", text);
}

/* "fighter:5,wizard:3" -> class rows and level counts; 0 on success */
int cli_parse_classes(rules *ru, const char *s, int *cl, int *cn, int *n)
{
    char buf[256], *tok, *save = NULL;
    int k;

    snprintf(buf, sizeof buf, "%s", s);
    *n = 0;
    for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *sep = strpbrk(tok, ":=");
        int row, lv;

        if (!sep || *n >= MAX_CLASSES)
            return -1;
        *sep = '\0';
        row = cli_find_row(rules_classes(ru), tok, 1);
        lv = atoi(sep + 1);
        if (row < 0 || lv < 1 || (twoda_get(rules_classes(ru), row, "PreReqTable") &&
                                  !lu_prestige_supported(ru, row)))
            return -1;
        for (k = 0; k < *n; k++)
            if (cl[k] == row)
                return -1;
        cl[*n] = row;
        cn[(*n)++] = lv;
    }
    return *n ? 0 : -1;
}
