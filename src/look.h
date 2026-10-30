#ifndef LOOK_H
#define LOOK_H

#include "chr.h"
#include "rng.h"

/* Fills in the cosmetic fields (appearance, head, portrait, sound set,
 * colours, body parts, age) with values the game can display for the
 * character's race and gender. Head numbers come from the models that
 * actually exist in the install. */
void look_randomize(rules *ru, chr *c, rng *g);

/* A plausible name for the race and gender. */
void name_random(int race, int gender, rng *g, char *first, size_t nf,
                 char *last, size_t nl);
void familiar_name_random(rng *g, char *out, size_t n);

#endif
