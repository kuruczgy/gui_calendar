#include <string.h>

#include "props.h"
#include "util.h"

#define EMPTY_VAL(type, name, capname) .has_##name = false,
#define EMPTY_STR(type, name, capname) .name = { .v = VEC_EMPTY(sizeof(char)) },
#define EMPTY_VEC(type, name, capname) .name = VEC_EMPTY(sizeof(type)),
const struct props props_empty = {
	PROPS_LIST_BY_VAL(EMPTY_VAL)
	PROPS_LIST_VEC(EMPTY_VEC)
	PROPS_LIST_STR(EMPTY_STR)
	.dirty = true
};
const struct props_mask props_mask_empty = { ._mask = 0U };
#undef EMPTY_VAL
#undef EMPTY_STR
#undef EMPTY_VEC

static void free_vec_default(struct vec *v) {
	vec_free(v);
	*v = vec_new_empty(sizeof(struct str));
}
static void free_vec_str(struct vec *v) {
	for (int i = 0; i < v->len; ++i) {
		struct str *s = vec_get(v, i);
		str_free(s);
	}
	free_vec_default(v);
}
static void copy_vec_default(struct vec *dst, const struct vec *src) {
	*dst = vec_copy(src);
}
static void free_vec_related_to(struct vec *v) {
	for (int i = 0; i < v->len; ++i) {
		struct prop_related_to *rel = vec_get(v, i);
		str_free(&rel->uid);
	}
	free_vec_default(v);
}
static void copy_vec_str(struct vec *dst, const struct vec *src) {
	for (int i = 0; i < src->len; ++i) {
		const struct str *s = vec_get_c(src, i);
		struct str copy = str_copy(s);
		vec_append(dst, &copy);
	}
}
static void copy_vec_related_to(struct vec *dst, const struct vec *src) {
	for (int i = 0; i < src->len; ++i) {
		const struct prop_related_to *rel = vec_get_c(src, i);
		struct prop_related_to copy = *rel;
		copy.uid = str_copy(&rel->uid);
		vec_append(dst, &copy);
	}
}
static bool eq_str(const void *pa, const void *pb) {
	const struct str *a = pa, *b = pb;
	return strcmp(str_cstr(a), str_cstr(b)) == 0;
}
static bool eq_prop_related_to(const void *pa, const void *pb) {
	const struct prop_related_to *a = pa, *b = pb;
	if (a->reltype != b->reltype) return false;
	if (strcmp(str_cstr(&a->uid), str_cstr(&b->uid)) != 0) return false;
	return true;
}
static bool eq_vec(const struct vec *a, const struct vec *b,
		bool (*eq)(const void *a, const void *b)) {
	if (a->len != b->len) return false;
	for (int i = 0; i < a->len; ++i) {
		const void *pa = vec_get_c(a, i);
		const void *pb = vec_get_c(b, i);
		if (!eq(pa, pb)) return false;
	}
	return true;
}


#define FREE_VEC(type, v) \
	_Generic(*(type *)0, \
		struct str: free_vec_str, \
		struct prop_related_to: free_vec_related_to \
	)(v)
#define COPY_VEC(type, dst, src) \
	_Generic(*(type *)0, \
		struct str: copy_vec_str, \
		struct prop_related_to: copy_vec_related_to, \
		default: copy_vec_default \
	)(dst, src)
#define EQ_VEC(type, a, b) eq_vec(a, b, \
	_Generic(*(type *)0, \
		struct str: eq_str, \
		struct prop_related_to: eq_prop_related_to \
	))

/* struct props getters */
#define GETTER_VAL(type, name, capname) \
	bool props_get_##name(const struct props *p, type *val) { \
		if (!p->has_##name) return false; \
		*val = p->name; \
		return true; \
	}
#define GETTER_VEC(type, name, capname) \
	const struct vec * props_get_##name(const struct props *p) { \
		return &p->name; }
#define GETTER_STR(type, name, capname) \
	const char * props_get_##name(const struct props *p) { \
		if (!str_any(&p->name)) return NULL; \
		return str_cstr(&p->name); \
	}
PROPS_LIST_BY_VAL(GETTER_VAL)
PROPS_LIST_VEC(GETTER_VEC)
PROPS_LIST_STR(GETTER_STR)
#undef GETTER_VAL
#undef GETTER_VEC
#undef GETTER_STR

/* struct props setters */
#define SETTER_VAL(type, name, capname) \
	void props_set_##name(struct props *p, type val) { \
		p->has_##name = true; \
		p->name = val; \
		p->dirty = true; \
	}
#define SETTER_VEC(type, name, capname) \
	void props_set_##name(struct props *p, struct vec val) { \
		FREE_VEC(type, &p->name); \
		p->name = val; \
		p->dirty = true; \
	}
#define SETTER_STR(type, name, capname) \
	void props_set_##name(struct props *p, const char *val) { \
		str_free(&p->name); \
		p->name = str_new_from_cstr(val); \
		p->dirty = true; \
	}
PROPS_LIST_BY_VAL(SETTER_VAL)
PROPS_LIST_VEC(SETTER_VEC)
PROPS_LIST_STR(SETTER_STR)
#undef SETTER_VAL
#undef SETTER_VEC
#undef SETTER_STR

void props_mask_add(struct props_mask *pm, enum prop pr) {
	pm->_mask |= (1U << pr);
}
void props_mask_remove(struct props_mask *pm, enum prop pr) {
	pm->_mask &= ~(1U << pr);
}
bool props_mask_get(const struct props_mask *pm, enum prop pr) {
	return pm->_mask & (1U << pr);
}
bool props_mask_any(const struct props_mask *pm) {
	return pm->_mask;
}

#define APPLY_MASK_VAL(type, name, capname) \
	if (pm->_mask & (1U << PROP_##capname)) p->has_##name = false;
#define APPLY_MASK_VEC(type, name, capname) \
	if (pm->_mask & (1U << PROP_##capname)) { \
		FREE_VEC(type, &p->name); \
	}
#define APPLY_MASK_STR(type, name, capname) \
	if (pm->_mask & (1U << PROP_##capname)) { \
		str_free(&p->name); \
		p->name = str_empty; \
	}
void props_apply_mask(struct props *p, const struct props_mask *pm) {
	PROPS_LIST_BY_VAL(APPLY_MASK_VAL)
	PROPS_LIST_VEC(APPLY_MASK_VEC)
	PROPS_LIST_STR(APPLY_MASK_STR)
	p->dirty = true;
}
#undef APPLY_MASK_VAL
#undef APPLY_MASK_VEC
#undef APPLY_MASK_STR

#define GET_MASK_VAL(type, name, capname) \
	if (p->has_##name) pm._mask |= (1U << PROP_##capname);
#define GET_MASK_VEC(type, name, capname) \
	if (p->name.len) pm._mask |= (1U << PROP_##capname);
#define GET_MASK_STR(type, name, capname) \
	if (str_any(&p->name)) pm._mask |= (1U << PROP_##capname);
struct props_mask props_get_mask(const struct props *p) {
	struct props_mask pm = props_mask_empty;
	PROPS_LIST_BY_VAL(GET_MASK_VAL)
	PROPS_LIST_VEC(GET_MASK_VEC)
	PROPS_LIST_STR(GET_MASK_STR)
	return pm;
}

#define UNION_VAL(type, name, capname) \
	if (rhs->has_##name) { \
		p->name = rhs->name; \
		p->has_##name = true; \
	}
#define UNION_VEC(type, name, capname) \
	if (rhs->name.len) { \
		FREE_VEC(type, &p->name); \
		COPY_VEC(type, &p->name, &rhs->name); \
	}
#define UNION_STR(type, name, capname) \
	if (str_any(&rhs->name)) { \
		str_free(&p->name); \
		p->name = str_copy(&rhs->name); \
	}
void props_union(struct props *p, const struct props *rhs) {
	PROPS_LIST_BY_VAL(UNION_VAL)
	PROPS_LIST_VEC(UNION_VEC)
	PROPS_LIST_STR(UNION_STR)
	p->dirty = true;
}
#undef UNION_VAL
#undef UNION_VEC
#undef UNION_STR

#define FINISH_VAL(type, name, capname)
#define FINISH_VEC(type, name, capname) FREE_VEC(type, &p->name);
#define FINISH_STR(type, name, capname) str_free(&p->name);
void props_finish(struct props *p) {
	PROPS_LIST_BY_VAL(FINISH_VAL)
	PROPS_LIST_VEC(FINISH_VEC)
	PROPS_LIST_STR(FINISH_STR)
}
#undef FINISH_VAL
#undef FINISH_VEC
#undef FINISH_STR

#define EQUAL_VAL(type, name, capname) \
	if (props_mask_get(pm, PROP_##capname)) { \
		if (a->has_##name != b->has_##name) return false; \
		if (a->has_##name && (a->name != b->name)) return false; \
	}
#define EQUAL_VEC(type, name, capname) \
	if (props_mask_get(pm, PROP_##capname)) \
		if (!EQ_VEC(type, &a->name, &b->name)) return false;
#define EQUAL_STR(type, name, capname) \
	if (props_mask_get(pm, PROP_##capname)) \
		if (strcmp(str_cstr(&a->name), str_cstr(&b->name)) != 0) \
			return false;
bool props_equal(const struct props *a, const struct props *b,
		const struct props_mask *pm) {
	PROPS_LIST_BY_VAL(EQUAL_VAL)
	PROPS_LIST_VEC(EQUAL_VEC)
	PROPS_LIST_STR(EQUAL_STR)
	return true;
}
#undef EQUAL_VAL
#undef EQUAL_VEC
#undef EQUAL_STR

static void props_recalc(struct props *p) {
	if (p->dirty) {
		p->color_val =
			lookup_color(str_cstr(&p->color), p->color.v.len);
		p->dirty = false;
	}
}

uint32_t props_get_color_val(struct props *p) {
	props_recalc(p);
	return p->color_val;
}
