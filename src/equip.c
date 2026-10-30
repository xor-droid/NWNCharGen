#define _POSIX_C_SOURCE 200809L
#include "equip.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_PACK  64
#define GRID_W    10
#define GRID_H    64     /* the pack is one tall grid; premades use rows past 5 */
#define FIRST_ID  193u      /* the engine numbers a new character's items from here */
#define BASE_TORCH 15
/* item properties that give the item a use of its own (cast spell, thieves'
 * tools, trap), as seen in the engine's characters */
static int useable_property(int name)
{
    return name == 15 || name == 55 || name == 70;
}

static const char *const HEAD[] = { "BaseItem", "Tag", "Identified", "Description",
                                    "DescIdentified", "LocalizedName", "StackSize",
                                    "Stolen" };
/* fields the builder places itself or leaves out */
static const char *const SKIP[] = { "BaseItem", "Tag", "Identified", "Description",
                                    "DescIdentified", "LocalizedName", "StackSize",
                                    "Stolen", "Dropable", "Pickpocketable", "Charges",
                                    "Cost", "AddCost", "Plot", "Cursed", "PropertiesList",
                                    "PaletteID", "Comment", "TemplateResRef",
                                    "XPosition", "YPosition", "ZPosition",
                                    "XOrientation", "YOrientation", "ZOrientation" };

static int listed(const char *const *l, size_t n, const char *s)
{
    size_t i;

    for (i = 0; i < n; i++)
        if (!strcmp(l[i], s))
            return 1;
    return 0;
}

static void lower(const char *s, char *out, size_t n)
{
    size_t i;

    for (i = 0; s[i] && i + 1 < n; i++)
        out[i] = (char)tolower((unsigned char)s[i]);
    out[i] = '\0';
}

static void put_if(gff_struct *dst, const gff_struct *src, const char *label,
                   int type, uint64_t def)
{
    gff_field *f = gff_get(src, label);

    if (f)
        gff_put_copy(dst, f);
    else
        gff_set_uint(dst, label, type, def);
}

gff_struct *equip_item_from_uti(rules *ru, const char *resref, uint32_t object_id)
{
    char name[48];
    size_t len = 0, i;
    uint8_t *d;
    gff_struct *u, *it;
    char err[120], ft[5];
    gff_field *pl;

    lower(resref, name, sizeof name);
    d = res_get(rules_res(ru), name, RES_UTI, &len);
    if (!d)
        return NULL;
    u = gff_read(d, len, ft, err, sizeof err);
    free(d);
    if (!u)
        return NULL;

    it = gff_new(0);
    gff_set_uint(it, "ObjectId", GFF_DWORD, object_id);
    for (i = 0; i < sizeof HEAD / sizeof *HEAD; i++) {
        gff_field *f = gff_get(u, HEAD[i]);

        if (!strcmp(HEAD[i], "Identified")) {
            gff_set_uint(it, "Identified", GFF_BYTE, 1);   /* always identified */
        } else if (f) {
            gff_put_copy(it, f);
        } else if (!strcmp(HEAD[i], "StackSize")) {
            gff_set_uint(it, "StackSize", GFF_WORD, 1);
        }
        if (!strcmp(HEAD[i], "Stolen"))
            gff_set_uint(it, "Stolen", GFF_BYTE, 0);     /* templates may say 1; new items never are */
        if (!strcmp(HEAD[i], "Stolen")) {
            gff_set_uint(it, "Dropable", GFF_BYTE, 1);
            gff_set_uint(it, "Pickpocketable", GFF_BYTE, 1);
        }
    }
    for (i = 0; i < u->n; i++)      /* model parts, armor parts, colours ... */
        if (!listed(SKIP, sizeof SKIP / sizeof *SKIP, u->f[i]->label))
            gff_put_copy(it, u->f[i]);
    put_if(it, u, "Charges", GFF_BYTE, 0);
    put_if(it, u, "Cost", GFF_DWORD, 0);
    put_if(it, u, "AddCost", GFF_DWORD, 0);
    put_if(it, u, "Plot", GFF_BYTE, 0);
    if (gff_get(u, "Cursed"))
        gff_put_copy(it, gff_get(u, "Cursed"));

    pl = gff_get(u, "PropertiesList");
    if (pl) {
        gff_put_copy(it, pl);
        pl = gff_get(it, "PropertiesList");
        for (i = 0; i < pl->v.list.n; i++) {
            gff_struct *p = pl->v.list.v[i];

            p->type = (uint32_t)i;      /* the engine numbers them by position */
            gff_set_uint(p, "UsesPerDay", GFF_BYTE, 255);
            gff_set_uint(p, "Useable", GFF_BYTE,
                         useable_property((int)gff_get_int(p, "PropertyName", -1)));
        }
    } else {
        gff_list_init(it, "PropertiesList");
    }
    gff_set_float(it, "XPosition", 0);
    gff_set_float(it, "YPosition", 0);
    gff_set_float(it, "ZPosition", 0);
    gff_set_float(it, "XOrientation", 1);
    gff_set_float(it, "YOrientation", 0);
    gff_set_float(it, "ZOrientation", 0);
    gff_free(u);
    return it;
}

int equip_package_resrefs(rules *ru, const chr *c, char out[][32], int cap)
{
    const twoda *pk = rules_tab(ru, "packages"), *eq;
    int row = chr_package(ru, c, c->lv[0].cls), r, n = 0;
    const char *tab;

    if (!pk || row < 0 || row >= pk->nrows)
        return 0;
    tab = twoda_get(pk, row, "Equip2DA");
    eq = tab ? rules_tab(ru, tab) : NULL;
    for (r = 0; eq && r < eq->nrows && n < cap; r++) {
        const char *l = twoda_get(eq, r, "Label");

        if (l && *l)
            snprintf(out[n++], 32, "%s", l);
    }
    return n;
}

typedef struct {
    gff_struct *st;
    char ref[32];
    int unit, max, w, h, count, id, x, y;
} slot;

static int fits(const unsigned char *grid, int x, int y, int w, int h)
{
    int i, j;

    if (x + w > GRID_W || y + h > GRID_H)
        return 0;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (grid[(y + j) * GRID_W + x + i])
                return 0;
    return 1;
}

void equip_fill(rules *ru, const chr *c, gff_struct *root, rng *g)
{
    static const int robes[] = { 1, 2, 4, 6, 7, 8 };
    char refs[MAX_PACK][32], nm[32];
    slot s[MAX_PACK];
    unsigned char grid[GRID_W * GRID_H];
    const twoda *bi = rules_tab(ru, "baseitems");
    int nref = equip_package_resrefs(ru, c, refs, MAX_PACK), n = 0, i, j, id;
    gff_struct *robe;

    memset(grid, 0, sizeof grid);
    for (i = 0; i < nref; i++) {
        gff_struct *st;
        int base, k;

        for (k = 0; k < n; k++)             /* one more of a stackable item */
            if (!strcasecmp(s[k].ref, refs[i]) && s[k].max > 1 &&
                s[k].unit * (s[k].count + 1) <= s[k].max)
                break;
        if (k < n) {
            s[k].count++;
            continue;
        }
        st = equip_item_from_uti(ru, refs[i], 0);
        if (!st)
            continue;
        base = (int)gff_get_int(st, "BaseItem", 0);
        snprintf(s[n].ref, sizeof s[n].ref, "%.31s", refs[i]);
        s[n].st = st;
        s[n].unit = (int)gff_get_int(st, "StackSize", 1);
        s[n].max = bi ? twoda_int(bi, base, "Stacking", 1) : 1;
        s[n].w = bi ? twoda_int(bi, base, "InvSlotWidth", 1) : 1;
        s[n].h = bi ? twoda_int(bi, base, "InvSlotHeight", 1) : 1;
        s[n].count = 1;
        n++;
    }

    /* The first item of the table gets the highest ObjectId, a torch always the
     * lowest; the pack is stored from the highest id down. */
    id = (int)FIRST_ID + n;
    for (i = 0, j = 0; i < n; i++)
        s[i].id = gff_get_int(s[i].st, "BaseItem", 0) == BASE_TORCH ?
                  (int)FIRST_ID + 1 + j++ : id--;

    /* first fit, row by row, in table order */
    for (i = 0; i < n; i++) {
        int x, y, done = 0;

        for (y = 0; y < GRID_H && !done; y++)
            for (x = 0; x < GRID_W && !done; x++)
                if (fits(grid, x, y, s[i].w, s[i].h)) {
                    int a, b;

                    for (b = 0; b < s[i].h; b++)
                        for (a = 0; a < s[i].w; a++)
                            grid[(y + b) * GRID_W + x + a] = 1;
                    s[i].x = x;
                    s[i].y = y;
                    done = 1;
                }
        if (!done)
            s[i].x = s[i].y = 0;            /* a full pack: the game re-places it */
    }

    /* the robe: worn in the chest slot, numbered first */
    snprintf(nm, sizeof nm, "nw_cloth00%d", robes[rng_below(g, 6)]);
    robe = equip_item_from_uti(ru, nm, FIRST_ID);
    if (robe) {
        robe->type = 2;
        gff_list_add(root, "Equip_ItemList", robe);
    }

    /* highest id first */
    for (j = 0; j < n; j++) {
        int best = -1;

        for (i = 0; i < n; i++)
            if (s[i].st && (best < 0 || s[i].id > s[best].id))
                best = i;
        {
            gff_struct *it = s[best].st;
            gff_struct *out = gff_new(0);
            size_t f;

            /* ObjectId (and stack) are set now that the pack is laid out */
            gff_set_uint(out, "ObjectId", GFF_DWORD, (uint64_t)s[best].id);
            for (f = 0; f < it->n; f++) {
                if (!strcmp(it->f[f]->label, "ObjectId"))
                    continue;
                gff_put_copy(out, it->f[f]);
            }
            if (s[best].count > 1) {
                gff_set_uint(out, "StackSize", GFF_WORD,
                             (uint64_t)s[best].unit * (uint64_t)s[best].count);
                gff_set_uint(out, "Cost", GFF_DWORD,
                             (uint64_t)gff_get_int(it, "Cost", 0) * (uint64_t)s[best].count);
            }
            gff_set_uint(out, "Repos_PosX", GFF_WORD, (uint64_t)s[best].x);
            gff_set_uint(out, "Repos_Posy", GFF_WORD, (uint64_t)s[best].y);
            gff_list_add(root, "ItemList", out);
            gff_free(it);
            s[best].st = NULL;
        }
    }
}
