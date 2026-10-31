#ifndef LOOK_H
#define LOOK_H

#include <stdio.h>

#include "chr.h"
#include "tlk.h"
#include "rng.h"

/* Fills in the cosmetic fields (appearance, head, portrait, sound set,
 * colours, body parts, age) with values the game can display for the
 * character's race and gender. Head numbers come from the models that
 * actually exist in the install. */
void look_randomize(rules *ru, chr *c, rng *g);

/* Chooses one aspect of the look, after look_randomize has run (so the random
 * stream is the same whatever is chosen). key is head, portrait, voice, hair,
 * skin, tattoo1 or tattoo2; the value is checked against what the install has
 * for the character's race and gender. 0, or -1 with the reason in err. */
int look_set(rules *ru, const tlk *t, chr *c, const char *key, const char *val,
             char *err, size_t n);
int look_is_key(const char *key);

/* Prints the choices for "heads", "portraits" or "voices"; look_show prints
 * the character's current ones on one line. */
void look_list(rules *ru, const tlk *t, FILE *fp, const char *what, int race, int gender);
void look_show(rules *ru, const tlk *t, FILE *fp, const chr *c);

/* A plausible name for the race and gender. */
void name_random(int race, int gender, rng *g, char *first, size_t nf,
                 char *last, size_t nl);
void familiar_name_random(rng *g, char *out, size_t n);

#endif
