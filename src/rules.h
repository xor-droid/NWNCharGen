#ifndef RULES_H
#define RULES_H

#include "res.h"
#include "twoda.h"

/* Ability order used everywhere in the code (not the GFF field order). */
enum { ABIL_STR, ABIL_DEX, ABIL_CON, ABIL_INT, ABIL_WIS, ABIL_CHA, NABIL };

typedef struct rules rules;

/* Loads the core tables (classes, racialtypes, feat, skills, ...) through r.
 * The res handle must outlive the rules object. NULL + err on failure. */
rules *rules_open(res *r, char *err, size_t errlen);
void rules_close(rules *ru);

/* Lazily loaded, cached 2DA by name; NULL when absent. Do not free. */
const twoda *rules_tab(rules *ru, const char *name);

const twoda *rules_classes(rules *ru);
const twoda *rules_races(rules *ru);
const twoda *rules_feats(rules *ru);
const twoda *rules_skills(rules *ru);

/* A table named by a column of classes.2da / racialtypes.2da for a row. */
const twoda *rules_class_tab(rules *ru, int cls, const char *col);
const twoda *rules_race_tab(rules *ru, int race, const char *col);

int rules_class_int(rules *ru, int cls, const char *col, int def);
int rules_race_int(rules *ru, int race, const char *col, int def);
const char *rules_class_str(rules *ru, int cls, const char *col);

/* floor((score - 10) / 2) */
int abil_mod(int score);

/* Racial adjustment to ability a for the race. */
int rules_race_adjust(rules *ru, int race, int a);

/* Per-class-level values from the class tables; level is 1-based within
 * the class. Out-of-range levels clamp to the last row. */
int rules_cls_bab(rules *ru, int cls, int level);
int rules_cls_save(rules *ru, int cls, int level, const char *col);
int rules_cls_bonus_feats(rules *ru, int cls, int level);

/* XP required to reach level (1-based) per exptable.2da; -1 if unknown. */
long rules_xp_for_level(rules *ru, int level);

/* Spell slots for a spell level at a class level, before the ability bonus.
 * Returns -1 when the class cannot cast that spell level yet ("****"). */
int rules_spells_per_day(rules *ru, int cls, int level, int spell_level);
int rules_spells_known(rules *ru, int cls, int level, int spell_level);
/* Extra slots granted by an ability modifier for spell_level (>= 1). */
int spell_bonus(int abil_modifier, int spell_level);

res *rules_res(rules *ru);
int rules_nskills(rules *ru);
int rules_nfeats(rules *ru);
int rules_nclasses(rules *ru);

#endif
