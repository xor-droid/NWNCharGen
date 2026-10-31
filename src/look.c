#define _POSIX_C_SOURCE 200809L
#include "look.h"

#include "cli.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    int n[2][26];
    int v[2][26][160];
} headtab;

static void head_visit(const char *name, int type, const char *src, void *arg)
{
    headtab *h = arg;
    int g, r, num;

    (void)type;
    (void)src;
    /* p<g><race>0_head<NNN>, e.g. pmh0_head012 */
    if (name[0] != 'p' || strncmp(name + 3, "0_head", 6) != 0)
        return;
    g = name[1] == 'f' ? 1 : name[1] == 'm' ? 0 : -1;
    r = name[2] - 'a';
    num = atoi(name + 9);
    if (g < 0 || r < 0 || r >= 26 || num < 1 || num >= 1000)
        return;
    if (h->n[g][r] < 160)
        h->v[g][r][h->n[g][r]++] = num;
}

static headtab *heads_table(rules *ru)
{
    static headtab *ht;
    static rules *ht_owner;

    if (!ht || ht_owner != ru) {
        free(ht);
        ht = calloc(1, sizeof *ht);
        ht_owner = ru;
        res_each(rules_res(ru), 2002, head_visit, ht);
    }
    return ht;
}

/* The head numbers with a model in the install for this race and gender. */
static int head_list(rules *ru, int race, int gender, const int **out)
{
    const twoda *ap = rules_tab(ru, "appearance");
    const char *letter;
    headtab *ht = heads_table(ru);
    int app = rules_race_int(ru, race, "Appearance", race);

    letter = ap ? twoda_get(ap, app, "RACE") : NULL;
    if (letter && isalpha((unsigned char)letter[0])) {
        int rl = tolower((unsigned char)letter[0]) - 'a';

        if (ht->n[gender][rl] > 0) {
            *out = ht->v[gender][rl];
            return ht->n[gender][rl];
        }
    }
    *out = NULL;
    return 0;
}

/* Rows of portraits.2da for this race and sex, no plot items. */
static int portrait_rows(rules *ru, int race, int gender, int *pick, int cap)
{
    const twoda *po = rules_tab(ru, "portraits");
    int r, np = 0;

    for (r = 0; po && r < po->nrows && np < cap; r++) {
        const char *base = twoda_get(po, r, "BaseResRef");
        if (base && twoda_int(po, r, "Sex", -1) == gender &&
            twoda_int(po, r, "Race", -1) == race &&
            twoda_int(po, r, "Plot", 0) == 0 &&
            !twoda_get(po, r, "InanimateType"))
            pick[np++] = r;
    }
    return np;
}

/* Rows of soundset.2da: the stock player voice sets (labelled *_PCV) of this gender. */
static int voice_rows(rules *ru, int gender, int *pick, int cap)
{
    const twoda *ss = rules_tab(ru, "soundset");
    int r, np = 0;

    for (r = 0; ss && r < ss->nrows && np < cap; r++) {
        const char *lab = twoda_get(ss, r, "LABEL");
        size_t ll = lab ? strlen(lab) : 0;

        if (twoda_int(ss, r, "TYPE", -1) == 0 && twoda_get(ss, r, "RESREF") &&
            twoda_int(ss, r, "GENDER", -1) == gender && ll >= 3 &&
            strcasecmp(lab + ll - 3, "pcv") == 0)
            pick[np++] = r;
    }
    return np;
}

void look_randomize(rules *ru, chr *c, rng *g)
{
    const twoda *po = rules_tab(ru, "portraits");
    int pick[400], np;
    const int *hv;
    int body[18] = { 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1 };

    c->appearance = rules_race_int(ru, c->race, "Appearance", c->race);
    c->age = rules_race_int(ru, c->race, "AGE", 18);
    c->phenotype = 0;
    c->faction = 5;
    memcpy(c->body, body, sizeof body);

    c->head = 1;
    c->portrait[0] = '\0';
    np = portrait_rows(ru, c->race, c->gender, pick, 400);
    if (np > 0) {
        snprintf(c->portrait, sizeof c->portrait, "po_%s",
                 twoda_get(po, pick[rng_below(g, (unsigned)np)], "BaseResRef"));
    }

    np = voice_rows(ru, c->gender, pick, 400);
    c->soundset = np > 0 ? pick[rng_below(g, (unsigned)np)] : 216;

    c->skin = c->race == 5 ? 26 : 0;   /* half-orc skin is a separate palette row */
    c->hair = (int)rng_below(g, 24);
    c->tattoo1 = c->tattoo2 = 0;

    /* head: one of the models that exist for this race letter and gender,
     * taken from the draws above so the random stream is not disturbed */
    np = head_list(ru, c->race, c->gender, &hv);
    if (np > 0) {
        unsigned k = (unsigned)c->hair * 31u + (unsigned)c->soundset * 17u;
        const char *q;

        for (q = c->portrait; *q; q++)
            k = k * 33u + (unsigned char)*q;
        c->head = hv[k % (unsigned)np];
    }
}

/* ---- choosing the look -------------------------------------------------- */

#define PALETTE 176     /* hair, skin and tattoo colours are rows of 176-entry palettes */

static int palette_value(const char *val, int *out)
{
    char *end;
    long v;

    if (!*val)
        return -1;
    v = strtol(val, &end, 10);
    if (*end || v < 0 || v >= PALETTE)
        return -1;
    *out = (int)v;
    return 0;
}

static const char *voice_name(const tlk *t, const twoda *ss, int row, char *b, size_t bn)
{
    const char *n = cli_tname(t, ss, row, "STRREF", b, bn);

    return n && *n ? n : twoda_get(ss, row, "LABEL");
}

int look_set(rules *ru, const tlk *t, chr *c, const char *key, const char *val,
             char *err, size_t n)
{
    const int *hv;
    int pick[400], np, i, v;

    if (!strcmp(key, "head")) {
        np = head_list(ru, c->race, c->gender, &hv);
        v = isdigit((unsigned char)val[0]) ? atoi(val) : -1;
        for (i = 0; i < np && hv[i] != v; i++)
            ;
        if (i == np) {
            snprintf(err, n, "no head %s for this race and gender (--list-heads shows them)", val);
            return -1;
        }
        c->head = v;
        return 0;
    }
    if (!strcmp(key, "portrait")) {
        const twoda *po = rules_tab(ru, "portraits");
        const char *w = strncasecmp(val, "po_", 3) == 0 ? val + 3 : val;

        np = portrait_rows(ru, c->race, c->gender, pick, 400);
        for (i = 0; i < np; i++)
            if (cli_alnum_eq(w, twoda_get(po, pick[i], "BaseResRef"))) {
                snprintf(c->portrait, sizeof c->portrait, "po_%s", twoda_get(po, pick[i], "BaseResRef"));
                return 0;
            }
        snprintf(err, n, "no portrait '%s' for this race and gender (--list-portraits shows them)", val);
        return -1;
    }
    if (!strcmp(key, "voice")) {
        const twoda *ss = rules_tab(ru, "soundset");
        char b[160];
        int hit = -1, hits = 0;

        np = voice_rows(ru, c->gender, pick, 400);
        if (isdigit((unsigned char)val[0])) {
            for (i = 0; i < np; i++)
                if (pick[i] == atoi(val))
                    hit = pick[i], hits = 1;
        } else {
            for (i = 0; i < np; i++)         /* a whole name first, then part of one */
                if (cli_alnum_eq(val, voice_name(t, ss, pick[i], b, sizeof b)) ||
                    cli_alnum_eq(val, twoda_get(ss, pick[i], "LABEL")))
                    hit = pick[i], hits = 1;
            for (i = 0; !hits && i < np; i++)
                if (cli_alnum_contains(voice_name(t, ss, pick[i], b, sizeof b), val)) {
                    if (hit < 0)
                        hit = pick[i];
                    hits++;
                }
        }
        if (hits != 1) {
            snprintf(err, n, hits ? "'%s' fits several voices (--list-voices shows them)"
                                  : "no voice '%s' for this gender (--list-voices shows them)", val);
            return -1;
        }
        c->soundset = hit;
        return 0;
    }
    if (!strcmp(key, "hair") || !strcmp(key, "skin") || !strcmp(key, "tattoo1") ||
        !strcmp(key, "tattoo2")) {
        if (palette_value(val, &v) != 0) {
            snprintf(err, n, "colour numbers run 0-%d", PALETTE - 1);
            return -1;
        }
        *(!strcmp(key, "hair") ? &c->hair : !strcmp(key, "skin") ? &c->skin :
          !strcmp(key, "tattoo1") ? &c->tattoo1 : &c->tattoo2) = v;
        return 0;
    }
    snprintf(err, n, "unknown appearance setting '%s'", key);
    return -1;
}

int look_is_key(const char *key)
{
    static const char *const k[] = { "head", "portrait", "voice", "hair", "skin", "tattoo1", "tattoo2" };
    size_t i;

    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!strcmp(key, k[i]))
            return 1;
    return 0;
}

void look_list(rules *ru, const tlk *t, FILE *fp, const char *what, int race, int gender)
{
    const int *hv;
    int pick[400], np, i;

    if (!strcmp(what, "heads")) {
        np = head_list(ru, race, gender, &hv);
        for (i = 0; i < np; i++)
            fprintf(fp, "%s%d", i ? " " : "", hv[i]);
        fprintf(fp, "%s", np ? "\n" : "no heads found\n");
    } else if (!strcmp(what, "portraits")) {
        const twoda *po = rules_tab(ru, "portraits");

        np = portrait_rows(ru, race, gender, pick, 400);
        for (i = 0; i < np; i++)
            fprintf(fp, "  %s\n", twoda_get(po, pick[i], "BaseResRef"));
    } else {
        const twoda *ss = rules_tab(ru, "soundset");
        char b[160];

        np = voice_rows(ru, gender, pick, 400);
        for (i = 0; i < np; i++)
            fprintf(fp, "  %3d %s\n", pick[i], voice_name(t, ss, pick[i], b, sizeof b));
    }
}

void look_show(rules *ru, const tlk *t, FILE *fp, const chr *c)
{
    const twoda *ss = rules_tab(ru, "soundset");
    char b[160];

    fprintf(fp, "  head %d, portrait %s, voice %d (%s), hair %d, skin %d, tattoo1 %d, tattoo2 %d\n",
            c->head, c->portrait[0] ? c->portrait + 3 : "none", c->soundset,
            c->soundset >= 0 && ss && c->soundset < ss->nrows ?
            voice_name(t, ss, c->soundset, b, sizeof b) : "?", c->hair, c->skin, c->tattoo1, c->tattoo2);
}

/* ---- names ------------------------------------------------------------- */

static const char *first_m[7][8] = {
    { "Brun", "Dorgan", "Thrain", "Kazrin", "Orvik", "Balgar", "Hadrek", "Mordin" },
    { "Aelar", "Erevan", "Lorien", "Soveliss", "Thamior", "Varis", "Ivellios", "Galinndan" },
    { "Fibblin", "Orryn", "Wrenn", "Boddyn", "Nim", "Zook", "Gimble", "Dimble" },
    { "Pip", "Merric", "Lindal", "Corrin", "Roscoe", "Osborn", "Perrin", "Eldon" },
    { "Dorian", "Kellan", "Tarin", "Aldric", "Faren", "Joren", "Caelan", "Brenn" },
    { "Grash", "Hurgo", "Krug", "Thokk", "Orgrim", "Dench", "Ront", "Shump" },
    { "Aldous", "Bram", "Cedric", "Edmund", "Gareth", "Hugh", "Tobias", "Wystan" }
};
static const char *first_f[7][8] = {
    { "Dagna", "Helda", "Gunnloda", "Torbera", "Vistra", "Amber", "Kathra", "Riswynn" },
    { "Adrie", "Birel", "Enna", "Faral", "Keyleth", "Lia", "Naivara", "Shava" },
    { "Bimpnottin", "Carlin", "Ellyjobell", "Lilli", "Nissa", "Roywyn", "Zanna", "Breena" },
    { "Andry", "Bree", "Callie", "Kithri", "Lidda", "Merla", "Seraphina", "Verna" },
    { "Alys", "Brielle", "Corwyn", "Elara", "Isolde", "Mirelle", "Rhea", "Sera" },
    { "Baggi", "Emen", "Engong", "Myev", "Neega", "Ovak", "Sutha", "Volen" },
    { "Alys", "Brenna", "Cecily", "Elspeth", "Isolde", "Maud", "Rowena", "Tamsin" }
};
static const char *last_n[7][8] = {
    { "Ironfist", "Stonehelm", "Deepdelver", "Battlehammer", "Rockbreaker", "Forgeborn", "Coalbeard", "Anvilcrest" },
    { "Amakiir", "Galanodel", "Holimion", "Liadon", "Meliamne", "Nailo", "Siannodel", "Xiloscient" },
    { "Beren", "Daergel", "Folkor", "Garrick", "Nackle", "Murnig", "Ningel", "Raulnor" },
    { "Brushgather", "Goodbarrel", "Greenbottle", "Highhill", "Leagallow", "Tealeaf", "Thorngage", "Underbough" },
    { "Brightwood", "Dawnstrider", "Fairwind", "Moonbrook", "Silverleaf", "Starhaven", "Westmere", "Thornfield" },
    { "Bonecrusher", "Skullsplitter", "Ironhide", "Bloodtusk", "Ragefist", "Grimjaw", "Stonefang", "Warcry" },
    { "Ashford", "Blackwood", "Carter", "Fletcher", "Hartley", "Marsh", "Rivers", "Thatcher" }
};

void name_random(int race, int gender, rng *g, char *first, size_t nf,
                 char *last, size_t nl)
{
    int r = race >= 0 && race < 7 ? race : 6;

    snprintf(first, nf, "%s", (gender ? first_f : first_m)[r][rng_below(g, 8)]);
    snprintf(last, nl, "%s", last_n[r][rng_below(g, 8)]);
}

void familiar_name_random(rng *g, char *out, size_t n)
{
    static const char *pool[] = { "Nidao", "Pip", "Whisper", "Ember", "Shade",
                                  "Tuck", "Sprocket", "Mote", "Bramble", "Flint" };

    snprintf(out, n, "%s", pool[rng_below(g, sizeof pool / sizeof *pool)]);
}
