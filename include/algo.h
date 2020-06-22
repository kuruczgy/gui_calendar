#ifndef GUI_CALENDAR_ALGO_H
#define GUI_CALENDAR_ALGO_H
#include <stddef.h>
#include <stdbool.h>
#include "datetime.h"

struct layout_event {
    struct ts_ran time;
    int max_n;
    int col;
    int idx;
};
struct schedule_todo {
    ts start;
    int estimated_duration;
};

void calendar_layout(struct layout_event *e, int N);

/* Schedule todos into the free spaces between events.
 * arg n, E: n event ranges
 * arg k, T: k todos
 */
struct ts_ran * todo_schedule(ts base, int n, struct ts_ran *E,
        int k, struct schedule_todo *T);

/* Given an array of n elements, finds the next permutation in
 * lexicographical order with a different k-prefix; in effect, it
 * generates all k-permutations of the array.
 * It is required that the suffix be sorted in ascending order. This
 * invariant will be maintained by the function.
 * Before the first call, the array must be sorted in ascending order.
 * Returns true unless the input is the last k-permutation.
 */
int next_k_permutation(int* elements, size_t n, size_t k);

typedef bool(*sort_lt)(void *a, void *b, void *cl);
void heapsort(void *a, int count, size_t is, sort_lt lt, void *cl);

#endif
