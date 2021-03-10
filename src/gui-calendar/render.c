#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <mgu/gl.h>
#include <ds/matrix.h>
#include "render.h"
#include "application.h"
#include "core.h"
#include "util.h"
#include <platform_utils/log.h>

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

void w_sidebar_init(struct w_sidebar *w, struct app *app) {
	w->cal_texts = vec_new_empty(sizeof(struct mgu_texture));
	w->filter_texts = vec_new_empty(sizeof(struct mgu_texture));
	w->action_texts = vec_new_empty(sizeof(struct mgu_texture));

	int text_px = 0.2 * app->out->ppvd;
	w->width = 2 * app->out->ppvd;

	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar *cal = vec_get(&app->cals, i);
		char *str = text_format("%i: %s", i + 1, str_cstr(&cal->name));
		struct mgu_texture tex = mgu_tex_text(app->text,
				(struct mgu_text_opts){
			.str = str,
			.s = { w->width, -1 },
			.size_px = text_px,
		});
		free(str);
		vec_append(&w->cal_texts, &tex);
	}

	for (int i = 0; i < app->filters.len; ++i) {
		struct filter *filter = vec_get(&app->filters, i);
		struct mgu_texture tex = mgu_tex_text(app->text,
				(struct mgu_text_opts){
			.str = str_cstr(&filter->desc),
			.s = { w->width, -1 },
			.size_px = text_px,
		});
		vec_append(&w->filter_texts, &tex);
	}

	for (int i = 0; i < app->actions.len; ++i) {
		struct action *action = vec_get(&app->actions, i);

		struct str s = str_new_empty();
		str_append_char(&s, '[');
		str_append_char(&s, action->key_sym);
		str_append_char(&s, ']');
		str_append_char(&s, ' ');
		str_append(&s, str_cstr(&action->label), action->label.v.len);

		struct mgu_texture tex = mgu_tex_text(app->text,
				(struct mgu_text_opts){
			.str = str_cstr(&s),
			.s = { w->width, -1 },
			.size_px = text_px,
			.align_center = true
		});
		str_free(&s);
		vec_append(&w->action_texts, &tex);
	}
}

void w_sidebar_finish(struct w_sidebar *w) {
	struct vec *vecs[] = {
		&w->cal_texts, &w->filter_texts, &w->action_texts };
	for (int j = 0; j < sizeof(vecs) / sizeof(vecs[0]); ++j) {
		struct vec *vj = vecs[j];
		for (int i = 0; i < vj->len; ++i) {
			struct mgu_texture *tex = vec_get(vj, i);
			mgu_texture_destroy(tex);
		}
		vec_free(vj);
	}
}

static void w_sidebar_render(struct w_sidebar *w, struct app *app,
		struct float4 b) {
	float h = 0;
	float pad = 6;
	for (int i = 0; i < app->cals.len; ++i) {
		struct calendar_info *cal_info = vec_get(&app->cal_infos, i);
		struct mgu_texture *tex = vec_get(&w->cal_texts, i);
		float height = tex->s[1];

		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y + h, b.w, height + pad },
			.argb = cal_info->color
		});

		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEX,
			.p = { b.x, b.y + h + pad / 2, b.w, height },
			.argb = 0xFF000000,
			.tex = *tex
		});

		h += height + pad + 1;
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y + h - 1, b.w, 1 },
			.argb = 0xFF000000
		});
	}

	vec_clear(&app->tap_areas);
	float btn_h = 10 * app->out->ppmm;
	for (int i = 0; i < app->filters.len; ++i) {
		struct mgu_texture *tex = vec_get(&w->filter_texts, i);
		float height = tex->s[1];

		if (app->current_filter == i) {
			sr_put(app->sr, (struct sr_spec){
				.t = SR_RECT,
				.p = { b.x, b.y + h, b.w, height + pad },
				.argb = 0xFF00FF00
			});
		}

		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEX,
			.p = { b.x, b.y + h, b.w, height + pad },
			.argb = 0xFF000000,
			.o = SR_CENTER_V,
			.tex = *tex
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

		struct mgu_texture *tex = vec_get(&w->action_texts, i);

		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEX,
			.p = { b.x, b.y + h, b.w, btn_h + pad },
			.argb = 0xFF000000,
			.o = SR_CENTER,
			.tex = *tex
		});

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

struct tview_params {
	bool dir;
	double pad;
	double sep_line;
	/* skip view_ran.fr from the start of each slice, and end at
	 * view_ran.to */
	struct ts_ran view_ran;
};
static void render_tobject(struct app *app,
		struct tobject *obj, fbox b, struct tview_params p) {
	int text_px = 0.18 * app->out->ppvd;

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
	bool draw_labels = w >= text_px * 2 && h >= text_px;

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
		float loc_h = 0;
		const char *location =
			props_get_location(obj->ac->ci->p);
		if (location) {
			struct mgu_texture *loc_tex = &obj->ac->loc_tex;
			if (!loc_tex->tex) {
				*loc_tex = mgu_tex_text(app->text,
						(struct mgu_text_opts){
					.str = location,
					.s = { w, -1 },
					.size_px = text_px,
				});
			}
			loc_h = mini(h / 2, loc_tex->s[1]);
			sr_put(app->sr, (struct sr_spec){
				.t = SR_TEX,
				.p = { x, y + h - loc_h, w, loc_h },
				.argb = light ? 0xFFA0A0A0 : 0xFF606060,
				.tex = *loc_tex
			});
		}

		if (!obj->ac->tex.tex) {
			const char *summary = props_get_summary(obj->ac->ci->p);
			struct simple_date local_start = simple_date_from_ts(
				obj->ac->ci->time.fr, app->zone);
			struct simple_date local_end = simple_date_from_ts(
				obj->ac->ci->time.to, app->zone);
			char *str = text_format("%02d:%02d-%02d:%02d %s",
					local_start.hour, local_start.minute,
					local_end.hour, local_end.minute,
					summary);
			obj->ac->tex = mgu_tex_text(app->text,
					(struct mgu_text_opts){
				.str = str,
				.s = { w, -1 },
				.size_px = text_px,
			});
			free(str);
		}
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEX,
			.p = { x, y, w, h - loc_h },
			.argb = fg,
			.tex = obj->ac->tex
		});

	}
	// if (draw_labels && obj->type == TOBJECT_TODO) {
	// 	cairo_set_source_argb(cr, fg);
	// 	cairo_move_to(cr, x, y);
	// 	app->tr->p.width = w; app->tr->p.height = h;
	// 	char *text = text_format("TODO: %s",
	// 		props_get_summary(obj->ac->ci->p));
	// 	text_print_free(cr, app->tr, text);
	// }

	/* draw keycode tags */
	if (obj->type == TOBJECT_EVENT && app->keystate == KEYSTATE_SELECT) {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { x, y, w, h },
			.argb = (color ^ 0x00FFFFFF) | 0xFF000000,
			.o = SR_CENTER,
			.text = { .px = text_px * 4, .s = obj->ac->code },
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
	uint32_t viewport[2];
};
static void render_ran(void *env, struct ts_ran ran, struct simple_date label) {
	struct ctx *ctx = env;
	struct app *app = ctx->app;
	fbox btop = ctx->btop;
	fbox bmain = ctx->bmain, bhead = ctx->bhead;
	fbox bsl = fbox_slice(bmain, ctx->dir, ctx->view, ran);
	fbox bhsl = fbox_slice(bhead, ctx->dir, ctx->view, ran);

	if (ctx->now_shading && ts_ran_in(ran, ctx->app->now)) {
		sr_clip_push(app->sr,
			(float[]){ btop.x, btop.y, btop.w, btop.h });

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

		sr_present(app->sr, ctx->viewport);
		sr_clip_pop(app->sr);
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
			slicing_iter_items(ctx->s, &next_ctx,
				render_ran, next_ctx.st, ran);
		}
	}

	sr_clip_push(app->sr, (float[]){ btop.x, btop.y, btop.w, btop.h });

	if ((ctx->level == 0 && ctx->st < SLICING_HOUR)
			|| ctx->st == SLICING_DAY) {
		/* draw overlapping objects */
		vec_clear(ctx->tobjs);
		struct interval_iter i_iter = interval_iter(
			&ctx->app->active_events.in_view,
			(long long int[]){ ran.fr, ran.to });
		struct interval_node *nx;
		while (interval_iter_next(&i_iter, &nx)) {
			struct active_comp *ac = container_of(nx,
				struct active_comp, node_by_view);
			struct tobject obj = {
				.time = ac->ci->time,
				.type = TOBJECT_EVENT,
				.ac = ac,
			};

			ts len = obj.time.to - obj.time.fr;
			if (ctx->len_clip.fr != -1
					&& ctx->len_clip.fr > len) continue;
			if (ctx->len_clip.to != -1
					&& ctx->len_clip.to <= len) continue;

			vec_append(ctx->tobjs, &obj);
		}
		tobject_layout(ctx->tobjs, NULL);
		double len = ran.to - ran.fr;
		for (int i = 0; i < ctx->tobjs->len; ++i) {
			struct tobject *obj = vec_get(ctx->tobjs, i);
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

	sr_present(app->sr, ctx->viewport);
	sr_clip_pop(app->sr);

	if (ctx->level == 1) {
		sr_clip_push(app->sr,
			(float[]){ bhead.x, bhead.y, bhead.w, bhead.h });

		/* draw header */
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { bhsl.x, bhsl.y, ctx->p.sep_line, bhsl.h },
			.argb = 0xFF000000
		});

		char *text;
		if (label.t[1] == 0) text = text_format("%d", label.t[0]);
		else if (label.t[2] == 0) text = text_format("%d-%d",
			label.t[0], label.t[1]);
		else text = text_format("%d-%d-%d",
			label.t[0], label.t[1], label.t[2]);
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { bhsl.x, bhsl.y, bhsl.w, bhsl.h },
			.argb = 0xFF000000,
			.o = SR_CENTER,
			.text = { .px = 18, .s = text }
		});
		free(text);

		sr_present(app->sr, ctx->viewport);
		sr_clip_pop(app->sr);
	}
}

/* return height */
static int render_todo_item(struct app *app, struct active_comp *ac, box b) {
	int text_px = 0.18 * app->out->ppvd;

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

	bool completed = has_status && status == PROP_STATUS_COMPLETED;
	bool inprocess = has_status && status == PROP_STATUS_INPROCESS;
	bool overdue = !completed && has_due && due < app->now;
	bool not_started = has_start && start > app->now;
	bool has_cats = cats && cats->len > 0;
	double perc = has_perc_c ? perc_c / 100.0 : 0.0;
	int hpad = 5;

	const int due_w = 80;
	int w = b.w - due_w;
	int n = 1;
	if (desc) n += 1;
	if (has_cats) n += 1;

	struct mgu_texture tex_summary = { 0 }, tex_desc = { 0 };

	/* calculate height */
	if (summary) {
		tex_summary = mgu_tex_text(app->text, (struct mgu_text_opts){
			.str = summary,
			.s = { w/n - 2*hpad, -1 },
			.size_px = text_px,
		});
		b.h = maxi(b.h, tex_summary.s[1]);
	}
	if (desc) {
		tex_desc = mgu_tex_text(app->text, (struct mgu_text_opts){
			.str = desc,
			.s = { w/n - 2*hpad, -1 },
			.size_px = text_px,
		});
		b.h = maxi(b.h, tex_desc.s[1]);
	}

	if (overdue) {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x + b.w - due_w, b.y, due_w, b.h },
			.argb = 0xFFD05050
		});
	}
	if (completed || inprocess) {
		double w = b.w - 80;
		if (perc > 0) w *= perc;
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y, w, b.h },
			.argb = completed ? 0xFFAAAAAA : 0xFF88FF88
		});
	} else if (has_perc_c) {
		double w = (b.w - 80) * perc;
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x, b.y, w, b.h },
			.argb = 0xFF8888FF
		});
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
			char *text_comb =
				text_format("%s\n~%s", text, text_dur);
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
			.o = SR_CENTER,
			.text = { .px = text_px, .s = text },
		});
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
			.o = SR_CENTER,
			.text = { .px = text_px, .s = str_cstr(&s) },
		});
		str_free(&s);
	}
	if (summary) {
		int i = 0 + (has_cats ? 1 : 0);
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEX,
			.p = { b.x + w*i/n + hpad, b.y, w/n - 2*hpad, b.h },
			.argb = 0xFF000000,
			.o = SR_CENTER | SR_TEX_PASS_OWNERSHIP,
			.tex = tex_summary,
		});
	}
	if (desc) {
		int i = 1 + (has_cats ? 1 : 0);
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEX,
			.p = { b.x + w*i/n + hpad, b.y, w/n - 2*hpad, b.h },
			.argb = 0xFF000000,
			.o = SR_CENTER_V | SR_TEX_PASS_OWNERSHIP,
			.tex = tex_desc,
		});
	}

	/* draw slot separators */
	for (int i = 1; i <= n; ++i) {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_RECT,
			.p = { b.x + w*i/n, b.y, 1, b.h },
			.argb = 0xFF000000
		});
	}

	/* draw keycode tags */
	if (app->keystate == KEYSTATE_SELECT) {
		sr_put(app->sr, (struct sr_spec){
			.t = SR_TEXT,
			.p = { b.x, b.y, b.w, b.h },
			.argb = 0xFFFF00FF,
			.o = SR_CENTER,
			.text = { .px = 40, .s = ac->code }
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

	return b.h + 2;
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

bool render_application(void *env, struct mgu_win_surf *surf, uint64_t t) {
	struct app *app = env;

	/* check whether we need to render */
	int w = surf->size[0], h = surf->size[1];
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

	app->out = mgu_win_surf_get_output(surf);
	asrt(app->out, "app->out NULL");

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
	float sidebar_w = app->w_sidebar.width;
	float header_h = 1 * app->out->ppvd;
	float top_h = header_h;

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
		app_update_projections(app);

		enum slicing_type st = SLICING_DAY;
		ts top_th = 3600 * 24;
		ts len = view.to - view.fr;
		if (len > 3600 * 24 * 365) st = SLICING_YEAR, top_th *= 365;
		else if (len > 3600 * 24 * 31) st = SLICING_MONTH, top_th *= 31;

		struct ts_ran bounds = slicing_get_bounds(s, st, view);
		app_use_view(app, bounds);

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
			.now_shading = true,
			.viewport = { surf->size[0], surf->size[1] },
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
	w_sidebar_render(&app->w_sidebar, app, (struct float4){
		.a = { 0, header_h, sidebar_w, h-header_h } });

	struct simple_date sd = simple_date_from_ts(app->now, app->zone);
	char *text = text_format(
			"%s\nframe %d\n%02d:%02d:%02d\nmode: %s",
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
		.text = { .px = 0.18 * app->out->ppvd, .s = text }
	});
	free(text);

	sr_present(app->sr, surf->size);

	app->dirty = false;
	// sw_end_print(sw, "render_application");
	return true;
}
