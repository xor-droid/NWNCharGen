#ifndef TLK_H
#define TLK_H

#include <stddef.h>
#include <stdint.h>

typedef struct tlk tlk;

tlk *tlk_open(const char *path);
void tlk_free(tlk *t);
uint32_t tlk_count(const tlk *t);
/* Copies the text for strref into out (NUL-terminated, truncated to cap).
 * Returns the full length, or -1 if absent. */
int tlk_copy(const tlk *t, uint32_t strref, char *out, size_t cap);

#endif
