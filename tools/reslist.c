#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/res.h"

static void show(const char *name, int type, const char *src, void *arg)
{
    const char *ext = res_ext(type);
    const char *slash = strrchr(src, '/');

    (void)arg;
    if (ext)
        printf("%s.%s\t%s\n", name, ext, slash ? slash + 1 : src);
    else
        printf("%s.#%d\t%s\n", name, type, slash ? slash + 1 : src);
}

/* reslist INSTALL [ext]       list resources
 * reslist INSTALL -x name.ext  write resource to stdout */
int main(int argc, char **argv)
{
    char err[200];
    res *r;

    if (argc < 2) {
        fprintf(stderr, "usage: reslist INSTALL_DIR [ext | -x name.ext]\n");
        return 2;
    }
    r = res_open(argv[1], NULL, err, sizeof err);
    if (!r) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    if (argc == 4 && strcmp(argv[2], "-x") == 0) {
        char *dot = strrchr(argv[3], '.');
        size_t n;
        uint8_t *d;
        int type;

        if (!dot || (type = res_type_from_ext(dot + 1)) < 0) {
            fprintf(stderr, "need name.ext with a known extension\n");
            return 2;
        }
        *dot = '\0';
        d = res_get(r, argv[3], type, &n);
        if (!d) {
            fprintf(stderr, "not found\n");
            return 1;
        }
        fwrite(d, 1, n, stdout);
    } else {
        res_each(r, argc > 2 ? res_type_from_ext(argv[2]) : -1, show, NULL);
    }
    res_close(r);
    return 0;
}
