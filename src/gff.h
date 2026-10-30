#ifndef GFF_H
#define GFF_H

#include <stdint.h>
#include <stdio.h>

#include "util.h"

/* GFF V3.2 field types, numbered as in the file format. */
enum {
    GFF_BYTE, GFF_CHAR, GFF_WORD, GFF_SHORT, GFF_DWORD, GFF_INT,
    GFF_DWORD64, GFF_INT64, GFF_FLOAT, GFF_DOUBLE, GFF_EXOSTR, GFF_RESREF,
    GFF_LOCSTR, GFF_VOID, GFF_STRUCT, GFF_LIST, GFF_ORIENT, GFF_VECTOR
};

typedef struct gff_struct gff_struct;

typedef struct {
    uint32_t id; /* language * 2 + gender */
    char *text;
} gff_locent;

typedef struct {
    char label[17];
    int type;
    union {
        uint64_t u;  /* BYTE, WORD, DWORD, DWORD64 */
        int64_t i;   /* CHAR, SHORT, INT, INT64 */
        double d;    /* FLOAT, DOUBLE */
        char *s;     /* EXOSTR, RESREF */
        struct {
            uint32_t strref;
            gff_locent *e;
            size_t n;
        } loc;
        struct {
            uint8_t *p;
            uint32_t n;
        } raw;       /* VOID, ORIENT, VECTOR */
        gff_struct *st;
        struct {
            gff_struct **v;
            size_t n, cap;
        } list;
    } v;
} gff_field;

struct gff_struct {
    uint32_t type;
    gff_field **f;
    size_t n, cap;
};

/* Parse a GFF image. On failure returns NULL and fills err. ftype receives
 * the 4-char file type ("BIC ", "UTC ", ...) and may be NULL. */
gff_struct *gff_read(const uint8_t *d, size_t len, char ftype[5], char *err,
                     size_t errlen);

/* Serialise a tree; the result is appended to out. */
void gff_write(const gff_struct *root, const char *ftype, buf *out);

void gff_free(gff_struct *s);
void gff_dump(FILE *fp, const gff_struct *s, int indent);

/* Construction. Setters replace an existing field with the same label. */
gff_struct *gff_new(uint32_t type);
void gff_set_uint(gff_struct *s, const char *label, int type, uint64_t v);
void gff_set_int(gff_struct *s, const char *label, int type, int64_t v);
void gff_set_float(gff_struct *s, const char *label, double v);
void gff_set_str(gff_struct *s, const char *label, int type, const char *v);
void gff_set_locstr(gff_struct *s, const char *label, uint32_t strref,
                    const char *english);
void gff_set_void(gff_struct *s, const char *label, const void *p, uint32_t n);
void gff_set_struct(gff_struct *s, const char *label, gff_struct *child);
void gff_list_init(gff_struct *s, const char *label);
void gff_list_add(gff_struct *s, const char *label, gff_struct *child);

/* Deep copy of a struct; copy of one field into a struct (appended, or
 * replacing a field of the same label); removal of a field by label. */
gff_struct *gff_clone(const gff_struct *s);
void gff_put_copy(gff_struct *dst, const gff_field *f);
void gff_remove(gff_struct *s, const char *label);

/* Lookup. */
gff_field *gff_get(const gff_struct *s, const char *label);
int64_t gff_get_int(const gff_struct *s, const char *label, int64_t def);
const char *gff_get_str(const gff_struct *s, const char *label);

#endif
