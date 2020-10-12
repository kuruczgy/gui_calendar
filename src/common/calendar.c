#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "calendar.h"
#include "core.h"
#include "util.h"

struct comp_info {
	bool deleted;
};

/* struct comp */
void comp_init(struct comp *c, struct str uid, enum comp_type type) {
	c->uid = uid;
	c->type = type;
	c->p = props_empty;
	c->recur_insts = vec_new_empty(sizeof(struct comp_recur_inst));
	c->recur = NULL;
	c->all_expanded = false;
}
void comp_finish(struct comp *c) {
	str_free(&c->uid);
	props_finish(&c->p);
	for (int i = 0; i < c->recur_insts.len; ++i) {
		struct comp_recur_inst *cri = vec_get(&c->recur_insts, i);
		props_finish(&cri->p);
	}
	vec_free(&c->recur_insts);
	recurrence_destroy(c->recur);
}

struct recur_cb_cl {
	struct comp *c;
	comp_recur_cb cb;
	void *cl;
	ts length;
};
static void recur_cb_fn(void *_cl, ts t) {
	struct recur_cb_cl *cl = _cl;

	struct ts_ran time = { t, t + cl->length };
	struct props *p = &cl->c->p;
	for (int i = 0; i < cl->c->recur_insts.len; ++i) {
		struct comp_recur_inst *cri = vec_get(&cl->c->recur_insts, i);
		if (cri->recurrence_id == t) {
			p = &cri->p;
			props_get_start(p, &time.fr);
			props_get_end(p, &time.to);
			break;
		}
	}

	cl->cb(cl->cl, time, p);
}
struct props * comp_recur_expand(struct comp *c, ts to,
		comp_recur_cb cb, void *cl) {
	bool is_event = c->type == COMP_TYPE_EVENT;
	bool is_todo = c->type == COMP_TYPE_TODO;

	ts start = -1, end = -1, due = -1;
	bool has_start = props_get_start(&c->p, &start);
	bool has_end = props_get_end(&c->p, &end);
	bool has_due = props_get_due(&c->p, &due);
	(void)has_start;
	(void)has_end;
	(void)has_due;

	ts length = -1;
	if (is_event) length = end - start;
	if (is_todo) length = due - start;

	if (c->recur) {
		struct recur_cb_cl _cl = {
			.c = c, .cb = cb, .cl = cl, .length = length };
		recurrence_expand(c->recur, to, &recur_cb_fn, &_cl);
	} else if (!c->all_expanded) {
		cb(cl, (struct ts_ran){ start, start + length }, &c->p);
		c->all_expanded = true;
	}

	return NULL;
}
bool comp_equal(const struct comp *a, const struct comp *b) {
	if (strcmp(str_cstr(&a->uid), str_cstr(&b->uid)) != 0) return false;
	if (a->type != b->type) return false;
	if (!props_equal(&a->p, &b->p)) return false;
	return true;
}

/* struct calendar */
static bool calendar_contains(struct calendar *cal, const char *uid) {
	return calendar_find_comp(cal, uid) != -1;
}
static int calendar_add_comp_unchecked(struct calendar *cal,
		struct comp c) {
	int idx = vec_append(&cal->comps_vec, &c);
	asrt(hashmap_put(&cal->comps_map, str_cstr(&c.uid), &idx) == MAP_OK, "");

	struct comp_info info = { .deleted = false };
	vec_append(&cal->comp_infos, &info);

	cal->comps_dirty = true;

	return idx;
}
void calendar_init(struct calendar *cal) {
	cal->comps_vec = vec_new_empty(sizeof(struct comp));
	hashmap_init(&cal->comps_map, sizeof(int));
	cal->comp_infos = vec_new_empty(sizeof(struct comp_info));
	cal->cis = vec_new_empty(sizeof(struct comp_inst));
	cal->name = str_empty;
	cal->storage = str_empty;
	cal->priv = false;
	cal->loaded.tv_sec = 0; // should work...
	cal->comps_dirty = false;
}
void calendar_finish(struct calendar *cal) {
	for (int i = 0; i < cal->comps_vec.len; ++i) {
		struct comp *c = vec_get(&cal->comps_vec, i);
		comp_finish(c);
	}
	vec_free(&cal->comps_vec);
	hashmap_finish(&cal->comps_map);
	vec_free(&cal->comp_infos);
	vec_free(&cal->cis);
	str_free(&cal->name);
	str_free(&cal->storage);
}
int calendar_new_comp(struct calendar *cal, struct str uid,
		enum comp_type type) {
	if (calendar_contains(cal, str_cstr(&uid))) {
		str_free(&uid);
		return -1;
	}
	struct comp c;
	comp_init(&c, uid, type);
	return calendar_add_comp_unchecked(cal, c);
}
int calendar_add_comp(struct calendar *cal, struct comp c) {
	int idx = calendar_find_comp(cal, str_cstr(&c.uid));
	if (idx != -1) {
		calendar_delete_comp(cal, idx);
	}
	cal->comps_dirty = true;
	return calendar_add_comp_unchecked(cal, c);
}
int calendar_find_comp(struct calendar *cal, const char *uid) {
	int *idx;
	if (hashmap_get(&cal->comps_map, uid, (void**)&idx) != MAP_OK) return -1;
	return *idx;
}
void calendar_delete_comp(struct calendar *cal, int idx) {
	struct comp_info *info = vec_get(&cal->comp_infos, idx);
	info->deleted = true;

	cal->comps_dirty = true;
}
struct comp * calendar_get_comp(struct calendar *cal, int idx) {
	return vec_get(&cal->comps_vec, idx);
}

struct comp_recur_cb_cl {
	struct calendar *cal;
	struct comp *c;
	int comp_idx;
};
static void comp_recur_cb_fn(void *_cl, struct ts_ran time, struct props *p) {
	struct comp_recur_cb_cl *cl = _cl;
	struct comp_inst ci = {
		.c = cl->c, .comp_idx = cl->comp_idx, .p = p, .time = time };
	vec_append(&cl->cal->cis, &ci);
}
void calendar_expand_instances_to(struct calendar *cal, ts to) {
	if (cal->comps_dirty) {
		vec_clear(&cal->cis);
		for (int i = 0; i < cal->comps_vec.len; ++i) {
			struct comp *c = vec_get(&cal->comps_vec, i);
			if (c->recur) recurrence_reset(c->recur);
			c->all_expanded = false;
		}
		cal->comps_dirty = false;
	}

	for (int i = 0; i < cal->comps_vec.len; ++i) {
		struct comp *c = vec_get(&cal->comps_vec, i);
		struct comp_info *info = vec_get(&cal->comp_infos, i);
		if (info->deleted) continue;

		struct comp_recur_cb_cl cl = { .cal = cal, .c = c, .comp_idx = i };
		comp_recur_expand(c, to, &comp_recur_cb_fn, &cl);
	}
}

bool props_valid_for_type(const struct props *p, enum comp_type type) {
	bool is_event = type == COMP_TYPE_EVENT;
	bool is_todo = type == COMP_TYPE_TODO;

	if (!is_event && !is_todo) return false;

	ts start, end, due;
	bool has_start = props_get_start(p, &start);
	bool has_end = props_get_end(p, &end);
	bool has_due = props_get_due(p, &due);

	if (is_event != has_end) return false; /* has end iff event */
	if (has_end) {
		if (!has_start) return false;
		if (!(start < end)) return false; /* rfc5545#section-3.8.2.2 */
	}

	/* rfc5545#section-3.8.2.3 */
	if (!is_todo && has_due) return false;
	if (has_due) {
		if (has_start && !(start < due)) return false;
	}

	/* rfc5545#section-3.8.1.11 */
	enum prop_status status;
	bool has_status = props_get_status(p, &status);
	if (has_status) {
		if (is_event
			&& status != PROP_STATUS_TENTATIVE
			&& status != PROP_STATUS_CONFIRMED
			&& status != PROP_STATUS_CANCELLED) return false;
		if (is_todo
			&& status != PROP_STATUS_NEEDSACTION
			&& status != PROP_STATUS_COMPLETED
			&& status != PROP_STATUS_INPROCESS
			&& status != PROP_STATUS_CANCELLED) return false;
	}

	/* rfc5545#section-3.8.1.8 */
	int perc;
	bool has_perc = props_get_percent_complete(p, &perc);
	if (!is_todo && has_perc) return false;

	/* draft-apthorp-ical-tasks-01#section-6.1 */
	int est;
	bool has_est = props_get_estimated_duration(p, &est);
	if (!is_todo && has_est) return false;

	return true;
}
