
#include <string.h>

#include "calendar.h"
#include "core.h"
#include "util.h"
#include "uexpr.h"
#include "application.h"

static const struct uexpr_value error_val = { .type = UEXPR_TYPE_ERROR };
static const struct uexpr_value void_val = { .type = UEXPR_TYPE_VOID };

struct fn { const char *key; uexpr_nativefn fn; };

enum obj_type {
	OBJ_TYPE_CAL_REF,
};
struct obj {
	enum obj_type type;
	int ref;
};
struct obj_cal_ref {
	struct obj obj;
	int cal_idx;
};
void obj_ref(void *self, int ref) {
	struct obj *obj = self;
	asrt(obj->ref > 0, "");
	obj->ref += ref;
	asrt(obj->ref >= 0, "");
	if (obj->ref == 0) {
		if (obj->type == OBJ_TYPE_CAL_REF) {
			struct obj_cal_ref *cal_ref =
				container_of(obj, struct obj_cal_ref, obj);
			free(cal_ref);
		}
	}
}
struct uexpr_value obj_init(struct obj *obj) {
	obj->ref += 1;
	return (struct uexpr_value){
		.type = UEXPR_TYPE_NATIVEOBJ,
		.nativeobj = { .self = obj, .ref = obj_ref }
	};
}
struct uexpr_value obj_cal_ref_create(int cal_idx) {
	struct obj_cal_ref *obj = malloc_check(sizeof(struct obj_cal_ref));
	*obj = (struct obj_cal_ref){
		.obj = { .type = OBJ_TYPE_CAL_REF },
		.cal_idx = cal_idx,
	};
	return obj_init(&obj->obj);
}

static enum app_view parse_enum_view(const char *str) {
	if (strcmp(str, "cal") == 0) return VIEW_CALENDAR;
	if (strcmp(str, "todo") == 0) return VIEW_TODO;
	return VIEW_N;
}
static enum comp_type parse_enum_comp_type(const char *str) {
	if (strcmp(str, "event") == 0) return COMP_TYPE_EVENT;
	if (strcmp(str, "todo") == 0) return COMP_TYPE_TODO;
	return COMP_TYPE_N;
}

#if 0
#define TRACE() pu_log_info("[cal_uexpr] %s\n", __func__)
#else
#define TRACE()
#endif

static struct uexpr_value fn_include(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 1) return error_val;

	struct uexpr_value va;
	uexpr_eval(e, *(int*)vec_get(&np.args, 0), ctx, &va);
	if (va.type != UEXPR_TYPE_STRING) {
		uexpr_value_finish(va);
		return error_val;
	}

	struct str expanded = str_wordexp(va.string_ref);
	app_add_uexpr_config(env->app, str_cstr(&expanded));
	str_free(&expanded);

	return void_val;
}
static struct uexpr_value fn_add_cal(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 2) return error_val;

	struct uexpr_ast_node na = *(struct uexpr_ast_node *)
		vec_get(&e->ast, *(int *)vec_get(&np.args, 0));
	if (na.op != UEXPR_OP_VAR) return error_val;

	struct uexpr_value vb;
	uexpr_eval(e, *(int*)vec_get(&np.args, 1), ctx, &vb);
	if (vb.type != UEXPR_TYPE_STRING) {
		uexpr_value_finish(vb);
		return error_val;
	}

	int cal_idx = app_add_cal(env->app, vb.string_ref);
	struct uexpr_value res = obj_cal_ref_create(cal_idx);
	((struct calendar_info *)vec_get(&env->app->cal_infos, cal_idx))
		->uexpr_tag = uexpr_value_copy(&res);

	uexpr_set_var(ctx, str_cstr(&na.str), res);

	return void_val;
}
static struct uexpr_value fn_add_filter(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 3) return error_val;

	struct uexpr_value va;
	uexpr_eval(e, *(int *)vec_get(&np.args, 0), ctx, &va);
	if (va.type != UEXPR_TYPE_STRING) {
		uexpr_value_finish(va);
		return error_val;
	}

	struct uexpr_value vb;
	uexpr_eval(e, *(int *)vec_get(&np.args, 1), ctx, &vb);
	if (vb.type != UEXPR_TYPE_NATIVEOBJ
			|| vb.nativeobj.ref != obj_ref) {
		uexpr_value_finish(vb);
		return error_val;
	}
	struct obj_cal_ref *obj_cal =
		container_of(vb.nativeobj.self, struct obj_cal_ref, obj);

	int root_c = *(int*)vec_get(&np.args, 2);

	app_add_uexpr_filter(env->app, va.string_ref, obj_cal->cal_idx, root_c);

	return void_val;
}
static struct uexpr_value fn_add_action(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len < 2 || np.args.len > 3) return error_val;

	struct action act = { .label = str_new_empty(), .cond.view = VIEW_N };

	struct uexpr_value va;
	uexpr_eval(e, *(int*)vec_get(&np.args, 0), ctx, &va);
	if (va.type == UEXPR_TYPE_STRING && va.string_ref[0]) {
		act.key_sym = va.string_ref[0];
	} else if (va.type == UEXPR_TYPE_LIST) {
		struct uexpr_value *v;
		if (va.list.len >= 1) {
			v = vec_get(&va.list, 0);
			if (v->type == UEXPR_TYPE_STRING && v->string_ref[0]) {
				act.key_sym = v->string_ref[0];
			}
		}
		if (va.list.len >= 2) {
			v = vec_get(&va.list, 1);
			if (v->type == UEXPR_TYPE_STRING && v->string_ref[0]) {
				act.label = str_new_from_cstr(v->string_ref);
			}
		}
		uexpr_value_finish(va);
	} else {
		uexpr_value_finish(va);
		return error_val;
	}

	int root_b = *(int*)vec_get(&np.args, 1);
	act.uexpr_fn = root_b;

	if (np.args.len >= 3) {
		struct uexpr_value vc;
		uexpr_eval(e, *(int*)vec_get(&np.args, 2), ctx, &vc);
		if (vc.type == UEXPR_TYPE_STRING) {
			act.cond.view = parse_enum_view(vc.string_ref);
		}
		uexpr_value_finish(vc);
	}

	app_add_action(env->app, act);

	return void_val;
}
static struct uexpr_value fn_set_alarm(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 2) return error_val;

	struct uexpr_value va;
	uexpr_eval(e, *(int *)vec_get(&np.args, 0), ctx, &va);
	if (va.type != UEXPR_TYPE_STRING) {
		uexpr_value_finish(va);
		return error_val;
	}

	int root_b = *(int*)vec_get(&np.args, 1);

	env->app->alarm_comps.shell_cmd = va.string_ref;
	env->app->alarm_comps.uexpr_filter = root_b;

	return void_val;
}
static struct uexpr_value fn_set_timezone(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 1) return error_val;

	struct uexpr_value va;
	uexpr_eval(e, *(int *)vec_get(&np.args, 0), ctx, &va);
	if (va.type != UEXPR_TYPE_STRING) {
		uexpr_value_finish(va);
		return error_val;
	}

	env->app->requested_timezone = va.string_ref;

	return void_val;
}
static struct uexpr_value fn_set_colors(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 6) return error_val;

	struct color_scheme_configurable col_c;
	for (int i = 0; i < np.args.len; ++i) {
		struct uexpr_value va;
		uexpr_eval(e, *(int *)vec_get(&np.args, i), ctx, &va);
		if (va.type != UEXPR_TYPE_STRING) {
			uexpr_value_finish(va);
			return error_val;
		}

		uint32_t c = lookup_color(va.string_ref, strlen(va.string_ref));
		if (!c) c = hex2uint(va.string_ref);
		if (c) col_c.c[i] = c;
	}

	app_cmd_set_color_scheme(env->app, col_c);

	return void_val;
}

static struct fn config_fns[] = {
	{ "add_cal", fn_add_cal },
	{ "add_filter", fn_add_filter },
	{ "add_action", fn_add_action },
	{ "include", fn_include },
	{ "set_alarm", fn_set_alarm },
	{ "set_timezone", fn_set_timezone },
	{ NULL, NULL },
};

static struct fn action_and_config_fns[] = {
	{ "set_colors", fn_set_colors },
	{ NULL, NULL },
};

static const char * get_single_arg_str(struct uexpr *e, int root,
		struct uexpr_ctx *ctx) {
	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 1) return NULL;

	struct uexpr_value va;
	uexpr_eval(e, *(int *)vec_get(&np.args, 0), ctx, &va);
	if (va.type != UEXPR_TYPE_STRING) {
		uexpr_value_finish(va);
		return NULL;
	}

	return va.string_ref;
}

static struct uexpr_value fn_switch_view(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	const char *str = get_single_arg_str(e, root, ctx);
	if (!str) return error_val;

	app_cmd_switch_view(env->app, parse_enum_view(str));

	return void_val;
}
static struct uexpr_value fn_move_view_discrete(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	const char *str = get_single_arg_str(e, root, ctx);
	if (!str) return error_val;

	int n = atoi(str);
	app_cmd_move_view_discrete(env->app, n);

	return void_val;
}
static struct uexpr_value fn_view_today(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 0) return error_val;

	app_cmd_view_today(env->app, -1);

	return void_val;
}
static struct uexpr_value fn_launch_editor(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len > 2) return error_val;

	if (np.args.len == 0) {
		if (env->kind & CAL_UEXPR_FILTER) {
			app_cmd_launch_editor(env->app, env->pi);
		}
	} else if (np.args.len == 1) {
		struct uexpr_value va;
		uexpr_eval(e, *(int *)vec_get(&np.args, 0), ctx, &va);
		if (va.type != UEXPR_TYPE_STRING) {
			uexpr_value_finish(va);
			return error_val;
		}

		app_cmd_launch_editor_new(env->app,
			parse_enum_comp_type(va.string_ref));
	} else {
		return error_val;
	}

	return void_val;
}
static struct uexpr_value fn_select_comp(void *_env, struct uexpr *e,
		int root, struct uexpr_ctx *ctx) {
	TRACE();
	struct cal_uexpr_env *env = _env;

	struct uexpr_ast_node np =
		*(struct uexpr_ast_node *)vec_get(&e->ast, root);
	if (np.args.len != 3) return error_val;

	struct uexpr_value va;
	uexpr_eval(e, *(int *)vec_get(&np.args, 0), ctx, &va);
	if (va.type != UEXPR_TYPE_STRING) {
		uexpr_value_finish(va);
		return error_val;
	}

	struct uexpr_value vb;
	uexpr_eval(e, *(int *)vec_get(&np.args, 1), ctx, &vb);
	if (vb.type != UEXPR_TYPE_STRING) {
		uexpr_value_finish(vb);
		return error_val;
	}

	int root_c = *(int*)vec_get(&np.args, 2);

	// TODO: use these
	// enum comp_type type = parse_enum_comp_type(va.string_ref);
	// const char *msg = vb.string_ref;

	app_cmd_select_comp_uexpr(env->app, root_c);

	return void_val;
}

static struct fn action_fns[] = {
	{ "switch_view", fn_switch_view },
	{ "move_view_discrete", fn_move_view_discrete },
	{ "view_today", fn_view_today },
	{ "launch_editor", fn_launch_editor },
	{ "select_comp", fn_select_comp },
	{ NULL, NULL },
};

static bool get_fns(struct cal_uexpr_env *env, struct fn *fns,
		const char *key, struct uexpr_value *v) {
	while (fns->key) {
		if (strcmp(fns->key, key) == 0) {
			*v = (struct uexpr_value) {
				.type = UEXPR_TYPE_NATIVEFN,
				.nativefn = { .f = fns->fn, .env = env }
			};
			return true;
		}
		++fns;
	}
	return false;
}

static bool get_ac(struct cal_uexpr_env *env, const char *key,
		struct uexpr_value *v) {
	struct proj_item *pi = env->pi;
	struct comp_inst *ci = pi->ci;
	if (strcmp(key, "ev") == 0)
		return *v = UEXPR_BOOLEAN(ci->c->type == COMP_TYPE_EVENT),
			true;
	if (strcmp(key, "sum") == 0)
		return *v = UEXPR_STRING(props_get_summary(ci->p)), true;
	if (strcmp(key, "color") == 0)
		return *v = UEXPR_STRING(props_get_color(ci->p)), true;
	if (strcmp(key, "loc") == 0)
		return *v = UEXPR_STRING(props_get_location(ci->p)), true;
	if (strcmp(key, "desc") == 0)
		return *v = UEXPR_STRING(props_get_desc(ci->p)), true;
	if (strcmp(key, "st") == 0) {
		enum prop_status status;
		bool has_status = props_get_status(ci->p, &status);
		*v = UEXPR_STRING(has_status ? cal_status_str(status) : "");
		return true;
	}
	if (strcmp(key, "clas") == 0) {
		enum prop_class class;
		bool has_class = props_get_class(ci->p, &class);
		*v = UEXPR_STRING(has_class ? cal_class_str(class) : "");
		return true;
	}
	if (strcmp(key, "cats") == 0) {
		*v = (struct uexpr_value){
			.type = UEXPR_TYPE_LIST,
			.list = vec_new_empty(sizeof(struct uexpr_value))
		};
		const struct vec *cats = props_get_categories(ci->p);
		for (int i = 0; i < cats->len; ++i) {
			const struct str *s = vec_get_c(cats, i);
			struct uexpr_value vi = UEXPR_STRING(str_cstr(s));
			vec_append(&v->list, &vi);
		}
		return true;
	}

	if (strcmp(key, "cal") == 0) {
		*v = uexpr_value_copy(&((struct calendar_info *)vec_get(
			&env->app->cal_infos, pi->cal_index))->uexpr_tag);
		return true;
	}

	if (strcmp(key, "vis") == 0)
		return *v = UEXPR_BOOLEAN(env->settings->vis), true;
	if (strcmp(key, "hide") == 0)
		return *v = UEXPR_BOOLEAN(env->settings->hide), true;
	if (strcmp(key, "fade") == 0)
		return *v = UEXPR_BOOLEAN(env->settings->fade), true;

	if (strcmp(key, "last_mod_today") == 0) {
		ts last_modified;
		bool has = props_get_last_modified(ci->p, &last_modified);
		bool in_today = false;
		if (has) {
			ts now = env->app->now;
			struct ts_ran today = slicing_get_bounds(
				env->app->slicing,
				SLICING_DAY, (struct ts_ran){ now, now });
			in_today = ts_ran_in(today, last_modified);
		}
		*v = UEXPR_BOOLEAN(in_today);
		return true;
	}

	return false;
}
bool cal_uexpr_get(void *_env, const char *key, struct uexpr_value *v) {
	struct cal_uexpr_env *env = _env;
	if (env->kind & CAL_UEXPR_CONFIG) {
		if (get_fns(env, config_fns, key, v)) return true;
		if (get_fns(env, action_and_config_fns, key, v)) return true;
	}
	if (env->kind & CAL_UEXPR_FILTER) {
		if (get_ac(env, key, v)) return true;
	}
	if (env->kind & CAL_UEXPR_ACTION) {
		if (get_fns(env, action_fns, key, v)) return true;
		if (get_fns(env, action_and_config_fns, key, v)) return true;
	}
	return false;
}

bool cal_uexpr_set(void *_env, const char *key, struct uexpr_value v) {
	struct cal_uexpr_env *env = _env;

	if (env->kind & CAL_UEXPR_FILTER) {
		if (v.type == UEXPR_TYPE_BOOLEAN) {
			bool b = v.boolean;
			if (strcmp(key, "fade") == 0)
				env->settings->fade = b;
			else if (strcmp(key, "hide") == 0)
				env->settings->hide = b;
			else if (strcmp(key, "vis") == 0)
				env->settings->vis = b;
			else
				return false;

			uexpr_value_finish(v);
			return true;
		} else if (v.type == UEXPR_TYPE_STRING) {
			const char *s = v.string_ref;
			if (strcmp(key, "st") == 0) {
				enum prop_status status;
				if (cal_parse_status(s, &status)) {
					props_set_status(&env->set_props,
						status);
					env->set_edit = true;
				}
			} else {
				return false;
			}

			uexpr_value_finish(v);
			return true;
		}
	}
	return false;
}
