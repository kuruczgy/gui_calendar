#ifndef GUI_CALENDAR_VEC_H
#define GUI_CALENDAR_VEC_H
#include <stddef.h>

struct vec {
    void *d;
    int len, cap;
    size_t itemsize;
};

struct str {
    struct vec v;
};

struct str str_new_empty();
void str_append(struct str *s, const char *n, int l);
void str_append_char(struct str *s, char c);
const char * str_cstr(struct str *s);
struct str str_copy(struct str *s);
void str_free(struct str *s);

struct vec vec_new_empty(size_t itemsize);
void vec_append_multiple(struct vec *v, const void *n, int l);
/* return index of added element */
int vec_append(struct vec *v, const void *n);
void * vec_get(struct vec *v, int i);
void vec_free(struct vec *v);

#endif
