#ifndef GUI_CALENDAR_UEXPR_H
#define GUI_CALENDAR_UEXPR_H

#include <stdbool.h>
#include <stdio.h>

typedef void *uexpr;
typedef void *uexpr_val;
typedef void *uexpr_fn;
typedef void *uexpr_ctx;

/* ownership IS transfered */
typedef uexpr_val (*uexpr_get)(void *cl, const char *key);

/* ownership is NOT transfered */
typedef bool(*uexpr_set)(void *cl, const char *key, uexpr_val val);

uexpr uexpr_parse(FILE *f);
void uexpr_print(uexpr e, FILE *f);
void uexpr_destroy(uexpr e);

uexpr_ctx uexpr_ctx_create(uexpr e);
void uexpr_ctx_set_handlers(uexpr_ctx ctx, uexpr_get get, uexpr_set set,
        void *cl);
void uexpr_ctx_destroy(uexpr_ctx ctx);
uexpr_fn uexpr_ctx_get_fn(uexpr_ctx ctx, const char *name);
char ** uexpr_get_all_fns(uexpr_ctx ctx);
bool uexpr_eval(uexpr_ctx ctx);
bool uexpr_eval_fn(uexpr_ctx ctx, uexpr_fn fn);

const char * uexpr_get_string(uexpr_val val);
bool uexpr_get_boolean(uexpr_val val, bool *b);
uexpr_val uexpr_create_string(const char *s);
uexpr_val uexpr_create_boolean(bool b);
uexpr_val uexpr_create_list_string(char **s, int n);
void uexpr_val_destroy(uexpr_val val);

#endif
