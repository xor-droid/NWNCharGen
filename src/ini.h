#ifndef INI_H
#define INI_H

#include <stddef.h>

/* A parsed INI file: [section] headers, key = value lines, ';' or '#'
 * comments (whole-line, or after whitespace). Sections and keys are lower
 * cased; keys before any header belong to section "". */
typedef struct {
    char *section, *key, *val;
    int line;
} ini_entry;

typedef struct {
    ini_entry *e;
    int n;
} ini;

/* 0 on success, -1 with err filled ("line N: ..."). */
int ini_parse(const char *text, size_t len, ini *out, char *err, size_t errlen);
void ini_free(ini *f);

#endif
