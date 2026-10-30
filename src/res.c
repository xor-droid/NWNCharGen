#define _POSIX_C_SOURCE 200809L
#include "res.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"

static const struct {
    int type;
    const char *ext;
} exts[] = {
    { RES_BMP, "bmp" }, { RES_TGA, "tga" }, { RES_WAV, "wav" },
    { RES_NSS, "nss" }, { RES_NCS, "ncs" }, { 2002, "mdl" }, { 2011, "mod" }, { RES_ARE, "are" },
    { 2013, "set" }, { RES_IFO, "ifo" }, { RES_BIC, "bic" }, { 2016, "wok" },
    { RES_2DA, "2da" }, { RES_TLK, "tlk" }, { 2022, "txi" }, { 2023, "git" },
    { RES_UTI, "uti" }, { 2026, "btc" }, { RES_UTC, "utc" }, { RES_DLG, "dlg" },
    { RES_ITP, "itp" }, { 2032, "utt" }, { 2033, "dds" }, { 2035, "uts" },
    { 2036, "ltr" }, { RES_GFF, "gff" }, { 2038, "fac" }, { 2040, "ute" },
    { 2042, "utd" }, { 2044, "utp" }, { 2045, "dft" }, { 2046, "gic" },
    { 2047, "gui" }, { 2051, "utm" }, { 2052, "dwk" }, { 2053, "pwk" },
    { 2056, "jrl" }, { 2058, "utw" }, { 2060, "ssf" }, { 2064, "ndb" },
    { 2065, "ptm" }, { 2066, "ptt" }, { 9998, "nui" },
};

const char *res_ext(int type)
{
    size_t i;
    for (i = 0; i < sizeof exts / sizeof *exts; i++)
        if (exts[i].type == type)
            return exts[i].ext;
    return NULL;
}

int res_type_from_ext(const char *ext)
{
    size_t i;
    for (i = 0; i < sizeof exts / sizeof *exts; i++)
        if (strcasecmp(exts[i].ext, ext) == 0)
            return exts[i].type;
    return -1;
}

typedef struct {
    char name[17];       /* lower-cased */
    uint16_t type;
    uint16_t bif;        /* index into r->bifs */
    uint32_t idx;        /* resource index within the BIF */
    uint32_t seq;        /* load order; later wins */
} key_ent;

struct res {
    char *install, *user;
    char **bifs;
    size_t nbifs;
    key_ent *keys;
    size_t nkeys, capkeys;
};

static int key_name_cmp(const key_ent *x, const key_ent *y)
{
    if (x->type != y->type)
        return x->type < y->type ? -1 : 1;
    return strcmp(x->name, y->name);
}

static int key_cmp(const void *a, const void *b)
{
    const key_ent *x = a, *y = b;
    int c = key_name_cmp(x, y);

    if (c)
        return c;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* Resolve rel (using / or \ separators) under base, matching each component
 * case-insensitively. Returns malloc'd path or NULL. */
static char *path_ci(const char *base, const char *rel)
{
    char *cur = xstrdup(base);
    char *copy = xstrdup(rel), *save = NULL, *comp;

    for (comp = copy; *comp; comp++)
        if (*comp == '\\')
            *comp = '/';
    for (comp = strtok_r(copy, "/", &save); comp; comp = strtok_r(NULL, "/", &save)) {
        DIR *d = opendir(cur);
        struct dirent *e;
        char *next = NULL;

        if (!d)
            goto bad;
        while ((e = readdir(d)) != NULL) {
            if (strcasecmp(e->d_name, comp) == 0) {
                next = xmalloc(strlen(cur) + strlen(e->d_name) + 2);
                sprintf(next, "%s/%s", cur, e->d_name);
                break;
            }
        }
        closedir(d);
        if (!next)
            goto bad;
        free(cur);
        cur = next;
    }
    free(copy);
    return cur;
bad:
    free(copy);
    free(cur);
    return NULL;
}

static int load_key(res *r, const char *path, char *err, size_t errlen)
{
    size_t n, i;
    uint8_t *d = read_file(path, &n);
    uint32_t nbif, nkey, fo, ko;
    size_t base_bif = r->nbifs;
    static uint32_t seq;

    if (!d) {
        snprintf(err, errlen, "cannot read %s", path);
        return -1;
    }
    if (n < 64 || memcmp(d, "KEY V1", 6) != 0) {
        snprintf(err, errlen, "%s is not a KEY file", path);
        free(d);
        return -1;
    }
    nbif = get_u32(d + 8);
    nkey = get_u32(d + 12);
    fo = get_u32(d + 16);
    ko = get_u32(d + 20);
    if ((uint64_t)fo + (uint64_t)nbif * 12 > n ||
        (uint64_t)ko + (uint64_t)nkey * 22 > n) {
        snprintf(err, errlen, "%s is truncated", path);
        free(d);
        return -1;
    }

    r->bifs = xrealloc(r->bifs, (r->nbifs + nbif) * sizeof *r->bifs);
    for (i = 0; i < nbif; i++) {
        const uint8_t *e = d + fo + i * 12;
        uint32_t off = get_u32(e + 4);
        uint16_t sz = get_u16(e + 8);
        char *rel;

        if ((uint64_t)off + sz > n) {
            snprintf(err, errlen, "%s: bad BIF name", path);
            free(d);
            return -1;
        }
        rel = xstrndup((const char *)d + off, sz);
        r->bifs[r->nbifs++] = path_ci(r->install, rel);
        if (!r->bifs[r->nbifs - 1])
            r->bifs[r->nbifs - 1] = rel;
        else
            free(rel);
    }

    if (r->nkeys + nkey > r->capkeys) {
        r->capkeys = r->nkeys + nkey;
        r->keys = xrealloc(r->keys, r->capkeys * sizeof *r->keys);
    }
    seq++;
    for (i = 0; i < nkey; i++) {
        const uint8_t *e = d + ko + i * 22;
        key_ent *k = &r->keys[r->nkeys++];
        uint32_t id = get_u32(e + 18);
        size_t j;

        memset(k, 0, sizeof *k);
        for (j = 0; j < 16 && e[j]; j++)
            k->name[j] = (char)tolower(e[j]);
        k->type = get_u16(e + 16);
        k->bif = (uint16_t)(base_bif + (id >> 20));
        k->idx = id & 0xfffff;
        k->seq = seq;
    }
    free(d);
    return 0;
}

res *res_open(const char *install, const char *user, char *err, size_t errlen)
{
    static const char *keys[] = { "data/nwn_base.key", "data/nwn_retail.key" };
    res *r = xcalloc(1, sizeof *r);
    size_t i;

    r->install = xstrdup(install);
    r->user = user ? xstrdup(user) : NULL;
    for (i = 0; i < sizeof keys / sizeof *keys; i++) {
        char *p = path_ci(install, keys[i]);
        int rc;

        if (!p) {
            snprintf(err, errlen, "%s/%s not found", install, keys[i]);
            res_close(r);
            return NULL;
        }
        rc = load_key(r, p, err, errlen);
        free(p);
        if (rc != 0) {
            res_close(r);
            return NULL;
        }
    }
    qsort(r->keys, r->nkeys, sizeof *r->keys, key_cmp);
    return r;
}

void res_close(res *r)
{
    size_t i;

    if (!r)
        return;
    for (i = 0; i < r->nbifs; i++)
        free(r->bifs[i]);
    free(r->bifs);
    free(r->keys);
    free(r->install);
    free(r->user);
    free(r);
}

const char *res_install_dir(const res *r)
{
    return r->install;
}

static uint8_t *read_bif(const char *path, uint32_t idx, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t h[20], e[16], *d = NULL;
    uint32_t nvar, tab, off, sz;

    if (!f)
        return NULL;
    if (fread(h, 1, 20, f) != 20 || memcmp(h, "BIFFV1", 6) != 0)
        goto out;
    nvar = get_u32(h + 8);
    tab = get_u32(h + 16);
    if (idx >= nvar)
        goto out;
    if (fseek(f, (long)tab + (long)idx * 16, SEEK_SET) != 0 ||
        fread(e, 1, 16, f) != 16)
        goto out;
    off = get_u32(e + 4);
    sz = get_u32(e + 8);
    if (fseek(f, (long)off, SEEK_SET) != 0)
        goto out;
    d = xmalloc((size_t)sz + 1);
    if (fread(d, 1, sz, f) != sz) {
        free(d);
        d = NULL;
        goto out;
    }
    d[sz] = '\0';
    if (len)
        *len = sz;
out:
    fclose(f);
    return d;
}

static uint8_t *read_override(const char *dir, const char *fname, size_t *len)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    uint8_t *data = NULL;

    if (!d)
        return NULL;
    while ((e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, fname) == 0) {
            char *p = xmalloc(strlen(dir) + strlen(e->d_name) + 2);
            sprintf(p, "%s/%s", dir, e->d_name);
            data = read_file(p, len);
            free(p);
            break;
        }
    }
    closedir(d);
    return data;
}

uint8_t *res_get(res *r, const char *name, int type, size_t *len)
{
    key_ent probe, *lo, *hi, *found = NULL;
    const char *ext = res_ext(type);
    char fname[64], dir[1024];
    uint8_t *d;
    size_t i;

    if (strlen(name) > 16)
        return NULL;
    if (ext) {
        snprintf(fname, sizeof fname, "%s.%s", name, ext);
        if (r->user) {
            snprintf(dir, sizeof dir, "%s/override", r->user);
            if ((d = read_override(dir, fname, len)) != NULL)
                return d;
        }
        snprintf(dir, sizeof dir, "%s/ovr", r->install);
        if ((d = read_override(dir, fname, len)) != NULL)
            return d;
    }

    memset(&probe, 0, sizeof probe);
    for (i = 0; name[i]; i++)
        probe.name[i] = (char)tolower((unsigned char)name[i]);
    probe.type = (uint16_t)type;
    /* Lower-bound on (type, name); duplicates are ordered by load sequence,
     * so the last equal entry is the one that wins. */
    lo = r->keys;
    hi = r->keys + r->nkeys;
    while (lo < hi) {
        key_ent *mid = lo + (hi - lo) / 2;
        if (key_name_cmp(mid, &probe) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == r->keys + r->nkeys || key_name_cmp(lo, &probe) != 0)
        return NULL;
    found = lo;
    while (found + 1 < r->keys + r->nkeys && key_name_cmp(found + 1, &probe) == 0)
        found++;
    return read_bif(r->bifs[found->bif], found->idx, len);
}

twoda *res_2da(res *r, const char *name)
{
    size_t n;
    uint8_t *d = res_get(r, name, RES_2DA, &n);
    twoda *t;

    if (!d)
        return NULL;
    t = twoda_parse((const char *)d, n);
    free(d);
    return t;
}

void res_each(res *r, int type, res_visit fn, void *arg)
{
    size_t i;

    for (i = 0; i < r->nkeys; i++) {
        const key_ent *k = &r->keys[i];
        if (type < 0 || k->type == type)
            fn(k->name, k->type, r->bifs[k->bif], arg);
    }
}
