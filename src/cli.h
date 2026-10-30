#ifndef CLI_H
#define CLI_H

#include <stddef.h>

#include "chr.h"
#include "tlk.h"

/* Helpers shared by the command-line front ends (bicgen and its guided mode). */

/* Case- and punctuation-insensitive comparison / substring test. */
int cli_alnum_eq(const char *a, const char *b);
int cli_alnum_contains(const char *hay, const char *needle);

/* A row of a Label-keyed 2DA by number or label; with playable_col >= 0 only
 * PlayerClass == 1 rows count. -1 if none. */
int cli_find_row(const twoda *t, const char *s, int playable_col);

/* "LG", "N", "CE" ... -> raw good/evil and lawful/chaotic values. 0 on success. */
int cli_parse_align(const char *s, int *ge, int *lc);
const char *cli_align_name(int ge, int lc);

/* Point buy: cost of a score, total of six scores, the class-recommended array,
 * and the allocator. fixed[a] >= 0 keeps that score, the rest are spent toward
 * the recommended values (and shuffled when randomize and nothing is fixed).
 * 0 on success, -1 over budget, -2 a score outside 8..18. */
int cli_pb_cost(int s);
int cli_pb_total(const int *a);
void cli_recommended(rules *ru, int cls, int *out);
int cli_allocate(rules *ru, int cls, int race, const int *fixed, int randomize,
                 rng *g, int *out);

/* "fighter:5,wizard:3" -> class rows and level counts (at most MAX_CLASSES
 * distinct base classes). 0 on success. */
int cli_parse_classes(rules *ru, const char *s, int *cl, int *cn, int *n);

/* Output file stem from the character's name; whether a path exists. */
void cli_file_name(const chr *c, char *out, size_t n);
int cli_exists(const char *p);

/* In-game text of a 2DA row's strref column (the Label if the TLK lacks it). */
const char *cli_tname(const tlk *t, const twoda *tab, int row, const char *col,
                      char *buf, size_t n);

void cli_sheet(rules *ru, const tlk *t, const chr *c);

/* A feat by number, label or in-game name; -1 if unknown. */
int cli_find_feat(rules *ru, const tlk *t, const char *s);
void cli_list_feats(rules *ru, const tlk *t, const char *text);

#endif
