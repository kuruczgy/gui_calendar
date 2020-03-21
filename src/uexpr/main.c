
#include <string.h>

#include "uexpr.h"

static uexpr_val test_get(void *cl, const char *key) {
    if (strcmp(key, "test") == 0) return uexpr_create_string("hello world");
    return NULL;
}
static bool test_set(void *cl, const char *key, uexpr_val val) {
    return false;
}
int main() {
    uexpr e = uexpr_parse(stdin);
    if (uexpr_eval(e, NULL, &test_get, &test_set)) return 0;
    else return 1;
}
