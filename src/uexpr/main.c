
#include <string.h>

#include "uexpr.h"

struct uexpr_value fn_test(void *env, struct uexpr *e, int root, struct uexpr_ctx *ctx) {
	struct uexpr_ast_node *np = vec_get(&e->ast, root);

	fprintf(stderr, "%s called!\n", __func__);

	if (np->args.len == 1) {
		int arg = *(int *)vec_get(&np->args, 0);
		uexpr_eval(e, arg, ctx, NULL);
	}

	return (struct uexpr_value) { .type = UEXPR_TYPE_VOID };
}

static bool try_get_var(void *env, const char *key, struct uexpr_value *v) {
	if (strcmp(key, "test") == 0) {
		*v = (struct uexpr_value) {
			.type = UEXPR_TYPE_NATIVEFN,
			.nativefn = { .f = fn_test, .env = NULL }
		};
		return true;
	}
	return false;
}
static bool try_set_var(void *env, const char *key, struct uexpr_value v) {
	return false;
}
int main(int argc, char **argv) {
	struct uexpr e;
	int root = uexpr_parse(&e, stdin);
	struct uexpr_ctx *ctx = uexpr_ctx_create();
	uexpr_ctx_set_ops(ctx, (struct uexpr_ops){
		.env = NULL,
		.try_get_var = try_get_var,
		.try_set_var = try_set_var
	});
	if (root != -1) {
		uexpr_eval(&e, root, ctx, NULL);
	} else {
		fprintf(stderr, "failed to parse!\n");
	}
	uexpr_ctx_destroy(ctx);
	uexpr_finish(&e);
}
