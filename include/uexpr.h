#ifndef GUI_CALENDAR_UEXPR_H
#define GUI_CALENDAR_UEXPR_H

#include <stdbool.h>
#include <stdio.h>

typedef void *uexpr;
typedef void *uexpr_val;

/* ownership IS transfered */
typedef uexpr_val (*uexpr_get)(void *cl, const char *key);

/* ownership is NOT transfered */
typedef bool(*uexpr_set)(void *cl, const char *key, uexpr_val val);

uexpr uexpr_parse(FILE *f);
bool uexpr_eval(uexpr e, void *cl, uexpr_get get, uexpr_set set);
void uexpr_print(uexpr e, FILE *f);
void uexpr_destroy(uexpr e);

const char * uexpr_get_string(uexpr_val val);
bool uexpr_get_boolean(uexpr_val val, bool *b);
uexpr_val uexpr_create_string(const char *s);
uexpr_val uexpr_create_boolean(bool b);
uexpr_val uexpr_create_list_string(char **s, int n);
void uexpr_val_destroy(uexpr_val val);

#endif
