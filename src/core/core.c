#include <stdlib.h>
#include <stdio.h>

#include "core.h"

void asrt(bool b, const char *msg) {
    if (!b) {
        fprintf(stderr, "assert error msg: %s\n", msg);
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
        abort();
#else
        exit(1);
#endif
    }
}

void * malloc_check(size_t size) {
    void *p = malloc(size);
    asrt(p, "oom");
    return p;
}

int mini(int a, int b) { return a < b ? a : b; }
int maxi(int a, int b) { return a < b ? b : a; }
