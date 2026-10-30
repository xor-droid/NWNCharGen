#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* Whole-file helpers. read_file returns a malloc'd block (NULL on error). */
uint8_t *read_file(const char *path, size_t *len);
int write_file(const char *path, const void *data, size_t len);

/* Growable byte buffer. */
typedef struct {
    uint8_t *p;
    size_t n, cap;
} buf;

void buf_put(buf *b, const void *d, size_t n);
void buf_zero(buf *b, size_t n);
void buf_u8(buf *b, uint32_t v);
void buf_u16(buf *b, uint32_t v);
void buf_u32(buf *b, uint32_t v);
void buf_u64(buf *b, uint64_t v);

/* Explicit little-endian access; never rely on host layout. */
uint16_t get_u16(const uint8_t *p);
uint32_t get_u32(const uint8_t *p);
uint64_t get_u64(const uint8_t *p);
void set_u32(uint8_t *p, uint32_t v);

#endif
