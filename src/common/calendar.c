#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "calendar.h"
#include "core.h"
#include "util.h"

void recur_dep_props_set_props(struct props *p,
		const struct recur_dep_props *rdp) {
	if (rdp->start != -1) props_set_start(p, rdp->start);
	if (rdp->end != -1) props_set_end(p, rdp->end);
	if (rdp->due != -1) props_set_due(p, rdp->due);
}

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
	c->recur_cache = vec_new_empty(sizeof(struct recur_dep_props));
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
	vec_free(&c->recur_cache);
}
bool comp_equal(const struct comp *a, const struct comp *b) {
	if (strcmp(str_cstr(&a->uid), str_cstr(&b->uid)) != 0) return false;
	if (a->type != b->type) return false;

	struct props_mask pm_full = props_mask_empty;
	pm_full._mask = ~pm_full._mask;
	if (!props_equal(&a->p, &b->p, &pm_full)) return false;
	return true;
}
bool comp_get_recur_point(struct comp *c, ts recurrence_id,
		struct recur_dep_props *rdp_out, struct props **p_out) {
	for (int i = 0; i < c->recur_insts.len; ++i) {
		struct comp_recur_inst *cri = vec_get(&c->recur_insts, i);
		if (cri->recurrence_id == recurrence_id) {
			*p_out = &cri->p;
			return true;
		}
	}
	for (int i = 0; i < c->recur_cache.len; ++i) {
		struct recur_dep_props *rdp = vec_get(&c->recur_cache, i);
		if (rdp->start == recurrence_id) {
			*rdp_out = *rdp;
			return true;
		}
	}
	return false;
}
struct props *comp_get_or_create_recur_inst(struct comp *c, ts recurrence_id) {
	struct recur_dep_props rdp;
	struct props *p = NULL;
	if (!comp_get_recur_point(c, recurrence_id, &rdp, &p)) return NULL;
	if (p) return p;

	struct comp_recur_inst cri = {
		.recurrence_id = recurrence_id,
		.p = props_empty,
	};
	props_union(&cri.p, &c->p); /* copy base instance */
	recur_dep_props_set_props(&cri.p, &rdp);
	int idx = vec_append(&c->recur_insts, &cri);
	return &((struct comp_recur_inst *)vec_get(&c->recur_insts, idx))->p;
}

struct recur_cb_env {
	struct comp *c;
	comp_recur_cb cb;
	void *env;
	ts end_length, due_length;
};
static void recur_cb_fn(void *_env, ts t) {
	struct recur_cb_env *env = _env;

	struct recur_dep_props rdp = {
		.start = t,
		.end = env->end_length != -1 ? t + env->end_length : -1,
		.due = env->due_length != -1 ? t + env->due_length : -1,
	};
	struct props *p = &env->c->p;
	for (int i = 0; i < env->c->recur_insts.len; ++i) {
		struct comp_recur_inst *cri = vec_get(&env->c->recur_insts, i);
		if (cri->recurrence_id == t) {
			p = &cri->p;
			props_get_start(p, &rdp.start);
			props_get_end(p, &rdp.end);
			props_get_due(p, &rdp.due);
			break;
		}
	}

	vec_append(&env->c->recur_cache, &rdp);
	env->cb(env->env, t, rdp, p);
}
struct props *comp_recur_expand(struct comp *c, ts to,
		comp_recur_cb cb, void *env) {
	struct recur_dep_props rdp = {
		.start = -1,
		.end = -1,
		.due = -1,
	};
	props_get_start(&c->p, &rdp.start);
	props_get_end(&c->p, &rdp.end);
	props_get_due(&c->p, &rdp.due);

	if (c->recur) {
		struct recur_cb_env _env = {
			.c = c,
			.cb = cb,
			.env = env,
			.end_length = -1,
			.due_length = -1,
		};
		if (rdp.start != -1) {
			if (rdp.end != -1)
				_env.end_length = rdp.end - rdp.start;
			if (rdp.due != -1)
				_env.due_length = rdp.due - rdp.start;
		}
		recurrence_expand(c->recur, to, &recur_cb_fn, &_env);
	} else if (!c->all_expanded) {
		vec_append(&c->recur_cache, &rdp);
		cb(env, -1, rdp, &c->p);
		c->all_expanded = true;
	}

	return NULL;
}

/* struct calendar */
static bool calendar_contains(struct calendar *cal, const char *uid) {
	return calendar_find_comp(cal, uid) != -1;
}
static int calendar_add_comp_unchecked(struct calendar *cal,
		struct comp c) {
	int idx = vec_append(&cal->comps_vec, &c);
	asrt(hashmap_put_cstr(&cal->comps_map, str_cstr(&c.uid), &idx)
		== MAP_OK, "");

	struct comp_info info = { .deleted = false };
	vec_append(&cal->comp_infos, &info);

	cal->cis_dirty[c.type] = true;

	return idx;
}
void calendar_init(struct calendar *cal) {
	cal->comps_vec = vec_new_empty(sizeof(struct comp));
	hashmap_init(&cal->comps_map, sizeof(int));
	cal->comp_infos = vec_new_empty(sizeof(struct comp_info));

	cal->cis = malloc_check(sizeof(struct rb_tree) * COMP_TYPE_N);
	for (int i = 0; i < COMP_TYPE_N; ++i) {
		rb_tree_init(&cal->cis[i], &interval_ops);
		cal->cis_n[i] = 0;
		cal->cis_dirty[i] = false;
	}

	cal->name = str_empty;
	cal->storage = str_empty;
	cal->priv = false;
	cal->loaded.tv_sec = 0; // should work...
}
static void cis_tree_free(struct rb_tree *T) {
	struct rb_iter iter = rb_iter(T, RB_ITER_ORDER_POST);
	struct rb_node *x;
	while (rb_iter_next(&iter, &x)) {
		struct interval_node *nx = container_of(x,
			struct interval_node, node);
		struct comp_inst *ci = container_of(nx,
			struct comp_inst, node);
		free(ci);
	}
}
void calendar_finish(struct calendar *cal) {
	for (int i = 0; i < cal->comps_vec.len; ++i) {
		struct comp *c = vec_get(&cal->comps_vec, i);
		comp_finish(c);
	}
	vec_free(&cal->comps_vec);
	hashmap_finish(&cal->comps_map);
	vec_free(&cal->comp_infos);

	for (int i = 0; i < COMP_TYPE_N; ++i) {
		cis_tree_free(&cal->cis[i]);
	}
	free(cal->cis);

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
	return calendar_add_comp_unchecked(cal, c);
}
int calendar_find_comp(struct calendar *cal, const char *uid) {
	int *idx;
	if (hashmap_get_cstr(&cal->comps_map, uid, (void**)&idx) != MAP_OK)
		return -1;
	return *idx;
}
void calendar_delete_comp(struct calendar *cal, int idx) {
	struct comp *c = vec_get(&cal->comps_vec, idx);
	struct comp_info *info = vec_get(&cal->comp_infos, idx);
	info->deleted = true;
	cal->cis_dirty[c->type] = true;
}
struct comp * calendar_get_comp(struct calendar *cal, int idx) {
	return vec_get(&cal->comps_vec, idx);
}

struct comp_recur_cb_env {
	struct calendar *cal;
	struct comp *c;
	int comp_idx;
};
static void comp_recur_cb_fn(void *_env, ts recurrence_id,
		struct recur_dep_props rdp, struct props *p) {
	struct comp_recur_cb_env *env = _env;
	struct comp_inst *ci = malloc_check(sizeof(struct comp_inst));
	*ci = (struct comp_inst){
		.c = env->c,
		.comp_idx = env->comp_idx,
		.p = p,
		.rdp = rdp,
		.recurrence_id = recurrence_id,
	};

	enum comp_type type = env->c->type;
	ci->node.lo = rdp.start;
	ci->node.max_hi = ci->node.hi = rdp.end;
	rb_insert(&env->cal->cis[type], &ci->node.node);
	++env->cal->cis_n[type];
}
void calendar_expand_instances_to(struct calendar *cal, enum comp_type type,
		ts to) {
	if (cal->cis_dirty[type]) {

		/* this clears the tree */
		cis_tree_free(&cal->cis[type]);
		rb_tree_init(&cal->cis[type], &interval_ops);
		cal->cis_n[type] = 0;

		for (int i = 0; i < cal->comps_vec.len; ++i) {
			struct comp *c = vec_get(&cal->comps_vec, i);
			if (c->type != type) continue;
			if (c->recur) recurrence_reset(c->recur);
			vec_clear(&c->recur_cache);
			c->all_expanded = false;
		}
		cal->cis_dirty[type] = false;
	}

	for (int i = 0; i < cal->comps_vec.len; ++i) {
		struct comp *c = vec_get(&cal->comps_vec, i);
		if (c->type != type) continue;
		struct comp_info *info = vec_get(&cal->comp_infos, i);
		if (info->deleted) continue;

		struct comp_recur_cb_env env = {
			.cal = cal, .c = c, .comp_idx = i };
		comp_recur_expand(c, to, &comp_recur_cb_fn, &env);
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
