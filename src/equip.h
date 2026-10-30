#ifndef EQUIP_H
#define EQUIP_H

#include "chr.h"

/* Starting equipment as the game stores it: every item of the first class's
 * package (packages.2da Equip2DA) goes into the pack as a full item struct
 * built from its .uti template, and one cloth robe is worn. */

/* An item struct built from the .uti named resref (any case), in the field
 * layout of the engine's own characters, with the given ObjectId. NULL when
 * the template does not exist. Position fields are not set (see equip_fill). */
gff_struct *equip_item_from_uti(rules *ru, const char *resref, uint32_t object_id);

/* The resrefs of the character's package equipment, in table order (first
 * class's package). Returns how many were written to out (capacity cap). */
int equip_package_resrefs(rules *ru, const chr *c, char out[][32], int cap);

/* Adds the pack and the worn robe to root's ItemList / Equip_ItemList. The
 * rng picks the robe, so call it after every other draw of the build. */
void equip_fill(rules *ru, const chr *c, gff_struct *root, rng *g);

#endif
