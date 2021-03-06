#ifndef GUI_CALENDAR_CALENDAR_H
#define GUI_CALENDAR_CALENDAR_H
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>
#include <ds/vec.h>
#include <ds/hashmap.h>
#include <ds/tree.h>

#include "datetime.h"
#include "props.h"

struct recurrence;

enum comp_type {
	COMP_TYPE_EVENT = 0,
	COMP_TYPE_TODO = 1,
	COMP_TYPE_N
};
struct recur_dep_props {
	union {
		struct { ts start, end; };
		struct ts_ran se_ran;
	};
	ts due;
};
void recur_dep_props_set_props(struct props *p,
	const struct recur_dep_props *rdp);

struct comp_recur_inst {
	ts recurrence_id;
	struct props p;
};
struct comp {
	struct str uid;
	enum comp_type type;
	struct props p;
	struct vec recur_insts; /* vec<struct comp_recur_inst> */
	struct recurrence *recur;
	struct vec recur_cache; /* vec<struct recur_dep_props> */
	bool all_expanded;
};
void comp_init(struct comp *c, struct str uid, enum comp_type type);
int comp_init_from_ics(struct comp *c, FILE *f);
void comp_finish(struct comp *c);
bool comp_equal(const struct comp *a, const struct comp *b);
bool comp_get_recur_point(struct comp *c, ts recurrence_id,
		struct recur_dep_props *rdp_out, struct props **p_out);
struct props *comp_get_or_create_recur_inst(struct comp *c, ts recurrence_id);

typedef void (*comp_recur_cb)(void *env, ts recurrence_id,
	struct recur_dep_props rdp, struct props *p);
struct props *comp_recur_expand(struct comp *c, ts to, comp_recur_cb cb,
	void *env);

struct comp_inst {
	struct comp *c;
	int comp_idx;
	struct props *p;
	struct recur_dep_props rdp;

	/* valid if this is actually a recurrence instance, -1 otherwise */
	ts recurrence_id;

	struct interval_node node;
};

struct calendar {
	struct vec comps_vec; /* vec<struct comp> */
	struct hashmap comps_map; /* hashmap<int> */
	struct vec comp_infos; /* vec<struct comp_info> */

	struct rb_tree *cis; /* exactly COMP_TYPE_N long */
	int cis_n[COMP_TYPE_N];
	bool cis_dirty[COMP_TYPE_N];

	struct str name;
	struct str storage;
	bool priv;
	struct timespec loaded;
};
void calendar_init(struct calendar* cal);
void calendar_finish(struct calendar *cal);

/* returns -1 if uid already exists */
int calendar_new_comp(struct calendar *cal, struct str uid,
		enum comp_type type);
/* returns -1 if uid already exists */
int calendar_add_comp(struct calendar *cal, struct comp c);

/* returns -1 if not found */
int calendar_find_comp(struct calendar *cal, const char *uid);
void calendar_delete_comp(struct calendar *cal, int idx);

struct comp * calendar_get_comp(struct calendar *cal, int idx);

void calendar_expand_instances_to(struct calendar *cal, enum comp_type type,
	ts to);

void update_calendar_from_storage(struct calendar *cal,
		struct cal_timezone *local_zone);
int libical_parse_ics(FILE *f, struct calendar *cal);

/* calendar utility functions */
const char * cal_status_str(enum prop_status v);
const char * cal_class_str(enum prop_class v);
bool cal_parse_status(const char *key, enum prop_status *status);

/* struct recurrence */
typedef void (*recur_cb)(void *cl, ts t);
void recurrence_expand(struct recurrence *recur, ts to, recur_cb cb, void *cl);
void recurrence_destroy(struct recurrence *recur);
void recurrence_reset(struct recurrence *recur);

/* struct props */
bool props_valid_for_type(const struct props *p, enum comp_type type);

#endif
