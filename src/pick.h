#ifndef PICK_H
#define PICK_H

#include "chr.h"
#include "rng.h"

#define VARIETY_MAX 3
#define MAX_PINS    48

/* How the autofill chooses. variety 0 follows the package lists exactly;
 * higher values weigh more of each list. Pinned feats are taken at the first
 * slot where they (or a missing prerequisite) are legal, from character
 * level `from` on; avoided feats are never picked unprompted. */
typedef struct {
    int variety;
    int npins;
    struct { int feat, from; } pin[MAX_PINS];
    int navoid;
    int avoid[MAX_PINS];
} lu_opts;

/* Chooses a feat for an open slot of class cls from the package's preference
 * list: the next few legal entries are weighed by list position and by how
 * well each suits the character, weapon feats stay on one weapon (one melee,
 * one ranged) the character is proficient with. Returns -1 when the lists
 * offer nothing legal. */
int pick_feat(rules *ru, const chr *c, int cls, int bonus, const lu_opts *o, rng *g);

/* The next pinned feat (or a prerequisite of one) that can be taken in this
 * slot now, or -1. */
int pick_pinned(rules *ru, const chr *c, int cls, int bonus, const lu_opts *o);

int pick_avoided(const lu_opts *o, int feat);

/* Shared weighting: how many list entries are in play, the weight of the
 * entry at a position, and a weighted draw (index into w[0..n-1]). */
int pick_window(int variety);
int pick_pos_weight(int pos);
int pick_weighted(const int *w, int n, rng *g);

#endif
