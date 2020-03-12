#include <math.h>

#include "render.h"
#include "pango.h"
#include "application.h"
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
    union {
        struct { double x, y; };
        float v[2];
    };
} v2;
typedef struct {
    double x, y, w, h;
} fbox;

static fbox fbox_slice(fbox b, int n, int i, bool dir) {
    fbox nb;
    if (dir) {
        nb.x = b.x + b.w / n * i;
        nb.y = b.y;
        nb.w = b.w / n;
        nb.h = b.h;
    } else {
        nb.x = b.x;
        nb.y = b.y + b.h / n * i;
        nb.w = b.w;
        nb.h = b.h / n;
    }
    return nb;
}

static bool same_day(struct simple_date a, struct simple_date b) {
    return
        a.year == b.year &&
        a.month == b.month &&
        a.day == b.day;
}

static char* natural_date_format(const struct date *d) {
    time_t timet_now = state.now;
    struct simple_date now =
        simple_date_from_timet(timet_now, state.zone->impl);
    timet_adjust_days(&timet_now, state.zone->impl, -1);
    struct simple_date yesterday =
        simple_date_from_timet(timet_now, state.zone->impl);
    struct simple_date t =
        simple_date_from_timet(d->timestamp, state.zone->impl);
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

static void draw_text(cairo_t *cr, int x, int y, char *text) {
    state.tr->p.width = -1; state.tr->p.height = -1;
    text_get_size(cr, state.tr, text);
    cairo_move_to(cr, x - state.tr->p.width / 2, y - state.tr->p.height / 2);
    text_print_own(cr, state.tr, text);
}

static void text_print_vert_center(cairo_t *cr, box b, char *text) {
    state.tr->p.width = b.w; state.tr->p.height = b.h;
    state.tr->p.wrap_char = false;
    text_get_size(cr, state.tr, text);
    cairo_move_to(cr, b.x,
                      b.y + b.h / 2 - state.tr->p.height / 2);
    text_print_own(cr, state.tr, text);
    state.tr->p.wrap_char = true;
}

static void text_print_center(cairo_t *cr, box b, char *text) {
    state.tr->p.width = b.w; state.tr->p.height = b.h;
    state.tr->p.wrap_char = false;
    text_get_size(cr, state.tr, text);
    cairo_move_to(cr, b.x + b.w / 2 - state.tr->p.width / 2,
                      b.y + b.h / 2 - state.tr->p.height / 2);
    text_print_own(cr, state.tr, text);
    state.tr->p.wrap_char = true;
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
static void render_tobject_todo(cairo_t *cr, struct tobject *obj, fbox b,
        struct tview_params p) {
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

    /* fill base rect */
    uint32_t color = 0xAA00AA00;
    cairo_set_source_argb(cr, color);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    /* draw label */
    cairo_set_source_argb(cr, 0xFF000000);
    cairo_move_to(cr, x, y);
    state.tr->p.width = w; state.tr->p.height = h;
    char *text = text_format("TODO: %s", obj->td->summary);
    text_print_free(cr, state.tr, text);
}
static void render_tobject_event(cairo_t *cr, struct tobject *obj, fbox b,
        struct tview_params p) {
    assert(obj->type == TOBJECT_EVENT, "object not event");
    struct event *ev = obj->ev;
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

    /* calculate color stuff */
    uint32_t color = ev->color;
    if (!color) color = 0xFF20D0D0;
    if (ev->status == ICAL_STATUS_TENTATIVE ||
            ev->status == ICAL_STATUS_CANCELLED) {
        color = (color & 0x00FFFFFF) | 0x30000000;
    }
    double lightness = (color & 0xFF) + ((color >> 8) & 0xFF)
        + ((color >> 16) & 0xFF);
    lightness /= 255.0;

    /* fill base rect */
    cairo_set_source_argb(cr, color);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    /* draw various labels */
    if (state.show_private_events || ev->clas != ICAL_CLASS_PRIVATE) {
        bool light = lightness < 0.9 ? true : false;
        uint32_t fg = light ? 0xFFFFFFFF : 0xFF000000;
        cairo_set_source_argb(cr, fg);

        if (ev->location) {
            state.tr->p.width = w; state.tr->p.height = -1;
            text_get_size(cr, state.tr, ev->location);
        }
        int loc_h = ev->location ? min(h / 2, state.tr->p.height) : 0;

        cairo_move_to(cr, x, y);
        state.tr->p.width = w; state.tr->p.height = h - loc_h;
        struct simple_date local_start =
                simple_date_from_ts(obj->aev->time.fr, state.zone->impl);
        struct simple_date local_end =
                simple_date_from_ts(obj->aev->time.to, state.zone->impl);
        char *text = text_format("%02d:%02d-%02d:%02d %s",
                local_start.hour, local_start.minute,
                local_end.hour, local_end.minute,
                ev->summary);
        text_print_free(cr, state.tr, text);

        if (ev->location) {
            cairo_set_source_argb(cr, light ? 0xFFA0A0A0 : 0xFF606060);
            cairo_move_to(cr, x, y+h-loc_h);

            state.tr->p.width = w; state.tr->p.height = loc_h;
            text_print_own(cr, state.tr, ev->location);
        }
    }

    /* draw keycode tags */
    struct active_event *aev = obj->aev;
    if (state.keystate == KEYSTATE_SELECT) {
        uint32_t c = (color ^ 0x00FFFFFF) | 0xFF000000;
        cairo_set_source_argb(cr, c);
        char *text = aev->tag.code;
        state.tr->p.scale = 3.0;
        state.tr->p.width = w; state.tr->p.height = -1;
        text_get_size(cr, state.tr, text);
        cairo_move_to(cr, x + w/2 - state.tr->p.width/2,
                          y + h/2 - state.tr->p.height/2);
        text_print_own(cr, state.tr, aev->tag.code);
        state.tr->p.scale = 1.0;
    }
}
static void render_tslice(cairo_t *cr, struct tslice *tsl, ts len, fbox b,
        struct tview_params p) {
    struct ts_ran ran = tsl->ran;
    for (int i = 0; i < tsl->n; ++i) {
        struct tobject *obj = &tsl->objs[i];
        struct ts_ran time = obj->time;
        time.fr = max(time.fr, ran.fr);
        time.to = min(time.to, ran.to);
        double pa = (time.fr - ran.fr - p.view_ran.fr) / (double)len;
        double pb = (time.to - ran.fr - p.view_ran.fr) / (double)len;
        double pl = pb - pa;
        fbox nb;
        if (p.dir) {
            nb.x = b.x + b.w / obj->max_n * obj->col;
            nb.y = b.y + b.h * pa;
            nb.w = b.w / obj->max_n;
            nb.h = b.h * pl;
        } else {
            nb.x = b.x + b.w * pa;
            nb.y = b.y + b.h / obj->max_n * obj->col;
            nb.w = b.w * pl;
            nb.h = b.h / obj->max_n;
        }
        if (obj->type == TOBJECT_EVENT) {
            render_tobject_event(cr, obj, nb, p);
        } else if (obj->type == TOBJECT_TODO) {
            render_tobject_todo(cr, obj, nb, p);
        } else {
            assert(false, "unknown tobject type");
        }
    }

    /* draw time marker red line */
    ts now = ts_now();
    if (ts_ran_in(ran, now)) {
        double pa = (now - ran.fr - p.view_ran.fr) / (double)len;
        fbox nb;
        if (p.dir) {
            nb.x = b.x;
            nb.y = b.y + b.h * pa;
            nb.w = b.w;
            nb.h = 0;
        } else {
            nb.x = b.x + b.w * pa;
            nb.y = b.y;
            nb.w = 0;
            nb.h = b.h;
        }
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, nb.x, nb.y);
        cairo_line_to(cr, nb.x + nb.w, nb.y + nb.h);
        cairo_set_source_rgba(cr, 255, 0, 0, 255);
        cairo_stroke(cr);
    }
}
static void render_tview(cairo_t *cr, struct tview *tv, fbox b,
        struct tview_params p) {
    ts len = p.view_ran.to - p.view_ran.fr;

    /* draw lines */
    cairo_set_line_width(cr, 1);
    for (int l = 0;; ++l) {
        bool any = false;
        for (int i = 0; i < tv->n; ++i) {
            struct tslice *tsl = &tv->s[i];
            if (tsl->lines.n <= l) continue;
            any = true;
            ts t = tsl->lines.s[l];
            double pa = (t - tsl->ran.fr - p.view_ran.fr) / (double)len;
            if (pa < 0 || pa > 1) continue;
            fbox nb = fbox_slice(b, tv->n, i, p.dir);
            double x1, x2, y1, y2;
            if (p.dir) {
                x1 = nb.x; x2 = nb.x + nb.w;
                y1 = y2 = round(nb.y + pa * nb.h + .5) - .5;
            } else {
                x1 = x2 = round(nb.x + pa * nb.w + .5) - .5;
                y1 = nb.y; y2 = nb.y + nb.w;
            }
            cairo_set_source_argb(cr, 0xFF000000);
            cairo_move_to(cr, x1, y1);
            cairo_line_to(cr, x2, y2);
            cairo_stroke(cr);
        }
        if (!any) break;
    }

    /* draw slices */
    for (int i = 0; i < tv->n; ++i) {
        struct tslice *tsl = &tv->s[i];
        fbox nb = fbox_slice(b, tv->n, i, p.dir);
        render_tslice(cr, tsl, len, nb, p);

        /* draw lines between slices */
        cairo_set_source_argb(cr, 0xFF000000);
        cairo_set_line_width(cr, p.sep_line);
        if (p.dir) {
            cairo_move_to(cr, b.x + b.w / tv->n * i, b.y);
            cairo_line_to(cr, b.x + b.w / tv->n * i, b.y + b.h);
        } else {
            cairo_move_to(cr, b.x, b.y + b.h / tv->n * i);
            cairo_line_to(cr, b.x + b.w, b.y + b.h / tv->n * i);
        }
        cairo_stroke(cr);
    }
}
static void render_tview_header(cairo_t *cr, struct tview *tv, fbox b,
        struct tview_params p) {
    cairo_set_source_argb(cr, 0xFF000000);
    cairo_set_line_width(cr, p.sep_line);
    for (int i = 0; i < tv->n; ++i) {
        fbox nb = fbox_slice(b, tv->n, i, p.dir);
        double x1, x2, y1, y2;
        if (p.dir) {
            x1 = x2 = b.x + b.w / tv->n * i;
            y1 = b.y; y2 = b.y + b.h;
        } else {
            x1 = b.x; x2 = b.x + b.w;
            y1 = y2 = b.y + b.h / tv->n * i;
        }
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);

        state.tr->p.scale = 1.5;
        text_print_center(cr, (box){ nb.x, nb.y, nb.w, nb.h },
            tv->s[i].header_label);
        state.tr->p.scale = 1.0;
    }
}
static void render_tview_strip(cairo_t *cr, struct tview *tv, fbox b,
        struct tview_params p) {
    cairo_set_source_argb(cr, 0xFF000000);
    ts len = p.view_ran.to - p.view_ran.fr;
    char buf[64];
    for (int l = 0;; ++l) {
        bool any = false;
        for (int i = 0; i == 0; ++i) {
            struct tslice *tsl = &tv->s[i];
            if (tsl->lines.n <= l) continue;
            any = true;

            double x, y;
            ts t = tsl->lines.s[l] - tsl->ran.fr - p.view_ran.fr;
            double pa = t / (double)len;
            if (pa < 0 || pa > 1) continue;
            if (p.dir) {
                x = b.x + b.w / 2;
                y = b.y + b.h * pa;
            } else {
                x = b.x + b.w * pa;
                y = b.y + b.h / 2;
            }
            snprintf(buf, 64, "%02d", l);
            draw_text(cr, x, y, buf);
        }
        if (!any) break;
    }
}

static void render_sidebar(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);
    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    int h = 0;
    int pad = 6;
    for (int i = 0; i < state.n_cal; i++) {
        bool vis = state.cal_info[i].visible;
        uint32_t cal_color = state.cal_info[i].color;
        const char *name = state.cal[i].name;
        if (!state.show_private_events && state.cal[i].priv) continue;

        char *text;
        if (state.interactive) {
            text = text_format("%i: %s", i + 1, name);
        } else {
            text = text_format("%s", name);
        }
        state.tr->p.width = b.w; state.tr->p.height = -1;
        text_get_size(cr, state.tr, text);
        int height = state.tr->p.height;

        cairo_set_source_argb(cr, vis ? cal_color : 0xFFFFFFFF);
        cairo_rectangle(cr, 0, h, b.w, height + pad);
        cairo_fill(cr);

        cairo_set_source_argb(cr, 0xFF000000);
        cairo_move_to(cr, 0, h + pad / 2);
        state.tr->p.width = b.w; state.tr->p.height = height;
        text_print_free(cr, state.tr, text);

        h += height + pad;
        cairo_move_to(cr, 0, h);
        cairo_line_to(cr, b.w, h);
        cairo_stroke(cr);
    }

    if (state.interactive) {
        cairo_set_source_argb(cr, 0xFF000000);
        cairo_move_to(cr, 0, h += 5);
        state.tr->p.width = b.w; state.tr->p.height = -1;
        const char *str = state.show_private_events ?
            "show private" : "hide private";
        text_get_size(cr, state.tr, str);
        h += state.tr->p.height;
        text_print_own(cr, state.tr, str);

        cairo_set_source_rgba(cr, .3, .3, .3, 1);
        cairo_move_to(cr, 0, h += 5);
        state.tr->p.width = b.w; state.tr->p.height = -1;
        text_get_size(cr, state.tr, usage);
        h += state.tr->p.height;
        text_print_own(cr, state.tr, usage);
    }

    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    cairo_move_to(cr, b.w, 0);
    cairo_line_to(cr, b.w, b.h);
    cairo_stroke(cr);
    cairo_identity_matrix(cr);
}

/* return height */
static int render_todo_item(cairo_t *cr, struct todo_tag *tag, box b) {
    cairo_translate(cr, b.x, b.y);

    /* prepare all the info we need */
    struct todo *td = tag->td;
    bool overdue = td->due.timestamp != -1 && td->due.timestamp < state.now,
         inprocess = td->status == ICAL_STATUS_INPROCESS;
    bool not_started = td->start.timestamp != -1
        && td->start.timestamp > state.now;
    int hpad = 5;

    int w = b.w - 80;
    int n = 1;
    if (td->desc) n += 1;

    /* calculate height */
    if (td->summary) {
        state.tr->p.width = w/n - 2*hpad; state.tr->p.height = -1;
        text_get_size(cr, state.tr, td->summary);
        b.h = max(b.h, state.tr->p.height);
    }
    if (td->desc) {
        state.tr->p.width = w/n - 2*hpad; state.tr->p.height = -1;
        text_get_size(cr, state.tr, td->desc);
        b.h = max(b.h, state.tr->p.height);
    }

    if (overdue) {
        cairo_set_source_argb(cr, 0xFFD05050);
        cairo_rectangle(cr, b.w - 80, 0, 80, b.h);
        cairo_fill(cr);
    }
    if (inprocess) {
        cairo_set_source_argb(cr, 0xFF88FF88);
        cairo_rectangle(cr, 0, 0, b.w - 80, b.h);
        cairo_fill(cr);
    }
    cairo_set_source_argb(cr, not_started ? 0xFF888888 : 0xFF000000);

    /* draw text in the slots */
    char *text = NULL;
    if (td->due.timestamp != -1) {
        free(text);
        text = natural_date_format(&td->due);
    }
    if (td->estimated_duration != -1) {
        char *text_dur = format_dur(td->estimated_duration);
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
        text_print_center(cr, (box){ b.w - 80, 0, 80, b.h }, text);
        free(text);
    }
    if (td->summary) {
        text_print_vert_center(cr, (box){ w*0/n + hpad, 0, w/n - 2*hpad, b.h },
            td->summary);
    }
    if (td->desc) {
        text_print_vert_center(cr, (box){ w*1/n + hpad, 0, w/n - 2*hpad, b.h },
            td->desc);
    }

    /* draw slot separators */
    cairo_set_line_width(cr, 1);
    for (int i = 1; i <= n; ++i) {
        cairo_move_to(cr, w*i/n +.5, 0);
        cairo_line_to(cr, w*i/n +.5, b.h);
        cairo_stroke(cr);
    }

    /* draw key tag code */
    if (state.keystate == KEYSTATE_SELECT) {
        uint32_t c = 0xFFFF00FF;
        cairo_set_source_argb(cr, c);
        char *text = tag->code;
        state.tr->p.scale = 3.0;
        state.tr->p.width = b.w; state.tr->p.height = -1;
        text_get_size(cr, state.tr, text);
        cairo_move_to(cr, b.w/2 - state.tr->p.width/2,
                          b.h/2 - state.tr->p.height/2);
        text_print_own(cr, state.tr, text);
        state.tr->p.scale = 1.0;
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

static void render_todo_list(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);

    /* draw separator on top */
    cairo_set_source_argb(cr, 0xFF000000);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, b.w, 0);
    cairo_stroke(cr);

    int y = 0;
    for (int i = 0; i < state.active_todo_n; ++i) {
        y += render_todo_item(cr, &state.active_todos_tag[i], (box){0,y,b.w,40});
        if (y > b.h) break;
    }
    cairo_identity_matrix(cr);
}

bool render_application(void *ud, cairo_t *cr) {
    int w, h;
    state.backend->vptr->get_window_size(state.backend, &w, &h);
    time_t now = time(NULL);
    if (state.now != now) {
        state.now = now;
        state.dirty = true;
    }
    if (state.window_width != w ||
            state.window_height != h) {
        state.window_width = w;
        state.window_height = h;
        state.dirty = true;
    }
    if (!state.dirty) return false;
    static int frame_counter = 0;
    ++frame_counter;

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    int time_strip_w = 30;
    int sidebar_w = 120;
    int header_h = 60;
    assert(state.top_tview.n == 1, "top_tview wrong slices");
    int top_h = 50 * state.top_tview.s[0].max_overlap;

    const char *view_name = "";
    switch (state.main_view) {
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
        struct tview_params params = {
            .dir = true,
            .pad = 2,
            .sep_line = 2,
        };

        if (state.tview_type == TVIEW_DAYS) {
            params.view_ran = (struct ts_ran){
                state.tview.min_content, state.tview.max_content };
        } else {
            params.view_ran = (struct ts_ran){ 0, state.tview.max_len };
        }
        render_tview(cr, &state.tview, main_box, params);
        render_tview_header(cr, &state.tview, header_box, params);
        if (state.tview_type == TVIEW_DAYS) {
            render_tview_strip(cr, &state.tview, time_strip_box, params);
        }

        params.dir = false;
        params.view_ran = (struct ts_ran){ 0, state.top_tview.max_len };
        render_tview(cr, &state.top_tview, top_box, params);
        view_name = "calendar";
        break;
    case VIEW_TODO:
        render_todo_list(cr,
            (box){ sidebar_w, header_h, w-sidebar_w, h-header_h });
        view_name = "todo";
    }
    render_sidebar(cr, (box){ 0, header_h, sidebar_w, h-header_h });

    if (state.n_cal > 0 && state.interactive) {
        cairo_move_to(cr, 0, 0);

        struct tm t = timet_to_tm_with_zone(state.now, state.zone->impl);
        char *text = text_format(
                "%s\rframe %d\r%02d:%02d:%02d\rmode: %s",
                get_timezone_desc(state.zone),
                frame_counter,
                t.tm_hour, t.tm_min, t.tm_sec,
                view_name);
        state.tr->p.width = -1 /* sidebar_w */; state.tr->p.height = header_h;
        state.tr->p.scale = 0.9;
        text_print_free(cr, state.tr, text);
        state.tr->p.scale = 1.0;
    }

    state.dirty = false;
    return true;
}
