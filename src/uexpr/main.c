
#include <string.h>

#include "uexpr.h"

struct uexpr_value fn_test(void *env, struct vec *ast, int root, struct uexpr_ctx *ctx) {
	struct uexpr_ast_node *np = vec_get(ast, root);

	fprintf(stderr, "%s called!\n", __func__);

	if (np->args.len == 1) {
		int arg = *(int *)vec_get(&np->args, 0);
		uexpr_eval(ctx, arg, NULL);
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
	struct uexpr *e = uexpr_parse(stdin);
	struct uexpr_ctx *ctx = uexpr_ctx_create(e);
	uexpr_ctx_set_ops(ctx, (struct uexpr_ops){
		.env = NULL,
		.try_get_var = try_get_var,
		.try_set_var = try_set_var
	});
	uexpr_eval(ctx, e->root, NULL);
}
