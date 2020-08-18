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

## Function "def", 2 arguments.
- Defines a new function. (First argument is the string name, second is the
  expression body.)
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
enum op {
	OP_LIT,
	OP_LIST,
	OP_BLOCK,
	OP_FN,
	OP_VAR,
	OP_NEG,
	OP_AND,
	OP_OR,
	OP_EQ,
	OP_IN
};
typedef struct {
	struct vec args;
	enum op op;
	struct str str;
} ast_node;
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
	case OP_LIT:
	case OP_VAR:
		str_free(&node->str);
		break;
	case OP_FN:
		str_free(&node->str);
		vec_free(&node->args);
		break;
	case OP_LIST:
	case OP_BLOCK:
		vec_free(&node->args);
		break;
	case OP_AND:
	case OP_OR:
	case OP_EQ:
	case OP_IN:
		vec_free(&node->args);
		break;
	case OP_NEG:
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
static struct parser_state new_parser(FILE *f) {
	return (struct parser_state){
		.f = f,
		.buf = next_token(f),
		.ast = vec_new_empty(sizeof(ast_node))
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
static int list(st s, enum op op) {
	char sep, end;
	switch (op) {
	case OP_LIST:
		sep = ','; end = ']';
		break;
	case OP_BLOCK:
		sep = ';'; end = '}';
		break;
	case OP_FN:
		sep = ','; end = ')';
		break;
	case OP_LIT:
	case OP_VAR:
	case OP_NEG:
	case OP_AND:
	case OP_OR:
	case OP_EQ:
	case OP_IN:
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
		case '&': n.op = OP_AND; break;
		case '|': n.op = OP_OR; break;
		case '=': n.op = OP_EQ; break;
		case '%': n.op = OP_IN; break;
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
		return list(s, OP_LIST);
	}
	if (t.c == '{') {
		get(s);
		return list(s, OP_BLOCK);
	}
	if (t.c == 0) {
		get(s);
		token t2 = peek(s);
		if (t2.c == '(') {
			// function call
			get(s);
			int i = list(s, OP_FN);
			if (i == -1) return -1;
			ast_node *np = vec_get(&s->ast, i);
			np->str = t.s;
			return i;
		} else {
			// string literal
			ast_node n = (ast_node){ .op = OP_LIT, .str = t.s };
			return vec_append(&s->ast, &n);
		}
	}
	if (t.c == '$') {
		get(s);
		t = get(s);
		if (t.c != 0) return -1;
		ast_node n = (ast_node){ .op = OP_VAR, .str = t.s };
		return vec_append(&s->ast, &n);
	}
	if (t.c == '~') {
		get(s);
		int i = expr(s);
		if (i == -1) return -1;
		ast_node n = (ast_node){
			.op = OP_NEG, .args = vec_new_empty(sizeof(int))
		};
		vec_append(&n.args, &i);
		return vec_append(&s->ast, &n);
	}
	return -1;
}

/* # Value stuff */
enum type {
	TYPE_STRING,
	TYPE_BOOLEAN,
	TYPE_LIST,
	TYPE_VOID,
	TYPE_ERROR
};
struct value {
	enum type type;
	union {
		const char *string_ref;
		bool boolean;
		struct vec list;
	};
};
static const struct value error_val = { .type = TYPE_ERROR };
static const struct value void_val = { .type = TYPE_VOID };
struct uexpr_impl {
	struct vec ast;
	int root;
};
struct fn_def {
	const char *name;
	int i;
};
struct context {
	struct hashmap vars; /* hashmap<struct value> */
	struct hashmap defs_map; /* hashmap<struct fn_def> */
	void *cl;
	uexpr_get get;
	uexpr_set set;
	struct uexpr_impl *uexpr;
};
static void value_finish(struct value v) {
	switch (v.type) {
	case TYPE_STRING:
		break;
	case TYPE_LIST:
		for (int i = 0; i < v.list.len; ++i) {
			struct value *vp = vec_get(&v.list, i);
			value_finish(*vp);
		}
		vec_free(&v.list);
		break;
	case TYPE_BOOLEAN:
	case TYPE_VOID:
	case TYPE_ERROR:
		break;
	}
}
static struct value value_copy(struct value *v) {
	struct value res;
	switch (v->type) {
	case TYPE_STRING:
		res.type = TYPE_STRING;
		res.string_ref = v->string_ref;
		return res;
	case TYPE_LIST:
		res.type = TYPE_LIST;
		res.list = vec_new_empty(sizeof(struct value));
		for (int i = 0; i < v->list.len; ++i) {
			struct value *vp = vec_get(&v->list, i);
			struct value resi = value_copy(vp);
			vec_append(&res.list, &resi);
		}
		return res;
	case TYPE_BOOLEAN:
	case TYPE_VOID:
	case TYPE_ERROR:
		return *v;
	}
	asrt(false, "");
	return *v; // shut up compiler
}
static void print_value(FILE *f, struct value v);

/* # Evaluation */
static struct value eval(struct vec *ast, int root, struct context *ctx);
static struct value get_var(struct context *ctx, const char *key) {
	struct value *vp;
	if (ctx->get && (vp = (struct value *)ctx->get(ctx->cl, key))) {
		struct value res = *vp;
		free(vp);
		return res;
	}
	if (hashmap_get(&ctx->vars, key, (void**)&vp) == MAP_OK) {
		if (vp->type == TYPE_STRING) {
			return value_copy(vp);
		} else if (vp->type == TYPE_BOOLEAN) {
			return value_copy(vp);
		}
	}
	return error_val;
}
static void set_var(struct context *ctx, const char *key, struct value val) {
	if (val.type == TYPE_STRING || val.type == TYPE_BOOLEAN) {
		if (ctx->set && ctx->set(ctx->cl, key, (void*)&val)) {
			value_finish(val);
			return;
		}
	}
	struct value *vp;
	if (hashmap_get(&ctx->vars, key, (void**)&vp) == MAP_OK) {
		value_finish(*vp);
		hashmap_remove(&ctx->vars, key);
	}
	hashmap_put(&ctx->vars, key, &val);
}

/* ## Builtin functions */
static struct value fn_let(struct vec *ast, int root, struct context *ctx) {
	ast_node *np = vec_get(ast, root);
	if (np->args.len != 2) return error_val;
	ast_node *na = vec_get(ast, *(int*)vec_get(&np->args, 0));
	if (na->op != OP_VAR) return error_val;
	const char *key = str_cstr(&na->str);
	struct value vb = eval(ast, *(int*)vec_get(&np->args, 1), ctx);
	if (vb.type != TYPE_STRING && vb.type != TYPE_BOOLEAN) {
		value_finish(vb);
		return error_val;
	}
	set_var(ctx, key, vb);
	return void_val;
}
static struct value fn_apply(struct vec *ast, int root, struct context *ctx) {
	ast_node *np = vec_get(ast, root);
	if (np->args.len != 2) return error_val;
	struct value va = eval(ast, *(int*)vec_get(&np->args, 0), ctx);
	if (va.type != TYPE_LIST) {
		value_finish(va);
		return error_val;
	}
	int *ib = vec_get(&np->args, 1);
	struct value res = (struct value){
		.type = TYPE_LIST, .list = vec_new_empty(sizeof(struct value))
	};
	for (int i = 0; i < va.list.len; ++i) {
		struct value r = *(struct value*)vec_get(&va.list, i);
		set_var(ctx, "i", r);
		r = eval(ast, *ib, ctx);
		vec_append(&res.list, &r);
	}
	// no value_finish(va) since we took all elements out of it...
	return res;
}
static struct value fn_startsw(struct vec *ast, int root, struct context *ctx) {
	ast_node *np = vec_get(ast, root);
	if (np->args.len != 2) return error_val;
	struct value va = eval(ast, *(int*)vec_get(&np->args, 0), ctx);
	struct value vb = eval(ast, *(int*)vec_get(&np->args, 1), ctx);
	struct value res = error_val;
	if (va.type == TYPE_STRING && vb.type == TYPE_STRING) {
		res = (struct value){
			.type = TYPE_BOOLEAN,
			.boolean = strncmp(va.string_ref, vb.string_ref,
					strlen(vb.string_ref)) == 0
		};
	}
	value_finish(va);
	value_finish(vb);
	return res;
}
static struct value fn_print(struct vec *ast, int root, struct context *ctx) {
	ast_node *np = vec_get(ast, root);
	for (int i = 0; i < np->args.len; ++i) {
		struct value vi = eval(ast, *(int*)vec_get(&np->args, i), ctx);
		if (vi.type == TYPE_STRING) {
			fprintf(stdout, "%s\n", vi.string_ref);
		} else {
			print_value(stdout, vi);
			fprintf(stdout, "\n");
		}
		value_finish(vi);
	}
	return void_val;
}
static struct value fn_def(struct vec *ast, int root, struct context *ctx) {
	ast_node *np = vec_get(ast, root);
	if (np->args.len != 2) return error_val;
	ast_node *na = vec_get(ast, *(int*)vec_get(&np->args, 0));
	if (na->op != OP_LIT) return error_val;
	const char *name = str_cstr(&na->str);
	int *i = vec_get(&np->args, 1);
	struct fn_def fn_def = { .name = name, .i = *i };
	hashmap_put(&ctx->defs_map, name, &fn_def);
	return void_val;
}
struct fn {
	const char *name;
	struct value (*f)(struct vec *ast, int root, struct context *ctx);
};
static struct fn fns[] = {
	{ "let", &fn_let },
	//{ "if", &fn_if },
	{ "apply", &fn_apply },
	{ "print", &fn_print },
	{ "startsw", &fn_startsw },
	{ "def", &fn_def },
	{ NULL, NULL }
};

/* ## Evaluation logic */
static struct value eval(struct vec *ast, int root, struct context *ctx) {
	struct value res, *vp;
	ast_node *np = vec_get(ast, root);
	switch (np->op) {
	case OP_LIT: return (struct value){
		.type = TYPE_STRING, .string_ref = str_cstr(&np->str)
	};
	case OP_LIST:
		res = (struct value){
			.type = TYPE_LIST, .list = vec_new_empty(sizeof(struct value))
		};
		for (int i = 0; i < np->args.len; ++i) {
			int *ni = vec_get(&np->args, i);
			struct value r = eval(ast, *ni, ctx);
			vec_append(&res.list, &r);
		}
		return res;
	case OP_BLOCK:
		res = void_val;
		for (int i = 0; i < np->args.len; ++i) {
			int *ni = vec_get(&np->args, i);
			value_finish(res);
			res = eval(ast, *ni, ctx);
		}
		return res;
	case OP_FN: {
		struct fn *fn = fns;
		while (fn->name) {
			if (strcmp(fn->name, str_cstr(&np->str)) == 0) {
				return fn->f(ast, root, ctx);
			}
			++fn;
		}
		return error_val;
	}
	case OP_VAR:
		return get_var(ctx, str_cstr(&np->str));
	case OP_NEG: {
		int *ap = vec_get(&np->args, 0);
		struct value v = eval(ast, *ap, ctx);
		if (v.type == TYPE_BOOLEAN) {
			res = (struct value){
				.type = TYPE_BOOLEAN, .boolean = !v.boolean
			};
		} else {
			res = error_val;
		}
		value_finish(v);
		return res;
	}
	case OP_AND:
	case OP_OR: {
		int *ap = vec_get(&np->args, 0);
		struct value v = eval(ast, *ap, ctx);
		if (v.type == TYPE_BOOLEAN) {
			if (np->op == OP_AND ? v.boolean : !v.boolean) {
				int *bp = vec_get(&np->args, 1);
				res = eval(ast, *bp, ctx);
			} else {
				res = (struct value){
					.type = TYPE_BOOLEAN,
					.boolean = np->op == OP_OR
				};
			}
		} else {
			res = error_val;
		}
		value_finish(v);
		return res;
	}
	case OP_EQ:
	case OP_IN: {
		int *ap = vec_get(&np->args, 0);
		int *bp = vec_get(&np->args, 1);
		struct value va = eval(ast, *ap, ctx);
		struct value vb = eval(ast, *bp, ctx);
		if (np->op == OP_EQ) {
			if (va.type == TYPE_STRING && vb.type == TYPE_STRING) {
				res = (struct value){
					.type = TYPE_BOOLEAN,
					.boolean = strcmp(va.string_ref, vb.string_ref) == 0
				};
			} else {
				res = error_val;
			}
		} else {
			if (va.type == TYPE_STRING && vb.type == TYPE_LIST) {
				res = (struct value){ .type = TYPE_BOOLEAN, .boolean = false };
				for (int i = 0; i < vb.list.len; ++i) {
					vp = vec_get(&vb.list, i);
					if (vp->type == TYPE_STRING) {
						if (strcmp(va.string_ref, vp->string_ref) == 0) {
							value_finish(res);
							res = (struct value){ .type = TYPE_BOOLEAN,
								.boolean = true };
							break;
						}
					}
				}
			} else {
				res = error_val;
			}
		}
		value_finish(va);
		value_finish(vb);
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
	case OP_LIT:
		fprintf(f, "\"%s\"", str_cstr(&np->str));
		break;
	case OP_LIST:
	case OP_BLOCK:
	case OP_FN:
		switch (np->op) {
		case OP_LIST:
			start = '['; sep = ','; end = ']';
			break;
		case OP_BLOCK:
			start = '{'; sep = ';'; end = '}';
			break;
		case OP_FN:
			start = '('; sep = ','; end = ')';
			break;
		case OP_LIT:
		case OP_VAR:
		case OP_NEG:
		case OP_AND:
		case OP_OR:
		case OP_EQ:
		case OP_IN:
			asrt(false, "shut up compiler warning");
			break;
		}
		if (np->op == OP_FN) fprintf(f, "%s", str_cstr(&np->str));
		fprintf(f, "%c", start);
		for (int i = 0; i < np->args.len; ++i) {
			int *ni = vec_get(&np->args, i);
			dump_ast(f, ast, *ni);
			if (i < np->args.len - 1) fprintf(f, "%c ", sep);
		}
		fprintf(f, "%c", end);
		break;
	case OP_VAR:
		fprintf(f, "$%s", str_cstr(&np->str));
		break;
	case OP_NEG:
		fprintf(f, "~");
		ni = vec_get(&np->args, 0);
		dump_ast(f, ast, *ni);
		break;
	case OP_AND:
	case OP_OR:
	case OP_EQ:
	case OP_IN:
		fprintf(f, "(");
		ni = vec_get(&np->args, 0);
		dump_ast(f, ast, *ni);
		switch (np->op) {
		case OP_AND: fprintf(f, "&"); break;
		case OP_OR: fprintf(f, "|"); break;
		case OP_EQ: fprintf(f, "="); break;
		case OP_IN: fprintf(f, "%%"); break;
		case OP_LIT:
		case OP_LIST:
		case OP_BLOCK:
		case OP_FN:
		case OP_VAR:
		case OP_NEG:
			asrt(false, "shut up compiler warning");
			break;
		}
		ni = vec_get(&np->args, 1);
		dump_ast(f, ast, *ni);
		fprintf(f, ")");
		break;
	}
}
static void print_value(FILE *f, struct value v) {
	switch (v.type) {
	case TYPE_STRING:
		fprintf(f, "\"%s\"", v.string_ref);
		break;
	case TYPE_LIST:
		fprintf(f, "[");
		for (int i = 0; i < v.list.len; ++i) {
			struct value *vp = vec_get(&v.list, i);
			print_value(f, *vp);
			if (i < v.list.len - 1) fprintf(f, ", ");
		}
		fprintf(f, "]");
		break;
	case TYPE_BOOLEAN:
		fprintf(f, "%s", v.boolean ? "True" : "False");
		break;
	case TYPE_VOID:
		fprintf(f, "Void");
		break;
	case TYPE_ERROR:
		fprintf(f, "Error");
		break;
	}
}

/* # Public interface */
uexpr uexpr_parse(FILE *f) {
	struct parser_state ps = new_parser(f);
	int root = term(&ps);
	if (root == -1) {
		// TODO: free ast
		return NULL;
	}
	struct uexpr_impl *impl = malloc_check(sizeof(struct uexpr_impl));
	impl->ast = ps.ast;
	impl->root = root;
	return impl;
}
static int iter_finish_ctx_vars(void *_cl, void *item) {
	struct value *vp = item;
	value_finish(*vp);
	return MAP_OK;
}

uexpr_ctx uexpr_ctx_create(uexpr e) {
	struct context *ctx = malloc_check(sizeof(struct context));
	*ctx = (struct context){
		.get = NULL,
		.set = NULL,
		.uexpr = (struct uexpr_impl*)e
	};
	hashmap_init(&ctx->vars, sizeof(struct value));
	hashmap_init(&ctx->defs_map, sizeof(struct fn_def));
	return (void*)ctx;
}
void uexpr_ctx_set_handlers(uexpr_ctx _ctx, uexpr_get get, uexpr_set set,
		void *cl) {
	struct context *ctx = _ctx;
	ctx->get = get;
	ctx->set = set;
	ctx->cl = cl;
}
void uexpr_ctx_destroy(uexpr_ctx _ctx) {
	struct context *ctx = _ctx;
	hashmap_iterate(&ctx->vars, &iter_finish_ctx_vars, NULL);
	hashmap_finish(&ctx->vars);
	hashmap_finish(&ctx->defs_map);
	free(ctx);
}
uexpr_fn uexpr_ctx_get_fn(uexpr_ctx _ctx, const char *name) {
	struct context *ctx = _ctx;
	struct fn_def *fn_def;
	if (hashmap_get(&ctx->defs_map, name, (void**)&fn_def) != MAP_OK) return -1;
	return fn_def->i;
}
struct iter_get_fn_names_cl {
	const char **res;
	int i;
};
static int iter_get_fn_names(void *_cl, void *item) {
	struct iter_get_fn_names_cl *cl = _cl;
	struct fn_def *fn_def = item;
	cl->res[cl->i++] = fn_def->name;
	return MAP_OK;
}
static int char_ptr_cmp(const void *a, const void *b) {
	return strcmp(*(const char **)a, *(const char **)b);
}
const char ** uexpr_get_all_fns(uexpr_ctx _ctx) {
	struct context *ctx = _ctx;
	struct iter_get_fn_names_cl cl = {
		.res = malloc_check(
			sizeof(const char *) * (hashmap_length(&ctx->defs_map) + 1)),
		.i = 0
	};
	hashmap_iterate(&ctx->defs_map, &iter_get_fn_names, &cl);
	qsort(cl.res, cl.i, sizeof(const char *), char_ptr_cmp);
	cl.res[cl.i++] = NULL;
	return cl.res;
}
bool uexpr_eval_fn(uexpr_ctx _ctx, uexpr_fn fn) {
	asrt(fn, "bad fn");
	struct context *ctx = _ctx;
	int i = fn;
	struct value val = eval(&ctx->uexpr->ast, i, ctx);
	bool res = val.type == TYPE_BOOLEAN && val.boolean;
	value_finish(val);
	return res;
}
bool uexpr_eval(uexpr_ctx _ctx) {
	struct context *ctx = _ctx;
	return uexpr_eval_fn(_ctx, ctx->uexpr->root);
}

void uexpr_print(uexpr e, FILE *f) {
	struct uexpr_impl *impl = e;
	dump_ast(f, impl->ast, impl->root);
}
void uexpr_destroy(uexpr e) {
	struct uexpr_impl *impl = e;
	for (int i = 0; i < impl->ast.len; ++i) {
		ast_node_finish((ast_node*)vec_get(&impl->ast, i));
	}
	vec_free(&impl->ast);
	free(impl);
}

const char * uexpr_get_string(uexpr_val val) {
	struct value *vp = val;
	if (vp->type == TYPE_STRING) return vp->string_ref;
	else return NULL;
}
bool uexpr_get_boolean(uexpr_val val, bool *b) {
	struct value *vp = val;
	if (vp->type == TYPE_BOOLEAN) {
		*b = vp->boolean;
		return true;
	}
	return false;
}
uexpr_val uexpr_create_string(const char *s) {
	struct value *vp = malloc_check(sizeof(struct value));
	vp->type = TYPE_STRING;
	vp->string_ref = s ? s : ""; // this is not exactly the nicest...
	return (void*)vp;
}
uexpr_val uexpr_create_boolean(bool b) {
	struct value *vp = malloc_check(sizeof(struct value));
	vp->type = TYPE_BOOLEAN;
	vp->boolean = b;
	return (void*)vp;
}
uexpr_val uexpr_create_list_string(const char **s, int n) {
	struct value *vp = malloc_check(sizeof(struct value));
	vp->type = TYPE_LIST;
	vp->list = vec_new_empty(sizeof(struct value));
	for (int i = 0; i < n; ++i) {
		struct value vi = { .type = TYPE_STRING, .string_ref = s[i] };
		vec_append(&vp->list, &vi);
	}
	return (void*)vp;
}
void uexpr_val_destroy(uexpr_val val) {
	struct value *vp = val;
	value_finish(*vp);
	free(vp);
}
