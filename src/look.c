#define _POSIX_C_SOURCE 200809L
#include "look.h"

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
    if (name[0] != 'p' || strncmp(name + 4, "0_head", 6) != 0)
        return;
    g = name[1] == 'f' ? 1 : name[1] == 'm' ? 0 : -1;
    r = name[2] - 'a';
    num = atoi(name + 10);
    if (g < 0 || r < 0 || r >= 26 || num < 1 || num >= 1000)
        return;
    if (h->n[g][r] < 160)
        h->v[g][r][h->n[g][r]++] = num;
}

void look_randomize(rules *ru, chr *c, rng *g)
{
    static headtab *ht;
    static rules *ht_owner;
    const twoda *ap = rules_tab(ru, "appearance");
    const twoda *po = rules_tab(ru, "portraits");
    const twoda *ss = rules_tab(ru, "soundset");
    int r, n, pick[400], np = 0;
    const char *letter;
    int body[18] = { 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1 };

    if (!ht || ht_owner != ru) {
        free(ht);
        ht = calloc(1, sizeof *ht);
        ht_owner = ru;
        res_each(rules_res(ru), 2002, head_visit, ht);
    }

    c->appearance = rules_race_int(ru, c->race, "Appearance", c->race);
    c->age = rules_race_int(ru, c->race, "AGE", 18);
    c->phenotype = 0;
    c->faction = 5;
    memcpy(c->body, body, sizeof body);

    /* head: from the models that exist for this race letter and gender */
    letter = ap ? twoda_get(ap, c->appearance, "RACE") : NULL;
    c->head = 1;
    if (letter && isalpha((unsigned char)letter[0])) {
        int rl = tolower((unsigned char)letter[0]) - 'a';
        if (ht->n[c->gender][rl] > 0)
            c->head = ht->v[c->gender][rl][rng_below(g, (unsigned)ht->n[c->gender][rl])];
    }

    /* portrait: rows of portraits.2da for this race and sex, no plot items */
    c->portrait[0] = '\0';
    for (r = 0; po && r < po->nrows && np < 400; r++) {
        const char *base = twoda_get(po, r, "BaseResRef");
        if (base && twoda_int(po, r, "Sex", -1) == c->gender &&
            twoda_int(po, r, "Race", -1) == c->race &&
            twoda_int(po, r, "Plot", 0) == 0 &&
            !twoda_get(po, r, "InanimateType"))
            pick[np++] = r;
    }
    if (np > 0) {
        snprintf(c->portrait, sizeof c->portrait, "po_%s",
                 twoda_get(po, pick[rng_below(g, (unsigned)np)], "BaseResRef"));
    }

    /* sound set: the player voice sets of the right gender */
    np = 0;
    for (r = 0; ss && r < ss->nrows && np < 400; r++) {
        const char *lab = twoda_get(ss, r, "LABEL");
        size_t ll = lab ? strlen(lab) : 0;

        /* the stock player voice sets are the ones labelled *_PCV */
        if (twoda_int(ss, r, "TYPE", -1) == 0 && twoda_get(ss, r, "RESREF") &&
            twoda_int(ss, r, "GENDER", -1) == c->gender && ll >= 3 &&
            strcasecmp(lab + ll - 3, "pcv") == 0)
            pick[np++] = r;
    }
    c->soundset = np > 0 ? pick[rng_below(g, (unsigned)np)] : 216;

    c->skin = c->race == 5 ? 26 : 0;   /* half-orc skin is a separate palette row */
    c->hair = (int)rng_below(g, 24);
    c->tattoo1 = c->tattoo2 = 0;
    (void)n;
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
