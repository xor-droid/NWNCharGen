#define _POSIX_C_SOURCE 200809L
#include "tlk.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

#define TLK_HDR 20
#define TLK_ENT 40

struct tlk {
    uint8_t *d;
    size_t n;
    uint32_t count, str_off;
};

tlk *tlk_open(const char *path)
{
    size_t n;
    uint8_t *d = read_file(path, &n);
    tlk *t;

    if (!d)
        return NULL;
    if (n < TLK_HDR || memcmp(d, "TLK V3.0", 8) != 0) {
        free(d);
        return NULL;
    }
    t = xcalloc(1, sizeof *t);
    t->d = d;
    t->n = n;
    t->count = get_u32(d + 12);
    t->str_off = get_u32(d + 16);
    if ((uint64_t)TLK_HDR + (uint64_t)t->count * TLK_ENT > n) {
        tlk_free(t);
        return NULL;
    }
    return t;
}

void tlk_free(tlk *t)
{
    if (t) {
        free(t->d);
        free(t);
    }
}

uint32_t tlk_count(const tlk *t)
{
    return t->count;
}

int tlk_copy(const tlk *t, uint32_t strref, char *out, size_t cap)
{
    const uint8_t *e;
    uint32_t off, len;
    size_t m;

    if (cap)
        out[0] = '\0';
    if (strref >= t->count)
        return -1;
    e = t->d + TLK_HDR + (size_t)strref * TLK_ENT;
    if (!(get_u32(e) & 1))
        return -1;
    off = get_u32(e + 28);
    len = get_u32(e + 32);
    if ((uint64_t)t->str_off + off + len > t->n)
        return -1;
    m = len < cap ? len : (cap ? cap - 1 : 0);
    if (cap) {
        memcpy(out, t->d + t->str_off + off, m);
        out[m] = '\0';
    }
    return (int)len;
}
