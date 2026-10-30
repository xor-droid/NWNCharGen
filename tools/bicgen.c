#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../src/chr.h"
#include "../src/cli.h"
#include "../src/guide.h"
#include "../src/levelup.h"
#include "../src/ini.h"
#include "../src/look.h"
#include "../src/pick.h"
#include "../src/plan.h"
#include "../src/tlk.h"

#define DEFAULT_INSTALL \
    "/mnt/c/Program Files (x86)/Steam/steamapps/common/Neverwinter Nights"

typedef struct {
    const char *install, *user, *outdir, *name;
    const char *race_s, *class_s, *align_s, *gender_s, *school_s, *classes_s, *hp_s, *variety_s;
    const char *spec_s, *find_feat_s;
    const char *pin_s[MAX_PINS], *avoid_s[MAX_PINS];   /* feat names from --pin/--avoid/spec */
    int pin_from[MAX_PINS], npin, navoid;
    lu_opts lo;                                        /* resolved variety, pins, avoids */
    int level, have_level, max_classes, count, show, fixed[NABIL], list, install_vault;
    int guided, per_level;      /* --guided; the guide picks the class at each level-up */
    hp_mode hm;
    int ncl, cl[MAX_CLASSES], cn[MAX_CLASSES];   /* --classes, parsed */
    uint64_t seed;
    int have_seed;
} opts;

static void usage(void)
{
    puts("usage: bicgen [options]\n"
         "  --nwn-dir DIR     NWN1:EE install (default: $NWN_HOME or the Steam path)\n"
         "  --user-dir DIR    Documents/Neverwinter Nights (for --install)\n"
         "  --race R          race name or id (default: random)\n"
         "  --class C         class name or id (default: random)\n"
         "  --gender m|f      (default: random)\n"
         "  --align LG|NG|CG|LN|N|CN|LE|NE|CE   (default: random legal for class)\n"
         "  --school S        wizard specialty: a school name, 0-8, generalist or\n"
         "                    random (default: generalist)\n"
         "  --name \"First Last\"   (default: random)\n"
         "  --str/--dex/--con/--int/--wis/--cha N   base scores before racial\n"
         "                    adjustment, 8-18, within the point-buy budget\n"
         "  --level N         character level, 1-40 (default 1)\n"
         "  --classes LIST    level split, e.g. fighter:5,wizard:3 (the first class\n"
         "                    takes level 1; sets the level)\n"
         "  --max-classes N   random builds use 1 to N classes (1-3, default 1)\n"
         "  --hp max|avg|roll hit points after level 1 (default roll)\n"
         "  --variety 0-3     choices: 0 follows the package lists exactly, higher\n"
         "                    values weigh more entries and vary the weapon (default 2)\n"
         "  --pin FEATS       feats to take as early as legal (names, labels or ids,\n"
         "                    comma separated; NAME@N waits until character level N)\n"
         "  --avoid FEATS     feats never to pick unprompted\n"
         "  --find-feat TEXT  list feats whose name or label contains TEXT\n"
         "  --spec FILE       read options from an INI file (see examples/); options\n"
         "                    on the command line override it\n"
         "  --seed N          RNG seed (printed so a build can be repeated)\n"
         "  --count N         generate N characters\n"
         "  --out-dir DIR     where to write .bic files (default: ./out)\n"
         "  --install         also copy each file into <user-dir>/localvault\n"
         "  --guided          build interactively: choose anything, or press Enter\n"
         "                    for the automatic pick (same seed + all Enters gives\n"
         "                    the same file as without --guided); answers are saved\n"
         "                    next to the .bic so the session can be replayed\n"
         "  --show            print a character sheet\n"
         "  --list            list race and class names\n");
}

/* ---- files ------------------------------------------------------------- */

/* "random", a number, or a spellschools.2da label; "generalist" is school 0 */
static int pick_school(rules *ru, const char *s, rng *g)
{
    const twoda *ss = rules_tab(ru, "spellschools");
    int k;

    if (!ss)
        return -1;
    (void)g;
    if (!strcmp(s, "random"))
        return PLAN_SCHOOL_RANDOM;
    if (cli_alnum_eq(s, "generalist"))
        return 0;
    for (k = 0; k < ss->nrows; k++)
        if (twoda_get(ss, k, "Label") && cli_alnum_eq(s, twoda_get(ss, k, "Label")))
            return k;
    return isdigit((unsigned char)s[0]) && atoi(s) < ss->nrows ? atoi(s) : -1;
}

static void default_user_dir(char *out, size_t n)
{
    static const char *pat[] = {
        "/mnt/c/Users/*/OneDrive/Documents/Neverwinter Nights",
        "/mnt/c/Users/*/Documents/Neverwinter Nights" };
    glob_t gl;
    size_t i;

    out[0] = '\0';
    for (i = 0; i < 2 && !out[0]; i++) {
        if (glob(pat[i], 0, NULL, &gl) == 0 && gl.gl_pathc > 0)
            snprintf(out, n, "%s", gl.gl_pathv[0]);
        globfree(&gl);
    }
}

/* ---- main -------------------------------------------------------------- */

static int want_arg(int argc, char **argv, int *i, const char *name,
                    const char **val)
{
    size_t n = strlen(name);
    const char *a = argv[*i];

    if (strncmp(a, name, n) != 0)
        return 0;
    if (a[n] == '=') {
        *val = a + n + 1;
        return 1;
    }
    if (a[n] == '\0') {
        if (*i + 1 >= argc) {
            fprintf(stderr, "bicgen: %s needs a value\n", name);
            exit(2);
        }
        *val = argv[++*i];
        return 1;
    }
    return 0;
}

/* ---- feat names, pins and the spec file --------------------------------- */

/* Option strings taken from a spec file or split from a list live until exit. */
static char *owned[512];
static int nowned;

static void free_owned(void)
{
    while (nowned > 0)
        free(owned[--nowned]);
}

static char *own(const char *s)
{
    char *d = strdup(s);

    if (!d || nowned >= (int)(sizeof owned / sizeof *owned))
        abort();
    if (!nowned)
        atexit(free_owned);
    return owned[nowned++] = d;
}

/* Splits "a, b@3, c" into names (and the optional @level). */
static int add_names(const char **names, int *from, int *n, const char *list)
{
    char *copy = strdup(list), *tok, *save = NULL;

    if (!copy)
        return -1;
    for (tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *at;

        while (isspace((unsigned char)*tok))
            tok++;
        for (at = tok + strlen(tok); at > tok && isspace((unsigned char)at[-1]); )
            *--at = '\0';
        if (!*tok)
            continue;
        if (*n >= MAX_PINS)
            return -1;
        at = strrchr(tok, '@');
        if (from && at && isdigit((unsigned char)at[1])) {
            from[*n] = atoi(at + 1);
            *at = '\0';
            while (at > tok && isspace((unsigned char)at[-1]))
                *--at = '\0';
        } else if (from) {
            from[*n] = 1;
        }
        names[(*n)++] = own(tok);
    }
    free(copy);
    return 0;
}

/* Fills o from an INI file. Unknown sections or keys are errors, so typos
 * do not silently turn into a different build. */
static int load_spec(const char *path, opts *o, char *err, size_t errlen)
{
    size_t n;
    uint8_t *d = read_file(path, &n);
    ini f;
    int i;

    if (!d) {
        snprintf(err, errlen, "cannot read %s", path);
        return -1;
    }
    if (ini_parse((const char *)d, n, &f, err, errlen) != 0) {
        free(d);
        return -1;
    }
    free(d);
    for (i = 0; i < f.n; i++) {
        const ini_entry *e = &f.e[i];
        const char *v = own(e->val);
        const char *sec = e->section, *k = e->key;
        int a, hit = 0;

        if (!strcmp(sec, "character")) {
            hit = 1;
            if (!strcmp(k, "name")) o->name = v;
            else if (!strcmp(k, "race")) o->race_s = v;
            else if (!strcmp(k, "class")) o->class_s = v;
            else if (!strcmp(k, "classes")) o->classes_s = v;
            else if (!strcmp(k, "gender")) o->gender_s = v;
            else if (!strcmp(k, "align") || !strcmp(k, "alignment")) o->align_s = v;
            else if (!strcmp(k, "school")) o->school_s = v;
            else if (!strcmp(k, "hp")) o->hp_s = v;
            else if (!strcmp(k, "variety")) o->variety_s = v;
            else if (!strcmp(k, "level")) { o->level = atoi(v); o->have_level = 1; }
            else if (!strcmp(k, "max-classes") || !strcmp(k, "max_classes")) o->max_classes = atoi(v);
            else if (!strcmp(k, "seed")) { o->seed = strtoull(v, NULL, 10); o->have_seed = 1; }
            else hit = 0;
        } else if (!strcmp(sec, "abilities")) {
            static const char *an[NABIL] = { "str", "dex", "con", "int", "wis", "cha" };

            for (a = 0; a < NABIL; a++)
                if (!strcmp(k, an[a])) {
                    o->fixed[a] = atoi(v);
                    hit = 1;
                }
        } else if (!strcmp(sec, "feats")) {
            if (!strcmp(k, "pin") && add_names(o->pin_s, o->pin_from, &o->npin, v) == 0)
                hit = 1;
            else if (!strcmp(k, "avoid") && add_names(o->avoid_s, NULL, &o->navoid, v) == 0)
                hit = 1;
        }
        if (!hit) {
            snprintf(err, errlen, "%s line %d: unknown or bad setting [%s] %s", path, e->line,
                     sec, k);
            ini_free(&f);
            return -1;
        }
    }
    ini_free(&f);
    return 0;
}

static int build_one(rules *ru, const opts *o, uint64_t seed, const char *outdir,
                     const tlk *t, const char *user, guide *gd, const char *command)
{
    rng g;
    chr c;
    char err[160], first[64] = "", last[64] = "", path[1024], base[32];
    int race, cls, ge, lc, tries, abil[NABIL], fx[NABIL];
    gff_struct *tree;
    buf out = { 0 };
    int n;
    char warn[1200] = "";

    rng_seed(&g, seed);
    chr_init(&c);

    race = o->race_s ? cli_find_row(rules_races(ru), o->race_s, -1) : -1;
    if (o->race_s && (race < 0 || !twoda_int(rules_races(ru), race, "PlayerRace", 0))) {
        fprintf(stderr, "bicgen: unknown or non-player race '%s'\n", o->race_s);
        return -1;
    }
    if (race < 0) {
        int pool[32], np = 0, r, k;
        for (r = 0; r < rules_races(ru)->nrows && np < 32; r++)
            if (twoda_int(rules_races(ru), r, "PlayerRace", 0))
                pool[np++] = r;
        /* a prestige class in the plan may be open to some races only */
        for (k = 0; k < o->ncl; k++) {
            lu_req rq[LU_MAX_REQS];
            int nr = lu_prestige_reqs(ru, o->cl[k], rq, LU_MAX_REQS), m, q, any = 0, w = 0;

            for (m = 0; m < nr; m++)
                any |= rq[m].type == 'R';
            for (q = 0; any && q < np; q++) {
                int ok = 0;

                for (m = 0; m < nr; m++)
                    ok |= rq[m].type == 'R' && rq[m].a == pool[q];
                if (ok)
                    pool[w++] = pool[q];
            }
            if (any)
                np = w;
        }
        if (np == 0) {
            fprintf(stderr, "bicgen: no race can take all of those classes\n");
            return -1;
        }
        race = pool[rng_below(&g, (unsigned)np)];
    }
    cls = o->ncl ? o->cl[0] : o->class_s ? cli_find_row(rules_classes(ru), o->class_s, 1) : -1;
    if (o->ncl && o->class_s && cli_find_row(rules_classes(ru), o->class_s, 1) != cls) {
        fprintf(stderr, "bicgen: --class '%s' is not the first class of --classes\n",
                o->class_s);
        return -1;
    }
    if (!o->ncl && o->class_s && cls < 0) {
        fprintf(stderr, "bicgen: unknown class '%s'\n", o->class_s);
        return -1;
    }
    if (cls < 0) {
        int pool[64], np = 0, r;
        int pge = -1, plc = 0;

        if (o->align_s && cli_parse_align(o->align_s, &pge, &plc) != 0)
            pge = -1;
        for (r = 0; r < rules_nclasses(ru) && np < 64; r++)
            if (twoda_int(rules_classes(ru), r, "PlayerClass", 0) == 1 &&
                !twoda_get(rules_classes(ru), r, "PreReqTable") &&
                (pge < 0 || chr_align_ok(ru, r, pge, plc)))
                pool[np++] = r;
        cls = pool[rng_below(&g, (unsigned)np)];
    }

    c.race = race;
    c.gender = o->gender_s ? (tolower((unsigned char)o->gender_s[0]) == 'f') :
                             (int)rng_below(&g, 2);
    if (o->align_s) {
        if (cli_parse_align(o->align_s, &ge, &lc) != 0) {
            fprintf(stderr, "bicgen: bad alignment '%s'\n", o->align_s);
            return -1;
        }
    } else {
        for (tries = 0; tries < 200; tries++) {
            ge = 15 + 35 * (int)rng_below(&g, 3);
            lc = 15 + 35 * (int)rng_below(&g, 3);
            int k, ok = chr_align_ok(ru, cls, ge, lc);

            for (k = 1; k < o->ncl; k++)
                ok = ok && chr_align_ok(ru, o->cl[k], ge, lc);
            if (ok)
                break;
        }
    }
    c.good_evil = ge;
    c.lawful_chaotic = lc;

    {
        /* scores the prestige classes' feat requirements need (Dodge: Dex 13) */
        static const char *adj[NABIL] = { "StrAdjust", "DexAdjust", "ConAdjust",
                                          "IntAdjust", "WisAdjust", "ChaAdjust" };
        int need[NABIL] = { 0 }, a, k, m;

        memcpy(fx, o->fixed, sizeof fx);
        for (k = 0; k < o->ncl; k++) {
            lu_req rq[LU_MAX_REQS];
            int nr = lu_prestige_reqs(ru, o->cl[k], rq, LU_MAX_REQS);

            for (m = 0; m < nr; m++)
                if (rq[m].type == 'F')
                    lu_feat_abil_need(ru, rq[m].a, need);
        }
        for (a = 0; a < NABIL; a++) {
            int base = need[a] - rules_race_int(ru, race, adj[a], 0);

            if (fx[a] < 0 && base > 8 && base <= 18)
                fx[a] = base;
        }
    }
    switch (cli_allocate(ru, cls, race, fx, 1, &g, abil)) {
    case 0:
        break;
    case -2:
        fprintf(stderr, "bicgen: base ability scores must be between 8 and 18\n");
        return -1;
    default:
        fprintf(stderr, "bicgen: ability scores exceed the point-buy budget\n");
        return -1;
    }
    memcpy(c.abil, abil, sizeof abil);

    look_randomize(ru, &c, &g);
    if (o->name) {
        const char *sp = strchr(o->name, ' ');
        snprintf(first, sizeof first, "%.*s", sp ? (int)(sp - o->name) : 63, o->name);
        if (sp)
            snprintf(last, sizeof last, "%s", sp + 1);
    } else {
        name_random(race, c.gender, &g, first, sizeof first, last, sizeof last);
    }
    snprintf(c.first, sizeof c.first, "%s", first);
    snprintf(c.last, sizeof c.last, "%s", last);
    c.xp = rules_xp_for_level(ru, o->level) > 0 ? rules_xp_for_level(ru, o->level) : 0;

    {
        lvl_plan pl;
        int k, any_school = 0;

        memset(&pl, 0, sizeof pl);
        if (o->ncl) {
            int m;

            for (pl.n = 0, k = 0; k < o->ncl; k++)
                for (m = 0; m < o->cn[k]; m++)
                    pl.seq[pl.n++] = o->cl[k];
        } else {
            plan_random(ru, &c, &pl, cls, o->level, o->max_classes, &g);
        }
        pl.hm = o->hm;
        pl.opts = o->lo;
        pl.school = o->school_s ? pick_school(ru, o->school_s, &g) : PLAN_SCHOOL_NONE;
        for (k = 0; k < pl.n; k++)
            if (rules_class_int(ru, pl.seq[k], "PickSchool", 0) > 0)
                any_school = 1;
        if (o->school_s && !any_school) {
            fprintf(stderr, "bicgen: --school applies to wizards only\n");
            return -1;
        }
        if (o->school_s && pl.school == -1) {
            fprintf(stderr, "bicgen: unknown school '%s' (try --list)\n", o->school_s);
            return -1;
        }
        if (gd)
            guide_attach(gd, &pl);
        if (plan_run(ru, &c, &pl, &g, err, sizeof err) != 0) {
            if (gd && gd->quit)
                fprintf(stderr, "bicgen: quit; nothing written\n");
            else
                fprintf(stderr, "bicgen: %s\n", err);
            chr_free(&c);
            return -1;
        }
        plan_finish(ru, &c, &g);
        if (gd && guide_finish(gd, &c) != 0) {
            fprintf(stderr, "bicgen: quit; nothing written\n");
            chr_free(&c);
            return -1;
        }
    }
    for (n = 0; n < o->lo.npins; n++) {
        int f = o->lo.pin[n].feat;

        if (!chr_has_feat(&c, f)) {
            char nm[160], msg[400];
            int last = c.lv[c.nlevels - 1].cls;

            snprintf(msg, sizeof msg, "pinned feat '%s' could not be taken: %s",
                     cli_tname(t, rules_feats(ru), f, "FEAT", nm, sizeof nm),
                     feat_selectable(ru, &c, last, f, 0) || feat_selectable(ru, &c, last, f, 1) ?
                     "it only became legal after the last free feat slot" :
                     "no slot ever had its prerequisites and class restrictions met");
            if (!gd) {
                fprintf(stderr, "bicgen: %s\n", msg);
                return -1;
            }
            /* a guided session keeps its answers: the person decides at review */
            snprintf(warn + strlen(warn), sizeof warn - strlen(warn), "%s\n", msg);
        }
    }

    if (gd) {
        int w = guide_review(gd, &c, warn);

        if (w != 1) {
            fprintf(stderr, "bicgen: %s; nothing written\n", w < 0 ? "quit" : "declined");
            chr_free(&c);
            return -1;
        }
    }
    tree = chr_to_gff(ru, &c, &g);
    gff_write(tree, "BIC ", &out);

    cli_file_name(&c, base, sizeof base);
    snprintf(path, sizeof path, "%s/%s.bic", outdir, base);
    for (n = 1; cli_exists(path) && n < 1000; n++)
        snprintf(path, sizeof path, "%s/%.13s%d.bic", outdir, base, n);
    if (write_file(path, out.p, out.n) != 0) {
        fprintf(stderr, "bicgen: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    printf("seed %llu -> %s\n", (unsigned long long)seed, path);
    if (gd) {
        char ap[1100];
        size_t stem;

        snprintf(ap, sizeof ap, "%s", path);
        stem = strrchr(ap, '.') ? (size_t)(strrchr(ap, '.') - ap) : strlen(ap);
        snprintf(ap + stem, sizeof ap - stem, ".answers");
        if (guide_save(gd, ap, command) == 0)
            printf("  answers saved to %s (replay: %s < that file)\n", ap, command);
        else
            fprintf(stderr, "bicgen: cannot write %s\n", ap);
    }
    if (o->show)
        cli_sheet(ru, t, &c);
    if (o->install_vault && user && *user) {
        char vp[1100];
        snprintf(vp, sizeof vp, "%s/localvault/%s", user, strrchr(path, '/') + 1);
        if (cli_exists(vp))
            fprintf(stderr, "bicgen: %s exists; not overwriting\n", vp);
        else if (write_file(vp, out.p, out.n) == 0)
            printf("  installed to %s\n", vp);
        else
            fprintf(stderr, "bicgen: cannot write %s\n", vp);
    }
    gff_free(tree);
    free(out.p);
    chr_free(&c);
    return 0;
}

int main(int argc, char **argv)
{
    static const char *an[NABIL] = { "--str", "--dex", "--con", "--int", "--wis", "--cha" };
    opts o;
    char err[200], user[1024], idir[1024];
    const char *v;
    res *r;
    rules *ru;
    tlk *t = NULL;
    int i, a, fails = 0;
    char tp[1100], command[2048] = "bicgen";
    guide gd;

    memset(&o, 0, sizeof o);
    memset(&gd, 0, sizeof gd);
    o.level = 1;
    o.max_classes = 1;
    o.lo.variety = 2;
    o.hm = HP_ROLL;
    o.count = 1;
    o.outdir = "out";
    for (a = 0; a < NABIL; a++)
        o.fixed[a] = -1;
    o.seed = (uint64_t)time(NULL) * 2654435761u ^ (uint64_t)getpid();

    for (i = 1; i < argc; i++) {   /* a spec file first, so the command line wins */
        int j = i;

        if (want_arg(argc, argv, &j, "--spec", &v)) {
            if (load_spec(v, &o, err, sizeof err) != 0) {
                fprintf(stderr, "bicgen: %s\n", err);
                return 2;
            }
            i = j;
        }
    }
    for (i = 1; i < argc; i++) {
        int matched = 0;
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage();
            return 0;
        } else if (!strcmp(argv[i], "--show")) {
            o.show = 1;
        } else if (!strcmp(argv[i], "--list")) {
            o.list = 1;
        } else if (!strcmp(argv[i], "--install")) {
            o.install_vault = 1;
        } else if (!strcmp(argv[i], "--guided") || !strcmp(argv[i], "-g")) {
            o.guided = 1;
        } else if (want_arg(argc, argv, &i, "--nwn-dir", &o.install) ||
                   want_arg(argc, argv, &i, "--user-dir", &o.user) ||
                   want_arg(argc, argv, &i, "--race", &o.race_s) ||
                   want_arg(argc, argv, &i, "--class", &o.class_s) ||
                   want_arg(argc, argv, &i, "--gender", &o.gender_s) ||
                   want_arg(argc, argv, &i, "--align", &o.align_s) ||
                   want_arg(argc, argv, &i, "--school", &o.school_s) ||
                   want_arg(argc, argv, &i, "--classes", &o.classes_s) ||
                   want_arg(argc, argv, &i, "--hp", &o.hp_s) ||
                   want_arg(argc, argv, &i, "--variety", &o.variety_s) ||
                   want_arg(argc, argv, &i, "--spec", &o.spec_s) ||
                   want_arg(argc, argv, &i, "--find-feat", &o.find_feat_s) ||
                   want_arg(argc, argv, &i, "--name", &o.name) ||
                   want_arg(argc, argv, &i, "--out-dir", &o.outdir)) {
            ;
        } else if (want_arg(argc, argv, &i, "--pin", &v)) {
            if (add_names(o.pin_s, o.pin_from, &o.npin, v) != 0) {
                fprintf(stderr, "bicgen: too many --pin feats (limit %d)\n", MAX_PINS);
                return 2;
            }
        } else if (want_arg(argc, argv, &i, "--avoid", &v)) {
            if (add_names(o.avoid_s, NULL, &o.navoid, v) != 0) {
                fprintf(stderr, "bicgen: too many --avoid feats (limit %d)\n", MAX_PINS);
                return 2;
            }
        } else if (want_arg(argc, argv, &i, "--level", &v)) {
            o.level = atoi(v);
            o.have_level = 1;
        } else if (want_arg(argc, argv, &i, "--max-classes", &v)) {
            o.max_classes = atoi(v);
        } else if (want_arg(argc, argv, &i, "--count", &v)) {
            o.count = atoi(v);
        } else if (want_arg(argc, argv, &i, "--seed", &v)) {
            o.seed = strtoull(v, NULL, 10);
            o.have_seed = 1;
        } else {
            for (a = 0; a < NABIL && !matched; a++)
                if (want_arg(argc, argv, &i, an[a], &v)) {
                    o.fixed[a] = atoi(v);
                    matched = 1;
                }
            if (!matched) {
                fprintf(stderr, "bicgen: unknown option '%s' (try --help)\n", argv[i]);
                return 2;
            }
        }
    }
    if (o.hp_s) {
        if (!strcmp(o.hp_s, "max"))
            o.hm = HP_MAX;
        else if (!strcmp(o.hp_s, "avg"))
            o.hm = HP_AVG;
        else if (strcmp(o.hp_s, "roll")) {
            fprintf(stderr, "bicgen: --hp takes max, avg or roll\n");
            return 2;
        }
    }
    if (o.variety_s) {
        o.lo.variety = atoi(o.variety_s);
        if (o.lo.variety < 0 || o.lo.variety > VARIETY_MAX ||
            !isdigit((unsigned char)o.variety_s[0])) {
            fprintf(stderr, "bicgen: --variety takes 0 to %d\n", VARIETY_MAX);
            return 2;
        }
    }
    if (o.max_classes < 1 || o.max_classes > MAX_CLASSES) {
        fprintf(stderr, "bicgen: --max-classes takes 1 to %d\n", MAX_CLASSES);
        return 2;
    }

    if (!o.install)
        o.install = getenv("NWN_HOME") ? getenv("NWN_HOME") : DEFAULT_INSTALL;
    snprintf(idir, sizeof idir, "%s", o.install);
    if (o.user)
        snprintf(user, sizeof user, "%s", o.user);
    else
        default_user_dir(user, sizeof user);

    r = res_open(idir, *user ? user : NULL, err, sizeof err);
    if (!r || !(ru = rules_open(r, err, sizeof err))) {
        fprintf(stderr, "bicgen: %s\n", err);
        return 1;
    }
    if (o.classes_s) {
        int k, sum = 0;

        if (cli_parse_classes(ru, o.classes_s, o.cl, o.cn, &o.ncl) != 0) {
            fprintf(stderr, "bicgen: bad --classes '%s' (want name:levels,... with at most "
                    "%d distinct base classes)\n", o.classes_s, MAX_CLASSES);
            return 2;
        }
        for (k = 0; k < o.ncl; k++)
            sum += o.cn[k];
        if (o.have_level && o.level != sum) {
            fprintf(stderr, "bicgen: --classes adds up to %d levels but --level is %d\n",
                    sum, o.level);
            return 2;
        }
        o.level = sum;
    }
    if (o.level < 1 || o.level > MAX_CHAR_LEVEL) {
        fprintf(stderr, "bicgen: --level takes 1 to 40\n");
        return 2;
    }
    snprintf(tp, sizeof tp, "%s/lang/en/data/dialog.tlk", idir);
    t = tlk_open(tp);

    if (o.find_feat_s) {
        cli_list_feats(ru, t, o.find_feat_s);
        return 0;
    }
    for (i = 0; i < o.npin; i++) {
        int f = cli_find_feat(ru, t, o.pin_s[i]);

        if (f < 0) {
            fprintf(stderr, "bicgen: unknown feat '%s'; close matches:\n", o.pin_s[i]);
            cli_list_feats(ru, t, o.pin_s[i]);
            return 2;
        }
        o.lo.pin[o.lo.npins].feat = f;
        o.lo.pin[o.lo.npins++].from = o.pin_from[i];
    }
    for (i = 0; i < o.navoid; i++) {
        int f = cli_find_feat(ru, t, o.avoid_s[i]);

        if (f < 0) {
            fprintf(stderr, "bicgen: unknown feat '%s' in --avoid\n", o.avoid_s[i]);
            return 2;
        }
        o.lo.avoid[o.lo.navoid++] = f;
    }
    if (o.list) {
        char b[160];
        int k;
        puts("Races:");
        for (k = 0; k < rules_races(ru)->nrows; k++)
            if (twoda_int(rules_races(ru), k, "PlayerRace", 0))
                printf("  %2d %s\n", k, cli_tname(t, rules_races(ru), k, "Name", b, sizeof b));
        puts("Wizard schools (--school):");
        for (k = 0; k < rules_tab(ru, "spellschools")->nrows; k++)
            printf("  %2d %s\n", k, twoda_get(rules_tab(ru, "spellschools"), k, "Label"));
        puts("Classes:");
        for (k = 0; k < rules_nclasses(ru); k++)
            if (twoda_int(rules_classes(ru), k, "PlayerClass", 0) == 1 &&
                !twoda_get(rules_classes(ru), k, "PreReqTable"))
                printf("  %2d %s\n", k, cli_tname(t, rules_classes(ru), k, "Name", b, sizeof b));
        puts("Prestige classes (--classes only, after the entry requirements are met):");
        for (k = 0; k < rules_nclasses(ru); k++)
            if (twoda_int(rules_classes(ru), k, "PlayerClass", 0) == 1 &&
                twoda_get(rules_classes(ru), k, "PreReqTable") && lu_prestige_supported(ru, k))
                printf("  %2d %s\n", k, cli_tname(t, rules_classes(ru), k, "Name", b, sizeof b));
        return 0;
    }
    if (o.guided) {
        guide_given gv;
        int row = -1;

        if (o.count != 1) {
            fprintf(stderr, "bicgen: --guided builds one character (--count must be 1)\n");
            return 2;
        }
        memset(&gv, 0, sizeof gv);
        guide_open(&gd, ru, t, o.seed, stdin, stdout);
        gv.race = o.race_s != NULL;
        gv.level = o.have_level || o.ncl;
        gv.align = o.align_s != NULL;
        gv.gender = o.gender_s != NULL;
        gv.name = o.name != NULL;
        gv.hp = o.hp_s != NULL;
        gv.variety = o.variety_s != NULL;
        for (a = 0; a < NABIL; a++)
            gv.abil[a] = o.fixed[a] >= 0;
        if (gv.level)
            gd.level = o.level;
        if (o.ncl) {
            gd.ncl = o.ncl;
            memcpy(gd.cl, o.cl, sizeof gd.cl);
            memcpy(gd.cn, o.cn, sizeof gd.cn);
        } else if (o.class_s) {
            row = cli_find_row(rules_classes(ru), o.class_s, 1);
            if (row < 0) {
                fprintf(stderr, "bicgen: unknown class '%s'\n", o.class_s);
                return 2;
            }
            snprintf(gd.cls, sizeof gd.cls, "%d", row);
        }
        if (guide_setup(&gd, &gv) != 0) {
            fprintf(stderr, "bicgen: quit; nothing written\n");
            guide_close(&gd);
            tlk_free(t);
            rules_close(ru);
            res_close(r);
            return 1;
        }
        if (gd.race[0])
            o.race_s = gd.race;
        if (!gv.level) {
            o.level = gd.level;
            o.have_level = 1;
        }
        if (gd.ncl && !o.ncl) {
            memcpy(o.cl, gd.cl, sizeof o.cl);
            memcpy(o.cn, gd.cn, sizeof o.cn);
            o.ncl = gd.ncl;
        }
        if (gd.cls[0] && !o.class_s && !o.ncl)
            o.class_s = gd.cls;
        if (gd.max_classes > 1)
            o.max_classes = gd.max_classes;
        if (gd.align[0])
            o.align_s = gd.align;
        if (gd.gender[0])
            o.gender_s = gd.gender;
        if (gd.name[0])
            o.name = gd.name;
        for (a = 0; a < NABIL; a++)
            if (gd.fixed[a] >= 0)
                o.fixed[a] = gd.fixed[a];
        if (gd.have_hm)
            o.hm = (hp_mode)gd.hm;
        if (gd.have_variety)
            o.lo.variety = gd.variety;
        {   /* the command that replays this session */
            size_t k = strlen(command);

            for (i = 1; i < argc && k + 2 < sizeof command; i++) {
                int sp = strchr(argv[i], ' ') != NULL;

                k += (size_t)snprintf(command + k, sizeof command - k, sp ? " '%s'" : " %s",
                                      argv[i]);
            }
            if (!o.have_seed && k < sizeof command)
                snprintf(command + k, sizeof command - k, " --seed %llu",
                         (unsigned long long)o.seed);
        }
    }
    if (mkdir(o.outdir, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "bicgen: cannot create %s: %s\n", o.outdir, strerror(errno));
        return 1;
    }
    for (i = 0; i < o.count; i++)
        if (build_one(ru, &o, o.seed + (uint64_t)i, o.outdir, t, user,
                      o.guided ? &gd : NULL, command) != 0)
            fails++;
    guide_close(&gd);
    tlk_free(t);
    rules_close(ru);
    res_close(r);
    return fails ? 1 : 0;
}
