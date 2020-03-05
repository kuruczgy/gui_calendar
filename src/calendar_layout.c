
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "util.h"

struct point {
    int val;
    bool start;
    int index;
};

static int cmp_point(const void *pa, const void *pb) {
    const struct point *a = pa, *b = pb;
    int val = a->val - b->val;
    if (val == 0) {
        if (!a->start && b->start) return -1;
        if (a->start && !b->start) return 1;
    }
    return val;
}

static int first_free_bit(uint32_t n) {
    for (int i = 0; i < 32; ++i) {
        if (!(n & (1 << i))) return i;
    }
    return -1;
}


/* looked at:
 * https://github.com/aosp-mirror/platform_packages_apps_calendar/blob/master/src/com/android/calendar/Event.java
 * for reference */
void calendar_layout(struct layout_event *e, int N) {
    struct point *points = malloc(sizeof(struct point) * N * 2);
    for (int i = 0; i < N; ++i) {
        points[2*i] = (struct point){
            .val = e[i].start, .start = true, .index = i };
        points[2*i+1] = (struct point){
            .val = e[i].end, .start = false, .index = i };
    }
    qsort(points, N * 2, sizeof(struct point), &cmp_point);

    for (int i = 0; i < N; i++) e[i].col = e[i].max_n = -1;

    int active_n = 0;
    int max_n = 0;
    int *component = malloc(sizeof(int) * N);
    int component_size = 0;
    uint32_t mask = 0;
    for (int i = 0; i < 2 * N; ++i) {
        const struct point *p = &points[i];
        /* printf("p %d %s, active_n=%d\n",
                p->index, p->start ? "start" : "end", active_n); */

        if (p->start) {
            component[component_size++] = p->index;
            int col = first_free_bit(mask);
            if (col < 0) col = 0;
            e[p->index].col = col;
            mask |= (1 << col);
        }

        if (!p->start) {
            int col = e[p->index].col;
            assert(col >= 0, "fuck 1");
            mask &= ~(1 << e[p->index].col);
        }

        active_n += p->start ? 1 : -1;
        assert(active_n >= 0, "fuck 2");
        if (active_n > max_n) max_n = active_n;

        if (active_n == 0) {
            assert(mask == 0, "fuck 3");
            // graph component boundary
            for (int k = 0; k < component_size; k++) {
                e[component[k]].max_n = max_n;
            }
            component_size = 0;
            max_n = 0;
        }
    }
    free(component);
    free(points);
}
