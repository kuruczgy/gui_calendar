#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "editor.h"
#include "util.h"

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
}
