#ifndef CHR_H
#define CHR_H

#include <stdint.h>

#include "gff.h"
#include "rng.h"
#include "rules.h"

#define MAX_LEVELS 60
#define MAX_SK     64
#define MAX_LFEATS 48   /* feats recorded for a single level */
#define MAX_CLS    64   /* class rows tracked per character */
#define MAX_KNOWN  48   /* spells learned at one level, per spell level */
#define NSPLVL     10

/* Everything the game records for one character level (LvlStatList). */
typedef struct {
    int cls;                 /* LvlStatClass */
    int hp;                  /* LvlStatHitDie: HP gained this level */
    int abil;                /* LvlStatAbility, -1 when none (ABIL_*) */
    int skillpts;            /* unspent skill points recorded at this level */
    int epic;                /* EpicLevel */
    int ranks[MAX_SK];       /* skill ranks bought this level */
    int nfeats;
    uint16_t feats[MAX_LFEATS];
    int nknown[NSPLVL];
    uint16_t known[NSPLVL][MAX_KNOWN];
} lvl_rec;

typedef struct {
    char first[64], last[64];
    char *desc;              /* malloc'd or NULL */
    char deity[48], subrace[48];
    int race, gender;
    int good_evil, lawful_chaotic;           /* 0..100 raw alignment */
    int abil[NABIL];         /* stored scores: before racial adjustment */
    long xp;
    int gold;
    int age;
    int unspent_skillpts;    /* top-level SkillPoints */
    int nlevels;
    lvl_rec lv[MAX_LEVELS];

    /* class-specific choices recorded on the class entry */
    int domain1, domain2;    /* cleric, -1 if none */
    int school;              /* wizard school, -1 if none */

    int familiar;            /* arcane casters: familiar type, -1 if none */
    char familiar_name[32];
    int companion;           /* druid/ranger: animal companion type, -1 if none */
    char companion_name[32];
    int package;             /* StartingPackage */
    int pkg[MAX_CLS];        /* packages.2da row picked per class, -1 = default */

    /* appearance */
    int appearance, head, soundset, phenotype, faction;
    int skin, hair, tattoo1, tattoo2;
    char portrait[40];
    int body[18];            /* BodyPart_* in file order */
} chr;

void chr_init(chr *c);
void chr_free(chr *c);

int rules_feat_by_label(rules *ru, const char *label);

/* Counting helpers. */
int chr_class_level(const chr *c, int cls);
int chr_class_level_upto(const chr *c, int cls, int nlevels);
int chr_has_feat(const chr *c, int feat);

/* Ability as it stood after the first nlevels levels (racial adjustment
 * included, level-up increases that came later removed). */
int chr_abil_at(rules *ru, const chr *c, int a, int nlevels);
int chr_eff_abil(rules *ru, const chr *c, int a);

int chr_bab(rules *ru, const chr *c);
int chr_save(rules *ru, const chr *c, int which); /* 0 fort, 1 ref, 2 will */
int chr_maxhp(rules *ru, const chr *c);
int chr_hp_rolled(const chr *c);

/* Skill points the game grants for level index li (0-based). */
int chr_skillpts_for_level(rules *ru, const chr *c, int li);
/* Cost in points of one rank of skill sk for class cls (1 or 2; 0 if
 * the class cannot take the skill at all). */
int chr_skill_cost(rules *ru, int cls, int sk);

/* Feats the rules hand out for free at level index li (0-based). Returns
 * the count written to out (capacity cap). */
int chr_auto_feats(rules *ru, const chr *c, int li, int *out, int cap);
/* How many selectable feats the character gets at level index li. */
int chr_general_feat_slots(rules *ru, const chr *c, int li);
int chr_bonus_feat_slots(rules *ru, const chr *c, int li);

/* Spell slots per day for a class at a spell level: base from the class
 * table plus the casting-ability bonus; -1 when the level is unavailable. */
int chr_spell_slots(rules *ru, const chr *c, int cls, int spell_level);
int chr_spell_in_school(rules *ru, int spell, int school);
int chr_spell_banned(rules *ru, const chr *c, int spell);   /* opposition school */
int chr_package(rules *ru, const chr *c, int cls);          /* packages.2da row */

/* Builds the .bic tree the game reads. rng is used for memorising wizard
 * spells and the like. */
gff_struct *chr_to_gff(rules *ru, const chr *c, rng *g);

/* GFF conversion. chr_from_gff returns 0 on success. */
int chr_from_gff(rules *ru, const gff_struct *g, chr *c, char *err,
                 size_t errlen);

#endif
