/*
=== Grammar ===
(* basic definitions *)
char = ? any character ? ;

(* string literal *)
str = ? [a-zA-Z0-9-_] ? ;
str = "\"", { char - "\"" }, "\"" ;

expr = str ; (* literal *)
expr = "(", expr, ")" ; (* parenthesized expression *)

expr = "[", expr, { ",", expr }, "]" | "[", "]" ; (* list *)
expr = "{", expr, { ";", expr }, "}" | "{", "}"; (* block of expressions *)
expr = str, "(", expr, { ",", expr }, ")" | str, "(", ")" ; (* function call *)

expr = "$", str ; (* variable *)

unary-op = "~" ;
binary-op = "&", "|", "=", "%" ;
expr = unary-op, expr ;
expr = expr, binary-op, expr ;

grammar = expr

=== Semantics ===

# Types
Every expression has a type: string, boolean, list, or void. Literal `str` has
type string. Additionally, variable expressions are special in some cases.

## string
Any Unicode string, excluding null characters.

## boolean
Has the values True and False.

## list
An ordered list of heterogenous values.

## void
Has the only value Void.

# Operators
- "=" operator: String equality. Both operands must have type string. Result is
  boolean.
- "&", "|" operators: Logical and and or, respectively. First operand must have
  type boolean. Result is boolean, or the second argument. (Short circuiting.)
- "%" operator: Operands must be string and list. Tests set membership. Result
  is boolean.
- "~" operator: Logical negation. Operand must be boolean. Result is boolean.

# Expression block
- Every expression is evaluated in sequence.
- Result is the last expression's result.

# Functions

## Function "let", 2 arguments.
- The first argument must be a variable. The variable's value and type will be
  set to that of the evaluated second operand. Only strings and booleans can be
  stored.

## Function "apply", 2 arguments.
- The first argument is a list. Evaluates the second expression for each
  element (with setting the variable $i to the current element), and returns the
  resulting list.

## Function "print"
- The string arguments are printed as-is, others in a canonical form to the
  standard output, line-by-line.

## Function "startsw", 2 arguments.
- Tests whether the first argument starts with the second. (Both must be type
  string.)
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <ds/vec.h>
#include <ds/hashmap.h>

#include "core.h"
#include "uexpr.h"

/* # Parsing */
typedef struct uexpr_ast_node ast_node;
typedef struct {
	char c; // 0: string; 1: end; 2: error
	struct str s;
} token;
struct parser_state {
	FILE *f;
	token buf;
	struct vec ast;
};
typedef struct parser_state *st;

static void ast_node_finish(ast_node *node) {
	switch (node->op) {
	case UEXPR_OP_LIT:
	case UEXPR_OP_VAR:
		str_free(&node->str);
		break;
	case UEXPR_OP_FN:
		str_free(&node->str);
		vec_free(&node->args);
		break;
	case UEXPR_OP_LIST:
	case UEXPR_OP_BLOCK:
		vec_free(&node->args);
		break;
	case UEXPR_OP_AND:
	case UEXPR_OP_OR:
	case UEXPR_OP_EQ:
	case UEXPR_OP_IN:
		vec_free(&node->args);
		break;
	case UEXPR_OP_NEG:
		vec_free(&node->args);
		break;
	}
}

static bool is_ident_char(char c) {
	return
		('0' <= c && c <= '9') |
		('A' <= c && c <= 'Z') |
		('a' <= c && c <= 'z') |
		c == '-' | c == '_';
}

/* ## Tokenizer */
static token next_token(FILE *f) {
	int c;
	while (c = getc(f), isspace(c));
	struct str s;
	if (c < 0) {
		// EOF
		return (token){ .c = 1 };
	}
	if (c != 0 && strchr("()[]{},$~&|=%;,", c)) {
		return (token){ .c = c };
	}
	if (c == '"') {
		// start of string
		s = str_new_empty();
		while (1) {
			c = getc(f);
			if (c == '\\') {
				c = getc(f);
				if (c != '"') return (token){ .c = 2 };
			} else if (c == '"') {
				break;
			}
			if (c == '\0' || c < 0) {
				// filter null characters and EOF
				str_free(&s);
				return (token){ .c = 2 };
			}
			str_append_char(&s, c);
		}
		return (token){ .c = 0, s = s };
	}
	if (is_ident_char(c)) {
		s = str_new_empty();
		str_append_char(&s, c);
		while (1) {
			c = getc(f);
			if (is_ident_char(c)) {
				str_append_char(&s, c);
			} else {
				ungetc(c, f);
				return (token){ .c = 0, .s = s };
			}
		}
	}
	return (token){ .c = 2 };
}

/* ## Actual parser */
static struct parser_state new_parser(FILE *f, struct vec ast) {
	return (struct parser_state){
		.f = f,
		.buf = next_token(f),
		.ast = ast
	};
}
static token peek(st s) {
	return s->buf;
}
static token get(st s) {
	token t = s->buf;
	s->buf = next_token(s->f);
	return t;
}
static int expr(st s);
static int term(st s);
static int list(st s, enum uexpr_op op) {
	char sep, end;
	switch (op) {
	case UEXPR_OP_LIST:
		sep = ','; end = ']';
		break;
	case UEXPR_OP_BLOCK:
		sep = ';'; end = '}';
		break;
	case UEXPR_OP_FN:
		sep = ','; end = ')';
		break;
	case UEXPR_OP_LIT:
	case UEXPR_OP_VAR:
	case UEXPR_OP_NEG:
	case UEXPR_OP_AND:
	case UEXPR_OP_OR:
	case UEXPR_OP_EQ:
	case UEXPR_OP_IN:
		asrt(false, "shut up compiler warning");
		break;
	}
	ast_node n = (ast_node){ .op = op, .args = vec_new_empty(sizeof(int)) };
	token t = peek(s);
	if (t.c != end) {
		while (1) {
			t = peek(s);
			int i = term(s);
			if (i == -1) return -1; // TODO: leak
			vec_append(&n.args, &i);
			t = get(s);
			if (t.c == end) break;
			if (t.c != sep)
				return -1; // TODO: leak
		}
	} else {
		t = get(s);
		asrt(t.c == end, "parsing bad");
	}
	return vec_append(&s->ast, &n);
}
static int term(st s) {
	int a = expr(s);
	if (a == -1) return -1;
	token t = peek(s);
	if (t.c != 0 && strchr("&|=%", t.c)) {
		get(s);
		int b = term(s);
		if (b == -1) return -1;
		ast_node n = (ast_node){ .args = vec_new_empty(sizeof(int)) };
		switch (t.c) {
		case '&': n.op = UEXPR_OP_AND; break;
		case '|': n.op = UEXPR_OP_OR; break;
		case '=': n.op = UEXPR_OP_EQ; break;
		case '%': n.op = UEXPR_OP_IN; break;
		}
		vec_append(&n.args, &a);
		vec_append(&n.args, &b);
		return vec_append(&s->ast, &n);
	}
	return a;
}
static int expr(st s) {
	token t = peek(s);
	if (t.c == 2) {
		return -1;
	}
	if (t.c == '(') {
		get(s);
		// paren expr
		int i = term(s);
		if (i == -1) return -1;
		t = get(s);
		if (t.c != ')') return -1;
		return i;
	}
	if (t.c == '[') {
		get(s);
		return list(s, UEXPR_OP_LIST);
	}
	if (t.c == '{') {
		get(s);
		return list(s, UEXPR_OP_BLOCK);
	}
	if (t.c == 0) {
		get(s);
		token t2 = peek(s);
		if (t2.c == '(') {
			// function call
			get(s);
			int i = list(s, UEXPR_OP_FN);
			if (i == -1) return -1;
			ast_node *np = vec_get(&s->ast, i);
			np->str = t.s;
			return i;
		} else {
			// string literal
			ast_node n = (ast_node){ .op = UEXPR_OP_LIT, .str = t.s };
			return vec_append(&s->ast, &n);
		}
	}
	if (t.c == '$') {
		get(s);
		t = get(s);
		if (t.c != 0) return -1;
		ast_node n = (ast_node){ .op = UEXPR_OP_VAR, .str = t.s };
		return vec_append(&s->ast, &n);
	}
	if (t.c == '~') {
		get(s);
		int i = expr(s);
		if (i == -1) return -1;
		ast_node n = (ast_node){
			.op = UEXPR_OP_NEG, .args = vec_new_empty(sizeof(int))
		};
		vec_append(&n.args, &i);
		return vec_append(&s->ast, &n);
	}
	return -1;
}

/* # Value stuff */
static const struct uexpr_value error_val = { .type = UEXPR_TYPE_ERROR };
static const struct uexpr_value void_val = { .type = UEXPR_TYPE_VOID };
struct uexpr_ctx {
	struct hashmap vars; /* hashmap<struct uexpr_value> */
	void *cl;
	struct uexpr_ops ops;
};
void uexpr_value_finish(struct uexpr_value v) {
	switch (v.type) {
	case UEXPR_TYPE_STRING:
		break;
	case UEXPR_TYPE_LIST:
		for (int i = 0; i < v.list.len; ++i) {
			struct uexpr_value *vp = vec_get(&v.list, i);
			uexpr_value_finish(*vp);
		}
		vec_free(&v.list);
		break;
	case UEXPR_TYPE_NATIVEOBJ:
		v.nativeobj.ref(v.nativeobj.self, -1);
		break;
	case UEXPR_TYPE_BOOLEAN:
	case UEXPR_TYPE_VOID:
	case UEXPR_TYPE_NATIVEFN:
	case UEXPR_TYPE_ERROR:
		break;
	}
}
struct uexpr_value uexpr_value_copy(const struct uexpr_value *v) {
	struct uexpr_value res;
	switch (v->type) {
	case UEXPR_TYPE_LIST:
		res.type = UEXPR_TYPE_LIST;
		res.list = vec_new_empty(sizeof(struct uexpr_value));
		for (int i = 0; i < v->list.len; ++i) {
			const struct uexpr_value *vp = vec_get_c(&v->list, i);
			struct uexpr_value resi = uexpr_value_copy(vp);
			vec_append(&res.list, &resi);
		}
		return res;
	case UEXPR_TYPE_NATIVEOBJ:
		v->nativeobj.ref(v->nativeobj.self, 1);
	case UEXPR_TYPE_STRING:
	case UEXPR_TYPE_BOOLEAN:
	case UEXPR_TYPE_VOID:
	case UEXPR_TYPE_NATIVEFN:
	case UEXPR_TYPE_ERROR:
		return *v;
	}
	asrt(false, "");
	return *v; // shut up compiler
}
static void print_value(FILE *f, struct uexpr_value v);

/* # Evaluation */
static struct uexpr_value eval(struct uexpr *e, int root, struct uexpr_ctx *ctx);
static struct uexpr_value get_var(struct uexpr_ctx *ctx, const char *key) {
	struct uexpr_ops *ops = &ctx->ops;
	struct uexpr_value v, *vp;
	if (ops->try_get_var && ops->try_get_var(ops->env, key, &v)) {
		return v;
	}
	if (hashmap_get(&ctx->vars, key, (void**)&vp) == MAP_OK) {
		if (vp->type == UEXPR_TYPE_STRING
				|| vp->type == UEXPR_TYPE_BOOLEAN
				|| vp->type == UEXPR_TYPE_NATIVEFN
				|| vp->type == UEXPR_TYPE_NATIVEOBJ) {
			return uexpr_value_copy(vp);
		}
	}
	return error_val;
}
void uexpr_set_var(struct uexpr_ctx *ctx, const char *key, struct uexpr_value val) {
	struct uexpr_ops *ops = &ctx->ops;
	if (ops->try_set_var && ops->try_set_var(ops->env, key, val)) {
		return;
	}

	struct uexpr_value *vp;
	if (hashmap_get(&ctx->vars, key, (void**)&vp) == MAP_OK) {
		uexpr_value_finish(*vp);
		hashmap_remove(&ctx->vars, key);
	}
	hashmap_put(&ctx->vars, key, &val);
}

/* ## Builtin functions */
static struct uexpr_value fn_let(struct uexpr *e, int root, struct uexpr_ctx *ctx) {
	ast_node np = *(ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 2) return error_val;
	ast_node na = *(ast_node *)vec_get(&e->ast, *(int*)vec_get(&np.args, 0));
	if (na.op != UEXPR_OP_VAR) return error_val;
	const char *key = str_cstr(&na.str);
	struct uexpr_value vb = eval(e, *(int*)vec_get(&np.args, 1), ctx);
	if (vb.type != UEXPR_TYPE_STRING && vb.type != UEXPR_TYPE_BOOLEAN) {
		uexpr_value_finish(vb);
		return error_val;
	}
	uexpr_set_var(ctx, key, vb);
	return void_val;
}
static struct uexpr_value fn_apply(struct uexpr *e, int root, struct uexpr_ctx *ctx) {
	ast_node np = *(ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 2) return error_val;
	struct uexpr_value va = eval(e, *(int*)vec_get(&np.args, 0), ctx);
	if (va.type != UEXPR_TYPE_LIST) {
		uexpr_value_finish(va);
		return error_val;
	}
	int *ib = vec_get(&np.args, 1);
	struct uexpr_value res = (struct uexpr_value){
		.type = UEXPR_TYPE_LIST, .list = vec_new_empty(sizeof(struct uexpr_value))
	};
	for (int i = 0; i < va.list.len; ++i) {
		struct uexpr_value r = *(struct uexpr_value*)vec_get(&va.list, i);
		uexpr_set_var(ctx, "i", r);
		r = eval(e, *ib, ctx);
		vec_append(&res.list, &r);
	}
	// no uexpr_value_finish(va) since we took all elements out of it...
	return res;
}
static struct uexpr_value fn_startsw(struct uexpr *e, int root, struct uexpr_ctx *ctx) {
	ast_node np = *(ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 2) return error_val;
	struct uexpr_value va = eval(e, *(int*)vec_get(&np.args, 0), ctx);
	struct uexpr_value vb = eval(e, *(int*)vec_get(&np.args, 1), ctx);
	struct uexpr_value res = error_val;
	if (va.type == UEXPR_TYPE_STRING && vb.type == UEXPR_TYPE_STRING) {
		res = (struct uexpr_value){
			.type = UEXPR_TYPE_BOOLEAN,
			.boolean = strncmp(va.string_ref, vb.string_ref,
					strlen(vb.string_ref)) == 0
		};
	}
	uexpr_value_finish(va);
	uexpr_value_finish(vb);
	return res;
}
static struct uexpr_value fn_print(struct uexpr *e, int root, struct uexpr_ctx *ctx) {
	ast_node np = *(ast_node *)vec_get(&e->ast, root);
	for (int i = 0; i < np.args.len; ++i) {
		struct uexpr_value vi = eval(e, *(int*)vec_get(&np.args, i), ctx);
		if (vi.type == UEXPR_TYPE_STRING) {
			fprintf(stdout, "%s\n", vi.string_ref);
		} else {
			print_value(stdout, vi);
			fprintf(stdout, "\n");
		}
		uexpr_value_finish(vi);
	}
	return void_val;
}
struct builtin_fn {
	const char *name;
	struct uexpr_value (*f)(struct uexpr *e, int root, struct uexpr_ctx *ctx);
};
static struct builtin_fn builtin_fns[] = {
	{ "let", &fn_let },
	{ "apply", &fn_apply },
	{ "print", &fn_print },
	{ "startsw", &fn_startsw },
	{ NULL, NULL }
};

/* ## Evaluation logic */
static struct uexpr_value eval(struct uexpr *e, int root, struct uexpr_ctx *ctx) {
	struct uexpr_value res, *vp;
	ast_node np = *(ast_node *)vec_get(&e->ast, root);
	switch (np.op) {
	case UEXPR_OP_LIT: return (struct uexpr_value){
		.type = UEXPR_TYPE_STRING, .string_ref = str_cstr(&np.str)
	};
	case UEXPR_OP_LIST:
		res = (struct uexpr_value){
			.type = UEXPR_TYPE_LIST, .list = vec_new_empty(sizeof(struct uexpr_value))
		};
		for (int i = 0; i < np.args.len; ++i) {
			int *ni = vec_get(&np.args, i);
			struct uexpr_value r = eval(e, *ni, ctx);
			vec_append(&res.list, &r);
		}
		return res;
	case UEXPR_OP_BLOCK:
		res = void_val;
		for (int i = 0; i < np.args.len; ++i) {
			int *ni = vec_get(&np.args, i);
			uexpr_value_finish(res);
			res = eval(e, *ni, ctx);
		}
		return res;
	case UEXPR_OP_FN: {
		/* try builtin functions */
		struct builtin_fn *fn = builtin_fns;
		while (fn->name) {
			if (strcmp(fn->name, str_cstr(&np.str)) == 0) {
				return fn->f(e, root, ctx);
			}
			++fn;
		}

		/* try calling the variable with the same name */
		struct uexpr_value v = get_var(ctx, str_cstr(&np.str));
		if (v.type == UEXPR_TYPE_NATIVEFN) {
			return v.nativefn.f(v.nativefn.env, e, root, ctx);
		}

		return error_val;
	}
	case UEXPR_OP_VAR:
		return get_var(ctx, str_cstr(&np.str));
	case UEXPR_OP_NEG: {
		int *ap = vec_get(&np.args, 0);
		struct uexpr_value v = eval(e, *ap, ctx);
		if (v.type == UEXPR_TYPE_BOOLEAN) {
			res = (struct uexpr_value){
				.type = UEXPR_TYPE_BOOLEAN, .boolean = !v.boolean
			};
		} else {
			res = error_val;
		}
		uexpr_value_finish(v);
		return res;
	}
	case UEXPR_OP_AND:
	case UEXPR_OP_OR: {
		int *ap = vec_get(&np.args, 0);
		struct uexpr_value v = eval(e, *ap, ctx);
		if (v.type == UEXPR_TYPE_BOOLEAN) {
			if (np.op == UEXPR_OP_AND ? v.boolean : !v.boolean) {
				int *bp = vec_get(&np.args, 1);
				res = eval(e, *bp, ctx);
			} else {
				res = (struct uexpr_value){
					.type = UEXPR_TYPE_BOOLEAN,
					.boolean = np.op == UEXPR_OP_OR
				};
			}
		} else {
			res = error_val;
		}
		uexpr_value_finish(v);
		return res;
	}
	case UEXPR_OP_EQ:
	case UEXPR_OP_IN: {
		int *ap = vec_get(&np.args, 0);
		int *bp = vec_get(&np.args, 1);
		struct uexpr_value va = eval(e, *ap, ctx);
		struct uexpr_value vb = eval(e, *bp, ctx);
		if (np.op == UEXPR_OP_EQ) {
			if (va.type == UEXPR_TYPE_STRING && vb.type == UEXPR_TYPE_STRING) {
				res = (struct uexpr_value){
					.type = UEXPR_TYPE_BOOLEAN,
					.boolean = strcmp(va.string_ref, vb.string_ref) == 0
				};
			} else if (va.type == UEXPR_TYPE_NATIVEOBJ && vb.type == UEXPR_TYPE_NATIVEOBJ) {
				res = (struct uexpr_value){
					.type = UEXPR_TYPE_BOOLEAN,
					.boolean = va.nativeobj.self == vb.nativeobj.self
				};
			} else {
				res = error_val;
			}
		} else {
			if (vb.type == UEXPR_TYPE_LIST) {
				res = (struct uexpr_value){ .type = UEXPR_TYPE_BOOLEAN, .boolean = false };
				for (int i = 0; i < vb.list.len; ++i) {
					vp = vec_get(&vb.list, i);
					if (vp->type == UEXPR_TYPE_STRING && va.type == UEXPR_TYPE_STRING) {
						if (strcmp(va.string_ref, vp->string_ref) == 0) {
							uexpr_value_finish(res);
							res = (struct uexpr_value){ .type = UEXPR_TYPE_BOOLEAN,
								.boolean = true };
							break;
						}
					} else if (vp->type == UEXPR_TYPE_NATIVEOBJ && va.type == UEXPR_TYPE_NATIVEOBJ) {
						if (va.nativeobj.self == vp->nativeobj.self) {
							uexpr_value_finish(res);
							res = (struct uexpr_value){ .type = UEXPR_TYPE_BOOLEAN,
								.boolean = true };
							break;
						}
					}
				}
			} else {
				res = error_val;
			}
		}
		uexpr_value_finish(va);
		uexpr_value_finish(vb);
		return res;
	}
	}
	asrt(false, "fuck u compiler");
	return error_val;
}

/* # Debug printing stuff */
static void dump_ast(FILE *f, struct vec ast, int root) {
	char start, sep, end;
	int *ni;
	ast_node *np = vec_get(&ast, root);
	switch (np->op) {
	case UEXPR_OP_LIT:
		fprintf(f, "\"%s\"", str_cstr(&np->str));
		break;
	case UEXPR_OP_LIST:
	case UEXPR_OP_BLOCK:
	case UEXPR_OP_FN:
		switch (np->op) {
		case UEXPR_OP_LIST:
			start = '['; sep = ','; end = ']';
			break;
		case UEXPR_OP_BLOCK:
			start = '{'; sep = ';'; end = '}';
			break;
		case UEXPR_OP_FN:
			start = '('; sep = ','; end = ')';
			break;
		case UEXPR_OP_LIT:
		case UEXPR_OP_VAR:
		case UEXPR_OP_NEG:
		case UEXPR_OP_AND:
		case UEXPR_OP_OR:
		case UEXPR_OP_EQ:
		case UEXPR_OP_IN:
			asrt(false, "shut up compiler warning");
			break;
		}
		if (np->op == UEXPR_OP_FN) fprintf(f, "%s", str_cstr(&np->str));
		fprintf(f, "%c", start);
		for (int i = 0; i < np->args.len; ++i) {
			int *ni = vec_get(&np->args, i);
			dump_ast(f, ast, *ni);
			if (i < np->args.len - 1) fprintf(f, "%c ", sep);
		}
		fprintf(f, "%c", end);
		break;
	case UEXPR_OP_VAR:
		fprintf(f, "$%s", str_cstr(&np->str));
		break;
	case UEXPR_OP_NEG:
		fprintf(f, "~");
		ni = vec_get(&np->args, 0);
		dump_ast(f, ast, *ni);
		break;
	case UEXPR_OP_AND:
	case UEXPR_OP_OR:
	case UEXPR_OP_EQ:
	case UEXPR_OP_IN:
		fprintf(f, "(");
		ni = vec_get(&np->args, 0);
		dump_ast(f, ast, *ni);
		switch (np->op) {
		case UEXPR_OP_AND: fprintf(f, "&"); break;
		case UEXPR_OP_OR: fprintf(f, "|"); break;
		case UEXPR_OP_EQ: fprintf(f, "="); break;
		case UEXPR_OP_IN: fprintf(f, "%%"); break;
		case UEXPR_OP_LIT:
		case UEXPR_OP_LIST:
		case UEXPR_OP_BLOCK:
		case UEXPR_OP_FN:
		case UEXPR_OP_VAR:
		case UEXPR_OP_NEG:
			asrt(false, "shut up compiler warning");
			break;
		}
		ni = vec_get(&np->args, 1);
		dump_ast(f, ast, *ni);
		fprintf(f, ")");
		break;
	}
}
static void print_value(FILE *f, struct uexpr_value v) {
	switch (v.type) {
	case UEXPR_TYPE_STRING:
		fprintf(f, "\"%s\"", v.string_ref);
		break;
	case UEXPR_TYPE_LIST:
		fprintf(f, "[");
		for (int i = 0; i < v.list.len; ++i) {
			struct uexpr_value *vp = vec_get(&v.list, i);
			print_value(f, *vp);
			if (i < v.list.len - 1) fprintf(f, ", ");
		}
		fprintf(f, "]");
		break;
	case UEXPR_TYPE_BOOLEAN:
		fprintf(f, "%s", v.boolean ? "True" : "False");
		break;
	case UEXPR_TYPE_VOID:
		fprintf(f, "Void");
		break;
	case UEXPR_TYPE_NATIVEOBJ:
		fprintf(f, "Native(%p, %p)\n",
			v.nativeobj.self, v.nativeobj.ref);
		break;
	case UEXPR_TYPE_NATIVEFN:
		fprintf(f, "NativeFn(%p, %p)\n", v.nativefn.f, v.nativefn.env);
		break;
	case UEXPR_TYPE_ERROR:
		fprintf(f, "Error");
		break;
	}
}

/* # Public functions */
void uexpr_init(struct uexpr *e) {
	*e = (struct uexpr){ .ast = vec_new_empty(sizeof(ast_node)) };
}
int uexpr_parse(struct uexpr *e, FILE *f) {
	struct parser_state ps = new_parser(f, e->ast);
	int root = term(&ps);
	if (root == -1) {
		// TODO: some junk gets left in e->ast
		return -1;
	}
	e->ast = ps.ast;
	return root;
}
static int iter_finish_ctx_vars(void *_cl, void *item) {
	struct uexpr_value *vp = item;
	uexpr_value_finish(*vp);
	return MAP_OK;
}
struct uexpr_ctx *uexpr_ctx_create() {
	struct uexpr_ctx *ctx = malloc_check(sizeof(struct uexpr_ctx));
	hashmap_init(&ctx->vars, sizeof(struct uexpr_value));
	return ctx;
}
void uexpr_ctx_set_ops(struct uexpr_ctx *ctx, struct uexpr_ops ops) {
	ctx->ops = ops;
}
void uexpr_ctx_destroy(struct uexpr_ctx *ctx) {
	hashmap_iterate(&ctx->vars, &iter_finish_ctx_vars, NULL);
	hashmap_finish(&ctx->vars);
	free(ctx);
}
void uexpr_eval(struct uexpr *e, int root, struct uexpr_ctx *ctx, struct uexpr_value *v_out) {
	struct uexpr_value val = eval(e, root, ctx);
	if (v_out) *v_out = val;
	else uexpr_value_finish(val);
}
void uexpr_print(struct uexpr *e, int root, FILE *f) {
	dump_ast(f, e->ast, root);
}
void uexpr_finish(struct uexpr *e) {
	for (int i = 0; i < e->ast.len; ++i) {
		ast_node_finish((ast_node*)vec_get(&e->ast, i));
	}
	vec_free(&e->ast);
}
