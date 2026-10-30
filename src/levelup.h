#ifndef LEVELUP_H
#define LEVELUP_H

#include "chr.h"
#include "pick.h"
#include "rng.h"

typedef enum { HP_MAX, HP_AVG, HP_ROLL } hp_mode;

/* Choices still owed for the level being built. */
typedef struct {
    int skill_pts;            /* points that can be spent now */
    int general, bonus;       /* selectable feat slots left */
    int abil_due;             /* an ability increase is owed */
    int spells[NSPLVL];       /* spells still to learn, by spell level */
    int need_domains;         /* cleric: two domains owed */
    int need_school;          /* wizard: school owed */
    int wiz_any;              /* wizard: spellbook picks of any castable level */
} pending;

/* Appends a level in class cls: rolls HP, grants the free feats and works
 * out what is still owed. 0 on success, -1 with err filled otherwise. */
int lu_begin(rules *ru, chr *c, int cls, hp_mode hm, rng *g, pending *p,
             char *err, size_t errlen);

/* Individual choices. Each returns 0 or -1 with err filled. */
int lu_pick_ability(rules *ru, chr *c, pending *p, int a, char *err, size_t n);
int lu_buy_skill(rules *ru, chr *c, pending *p, int sk, int ranks, char *err,
                 size_t n);
int lu_pick_feat(rules *ru, chr *c, pending *p, int feat, int bonus, char *err,
                 size_t n);
int lu_pick_spell(rules *ru, chr *c, pending *p, int spell_level, int spell,
                  char *err, size_t n);
int lu_pick_domains(rules *ru, chr *c, pending *p, int d1, int d2, char *err,
                    size_t n);
int lu_pick_school(rules *ru, chr *c, pending *p, int school, char *err,
                   size_t n);

/* Fills every choice still owed from the package's preference lists; o->variety
 * 0..3 loosens the choices and o pins or avoids feats (see pick.h). */
void lu_autofill(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o);

/* The same choices one step at a time, for front ends that let the user decide
 * some of them: each fills what is owed from the package lists exactly as
 * lu_autofill does, which calls them in this order. The single-pick ones return
 * 0 when they made a pick and -1 when nothing more can be picked now. */
void lu_auto_domains(rules *ru, chr *c, pending *p, rng *g);
void lu_auto_school(rules *ru, chr *c, pending *p);
void lu_auto_ability(rules *ru, chr *c, pending *p);
int lu_auto_feat(rules *ru, chr *c, pending *p, int bonus, rng *g, const lu_opts *o);
void lu_auto_feats(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o);
/* A specialist wizard needs a school spell at every castable spell level. */
int lu_school_due(rules *ru, const chr *c, const pending *p, int sl);
int lu_auto_school_spell(rules *ru, chr *c, pending *p, int sl, rng *g,
                         const lu_opts *o);
int lu_auto_spell(rules *ru, chr *c, pending *p, int sl, rng *g, const lu_opts *o);
int lu_auto_wiz_any(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o);
void lu_auto_spells(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o);
int lu_auto_skill(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o);
void lu_auto_skills(rules *ru, chr *c, pending *p, rng *g, const lu_opts *o);

/* Spell level of a spell on the class's list, -1 if it is not on it. */
int lu_spell_level(rules *ru, int cls, int spell);

/* The preference list a packages.2da column names: which 0 is the
 * character's package, 1 the class's default package. NULL if none. */
const twoda *lu_pref_tab(rules *ru, const chr *c, int cls, const char *col,
                         int which);

/* Closes the level: unspent skill points carry over. */
void lu_end(chr *c, const pending *p);

/* Queries for front ends. Each fills out[] (capacity cap) and returns the
 * number of entries. */
int lu_feat_pool(rules *ru, const chr *c, int bonus, int *out, int cap);
int lu_spell_pool(rules *ru, const chr *c, int cls, int spell_level, int *out,
                  int cap);
int feat_selectable(rules *ru, const chr *c, int cls, int feat, int bonus);

/* Spell list column of spells.2da for a class ("Wiz_Sorc", "Bard", ...). */
const char *spell_list_column(rules *ru, int cls);
/* Whether the class's alignment restriction allows (good/evil, law/chaos). */
int chr_align_ok(rules *ru, int cls, int ge, int lc);

/* Highest spell level the class can currently cast (-1 if none). */
int chr_max_spell_level(rules *ru, const chr *c);

#endif
