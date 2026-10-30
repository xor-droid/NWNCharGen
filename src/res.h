#ifndef RES_H
#define RES_H

#include <stddef.h>
#include <stdint.h>

#include "twoda.h"

/* Resource type ids used by the Aurora engine. */
enum {
    RES_BMP = 1, RES_TGA = 3, RES_WAV = 4, RES_ARE = 2012, RES_IFO = 2014,
    RES_BIC = 2015, RES_2DA = 2017, RES_TLK = 2018, RES_NSS = 2009,
    RES_NCS = 2010, RES_ITP = 2030, RES_GFF = 2037, RES_UTC = 2027,
    RES_UTI = 2025, RES_DLG = 2029
};

const char *res_ext(int type);   /* "2da", ... or NULL */
int res_type_from_ext(const char *ext); /* -1 if unknown */

typedef struct res res;

/* install: the game directory (contains data/ and ovr/).
 * user: the Documents/Neverwinter Nights directory (override/), or NULL.
 * Returns NULL and fills err on failure. */
res *res_open(const char *install, const char *user, char *err, size_t errlen);
void res_close(res *r);

/* malloc'd, NUL-terminated copy of a resource; precedence is user override,
 * install ovr, then KEY/BIF. NULL if missing. */
uint8_t *res_get(res *r, const char *name, int type, size_t *len);
twoda *res_2da(res *r, const char *name);

/* Enumerate KEY/BIF resources of a type (type < 0 for all). */
typedef void (*res_visit)(const char *name, int type, const char *source,
                          void *arg);
void res_each(res *r, int type, res_visit fn, void *arg);

const char *res_install_dir(const res *r);

#endif
