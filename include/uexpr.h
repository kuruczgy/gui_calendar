#ifndef GUI_CALENDAR_UEXPR_H
#define GUI_CALENDAR_UEXPR_H
#include <stdbool.h>
#include <stdio.h>
#include <ds/vec.h>

enum uexpr_op {
	UEXPR_OP_LIT,
	UEXPR_OP_LIST,
	UEXPR_OP_BLOCK,
	UEXPR_OP_FN,
	UEXPR_OP_VAR,
	UEXPR_OP_NEG,
	UEXPR_OP_AND,
	UEXPR_OP_OR,
	UEXPR_OP_EQ,
	UEXPR_OP_IN
};
struct uexpr_ast_node {
	struct vec args;
	enum uexpr_op op;
	struct str str;
};
struct uexpr {
	struct vec ast; /* vec<struct uexpr_ast_node> */
	int root;
};
struct uexpr_ctx;

enum uexpr_type {
	UEXPR_TYPE_STRING,
	UEXPR_TYPE_BOOLEAN,
	UEXPR_TYPE_LIST,
	UEXPR_TYPE_VOID,
	UEXPR_TYPE_NATIVEPTR,
	UEXPR_TYPE_NATIVEFN,
	UEXPR_TYPE_ERROR
};
typedef struct uexpr_value (*uexpr_nativefn)(
	void *env, struct vec *ast, int root, struct uexpr_ctx *ctx);
struct uexpr_value {
	enum uexpr_type type;
	union {
		const char *string_ref;
		bool boolean;
		struct vec list; /* vec<struct uexpr_value> */
		void *nativeptr;
		struct { uexpr_nativefn f; void *env; } nativefn;
	};
};
void uexpr_value_finish(struct uexpr_value v);

#define UEXPR_BOOLEAN(x) (struct uexpr_value){ .type = UEXPR_TYPE_BOOLEAN, .boolean = (x) }
#define UEXPR_STRING(x) (struct uexpr_value){ .type = UEXPR_TYPE_STRING, .string_ref = (x) }

struct uexpr_ops {
	void *env;

	/* ownership of value is transfered IFF true is returned */
	bool (*try_get_var)(void *env, const char *key, struct uexpr_value *v);
	bool (*try_set_var)(void *env, const char *key, struct uexpr_value v);
};

struct uexpr *uexpr_parse(FILE *f);
void uexpr_print(struct uexpr *e, FILE *f);
void uexpr_destroy(struct uexpr *e);
struct uexpr_ctx *uexpr_ctx_create(struct uexpr *e);
void uexpr_ctx_set_ops(struct uexpr_ctx *ctx, struct uexpr_ops ops);
void uexpr_ctx_destroy(struct uexpr_ctx *ctx);
void uexpr_eval(struct uexpr_ctx *ctx, int i, struct uexpr_value *v_out);
void uexpr_set_var(struct uexpr_ctx *ctx, const char *key, struct uexpr_value val);

#endif
