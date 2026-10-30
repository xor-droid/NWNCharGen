#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gff.h"

/* gffdump [-r out] file : dump a GFF file; with -r also re-serialise it to
 * out and verify that reading the result back dumps identically. */
static char *dump_to_string(const gff_struct *s, size_t *len)
{
    char *p = NULL;
    FILE *f = open_memstream(&p, len);

    gff_dump(f, s, 0);
    fclose(f);
    return p;
}

int main(int argc, char **argv)
{
    const char *rt = NULL, *path;
    size_t n;
    uint8_t *d;
    char ft[5], err[160];
    gff_struct *g;

    if (argc >= 4 && strcmp(argv[1], "-r") == 0) {
        rt = argv[2];
        path = argv[3];
    } else if (argc == 2) {
        path = argv[1];
    } else {
        fprintf(stderr, "usage: gffdump [-r roundtrip.out] file\n");
        return 2;
    }
    d = read_file(path, &n);
    if (!d) {
        perror(path);
        return 1;
    }
    g = gff_read(d, n, ft, err, sizeof err);
    if (!g) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 1;
    }
    if (!rt) {
        printf("%s V3.2  root struct id %u\n", ft, g->type);
        gff_dump(stdout, g, 0);
    } else {
        buf out = { 0 };
        gff_struct *g2;
        char *a, *b;
        size_t la, lb;

        gff_write(g, ft, &out);
        write_file(rt, out.p, out.n);
        g2 = gff_read(out.p, out.n, NULL, err, sizeof err);
        if (!g2) {
            fprintf(stderr, "re-read failed: %s\n", err);
            return 1;
        }
        a = dump_to_string(g, &la);
        b = dump_to_string(g2, &lb);
        if (la != lb || memcmp(a, b, la) != 0) {
            fprintf(stderr, "ROUNDTRIP MISMATCH\n");
            return 1;
        }
        printf("roundtrip ok: %zu bytes in, %zu bytes out, %zu dump bytes\n", n,
               out.n, la);
    }
    return 0;
}
