#ifndef GUI_CALENDAR_VEC_H
#define GUI_CALENDAR_VEC_H
#include <stddef.h>
#include <stdbool.h>

#include "algo.h"

struct vec {
    void *d;
    int len, cap;
    size_t itemsize;
};
#define VEC_EMPTY(is) \
    (struct vec){ .d = NULL, .len = 0, .cap = 0, .itemsize = is }

struct str {
    struct vec v;
};
extern const struct str str_empty;

struct str str_new_empty();
struct str str_new_from_cstr(const char *cstr);
void str_append(struct str *s, const char *n, int l);
void str_append_char(struct str *s, char c);
const char * str_cstr(const struct str *s);
struct str str_copy(const struct str *s);
void str_free(struct str *s);
bool str_any(const struct str *s);
void str_clear(struct str *s);

struct vec vec_new_empty(size_t itemsize);
void vec_append_multiple(struct vec *v, const void *n, int l);
/* return index of added element */
int vec_append(struct vec *v, const void *n);
void * vec_get(struct vec *v, int i);
const void * vec_get_c(const struct vec *v, int i);
void vec_free(struct vec *v);
void vec_sort(struct vec *v, sort_lt lt, void *cl);
void vec_clear(struct vec *v);
struct vec vec_copy(const struct vec *v);
bool vec_any(const struct vec *v);

#endif
