#include <stdlib.h>
#include <stdio.h>
#include <time.h>

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

struct stopwatch sw_start() {
    struct stopwatch sw;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &sw.fr);
    return sw;
}

void sw_end_print(struct stopwatch sw, const char *msg) {
    struct timespec to;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &to);
    unsigned long long int diff =
        (to.tv_sec - sw.fr.tv_sec)*1000000000ULL + (to.tv_nsec - sw.fr.tv_nsec);
    double ms = diff / 1e6;
    fprintf(stderr, "STOPWATCH %0.2fms: %s\n", ms, msg);
}
