#include <string.h>

#include "props.h"
#include "util.h"

#define EMPTY_VAL(type, name, capname) .has_##name = false,
#define EMPTY_STR(type, name, capname) .name = { .v = VEC_EMPTY(sizeof(char)) },
#define EMPTY_VEC_STR(type, name, capname) \
	.name = VEC_EMPTY(sizeof(struct str)),
const struct props props_empty = {
	PROPS_LIST_BY_VAL(EMPTY_VAL)
	PROPS_LIST_STR(EMPTY_STR)
	PROPS_LIST_VEC_STR(EMPTY_VEC_STR)
	.dirty = true
};
const struct props_mask props_mask_empty = { ._mask = 0U };

static void free_vec_str(struct vec *v) {
	for (int i = 0; i < v->len; ++i) {
		struct str *s = vec_get(v, i);
		str_free(s);
	}
	vec_free(v);
	*v = vec_new_empty(sizeof(struct str));
}
static void copy_vec_str(struct vec *dst, const struct vec *src) {
	for (int i = 0; i < src->len; ++i) {
		struct str *s = vec_get((struct vec *)src, i); /* const cast */
		struct str copy = str_copy(s);
		vec_append(dst, &copy);
	}
}
static bool eq_vec_str(const struct vec *a, const struct vec *b) {
	if (a->len != b->len) return false;
	for (int i = 0; i < a->len; ++i) {
		const struct str *sa = vec_get((struct vec *)a, i); /* const cast */
		const struct str *sb = vec_get((struct vec *)b, i); /* const cast */
		if (strcmp(str_cstr(sa), str_cstr(sb)) != 0) return false;
	}
	return true;
}

/* struct props getters */
#define GETTER_VAL(type, name, capname) \
	bool props_get_##name(const struct props *p, type *val) { \
		if (!p->has_##name) return false; \
		*val = p->name; \
		return true; \
	}
#define GETTER_STR(type, name, capname) \
	const char * props_get_##name(const struct props *p) { \
		if (!str_any(&p->name)) return NULL; \
		return str_cstr(&p->name); \
	}
#define GETTER_VEC_STR(type, name, capname) \
	const struct vec * props_get_##name(const struct props *p) { \
		return &p->name; }
PROPS_LIST_BY_VAL(GETTER_VAL)
PROPS_LIST_STR(GETTER_STR)
PROPS_LIST_VEC_STR(GETTER_VEC_STR)

/* struct props setters */
#define SETTER_VAL(type, name, capname) \
	void props_set_##name(struct props *p, type val) { \
		p->has_##name = true; \
		p->name = val; \
		p->dirty = true; \
	}
#define SETTER_STR(type, name, capname) \
	void props_set_##name(struct props *p, const char *val) { \
		str_free(&p->name); \
		p->name = str_new_from_cstr(val); \
		p->dirty = true; \
	}
#define SETTER_VEC_STR(type, name, capname) \
	void props_set_##name(struct props *p, struct vec val) { \
		free_vec_str(&p->name); \
		p->name = val; \
		p->dirty = true; \
	}
PROPS_LIST_BY_VAL(SETTER_VAL)
PROPS_LIST_STR(SETTER_STR)
PROPS_LIST_VEC_STR(SETTER_VEC_STR)

void props_mask_add(struct props_mask *pm, enum prop pr) {
	pm->_mask |= (1U << pr);
}
void props_mask_remove(struct props_mask *pm, enum prop pr) {
	pm->_mask &= ~(1U << pr);
}
bool props_mask_get(struct props_mask *pm, enum prop pr) {
	return pm->_mask & (1U << pr);
}

#define APPLY_MASK_VAL(type, name, capname) \
	if (pm->_mask & (1U << PROP_##capname)) p->has_##name = false;
#define APPLY_MASK_STR(type, name, capname) \
	if (pm->_mask & (1U << PROP_##capname)) { \
		str_free(&p->name); \
		p->name = str_empty; \
	}
#define APPLY_MASK_VEC_STR(type, name, capname) \
	if (pm->_mask & (1U << PROP_##capname)) { \
		free_vec_str(&p->name); \
	}
void props_apply_mask(struct props *p, const struct props_mask *pm) {
	PROPS_LIST_BY_VAL(APPLY_MASK_VAL)
	PROPS_LIST_STR(APPLY_MASK_STR)
	PROPS_LIST_VEC_STR(APPLY_MASK_VEC_STR)
	p->dirty = true;
}

#define UNION_VAL(type, name, capname) \
	if (rhs->has_##name) { \
		p->name = rhs->name; \
		p->has_##name = true; \
	}
#define UNION_STR(type, name, capname) \
	if (str_any(&rhs->name)) { \
		str_free(&p->name); \
		p->name = str_copy(&rhs->name); \
	}
#define UNION_VEC_STR(type, name, capname) \
	if (rhs->name.len) { \
		free_vec_str(&p->name); \
		copy_vec_str(&p->name, &rhs->name); \
	}
void props_union(struct props *p, const struct props *rhs) {
	PROPS_LIST_BY_VAL(UNION_VAL)
	PROPS_LIST_STR(UNION_STR)
	PROPS_LIST_VEC_STR(UNION_VEC_STR)
	p->dirty = true;
}

#define FINISH_STR(type, name, capname) str_free(&p->name);
#define FINISH_VEC_STR(type, name, capname) free_vec_str(&p->name);
void props_finish(struct props *p) {
	PROPS_LIST_STR(FINISH_STR)
	PROPS_LIST_VEC_STR(FINISH_VEC_STR)
}

#define EQUAL_VAL(type, name, capname) \
	if (a->has_##name != b->has_##name) return false; \
	if (a->has_##name && (a->name != b->name)) return false;
#define EQUAL_STR(type, name, capname) \
	if (strcmp(str_cstr(&a->name), str_cstr(&b->name)) != 0) return false;
#define EQUAL_VEC_STR(type, name, capname) \
	if (!eq_vec_str(&a->name, &b->name)) return false;
bool props_equal(const struct props *a, const struct props *b) {
	PROPS_LIST_BY_VAL(EQUAL_VAL)
	PROPS_LIST_STR(EQUAL_STR)
	PROPS_LIST_VEC_STR(EQUAL_VEC_STR)
	return true;
}

static void props_recalc(struct props *p) {
	if (p->dirty) {
		p->color_val = lookup_color(str_cstr(&p->color), p->color.v.len);
		p->dirty = false;
	}
}

uint32_t props_get_color_val(struct props *p) {
	props_recalc(p);
	return p->color_val;
}
