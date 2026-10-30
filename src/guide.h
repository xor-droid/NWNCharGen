#ifndef GUIDE_H
#define GUIDE_H

#include <stdio.h>

#include "chr.h"
#include "plan.h"
#include "tlk.h"
#include "util.h"

/* The guided builder: a line-oriented dialogue on stdin/stdout that lets a
 * person decide any choice of the build and leaves the rest to the engine.
 * Pressing Enter at a prompt does exactly what the random builder would do at
 * that point, so a session of nothing but Enters yields the same file as the
 * same seed without --guided. Answers are logged so a session can be replayed
 * (the log read back as stdin, with the same options and seed). */

/* What the command line or spec file already decided: not asked again. The
 * caller also preloads g->cls / g->ncl (classes) and g->level for the ones it
 * has, so the questions that follow can use them. */
typedef struct {
    int race, level, align, gender, name, hp, variety;
    int abil[NABIL];            /* nonzero: that score was given */
} guide_given;

typedef struct guide {
    rules *ru;
    const tlk *t;
    uint64_t seed;
    FILE *in, *out;

    /* setup answers; empty or -1 leaves the choice to the build */
    char race[64], cls[64], align[8], gender[4], name[128];
    int fixed[NABIL];
    int level;                  /* 0: not asked */
    int ncl, cl[MAX_CLASSES], cn[MAX_CLASSES];
    int max_classes;            /* >1: random mix of up to that many classes */
    int per_level;              /* the class is chosen at each level-up */
    int hm, have_hm, variety, have_variety;

    /* session state */
    int quit, auto_level, auto_all, step_auto, skills_done;
    buf log;                    /* the lines answered, for replay */
} guide;

void guide_open(guide *g, rules *ru, const tlk *t, uint64_t seed, FILE *in, FILE *out);
void guide_close(guide *g);

/* Asks the setup questions (race, level, classes, alignment, gender, scores,
 * name, settings) that given does not cover. 0, or -1 if the person quit. */
int guide_setup(guide *g, const guide_given *given);

/* Points the plan's per-level hooks at this guide. */
void guide_attach(guide *g, lvl_plan *pl);

/* Familiar and companion once the levels are done. 0, or -1 if quit. */
int guide_finish(guide *g, chr *c);

/* Shows the finished character, the seed and any warnings (one per line, may
 * be NULL), and asks whether to write it: 1 yes, 0 no, -1 quit. */
int guide_review(guide *g, const chr *c, const char *warning);

/* Writes the answer log, headed by a comment with the command to replay it. */
int guide_save(const guide *g, const char *path, const char *command);

#endif
