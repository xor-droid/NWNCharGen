#ifndef TWODA_H
#define TWODA_H

#include <stddef.h>

typedef struct {
    int ncols, nrows;
    char **cols;      /* column names, as written */
    char ***cells;    /* cells[row][col]; NULL for "****" */
    char *deflt;      /* DEFAULT: value, or NULL */
} twoda;

twoda *twoda_parse(const char *text, size_t len);
void twoda_free(twoda *t);
int twoda_col(const twoda *t, const char *name); /* case-insensitive, -1 if absent */
const char *twoda_get(const twoda *t, int row, const char *col);
const char *twoda_geti_s(const twoda *t, int row, int col);
int twoda_int(const twoda *t, int row, const char *col, int def);

#endif
