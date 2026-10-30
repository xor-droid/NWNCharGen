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

/* ---- prestige classes ---- */

/* One row of a class's cls_pres_*.2da. type: 'S' skill a with b ranks, 'F' feat a,
 * 'O' feat a (any one of these), 'R' race a (any one of these), 'B' base attack
 * a, 'A' arcane spell level a, 'P' spell level a of any kind, 'C' class a (any
 * one of these). Rows the build cannot or need not test (script variables) are
 * left out. */
typedef struct { char type; int a, b; } lu_req;
#define LU_MAX_REQS 48

/* Whether the generator models this prestige class (rules for what it adds to
 * casting or choices are in place). Other classes are refused. */
int lu_prestige_supported(rules *ru, int cls);
/* The entry requirements of a prestige class; 0 for a base class. */
int lu_prestige_reqs(rules *ru, int cls, lu_req *out, int cap);
/* Whether c, as it stands, may take a first level of cls: 1 yes, 0 no with the
 * first unmet requirement in why. */
int lu_prestige_ok(rules *ru, const chr *c, int cls, char *why, size_t n);
/* Raises need[] (NABIL entries) to the scores the feat and the feats it follows
 * from ask for. */
void lu_feat_abil_need(rules *ru, int feat, int *need);
/* Why a feat cannot be taken yet (a missing score, base attack bonus or
 * prerequisite feat), for messages; empty if nothing is missing. */
void lu_feat_block(rules *ru, const chr *c, int feat, char *why, size_t n);

/* Spell list column of spells.2da for a class ("Wiz_Sorc", "Bard", ...). */
const char *spell_list_column(rules *ru, int cls);
/* Whether the class's alignment restriction allows (good/evil, law/chaos). */
int chr_align_ok(rules *ru, int cls, int ge, int lc);

/* Highest spell level the character can currently cast (-1 if none), and the
 * same for one class's own levels. */
int chr_max_spell_level(rules *ru, const chr *c);
/* The same counting only classes that cast arcane spells. */
int chr_max_arcane_spell_level(rules *ru, const chr *c);
int lu_class_max_spell_level(rules *ru, const chr *c, int cls);

#endif
