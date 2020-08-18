#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "application.h"
#include "core.h"
#include "util.h"
#include "algo.h"
#include "keyboard.h"
#include "backend.h"
#include "render.h"
#include "editor.h"

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

struct print_template_cl {
	struct app *app;
	struct active_comp *ac;
};
static void print_template_callback(void *_cl, FILE *f) {
	struct print_template_cl *cl = _cl;
	print_template(f, cl->ac->ci, cl->app->zone, cl->ac->cal_index + 1);
}
static int get_default_cal_index(struct app *app) {
	asrt(app->cals.len > 0, "no calendars");
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
static void app_launch_editor(struct app *app, struct active_comp *ac) {
	if (!app->sp) {
		struct print_template_cl cl = { .app = app, .ac = ac };
		const char **args = new_editor_args(app);
		struct str *s = vec_get(&app->editor_args, 0);
		app->sp = subprocess_new_input(str_cstr(s),
			args, &print_template_callback, &cl);
		free(args);
	}
}
static void app_launch_editor_str(struct app *app, const struct str *str) {
	if (!app->sp) {
		const char **args = new_editor_args(app);
		struct str *s = vec_get(&app->editor_args, 0);
		app->sp = subprocess_new_input(str_cstr(s),
			args, &print_str_callback, (void*)str);
		free(args);
	}
}
static void app_launch_new_editor(struct app *app) {
	void (*cb)(void*, FILE*);
	if (app->main_view == VIEW_CALENDAR) {
		cb = &print_new_event_template_callback;
	} else {
		cb = &print_new_todo_template_callback;
	}
	if (!app->sp) {
		const char **args = new_editor_args(app);
		struct str *s = vec_get(&app->editor_args, 0);
		app->sp = subprocess_new_input(str_cstr(s), args, cb, app);
		free(args);
	}
}

struct ac_iter_assign_code_env {
	struct app *app;
	struct vec *acs; /* vec<struct active_comp*> */
};
static void ac_iter_assign_code(void *_env, struct rb_node *x) {
	struct interval_node *nx = container_of(x, struct interval_node, node);
	struct active_comp *ac = container_of(nx, struct active_comp, node);
	struct ac_iter_assign_code_env *env = _env;

	if (ts_ran_overlap(ac->ci->time, env->app->view)) {
		vec_append(env->acs, &ac);
	} else {
		ac->code[0] = '\0';
	}
}
static void app_switch_mode_select(struct app *app) {
	if (app->keystate == KEYSTATE_SELECT) return;
	app->mode_select_code_n = 0;
	app->keystate = KEYSTATE_SELECT;

	// TODO: code duplication
	if (app->main_view == VIEW_CALENDAR) {
		struct vec acs = vec_new_empty(sizeof(struct active_comp *));
		struct ac_iter_assign_code_env env = { app, &acs };
		rb_iter(&app->active_events, &env, ac_iter_assign_code);

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
		key_gen_init(app->active_todos.len, &g);
		app->mode_select_len = g.k;
		for (int i = 0; i < app->active_todos.len; ++i) {
			struct active_comp *ac = vec_get(&app->active_todos, i);
			const char *code = key_gen_get(&g);
			asrt(code, "not enough codes");
			strcpy(ac->code, code);
		}
	} else {
		asrt(false, "unknown view");
	}
}
struct ac_iter_find_code_env {
	struct app *app;
	struct active_comp *res;
};
static void ac_iter_find_code(void *_env, struct rb_node *x) {
	struct interval_node *nx = container_of(x, struct interval_node, node);
	struct active_comp *ac = container_of(nx, struct active_comp, node);
	struct ac_iter_find_code_env *env = _env;

	if (strncmp(ac->code, env->app->mode_select_code,
			env->app->mode_select_code_n) == 0) {
		env->res = ac;
	}
}
static void app_mode_select_finish(struct app *app) {
	//TODO: fix code duplication
	app->keystate = KEYSTATE_BASE;
	app->dirty = true;
	if (app->main_view == VIEW_CALENDAR) {
		struct ac_iter_find_code_env env = { app, NULL };
		rb_iter(&app->active_events, &env, ac_iter_find_code);
		if (env.res) {
			fprintf(stderr, "selected comp: %s\n",
				props_get_summary(env.res->ci->p));
			app_launch_editor(app, env.res);
		}
	} else if (app->main_view == VIEW_TODO) {
		for (int i = 0; i < app->active_todos.len; ++i) {
			struct active_comp *ac = vec_get(&app->active_todos, i);
			if (strncmp(ac->code, app->mode_select_code,
					app->mode_select_code_n) == 0) {
				fprintf(stderr, "selected comp: %s\n",
					props_get_summary(ac->ci->p));
				app_launch_editor(app, ac);
				break;
			}
		}
	} else {
		asrt(false, "unknown mode");
	}
}

static void app_mode_select_append_sym(struct app *app, char sym) {
	app->mode_select_code[app->mode_select_code_n++] = sym;
	if (app->mode_select_code_n >= app->mode_select_len) {
		app_mode_select_finish(app);
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
//				 .start = has_start ? start : -1, .estimated_duration = est };
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

static void execute_filters(struct app *app, struct active_comp *ac) {
	/* apply builtin filter */
	if (app->builtin_expr && ac->ci->c->type == COMP_TYPE_EVENT) {
		uexpr_fn fn = uexpr_ctx_get_fn(app->builtin_expr_ctx, "filter_0");
		uexpr_ctx_set_handlers(app->builtin_expr_ctx, &uexpr_cal_ac_get,
				&uexpr_cal_ac_set, ac);
		uexpr_eval_fn(app->builtin_expr_ctx, fn);
	}

	/* apply current_filter_fn */
	if (app->current_filter_fn && app->config_expr) {
		uexpr_fn fn = uexpr_ctx_get_fn(app->config_ctx, app->current_filter_fn);
		uexpr_ctx_set_handlers(app->config_ctx, &uexpr_cal_ac_get,
				&uexpr_cal_ac_set, ac);
		uexpr_eval_fn(app->config_ctx, fn);
	}

	/* apply runtime configured filter */
	if (app->expr) {
		uexpr_ctx ctx = uexpr_ctx_create(app->expr);
		uexpr_ctx_set_handlers(ctx, &uexpr_cal_ac_get, &uexpr_cal_ac_set, ac);
		uexpr_eval(ctx);
		uexpr_ctx_destroy(ctx);
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

struct cis_tree_iter_env {
	struct app *app;
	int cal_index;
	struct calendar *cal;
	struct vec *acs;
};
static void cis_tree_iter(void *_env, struct interval_node *x) {
	struct cis_tree_iter_env *env = _env;
	struct app *app = env->app;
	struct comp_inst *ci = container_of(x, struct comp_inst, node);

	struct active_comp ac = {
		.ci = ci,
		.cal_index = env->cal_index,
		.fade = false, .hide = false, .vis = true,
		.cal = env->cal,
		.node = ci->node /* this is just to copy the interval */
	};

	/* execute filter expressions */
	execute_filters(app, &ac);
	if (!ac.vis) return;

	vec_append(env->acs, &ac);
}
static void app_filter_active_events(struct app *app, struct ts_ran view) {
	struct vec acs = vec_new_empty(sizeof(struct active_comp));
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);

		struct cis_tree_iter_env env = {
			.app = app, .cal_index = i, .cal = cal, .acs = &acs };
		interval_query(
			&cal->cis[COMP_TYPE_EVENT],
			(long long int[]){ view.fr, view.to },
			&env,
			cis_tree_iter
		);

		for (int k = 0; k < acs.len; ++k) {
			struct active_comp *aci = vec_get(&acs, k);
			struct active_comp *ac = malloc_check(sizeof(struct active_comp));
			*ac = *aci;
			rb_delete(&cal->cis[COMP_TYPE_EVENT], &ac->ci->node.node);
			rb_insert(&app->active_events, &ac->node.node);
			++app->active_events_n;
		}
		vec_clear(&acs);
	}
}
struct todo_cis_tree_iter_env {
	struct app *app;
	int cal_index;
	struct calendar *cal;
};
static void todo_cis_tree_iter(void *_env, struct rb_node *x) {
	struct interval_node *nx = container_of(x, struct interval_node, node);
	struct comp_inst *ci = container_of(nx, struct comp_inst, node);
	struct todo_cis_tree_iter_env *env = _env;

	/* retrieve some properties */
	enum comp_type type = ci->c->type;
	enum prop_status status;
	bool has_status = props_get_status(ci->p, &status);
	enum prop_class class;
	props_get_class(ci->p, &class);

	if (type == COMP_TYPE_TODO
		&& has_status
		&& (status == PROP_STATUS_COMPLETED
			|| status == PROP_STATUS_CANCELLED)) return;

	struct active_comp ac = {
		.ci = ci,
		.cal_index = env->cal_index,
		.fade = false, .hide = false, .vis = true,
		.cal = env->cal,
		.node = ci->node /* this is just to copy the interval */
	};

	/* execute filter expressions */
	execute_filters(env->app, &ac);
	if (!ac.vis) return;

	vec_append(&env->app->active_todos, &ac);
}
static void app_update_active_todos(struct app *app) {
	vec_clear(&app->active_todos);
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		struct todo_cis_tree_iter_env env = {
			.app = app, .cal_index = i, .cal = cal };
		rb_iter(&cal->cis[COMP_TYPE_TODO], &env, todo_cis_tree_iter);
	}
}

void app_use_view(struct app *app, struct ts_ran view) {
	app_filter_active_events(app, view);
}

static void ac_iter_free(void *env, struct rb_node *x) {
	struct interval_node *nx = container_of(x, struct interval_node, node);
	struct active_comp *ac = container_of(nx, struct active_comp, node);
	free(ac->ci);
	free(ac);
}
static void app_clear_active_events(struct app *app) {
	struct stopwatch sw = sw_start();
	rb_iter_post(&app->active_events, NULL, ac_iter_free);
	rb_tree_init(&app->active_events, &interval_ops);
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		cal->cis_dirty[COMP_TYPE_EVENT] = true;
	}
	app_expand(app, COMP_TYPE_EVENT, app->expand_to);
	app_use_view(app, app->view);
	sw_end_print(sw, __func__);
}

static void app_reload_calendars(struct app *app) {
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		update_calendar_from_storage(cal, app->zone);
	}
	app_clear_active_events(app);
	app_update_active_todos(app);
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
}
void app_cmd_editor(struct app *app, FILE *in) {
	struct str in_s = str_empty;
	char c;
	while ((c = getc(in)) != EOF) str_append_char(&in_s, c);
	fclose(in);

	if (!str_any(&in_s)) return;

	FILE *f = fmemopen((void*)in_s.v.d, in_s.v.len, "r");
	asrt(f, "");

	struct edit_spec es;
	edit_spec_init(&es);
	int res = parse_edit_template(f, &es, app->zone);
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
	struct calendar *cal;
	if (es.calendar_num > 0) {
		if (es.calendar_num >= 1 && es.calendar_num <= app->cals.len) {
			cal = vec_get(&app->cals, es.calendar_num - 1);
		}
	}
	if (!cal) {
		fprintf(stderr, "[editor] calendar not specified.\n");
		app_launch_editor_str(app, &in_s); /* relaunch editor */
		goto cleanup;
	}

	/* apply edit */
	if (apply_edit_spec_to_calendar(&es, cal) == 0) {
		app_clear_active_events(app);
		app_update_active_todos(app);
		app->dirty = true;
	} else {
		fprintf(stderr, "[editor] error: could not save edit\n");
		app_launch_editor_str(app, &in_s); /* relaunch editor */
		goto cleanup;
	}

cleanup:
	edit_spec_finish(&es);
	str_free(&in_s);
}
void app_cmd_reload(struct app *app) {
	app_reload_calendars(app);
}
void app_cmd_activate_filter(struct app *app, int n) {
	const char **k = app->config_fns;
	for (int i = 0; i < n; ++i) {
		if (*k) ++k;
		else break;
	}
	if (app->current_filter_fn != *k) {
		app->current_filter_fn = *k;
		app_clear_active_events(app);
		app_update_active_todos(app);
		app->dirty = true;
	}
}
void app_cmd_view_today(struct app *app, int n) {
	app->view.fr = ts_get_day_base(app->now, app->zone, true);
	app->view.to = app->view.fr + 3600 * 24 * 7;
	app->dirty = true;
}
void app_cmd_toggle_show_private(struct app *app, int n) {
	// app->show_private_events = !app->show_private_events;
	// app_update_active_objects(app);
	// app->dirty = true;
}
void app_cmd_switch_view(struct app *app, int n) {
	if (n == -1) {
		app->main_view = (app->main_view + 1) % VIEW_N;
		app->dirty = true;
	} else if (0 <= n && n < VIEW_N) {
		app->main_view = n;
		app->dirty = true;
	}
}

static void application_handle_key(void *ud, uint32_t key, uint32_t mods) {
	struct app *app = ud;
	int n;
	// bool shift = mods & 1;
	char sym = key_get_sym(key);
	switch (app->keystate) {
	case KEYSTATE_SELECT:
		if (key_is_gen(key)) {
			app_mode_select_append_sym(app, key_get_sym(key));
		} else {
			app->keystate = KEYSTATE_BASE;
			app->dirty = true;
		}
		break;
	case KEYSTATE_BASE:
		switch (sym) {
		case 'a':
			app_cmd_switch_view(app, VIEW_CALENDAR);
			break;
		case 's':
			app_cmd_switch_view(app, VIEW_TODO);
			break;
		case 'l':
			app_cmd_move_view_discrete(app, 1);
			app->dirty = true;
			break;
		case 'h':
			app_cmd_move_view_discrete(app, -1);
			app->dirty = true;
			break;
		case 't':
			app_cmd_view_today(app, -1);
			break;
		case 'n':
			app_launch_new_editor(app);
			break;
		case 'e':
			app_switch_mode_select(app);
			app->dirty = true;
			break;
		case 'r':
			app_cmd_reload(app);
			break;
		case 'p':
			app_cmd_toggle_show_private(app, -1);
			break;
		case 'i':
			app->keystate = KEYSTATE_VIEW_SWITCH;
			break;
		case '\0':
			// if ((n = key_fn(key)) > 0) { /* numeric key */
			// 	--n; /* key 1->0 .. key 9->8 */
			// 	if (shift) n += 9;
			// 	if (n < app->cals.len) {
			// 		struct calendar_info *cal_info = vec_get(&app->cal_infos,n);
			// 		cal_info->visible = !cal_info->visible;
			// 		app_clear_active_events(app);
			// 		app->dirty = true;
			// 	}
			// }
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
			app->dirty = true;
		}
		app->keystate = KEYSTATE_BASE;
		break;
	}
	default:
		asrt(false, "bad keystate");
		break;
	}
}

static void application_handle_child(void *ud, pid_t pid) {
	struct app *app = ud;

	bool sp_expr = app->sp_expr; app->sp_expr = false;

	FILE *f = subprocess_get_result(&app->sp, pid);
	if (!f) return;

	if (sp_expr) {
		// editing expr
		if (app->expr) uexpr_destroy(app->expr);
		app->expr = uexpr_parse(f);
		return;
	}

	app_cmd_editor(app, f);
}

static void application_handle_input(void *ud, struct mgu_input_event_args ev) {
	struct app *app = ud;
	if (ev.t & MGU_TOUCH) {
		const double *p = ev.touch.down_or_move.p;
		if (ev.t & MGU_DOWN) {
			libtouch_surface_down(app->touch_surf, ev.time, ev.touch.id,
				(float[]){ p[0], p[1] });
			for (int i = 0; i < app->tap_areas.len; ++i) {
				struct tap_area *ta = vec_get(&app->tap_areas, i);
				if (p[0] >= ta->aabb[0] && p[0] < ta->aabb[0] + ta->aabb[2]
						&& p[1] >= ta->aabb[1] && p[1] < ta->aabb[1] + ta->aabb[3]) {
					ta->cmd(app, ta->n);
				}
			}
		} else if (ev.t & MGU_MOVE) {
			libtouch_surface_motion(app->touch_surf, ev.time, ev.touch.id,
				(float[]){ p[0], p[1] });
		} else if (ev.t & MGU_UP) {
			libtouch_surface_up(app->touch_surf, ev.time, ev.touch.id);
		}
		app->dirty = true;
	}
}

static const char * get_comp_color(void *ptr) {
	/* we only get the color from the base instance of the recur set */
	struct comp *c = ptr;
	return props_get_color(&c->p);
}

static void touch_end(void *env, struct libtouch_rt rt) {
	struct app *app = env;

	// TODO: code duplication
	float g = libtouch_rt_scaling(&rt);
	double a = app->view.fr, b = app->view.to;
	b = a + (b - a) / g;
	double tx = (rt.t1 / app->window_width) * (b - a);
	a -= tx, b -= tx;
	struct ts_ran view = { a, b };
	app->view = view;
}

void app_init(struct app *app, struct application_options opts,
		struct backend *backend) {
	struct stopwatch sw = sw_start();

	*app = (struct app){
		.cals = VEC_EMPTY(sizeof(struct calendar)),
		.cal_infos = VEC_EMPTY(sizeof(struct calendar_info)),
		.active_todos = VEC_EMPTY(sizeof(struct active_comp)),
		.main_view = VIEW_CALENDAR,
		.keystate = KEYSTATE_BASE,
		.show_private_events = opts.show_private_events,
		.tap_areas = VEC_EMPTY(sizeof(struct tap_area)),
		.sp = NULL,
		.sp_expr = false,
		.window_width = -1, .window_height = -1,
		.backend = backend,
		.interactive = backend->vptr->is_interactive(backend),
		.editor_args = VEC_EMPTY(sizeof(struct str)),
		.expr = NULL,
		.builtin_expr = NULL,
		.config_expr = NULL,
		.config_ctx = NULL,
		.config_fns = NULL,
		.current_filter_fn = NULL,
	};

	rb_tree_init(&app->active_events, &interval_ops);

	/* load all uexpr stuff */
	const char *builtin_expr = "{"
		"def(filter_0, {"
			"($st % [ tentative, cancelled ]) & let($fade, a=a);"
			"let($hide, ($clas = private) & ~$show_priv)"
		"})"
		"}";
	FILE *f = fmemopen((void*)builtin_expr, strlen(builtin_expr), "r");
	app->builtin_expr = uexpr_parse(f);
	fclose(f);
	asrt(app->builtin_expr, "builtin_expr parsing failed");
	app->builtin_expr_ctx = uexpr_ctx_create(app->builtin_expr);
	uexpr_eval(app->builtin_expr_ctx);

	if (opts.config_file) {
		f = fopen(opts.config_file, "r");
		app->config_expr = uexpr_parse(f);
		fclose(f);
		if (app->config_expr) {
			app->config_ctx = uexpr_ctx_create(app->config_expr);
			uexpr_eval(app->config_ctx);
			app->config_fns = uexpr_get_all_fns(app->config_ctx);
			const char **k = app->config_fns;
			while (*k) {
				fprintf(stderr, "fn: %s\n", *k);
				k++;
			}
		} else {
			fprintf(stderr, "WARNING: could not parse config script!\n");
		}
	}

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

	app->zone = cal_timezone_new("Europe/Budapest");
	app->now = ts_now();
	app->view.fr = ts_get_day_base(app->now, app->zone, true);
	app->view.to = app->view.fr + 3600 * 24 * 7;

	for (int i = 0; i < opts.argc; i++) {
		struct calendar cal;
		struct calendar_info cal_info;

		/* init */
		calendar_init(&cal);

		/* read */
		cal.storage = str_empty;
		str_append(&cal.storage, opts.argv[i], strlen(opts.argv[i]));
		fprintf(stderr, "loading %s\n", str_cstr(&cal.storage));
		update_calendar_from_storage(&cal, app->zone);

		/* set metadata */
		// TODO: set calendar name from storage name
		// if (!cal->name) cal->name = str_dup(cal->storage);
		// trim_end(cal->name);

		/* calculate most frequent color */
		const char *fc = most_frequent(&cal.comps_vec, &get_comp_color);
		uint32_t color = fc ? lookup_color(fc, strlen(fc)) : 0;
		if (!color) color = 0xFF20D0D0;
		cal_info.color = color;

		vec_append(&app->cals, &cal);
		vec_append(&app->cal_infos, &cal_info);
	}

	app->expand_to = app->now + 3600 * 24 * 365;
	app_expand(app, COMP_TYPE_EVENT, app->expand_to);
	app_expand(app, COMP_TYPE_TODO, app->expand_to);

	app_update_active_todos(app);

	app->touch_surf = libtouch_surface_create();
	memcpy(app->touch_aabb, &(float[]) { 120, 0, 5000, 5000 },
		sizeof(float) * 4);
	app->touch_area = libtouch_surface_add_area(
		app->touch_surf,
		app->touch_aabb,
		LIBTOUCH_TSR,
		(struct libtouch_area_ops){ .env = app, .end = touch_end }
	);

	app->tr = text_renderer_new("Monospace 8");

	app->slicing = slicing_create(app->zone);

	app->backend->vptr->set_callbacks(app->backend,
		&render_application,
		&application_handle_key,
		&application_handle_child,
		&application_handle_input,
		app
	);

	app->dirty = true;
	sw_end_print(sw, "initialization");
}

void app_main(struct app *app) {
	app->backend->vptr->run(app->backend);
}

void app_finish(struct app *app) {
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		calendar_finish(cal);
	}
	vec_free(&app->cals);
	vec_free(&app->cal_infos);

	for (int i = 0; i < app->editor_args.len; ++i) {
		struct str *s = vec_get(&app->editor_args, i);
		str_free(s);
	}
	vec_free(&app->editor_args);

	// tslice_finish(&app->slice_main);
	// tslice_finish(&app->slice_top);

	// vec_free(&app->active_events); // TODO
	vec_free(&app->active_todos);

	cal_timezone_destroy(app->zone);
	text_renderer_free(app->tr);

	libtouch_surface_destroy(app->touch_surf);

	if (app->config_ctx) uexpr_ctx_destroy(app->config_ctx);
	if (app->builtin_expr_ctx) uexpr_ctx_destroy(app->builtin_expr_ctx);
	if (app->builtin_expr) uexpr_destroy(app->builtin_expr);
	if (app->expr) uexpr_destroy(app->expr);
	free(app->config_fns);

	slicing_destroy(app->slicing);
	vec_free(&app->tap_areas);

	app->backend->vptr->destroy(app->backend);
}
