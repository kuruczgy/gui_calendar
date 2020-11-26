#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <mgu/gl.h>
#include <ds/matrix.h>
#include "render.h"
#include "application.h"
#include "core.h"
#include "util.h"

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

char* text_format(const char *fmt, ...) {
       va_list args;
       va_start(args, fmt);
       // Add one since vsnprintf excludes null terminator.
       int length = vsnprintf(NULL, 0, fmt, args) + 1;
       va_end(args);

       char *buf = malloc(length);
       asrt(buf, "malloc error");
       va_start(args, fmt);
       vsnprintf(buf, length, fmt, args);
       va_end(args);

       return buf;
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

struct tview_params {
	bool dir;
	double pad;
	double sep_line;
	/* skip view_ran.fr from the start of each slice, and end at view_ran.to */
	struct ts_ran view_ran;
};
static void render_tobject(struct app *app,
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
	uint32_t color = 0;
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
	sr_put(app->sr, (struct sr_spec){
		.t = SR_RECT,
		.p = { x, y, w, h },
		.argb = color
	});
	//- cairo_set_source_argb(cr, color);
	//- cairo_rectangle(cr, x, y, w, h);
	//- cairo_fill(cr);

	/* draw various labels */
	if (draw_labels && obj->type == TOBJECT_EVENT && !obj->ac->hide) {
		const char *location = props_get_location(obj->ac->ci->p);
		float s[2] = { 0, 0 };
		if (location) {
			sr_measure(app->sr, s, (struct sr_spec){
				.t = SR_TEXT,
				.p = { 0, 0, w, h },
				.text = { .px = 10, .s = location }
			});
		}
		int loc_h = location ? mini(h / 2, s[1]) : 0;

		const char *summary = props_get_summary(obj->ac->ci->p);
		struct simple_date local_start =
			simple_date_from_ts(obj->ac->ci->time.fr, app->zone);
		struct simple_date local_end =
			simple_date_from_ts(obj->ac->ci->time.to, app->zone);
		char *text = text_format("%02d:%02d-%02d:%02d %s",
				local_start.hour, local_start.minute,
				local_end.hour, local_end.minute,
				summary);
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { x, y, w, h - loc_h },
			.argb = fg,
			.text = { .px = 10, .s = text }
		});
		free(text);

		if (location) {
			sr_put(app->sr, (struct sr_spec){
				.t = SR_TEXT,
				.p = { x, y + h - loc_h, w, loc_h },
				.argb = light ? 0xFFA0A0A0 : 0xFF606060,
				.text = { .px = 10, .s = location }
			});
		}
	}
	// if (draw_labels && obj->type == TOBJECT_TODO) {
	// 	cairo_set_source_argb(cr, fg);
	// 	cairo_move_to(cr, x, y);
	// 	app->tr->p.width = w; app->tr->p.height = h;
	// 	char *text = text_format("TODO: %s", props_get_summary(obj->ac->ci->p));
	// 	text_print_free(cr, app->tr, text);
	// }

	/* draw keycode tags */
	if (obj->type == TOBJECT_EVENT && app->keystate == KEYSTATE_SELECT) {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { x, y, w, h },
			.argb = (color ^ 0x00FFFFFF) | 0xFF000000,
			.text = { .px = 40, .s = obj->ac->code, .o = SR_CENTER }
		});
	}
}
struct ctx {
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
	struct app *app = ctx->app;
	// fbox btop = ctx->btop;
	fbox bmain = ctx->bmain, bhead = ctx->bhead;
	fbox bsl = fbox_slice(bmain, ctx->dir, ctx->view, ran);
	fbox bhsl = fbox_slice(bhead, ctx->dir, ctx->view, ran);

	if (ctx->now_shading && ts_ran_in(ran, ctx->app->now)) {
		// cairo_reset_clip(ctx->cr);
		// cairo_rectangle(ctx->cr, btop.x, btop.y, btop.w, btop.h);
		// cairo_clip(ctx->cr);

		uint32_t l = (64 + ctx->level * 128) & 0xFF;
		uint32_t bg = 0xFFFF0000 | (l << 8) | l;
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { bsl.x, bsl.y, bsl.w, bsl.h },
			.argb = bg
		});
		if (ctx->level == 1) {
			sr_put(app->sr, (struct sr_spec){
				.t = SR_RECT,
				.p = { bhsl.x, bhsl.y, bhsl.w, bhsl.h },
				.argb = bg
			});
		}
		//- cairo_set_source_argb(ctx->cr, bg);
		//- cairo_rectangle(ctx->cr, bsl.x, bsl.y, bsl.w, bsl.h);
		//- if (ctx->level == 1) cairo_rectangle(ctx->cr, bhsl.x, bhsl.y, bhsl.w, bhsl.h);
		//- cairo_fill(ctx->cr);
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

	// cairo_reset_clip(ctx->cr);
	// cairo_rectangle(ctx->cr, btop.x, btop.y, btop.w, btop.h);
	// cairo_clip(ctx->cr);

	if ((ctx->level == 0 && ctx->st < SLICING_HOUR) || ctx->st == SLICING_DAY) {
		/* draw overlapping objects */
		vec_clear(ctx->tobjs);
		interval_query(
			&ctx->app->active_events.tree,
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
			render_tobject(ctx->app, obj, nb, ctx->p);
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
				nb.h = 2;
			} else {
				nb.x = bsl.x + bsl.w * pa;
				nb.y = bsl.y;
				nb.w = 2;
				nb.h = bsl.h;
			}
			sr_put(app->sr, (struct sr_spec){
				.t = SR_RECT,
				.p = { nb.x, nb.y, nb.w, nb.h },
				.argb = 0xFFFF0000
			});
			//- cairo_set_line_width(ctx->cr, 2);
			//- cairo_move_to(ctx->cr, nb.x, nb.y);
			//- cairo_line_to(ctx->cr, nb.x + nb.w, nb.y + nb.h);
			//- cairo_set_source_rgba(ctx->cr, 255, 0, 0, 255);
			//- cairo_stroke(ctx->cr);
		}
	}

	/* draw lines between slices */
	float lw = ctx->p.sep_line;
	if (ctx->dir) {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { bsl.x - lw/2, bsl.y, lw, bsl.h },
			.argb = 0xFF000000
		});
	} else {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { bsl.x, bsl.y - lw/2, bsl.w, lw },
			.argb = 0xFF000000
		});
	}

	if (ctx->level == 1) {
		// cairo_reset_clip(ctx->cr);
		// cairo_rectangle(ctx->cr, bhead.x, bhead.y, bhead.w, bhead.h);
		// cairo_clip(ctx->cr);

		/* draw header */
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { bhsl.x, bhsl.y, ctx->p.sep_line, bhsl.h },
			.argb = 0xFF000000
		});
		//- cairo_set_source_argb(ctx->cr, 0xFF000000);
		//- cairo_set_line_width(ctx->cr, ctx->p.sep_line);
		//- cairo_move_to(ctx->cr, bhsl.x, bhsl.y);
		//- cairo_line_to(ctx->cr, bhsl.x, bhsl.y + bhsl.h);
		//- cairo_stroke(ctx->cr);

		char *text;
		if (label.t[1] == 0) text = text_format("%d", label.t[0]);
		else if (label.t[2] == 0) text = text_format("%d-%d", label.t[0], label.t[1]);
		else text = text_format("%d-%d-%d", label.t[0], label.t[1], label.t[2]);
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { bhsl.x, bhsl.y, bhsl.w, bhsl.h },
			.argb = 0xFF000000,
			.text = { .px = 18, .s = text, .o = SR_CENTER }
		});
		free(text);

		// cairo_reset_clip(ctx->cr);
	}
}

static void render_sidebar(struct app *app, box b) {
	int h = 0;
	int pad = 6;
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		struct calendar_info *cal_info = vec_get(&app->cal_infos, i);
		const char *name = str_cstr(&cal->name);
		// if (!app->show_private_events && cal->priv) continue;

		char *text = text_format("%i: %s", i + 1, name);
		float s[2];
		sr_measure(app->sr, s, (struct sr_spec){
			.t = SR_TEXT,
			.p = { 0, 0, b.w, -1 },
			.text = { .px = 10, .s = text }
		});
		int height = s[1];

		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y + h, b.w, height + pad },
			.argb = cal_info->color
		});
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { b.x, b.y + h + pad / 2, b.w, height },
			.argb = 0xFF000000,
			.text = { .px = 10, .s = text },
		});

		h += height + pad + 1;
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y + h - 1, b.w, 1 },
			.argb = 0xFF000000
		});
	}

	vec_clear(&app->tap_areas);
	int btn_h = 20;

	for (int i = 0; i < app->filters.len; ++i) {
		struct filter *f = vec_get(&app->filters, i);

		float s[2];
		sr_measure(app->sr, s, (struct sr_spec){
			.t = SR_TEXT,
			.p = { 0, 0, b.w, -1 },
			.text = { .px = 10, .s = str_cstr(&f->desc) }
		});
		int height = maxi(s[1], btn_h);

		if (app->current_filter == i) {
			sr_put(app->sr, (struct sr_spec){
				.t = SR_RECT,
				.p = { b.x, b.y + h, b.w, height + pad },
				.argb = 0xFF00FF00
			});
		}

		// struct tap_area ta = {
		// 	.aabb = { b.x, b.y + h, b.w, height + pad },
		// 	.n = i,
		// 	.cmd = app_cmd_activate_filter
		// };
		// vec_append(&app->tap_areas, &ta);

		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { b.x, b.y + h, b.w, height + pad },
			.argb = 0xFF000000,
			.text = { .px = 10, .s = str_cstr(&f->desc),
					.o = SR_CENTER_V },
		});

		h += height + pad + 1;
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y + h - 1, b.w, 1 },
			.argb = 0xFF000000
		});
	}

	for (int i = 0; i < app->actions.len; ++i) {
		struct action *act = vec_get(&app->actions, i);
		if (!str_any(&act->label)) continue;
		if (act->cond.view != app->main_view) continue;

		struct str s = str_new_empty();
		str_append_char(&s, '[');
		str_append_char(&s, act->key_sym);
		str_append_char(&s, ']');
		str_append_char(&s, ' ');
		str_append(&s, str_cstr(&act->label), act->label.v.len);

		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { b.x, b.y + h, b.w, btn_h + pad },
			.argb = 0xFF000000,
			.text = { .px = 13, .s = str_cstr(&s), .o = SR_CENTER },
		});

		str_free(&s);

		struct tap_area ta = {
			.aabb = { b.x, b.y + h, b.w, btn_h },
			.action_idx = i
		};
		vec_append(&app->tap_areas, &ta);

		h += btn_h + pad + 1;
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y + h - 1, b.w, 1 },
			.argb = 0xFF000000
		});
	}


	sr_put(app->sr, (struct sr_spec){
		.t = SR_RECT,
		.p = { b.x + b.w, b.y, 2, b.h },
		.argb = 0xFF000000
	});
}

/* return height */
static int render_todo_item(struct app *app, struct active_comp *ac, box b) {
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
	const struct vec *cats = props_get_categories(ac->ci->p);

	bool overdue = has_due && due < app->now;
	bool inprocess = has_status && status == PROP_STATUS_INPROCESS;
	bool not_started = has_start && start > app->now;
	bool has_cats = cats && cats->len > 0;
	double perc = has_perc_c ? perc_c / 100.0 : 0.0;
	int hpad = 5;

	const int due_w = 80;
	int w = b.w - due_w;
	int n = 1;
	if (desc) n += 1;
	if (has_cats) n += 1;
	float s[2];

	/* calculate height */
	if (summary) {
		sr_measure(app->sr, s, (struct sr_spec){
			.t = SR_TEXT,
			.p = { 0, 0, w/n - 2*hpad, -1 },
			.text = { .px = 10, .s = summary },
		});
		//- app->tr->p.width = w/n - 2*hpad; app->tr->p.height = -1;
		//- text_get_size(cr, app->tr, summary);
		b.h = maxi(b.h, s[1]);
	}
	if (desc) {
		sr_measure(app->sr, s, (struct sr_spec){
			.t = SR_TEXT,
			.p = { 0, 0, w/n - 2*hpad, -1 },
			.text = { .px = 10, .s = desc },
		});
		//- app->tr->p.width = w/n - 2*hpad; app->tr->p.height = -1;
		//- text_get_size(cr, app->tr, desc);
		b.h = maxi(b.h, s[1]);
	}

	if (overdue) {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x + b.w - due_w, b.y, due_w, b.h },
			.argb = 0xFFD05050
		});
		//- cairo_set_source_argb(cr, 0xFFD05050);
		//- cairo_rectangle(cr, b.w - 80, 0, 80, b.h);
		//- cairo_fill(cr);
	}
	if (inprocess) {
		double w = b.w - 80;
		if (perc > 0) w *= perc;
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y, w, b.h },
			.argb = 0xFF88FF88
		});
		//- cairo_set_source_argb(cr, 0xFF88FF88);
		//- cairo_rectangle(cr, 0, 0, w, b.h);
		//- cairo_fill(cr);
	} else if (has_perc_c) {
		double w = (b.w - 80) * perc;
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y, w, b.h },
			.argb = 0xFF8888FF
		});
		//- cairo_set_source_argb(cr, 0xFF8888FF);
		//- cairo_rectangle(cr, 0, 0, w, b.h);
		//- cairo_fill(cr);
	}

	uint32_t t_col = not_started ? 0xFF888888 : 0xFF000000;

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
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { b.x + b.w - due_w, b.y, due_w, b.h },
			.argb = t_col,
			.text = { .px = 10, .s = text, .o = SR_CENTER },
		});
		//- text_print_center(cr, app->tr, (box){ b.w - 80, 0, 80, b.h }, text);
		free(text);
	}
	if (has_cats) {
		int i = 0;
		struct str s = str_new_empty();
		str_append_char(&s, '[');
		for (int k = 0; k < cats->len; ++k) {
			const struct str *si = vec_get_c(cats, k);
			str_append(&s, str_cstr(si), si->v.len);
			if (k < cats->len - 1) str_append_char(&s, ' ');
		}
		str_append_char(&s, ']');
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { b.x + w*i/n + hpad, b.y, w/n - 2*hpad, b.h },
			.argb = 0xFF000000,
			.text = { .px = 10, .s = str_cstr(&s), .o = SR_CENTER },
		});
		//- text_print_vert_center(cr, app->tr,
		//- 	(box){ w*i/n + hpad, 0, w/n - 2*hpad, b.h }, str_cstr(&s));
		str_free(&s);
	}
	if (summary) {
		int i = 0 + (has_cats ? 1 : 0);
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { b.x + w*i/n + hpad, b.y, w/n - 2*hpad, b.h },
			.argb = 0xFF000000,
			.text = { .px = 10, .s = summary, .o = SR_CENTER },
		});
		//- text_print_vert_center(cr, app->tr,
		//- 	(box){ w*i/n + hpad, 0, w/n - 2*hpad, b.h }, summary);
	}
	if (desc) {
		int i = 1 + (has_cats ? 1 : 0);
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { b.x + w*i/n + hpad, b.y, w/n - 2*hpad, b.h },
			.argb = 0xFF000000,
			.text = { .px = 10, .s = desc, .o = SR_CENTER_V },
		});
	}

	/* draw slot separators */
	for (int i = 1; i <= n; ++i) {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x + w*i/n, b.y, 1, b.h },
			.argb = 0xFF000000
		});
		//- cairo_move_to(cr, w*i/n +.5, 0);
		//- cairo_line_to(cr, w*i/n +.5, b.h);
		//- cairo_stroke(cr);
	}

	/* draw keycode tags */
	if (app->keystate == KEYSTATE_SELECT) {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { b.x, b.y, b.w, b.h },
			.argb = 0xFFFF00FF,
			.text = { .px = 40, .s = ac->code, .o = SR_CENTER }
		});
	}

	/* draw separator on bottom side */
	sr_put(app->sr, (struct sr_spec){
		.t = SR_RECT,
		.p = { b.x, b.y + b.h, b.w, 2 },
		.argb = 0xFF000000
	});
	//- cairo_set_source_argb(cr, 0xFF000000);
	//- cairo_set_line_width(cr, 2);
	//- cairo_move_to(cr, 0, b.h);
	//- cairo_line_to(cr, b.w, b.h);
	//- cairo_stroke(cr);

	return b.h;
}

static void render_todo_list(struct app *app, box b) {
	/* draw separator on top */
	sr_put(app->sr, (struct sr_spec){
		.t = SR_RECT,
		.p = { b.x, b.y, b.w, 2 },
		.argb = 0xFF000000
	});
	//- cairo_set_source_argb(cr, 0xFF000000);
	//- cairo_set_line_width(cr, 2);
	//- cairo_move_to(cr, 0, 0);
	//- cairo_line_to(cr, b.w, 0);
	//- cairo_stroke(cr);

	int y = 0;
	for (int i = 0; i < app->active_todos.v.len; ++i) {
		struct active_comp *ac = vec_get(&app->active_todos.v, i);
		if (!ac->vis) continue;
		y += render_todo_item(app, ac, (box){ b.x, b.y + y, b.w, 40 });
		if (y > b.h) break;
	}
}

bool render_application(void *env, float t) {
	struct app *app = env;

	/* check whether we need to render */
	int w = app->win->size[0], h = app->win->size[1];
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

	glViewport(0, 0, w, h);

	/* set blending */
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	int time_strip_w = 30;
	int sidebar_w = 160;
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
		// fbox time_strip_box = {
		// 	cal_box.x, cal_box.y + header_h + top_h,
		// 	time_strip_w, cal_box.h - header_h - top_h
		// };
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
		app_set_view(app, view);
		app_update_projections(app);
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

		// cairo_reset_clip(cr);

		view_name = "calendar";
		break;
	case VIEW_TODO:
		app_update_projections(app);
		render_todo_list(app,
			(box){ sidebar_w, header_h, w-sidebar_w, h-header_h });
		view_name = "todo";
		break;
	default:
		asrt(false, "");
		break;
	}
	render_sidebar(app, (box){ 0, header_h, sidebar_w, h-header_h });

	struct simple_date sd = simple_date_from_ts(app->now, app->zone);
	char *text = text_format(
			"%s\rframe %d\r%02d:%02d:%02d\rmode: %s",
			cal_timezone_get_desc(app->zone),
			frame_counter,
			sd.hour, sd.minute, sd.second,
			view_name);
	// app->tr->p.width = -1 /* sidebar_w */; app->tr->p.height = header_h;
	// app->tr->p.scale = 0.9;
	// app->tr->p.scale = 1.0;
	sr_put(app->sr, (struct sr_spec){
		.t = SR_TEXT,
		.p = { 0, 0, sidebar_w, header_h },
		.argb = 0xFF000000,
		.text = { .px = 10, .s = text }
	});
	free(text);

	float proj[9];
	mat3_ident(proj);
	mat3_proj(proj, app->win->size);
	sr_present(app->sr, proj);

	app->dirty = false;
	// sw_end_print(sw, "render_application");
	return true;
}
