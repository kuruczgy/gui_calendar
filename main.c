#include <stdio.h>
#include <cairo.h>
#include <sys/types.h>
#include <unistd.h>
#include "util.h"
#include "calendar.h"
#include "pango.h"
#include "backend/gui.h"
#include "keyboard.h"

const char *usage =
    "Usage:\r"
    " h/l: next/prev\r"
    " t: today\r"
    " n: new event\r"
    " e: edit event\r"
    " v: cycle view modes\r"
    " up/down: move 1 hour up/down\r"
    " +/-: inc./dec. vertical scale\r"
    " r: reload calendars\r";

struct calendar_info {
    bool visible;
};

struct event_tag {
    struct event *ev;
    char code[33];
};

typedef struct {
    int from, to;
} range;

struct state {
    struct text_renderer *tr;

    struct calendar cal[16];
    struct calendar_info cal_info[16];
    struct event **active_events;
    struct event_tag *active_events_tag;
    struct layout_event **layout_events;
    int *layout_event_n;
    int active_n;
    int n_cal;

    struct todo **active_todos;
    int active_todo_n;

    struct cal_timezone *zone;
    time_t base;
    int view_days;
    range hours_view_events;
    range hours_view;
    range hours_view_manual;
    time_t now;

    int window_width, window_height;
    struct subprocess_handle *sp;
    const char **editor;
    bool mode_select;
    char mode_select_code[33];
    int mode_select_code_n;
    int mode_select_len;

    bool show_private_events;

    enum {
        VIEW_CALENDAR,
        VIEW_TODO
    } main_view;

    bool dirty;
};

static struct state state;

typedef struct {
    int x, y, w, h;
} box;

static bool interval_overlap(int a1, int a2, int b1, int b2) {
    return a1 <= b2 && a2 >= b1;
}

static void draw_text(cairo_t *cr, int x, int y, char *text) {
    state.tr->p.width = -1; state.tr->p.height = -1;
    text_get_size(cr, state.tr, text);
    cairo_move_to(cr, x - state.tr->p.width / 2, y - state.tr->p.height / 2);
    text_print_own(cr, state.tr, text);
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

static void draw_text_scale(cairo_t *cr, int x, int y, char *text, double scale) {
    state.tr->p.scale = scale;
    draw_text(cr, x, y, text);
    state.tr->p.scale = 1.0;
}

static int day_sec(struct tm t) {
    return 3600 * t.tm_hour + 60 * t.tm_min + t.tm_sec;
}

static bool different_day(struct tm a, struct tm b) {
    return a.tm_mday != b.tm_mday || a.tm_mon != b.tm_mon || a.tm_year !=
        b.tm_year;
}

void disable_mode_select() {
    state.mode_select = false;
}

void switch_mode_select() {
    state.mode_select_code_n = 0;
    if (state.mode_select) return;
    state.mode_select = true;
    struct key_gen g;
    key_gen_init(state.active_n, &g);
    state.mode_select_len = g.k;
    for (int i = 0; i < state.active_n; ++i) {
        const char *code = key_gen_get(&g);
        assert(code, "not enough codes");
        strcpy(state.active_events_tag[i].code, code);
    }
}

void mode_select_append_sym(char sym) {
    state.mode_select_code[state.mode_select_code_n++] = sym;
    if (state.mode_select_code_n >= state.mode_select_len) {
        disable_mode_select();
        state.dirty = true;
        for (int i = 0; i < state.active_n; ++i) {
            if (strncmp(
                    state.active_events_tag[i].code, state.mode_select_code,
                    state.mode_select_code_n) == 0) {
                fprintf(stderr, "selected event: %s\n",
                        state.active_events[i]->summary);
                if (!state.sp) {
                    state.sp = subprocess_new_event_input(state.editor[0],
                            state.editor + 1, state.active_events[i]);
                }
                break;
            }
        }
    }
}

struct event_filterer {
    struct event **list;
    int n;
    time_t from, to;
    bool show_priv;
};
void init_event_filterer(struct event_filterer *f, struct event **list) {
    f->list = list;
    f->n = 0;
    f->from = -1;
    f->to = -1;
    f->show_priv = false;
}
static int filter_events(void *f_p, void *e_p) {
    struct event_filterer *f = f_p;
    struct event *e = e_p;
    do {
        if (f->from != -1 && !interval_overlap(f->from, f->to,
                e->start.timestamp, e->end.timestamp)) continue;
        if (!f->show_priv && e->clas == ICAL_CLASS_PRIVATE) continue;

        f->list[f->n++] = e;
    } while (e = e->recur);
    return MAP_OK;
}

static void discard_temp_structures() {
    // discard existing structures
    if (state.active_events) {
        free(state.active_events);
        free(state.active_events_tag);
    }
    if (state.layout_event_n) {
        for (int d = 0; state.layout_event_n[d] >= 0; d++)
            free(state.layout_events[d]);
        free(state.layout_events);
        free(state.layout_event_n);
    }
}

static void update_active_events() {
    discard_temp_structures();

    // clear any modes that depend on current event structures
    disable_mode_select();

    int n_all_events = 0;
    for (int i = 0; i < state.n_cal; i++)
        n_all_events += state.cal[i].num_events;
        //hashmap_length(state.cal[i].events);
    struct event **active = malloc(sizeof(struct event*) * n_all_events);
    struct event_filterer filterer;

    init_event_filterer(&filterer, active);
    filterer.from = state.base,
    filterer.to = state.base + state.view_days * 3600 * 24;
    filterer.show_priv = true; // state.show_private_events;

    for (int i = 0; i < state.n_cal; i++) {
        if (state.cal_info[i].visible) {
            hashmap_iterate(state.cal[i].events, filter_events, &filterer);
        }
    }
    int active_n = filterer.n;
    state.active_events_tag = malloc(sizeof(struct event_tag) * active_n);

    struct layout_event **layout_events =
        malloc(sizeof(struct layout_event*) * state.view_days);
    int *layout_event_n = malloc(sizeof(int) * (state.view_days + 1));
    layout_event_n[state.view_days] = -1;
    for (int d = 0; d < state.view_days; d++) {
        // TODO: what if day not 24h long?
        time_t day_base = state.base + 3600 * 24 * d;
        int l = 0;
        struct layout_event *la = layout_events[d]
            = malloc(sizeof(struct layout_event) * active_n);
        for (int k = 0; k < active_n; k++) {
            struct event *ev = active[k];
            if ( ! interval_overlap(
                    day_base, day_base + 3600 * 24,
                    ev->start.timestamp, ev->end.timestamp)
                ) continue;
            int start_sec = max(0, ev->start.timestamp - day_base);
            int end_sec = min(3600 * 24, ev->end.timestamp - day_base);
            assert(l < active_n, "too many events");
            la[l++] = (struct layout_event){
                .start = start_sec,
                .end = end_sec,
                .idx = k
            };
        }
        calendar_layout(la, l);
        layout_event_n[d] = l;
    }

    state.active_events = active;
    state.active_n = active_n;
    state.layout_events = layout_events;
    state.layout_event_n = layout_event_n;
}

static int count_active_todos(void *f_p, void *t_p) {
    struct todo *td = t_p;
    int *cnt = f_p;
    if (td->is_active &&
        (state.show_private_events || td->clas != ICAL_CLASS_PRIVATE)) {
        if (state.active_todos) state.active_todos[*cnt] = td;
        (*cnt)++;
    }
    return MAP_OK;
}

static void update_active_todos() {
    if (state.active_todos) {
        free(state.active_todos);
        state.active_todos = NULL;
    }
    int n = 0;
    for (int i = 0; i < state.n_cal; ++i)
        hashmap_iterate(state.cal[i].todos, count_active_todos, &n);
    state.active_todos = malloc(sizeof(struct todo*) * n);
    state.active_todo_n = 0;
    for (int i = 0; i < state.n_cal; ++i)
        hashmap_iterate(state.cal[i].todos, count_active_todos,
                &state.active_todo_n);
    assert(n == state.active_todo_n, "todo count mismatch");
    priority_sort_todos(state.active_todos, state.active_todo_n);
}

static void update_actual_fit() { 
    if (state.hours_view_manual.from != -1) {
        state.hours_view = state.hours_view_manual;
    } else {
        int min_sec = state.hours_view_events.from * 3600,
            max_sec = state.hours_view_events.to * 3600;
        struct tm t = timet_to_tm_with_zone(state.now, state.zone);
        int now_sec = day_sec(t);
        time_t diff = time(NULL) - state.base;
        if (!(diff > state.view_days * 3600 * 24 || diff < 0)) {
            // fprintf(stderr, "fit now_sec: %f\n", now_sec / 3600.0);
            if (now_sec < min_sec) min_sec = now_sec;
            if (now_sec > max_sec) max_sec = now_sec;
        }
        range r;
        r.from = max(0, min_sec / 3600);
        r.to = min(24, (max_sec + 3599) / 3600);
        state.hours_view = r;
    }
    assert(state.hours_view.to > state.hours_view.from
            && state.hours_view.to <= 24
            && state.hours_view.from >= 0, "wrong from/to hour");
}

static void fit_events() {
    int min_sec = 3600 * 24 + 1, max_sec = -1;
    for (int i = 0; i < state.active_n; ++i) {
        struct event *ev = state.active_events[i];
        if (different_day(ev->start.local_time, ev->end.local_time)) {
            /* TODO: maybe could do better when the event begins on the
             * last day of the view range. */
            min_sec = 0;
            max_sec = 3600 * 24;
            break;
        }
        int start = day_sec(ev->start.local_time),
            end = day_sec(ev->end.local_time);
        if (start < min_sec) min_sec = start;
        if (end > max_sec) max_sec = end;
    }

    if (min_sec > max_sec) min_sec = 0, max_sec = 3600 * 24;

    range r;
    r.from = max(0, min_sec / 3600);
    r.to = min(24, (max_sec + 3599) / 3600);
    if (r.to <= r.from) r = (range){ 0, 24 };
    state.hours_view_events = r;
    state.hours_view_manual = (range){ -1, -1 };

    update_actual_fit();

    // fprintf(stderr, "fit: %f-%f\n", min_sec / 3600.0, max_sec / 3600.0);
}

static void reload_calendars() {
    for (int i = 0; i < state.n_cal; i++) {
        update_calendar_from_storage(&state.cal[i], state.zone->impl);
    }
    update_active_events();
    fit_events();
    update_active_todos();
    state.dirty = true;
}

static void cairo_set_source_argb(cairo_t *cr, uint32_t c){
    cairo_set_source_rgba(cr,
            ((c >> 16) & 0xFF) / 255.0,
            ((c >> 8) & 0xFF) / 255.0,
            (c & 0xFF) / 255.0,
            ((c >> 24) & 0xFF) / 255.0);
}

void paint_event(cairo_t *cr, int day_i, time_t day_base,
        box b, int max_n, int col, int idx) {
    struct event *ev = state.active_events[idx];
    struct event_tag *event_tag = &state.active_events_tag[idx];
    assert(interval_overlap(
                ev->start.timestamp, ev->end.timestamp,
                day_base, day_base + 3600 * 24
    ), "event does not overlap with day");

    int num_days = state.view_days;

    int start_sec = max(0, ev->start.timestamp - day_base);
    int end_sec = min(3600 * 24, ev->end.timestamp - day_base);

    int from_sec = state.hours_view.from * 3600;
    int to_sec = state.hours_view.to * 3600;
    int interval_sec = to_sec - from_sec;

    int pad = 2;
    int sw = b.w / num_days;
    int dw = sw / max_n;
    int x = sw * day_i + dw * col + pad;
    int y = b.h * (start_sec - from_sec) / interval_sec;
    int w = dw - 2*pad;
    int h = b.h * (end_sec - start_sec) / interval_sec;

    uint32_t color = ev->color;
    if (!color) color = 0xFF00FF00;
    if (ev->tentative) {
        color = (color & 0x00FFFFFF) | 0x30000000;
    }
    double lightness = (color & 0xFF) + ((color >> 8) & 0xFF)
        + ((color >> 16) & 0xFF);
    lightness /= 255.0;
    cairo_set_source_argb(cr, color);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

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
        char *text = text_format("%02d:%02d-%02d:%02d %s",
                ev->start.local_time.tm_hour, ev->start.local_time.tm_min,
                ev->end.local_time.tm_hour, ev->end.local_time.tm_min,
                ev->summary);
        text_print_free(cr, state.tr, text);

        if (ev->location) {
            cairo_set_source_argb(cr, light ? 0xFFA0A0A0 : 0xFF606060);
            cairo_move_to(cr, x, y+h-loc_h);

            state.tr->p.width = w; state.tr->p.height = loc_h;
            text_print_own(cr, state.tr, ev->location);
        }
    }

    if (state.mode_select) {
        uint32_t c = (color ^ 0x00FFFFFF) | 0xFF000000;
        cairo_set_source_argb(cr, c);
        char *text = event_tag->code;
        state.tr->p.scale = 3.0;
        state.tr->p.width = w; state.tr->p.height = -1;
        text_get_size(cr, state.tr, text);
        cairo_move_to(cr, x + w/2 - state.tr->p.width/2,
                          y + h/2 - state.tr->p.height/2);
        text_print_own(cr, state.tr, event_tag->code);
        state.tr->p.scale = 1.0;
    }
}

void paint_sidebar(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);
    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    int h = 0;
    int pad = 6;
    for (int i = 0; i < state.n_cal; i++) {
        bool vis = state.cal_info[i].visible;
        const char *name = state.cal[i].name;
        if (!state.show_private_events && state.cal[i].priv) continue;

        char *text = text_format("%i: %s", i + 1, name);
        state.tr->p.width = b.w;
        text_get_size(cr, state.tr, text);
        int height = state.tr->p.height;

        cairo_set_source_argb(cr, vis ? 0xFF00FF00 : 0xFFFFFFFF);
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

    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    cairo_move_to(cr, b.w, 0);
    cairo_line_to(cr, b.w, b.h);
    cairo_stroke(cr);
    cairo_identity_matrix(cr);
}

void paint_header(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);

    cairo_set_source_rgba(cr, 0, 0, 0, 255);

    cairo_move_to(cr, 0, b.h);
    cairo_line_to(cr, b.w, b.h);
    cairo_stroke(cr);

    int num_days = state.view_days;
    int sw = b.w / num_days;
    for (int i = 1; i < num_days; i++) {
        cairo_move_to(cr, sw*i, 0);
        cairo_line_to(cr, sw*i, b.h);
    }
    cairo_stroke(cr);

    char *days[] = { "H", "K", "Sze", "Cs", "P", "Szo", "V" };
    char buf[64];
    for (int i = 0; i < num_days; i++) {
        time_t time_off = state.base + 3600 * 24 * i;
        struct icaltimetype t = icaltime_from_timet_with_zone(
            time_off, false, state.zone->impl);
        int dow = icaltime_day_of_week(t);
        snprintf(buf, 64, "%s: %d-%d",
                days[(dow+5)%7], t.month, t.day);
        state.tr->p.scale = 1.5;
        text_print_center(cr, (box){ i*sw, 0, sw, b.h }, buf);
        state.tr->p.scale = 1.0;
    }

    cairo_identity_matrix(cr);
}

void paint_calendar_events(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);
    for (int d = 0; d < state.view_days; d++) {
        // TODO: what if day not 24h long?
        time_t day_base = state.base + 3600 * 24 * d;
        int n = state.layout_event_n[d];
        struct layout_event *la = state.layout_events[d];
        for (int k = 0; k < n; k++) {
            struct layout_event *le = &la[k];
            paint_event(cr, d, day_base, b, le->max_n, le->col, le->idx);
        }
    }
    cairo_translate(cr, -b.x, -b.y);
}

void paint_calendar(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);

    int num_days = state.view_days;
    int time_strip_w = 30;
    int sw = (b.w - time_strip_w) / num_days;

    cairo_translate(cr, time_strip_w, 0);

    // draw vertical lines
    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    for (int i = 0; i < num_days; i++) {
        cairo_move_to(cr, sw*i, 0);
        cairo_line_to(cr, sw*i, b.h);
    }
    cairo_stroke(cr);

    cairo_set_line_width(cr, 1);
    cairo_set_source_argb(cr, 0xFF555555);
    range r = state.hours_view;
    for (int i = r.from + 1; i < r.to; i++) {
        int y = b.h * (i - r.from) / (r.to - r.from);
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, b.w - time_strip_w, y);
    }
    cairo_stroke(cr);
    cairo_set_line_width(cr, 2);

    cairo_translate(cr, -time_strip_w, 0);
    char buf[64];
    for (int i = r.from + 1; i < r.to; i++) {
        int y = b.h * (i - r.from) / (r.to - r.from);
        snprintf(buf, 64, "%02d", i);
        draw_text(cr, time_strip_w / 2, y, buf);
    }

    // draw all the events
    paint_calendar_events(cr, (box){ time_strip_w, 0, b.w-time_strip_w, b.h });

    // draw time marker red line
    cairo_translate(cr, time_strip_w, 0);
    time_t now = state.now;
    struct tm t = timet_to_tm_with_zone(now, state.zone);
    int now_sec = day_sec(t);
    int interval_sec = (r.to - r.from) * 3600;
    int day_sec = 24 * 3600;
    int day_i = (now - state.base) / day_sec;
    int y = b.h * (now_sec - r.from * 3600) / interval_sec;
    if (now >= state.base) {
        cairo_move_to(cr, day_i * sw, y);
        cairo_line_to(cr, (day_i+1) * sw, y);
        cairo_set_source_rgba(cr, 255, 0, 0, 255);
        cairo_stroke(cr);
    }

    cairo_identity_matrix(cr);
}

static char* natural_date_format(const struct date *d) {
    time_t now = state.now;
    struct tm t = timet_to_tm_with_zone(now, state.zone);
    struct tm lt = d->local_time;
    if (t.tm_year == lt.tm_year && t.tm_yday == lt.tm_yday)
        return text_format("%02d:%02d", lt.tm_hour, lt.tm_min);
    else
        return text_format("%04d-%02d-%02d %02d:%02d", lt.tm_year + 1900,
            lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
}

static void paint_todo_item(cairo_t *cr, struct todo *td, box b) {
    cairo_translate(cr, b.x, b.y);
    cairo_set_source_argb(cr, 0xFF000000);

    int w = b.w;
    if (td->due.timestamp != -1) {
        char *text = natural_date_format(&td->due);
        text_print_center(cr, (box){ b.w - 80, 0, 80, b.h }, text);
        free(text);
    }
    w -= 80;

    int n = 1;
    if (td->desc) ++n;
    state.tr->p.width = w/n; state.tr->p.height = b.h;
    text_get_size(cr, state.tr, td->summary);
    cairo_move_to(cr, 0, b.h/2 - state.tr->p.height/2);
    text_print_own(cr, state.tr, td->summary);

    if (td->desc) {
        state.tr->p.width = w/n; state.tr->p.height = b.h;
        text_get_size(cr, state.tr, td->summary);
        cairo_move_to(cr, w/n, b.h/2 - state.tr->p.height/2);
        text_print_own(cr, state.tr, td->desc);
    }

    cairo_set_line_width(cr, 1);
    for (int i = 1; i <= n; ++i) {
        cairo_move_to(cr, w*i/n +.5, 0);
        cairo_line_to(cr, w*i/n +.5, b.h);
        cairo_stroke(cr);
    }

    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, b.h);
    cairo_line_to(cr, b.w, b.h);
    cairo_stroke(cr);
    cairo_translate(cr, -b.x, -b.y);
}

void paint_todo_list(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);
    for (int i = 0; i < state.active_todo_n; ++i) {
        paint_todo_item(cr, state.active_todos[i], (box){ 0, i*40, b.w, 40 });
        if (i*40 > b.h) break;
    }
    cairo_identity_matrix(cr);
}

static bool
paint(struct window *window, cairo_t *cr) {
    int w, h;
    get_window_size(window, &w, &h);
    time_t now = time(NULL);
    if (state.now != now) {
        state.now = now;
        update_actual_fit();
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

    const char *view_name = "";
    switch (state.main_view) {
    case VIEW_CALENDAR:
        paint_calendar(cr,
            (box){ sidebar_w, header_h, w-sidebar_w, h-header_h });
        paint_header(cr, (box){
            sidebar_w + time_strip_w, 0,
            w-sidebar_w-time_strip_w, header_h
        });
        view_name = "calendar";
        break;
    case VIEW_TODO:
        paint_todo_list(cr,
            (box){ sidebar_w, header_h, w-sidebar_w, h-header_h });
        view_name = "todo";
    }
    paint_sidebar(cr, (box){ 0, header_h, sidebar_w, h-header_h });

    if (state.n_cal > 0) {
        cairo_move_to(cr, 0, 0);

        struct tm t = timet_to_tm_with_zone(state.now, state.zone);
        char *text = text_format(
                "%s\rframe %d\r%02d:%02d:%02d\rmode: %s",
                get_timezone_desc(state.zone),
                frame_counter,
                t.tm_hour, t.tm_min, t.tm_sec,
                view_name);
        state.tr->p.width = -1 /* sidebar_w */; state.tr->p.height = header_h;
        text_print_free(cr, state.tr, text);
    }

    state.dirty = false;
    return true;
}

static void
handle_key(struct display *display, uint32_t key, uint32_t mods) {
    if (key_is_sym(key)) {
        if (state.mode_select) {
            mode_select_append_sym(key_get_sym(key));
            return;
        }
        if (key_sym(key, 'a')) {
            state.main_view = VIEW_CALENDAR;
            state.dirty = true;
        } else if (key_sym(key, 's')) {
            state.main_view = VIEW_TODO;
            state.dirty = true;
        } else if (key_sym(key, 'l')) {
            timet_adjust_days(&state.base, state.zone, state.view_days);
            update_active_events();
            fit_events();
            state.dirty = true;
        } else if (key_sym(key, 'h')) {
            timet_adjust_days(&state.base, state.zone, -state.view_days);
            update_active_events();
            fit_events();
            state.dirty = true;
        } else if (key_sym(key, 't')) {
            state.base = get_day_base(state.zone, state.view_days > 1);
            update_active_events();
            fit_events();
            state.dirty = true;
        } else if (key_sym(key, 'n')) {
            if (!state.sp) {
                time_t now = time(NULL);
                struct tm t = *gmtime(&now);
                struct event template = {
                    .uid = NULL,
                    .summary = NULL,
                    .start = { .local_time = t },
                    .end = { .local_time = t },
                    .location = NULL,
                    .desc = NULL
                };

                state.sp = subprocess_new_event_input(
                    state.editor[0], state.editor + 1, &template);
            }
        } else if (key_sym(key, 'e')) {
            switch_mode_select();
            state.dirty = true;
        } else if (key_sym(key, 'c')) {
            //TODO: reset calendar visibility
        } else if (key_sym(key, 'v')) {
            if (state.view_days > 1) state.view_days = 1;
            else state.view_days = 7;
            update_active_events();
            fit_events();
            state.dirty = true;
        } else if (key_sym(key, 'r')) {
            reload_calendars();
        } else if (key_sym(key, 'p')) {
            state.show_private_events = !state.show_private_events;
            update_active_events();
            update_active_todos();
            state.dirty = true;
        }
    }
    int n;
    if ((n = key_num(key)) >= 0) {
        --n; /* key 1->0 .. key 9->8 */
        if (n < state.n_cal) {
            state.cal_info[n].visible = ! state.cal_info[n].visible;
            update_active_events();
            fit_events();
            state.dirty = true;
        }
    }
    if (mods & 1) { // shift
        if (key == 13) { // KEY_EQUALS
            if (state.hours_view_manual.from == -1) {
                state.hours_view_manual = state.hours_view_events;
                update_actual_fit();
                state.dirty = true;
            }
            if (state.hours_view_manual.to > state.hours_view_manual.from + 1) {
                --state.hours_view_manual.to;
                update_actual_fit();
                state.dirty = true;
            }
        }
        if (key == 12) { // KEY_MINUS
            if (state.hours_view_manual.from == -1) {
                state.hours_view_manual = state.hours_view_events;
                update_actual_fit();
                state.dirty = true;
            }
            if (state.hours_view_manual.to < 24) {
                ++state.hours_view_manual.to;
                update_actual_fit();
                state.dirty = true;
            }
            if (state.hours_view_manual.from > 0) {
                --state.hours_view_manual.from;
                update_actual_fit();
                state.dirty = true;
            }
        }
    }
    if (key == 103) { // up
        if (state.hours_view_manual.from == -1) {
            state.hours_view_manual = state.hours_view_events;
            update_actual_fit();
            state.dirty = true;
        }
        if (state.hours_view_manual.from > 0) {
            --state.hours_view_manual.from;
            --state.hours_view_manual.to;
            update_actual_fit();
            state.dirty = true;
        }
    }
    if (key == 108) { // down
        if (state.hours_view_manual.from == -1) {
            state.hours_view_manual = state.hours_view_events;
            update_actual_fit();
            state.dirty = true;
        }
        if (state.hours_view_manual.to < 24) {
            ++state.hours_view_manual.from;
            ++state.hours_view_manual.to;
            update_actual_fit();
            state.dirty = true;
        }
    }
}

static void handle_child(struct display *display, pid_t pid) {
    FILE *f = subprocess_get_result(&(state.sp), pid);
    if (!f) return;

    struct event ev;
    int res;
    bool del;
    parse_event_template(f, &ev, state.zone->impl, &del);
    assert(state.n_cal > 0, "no calendar to save to");
    struct calendar *cal = &(state.cal[0]);
    res = save_event(&ev, cal, del, state.zone->impl);
    if (res >= 0) {
        update_active_events();
        fit_events();
        state.dirty = true;
    } else {
        fprintf(stderr, "event creation failed\n");
    }
}

int
main(int argc, char **argv) {
    state = (struct state){
        .n_cal = 0,
        .view_days = 7,
        .window_width = -1,
        .window_height = -1,
        .sp = NULL,
        .active_events = NULL,
        .active_todos = NULL,
        .layout_event_n = NULL,
        .mode_select = false,
        .show_private_events = false
    };

    for (int i = 0; i < 16; ++i) {
        // init cal_info
        state.cal_info[i] = (struct calendar_info) {
            .visible = false
        };
    }

    char editor_buffer[128], term_buffer[128];
    editor_buffer[0] = term_buffer[0] = '\0';
    const char *editor_env = getenv("EDITOR");
    if (editor_env) snprintf(editor_buffer, 128, "%s", editor_env);
    snprintf(term_buffer, 128, "st");

    int opt, d;
    while ((opt = getopt(argc, argv, "pd:e:")) != -1) {
        switch (opt) {
        case 'p':
            fprintf(stderr, "setting show_private_events = true\n");
            state.show_private_events = true;
            break;
        case 'd':
            d = atoi(optarg) - 1;
            if (d < 0 || d >= 16) {
                fprintf(stderr, "bad -d option index");
                exit(1);
            }
            state.cal_info[d].visible = true;
            break;
        case 'e':
            snprintf(editor_buffer, 128, "%s", optarg);
            break;
        case 't':
            snprintf(term_buffer, 128, "%s", optarg);
            break;
        }
    }

    assert(editor_buffer[0], "please set editor!");
    assert(term_buffer[0], "please set terminal emulator!");
    const char *editor[] = { term_buffer, term_buffer,
        editor_buffer, "{file}", NULL };
    fprintf(stderr, "editor command: %s, term command: %s\n",
        editor_buffer, term_buffer);
    state.editor = editor;

    state.zone = new_timezone("Europe/Budapest");
    state.base = get_day_base(state.zone, state.view_days > 1);

    for (int i = optind; i < argc; i++) {
        struct calendar *cal = &state.cal[state.n_cal];

        // init
        init_calendar(cal);

        // read
        cal->storage = str_dup(argv[i]);
        fprintf(stderr, "loading %s\n", cal->storage);
        update_calendar_from_storage(cal, state.zone->impl);

        // set metadata
        if (!cal->name) cal->name = str_dup(cal->storage);
        cal->storage = str_dup(argv[i]);

        // next
        if (++state.n_cal >= 16) break;
    }

    update_active_events();
    fit_events();
    update_active_todos();

    struct display *display =
        create_display(&paint, &handle_key, &handle_child);
    struct window *window = create_window(display, 900, 700);
    if (!window) return 1;

    state.tr = text_renderer_new("Monospace 8");

    state.dirty = true;
    gui_run(window);

    discard_temp_structures();
    for (int i = 0; i < state.n_cal; i++) {
        free_calendar(&state.cal[i]);
    }
    free_timezone(state.zone);
    text_renderer_free(state.tr);

    destroy_window(window);
    destroy_display(display);
    return 0;
}
