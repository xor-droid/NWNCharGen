#define _POSIX_C_SOURCE 200809L
#include "gff.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define HDR_SIZE 56
#define MAX_DEPTH 64

/* ---- construction and lookup ------------------------------------------ */

gff_struct *gff_new(uint32_t type)
{
    gff_struct *s = xcalloc(1, sizeof *s);
    s->type = type;
    return s;
}

static void field_free(gff_field *f)
{
    size_t i;

    switch (f->type) {
    case GFF_EXOSTR:
    case GFF_RESREF:
        free(f->v.s);
        break;
    case GFF_LOCSTR:
        for (i = 0; i < f->v.loc.n; i++)
            free(f->v.loc.e[i].text);
        free(f->v.loc.e);
        break;
    case GFF_VOID:
    case GFF_ORIENT:
    case GFF_VECTOR:
        free(f->v.raw.p);
        break;
    case GFF_STRUCT:
        gff_free(f->v.st);
        break;
    case GFF_LIST:
        for (i = 0; i < f->v.list.n; i++)
            gff_free(f->v.list.v[i]);
        free(f->v.list.v);
        break;
    default:
        break;
    }
    free(f);
}

void gff_free(gff_struct *s)
{
    size_t i;

    if (!s)
        return;
    for (i = 0; i < s->n; i++)
        field_free(s->f[i]);
    free(s->f);
    free(s);
}

gff_field *gff_get(const gff_struct *s, const char *label)
{
    size_t i;

    for (i = 0; i < s->n; i++)
        if (strncmp(s->f[i]->label, label, 16) == 0)
            return s->f[i];
    return NULL;
}

/* Returns a fresh, empty field of the given type, replacing any existing
 * field with the same label (keeps its position). */
static gff_field *field_slot(gff_struct *s, const char *label, int type)
{
    gff_field *f = xcalloc(1, sizeof *f);
    size_t i;

    strncpy(f->label, label, 16);
    f->type = type;
    for (i = 0; i < s->n; i++) {
        if (strncmp(s->f[i]->label, label, 16) == 0) {
            field_free(s->f[i]);
            s->f[i] = f;
            return f;
        }
    }
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->f = xrealloc(s->f, s->cap * sizeof *s->f);
    }
    s->f[s->n++] = f;
    return f;
}

void gff_set_uint(gff_struct *s, const char *label, int type, uint64_t v)
{
    field_slot(s, label, type)->v.u = v;
}

void gff_set_int(gff_struct *s, const char *label, int type, int64_t v)
{
    field_slot(s, label, type)->v.i = v;
}

void gff_set_float(gff_struct *s, const char *label, double v)
{
    field_slot(s, label, GFF_FLOAT)->v.d = (float)v;
}

void gff_set_str(gff_struct *s, const char *label, int type, const char *v)
{
    field_slot(s, label, type)->v.s = xstrdup(v);
}

void gff_set_locstr(gff_struct *s, const char *label, uint32_t strref,
                    const char *english)
{
    gff_field *f = field_slot(s, label, GFF_LOCSTR);

    f->v.loc.strref = strref;
    if (english) {
        f->v.loc.e = xcalloc(1, sizeof *f->v.loc.e);
        f->v.loc.e[0].id = 0;
        f->v.loc.e[0].text = xstrdup(english);
        f->v.loc.n = 1;
    }
}

void gff_set_void(gff_struct *s, const char *label, const void *p, uint32_t n)
{
    gff_field *f = field_slot(s, label, GFF_VOID);

    f->v.raw.p = xmalloc(n);
    if (n)
        memcpy(f->v.raw.p, p, n);
    f->v.raw.n = n;
}

void gff_set_struct(gff_struct *s, const char *label, gff_struct *child)
{
    field_slot(s, label, GFF_STRUCT)->v.st = child;
}

gff_struct *gff_clone(const gff_struct *s)
{
    gff_struct *d = gff_new(s->type);
    size_t i;

    for (i = 0; i < s->n; i++)
        gff_put_copy(d, s->f[i]);
    return d;
}

void gff_put_copy(gff_struct *dst, const gff_field *f)
{
    gff_field *n = field_slot(dst, f->label, f->type);
    size_t i;

    switch (f->type) {
    case GFF_EXOSTR:
    case GFF_RESREF:
        n->v.s = xstrdup(f->v.s ? f->v.s : "");
        break;
    case GFF_LOCSTR:
        n->v.loc.strref = f->v.loc.strref;
        if (f->v.loc.n) {
            n->v.loc.e = xcalloc(f->v.loc.n, sizeof *n->v.loc.e);
            for (i = 0; i < f->v.loc.n; i++) {
                n->v.loc.e[i].id = f->v.loc.e[i].id;
                n->v.loc.e[i].text = xstrdup(f->v.loc.e[i].text ? f->v.loc.e[i].text : "");
            }
            n->v.loc.n = f->v.loc.n;
        }
        break;
    case GFF_VOID:
    case GFF_ORIENT:
    case GFF_VECTOR:
        n->v.raw.p = xmalloc(f->v.raw.n ? f->v.raw.n : 1);
        if (f->v.raw.n)
            memcpy(n->v.raw.p, f->v.raw.p, f->v.raw.n);
        n->v.raw.n = f->v.raw.n;
        break;
    case GFF_STRUCT:
        n->v.st = gff_clone(f->v.st);
        break;
    case GFF_LIST:
        for (i = 0; i < f->v.list.n; i++)
            gff_list_add(dst, f->label, gff_clone(f->v.list.v[i]));
        break;
    default:
        n->v = f->v;
        break;
    }
}

void gff_remove(gff_struct *s, const char *label)
{
    size_t i;

    for (i = 0; i < s->n; i++)
        if (strncmp(s->f[i]->label, label, 16) == 0) {
            field_free(s->f[i]);
            memmove(s->f + i, s->f + i + 1, (s->n - i - 1) * sizeof *s->f);
            s->n--;
            return;
        }
}

void gff_list_init(gff_struct *s, const char *label)
{
    if (!gff_get(s, label))
        field_slot(s, label, GFF_LIST);
}

void gff_list_add(gff_struct *s, const char *label, gff_struct *child)
{
    gff_field *f = gff_get(s, label);

    if (!f)
        f = field_slot(s, label, GFF_LIST);
    if (f->v.list.n == f->v.list.cap) {
        f->v.list.cap = f->v.list.cap ? f->v.list.cap * 2 : 8;
        f->v.list.v = xrealloc(f->v.list.v, f->v.list.cap * sizeof(gff_struct *));
    }
    f->v.list.v[f->v.list.n++] = child;
}

int64_t gff_get_int(const gff_struct *s, const char *label, int64_t def)
{
    gff_field *f = gff_get(s, label);

    if (!f)
        return def;
    switch (f->type) {
    case GFF_BYTE: case GFF_WORD: case GFF_DWORD: case GFF_DWORD64:
        return (int64_t)f->v.u;
    case GFF_CHAR: case GFF_SHORT: case GFF_INT: case GFF_INT64:
        return f->v.i;
    default:
        return def;
    }
}

const char *gff_get_str(const gff_struct *s, const char *label)
{
    gff_field *f = gff_get(s, label);

    if (f && (f->type == GFF_EXOSTR || f->type == GFF_RESREF))
        return f->v.s;
    return NULL;
}

/* ---- reader ------------------------------------------------------------ */

typedef struct {
    const uint8_t *d;
    size_t n;
    uint32_t so, sc, fo, fc, lo, lc, fdo, fdn, fio, fin, lio, lin;
    char *err;
    size_t errlen;
} rd;

static int fail(rd *r, const char *msg)
{
    snprintf(r->err, r->errlen, "%s", msg);
    return -1;
}

static int in_range(const rd *r, uint64_t off, uint64_t len)
{
    return off <= r->n && len <= r->n - off;
}

static int read_struct(rd *r, uint32_t idx, int depth, gff_struct **out);

static int read_field(rd *r, uint32_t idx, int depth, gff_field **out)
{
    const uint8_t *p = r->d + r->fo + (size_t)idx * 12;
    uint32_t type = get_u32(p), li = get_u32(p + 4), data = get_u32(p + 8);
    const uint8_t *q;
    uint64_t off;
    gff_field *f = xcalloc(1, sizeof *f);
    uint32_t len, i;

    *out = f;
    f->type = (int)type;
    if (li >= r->lc)
        return fail(r, "label index out of range");
    memcpy(f->label, r->d + r->lo + (size_t)li * 16, 16);
    f->label[16] = '\0';

    switch (type) {
    case GFF_BYTE:  f->v.u = data & 0xff; return 0;
    case GFF_CHAR:  f->v.i = (int8_t)(data & 0xff); return 0;
    case GFF_WORD:  f->v.u = data & 0xffff; return 0;
    case GFF_SHORT: f->v.i = (int16_t)(data & 0xffff); return 0;
    case GFF_DWORD: f->v.u = data; return 0;
    case GFF_INT:   f->v.i = (int32_t)data; return 0;
    case GFF_FLOAT: {
        float fl;
        memcpy(&fl, &data, 4);
        f->v.d = fl;
        return 0;
    }
    case GFF_STRUCT:
        if (depth >= MAX_DEPTH)
            return fail(r, "nesting too deep");
        return read_struct(r, data, depth + 1, &f->v.st);
    case GFF_LIST:
        off = (uint64_t)r->lio + data;
        if (data > r->lin || !in_range(r, off, 4))
            return fail(r, "list offset out of range");
        len = get_u32(r->d + off);
        if (!in_range(r, off + 4, (uint64_t)len * 4))
            return fail(r, "list truncated");
        if (depth >= MAX_DEPTH)
            return fail(r, "nesting too deep");
        f->v.list.v = xcalloc(len, sizeof(gff_struct *));
        f->v.list.cap = len;
        for (i = 0; i < len; i++) {
            uint32_t si = get_u32(r->d + off + 4 + (size_t)i * 4);
            if (read_struct(r, si, depth + 1, &f->v.list.v[i]) != 0)
                return -1;
            f->v.list.n = i + 1;
        }
        return 0;
    default:
        break;
    }

    /* Everything else lives in the field data block. */
    off = (uint64_t)r->fdo + data;
    if (data > r->fdn || !in_range(r, off, 1))
        return fail(r, "field data offset out of range");
    q = r->d + off;
    switch (type) {
    case GFF_DWORD64:
    case GFF_INT64:
    case GFF_DOUBLE:
        if (!in_range(r, off, 8))
            return fail(r, "field data truncated");
        if (type == GFF_DOUBLE) {
            uint64_t bits = get_u64(q);
            memcpy(&f->v.d, &bits, 8);
        } else if (type == GFF_INT64) {
            f->v.i = (int64_t)get_u64(q);
        } else {
            f->v.u = get_u64(q);
        }
        return 0;
    case GFF_EXOSTR:
        if (!in_range(r, off, 4) || !in_range(r, off + 4, get_u32(q)))
            return fail(r, "string truncated");
        f->v.s = xstrndup((const char *)q + 4, get_u32(q));
        return 0;
    case GFF_RESREF:
        len = q[0];
        if (!in_range(r, off + 1, len))
            return fail(r, "resref truncated");
        f->v.s = xstrndup((const char *)q + 1, len);
        return 0;
    case GFF_LOCSTR: {
        uint64_t pos = off + 12;
        uint32_t count;

        if (!in_range(r, off, 12))
            return fail(r, "locstring truncated");
        f->v.loc.strref = get_u32(q + 4);
        count = get_u32(q + 8);
        if (count > r->n)
            return fail(r, "locstring count bogus");
        f->v.loc.e = xcalloc(count ? count : 1, sizeof *f->v.loc.e);
        for (i = 0; i < count; i++) {
            if (!in_range(r, pos, 8))
                return fail(r, "locstring entry truncated");
            f->v.loc.e[i].id = get_u32(r->d + pos);
            len = get_u32(r->d + pos + 4);
            if (!in_range(r, pos + 8, len))
                return fail(r, "locstring text truncated");
            f->v.loc.e[i].text = xstrndup((const char *)r->d + pos + 8, len);
            f->v.loc.n = i + 1;
            pos += 8 + (uint64_t)len;
        }
        return 0;
    }
    case GFF_VOID:
        if (!in_range(r, off, 4) || !in_range(r, off + 4, get_u32(q)))
            return fail(r, "void truncated");
        len = get_u32(q);
        f->v.raw.p = xmalloc(len);
        memcpy(f->v.raw.p, q + 4, len);
        f->v.raw.n = len;
        return 0;
    case GFF_ORIENT:
    case GFF_VECTOR:
        len = type == GFF_ORIENT ? 16 : 12;
        if (!in_range(r, off, len))
            return fail(r, "vector truncated");
        f->v.raw.p = xmalloc(len);
        memcpy(f->v.raw.p, q, len);
        f->v.raw.n = len;
        return 0;
    default:
        return fail(r, "unsupported field type");
    }
}

static int read_struct(rd *r, uint32_t idx, int depth, gff_struct **out)
{
    const uint8_t *p;
    uint32_t i, count, dof;
    gff_struct *s;

    if (idx >= r->sc)
        return fail(r, "struct index out of range");
    p = r->d + r->so + (size_t)idx * 12;
    s = gff_new(get_u32(p));
    *out = s;
    dof = get_u32(p + 4);
    count = get_u32(p + 8);
    if (count == 0)
        return 0;
    if (count > r->fc)
        return fail(r, "struct field count bogus");
    if (count > 1 && (dof > r->fin || !in_range(r, (uint64_t)r->fio + dof,
                                                  (uint64_t)count * 4)))
        return fail(r, "field index list out of range");

    s->f = xcalloc(count, sizeof *s->f);
    s->cap = count;
    for (i = 0; i < count; i++) {
        uint32_t fi = count == 1 ? dof : get_u32(r->d + r->fio + dof + (size_t)i * 4);
        if (fi >= r->fc)
            return fail(r, "field index out of range");
        s->n = i + 1;
        if (read_field(r, fi, depth, &s->f[i]) != 0)
            return -1;
    }
    return 0;
}

gff_struct *gff_read(const uint8_t *d, size_t len, char ftype[5], char *err,
                     size_t errlen)
{
    rd r;
    gff_struct *root = NULL;
    uint32_t h[12];
    int i;

    memset(&r, 0, sizeof r);
    r.d = d;
    r.n = len;
    r.err = err;
    r.errlen = errlen;
    if (len < HDR_SIZE) {
        fail(&r, "file too small for a GFF header");
        return NULL;
    }
    if (memcmp(d + 4, "V3.2", 4) != 0) {
        fail(&r, "not a GFF V3.2 file");
        return NULL;
    }
    if (ftype) {
        memcpy(ftype, d, 4);
        ftype[4] = '\0';
    }
    for (i = 0; i < 12; i++)
        h[i] = get_u32(d + 8 + i * 4);
    r.so = h[0];  r.sc = h[1];
    r.fo = h[2];  r.fc = h[3];
    r.lo = h[4];  r.lc = h[5];
    r.fdo = h[6]; r.fdn = h[7];
    r.fio = h[8]; r.fin = h[9];
    r.lio = h[10]; r.lin = h[11];

    if (!in_range(&r, r.so, (uint64_t)r.sc * 12) ||
        !in_range(&r, r.fo, (uint64_t)r.fc * 12) ||
        !in_range(&r, r.lo, (uint64_t)r.lc * 16) ||
        !in_range(&r, r.fdo, r.fdn) || !in_range(&r, r.fio, r.fin) ||
        !in_range(&r, r.lio, r.lin)) {
        fail(&r, "section table out of range");
        return NULL;
    }
    if (r.sc == 0) {
        fail(&r, "no root struct");
        return NULL;
    }
    if (read_struct(&r, 0, 0, &root) != 0) {
        gff_free(root);
        return NULL;
    }
    return root;
}

/* ---- writer ------------------------------------------------------------ */

typedef struct {
    buf structs, fields, labels, fdata, findex, lindex;
    uint32_t nstructs, nfields, nlabels;
} wr;

static uint32_t label_index(wr *w, const char *label)
{
    uint32_t i;
    char key[16];

    memset(key, 0, sizeof key);
    memcpy(key, label, strnlen(label, 16));
    for (i = 0; i < w->nlabels; i++)
        if (memcmp(w->labels.p + (size_t)i * 16, key, 16) == 0)
            return i;
    buf_put(&w->labels, key, 16);
    return w->nlabels++;
}

static uint32_t write_struct(wr *w, const gff_struct *s);

static void write_field(wr *w, const gff_field *f, uint32_t fidx)
{
    uint8_t *slot;
    uint32_t data = 0, i;

    switch (f->type) {
    case GFF_BYTE: case GFF_WORD: case GFF_DWORD:
        data = (uint32_t)f->v.u;
        if (f->type == GFF_BYTE) data &= 0xff;
        if (f->type == GFF_WORD) data &= 0xffff;
        break;
    case GFF_CHAR: case GFF_SHORT: case GFF_INT:
        data = (uint32_t)f->v.i;
        if (f->type == GFF_CHAR) data &= 0xff;
        if (f->type == GFF_SHORT) data &= 0xffff;
        break;
    case GFF_FLOAT: {
        float fl = (float)f->v.d;
        memcpy(&data, &fl, 4);
        break;
    }
    case GFF_DWORD64:
        data = (uint32_t)w->fdata.n;
        buf_u64(&w->fdata, f->v.u);
        break;
    case GFF_INT64:
        data = (uint32_t)w->fdata.n;
        buf_u64(&w->fdata, (uint64_t)f->v.i);
        break;
    case GFF_DOUBLE: {
        uint64_t bits;
        memcpy(&bits, &f->v.d, 8);
        data = (uint32_t)w->fdata.n;
        buf_u64(&w->fdata, bits);
        break;
    }
    case GFF_EXOSTR: {
        size_t n = strlen(f->v.s);
        data = (uint32_t)w->fdata.n;
        buf_u32(&w->fdata, (uint32_t)n);
        buf_put(&w->fdata, f->v.s, n);
        break;
    }
    case GFF_RESREF: {
        size_t n = strlen(f->v.s);
        if (n > 255)
            n = 255; /* NWN1 callers must keep to 16; NWN2 allows 32 */
        data = (uint32_t)w->fdata.n;
        buf_u8(&w->fdata, (uint32_t)n);
        buf_put(&w->fdata, f->v.s, n);
        break;
    }
    case GFF_LOCSTR: {
        uint32_t total = 8;
        data = (uint32_t)w->fdata.n;
        for (i = 0; i < f->v.loc.n; i++)
            total += 8 + (uint32_t)strlen(f->v.loc.e[i].text);
        buf_u32(&w->fdata, total);
        buf_u32(&w->fdata, f->v.loc.strref);
        buf_u32(&w->fdata, (uint32_t)f->v.loc.n);
        for (i = 0; i < f->v.loc.n; i++) {
            size_t n = strlen(f->v.loc.e[i].text);
            buf_u32(&w->fdata, f->v.loc.e[i].id);
            buf_u32(&w->fdata, (uint32_t)n);
            buf_put(&w->fdata, f->v.loc.e[i].text, n);
        }
        break;
    }
    case GFF_VOID:
        data = (uint32_t)w->fdata.n;
        buf_u32(&w->fdata, f->v.raw.n);
        buf_put(&w->fdata, f->v.raw.p, f->v.raw.n);
        break;
    case GFF_ORIENT:
    case GFF_VECTOR:
        data = (uint32_t)w->fdata.n;
        buf_put(&w->fdata, f->v.raw.p, f->v.raw.n);
        break;
    case GFF_STRUCT:
        data = write_struct(w, f->v.st);
        break;
    case GFF_LIST: {
        uint32_t *idx = xmalloc((f->v.list.n + 1) * sizeof *idx);
        for (i = 0; i < f->v.list.n; i++)
            idx[i] = write_struct(w, f->v.list.v[i]);
        data = (uint32_t)w->lindex.n;
        buf_u32(&w->lindex, (uint32_t)f->v.list.n);
        for (i = 0; i < f->v.list.n; i++)
            buf_u32(&w->lindex, idx[i]);
        free(idx);
        break;
    }
    }

    slot = w->fields.p + (size_t)fidx * 12;
    set_u32(slot, (uint32_t)f->type);
    set_u32(slot + 4, label_index(w, f->label));
    set_u32(slot + 8, data);
}

static uint32_t write_struct(wr *w, const gff_struct *s)
{
    uint32_t idx = w->nstructs++, first = w->nfields, i;
    uint32_t dof;

    buf_zero(&w->structs, 12);
    /* Reserve this struct's fields contiguously before recursing so the
     * field-index list for multi-field structs is a simple run. */
    w->nfields += (uint32_t)s->n;
    buf_zero(&w->fields, s->n * 12);

    if (s->n == 1) {
        dof = first;
    } else if (s->n > 1) {
        dof = (uint32_t)w->findex.n;
        for (i = 0; i < s->n; i++)
            buf_u32(&w->findex, first + i);
    } else {
        dof = 0xffffffffu;
    }
    for (i = 0; i < s->n; i++)
        write_field(w, s->f[i], first + i);

    set_u32(w->structs.p + (size_t)idx * 12, s->type);
    set_u32(w->structs.p + (size_t)idx * 12 + 4, dof);
    set_u32(w->structs.p + (size_t)idx * 12 + 8, (uint32_t)s->n);
    return idx;
}

void gff_write(const gff_struct *root, const char *ftype, buf *out)
{
    wr w;
    uint32_t off = HDR_SIZE;

    memset(&w, 0, sizeof w);
    write_struct(&w, root);

    buf_put(out, ftype, 4);
    buf_put(out, "V3.2", 4);
    buf_u32(out, off);                       /* structs */
    buf_u32(out, w.nstructs);
    off += (uint32_t)w.structs.n;
    buf_u32(out, off);                       /* fields */
    buf_u32(out, w.nfields);
    off += (uint32_t)w.fields.n;
    buf_u32(out, off);                       /* labels */
    buf_u32(out, w.nlabels);
    off += (uint32_t)w.labels.n;
    buf_u32(out, off);                       /* field data */
    buf_u32(out, (uint32_t)w.fdata.n);
    off += (uint32_t)w.fdata.n;
    buf_u32(out, off);                       /* field indices */
    buf_u32(out, (uint32_t)w.findex.n);
    off += (uint32_t)w.findex.n;
    buf_u32(out, off);                       /* list indices */
    buf_u32(out, (uint32_t)w.lindex.n);

    buf_put(out, w.structs.p, w.structs.n);
    buf_put(out, w.fields.p, w.fields.n);
    buf_put(out, w.labels.p, w.labels.n);
    buf_put(out, w.fdata.p, w.fdata.n);
    buf_put(out, w.findex.p, w.findex.n);
    buf_put(out, w.lindex.p, w.lindex.n);

    free(w.structs.p); free(w.fields.p); free(w.labels.p);
    free(w.fdata.p); free(w.findex.p); free(w.lindex.p);
}

/* ---- dump -------------------------------------------------------------- */

static const char *type_name(int t)
{
    static const char *names[] = {
        "BYTE", "CHAR", "WORD", "SHORT", "DWORD", "INT", "DWORD64", "INT64",
        "FLOAT", "DOUBLE", "CExoString", "CResRef", "CExoLocString", "VOID",
        "Struct", "List", "Orientation", "Vector"
    };
    return t >= 0 && t < 18 ? names[t] : "???";
}

void gff_dump(FILE *fp, const gff_struct *s, int indent)
{
    size_t i, j;

    for (i = 0; i < s->n; i++) {
        const gff_field *f = s->f[i];

        fprintf(fp, "%*s%s [%s] ", indent, "", f->label, type_name(f->type));
        switch (f->type) {
        case GFF_BYTE: case GFF_WORD: case GFF_DWORD: case GFF_DWORD64:
            fprintf(fp, "%" PRIu64 "\n", f->v.u);
            break;
        case GFF_CHAR: case GFF_SHORT: case GFF_INT: case GFF_INT64:
            fprintf(fp, "%" PRId64 "\n", f->v.i);
            break;
        case GFF_FLOAT: case GFF_DOUBLE:
            fprintf(fp, "%.9g\n", f->v.d);
            break;
        case GFF_EXOSTR: case GFF_RESREF:
            fprintf(fp, "\"%s\"\n", f->v.s);
            break;
        case GFF_LOCSTR:
            fprintf(fp, "strref=%" PRIu32, f->v.loc.strref);
            for (j = 0; j < f->v.loc.n; j++)
                fprintf(fp, " [%" PRIu32 "]=\"%s\"", f->v.loc.e[j].id,
                        f->v.loc.e[j].text);
            fputc('\n', fp);
            break;
        case GFF_VOID: case GFF_ORIENT: case GFF_VECTOR:
            fprintf(fp, "%" PRIu32 " bytes\n", f->v.raw.n);
            break;
        case GFF_STRUCT:
            fprintf(fp, "(id %" PRIu32 ")\n", f->v.st->type);
            gff_dump(fp, f->v.st, indent + 2);
            break;
        case GFF_LIST:
            fprintf(fp, "(%zu)\n", f->v.list.n);
            for (j = 0; j < f->v.list.n; j++) {
                fprintf(fp, "%*s[%zu] (id %" PRIu32 ")\n", indent + 2, "", j,
                        f->v.list.v[j]->type);
                gff_dump(fp, f->v.list.v[j], indent + 4);
            }
            break;
        }
    }
}
