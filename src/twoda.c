#define _POSIX_C_SOURCE 200809L
#include "twoda.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"

/* Split one line into whitespace-separated tokens; "quoted" tokens may hold
 * spaces. Returns the token count; tokens are malloc'd. */
static int tokenize(const char *p, const char *end, char ***out)
{
    char **v = NULL;
    int n = 0, cap = 0;

    while (p < end) {
        const char *s;
        char *tok;

        while (p < end && isspace((unsigned char)*p))
            p++;
        if (p >= end)
            break;
        if (*p == '"') {
            s = ++p;
            while (p < end && *p != '"')
                p++;
            tok = xstrndup(s, (size_t)(p - s));
            if (p < end)
                p++;
        } else {
            s = p;
            while (p < end && !isspace((unsigned char)*p))
                p++;
            tok = xstrndup(s, (size_t)(p - s));
        }
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            v = xrealloc(v, (size_t)cap * sizeof *v);
        }
        v[n++] = tok;
    }
    *out = v;
    return n;
}

static void free_tokens(char **v, int n)
{
    int i;
    for (i = 0; i < n; i++)
        free(v[i]);
    free(v);
}

twoda *twoda_parse(const char *text, size_t len)
{
    const char *p = text, *end = text + len;
    twoda *t = xcalloc(1, sizeof *t);
    int rowcap = 0, have_cols = 0, first = 1;

    while (p < end) {
        const char *e = memchr(p, '\n', (size_t)(end - p));
        char **tok;
        int n, i;

        if (!e)
            e = end;
        n = tokenize(p, e, &tok);
        p = e < end ? e + 1 : end;
        if (n == 0) {
            free(tok);
            continue;
        }
        if (first) {
            first = 0;
            if (strcmp(tok[0], "2DA") != 0) {
                free_tokens(tok, n);
                twoda_free(t);
                return NULL;
            }
            free_tokens(tok, n);
            continue;
        }
        if (!have_cols) {
            if (strcasecmp(tok[0], "DEFAULT:") == 0) {
                if (n > 1)
                    t->deflt = xstrdup(tok[1]);
                free_tokens(tok, n);
                continue;
            }
            t->cols = tok;
            t->ncols = n;
            have_cols = 1;
            continue;
        }
        /* data row: first token is the row label */
        if (t->nrows == rowcap) {
            rowcap = rowcap ? rowcap * 2 : 64;
            t->cells = xrealloc(t->cells, (size_t)rowcap * sizeof *t->cells);
        }
        t->cells[t->nrows] = xcalloc((size_t)t->ncols, sizeof(char *));
        for (i = 0; i < t->ncols && i + 1 < n; i++) {
            if (strcmp(tok[i + 1], "****") != 0)
                t->cells[t->nrows][i] = tok[i + 1];
            else
                free(tok[i + 1]);
            tok[i + 1] = NULL;
        }
        t->nrows++;
        free_tokens(tok, n);
    }
    if (!have_cols) {
        twoda_free(t);
        return NULL;
    }
    return t;
}

void twoda_free(twoda *t)
{
    int r, c;

    if (!t)
        return;
    for (r = 0; r < t->nrows; r++) {
        for (c = 0; c < t->ncols; c++)
            free(t->cells[r][c]);
        free(t->cells[r]);
    }
    free(t->cells);
    free_tokens(t->cols, t->ncols);
    free(t->deflt);
    free(t);
}

int twoda_col(const twoda *t, const char *name)
{
    int c;

    for (c = 0; c < t->ncols; c++)
        if (strcasecmp(t->cols[c], name) == 0)
            return c;
    return -1;
}

const char *twoda_geti_s(const twoda *t, int row, int col)
{
    if (row < 0 || row >= t->nrows || col < 0 || col >= t->ncols)
        return NULL;
    return t->cells[row][col];
}

const char *twoda_get(const twoda *t, int row, const char *col)
{
    return twoda_geti_s(t, row, twoda_col(t, col));
}

int twoda_int(const twoda *t, int row, const char *col, int def)
{
    const char *s = twoda_get(t, row, col);
    char *e;
    long v;

    if (!s)
        return def;
    v = strtol(s, &e, (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10);
    return e == s ? def : (int)v;
}
