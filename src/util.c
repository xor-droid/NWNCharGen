#define _POSIX_C_SOURCE 200809L
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die_oom(void)
{
    fputs("out of memory\n", stderr);
    exit(1);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        die_oom();
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p)
        die_oom();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        die_oom();
    return p;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n)
{
    char *r = xmalloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *d = NULL;
    long sz;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    d = xmalloc((size_t)sz + 1);
    if (fread(d, 1, (size_t)sz, f) != (size_t)sz) {
        free(d);
        fclose(f);
        return NULL;
    }
    fclose(f);
    d[sz] = '\0'; /* convenience for text files */
    if (len)
        *len = (size_t)sz;
    return d;
}

int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");

    if (!f)
        return -1;
    if (fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

void buf_put(buf *b, const void *d, size_t n)
{
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap : 256;
        while (c < b->n + n)
            c *= 2;
        b->p = xrealloc(b->p, c);
        b->cap = c;
    }
    if (n)
        memcpy(b->p + b->n, d, n);
    b->n += n;
}

void buf_zero(buf *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        buf_u8(b, 0);
}

void buf_u8(buf *b, uint32_t v)
{
    uint8_t t = (uint8_t)v;
    buf_put(b, &t, 1);
}

void buf_u16(buf *b, uint32_t v)
{
    uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    buf_put(b, t, 2);
}

void buf_u32(buf *b, uint32_t v)
{
    uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16),
                     (uint8_t)(v >> 24) };
    buf_put(b, t, 4);
}

void buf_u64(buf *b, uint64_t v)
{
    buf_u32(b, (uint32_t)v);
    buf_u32(b, (uint32_t)(v >> 32));
}

uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32);
}

void set_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
