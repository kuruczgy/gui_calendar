#ifndef GUI_CALENDAR_UEXPR_H
#define GUI_CALENDAR_UEXPR_H

#include <stdbool.h>
#include <stdio.h>

typedef void *uexpr;
typedef const char * (*uexpr_get)(void *cl, const char *key);
typedef bool(*uexpr_set)(void *cl, const char *key, const char *val);

uexpr uexpr_parse(FILE *f);
bool uexpr_eval(uexpr e, void *cl, uexpr_get get, uexpr_set set);
void uexpr_print(uexpr e, FILE *f);
void uexpr_destroy(uexpr e);

#endif
