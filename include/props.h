#ifndef GUI_CALENDAR_PROPS_H
#define GUI_CALENDAR_PROPS_H

#include <stdbool.h>
#include <stdint.h>
#include <ds/vec.h>

#include "datetime.h"

enum prop_status {
	PROP_STATUS_TENTATIVE,
	PROP_STATUS_CONFIRMED,
	PROP_STATUS_CANCELLED,
	PROP_STATUS_COMPLETED,
	PROP_STATUS_NEEDSACTION,
	PROP_STATUS_INPROCESS,
};
enum prop_class {
	PROP_CLASS_PRIVATE,
	PROP_CLASS_PUBLIC,
};
enum prop_reltype {
	PROP_RELTYPE_PARENT,
	PROP_RELTYPE_CHILD,
	PROP_RELTYPE_SIBLING,
	PROP_RELTYPE_DEPENDS_ON,
};
struct prop_related_to {
	enum prop_reltype reltype;
	struct str uid;
};

struct props;

/* Search for these comments in the code to get an idea of places you probably
 * want to also add a new property to:
 * DEP: new prop
 */

/* m(type, name, capname) */
#define PROPS_LIST_BY_VAL(m) \
	m(ts, start, START) \
	m(ts, end, END) \
	m(ts, due, DUE) \
	m(ts, last_modified, LAST_MODIFIED) \
	m(enum prop_status, status, STATUS) \
	m(enum prop_class, class, CLASS) \
	m(int, estimated_duration, ESTIMATED_DURATION) \
	m(int, percent_complete, PERCENT_COMPLETE)
#define PROPS_LIST_VEC(m) \
	m(struct prop_related_to, related_to, RELATED_TO) \
	m(struct str, categories, CATEGORIES)
#define PROPS_LIST_STR(m) \
	m(struct str, color, COLOR) \
	m(struct str, summary, SUMMARY) \
	m(struct str, location, LOCATION) \
	m(struct str, desc, DESC)
#define PROPS_LIST_ALL(m) \
	PROPS_LIST_BY_VAL(m) \
	PROPS_LIST_VEC(m) \
	PROPS_LIST_STR(m)

/* struct props getters */
#define DECL(type, name, capname) \
	bool props_get_##name(const struct props *p, type *val);
PROPS_LIST_BY_VAL(DECL)
#undef DECL

#define DECL(type, name, capname) \
	const struct vec * props_get_##name(const struct props *p);
PROPS_LIST_VEC(DECL)
#undef DECL

#define DECL(type, name, capname) \
	const char * props_get_##name(const struct props *p);
PROPS_LIST_STR(DECL)
#undef DECL

/* struct props setters */
#define DECL(type, name, capname) \
	void props_set_##name(struct props *p, type val);
PROPS_LIST_BY_VAL(DECL)
#undef DECL

#define DECL(type, name, capname) \
	void props_set_##name(struct props *p, struct vec val);
PROPS_LIST_VEC(DECL)
#undef DECL

#define DECL(type, name, capname) \
	void props_set_##name(struct props *p, const char *val);
PROPS_LIST_STR(DECL)
#undef DECL

/* enum prop */
#define DECL(type, name, capname) PROP_##capname,
enum prop {
	PROPS_LIST_ALL(DECL)
	PROP_MAX
};
#undef DECL

/* struct props */
#define FIELD(type, name, capname) type name;
#define FIELD_VEC(type, name, capname) struct vec name;
#define FIELD_HAS(type, name, capname) bool has_##name : 1;
struct props {
	PROPS_LIST_BY_VAL(FIELD)
	PROPS_LIST_VEC(FIELD_VEC)
	PROPS_LIST_STR(FIELD)

	uint32_t color_val;

	PROPS_LIST_BY_VAL(FIELD_HAS)
	bool dirty : 1; /* have any fields changed since last recalc */
};
#undef FIELD
#undef FIELD_VEC
#undef FIELD_HAS

/* use this to initialize struct props */
extern const struct props props_empty;
extern const struct props_mask props_mask_empty;

/* struct props_mask */
struct props_mask {
	unsigned int _mask;
	_Static_assert(sizeof(unsigned int) * 8 >= PROP_MAX,
			"too many props for mask");
};
void props_mask_add(struct props_mask *pm, enum prop pr);
void props_mask_remove(struct props_mask *pm, enum prop pr);
bool props_mask_get(const struct props_mask *pm, enum prop pr);
bool props_mask_any(const struct props_mask *pm);

/* struct props other methods */

/* Deletes all properties from p that are not present in the mask. */
void props_apply_mask(struct props *p, const struct props_mask *pm);

/* Returns a mask representing the properties set in p. */
struct props_mask props_get_mask(const struct props *p);

void props_union(struct props *p, const struct props *rhs);
void props_finish(struct props *p);

/* exact equality on the set of properties pm */
bool props_equal(const struct props *a, const struct props *b,
	const struct props_mask *pm);

/* calculated values */
uint32_t props_get_color_val(struct props *p);

#endif
