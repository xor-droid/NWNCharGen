#ifndef PLAN_H
#define PLAN_H

#include "levelup.h"

#define PLAN_SCHOOL_NONE   (-1)   /* leave the choice to autofill (generalist) */
#define PLAN_SCHOOL_RANDOM (-2)
#define MAX_CLASSES 3             /* the game allows three classes per character */

/* The class taken at each character level, plus how HP are decided after
 * level 1 (level 1 is always maximum). Shared by every front end. */
typedef struct {
    int n;
    int seq[MAX_LEVELS];
    hp_mode hm;
    int school;               /* wizard specialty for the first wizard level */
    lu_opts opts;             /* variety and pinned/avoided feats */

    /* Optional hooks for front ends that let a person decide (NULL = random).
     * pick_class returns the class for level index li (0 is never asked; dflt
     * is seq[li]). fill replaces lu_autofill for the level just begun and
     * returns nonzero to stop the build. */
    int (*pick_class)(rules *ru, const chr *c, int li, int dflt, void *ud);
    int (*fill)(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o, void *ud);
    void *ud;
} lvl_plan;

/* Fills seq with `levels` levels of a single class. */
void plan_single(lvl_plan *pl, int cls, int levels);

/* Random multiclass plan: between 1 and max_classes classes (never more than
 * the level count), first class fixed, the others drawn from classes the
 * alignment allows. Each class gets at least one level. */
void plan_random(rules *ru, const chr *c, lvl_plan *pl, int first, int levels,
                 int max_classes, rng *g);

/* Appends every level of the plan to c (which must hold no levels yet) and
 * fills each with random legal choices. 0 on success, -1 with err filled. */
int plan_run(rules *ru, chr *c, const lvl_plan *pl, rng *g, char *err, size_t n);

/* Starting gold, familiar and animal companion for the finished build. */
void plan_finish(rules *ru, chr *c, rng *g);

#endif
