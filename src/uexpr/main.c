
#include <string.h>

#include "uexpr.h"

static uexpr_val test_get(void *cl, const char *key) {
    if (strcmp(key, "test") == 0) return uexpr_create_string("hello world");
    return NULL;
}
static bool test_set(void *cl, const char *key, uexpr_val val) {
    return false;
}
int main(int argc, char **argv) {
    uexpr e = uexpr_parse(stdin);
    uexpr_ctx ctx = uexpr_ctx_create(e, &test_get, &test_set);
    bool res = uexpr_eval(ctx, NULL);
    if (argc > 1) {
        uexpr_fn fn = uexpr_ctx_get_fn(ctx, argv[1]);
        res = uexpr_eval_fn(ctx, NULL, fn);
    }
    return res ? 0 : 1;
}
