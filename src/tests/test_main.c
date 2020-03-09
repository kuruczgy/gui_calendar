#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "editor.h"
#include "util.h"
#include "algo.h"

void test_todo_schedule() {
    // 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20
    // (    )[     [   ]   ](    )( )[    ](     )
    int n = 3;
    struct ts_ran E[] = { { 3, 8 }, { 6, 10 }, { 13, 15 } };
    int k = 4;
    struct todo tds[] = {
        { .estimated_duration = 3 },
        { .estimated_duration = 2 },
        { .estimated_duration = 1 },
        { .estimated_duration = 2 }
    };
    struct todo *T[] = { &tds[0], &tds[1], &tds[2], &tds[3] };
    struct ts_ran *G = todo_schedule(0, n, E, k, T);
    struct ts_ran exp[] = { { 0, 3 }, { 10, 12 }, { 12, 13 }, { 15, 17 } };
    for (int i = 0; i < k; ++ i) {
        assert(G[i].fr == exp[i].fr, "");
        assert(G[i].to == exp[i].to, "");
    }
}

void test_lookup_color() {
    assert(lookup_color("cornflowerblue") == 0xFF6495ED, "");
    assert(lookup_color("yellowgreen") == 0xFF9ACD32, "");
    assert(lookup_color("aliceblue") == 0xFFF0F8FF, "");
    assert(lookup_color("black") == 0xFF000000, "");
    assert(lookup_color("aaa") == 0, "");
    assert(lookup_color("eee") == 0, "");
    assert(lookup_color("zzz") == 0, "");
}

extern void test_editor_parser();

int main() {
    test_lookup_color();
    test_editor_parser();
    test_todo_schedule();
}
