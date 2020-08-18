#include <math.h>

#include "render.h"
#include "pango.h"
#include "application.h"
#include "core.h"
#include "util.h"

static const char *usage =
	"Usage:\r"
	" a: switch to calendar view\r"
	" s: switch to todo view\r"
	" h/l: prev/next\r"
	" t: today\r"
	" n: new event/todo\r"
	" e: edit event/todo\r"
	" c: reset visibility\r"
	" r: reload calendars\r"
	" p: toggle private view\r"
	" i{h,j,k,l}: select view mode\r"
	" [1-9]: toggle calendar visibility\r";

typedef struct {
	double x, y, w, h;
} fbox;

/* dir: false is vertical */
static fbox fbox_slice(fbox f, bool dir,
		struct ts_ran view, struct ts_ran ran) {
	double len = view.to - view.fr;
	double a = (ran.fr - view.fr) / len, b = (ran.to - view.fr) / len;
	if (dir) {
		return (fbox){
			.x = f.x + f.w * a,
			.y = f.y,
			.w = (b - a) * f.w,
			.h = f.h
		};
	} else {
		return (fbox){
			.x = f.x,
			.y = f.y + f.h * a,
			.w = f.w,
			.h = (b - a) * f.h
		};
	}
}

static bool same_day(struct simple_date a, struct simple_date b) {
	return
		a.year == b.year &&
		a.month == b.month &&
		a.day == b.day;
}

static char* natural_date_format(struct app *app, ts d) {
	ts ts_now = app->now;
	struct simple_date now = simple_date_from_ts(ts_now, app->zone);
	ts_adjust_days(&ts_now, app->zone, -1);
	struct simple_date yesterday = simple_date_from_ts(ts_now, app->zone);
	struct simple_date t = simple_date_from_ts(d, app->zone);
	if (same_day(t, now)) {
		return text_format("%02d:%02d", t.hour, t.minute);
	} else if (same_day(t, yesterday)) {
		return text_format("yesterday %02d:%02d", t.hour, t.minute);
	} else if (t.year == now.year) {
		return text_format("%02d-%02d %02d:%02d",
			t.month, t.day, t.hour, t.minute);
	} else {
		return text_format("%04d-%02d-%02d %02d:%02d",
			t.year, t.month, t.day, t.hour, t.minute);
	}
}
static char * format_dur(int v) {
	// ugly af...
	char *s = malloc_check(64);
	s[0] = '\0';
	int n = 0;
	struct simple_dur sdu = simple_dur_from_int(v);
	if (sdu.d != 0) n += snprintf(s + n, 64 - n, "%dd", sdu.d);
	if (sdu.h != 0) n += snprintf(s + n, 64 - n, "%dh", sdu.h);
	if (sdu.m != 0) n += snprintf(s + n, 64 - n, "%dm", sdu.m);
	if (sdu.s != 0) n += snprintf(s + n, 64 - n, "%ds", sdu.s);
	if (sdu.d == 0 && sdu.h == 0 && sdu.m == 0 && sdu.s == 0)
		n -= snprintf(s + n, n, "0s");
	return s;
}

static void draw_text(cairo_t *cr, struct text_renderer *tr,
		int x, int y, const char *text) {
	tr->p.width = -1; tr->p.height = -1;
	text_get_size(cr, tr, text);
	cairo_move_to(cr, x - tr->p.width / 2, y - tr->p.height / 2);
	text_print_own(cr, tr, text);
}

static void text_print_vert_center(cairo_t *cr, struct text_renderer *tr,
		box b, const char *text) {
	tr->p.width = b.w; tr->p.height = b.h;
	tr->p.wrap_char = false;
	text_get_size(cr, tr, text);
	cairo_move_to(cr, b.x, b.y + b.h / 2 - tr->p.height / 2);
	text_print_own(cr, tr, text);
	tr->p.wrap_char = true;
}

static void text_print_center(cairo_t *cr, struct text_renderer *tr,
		box b, const char *text) {
	tr->p.width = b.w; tr->p.height = b.h;
	tr->p.wrap_char = false;
	text_get_size(cr, tr, text);
	cairo_move_to(cr, b.x + b.w / 2 - tr->p.width / 2,
		b.y + b.h / 2 - tr->p.height / 2);
	text_print_own(cr, tr, text);
	tr->p.wrap_char = true;
}

static void cairo_set_source_argb(cairo_t *cr, uint32_t c){
	cairo_set_source_rgba(cr,
			((c >> 16) & 0xFF) / 255.0,
			((c >> 8) & 0xFF) / 255.0,
			(c & 0xFF) / 255.0,
			((c >> 24) & 0xFF) / 255.0);
}

struct tview_params {
	bool dir;
	double pad;
	double sep_line;
	/* skip view_ran.fr from the start of each slice, and end at view_ran.to */
	struct ts_ran view_ran;
};
static void render_tobject(cairo_t *cr, struct app *app,
		struct tobject *obj, fbox b, struct tview_params p) {
	double x, y, w, h;
	if (p.dir) {
		x = round(b.x + p.pad);
		y = round(b.y);
		w = round(b.w - 2 * p.pad);
		h = round(b.h);
	} else {
		x = round(b.x);
		y = round(b.y + p.pad);
		w = round(b.w);
		h = round(b.h - 2 * p.pad);
	}
	bool draw_labels = w > 8 && h > 8;

	/* calculate color stuff */
	uint32_t color;
	if (obj->type == TOBJECT_EVENT) {
		color = props_get_color_val(obj->ac->ci->p);
		if (!color) color = 0xFF20D0D0;
		if (obj->ac->fade) {
			color = (color & 0x00FFFFFF) | 0x30000000;
		}
	} else if (obj->type == TOBJECT_TODO) {
		color = 0xAA00AA00;
	} else asrt(false, "");
	double lightness = (color & 0xFF) + ((color >> 8) & 0xFF)
		+ ((color >> 16) & 0xFF);
	lightness /= 255.0;
	bool light = lightness < 0.9 ? true : false;
	uint32_t fg = light ? 0xFFFFFFFF : 0xFF000000;

	/* fill base rect */
	cairo_set_source_argb(cr, color);
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);

	/* draw various labels */
	if (draw_labels && obj->type == TOBJECT_EVENT && !obj->ac->hide) {
		cairo_set_source_argb(cr, fg);

		const char *location = props_get_location(obj->ac->ci->p);
		if (location) {
			app->tr->p.width = w; app->tr->p.height = -1;
			text_get_size(cr, app->tr, location);
		}
		int loc_h = location ? mini(h / 2, app->tr->p.height) : 0;

		const char *summary = props_get_summary(obj->ac->ci->p);
		cairo_move_to(cr, x, y);
		app->tr->p.width = w; app->tr->p.height = h - loc_h;
		struct simple_date local_start =
			simple_date_from_ts(obj->ac->ci->time.fr, app->zone);
		struct simple_date local_end =
			simple_date_from_ts(obj->ac->ci->time.to, app->zone);
		char *text = text_format("%02d:%02d-%02d:%02d %s",
				local_start.hour, local_start.minute,
				local_end.hour, local_end.minute,
				summary);
		text_print_free(cr, app->tr, text);

		if (location) {
			cairo_set_source_argb(cr, light ? 0xFFA0A0A0 : 0xFF606060);
			cairo_move_to(cr, x, y+h-loc_h);

			app->tr->p.width = w; app->tr->p.height = loc_h;
			text_print_own(cr, app->tr, location);
		}
	}
	if (draw_labels && obj->type == TOBJECT_TODO) {
		cairo_set_source_argb(cr, fg);
		cairo_move_to(cr, x, y);
		app->tr->p.width = w; app->tr->p.height = h;
		char *text = text_format("TODO: %s", props_get_summary(obj->ac->ci->p));
		text_print_free(cr, app->tr, text);
	}

	/* draw keycode tags */
	if (obj->type == TOBJECT_EVENT && app->keystate == KEYSTATE_SELECT) {
		uint32_t c = (color ^ 0x00FFFFFF) | 0xFF000000;
		cairo_set_source_argb(cr, c);
		char *text = obj->ac->code;
		app->tr->p.scale = 3.0;
		app->tr->p.width = w; app->tr->p.height = -1;
		text_get_size(cr, app->tr, text);
		cairo_move_to(cr, x + w/2 - app->tr->p.width/2,
			y + h/2 - app->tr->p.height/2);
		text_print_own(cr, app->tr, obj->ac->code);
		app->tr->p.scale = 1.0;
	}
}
struct ctx {
	cairo_t *cr;
	struct app *app;
	struct slicing *s;
	enum slicing_type st;
	int level;
	bool dir;
	fbox btop, bmain, bhead;
	struct tview_params p;
	struct ts_ran view;
	struct ts_ran len_clip;
	struct vec *tobjs;
	bool now_shading;
};
static void iter_ac(void *env, struct interval_node *x) {
	struct ctx *ctx = env;
	struct active_comp *ac = container_of(x, struct active_comp, node);
	struct tobject obj = {
		.time = ac->ci->time,
		.type = TOBJECT_EVENT,
		.ac = ac,
	};

	ts len = obj.time.to - obj.time.fr;
	if (ctx->len_clip.fr != -1 && ctx->len_clip.fr > len) return;
	if (ctx->len_clip.to != -1 && ctx->len_clip.to <= len) return;

	vec_append(ctx->tobjs, &obj);
}
static void render_ran(void *env, struct ts_ran ran, struct simple_date label) {
	struct ctx *ctx = env;
	fbox btop = ctx->btop, bmain = ctx->bmain, bhead = ctx->bhead;
	fbox bsl = fbox_slice(bmain, ctx->dir, ctx->view, ran);
	fbox bhsl = fbox_slice(bhead, ctx->dir, ctx->view, ran);

	if (ctx->now_shading && ts_ran_in(ran, ctx->app->now)) {
		cairo_reset_clip(ctx->cr);
		cairo_rectangle(ctx->cr, btop.x, btop.y, btop.w, btop.h);
		cairo_clip(ctx->cr);

		uint32_t l = (64 + ctx->level * 128) & 0xFF;
		uint32_t bg = 0xFFFF0000 | (l << 8) | l;
		cairo_set_source_argb(ctx->cr, bg);
		cairo_rectangle(ctx->cr, bsl.x, bsl.y, bsl.w, bsl.h);
		if (ctx->level == 1) cairo_rectangle(ctx->cr, bhsl.x, bhsl.y, bhsl.w, bhsl.h);
		cairo_fill(ctx->cr);
	}

	if (ctx->level > 0) {
		/* recurse */
		struct ctx next_ctx = *ctx;
		--next_ctx.level;
		next_ctx.bmain = bsl;
		next_ctx.view = ran;
		next_ctx.dir = !next_ctx.dir;
		++next_ctx.st;
		if (next_ctx.st <= SLICING_HOUR) {
			slicing_iter_items(ctx->s, &next_ctx, render_ran, next_ctx.st, ran);
		}
	}

	cairo_reset_clip(ctx->cr);
	cairo_rectangle(ctx->cr, btop.x, btop.y, btop.w, btop.h);
	cairo_clip(ctx->cr);

	if ((ctx->level == 0 && ctx->st < SLICING_HOUR) || ctx->st == SLICING_DAY) {
		/* draw overlapping objects */
		vec_clear(ctx->tobjs);
		interval_query(
			&ctx->app->active_events,
			(long long int[]){ ran.fr, ran.to },
			ctx,
			iter_ac
		);
		tobject_layout(ctx->tobjs, NULL);
		double len = ran.to - ran.fr;
		for (int i = 0; i < ctx->tobjs->len; ++i) {
			struct tobject *obj = vec_get(ctx->tobjs, i);;
			struct ts_ran time = obj->time;

			time.fr = max_ts(time.fr, ran.fr);
			time.to = min_ts(time.to, ran.to);

			double pa = (time.fr - ran.fr) / len;
			double pb = (time.to - ran.fr) / len;
			double pl = pb - pa;

			fbox nb;
			if (ctx->dir) {
				nb.x = bsl.x + bsl.w / obj->max_n * obj->col;
				nb.y = bsl.y + bsl.h * pa;
				nb.w = bsl.w / obj->max_n;
				nb.h = bsl.h * pl;
			} else {
				nb.x = bsl.x + bsl.w * pa;
				nb.y = bsl.y + bsl.h / obj->max_n * obj->col;
				nb.w = bsl.w * pl;
				nb.h = bsl.h / obj->max_n;
			}
			render_tobject(ctx->cr, ctx->app, obj, nb, ctx->p);
		}

		/* draw time marker red line */
		ts now = ctx->app->now;
		if (ts_ran_in(ran, now)) {
			double pa = (now - ran.fr) / len;
			fbox nb;
			if (ctx->dir) {
				nb.x = bsl.x;
				nb.y = bsl.y + bsl.h * pa;
				nb.w = bsl.w;
				nb.h = 0;
			} else {
				nb.x = bsl.x + bsl.w * pa;
				nb.y = bsl.y;
				nb.w = 0;
				nb.h = bsl.h;
			}
			cairo_set_line_width(ctx->cr, 2);
			cairo_move_to(ctx->cr, nb.x, nb.y);
			cairo_line_to(ctx->cr, nb.x + nb.w, nb.y + nb.h);
			cairo_set_source_rgba(ctx->cr, 255, 0, 0, 255);
			cairo_stroke(ctx->cr);
		}
	}

	/* draw lines between slices */
	cairo_set_source_argb(ctx->cr, 0xFF000000);
	cairo_set_line_width(ctx->cr, ctx->p.sep_line);
	if (ctx->dir) {
		cairo_move_to(ctx->cr, bsl.x, bsl.y);
		cairo_line_to(ctx->cr, bsl.x, bsl.y + bsl.h);
	} else {
		cairo_move_to(ctx->cr, bsl.x, bsl.y);
		cairo_line_to(ctx->cr, bsl.x + bsl.w, bsl.y);
	}
	cairo_stroke(ctx->cr);

	if (ctx->level == 1) {
		cairo_reset_clip(ctx->cr);
		cairo_rectangle(ctx->cr, bhead.x, bhead.y, bhead.w, bhead.h);
		cairo_clip(ctx->cr);

		/* draw header */
		cairo_set_source_argb(ctx->cr, 0xFF000000);
		cairo_set_line_width(ctx->cr, ctx->p.sep_line);
		cairo_move_to(ctx->cr, bhsl.x, bhsl.y);
		cairo_line_to(ctx->cr, bhsl.x, bhsl.y + bhsl.h);
		cairo_stroke(ctx->cr);

		ctx->app->tr->p.scale = 1.5;
		char *text;
		if (label.t[1] == 0) text = text_format("%d", label.t[0]);
		else if (label.t[2] == 0) text = text_format("%d-%d", label.t[0], label.t[1]);
		else text = text_format("%d-%d-%d", label.t[0], label.t[1], label.t[2]);
		text_print_center(ctx->cr, ctx->app->tr, (box){ bhsl.x, bhsl.y, bhsl.w, bhsl.h }, text);
		free(text);
		ctx->app->tr->p.scale = 1.0;

		cairo_reset_clip(ctx->cr);
	}
}

static void render_sidebar(cairo_t *cr, struct app *app, box b) {
	cairo_translate(cr, b.x, b.y);
	cairo_set_source_rgba(cr, 0, 0, 0, 255);
	int h = 0;
	int pad = 6;
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		struct calendar_info *cal_info = vec_get(&app->cal_infos, i);
		const char *name = str_cstr(&cal->name);
		if (!app->show_private_events && cal->priv) continue;

		char *text;
		if (app->interactive) {
			text = text_format("%i: %s", i + 1, name);
		} else {
			text = text_format("%s", name);
		}
		app->tr->p.width = b.w; app->tr->p.height = -1;
		text_get_size(cr, app->tr, text);
		int height = app->tr->p.height;

		cairo_set_source_argb(cr, cal_info->color);
		cairo_rectangle(cr, 0, h, b.w, height + pad);
		cairo_fill(cr);

		cairo_set_source_argb(cr, 0xFF000000);
		cairo_move_to(cr, 0, h + pad / 2);
		app->tr->p.width = b.w; app->tr->p.height = height;
		text_print_free(cr, app->tr, text);

		h += height + pad;
		cairo_move_to(cr, 0, h);
		cairo_line_to(cr, b.w, h);
		cairo_stroke(cr);
	}

	vec_clear(&app->tap_areas);
	int btn_h = 90;

	const char **k = app->config_fns;
	int i = 0;
	if (k) while (*k) {
		app->tr->p.width = b.w; app->tr->p.height = -1;
		text_get_size(cr, app->tr, *k);
		int height = maxi(app->tr->p.height, btn_h);

		if (app->current_filter_fn == *k) {
			cairo_set_source_argb(cr, 0xFF00FF00);
			cairo_rectangle(cr, 0, h, b.w, height + pad);
			cairo_fill(cr);
		}

		struct tap_area ta = {
			.aabb = { b.x, b.y + h, b.w, height + pad },
			.n = i++,
			.cmd = app_cmd_activate_filter
		};
		vec_append(&app->tap_areas, &ta);

		cairo_set_source_argb(cr, 0xFF000000);
		cairo_move_to(cr, 0, h + pad / 2);
		app->tr->p.width = b.w; app->tr->p.height = height;
		text_print_own(cr, app->tr, *k);

		h += height + pad;
		cairo_move_to(cr, 0, h);
		cairo_line_to(cr, b.w, h);
		cairo_stroke(cr);
		k++;
	}

	struct {
		const char *label;
		void (*cmd)(struct app * app, int n);
	} btns[] = {
		{ "today", app_cmd_view_today },
		{ "private", app_cmd_toggle_show_private },
		{ "calendar/todo", app_cmd_switch_view },
	};
	for (int i = 0; i < sizeof(btns) / sizeof(btns[0]); ++i) {
		cairo_set_source_argb(cr, 0xFF000000);
		app->tr->p.scale = 1.5;
		text_print_center(cr, app->tr, (box){ 0, h, b.w, btn_h },
			btns[i].label);
		app->tr->p.scale = 1.0;

		struct tap_area ta = {
			.aabb = { b.x, b.y + h, b.w, btn_h },
			.n = -1,
			.cmd = btns[i].cmd
		};
		vec_append(&app->tap_areas, &ta);

		h += btn_h + pad / 2;
		cairo_move_to(cr, 0, h);
		cairo_line_to(cr, b.w, h);
		cairo_stroke(cr);
	}

	if (app->interactive) {
		cairo_set_source_argb(cr, 0xFF000000);
		cairo_move_to(cr, 0, h += 5);
		app->tr->p.width = b.w; app->tr->p.height = -1;
		const char *str = app->show_private_events ?
			"show private" : "hide private";
		text_get_size(cr, app->tr, str);
		h += app->tr->p.height;
		text_print_own(cr, app->tr, str);

		cairo_set_source_rgba(cr, .3, .3, .3, 1);
		cairo_move_to(cr, 0, h += 5);
		app->tr->p.width = b.w; app->tr->p.height = -1;
		text_get_size(cr, app->tr, usage);
		h += app->tr->p.height;
		text_print_own(cr, app->tr, usage);
	}

	cairo_set_source_rgba(cr, 0, 0, 0, 255);
	cairo_move_to(cr, b.w, 0);
	cairo_line_to(cr, b.w, b.h);
	cairo_stroke(cr);
	cairo_identity_matrix(cr);
}

/* return height */
static int render_todo_item(cairo_t *cr, struct app *app,
		struct active_comp *ac, box b) {
	cairo_translate(cr, b.x, b.y);

	/* prepare all the info we need */
	ts due, start;
	enum prop_status status;
	int perc_c, est;
	bool has_due = props_get_due(ac->ci->p, &due);
	bool has_start = props_get_start(ac->ci->p, &start);
	bool has_status = props_get_status(ac->ci->p, &status);
	bool has_perc_c = props_get_percent_complete(ac->ci->p, &perc_c);
	bool has_est = props_get_estimated_duration(ac->ci->p, &est);
	const char *desc = props_get_desc(ac->ci->p);
	const char *summary = props_get_summary(ac->ci->p);

	bool overdue = has_due && due < app->now;
	bool inprocess = has_status && status == PROP_STATUS_INPROCESS;
	bool not_started = has_start && start > app->now;
	double perc = has_perc_c ? perc_c / 100.0 : 0.0;
	int hpad = 5;

	/* skip invisible (TODO: this should not be here...) */
	if (!ac->vis) {
		cairo_translate(cr, -b.x, -b.y);
		return 0;
	}

	int w = b.w - 80;
	int n = 1;
	if (desc) n += 1;

	/* calculate height */
	if (summary) {
		app->tr->p.width = w/n - 2*hpad; app->tr->p.height = -1;
		text_get_size(cr, app->tr, summary);
		b.h = maxi(b.h, app->tr->p.height);
	}
	if (desc) {
		app->tr->p.width = w/n - 2*hpad; app->tr->p.height = -1;
		text_get_size(cr, app->tr, desc);
		b.h = maxi(b.h, app->tr->p.height);
	}

	if (overdue) {
		cairo_set_source_argb(cr, 0xFFD05050);
		cairo_rectangle(cr, b.w - 80, 0, 80, b.h);
		cairo_fill(cr);
	}
	if (inprocess) {
		double w = b.w - 80;
		if (perc > 0) w *= perc;
		cairo_set_source_argb(cr, 0xFF88FF88);
		cairo_rectangle(cr, 0, 0, w, b.h);
		cairo_fill(cr);
	} else if (has_perc_c) {
		double w = (b.w - 80) * perc;
		cairo_set_source_argb(cr, 0xFF8888FF);
		cairo_rectangle(cr, 0, 0, w, b.h);
		cairo_fill(cr);
	}

	cairo_set_source_argb(cr, not_started ? 0xFF888888 : 0xFF000000);

	/* draw text in the slots */
	char *text = NULL;
	if (has_due) {
		free(text);
		text = natural_date_format(app, due);
	}
	if (has_est) {
		char *text_dur = format_dur(est);
		if (text != NULL) {
			char *text_comb = text_format("%s\n~%s", text, text_dur);
			free(text);
			text = text_comb;
		} else {
			char *text_comb = text_format("~%s", text_dur);
			text = text_comb;
		}
		free(text_dur);
	}
	if (text) {
		text_print_center(cr, app->tr, (box){ b.w - 80, 0, 80, b.h }, text);
		free(text);
	}
	if (summary) {
		text_print_vert_center(cr, app->tr,
			(box){ w*0/n + hpad, 0, w/n - 2*hpad, b.h }, summary);
	}
	if (desc) {
		text_print_vert_center(cr, app->tr,
			(box){ w*1/n + hpad, 0, w/n - 2*hpad, b.h }, desc);
	}

	/* draw slot separators */
	cairo_set_line_width(cr, 1);
	for (int i = 1; i <= n; ++i) {
		cairo_move_to(cr, w*i/n +.5, 0);
		cairo_line_to(cr, w*i/n +.5, b.h);
		cairo_stroke(cr);
	}

	/* draw key tag code */
	if (app->keystate == KEYSTATE_SELECT) {
		uint32_t c = 0xFFFF00FF;
		cairo_set_source_argb(cr, c);
		char *text = ac->code;
		app->tr->p.scale = 3.0;
		app->tr->p.width = b.w; app->tr->p.height = -1;
		text_get_size(cr, app->tr, text);
		cairo_move_to(cr, b.w/2 - app->tr->p.width/2,
			b.h/2 - app->tr->p.height/2);
		text_print_own(cr, app->tr, text);
		app->tr->p.scale = 1.0;
	}

	/* draw separator on bottom side */
	cairo_set_source_argb(cr, 0xFF000000);
	cairo_set_line_width(cr, 2);
	cairo_move_to(cr, 0, b.h);
	cairo_line_to(cr, b.w, b.h);
	cairo_stroke(cr);

	cairo_translate(cr, -b.x, -b.y);

	return b.h;
}

static void render_todo_list(cairo_t *cr, struct app *app, box b) {
	cairo_translate(cr, b.x, b.y);

	/* draw separator on top */
	cairo_set_source_argb(cr, 0xFF000000);
	cairo_set_line_width(cr, 2);
	cairo_move_to(cr, 0, 0);
	cairo_line_to(cr, b.w, 0);
	cairo_stroke(cr);

	int y = 0;
	for (int i = 0; i < app->active_todos.len; ++i) {
		struct active_comp *ac = vec_get(&app->active_todos, i);
		y += render_todo_item(cr, app, ac, (box){0,y,b.w,40});
		if (y > b.h) break;
	}
	cairo_identity_matrix(cr);
}

bool render_application(void *ud, cairo_t *cr) {
	struct app *app = ud;

	/* check whether we need to render */
	int w, h;
	app->backend->vptr->get_window_size(app->backend, &w, &h);
	ts now = ts_now();
	if (app->now != now) {
		app->now = now;
		app->dirty = true;
	}
	if (app->window_width != w ||
			app->window_height != h) {
		app->window_width = w;
		app->window_height = h;
		app->dirty = true;
	}
	if (!app->dirty) return false;

	// struct stopwatch sw = sw_start();
	static int frame_counter = 0;
	++frame_counter;

	cairo_set_source_argb(cr, 0xFFFFFFFF);
	cairo_paint(cr);

	int time_strip_w = 30;
	int sidebar_w = 120;
	int header_h = 60;
	int top_h = 50;

	struct libtouch_rt rt = libtouch_area_get_transform(app->touch_area);

	const char *view_name = "";
	switch (app->main_view) {
	case VIEW_CALENDAR:
		;
		fbox cal_box = { sidebar_w, 0, w - sidebar_w, h };
		fbox header_box = {
			cal_box.x + time_strip_w, cal_box.y,
			cal_box.w - time_strip_w, header_h
		};
		fbox time_strip_box = {
			cal_box.x, cal_box.y + header_h + top_h,
			time_strip_w, cal_box.h - header_h - top_h
		};
		fbox top_box = {
			cal_box.x + time_strip_w, cal_box.y + header_h,
			cal_box.w - time_strip_w, top_h
		};
		fbox main_box = {
			cal_box.x + time_strip_w, cal_box.y + header_h + top_h,
			cal_box.w - time_strip_w, cal_box.h - header_h - top_h
		};

		struct slicing *s = app->slicing;
		float g = libtouch_rt_scaling(&rt);
		double a = app->view.fr, b = app->view.to;
		b = a + (b - a) / g;
		double tx = (rt.t1 / w) * (b - a);
		a -= tx, b -= tx;
		struct ts_ran view = { a, b };
		app_use_view(app, view);
		// fprintf(stderr, "g: %f, t1: %f, view: [%lld, %lld]\n", g, rt.t1, view.fr, view.to);

		enum slicing_type st = SLICING_DAY;
		ts top_th = 3600 * 24;
		ts len = view.to - view.fr;
		if (len > 3600 * 24 * 365) st = SLICING_YEAR, top_th *= 365;
		else if (len > 3600 * 24 * 31) st = SLICING_MONTH, top_th *= 31;

		struct vec tobjs = vec_new_empty(sizeof(struct tobject));
		struct tview_params params = {
			.dir = false,
			.pad = 2,
			.sep_line = 2,
		};
		struct ctx ctx = {
			.cr = cr,
			.app = app,
			.s = s,
			.st = st,
			.level = 1,
			.dir = true,
			.btop = main_box,
			.bmain = main_box,
			.bhead = header_box,
			.p = params,
			.view = view,
			.tobjs = &tobjs,
			.now_shading = true
		};

		ctx.len_clip = (struct ts_ran){ -1, top_th };
		slicing_iter_items(s, &ctx, render_ran, st, view);

		ctx.len_clip = (struct ts_ran){ top_th, -1 };
		ctx.btop = ctx.bmain = top_box;
		ctx.dir = false;
		ctx.level = 0;
		ctx.st = SLICING_DAY;
		ctx.now_shading = false;
		render_ran(&ctx, view, (struct simple_date){ });

		vec_free(&tobjs);

		cairo_reset_clip(cr);

		view_name = "calendar";
		break;
	case VIEW_TODO:
		render_todo_list(cr, app,
			(box){ sidebar_w, header_h, w-sidebar_w, h-header_h });
		view_name = "todo";
	}
	render_sidebar(cr, app, (box){ 0, header_h, sidebar_w, h-header_h });

	if (app->interactive) {
		cairo_move_to(cr, 0, 0);

		struct simple_date sd = simple_date_from_ts(app->now, app->zone);
		char *text = text_format(
				"%s\rframe %d\r%02d:%02d:%02d\rmode: %s",
				cal_timezone_get_desc(app->zone),
				frame_counter,
				sd.hour, sd.minute, sd.second,
				view_name);
		app->tr->p.width = -1 /* sidebar_w */; app->tr->p.height = header_h;
		app->tr->p.scale = 0.9;
		text_print_free(cr, app->tr, text);
		app->tr->p.scale = 1.0;
	}

	app->dirty = false;
	// sw_end_print(sw, "render_application");
	return true;
}
