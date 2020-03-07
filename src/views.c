#include <stdlib.h>

#include "views.h"
#include "pango.h"
#include "algo.h"
#undef assert
#include "util.h"

void destruct_tslice(struct tslice *tsl) {
    free(tsl->lines.s);
    tsl->lines.s = NULL;
    tsl->lines.n = -1;

    free(tsl->objs);
    tsl->objs = NULL;
    tsl->n = -1;

    free(tsl->header_label);
    tsl->header_label = NULL;
}
void destruct_tview(struct tview *tv) {
    for (int i = 0; i < tv->n; ++i) {
        destruct_tslice(&tv->s[i]);
    }
    free(tv->s);
    *tv = (struct tview){ .s = NULL, .n = -1 };
}
static void init_tslice(struct tslice *tsl) {
    tsl->objs = NULL;
    tsl->n = 0;
    tsl->max = -1;
}
static void alloc_n_slices(struct tview *tv, int n) {
    tv->s = malloc_check(sizeof(struct tslice) * n);
    tv->n = n;
    for (int i = 0; i < n; ++i) {
        init_tslice(&tv->s[i]);
    }
}
static void alloc_lines(struct tslice *tsl, int n) {
    tsl->lines = (struct tslice_lines){
        .s = malloc_check(sizeof(ts) * n),
        .n = n
    };
}
void init_tview_slices(struct tview *tv, int max) {
    tv->max = max;
    for (int i = 0; i < tv->n; ++i) {
        tv->s[i].objs = malloc_check(sizeof(struct tobject) * max);
        tv->s[i].max = max;
    }
}

void init_tview_range(struct tview *tv, struct tview_spec *spec) {
    alloc_n_slices(tv, 1);

    struct tslice *tsl = &tv->s[0];
    tsl->ran.fr = spec->base;
    tsl->ran.to = spec->to;
    tsl->header_label = NULL;

    tsl->lines.n = 0;
    tsl->lines.s = NULL;

    tv->max_len = tsl->ran.to - tsl->ran.fr;
}

void init_tview(struct tview *tv, struct tview_spec *spec) {
    if (spec->type == TVIEW_RANGE) assert(spec->n == 1, "wrong spec");
    alloc_n_slices(tv, spec->n);
    tv->max_len = -1;
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
            assert(false, "wrong tview type");
            break;
        }
        ts len = r->to - r->fr;
        tv->max_len = max(tv->max_len, len);
    }
    spec->to = tv->s[tv->n - 1].ran.to;
    tv->min_content = tv->max_len + 1; tv->max_content = -1;
}
bool tview_try_put(struct tview *tv, struct tobject obj) {
    bool inserted = false;
    for (int i = 0; i < tv->n; ++i) {
        struct tslice *tsl = &tv->s[i];
        if (ts_ran_overlap(tsl->ran, obj.time)) {
            assert(tsl->n < tsl->max, "tslice reached max limit");
            tsl->objs[tsl->n++] = obj;
            struct ts_ran ran = {
                obj.time.fr - tsl->ran.fr, obj.time.to - tsl->ran.fr };
            if (ran.fr < tv->min_content)
                tv->min_content = max(ran.fr, 0);
            if (ran.to > tv->max_content)
                tv->max_content = min(ran.to, tsl->ran.to - tsl->ran.fr);
            inserted = true;
        }
    }
    return inserted;
}
void tview_update_layout(struct tview *tv) {
    struct layout_event *la =
        malloc_check(sizeof(struct layout_event) * tv->max);
    for (int i = 0; i < tv->n; ++i) {
        struct tslice *tsl = &tv->s[i];
        assert(tsl->n <= tv->max, "too small tview max");
        for (int k = 0; k < tsl->n; ++k) {
            struct tobject *obj = &tsl->objs[k];
            la[k] = (struct layout_event){
                .start = (long long int)obj->time.fr, //TODO: these casts
                .end = (long long int)obj->time.to,
                .idx = k
            };
        }
        calendar_layout(la, tsl->n);
        tsl->max_overlap = 0;
        for (int k = 0; k < tsl->n; ++k) {
            struct tobject *obj = &tsl->objs[la[k].idx];
            obj->max_n = la[k].max_n;
            obj->col = la[k].col;
            if (obj->max_n > tsl->max_overlap) tsl->max_overlap = obj->max_n;
        }
    }
    free(la);

    /* extend to full range if there is no content */
    if (tv->min_content > tv->max_len) tv->min_content = 0;
    if (tv->max_content < 0) tv->max_content = tv->max_len;
}
