#define _POSIX_C_SOURCE 200809L
#include "ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static char *trim(char *s)
{
    char *e;

    while (isspace((unsigned char)*s))
        s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return s;
}

static char *dup(const char *s)
{
    char *d = xmalloc(strlen(s) + 1);

    strcpy(d, s);
    return d;
}

int ini_parse(const char *text, size_t len, ini *out, char *err, size_t errlen)
{
    char *copy = xmalloc(len + 1), *line, *save = NULL, section[64] = "";
    int ln = 0, cap = 0;

    memcpy(copy, text, len);
    copy[len] = '\0';
    out->e = NULL;
    out->n = 0;
    for (line = copy; line; line = save) {
        char *nl = strchr(line, '\n'), *p, *eq, *c;

        save = nl ? nl + 1 : NULL;
        if (nl)
            *nl = '\0';
        ln++;
        for (c = line; *c; c++)       /* a trailing comment needs leading space */
            if ((*c == ';' || *c == '#') && (c == line || isspace((unsigned char)c[-1]))) {
                *c = '\0';
                break;
            }
        p = trim(line);
        if (!*p)
            continue;
        if (*p == '[') {
            char *close = strchr(p, ']');
            size_t i;

            if (!close || close[1] || close - p - 1 >= (long)sizeof section) {
                snprintf(err, errlen, "line %d: bad section header", ln);
                goto fail;
            }
            *close = '\0';
            snprintf(section, sizeof section, "%s", trim(p + 1));
            for (i = 0; section[i]; i++)
                section[i] = (char)tolower((unsigned char)section[i]);
            continue;
        }
        eq = strchr(p, '=');
        if (!eq || eq == p) {
            snprintf(err, errlen, "line %d: expected key = value", ln);
            goto fail;
        }
        *eq = '\0';
        if (out->n == cap) {
            cap = cap ? cap * 2 : 32;
            out->e = realloc(out->e, (size_t)cap * sizeof *out->e);
            if (!out->e)
                abort();
        }
        {
            ini_entry *e = &out->e[out->n++];
            char *k = trim(p), *v = trim(eq + 1);
            size_t i;

            e->section = dup(section);
            e->key = dup(k);
            for (i = 0; e->key[i]; i++)
                e->key[i] = (char)tolower((unsigned char)e->key[i]);
            e->val = dup(v);
            e->line = ln;
        }
    }
    free(copy);
    return 0;
fail:
    free(copy);
    ini_free(out);
    return -1;
}

void ini_free(ini *f)
{
    int i;

    for (i = 0; i < f->n; i++) {
        free(f->e[i].section);
        free(f->e[i].key);
        free(f->e[i].val);
    }
    free(f->e);
    f->e = NULL;
    f->n = 0;
}
