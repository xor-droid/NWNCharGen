#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/* xoshiro256** seeded through splitmix64. Self-contained so that a printed
 * seed reproduces a build on any platform. */
typedef struct {
    uint64_t s[4];
} rng;

static inline uint64_t rng_rotl(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static inline void rng_seed(rng *r, uint64_t seed)
{
    int i;

    for (i = 0; i < 4; i++) {
        uint64_t z = (seed += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        r->s[i] = z ^ (z >> 31);
    }
}

static inline uint64_t rng_next(rng *r)
{
    uint64_t res = rng_rotl(r->s[1] * 5, 7) * 9;
    uint64_t t = r->s[1] << 17;

    r->s[2] ^= r->s[0];
    r->s[3] ^= r->s[1];
    r->s[1] ^= r->s[2];
    r->s[0] ^= r->s[3];
    r->s[2] ^= t;
    r->s[3] = rng_rotl(r->s[3], 45);
    return res;
}

/* Uniform in [0, n), n > 0, without modulo bias. */
static inline unsigned rng_below(rng *r, unsigned n)
{
    uint64_t lim = UINT64_MAX - (UINT64_MAX % n);
    uint64_t v;

    do {
        v = rng_next(r);
    } while (v >= lim);
    return (unsigned)(v % n);
}

#endif
