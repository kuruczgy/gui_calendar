#include <stdlib.h>

#include "views.h"
#include "pango.h"
#include "algo.h"
#include "core.h"

static void tslice_finish(struct tslice *tsl) {
	free(tsl->lines.s);
	tsl->lines.s = NULL;
	tsl->lines.n = -1;

	vec_free(&tsl->objs);

	free(tsl->header_label);
	tsl->header_label = NULL;
}
void tview_finish(struct tview *tv) {
	for (int i = 0; i < tv->n; ++i) {
		tslice_finish(&tv->s[i]);
	}
	free(tv->s);
	*tv = (struct tview){ .s = NULL, .n = -1 };
}
static void tslice_init(struct tslice *tsl) {
	tsl->objs = vec_new_empty(sizeof(struct tobject));
}
static void alloc_n_slices(struct tview *tv, int n) {
	tv->s = malloc_check(sizeof(struct tslice) * n);
	tv->n = n;
	for (int i = 0; i < n; ++i) {
		tslice_init(&tv->s[i]);
	}
}
static void alloc_lines(struct tslice *tsl, int n) {
	tsl->lines = (struct tslice_lines){
		.s = malloc_check(sizeof(ts) * n),
		.n = n
	};
}

void tview_init_range(struct tview *tv, struct tview_spec *spec) {
	alloc_n_slices(tv, 1);

	struct tslice *tsl = &tv->s[0];
	tsl->ran.fr = spec->base;
	tsl->ran.to = spec->to;
	tsl->header_label = NULL;

	tsl->lines.n = 0;
	tsl->lines.s = NULL;

	tv->max_len = tsl->ran.to - tsl->ran.fr;
	tv->ran_hull = tsl->ran;
}

void tview_init(struct tview *tv, struct tview_spec *spec) {
	if (spec->type == TVIEW_RANGE) asrt(spec->n == 1, "wrong spec");
	alloc_n_slices(tv, spec->n);
	tv->max_len = -1;
	tv->ran_hull = (struct ts_ran){ -1, -1 };
	struct simple_date base = simple_date_from_ts(spec->base, spec->zone);
	base.second = base.minute = base.hour = 0;
	for (int i = 0; i < tv->n; ++i) {
		struct tslice *tsl = &tv->s[i];
		struct ts_ran *r = &tsl->ran;
		switch (spec->type) {
		case TVIEW_DAYS:
			base.hour = spec->h1;
			r->fr = simple_date_to_ts(base, spec->zone);
			tsl->header_label = text_format("%s: %02d-%02d",
				simple_date_day_of_week_name(base), base.month, base.day);
			base.hour = spec->h2;
			r->to = simple_date_to_ts(base, spec->zone);
			alloc_lines(tsl, spec->h2 - spec->h1);
			for (int k = 0; k < tsl->lines.n; ++k) {
				base.hour = spec->h1 + k;
				tsl->lines.s[k] = simple_date_to_ts(base, spec->zone);
			}
			base.day += 1;
			simple_date_normalize(&base);
			break;
		case TVIEW_WEEKS:
			r->fr = simple_date_to_ts(base, spec->zone);
			tsl->header_label = text_format("%04d w%02d",
				base.year, simple_date_week_number(base));
			alloc_lines(tsl, 7);
			for (int k = 0; k < tsl->lines.n; ++k) {
				base.day++;
				tsl->lines.s[k] = simple_date_to_ts(base, spec->zone);
			}
			r->to = simple_date_to_ts(base, spec->zone);
			simple_date_normalize(&base);
			break;
		case TVIEW_MONTHS:
			r->fr = simple_date_to_ts(base, spec->zone);
			tsl->header_label = text_format("%04d-%02d", base.year, base.month);
			alloc_lines(tsl, simple_date_days_in_month(base));
			for (int k = 0; k < tsl->lines.n; ++k) {
				base.day = k + 1;
				tsl->lines.s[k] = simple_date_to_ts(base, spec->zone);
			}
			base.day = 1;
			base.month += 1;
			simple_date_normalize(&base);
			r->to = simple_date_to_ts(base, spec->zone);
			break;
		case TVIEW_YEARS:
			r->fr = simple_date_to_ts(base, spec->zone);
			tsl->header_label = text_format("%04d", base.year);
			alloc_lines(tsl, 12);
			for (int k = 0; k < tsl->lines.n; ++k) {
				base.month = k + 1;
				tsl->lines.s[k] = simple_date_to_ts(base, spec->zone);
			}
			base.month = 1;
			base.year += 1;
			simple_date_normalize(&base);
			r->to = simple_date_to_ts(base, spec->zone);
			break;
		default:
			asrt(false, "wrong tview type");
			break;
		}

		ts len = r->to - r->fr;
		tv->max_len = max_ts(tv->max_len, len);

		if (tv->ran_hull.fr == -1) tv->ran_hull = *r;
		else tv->ran_hull = ts_ran_hull(tv->ran_hull, *r);
	}
	spec->to = tv->s[tv->n - 1].ran.to;
	tv->min_content = tv->max_len + 1; tv->max_content = -1;
}
bool tview_try_put(struct tview *tv, struct tobject obj) {
	bool inserted = false;
	for (int i = 0; i < tv->n; ++i) {
		struct tslice *tsl = &tv->s[i];
		if (ts_ran_overlap(tsl->ran, obj.time)) {
			vec_append(&tsl->objs, &obj);
			struct ts_ran ran = {
				obj.time.fr - tsl->ran.fr, obj.time.to - tsl->ran.fr };
			if (ran.fr < tv->min_content)
				tv->min_content = max_ts(ran.fr, 0);
			if (ran.to > tv->max_content)
				tv->max_content = min_ts(ran.to, tsl->ran.to - tsl->ran.fr);
			inserted = true;
		}
	}
	return inserted;
}
void tview_update_layout(struct tview *tv) {
	struct vec la = vec_new_empty(sizeof(struct layout_event));
	for (int i = 0; i < tv->n; ++i) {
		struct tslice *tsl = &tv->s[i];
		for (int k = 0; k < tsl->objs.len; ++k) {
			struct tobject *obj = vec_get(&tsl->objs, k);
			struct layout_event l = {
				.time = obj->time,
				.idx = k
			};
			vec_append(&la, &l);
		}
		calendar_layout(la.d, la.len);
		tsl->max_overlap = 0;
		for (int k = 0; k < la.len; ++k) {
			struct layout_event *l = vec_get(&la, k);
			struct tobject *obj = vec_get(&tsl->objs, l->idx);
			obj->max_n = l->max_n;
			obj->col = l->col;
			if (obj->max_n > tsl->max_overlap) tsl->max_overlap = obj->max_n;
		}
		vec_clear(&la);
	}
	vec_free(&la);

	/* extend to full range if there is no content */
	if (tv->min_content > tv->max_len) tv->min_content = 0;
	if (tv->max_content < 0) tv->max_content = tv->max_len;
}
