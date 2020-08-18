#include <stdlib.h>

#include "views.h"
#include "pango.h"
#include "algo.h"
#include "core.h"

enum hlevel { YEAR = 0, MONTH = 1, DAY = 2, HOUR = 3, N_LEVELS };
struct item {
	struct ts_ran ran;
	union {
		struct {
			int n;
			int subs[31];
		};
		bool hour_leap;
	};
};
struct slicing {
	struct cal_timezone *zone;
	struct vec items[N_LEVELS]; /* vec<struct item> */
};

struct slicing *slicing_create(struct cal_timezone *zone) {
	struct slicing *s = malloc_check(sizeof(struct slicing));
	s->zone = zone;
	for (int i = 0; i < N_LEVELS; ++i) {
		s->items[i] = vec_new_empty(sizeof(struct item));
	}
	return s;
}
void slicing_destroy(struct slicing *s) {
	for (int i = 0; i < N_LEVELS; ++i) {
		vec_free(&s->items[i]);
	}
}
int get_or_create(struct slicing *s, enum hlevel lev, struct ts_ran ran) {
	struct vec *v = &s->items[lev];
	for (int i = 0; i < v->len; ++i) {
		struct item *item = vec_get(v, i);
		if (item->ran.fr == ran.fr) {
			asrt(item->ran.to == ran.to, "[slicing] ran.to does not match");
			return i;
		}
	}
	struct item item = { .ran = ran, .n = -1 };
	return vec_append(v, &item);
}
struct item *get_item(struct slicing *s, enum hlevel lev, int id) {
	return vec_get(&s->items[lev], id);
}

struct iter {
	enum hlevel type;
	struct ts_ran ran;
	void *env;
	void (*f)(void *env, struct ts_ran ran, struct simple_date label);
	struct simple_date label;
};
static void iter_items(struct slicing *s, struct iter *iter,
		enum hlevel lev, int id) {
	asrt(lev <= iter->type, "[slicing] too deep");

	struct item *item = get_item(s, lev, id);
	if (!ts_ran_overlap(iter->ran, item->ran)) return;
	if (lev == iter->type) {
		iter->f(iter->env, item->ran, iter->label);
		return;
	}

	asrt(lev < HOUR, "[slicing] too deep iter type");
	int adj = (lev + 1 == MONTH || lev + 1 == DAY) ? 1 : 0;

	if (item->n == -1) {
		/* b: the start of this range */
		struct simple_date b = simple_date_from_ts(item->ran.fr, s->zone);
		switch (lev) { /* all of these FALLTHROUGH */
		default: asrt(false, "[slicing] bad lev");
		case YEAR: asrt(b.t[1] == 1, "[slicing] year");
		case MONTH: asrt(b.t[2] == 1, "[slicing] month");
		case DAY: asrt(b.t[3] == 0, "[slicing] day");
		case HOUR: asrt(b.t[4] == 0 && b.t[5] == 0, "[slicing] hour");
		}

		if (lev == YEAR) item->n = 12;
		else if (lev == MONTH) item->n = simple_date_days_in_month(b);
		else if (lev == DAY) item->n = -1; /* could be a 23 or 25 hour day */
		asrt(item->n <= 31, "");
		for (int i = 0;; ++i) {
			struct simple_date bi = b;
			struct ts_ran r; /* the range of this sub */

			/* calculate r.fr */
			bi.t[lev + 1] = i + adj;
			simple_date_normalize(&bi);
			r.fr = simple_date_to_ts(bi, s->zone);

			/* check when we would step into the next item */
			if (bi.t[lev] != b.t[lev]) {
				if (item->n != -1) {
					asrt(item->n == i, "item n mismatch");
				} else {
					asrt(lev == DAY, "");
					asrt(23 <= i && i <= 25, "bad day item n");
					item->n = i;
				}
				break;
			}

			/* calculate r.to */
			bi.t[lev + 1] = i + adj + 1;
			simple_date_normalize(&bi);
			r.to = simple_date_to_ts(bi, s->zone);

			if (r.fr == r.to) {
				/* invalid range, we got a zero length hour brought to you by
				 * our dear friend DST */
				asrt(lev == DAY, "");
				++adj;
				--i;
				continue;
			}

			/* create sub */
			item->subs[i] = get_or_create(s, lev + 1, r);

			/* assign label & recurse */
			iter->label.t[lev + 1] = i + adj;
			iter_items(s, iter, lev + 1, item->subs[i]);
		}
	} else {
		for (int i = 0; i < item->n; ++i) {
			iter->label.t[lev + 1] = i + adj;
			iter_items(s, iter, lev + 1, item->subs[i]);
		}
	}
}
void slicing_iter_items(struct slicing *s, void *env,
		void (*f)(void *env, struct ts_ran ran, struct simple_date label),
		enum slicing_type type, struct ts_ran ran) {
	struct iter iter = { .env = env, .f = f, .ran = ran };
	switch (type) {
	case SLICING_YEAR: iter.type = YEAR; break;
	case SLICING_MONTH: iter.type = MONTH; break;
	case SLICING_DAY: iter.type = DAY; break;
	case SLICING_HOUR: iter.type = HOUR; break;
	}

	struct simple_date b = { .month = 1, .day = 1 };
	b.year = simple_date_from_ts(ran.fr, s->zone).year;
	while (1) {
		struct ts_ran r;
		r.fr = simple_date_to_ts(b, s->zone);
		if (r.fr >= ran.to) break;
		iter.label.t[YEAR] = b.year;
		++b.year;
		r.to = simple_date_to_ts(b, s->zone);
		int id = get_or_create(s, YEAR, r);
		iter_items(s, &iter, YEAR, id);
	}
}

int slicing_test_get_total_len(struct slicing *s) {
	int n = 0;
	for (int i = 0; i < N_LEVELS; ++i) n += s->items[i].len;
	return n;
}

void tobject_layout(struct vec *tobjs, int *max_overlap) {
	struct vec la = vec_new_empty(sizeof(struct layout_event));
	for (int k = 0; k < tobjs->len; ++k) {
		struct tobject *obj = vec_get(tobjs, k);
		struct layout_event l = {
			.time = obj->time,
			.idx = k
		};
		vec_append(&la, &l);
	}
	calendar_layout(la.d, la.len);
	if (max_overlap) *max_overlap = 0;
	for (int k = 0; k < la.len; ++k) {
		struct layout_event *l = vec_get(&la, k);
		struct tobject *obj = vec_get(tobjs, l->idx);
		obj->max_n = l->max_n;
		obj->col = l->col;
		if (max_overlap) {
			if (obj->max_n > *max_overlap) *max_overlap = obj->max_n;
		}
	}
	vec_free(&la);
}
