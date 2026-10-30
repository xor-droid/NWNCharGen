#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "chr.h"
#include "levelup.h"

#define BYTE(g, l, v)   gff_set_uint((g), (l), GFF_BYTE, (uint64_t)(v))
#define WORD(g, l, v)   gff_set_uint((g), (l), GFF_WORD, (uint64_t)(v))
#define DWORD(g, l, v)  gff_set_uint((g), (l), GFF_DWORD, (uint64_t)(v))
#define SHORT(g, l, v)  gff_set_int((g), (l), GFF_SHORT, (int64_t)(v))
#define CHAR(g, l, v)   gff_set_int((g), (l), GFF_CHAR, (int64_t)(v))
#define INT(g, l, v)    gff_set_int((g), (l), GFF_INT, (int64_t)(v))

#define OBJECT_INVALID 0x7f000000u

static void locstr(gff_struct *g, const char *label, const char *text)
{
    gff_set_locstr(g, label, 0xffffffffu, text);
}

static void spell_list(gff_struct *owner, const char *label, const uint16_t *sp,
                       int n, uint32_t id)
{
    int i;

    gff_list_init(owner, label);
    for (i = 0; i < n; i++) {
        gff_struct *e = gff_new(id);
        WORD(e, "Spell", sp[i]);
        gff_list_add(owner, label, e);
    }
}

/* All spells of one level a class has learned, in learning order. */
static int known_at(const chr *c, int cls, int sl, uint16_t *out, int cap)
{
    int i, j, n = 0;

    for (i = 0; i < c->nlevels; i++) {
        if (c->lv[i].cls != cls)
            continue;
        for (j = 0; j < c->lv[i].nknown[sl] && n < cap; j++)
            out[n++] = c->lv[i].known[sl][j];
    }
    return n;
}

static gff_struct *class_entry(rules *ru, const chr *c, int cls, rng *g)
{
    gff_struct *e = gff_new(2);
    uint16_t sp[256];
    int sl, i, n;

    INT(e, "Class", cls);
    SHORT(e, "ClassLevel", chr_class_level(c, cls));
    if (c->school >= 0 && rules_class_int(ru, cls, "PickSchool", 0))
        BYTE(e, "School", c->school);
    if (rules_class_int(ru, cls, "PickDomains", 0) > 0 && c->domain1 >= 0) {
        BYTE(e, "Domain1", c->domain1);
        BYTE(e, "Domain2", c->domain2);
    }

    /* spontaneous casters record their slots per spell level */
    if (rules_class_tab(ru, cls, "SpellKnownTable") &&
        rules_class_int(ru, cls, "MemorizesSpells", 1) == 0) {
        gff_list_init(e, "SpellsPerDayList");
        for (sl = 0; sl < NSPLVL; sl++) {
            gff_struct *d = gff_new(17767);
            int slots = chr_spell_slots(ru, c, cls, sl);
            BYTE(d, "NumSpellsLeft", slots > 0 ? slots : 0);
            gff_list_add(e, "SpellsPerDayList", d);
        }
    }
    for (sl = 0; sl < NSPLVL; sl++) {
        char label[32];
        n = known_at(c, cls, sl, sp, 256);
        if (!n)
            continue;
        snprintf(label, sizeof label, "KnownList%d", sl);
        spell_list(e, label, sp, n, 3);
    }

    /* wizards prepare from their book; fill every slot */
    if (rules_class_int(ru, cls, "MemorizesSpells", 0) == 1 &&
        rules_class_int(ru, cls, "Arcane", 0) == 1) {
        for (sl = 0; sl < NSPLVL; sl++) {
            int slots = chr_spell_slots(ru, c, cls, sl);
            char label[32];
            uint16_t book[256];
            int nb = known_at(c, cls, sl, book, 256);
            int first = -1;   /* specialist: lead with a school spell */

            if (slots <= 0 || nb == 0)
                continue;
            snprintf(label, sizeof label, "MemorizedList%d", sl);
            gff_list_init(e, label);

            if (sl >= 1 && c->school > 0)
                for (i = 0; i < nb && first < 0; i++)
                    if (chr_spell_in_school(ru, book[i], c->school))
                        first = i;
            if (first > 0) {   /* move it to the front, keep the rest in order */
                uint16_t sv = book[first];
                memmove(book + 1, book, (size_t)first * sizeof book[0]);
                book[0] = sv;
            }
            for (i = 0; i < slots; i++) {
                gff_struct *m = gff_new(3);
                /* each known spell once, then repeats chosen at random */
                uint16_t pick = i < nb ? book[i] : book[rng_below(g, (unsigned)nb)];
                WORD(m, "Spell", pick);
                BYTE(m, "Ready", 1);
                BYTE(m, "SpellMetaMagic", 0);
                gff_list_add(e, label, m);
            }
        }
    }
    return e;
}

static gff_struct *lvl_entry(rules *ru, const chr *c, int li)
{
    const lvl_rec *lr = &c->lv[li];
    gff_struct *e = gff_new(0);
    int sl, i, nsk = rules_nskills(ru);
    uint16_t sp[MAX_KNOWN];

    if (lr->abil >= 0)
        BYTE(e, "LvlStatAbility", lr->abil);
    BYTE(e, "LvlStatHitDie", lr->hp);
    BYTE(e, "LvlStatClass", lr->cls);
    BYTE(e, "EpicLevel", lr->epic);
    for (sl = 0; sl < NSPLVL; sl++) {
        char label[32];
        if (!lr->nknown[sl])
            continue;
        snprintf(label, sizeof label, "KnownList%d", sl);
        for (i = 0; i < lr->nknown[sl]; i++)
            sp[i] = lr->known[sl][i];
        spell_list(e, label, sp, lr->nknown[sl], 0);
    }
    WORD(e, "SkillPoints", lr->skillpts);
    gff_list_init(e, "SkillList");
    for (i = 0; i < nsk; i++) {
        gff_struct *s = gff_new(0);
        BYTE(s, "Rank", i < MAX_SK ? lr->ranks[i] : 0);
        gff_list_add(e, "SkillList", s);
    }
    gff_list_init(e, "FeatList");
    for (i = 0; i < lr->nfeats; i++) {
        gff_struct *f = gff_new(1);
        WORD(f, "Feat", lr->feats[i]);
        gff_list_add(e, "FeatList", f);
    }
    return e;
}

static int has_labeled(rules *ru, const chr *c, const char *l)
{
    int f = rules_feat_by_label(ru, l);

    return f >= 0 && chr_has_feat(c, f);
}

static int armor_class(rules *ru, const chr *c)
{
    int ac = 10 + abil_mod(chr_eff_abil(ru, c, ABIL_DEX));

    if (has_labeled(ru, c, "MonkACBonus")) {
        int w = abil_mod(chr_eff_abil(ru, c, ABIL_WIS));
        ac += w > 0 ? w : 0;
    }
    return ac;
}

static int class_order(const chr *c, int *out)
{
    int i, j, n = 0;

    for (i = 0; i < c->nlevels; i++) {
        for (j = 0; j < n; j++)
            if (out[j] == c->lv[i].cls)
                break;
        if (j == n)
            out[n++] = c->lv[i].cls;
    }
    return n;
}

gff_struct *chr_to_gff(rules *ru, const chr *c, rng *g)
{
    static const char *bp[18] = { "BodyPart_Neck", "BodyPart_Torso", "BodyPart_Belt",
        "BodyPart_Pelvis", "BodyPart_LShoul", "BodyPart_LBicep", "BodyPart_LFArm",
        "BodyPart_LHand", "BodyPart_LThigh", "BodyPart_LShin", "BodyPart_LFoot",
        "BodyPart_RShoul", "BodyPart_RBicep", "BodyPart_RFArm", "BodyPart_RHand",
        "BodyPart_RThigh", "BodyPart_RShin", "ArmorPart_RFoot" };
    static const char *scripts[] = { "ScriptHeartbeat", "ScriptOnNotice",
        "ScriptSpellAt", "ScriptAttacked", "ScriptDamaged", "ScriptDisturbed",
        "ScriptEndRound", "ScriptDialogue", "ScriptSpawn", "ScriptRested",
        "ScriptDeath", "ScriptUserDefine", "ScriptOnBlocked" };
    gff_struct *r = gff_new(0xffffffffu), *ci;
    int order[16], ncls, i, j, nsk = rules_nskills(ru);
    int totals[MAX_SK];
    int bab = chr_bab(ru, c), smod = abil_mod(chr_eff_abil(ru, c, ABIL_STR));
    int hp = chr_hp_rolled(c), maxhp = chr_maxhp(ru, c);
    int first_cls = c->lv[0].cls;
    int size = has_labeled(ru, c, "Small") ? 2 : 3;
    int attacks = 1 + (bab > 0 ? (bab - 1) / 5 : 0);

    locstr(r, "FirstName", c->first);
    locstr(r, "LastName", c->last);
    locstr(r, "Description", c->desc ? c->desc : "");
    BYTE(r, "IsPC", 1);
    BYTE(r, "IsDM", 0);
    gff_set_str(r, "Tag", GFF_EXOSTR, "");
    gff_set_str(r, "Conversation", GFF_RESREF, "");
    BYTE(r, "Interruptable", 1);
    INT(r, "Age", c->age);
    BYTE(r, "Gender", c->gender);
    BYTE(r, "Race", c->race);
    gff_set_str(r, "Subrace", GFF_EXOSTR, c->subrace);
    BYTE(r, "StartingPackage", chr_package(ru, c, first_cls));
    gff_set_str(r, "Deity", GFF_EXOSTR, c->deity);
    ncls = class_order(c, order);
    for (j = 0; j < ncls; j++)
        if (order[j] == c->lv[c->nlevels - 1].cls)
            BYTE(r, "MClassLevUpIn", j);
    SHORT(r, "willbonus", 0);
    SHORT(r, "fortbonus", 0);
    SHORT(r, "refbonus", 0);
    DWORD(r, "Gold", c->gold);
    BYTE(r, "BaseAttackBonus", bab > 255 ? 255 : bab);
    CHAR(r, "RefSaveThrow", chr_save(ru, c, 1));
    CHAR(r, "WillSaveThrow", chr_save(ru, c, 2));
    CHAR(r, "FortSaveThrow", chr_save(ru, c, 0));
    SHORT(r, "ArmorClass", armor_class(ru, c));
    BYTE(r, "OverrideBAB", 0);
    /* the file orders the abilities Str Dex Int Wis Con Cha */
    BYTE(r, "Str", c->abil[ABIL_STR]);
    BYTE(r, "Dex", c->abil[ABIL_DEX]);
    BYTE(r, "Int", c->abil[ABIL_INT]);
    BYTE(r, "Wis", c->abil[ABIL_WIS]);
    BYTE(r, "Con", c->abil[ABIL_CON]);
    BYTE(r, "Cha", c->abil[ABIL_CHA]);
    BYTE(r, "NaturalAC", 0);
    WORD(r, "SoundSetFile", c->soundset);
    BYTE(r, "Plot", 0);
    BYTE(r, "NoPermDeath", 0);
    BYTE(r, "Disarmable", 1);
    BYTE(r, "BodyBag", 0);
    SHORT(r, "HitPoints", hp);
    SHORT(r, "CurrentHitPoints", hp);
    SHORT(r, "MaxHitPoints", maxhp);
    SHORT(r, "PregameCurrent", maxhp);
    DWORD(r, "Experience", c->xp);
    BYTE(r, "MovementRate", 0);
    gff_set_str(r, "Portrait", GFF_RESREF, c->portrait);
    BYTE(r, "GoodEvil", c->good_evil);
    BYTE(r, "LawfulChaotic", c->lawful_chaotic);
    BYTE(r, "Color_Skin", c->skin);
    BYTE(r, "Color_Hair", c->hair);
    BYTE(r, "Color_Tattoo1", c->tattoo1);
    BYTE(r, "Color_Tattoo2", c->tattoo2);
    INT(r, "Phenotype", c->phenotype);
    WORD(r, "Appearance_Type", c->appearance);
    BYTE(r, "Appearance_Head", c->head);
    BYTE(r, "Tail", 0);
    BYTE(r, "Wings", 0);
    WORD(r, "FactionID", c->faction);
    gff_set_float(r, "ChallengeRating", 0);
    for (i = 0; i < 18; i++)
        BYTE(r, bp[i], c->body[i]);

    gff_list_init(r, "ClassList");
    for (j = 0; j < ncls; j++)
        gff_list_add(r, "ClassList", class_entry(ru, c, order[j], g));
    gff_list_init(r, "LvlStatList");
    for (i = 0; i < c->nlevels; i++)
        gff_list_add(r, "LvlStatList", lvl_entry(ru, c, i));

    if (c->familiar >= 0) {
        INT(r, "FamiliarType", c->familiar);
        gff_set_str(r, "FamiliarName", GFF_EXOSTR, c->familiar_name);
    }
    if (c->companion >= 0) {
        INT(r, "CompanionType", c->companion);
        gff_set_str(r, "CompanionName", GFF_EXOSTR, c->companion_name);
    }
    WORD(r, "SkillPoints", c->unspent_skillpts);

    memset(totals, 0, sizeof totals);
    for (i = 0; i < c->nlevels; i++)
        for (j = 0; j < nsk && j < MAX_SK; j++)
            totals[j] += c->lv[i].ranks[j];
    gff_list_init(r, "SkillList");
    for (j = 0; j < nsk; j++) {
        gff_struct *s = gff_new(0);
        BYTE(s, "Rank", j < MAX_SK ? totals[j] : 0);
        gff_list_add(r, "SkillList", s);
    }
    gff_list_init(r, "FeatList");
    for (i = 0; i < c->nlevels; i++)
        for (j = 0; j < c->lv[i].nfeats; j++) {
            gff_struct *f = gff_new(1);
            WORD(f, "Feat", c->lv[i].feats[j]);
            gff_list_add(r, "FeatList", f);
        }

    ci = gff_new(51882);
    BYTE(ci, "NumAttacks", attacks > 4 ? 4 : attacks);
    CHAR(ci, "OnHandAttackMod", smod);
    CHAR(ci, "OnHandDamageMod", smod);
    CHAR(ci, "OffHandAttackMod", 0);
    CHAR(ci, "OffHandDamageMod", 0);
    BYTE(ci, "SpellResistance", 0);
    BYTE(ci, "ArcaneSpellFail", 0);
    BYTE(ci, "ArmorCheckPen", 0);
    BYTE(ci, "UnarmedDamDice", 1);
    BYTE(ci, "UnarmedDamDie", 3);
    BYTE(ci, "OnHandCritRng", 1);
    BYTE(ci, "OnHandCritMult", 2);
    BYTE(ci, "OffHandWeaponEq", 0);
    BYTE(ci, "OffHandCritRng", 1);
    BYTE(ci, "OffHandCritMult", 2);
    DWORD(ci, "LeftEquip", OBJECT_INVALID);
    DWORD(ci, "RightEquip", OBJECT_INVALID);
    gff_set_str(ci, "LeftString", GFF_EXOSTR, "");
    gff_set_str(ci, "RightString", GFF_EXOSTR, "");
    BYTE(ci, "DamageDice", 0);
    BYTE(ci, "DamageDie", 0);
    gff_list_init(ci, "AttackList");
    gff_list_init(ci, "DamageList");
    gff_set_struct(r, "CombatInfo", ci);

    BYTE(r, "DetectMode", 0);
    BYTE(r, "StealthMode", 0);
    DWORD(r, "MasterID", OBJECT_INVALID);
    INT(r, "CreatureSize", size);
    BYTE(r, "IsDestroyable", 1);
    BYTE(r, "IsRaiseable", 1);
    BYTE(r, "DeadSelectable", 1);
    BYTE(r, "IsCommandable", 1);
    BYTE(r, "IsImmortal", 0);
    BYTE(r, "Lootable", 0);
    DWORD(r, "BodyBagId", OBJECT_INVALID);
    DWORD(r, "DecayTime", 5000);
    gff_set_str(r, "TemplateResRef", GFF_RESREF, "");
    for (i = 0; i < (int)(sizeof scripts / sizeof *scripts); i++)
        gff_set_str(r, scripts[i], GFF_RESREF, "default");
    gff_list_init(r, "Equip_ItemList");
    gff_list_init(r, "ItemList");
    gff_list_init(r, "PerceptionList");
    gff_set_struct(r, "CombatRoundData", gff_new(51930));
    DWORD(r, "AreaId", 1);
    DWORD(r, "SitObject", OBJECT_INVALID);
    BYTE(r, "AmbientAnimState", 0);
    BYTE(r, "PM_IsPolymorphed", 0);
    BYTE(r, "Listening", 0);
    gff_set_float(r, "XPosition", 20.0);
    gff_set_float(r, "YPosition", 2.11350584);
    gff_set_float(r, "ZPosition", 0);
    gff_set_float(r, "XOrientation", 0);
    gff_set_float(r, "YOrientation", 1);
    gff_set_float(r, "ZOrientation", 0);
    DWORD(r, "AnimationDay", 0);
    DWORD(r, "AnimationTime", 0);
    gff_list_init(r, "QBList");
    for (i = 0; i < 36; i++) {
        gff_struct *q = gff_new(0);
        BYTE(q, "QBObjectType", 0);
        gff_list_add(r, "QBList", q);
    }
    return r;
}
