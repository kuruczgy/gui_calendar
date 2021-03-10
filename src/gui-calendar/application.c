#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <platform_utils/log.h>
#include <platform_utils/assets.h>

#include "application.h"
#include "core.h"
#include "util.h"
#include "algo.h"
#include "keyboard.h"
#include "editor.h"

pu_assets_declare(default_uexpr)

/* provides a partial ordering over todos; implements the a < b comparison */
static bool todo_priority_cmp(
		struct app *app,
		const struct comp_inst *a,
		const struct comp_inst *b,
		bool consider_started
	) {
	ts start;
	bool a_started = true, b_started = true;
	if (props_get_start(a->p, &start)) a_started = start <= app->now;
	if (props_get_start(b->p, &start)) b_started = start <= app->now;

	enum prop_status status;
	bool a_inprocess = false, b_inprocess = false;
	if (props_get_status(a->p, &status))
		a_inprocess = status == PROP_STATUS_INPROCESS;
	if (props_get_status(b->p, &status))
		b_inprocess = status == PROP_STATUS_INPROCESS;

	int ed;
	const int sh = 60 * 10; // 10m
	bool a_short = false, b_short = false;
	if (props_get_estimated_duration(a->p, &ed)) a_short = ed <= sh;
	if (props_get_estimated_duration(b->p, &ed)) b_short = ed <= sh;

	ts a_due, b_due;
	bool a_has_due, b_has_due;
	a_has_due = props_get_due(a->p, &a_due);
	b_has_due = props_get_due(b->p, &b_due);

	if (a_inprocess != b_inprocess) {
		if (a_inprocess) return true;
		else return false;
	} else if (consider_started && a_started != b_started) {
		if (a_started) return true;
		else return false;
	} else if (a_short != b_short) {
		if (a_short) return true;
		else return false;
	} else if (a_has_due || b_has_due) {
		if (!a_has_due) return false;
		else if (!b_has_due) return true;
		return a_due < b_due;
	}
	return false;
}

struct print_template_env {
	struct app *app;
	struct proj_item *pi;
};
static void print_template_callback(void *_env, FILE *f) {
	struct print_template_env *env = _env;
	print_template(f, env->pi->ci, env->app->zone, env->pi->cal_index + 1);
}
static int get_default_cal_index(struct app *app) {
	asrt(app->cals.len > 0, "no calendars");
	if (app->current_filter != -1) {
		struct filter *f = vec_get(&app->filters, app->current_filter);
		return f->def_cal;
	}
	return 0;
}
static void print_new_event_template_callback(void *cl, FILE *f) {
	struct app *app = cl;
	int cal = get_default_cal_index(app);
	print_new_event_template(f, app->zone, cal + 1);
}
static void print_new_todo_template_callback(void *cl, FILE *f) {
	struct app *app = cl;
	int cal = get_default_cal_index(app);
	print_new_todo_template(f, app->zone, cal + 1);
}
static void print_str_callback(void *cl, FILE *f) {
	const struct str *str = cl;
	fprintf(f, "%s", str_cstr(str));
}
static const char ** new_editor_args(struct app *app) {
	int len = app->editor_args.len;
	asrt(len > 1, "editor args len");
	const char **args = malloc_check(sizeof(char*) * len);
	for (int i = 0; i < len - 1; ++i) {
		struct str *s = vec_get(&app->editor_args, i + 1);
		args[i] = str_cstr(s);
	}
	args[len - 1] = NULL;
	return args;
}
static void application_handle_child(struct app *app, int pidfd);
static void subprocess_pidfd_cb(void *env, struct pollfd pfd) {
	struct app *app = env;
	if (pfd.revents & POLLIN) {
		event_loop_remove_fd(app->event_loop, pfd.fd);
		application_handle_child(app, pfd.fd);
	}
}
void app_cmd_launch_editor(struct app *app, struct proj_item *pi) {
	if (!app->sp) {
		struct print_template_env env = {
			.app = app,
			.pi = pi,
		};
		const char **args = new_editor_args(app);
		struct str *s = vec_get(&app->editor_args, 0);
		app->sp = subprocess_new_input(str_cstr(s),
			args, &print_template_callback, &env);
		if (app->sp) {
			event_loop_add_fd(app->event_loop, app->sp->pidfd,
				POLLIN, app, subprocess_pidfd_cb);
		}
		free(args);
	}
}
static void app_launch_editor_str(struct app *app, const struct str *str) {
	if (!app->sp) {
		const char **args = new_editor_args(app);
		struct str *s = vec_get(&app->editor_args, 0);
		app->sp = subprocess_new_input(str_cstr(s),
			args, &print_str_callback, (void*)str);
		if (app->sp) {
			event_loop_add_fd(app->event_loop, app->sp->pidfd,
				POLLIN, app, subprocess_pidfd_cb);
		}
		free(args);
	}
}
void app_cmd_launch_editor_new(struct app *app, enum comp_type t) {
	void (*cb)(void*, FILE*);
	if (t == COMP_TYPE_EVENT) {
		cb = &print_new_event_template_callback;
	} else if (t == COMP_TYPE_TODO) {
		cb = &print_new_todo_template_callback;
	} else {
		return;
	}

	if (!app->sp) {
		const char **args = new_editor_args(app);
		struct str *s = vec_get(&app->editor_args, 0);
		app->sp = subprocess_new_input(str_cstr(s), args, cb, app);
		if (app->sp) {
			event_loop_add_fd(app->event_loop, app->sp->pidfd,
				POLLIN, app, subprocess_pidfd_cb);
		}
		free(args);
	}
}

struct ac_iter_assign_code_env {
	struct app *app;
	struct vec *acs; /* vec<struct active_comp*> */
};
static void app_switch_mode_select(struct app *app) {
	if (app->keystate == KEYSTATE_SELECT) return;
	app->mode_select_code_n = 0;
	app->keystate = KEYSTATE_SELECT;
	app->mode_select_uexpr_fn = -1;

	if (app->main_view == VIEW_CALENDAR) {
		struct vec acs = vec_new_empty(sizeof(struct active_comp *));

		struct rb_iter iter = rb_iter(
			&app->active_events.processed_not_hidden,
			RB_ITER_ORDER_IN);
		struct rb_node *x;
		while (rb_iter_next(&iter, &x)) {
			struct interval_node *nx =
				container_of(x, struct interval_node, node);
			struct active_comp *ac =
				container_of(nx, struct active_comp, node);

			if (ts_ran_overlap(ac->ci->time, app->view)) {
				vec_append(&acs, &ac);
			} else {
				ac->code[0] = '\0';
			}
		}

		struct key_gen g;
		key_gen_init(acs.len, &g);
		app->mode_select_len = g.k;

		for (int i = 0; i < acs.len; ++i) {
			struct active_comp **ac = vec_get(&acs, i);
			const char *code = key_gen_get(&g);
			asrt(code, "not enough codes");
			strcpy((*ac)->code, code);
		}
	} else if (app->main_view == VIEW_TODO) {
		struct key_gen g;
		key_gen_init(app->active_todos.v.len, &g);
		app->mode_select_len = g.k;
		for (int i = 0; i < app->active_todos.v.len; ++i) {
			struct active_comp *ac =
				vec_get(&app->active_todos.v, i);
			const char *code = key_gen_get(&g);
			asrt(code, "not enough codes");
			strcpy(ac->code, code);
		}
	} else {
		asrt(false, "unknown view");
	}
}

// static void schedule_active_todos(struct app *app) {
//	 struct vec E = vec_new_empty(sizeof(struct ts_ran));
//	 for (int i = 0; i < app->cals.len; ++i) {
//		 struct calendar *cal = vec_get(&app->cals, i);
//		 for (int j = 0; j < cal->cis.len; ++j) {
//			 struct comp_inst *ci = vec_get(&cal->cis, j);
//			 enum prop_status status;
//			 bool has_status = props_get_status(ci->p, &status);
//			 if (has_status && status == PROP_STATUS_CONFIRMED) {
//				 vec_append(&E, &ci->time);
//			 }
//		 }
//	 }
// 
//	 struct vec T = vec_new_empty(sizeof(struct schedule_todo));
//	 struct vec acs = vec_new_empty(sizeof(struct active_comp *));
//	 for (int i = 0; i < app->active_todos.len; ++i) {
//		 struct active_comp *ac = vec_get(&app->active_todos, i);
// 
//		 ts start;
//		 bool has_start = props_get_start(ac->ci->p, &start);
//		 int est;
//		 bool has_est = props_get_estimated_duration(ac->ci->p, &est);
//		 if (has_est) {
//			 struct schedule_todo st = {
//				 .start = has_start ? start : -1,
//				 .estimated_duration = est };
//			 vec_append(&T, &st);
//			 vec_append(&acs, &ac);
//		 }
//	 }
// 
//	 struct ts_ran *G = todo_schedule(app->now, E.len, E.d, T.len, T.d);
// 
//	 vec_free(&E);
//	 vec_free(&T);
// 
//	 for (int i = 0; i < acs.len; ++i) {
//		 struct active_comp **acp = vec_get(&acs, i);
//		 struct tobject obj = (struct tobject){
//			 .time = G[i],
//			 .type = TOBJECT_TODO,
//			 .ac = *acp
//		 };
//		 tslice_try_put(&app->slice_main, obj);
//	 }
// 
//	 vec_free(&acs);
//	 free(G);
// }

static void execute_filter(struct app *app, int fn, struct proj_item *pi,
		struct comp_display_settings *settings) {
	struct cal_uexpr_env env = {
		.app = app,
		.kind = CAL_UEXPR_FILTER,
		.pi = pi,
		.settings = settings,
		.set_props = props_empty,
		.set_edit = false
	};
	struct uexpr_ops ops = {
		.env = &env,
		.try_get_var = cal_uexpr_get,
		.try_set_var = cal_uexpr_set
	};

	if (fn != -1) {
		uexpr_ctx_set_ops(app->uexpr_ctx, ops);
		uexpr_eval(&app->uexpr, fn, app->uexpr_ctx, NULL);
	}
}
static void execute_current_filter(struct app *app, struct proj_item *pi,
		struct comp_display_settings *settings) {
	if (app->current_filter != -1) {
		struct filter *f = vec_get(&app->filters, app->current_filter);
		execute_filter(app, f->uexpr_fn, pi, settings);
	}
}

static bool active_comp_todo_cmp(void *pa, void *pb, void *cl) {
	struct app *app = cl;
	const struct active_comp *a = pa, *b = pb;
	return todo_priority_cmp(app, a->ci, b->ci, true);
}

static void app_expand(struct app *app, enum comp_type type, ts expand_to) {
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		calendar_expand_instances_to(cal, type, expand_to);
	}
}

static void proj_active_events_add(void *_self, struct proj_item pi) {
	struct proj_active_events *self = _self;
	asrt(pi.ci->c->type == COMP_TYPE_EVENT, "");

	struct active_comp *ac = malloc_check(sizeof(struct active_comp));
	*ac = (struct active_comp){
		.ci = pi.ci,
		.cal_index = pi.cal_index,
		.cal = pi.cal,
		.settings = { .fade = false, .hide = false, .vis = true },

		/* this is just to copy the interval */
		.node = pi.ci->node,
		.node_by_view = pi.ci->node,
	};

	rb_insert(&self->unprocessed, &ac->node.node);
}
static void proj_active_events_clear(void *_self) {
	struct proj_active_events *self = _self;

	struct rb_iter iter = rb_iter(&self->unprocessed, RB_ITER_ORDER_POST);
	struct rb_node *x;
	while (rb_iter_next(&iter, &x)) {
		struct interval_node *nx = container_of(x,
			struct interval_node, node);
		struct active_comp *ac = container_of(nx,
			struct active_comp, node);
		free(ac);
	}

	for (int i = 0; i < self->processed.len; ++i) {
		struct active_comp *ac =
			*(struct active_comp**)vec_get(&self->processed, i);
		mgu_texture_destroy(&ac->tex);
		mgu_texture_destroy(&ac->loc_tex);
		free(ac);
	}
	vec_clear(&self->processed);
	rb_tree_init(&self->unprocessed, &interval_ops);
	rb_tree_init(&self->processed_not_hidden, &interval_ops);
	rb_tree_init(&self->in_view, &interval_ops);
	rb_tree_init(&self->not_in_view, &interval_ops);
}
static bool proj_active_events_type(void *_self, enum comp_type t) {
	return t == COMP_TYPE_EVENT;
}
static struct proj proj_active_events_init(struct proj_active_events *self,
		struct app *app) {
	self->app = app;
	self->processed = vec_new_empty(sizeof(struct active_comp *));
	rb_tree_init(&self->unprocessed,  &interval_ops);
	rb_tree_init(&self->processed_not_hidden, &interval_ops);
	rb_tree_init(&self->in_view, &interval_ops);
	rb_tree_init(&self->not_in_view, &interval_ops);
	return (struct proj){
		.self = self,
		.add = proj_active_events_add,
		.done = NULL,
		.clear = proj_active_events_clear,
		.type = proj_active_events_type,
	};
}
static void in_view_changed(struct app *app, struct active_comp *ac,
		bool in_view) {
	if (!in_view) {
		mgu_texture_destroy(&ac->tex);
		mgu_texture_destroy(&ac->loc_tex);
	} else {
		asrt(!ac->tex.tex && ! ac->loc_tex.tex, "ac tex");
	}
}
static void in_view_destroy_texs(struct app *app) {
	struct rb_iter iter =
		rb_iter(&app->active_events.in_view, RB_ITER_ORDER_IN);
	struct rb_node *x;
	while (rb_iter_next(&iter, &x)) {
		struct interval_node *nx =
			container_of(x, struct interval_node, node);
		struct active_comp *ac =
			container_of(nx, struct active_comp, node_by_view);
		mgu_texture_destroy(&ac->tex);
		mgu_texture_destroy(&ac->loc_tex);
	}
}
static void proj_active_events_process(struct proj_active_events *self,
		struct ts_ran ran) {
	struct interval_iter i_iter;
	struct interval_node *nx;

	/* first of all, fix up in_view and not_in_view for this new ran */
	long long int ran_left[] = { LLONG_MIN, ran.fr };
	long long int ran_in[] = { ran.fr, ran.to };
	long long int ran_right[] = { ran.to, LLONG_MAX };
	struct vec move = vec_new_empty(sizeof(struct rb_node *));
	i_iter = interval_iter(&self->not_in_view, ran_in);
	while (interval_iter_next(&i_iter, &nx)) {
		struct rb_node *x = &nx->node;
		vec_append(&move, &x);
	}
	for (int i = 0; i < move.len; ++i) {
		struct rb_node *x = *(struct rb_node **)vec_get(&move, i);
		struct interval_node *nx =
			container_of(x, struct interval_node, node);
		struct active_comp *ac =
			container_of(nx, struct active_comp, node_by_view);
		in_view_changed(self->app, ac, true);
		rb_delete(&self->not_in_view, x);
		rb_insert(&self->in_view, x);
	}
	vec_clear(&move);
	// TODO: there is no "not in range" query, so this is the best we can do
	i_iter = interval_iter(&self->in_view, ran_left);
	while (interval_iter_next(&i_iter, &nx)) {
		struct rb_node *x = &nx->node;
		if (!interval_overlap(nx->ran, ran_in)) {
			vec_append(&move, &x);
		}
	}
	i_iter = interval_iter(&self->in_view, ran_right);
	while (interval_iter_next(&i_iter, &nx)) {
		struct rb_node *x = &nx->node;
		if (!interval_overlap(nx->ran, ran_in)
				&& !interval_overlap(nx->ran, ran_left)) {
			vec_append(&move, &x);
		}
	}
	for (int i = 0; i < move.len; ++i) {
		struct rb_node *x = *(struct rb_node **)vec_get(&move, i);
		struct interval_node *nx =
			container_of(x, struct interval_node, node);
		struct active_comp *ac =
			container_of(nx, struct active_comp, node_by_view);
		in_view_changed(self->app, ac, false);
		rb_delete(&self->in_view, x);
		rb_insert(&self->not_in_view, x);
	}
	vec_free(&move);

	int from = self->processed.len;
	i_iter = interval_iter(&self->unprocessed, ran_in);
	while (interval_iter_next(&i_iter, &nx)) {
		struct active_comp *ac =
			container_of(nx, struct active_comp, node);
		struct proj_item pi = {
			.ci = ac->ci,
			.cal_index = ac->cal_index,
			.cal = ac->cal,
		};
		execute_current_filter(self->app, &pi, &ac->settings);
		vec_append(&self->processed, &ac);
	}
	for (int i = from; i < self->processed.len; ++i) {
		struct active_comp *ac =
			*(struct active_comp**)vec_get(&self->processed, i);
		rb_delete(&self->unprocessed, &ac->node.node);
		if (ac->settings.vis) {
			rb_insert(&self->processed_not_hidden, &ac->node.node);
			if (interval_overlap(ran_in, ac->node.ran)) {
				in_view_changed(self->app, ac, true);
				rb_insert(&self->in_view,
					&ac->node_by_view.node);
			} else {
				rb_insert(&self->not_in_view,
					&ac->node_by_view.node);
			}
		}
	}
}
static void proj_active_todos_add(void *_self, struct proj_item pi) {
	struct proj_active_todos *self = _self;
	asrt(pi.ci->c->type == COMP_TYPE_TODO, "");

	/* retrieve some properties */
	enum prop_status status;
	bool has_status = props_get_status(pi.ci->p, &status);
	enum prop_class class;
	props_get_class(pi.ci->p, &class);

	struct active_comp ac = {
		.ci = pi.ci,
		.cal_index = pi.cal_index,
		.cal = pi.cal,
		.settings = { .fade = false, .hide = false, .vis = true },
	};

	if (has_status && (status == PROP_STATUS_COMPLETED
			|| status == PROP_STATUS_CANCELLED)) {
		ac.settings.vis = false;
	}

	/* execute filter expressions */
	execute_current_filter(self->app, &pi, &ac.settings);
	if (!ac.settings.vis) return;

	vec_append(&self->v, &ac);
}
static void proj_active_todos_done(void *_self) {
	struct proj_active_todos *self = _self;
	vec_sort(&self->v, &active_comp_todo_cmp, self->app);
}
static void proj_active_todos_clear(void *_self) {
	struct proj_active_todos *self = _self;
	vec_clear(&self->v);
}
static bool proj_active_todos_type(void *_self, enum comp_type t) {
	return t == COMP_TYPE_TODO;
}
static struct proj proj_active_todos_init(struct proj_active_todos *self,
		struct app *app) {
	self->app = app;
	self->v = vec_new_empty(sizeof(struct active_comp));
	return (struct proj){
		.self = self,
		.add = proj_active_todos_add,
		.done = proj_active_todos_done,
		.clear = proj_active_todos_clear,
		.type = proj_active_todos_type,
	};
}
static void proj_alarm_add(void *_self, struct proj_item pi) {
	struct proj_alarm *self = _self;

	ts due = 0, start = 0;
	bool has_due = props_get_due(pi.ci->p, &due);
	bool has_start = props_get_start(pi.ci->p, &start);
	(void)has_start;
	struct alarm_comp alc = {
		.pi = pi,
		.node.val = has_due ? due : start
	};
	execute_filter(self->app, self->uexpr_filter, &alc.pi, &alc.settings);
	if (alc.settings.vis) {
		struct alarm_comp *alc_p =
			malloc_check(sizeof(struct alarm_comp));
		*alc_p = alc;
		rb_insert(&self->tree, &alc_p->node.node);
	}
}
static void proj_alarm_done(void *_self) {
	struct proj_alarm *self = _self;

	struct timespec now;
	asrt(clock_gettime(CLOCK_REALTIME, &now) == 0, "");
	struct rb_integer_node *n =
		rb_integer_min_greater(&self->tree, now.tv_sec);

	self->next = NULL;
	if (n) {
		struct alarm_comp *alc =
			container_of(n, struct alarm_comp, node);
		self->next = alc;
		pu_log_info("[alarm] next at: %lld [now: %ld], %s\n",
			n->val, now.tv_sec, props_get_summary(alc->pi.ci->p));
		struct timespec ts = { .tv_sec = n->val };
		event_loop_timer_set_abs(&self->app->alarm_timer, ts);
	} else {
		pu_log_info("[alarm] no next [now: %ld]\n", now.tv_sec);
	}
}
static void alarm_cb(void *env) {
	struct app *app = env;
	struct alarm_comp *alc = app->alarm_comps.next;
	asrt(alc, "alarm next");
	const char *summary = props_get_summary(alc->pi.ci->p);
	pu_log_info("[alarm] ALARM for %s\n", summary);
	const char *argv[] = { "-", summary, NULL };
	subprocess_shell(app->alarm_comps.shell_cmd, argv);
	proj_alarm_done(&app->alarm_comps);
}
static void proj_alarm_clear(void *_self) {
	struct proj_alarm *self = _self;

	struct rb_iter iter = rb_iter(&self->tree, RB_ITER_ORDER_POST);
	struct rb_node *x;
	while (rb_iter_next(&iter, &x)) {
		struct rb_integer_node *nx =
			container_of(x, struct rb_integer_node, node);
		struct alarm_comp *alc =
			container_of(nx, struct alarm_comp, node);
		free(alc);
	}

	rb_tree_init(&self->tree, &rb_integer_ops);
}
static struct proj proj_alarm_init(struct proj_alarm *self, struct app *app) {
	self->app = app;
	self->next = NULL;
	rb_tree_init(&self->tree, &rb_integer_ops);
	self->uexpr_filter = -1;
	return (struct proj){
		.self = self,
		.add = proj_alarm_add,
		.done = proj_alarm_done,
		.clear = proj_alarm_clear,
		.type = NULL,
	};
}

void app_update_projections(struct app *app) {
	app_expand(app, COMP_TYPE_EVENT, app->expand_to);
	app_expand(app, COMP_TYPE_TODO, app->expand_to);

	// push all expanded comp_insts to projs
	bool any = false;
	struct vec remove = vec_new_empty(sizeof(struct interval_node *));
	for (int t = 0; t < COMP_TYPE_N; ++t) {
		for (int j = 0; j < app->cals.len; ++j) {
			struct calendar *cal = vec_get(&app->cals, j);
			enum comp_type type = t;

			struct rb_iter iter =
				rb_iter(&cal->cis[type], RB_ITER_ORDER_IN);
			struct rb_node *x;
			while (rb_iter_next(&iter, &x)) {
				struct interval_node *nx = container_of(x,
					struct interval_node, node);
				struct comp_inst *ci = container_of(nx,
					struct comp_inst, node);
				for (int i = 0; i < app->projs.len; ++i) {
					struct proj *p =
						vec_get(&app->projs, i);
					if (p->type && !p->type(p->self, type))
						continue;
					if (p->add) p->add(p->self,
							(struct proj_item){
						.ci = ci,
						.cal_index = j,
						.cal = cal
					});
				}
				vec_append(&remove, &nx);

			}

			if (remove.len > 0) any = true;
			for (int k = 0; k < remove.len; ++k) {
				struct interval_node **ni = vec_get(&remove, k);
				rb_delete(&cal->cis[type], &(*ni)->node);
				struct comp_inst *ci = container_of(*ni,
					struct comp_inst, node);
				vec_append(&app->cis, &ci);
			}
			vec_clear(&remove);
		}
	}
	vec_free(&remove);
	if (any) {
		for (int i = 0; i < app->projs.len; ++i) {
			struct proj *p = vec_get(&app->projs, i);
			if (p->done) p->done(p->self);
		}
	}
}

static void app_invalidate_calendars(struct app *app) {
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		cal->cis_dirty[COMP_TYPE_EVENT] = true;
		cal->cis_dirty[COMP_TYPE_TODO] = true;
	}

	app->expand_to = app->now + 3600 * 24 * 365;
	for (int i = 0; i < app->projs.len; ++i) {
		struct proj *p = vec_get(&app->projs, i);
		p->clear(p->self);
	}

	for (int i = 0; i < app->cis.len; ++i) {
		struct comp_inst **ci = vec_get(&app->cis, i);
		free(*ci);
	}
	vec_clear(&app->cis);
}
static void app_reload_calendars(struct app *app) {
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		update_calendar_from_storage(cal, app->zone);
	}
	app_invalidate_calendars(app);
}
void app_use_view(struct app *app, struct ts_ran view) {
	proj_active_events_process(&app->active_events, view);
}

static void app_mark_dirty(struct app *app) {
	app->dirty = true;
	mgu_win_surf_mark_dirty(app->win);
}

static bool apply_edit_spec_with_mod_time(struct app *app,
		struct edit_spec *es, struct calendar *cal) {
	/* check if there are actually any changes */
	if (edit_spec_is_identity(es, cal)) {
		fprintf(stderr, "[editor] identity edit, no changes\n");
		return true;
	}

	/* set LAST-MODIFIED */
	props_set_last_modified(&es->p, ts_now());

	/* apply edit */
	if (apply_edit_spec_to_calendar(es, cal) == 0) {
		app_invalidate_calendars(app);
		app_mark_dirty(app);
	} else {
		fprintf(stderr, "[editor] error: could not save edit\n");
		return false;
	}

	return true;
}

static void app_mode_select_finish(struct app *app) {
	app->keystate = KEYSTATE_BASE;
	app_mark_dirty(app);
	struct active_comp *ac = NULL;
	if (app->main_view == VIEW_CALENDAR) {
		struct active_comp *res = NULL;

		struct rb_iter iter = rb_iter(
			&app->active_events.processed_not_hidden,
			RB_ITER_ORDER_IN);
		struct rb_node *x;
		while (rb_iter_next(&iter, &x)) {
			struct interval_node *nx =
				container_of(x, struct interval_node, node);
			struct active_comp *ac =
				container_of(nx, struct active_comp, node);

			if (strncmp(ac->code, app->mode_select_code,
					app->mode_select_code_n) == 0) {
				res = ac;
				break;
			}
		}

		if (res) {
			fprintf(stderr, "selected comp: %s\n",
				props_get_summary(res->ci->p));
			ac = res;
		}
	} else if (app->main_view == VIEW_TODO) {
		for (int i = 0; i < app->active_todos.v.len; ++i) {
			struct active_comp *aci =
				vec_get(&app->active_todos.v, i);
			if (strncmp(aci->code, app->mode_select_code,
					app->mode_select_code_n) == 0) {
				fprintf(stderr, "selected comp: %s\n",
					props_get_summary(aci->ci->p));
				ac = aci;
				break;
			}
		}
	} else {
		asrt(false, "unknown mode");
	}

	if (app->mode_select_uexpr_fn != -1) {
		struct proj_item pi = {
			.ci = ac->ci,
			.cal_index = ac->cal_index,
			.cal = ac->cal,
		};
		struct cal_uexpr_env env = {
			.app = app,
			.kind = CAL_UEXPR_FILTER | CAL_UEXPR_ACTION,
			.pi = &pi,
			.set_props = props_empty,
			.set_edit = false
		};
		struct uexpr_ops ops = {
			.env = &env,
			.try_get_var = cal_uexpr_get,
			.try_set_var = cal_uexpr_set
		};
		uexpr_ctx_set_ops(app->uexpr_ctx, ops);
		uexpr_eval(&app->uexpr, app->mode_select_uexpr_fn,
			app->uexpr_ctx, NULL);

		if (env.set_edit) {
			struct edit_spec es;
			edit_spec_init(&es);
			es.method = EDIT_METHOD_UPDATE;
			es.uid = str_copy(&ac->ci->c->uid);
			es.p = env.set_props;
			es.type = ac->ci->c->type;

			apply_edit_spec_with_mod_time(app, &es, ac->cal);

			edit_spec_finish(&es);
		}
	}
}
static void app_mode_select_append_sym(struct app *app, char sym) {
	app->mode_select_code[app->mode_select_code_n++] = sym;
	if (app->mode_select_code_n >= app->mode_select_len) {
		app_mode_select_finish(app);
	}
}

void app_cmd_move_view_discrete(struct app *app, int n) {
	struct ts_ran view = app->view;

	int len_days = (view.to - view.fr + 3600 * 12) / (3600 * 24);

	struct simple_date sd = simple_date_from_ts(view.fr, app->zone);
	sd.second = sd.minute = sd.hour = 0;

	sd.day += len_days * n;
	simple_date_normalize(&sd);
	app->view.fr = simple_date_to_ts(sd, app->zone);

	sd.day += len_days;
	simple_date_normalize(&sd);
	app->view.to = simple_date_to_ts(sd, app->zone);

	app_mark_dirty(app);
}
void app_cmd_editor(struct app *app, FILE *in) {
	struct str in_s = str_empty;
	int c;
	while ((c = getc(in)) != EOF) str_append_char(&in_s, c);
	fclose(in);

	if (!str_any(&in_s)) return;

	FILE *f = fmemopen((void*)in_s.v.d, in_s.v.len, "r");
	asrt(f, "");

	struct edit_spec es;
	int res = edit_spec_init_parse(&es, f, app->zone, app->now);
	fclose(f);
	if (res != 0) {
		fprintf(stderr, "[editor] can't parse edit template.\n");
		app_launch_editor_str(app, &in_s); /* relaunch editor */
		goto cleanup;
	}

	/* generate uid */
	if (es.method == EDIT_METHOD_CREATE && !str_any(&es.uid)) {
		char uid_buf[64];
		generate_uid(uid_buf);
		str_append(&es.uid, uid_buf, strlen(uid_buf));
	}

	/* determine calendar */
	struct calendar *cal = NULL;
	if (es.calendar_num > 0) {
		if (es.calendar_num >= 1 && es.calendar_num <= app->cals.len) {
			cal = vec_get(&app->cals, es.calendar_num - 1);
		}
	}
	if (!cal) {
		fprintf(stderr, "[editor] calendar not specified.\n");
		app_launch_editor_str(app, &in_s); /* relaunch editor */
		goto cleanup_es;
	}

	if (!apply_edit_spec_with_mod_time(app, &es, cal)) {
		app_launch_editor_str(app, &in_s); /* relaunch editor */
	}

cleanup_es:
	edit_spec_finish(&es);
cleanup:
	str_free(&in_s);
}
void app_cmd_reload(struct app *app) {
	app_reload_calendars(app);
}
void app_cmd_activate_filter(struct app *app, int n) {
	if (app->current_filter != n) {
		if (n < 0 || n >= app->filters.len) {
			app->current_filter = -1;
		} else {
			app->current_filter = n;
		}
		app_invalidate_calendars(app);
		app_mark_dirty(app);
	}
}
void app_cmd_view_today(struct app *app, int n) {
	app->view.fr = ts_get_day_base(app->now, app->zone, true);
	app->view.to = app->view.fr + 3600 * 24 * 7;
	app_mark_dirty(app);
}
void app_cmd_toggle_show_private(struct app *app, int n) {
	// TODO
}
void app_cmd_switch_view(struct app *app, int n) {
	if (n == -1) {
		app->main_view = (app->main_view + 1) % VIEW_N;
		app_mark_dirty(app);
	} else if (0 <= n && n < VIEW_N) {
		app->main_view = n;
		app_mark_dirty(app);
	}
}
void app_cmd_select_comp_uexpr(struct app *app, int uexpr_fn) {
	app_switch_mode_select(app);
	app->mode_select_uexpr_fn = uexpr_fn;
	app_mark_dirty(app);
}

static void run_action(struct app *app, struct action *act) {
	struct cal_uexpr_env env = {
		.app = app,
		.kind = CAL_UEXPR_ACTION,
	};
	struct uexpr_ops ops = {
		.env = &env,
		.try_get_var = cal_uexpr_get,
		.try_set_var = cal_uexpr_set
	};

	if (act->uexpr_fn != -1) {
		uexpr_ctx_set_ops(app->uexpr_ctx, ops);
		uexpr_eval(&app->uexpr, act->uexpr_fn, app->uexpr_ctx, NULL);
	}
}

static void handle_key(struct app *app, uint32_t key) {
	int n;
	char sym = key_get_sym(key);
	switch (app->keystate) {
	case KEYSTATE_SELECT:
		if (key_is_gen(key)) {
			app_mode_select_append_sym(app, key_get_sym(key));
		} else {
			app->keystate = KEYSTATE_BASE;
			app_mark_dirty(app);
		}
		break;
	case KEYSTATE_BASE:
		for (int i = 0; i < app->actions.len; ++i) {
			struct action *act = vec_get(&app->actions, i);
			if (sym == act->key_sym) {
				if (act->cond.view == app->main_view) {
					run_action(app, act);
					return;
				}
			}
		}

		switch (sym) {
		case 'r':
			app_cmd_reload(app);
			break;
		case '\0':
			if ((n = key_num(key)) > 0) {
				app_cmd_activate_filter(app, n - 1);
			}
			break;
		default:
			asrt(key_is_sym(key), "bad key symbol");
			break;
		}
		break;
	case KEYSTATE_VIEW_SWITCH: {
		bool update = false;
		switch(sym) {
		case 'h':
			// app->tview_n = 12;
			// app->tview_type = TVIEW_MONTHS;
			update = true;
			break;
		case 'j':
			// app->tview_n = 4;
			// app->tview_type = TVIEW_WEEKS;
			update = true;
			break;
		case 'k':
			// app->tview_n = 7;
			// app->tview_type = TVIEW_DAYS;
			update = true;
			break;
		case 'l':
			// app->tview_n = 1;
			// app->tview_type = TVIEW_DAYS;
			update = true;
			break;
		}
		if (update) {
			// app_update_active_objects(app);
			app_mark_dirty(app);
		}
		app->keystate = KEYSTATE_BASE;
		break;
	}
	default:
		asrt(false, "bad keystate");
		break;
	}
}

static void application_handle_child(struct app *app, int pidfd) {
	if (app->sp->pidfd == pidfd) {
		FILE *f = subprocess_get_result(&app->sp);
		if (!f) return;
		app_cmd_editor(app, f);
	}
}

static void application_handle_input(void *ud, struct mgu_win_surf *surf,
		struct mgu_input_event_args ev) {
	struct app *app = ud;
	if (ev.t & MGU_KEYBOARD) {
		if (ev.t & MGU_DOWN) {
			handle_key(app, ev.keyboard.down.key);
		}
	}
	if (ev.t & MGU_TOUCH) {
		const double *p = ev.touch.down_or_move.p;
		if (ev.t & MGU_DOWN) {
			libtouch_surface_down(app->touch_surf, ev.time,
				ev.touch.id, (float[]){ p[0], p[1] });
			for (int i = 0; i < app->tap_areas.len; ++i) {
				struct tap_area *ta =
					vec_get(&app->tap_areas, i);
				if (p[0] >= ta->aabb[0]
					&& p[0] < ta->aabb[0] + ta->aabb[2]
					&& p[1] >= ta->aabb[1]
					&& p[1] < ta->aabb[1] + ta->aabb[3]) {
					struct action *act =
						vec_get(&app->actions,
						ta->action_idx);
					run_action(app, act);
				}
			}
		} else if (ev.t & MGU_MOVE) {
			libtouch_surface_motion(app->touch_surf, ev.time,
				ev.touch.id, (float[]){ p[0], p[1] });
		} else if (ev.t & MGU_UP) {
			libtouch_surface_up(app->touch_surf, ev.time,
				ev.touch.id);
		}
		app_mark_dirty(app);
	}
}

static const char * get_comp_color(void *ptr) {
	/* we only get the color from the base instance of the recur set */
	struct comp *c = ptr;
	return props_get_color(&c->p);
}

static void touch_end(void *env, struct libtouch_gesture_data data) {
	struct app *app = env;
	struct libtouch_rt rt = data.rt;

	// TODO: code duplication
	float g = libtouch_rt_scaling(&rt);
	double a = app->view.fr, b = app->view.to;
	b = a + (b - a) / g;
	double tx = (rt.t1 / app->window_width) * (b - a);
	a -= tx, b -= tx;
	struct ts_ran view = { a, b };
	app->view = view;

	in_view_destroy_texs(app);
}

static void app_add_uexpr_config_file(struct app *app, FILE *f) {
	int root = -1;
	root = uexpr_parse(&app->uexpr, f);
	if (root != -1) {
		uexpr_eval(&app->uexpr, root, app->uexpr_ctx, NULL);
	} else {
		fprintf(stderr, "WARNING: could not parse script.\n");
	}
}
void app_add_uexpr_config(struct app *app, const char *path) {
	FILE *f = fopen(path, "r");
	app_add_uexpr_config_file(app, f);
	fclose(f);
}

void context_cb(void *env, bool have_ctx) {
	struct app *app = env;
	if (have_ctx) {
		app->sr = sr_create_opengl(app->plat);

		app->out = mgu_disp_get_default_output(app->win->disp);
		asrt(app->out, "app->out NULL");

		w_sidebar_init(&app->w_sidebar, app);
	} else {
		sr_destroy(app->sr);
		in_view_destroy_texs(app);
		w_sidebar_finish(&app->w_sidebar);
	}
}

void app_init(struct app *app, struct application_options opts,
		struct platform *plat, struct mgu_win_surf *win) {
	struct stopwatch sw = sw_start();

	*app = (struct app){
		.cals = VEC_EMPTY(sizeof(struct calendar)),
		.cal_infos = VEC_EMPTY(sizeof(struct calendar_info)),
		.projs = VEC_EMPTY(sizeof(struct proj)),
		.cis = VEC_EMPTY(sizeof(struct comp_inst *)),
		.main_view = VIEW_CALENDAR,
		.keystate = KEYSTATE_BASE,
		.show_private_events = opts.show_private_events,
		.tap_areas = VEC_EMPTY(sizeof(struct tap_area)),
		.window_width = -1, .window_height = -1,
		.requested_timezone = "UTC",
		.editor_args = VEC_EMPTY(sizeof(struct str)),
		.filters = VEC_EMPTY(sizeof(struct filter)),
		.current_filter = -1,
		.actions = VEC_EMPTY(sizeof(struct action)),
		.win = win,
		.plat = plat,
	};

	struct proj p;
	p = proj_active_events_init(&app->active_events, app);
	vec_append(&app->projs, &p);
	p = proj_active_todos_init(&app->active_todos, app);
	vec_append(&app->projs, &p);
	p = proj_alarm_init(&app->alarm_comps, app);
	vec_append(&app->projs, &p);

	/* load all uexpr stuff */
	uexpr_init(&app->uexpr);
	app->uexpr_ctx = uexpr_ctx_create();

	struct cal_uexpr_env env = {
		.app = app,
		.kind = CAL_UEXPR_CONFIG,
	};
	struct uexpr_ops ops = {
		.env = &env,
		.try_get_var = cal_uexpr_get,
		.try_set_var = cal_uexpr_set
	};
	uexpr_ctx_set_ops(app->uexpr_ctx, ops);

	// try command line config file
	if (opts.config_file) {
		pu_log_info("[config] loading: `%s`\n", opts.config_file);
		app_add_uexpr_config(app, opts.config_file);
		goto config_found;
	}

	// try to find config file in platform specific config dir
	const char *config_dir = pu_get_config_dir();
	if (config_dir) {
		struct str config_path = str_new_from_cstr(config_dir);
		const char *config_fname = "/config.uexpr";
		str_append(&config_path, config_fname, strlen(config_fname));
		FILE *f = fopen(str_cstr(&config_path), "r");
		if (f) {
			pu_log_info("[config] loading: `%s`\n",
				str_cstr(&config_path));
			app_add_uexpr_config_file(app, f);
			fclose(f);
			goto config_found;
		} else {
			pu_log_info("[config] not found (%s): `%s`\n",
				strerror(errno),
				str_cstr(&config_path));
		}
	}

	// load compiled-in default config
	struct pu_asset default_uexpr = pu_assets_get(default_uexpr);
	FILE *f = fmemopen(default_uexpr.data, default_uexpr.size, "r");
	if (f) {
		pu_log_info("[config] loading builtin\n");
		app_add_uexpr_config_file(app, f);
		fclose(f);
		goto config_found;
	}
config_found: ;

	// TODO: state.view_days = opts.view_days;

	const char *editor_buffer = "vim", *term_buffer = "st";
	const char *editor_env = getenv("EDITOR");
	if (editor_env) editor_buffer = editor_env;

	if (opts.editor) editor_buffer = opts.editor;
	if (opts.terminal) term_buffer = opts.terminal;

	asrt(editor_buffer[0], "please set editor!");
	asrt(term_buffer[0], "please set terminal emulator!");

	struct str s;
	s = str_new_from_cstr(term_buffer); vec_append(&app->editor_args, &s);
	s = str_new_from_cstr(term_buffer); vec_append(&app->editor_args, &s);
	s = str_new_from_cstr(editor_buffer); vec_append(&app->editor_args, &s);
	s = str_new_from_cstr("{file}"); vec_append(&app->editor_args, &s);

	fprintf(stderr, "editor command: %s, term command: %s\n",
		editor_buffer, term_buffer);

	pu_log_info("[init] loading timezone: %s\n", app->requested_timezone);
	app->zone = cal_timezone_new(app->requested_timezone);
	app->now = ts_now();
	app->view.fr = ts_get_day_base(app->now, app->zone, true);
	app->view.to = app->view.fr + 3600 * 24 * 7;

	app->expand_to = app->now + 3600 * 24 * 365;
	app_expand(app, COMP_TYPE_EVENT, app->expand_to);
	app_expand(app, COMP_TYPE_TODO, app->expand_to);

	app->touch_surf = libtouch_surface_create();
	memcpy(app->touch_aabb, &(float[]) { 120, 0, 5000, 5000 },
		sizeof(float) * 4);
	app->touch_area = libtouch_surface_add_area(
		app->touch_surf,
		app->touch_aabb,
		(struct libtouch_area_opts){
			.g = LIBTOUCH_TSR,
			.env = app,
			.end = touch_end
		}
	);

	app->text = mgu_text_create(app->plat);

	app->slicing = slicing_create(app->zone);

	app->win->disp->seat.cb = (struct mgu_seat_cb){
		.env = app, .f = application_handle_input };
	app->win->disp->render_cb = (struct mgu_render_cb){
		.env = app, .f = render_application };

	app->init_done = true;

	mgu_disp_set_context_cb(app->win->disp, (struct mgu_context_cb){
		.env = app, .f = context_cb });

	app->event_loop = event_loop_create(plat);
	mgu_disp_add_to_event_loop(app->win->disp, app->event_loop);

	event_loop_timer_init(&app->alarm_timer, app->event_loop,
		app, alarm_cb);

	app_mark_dirty(app);
	sw_end_print(sw, "initialization");
}

void app_main(struct app *app) {
	event_loop_run(app->event_loop);
}

void app_finish(struct app *app) {
	event_loop_timer_finish(&app->alarm_timer);

	event_loop_destroy(app->event_loop);

	mgu_text_destroy(app->text);

	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		struct calendar_info *cal_info = vec_get(&app->cal_infos, i);
		calendar_finish(cal);
		uexpr_value_finish(cal_info->uexpr_tag);
	}
	vec_free(&app->cals);
	vec_free(&app->cal_infos);

	for (int i = 0; i < app->actions.len; ++i) {
		struct action *act = vec_get(&app->actions, i);
		str_free(&act->label);
	}
	vec_free(&app->actions);

	for (int i = 0; i < app->editor_args.len; ++i) {
		struct str *s = vec_get(&app->editor_args, i);
		str_free(s);
	}
	vec_free(&app->editor_args);

	for (int i = 0; i < app->filters.len; ++i) {
		struct filter *f = vec_get(&app->filters, i);
		str_free(&f->desc);
	}
	vec_free(&app->filters);

	// tslice_finish(&app->slice_main);
	// tslice_finish(&app->slice_top);

	vec_free(&app->projs);
	proj_active_events_clear(&app->active_events);
	vec_free(&app->active_todos.v);
	for (int i = 0; i < app->cis.len; ++i) {
		struct comp_inst **ci = vec_get(&app->cis, i);
		free(*ci);
	}
	vec_free(&app->cis);

	cal_timezone_destroy(app->zone);

	libtouch_surface_destroy(app->touch_surf);

	if (app->uexpr_ctx) uexpr_ctx_destroy(app->uexpr_ctx);
	uexpr_finish(&app->uexpr);

	slicing_destroy(app->slicing);
	vec_free(&app->tap_areas);
}

int app_add_cal(struct app *app, const char *path) {
	asrt(!app->init_done, "");
	struct calendar cal;
	struct calendar_info cal_info;
	calendar_init(&cal);
	cal.storage = str_wordexp(path);

	update_calendar_from_storage(&cal, app->zone);
	pu_log_info("add_cal %s, comps: %d\n", str_cstr(&cal.storage),
		cal.comps_vec.len);

	/* calculate most frequent color */
	const char *fc = most_frequent(&cal.comps_vec, &get_comp_color);
	uint32_t color = fc ? lookup_color(fc, strlen(fc)) : 0;
	if (!color) color = 0xFF20D0D0;
	cal_info.color = color;

	vec_append(&app->cals, &cal);
	return vec_append(&app->cal_infos, &cal_info);
}
void app_add_uexpr_filter(struct app *app, const char *key,
		int def_cal, int uexpr_fn) {
	asrt(!app->init_done, "");
	struct filter f = {
		.desc = str_new_from_cstr(key),
		.def_cal = def_cal,
		.uexpr_fn = uexpr_fn
	};
	vec_append(&app->filters, &f);
}
void app_add_action(struct app *app, struct action act) {
	asrt(!app->init_done, "");
	vec_append(&app->actions, &act);
}
