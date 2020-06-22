#include <stdlib.h>
#include <string.h>

#include "vec.h"
#include "core.h"

const struct str str_empty = { .v = VEC_EMPTY(sizeof(char)) };

static void vec_realloc(struct vec *v, int new_cap) {
    asrt(new_cap > v->len, "wrong len");
    v->d = realloc(v->d, new_cap * v->itemsize);
    v->cap = new_cap;
}
static void vec_append_multiple_no_realloc(
        struct vec *v, const void *n, int l) {
    asrt(v->len + l <= v->cap, "realloc went wrong");
    memcpy(v->d + v->len * v->itemsize, n, l * v->itemsize);
    v->len += l;
}

struct str str_new_empty() {
    return str_empty;
}
struct str str_new_from_cstr(const char *cstr) {
    struct str s = str_empty;
    str_append(&s, cstr, strlen(cstr));
    return s;
}
void str_append(struct str *s, const char *n, int l) {
    if (s->v.len + l + 1 > s->v.cap) {
        vec_realloc(&s->v, maxi(s->v.cap * 2, s->v.len + l + 1));
    }
    vec_append_multiple_no_realloc(&s->v, n, l);
    asrt(s->v.len + 1 <= s->v.cap, "bad str len calc");
    char *d = s->v.d;
    d[s->v.len] = '\0';
}
void str_append_char(struct str *s, char c) {
    str_append(s, &c, 1);
}
const char * str_cstr(const struct str *s) {
    if (s->v.len == 0) return ""; // we do this to avoid alloc at init
    return s->v.d;
}
struct str str_copy(const struct str *s) {
    struct str res = str_new_empty();
    str_append(&res, (const char *)s->v.d, s->v.len);
    return res;
}
void str_free(struct str *s) {
    vec_free(&s->v);
}
bool str_any(const struct str *s) {
    return s->v.len > 0;
}
void str_clear(struct str *s) {
    if (str_any(s)) {
        s->v.len = 0;
        asrt(s->v.cap >= 1, "zero cap");
        char *d = s->v.d;
        d[s->v.len] = '\0';
    }
}

struct vec vec_new_empty(size_t itemsize) {
    return (struct vec){ .d = NULL, .len = 0, .cap = 0, .itemsize = itemsize };
}
void vec_append_multiple(struct vec *v, const void *n, int l) {
    if (v->len + l > v->cap) {
        vec_realloc(v, maxi(v->cap * 2, v->len + l));
    }
    vec_append_multiple_no_realloc(v, n, l);
}
int vec_append(struct vec *v, const void *n) {
    vec_append_multiple(v, n, 1);
    return v->len - 1;
}
void * vec_get(struct vec *v, int i) {
    asrt(i >= 0 && i < v->len, "bad vec index");
    return v->d + i * v->itemsize;
}
const void * vec_get_c(const struct vec *v, int i) {
    asrt(i >= 0 && i < v->len, "bad vec index");
    return v->d + i * v->itemsize;
}
void vec_free(struct vec *v) {
    free(v->d);
}
void vec_clear(struct vec *v) {
    v->len = 0;
}
