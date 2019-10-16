#include <stdio.h>
#include <cairo.h>
#include <sys/types.h>
#include "util.h"
#include "parse.h"
#include "pango.h"
#include "gui.h"
#include "keyboard.h"

struct calendar_info {
    bool visible;
};

struct event_tag {
    struct event *ev;
    char code[33];
};

struct state {
    struct calendar cal[16];
    struct calendar_info cal_info[16];
    struct event **active_events;
    struct event_tag *active_events_tag;
    struct layout_event **layout_events;
    int *layout_event_n;
    int active_n;
    int n_cal;

    struct cal_timezone *zone;
    time_t base;
    int view_days;
    int hour_from, hour_to;
    time_t now;

    int window_width, window_height;
    struct subprocess_handle *sp;
    const char **editor;
    bool mode_select;
    char mode_select_code[33];
    int mode_select_code_n;
    int mode_select_len;

    bool dirty;
};

static struct state state;

typedef struct {
    int x, y, w, h;
} box;

static bool interval_overlap(int a1, int a2, int b1, int b2) {
    return a1 <= b2 && a2 >= b1;
}

void draw_text(cairo_t *cr, int x, int y, char *text) {
    cairo_text_extents_t ex;
    cairo_text_extents(cr, text, &ex);
    cairo_move_to(cr, x - ex.width/2, y + ex.height/2);
    cairo_show_text(cr, text);
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

struct overlap_filterer {
    struct event **list;
    int n;
    time_t from, to;
};
static int filter_events_overlap(void *f_p, void *e_p) {
    struct overlap_filterer *f = f_p;
    struct event *e = e_p;
    do {
        if (interval_overlap(f->from,f->to,e->start.timestamp,e->end.timestamp)) {
            f->list[f->n++] = e;
        }
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
    struct overlap_filterer of = {
        .list = active,
        .n = 0,
        .from = state.base,
        .to = state.base + state.view_days * 3600 * 24
    };
    for (int i = 0; i < state.n_cal; i++) {
        if (state.cal_info[i].visible) {
            hashmap_iterate(state.cal[i].events,
                    filter_events_overlap, &of);
        }
    }
    int active_n = of.n;
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

    struct tm t = timet_to_tm_with_zone(state.now, state.zone);
    int now_sec = day_sec(t);
    time_t diff = time(NULL) - state.base;
    if (!(diff > state.view_days * 3600 * 24 || diff < 0)) {
        // fprintf(stderr, "fit now_sec: %f\n", now_sec / 3600.0);
        if (now_sec < min_sec) min_sec = now_sec;
        if (now_sec > max_sec) max_sec = now_sec;
    }

    state.hour_from = max(0, min_sec / 3600);
    state.hour_to = min(24, (max_sec + 3599) / 3600);
    if (state.hour_to<=state.hour_from) state.hour_from = 0, state.hour_to = 24;

    /* hack until this problem caused by DST is properly fixed */
    if (state.hour_to < 24) state.hour_to++;

    // fprintf(stderr, "fit: %f-%f\n", min_sec / 3600.0, max_sec / 3600.0);
}

static void reload_calendars() {
    for (int i = 0; i < state.n_cal; i++) {
        update_calendar_from_storage(&state.cal[i]);
        calendar_calc_local_times(&state.cal[i], state.zone);
    }
    update_active_events();
    fit_events();
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

    int from_sec = state.hour_from * 3600;
    int to_sec = state.hour_to * 3600;
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

    bool light = lightness < 0.9 ? true : false;
    uint32_t fg = light ? 0xFFFFFFFF : 0xFF000000;
    cairo_set_source_argb(cr, fg);

    int loc_h;
    get_text_size(cr, "Monospace 8", w, &loc_h, 1.0, "%s", ev->location);
    loc_h = min(h / 2, loc_h);

    cairo_move_to(cr, x, y);
    pango_printf(cr, "Monospace 8", 1.0, w, h-loc_h, "%02d:%02d-%02d:%02d %s",
            ev->start.local_time.tm_hour, ev->start.local_time.tm_min,
            ev->end.local_time.tm_hour, ev->end.local_time.tm_min,
            ev->summary);

    if (ev->location) {
        cairo_set_source_argb(cr, light ? 0xFFA0A0A0 : 0xFF606060);
        cairo_move_to(cr, x, y+h-loc_h);
        pango_printf(cr, "Monospace 8", 1.0, w, loc_h, "%s", ev->location);
    }

    if (state.mode_select) {
        cairo_set_source_argb(cr, 0xFFFF0000);
        cairo_move_to(cr, x, y);
        pango_printf(cr, "Monospace 13", 1.0, -1, -1, "%s", event_tag->code);
    }

    // cairo_move_to(cr, x, y+h-loc_h);
    // cairo_line_to(cr, x+w, y+h-loc_h);
    // cairo_stroke(cr);
}

void paint_sidebar(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);
    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    int h = 0;
    int pad = 6;
    for (int i = 0; i < state.n_cal; i++) {
        bool vis = state.cal_info[i].visible;
        const char *name = state.cal[i].name;

        int height;
        get_text_size(cr, "Monospace 8", b.w, &height, 1.0, "%i: %s",i+1,name);

        cairo_set_source_argb(cr, vis ? 0xFF00FF00 : 0xFFFFFFFF);
        cairo_rectangle(cr, 0, h, b.w, height + pad);
        cairo_fill(cr);

        cairo_set_source_argb(cr, 0xFF000000);
        cairo_move_to(cr, 0, h + pad / 2);
        pango_printf(cr, "Monospace 8", 1.0, b.w, height, "%i: %s", i+1, name);

        h += height + pad;
        cairo_move_to(cr, 0, h);
        cairo_line_to(cr, b.w, h);
        cairo_stroke(cr);
    }

    cairo_set_source_rgba(cr, .3, .3, .3, 1);
    cairo_move_to(cr, 0, h += 5);
    const char *usage =
        "Usage:\r"
        " n: next\r"
        " p: previous\r"
        " t: today\r"
        " v: cycle view modes\r"
        " up/down: move 1 hour up/down\r"
        " +/-: inc./dec. vertical scale\r"
        " c: create event\r"
        " e: edit event\r"
        " r: reload calendars\r";
    int height;
    get_text_size(cr, "Monospace 8", b.w, &height, 1.0, "%s", usage);
    pango_printf(cr, "Monospace 8", 1.0, b.w, b.h, "%s", usage);
    h += height;

    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    cairo_move_to(cr, b.w, 0);
    cairo_line_to(cr, b.w, b.h);
    cairo_stroke(cr);
    cairo_identity_matrix(cr);
}

void paint_header(cairo_t *cr, box b, time_t base) {
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
        time_t time_off = base + 3600 * 24 * i;
        struct icaltimetype t = icaltime_from_timet_with_zone(
            time_off, false, state.zone->impl);
        int dow = icaltime_day_of_week(t);
        snprintf(buf, 64, "%s: %d-%d",
                days[(dow+5)%7], t.month, t.day);
        draw_text(cr, i*sw+sw/2, b.h/2, buf);
    }

    cairo_identity_matrix(cr);
}

void paint_calendar_events(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);
    int sw = b.w / state.view_days;
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

void paint_calendar(cairo_t *cr, box b, time_t base) {
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
    for (int i = state.hour_from + 1; i < state.hour_to; i++) {
        int y = b.h * (i - state.hour_from) / (state.hour_to - state.hour_from);
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, b.w - time_strip_w, y);
    }
    cairo_stroke(cr);
    cairo_set_line_width(cr, 2);

    cairo_translate(cr, -time_strip_w, 0);
    char buf[64];
    for (int i = state.hour_from + 1; i < state.hour_to; i++) {
        int y = b.h * (i - state.hour_from) / (state.hour_to - state.hour_from);
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
    int interval_sec = (state.hour_to - state.hour_from) * 3600;
    int day_sec = 24 * 3600;
    int day_i = (now - base) / day_sec;
    int y = b.h * (now_sec - state.hour_from * 3600) / interval_sec;
    if (now >= base) {
        cairo_move_to(cr, day_i * sw, y);
        cairo_line_to(cr, (day_i+1) * sw, y);
        cairo_set_source_rgba(cr, 255, 0, 0, 255);
        cairo_stroke(cr);
    }

    cairo_identity_matrix(cr);
}

static bool
paint(struct window *window, cairo_t *cr) {
    int w = window->width, h = window->height;
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


    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
            CAIRO_FONT_WEIGHT_NORMAL); //TODO: this leaks??
    cairo_set_font_size(cr, 12);
    cairo_set_line_width(cr, 2);

	cairo_set_source_rgba(cr, 255, 255, 255, 255);
	cairo_paint(cr);

    int time_strip_w = 30;
    int sidebar_w = 120;
    int header_h = 60;

    time_t base = state.base;
    paint_calendar(cr,  (box){ sidebar_w, header_h, w-sidebar_w, h-header_h },
            base);
    paint_sidebar(cr,   (box){ 0, header_h, sidebar_w, h-header_h });
    paint_header(cr,    (box){ sidebar_w + time_strip_w, 0,
            w-sidebar_w-time_strip_w, header_h }, base);

    if (state.n_cal > 0) {
        cairo_move_to(cr, 0, 0);

        struct tm t = timet_to_tm_with_zone(state.now, state.zone);
        pango_printf(cr, "Monospace 8", 1.0, sidebar_w, header_h,
                "%s\rframe %d\r%02d:%02d:%02d",
                get_timezone_desc(state.zone),
                frame_counter,
                t.tm_hour, t.tm_min, t.tm_sec);
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
        if (key_sym(key, 'n')) {
            timet_adjust_days(&state.base, state.zone, state.view_days);
            update_active_events();
            fit_events();
            state.dirty = true;
        }
        if (key_sym(key, 'p')) {
            timet_adjust_days(&state.base, state.zone, -state.view_days);
            update_active_events();
            fit_events();
            state.dirty = true;
        }
        if (key_sym(key, 't')) {
            state.base = get_day_base(state.zone, state.view_days > 1);
            update_active_events();
            fit_events();
            state.dirty = true;
        }
        if (key_sym(key, 'v')) {
            if (state.view_days > 1) state.view_days = 1;
            else state.view_days = 7;
            update_active_events();
            fit_events();
            state.dirty = true;
        }
        if (key_sym(key, 'c')) {
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
        }
        if (key_sym(key, 'e')) {
            switch_mode_select();
            state.dirty = true;
        }
        if (key_sym(key, 'r')) {
            reload_calendars();
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
            if (state.hour_to > state.hour_from + 1) --state.hour_to;
            state.dirty = true;
        }
        if (key == 12) { // KEY_MINUS
            if (state.hour_to < 24) { ++state.hour_to; state.dirty = true; }
            if (state.hour_from > 0) { --state.hour_from; state.dirty = true; }
        }
    }
    if (key == 103) { // up
        if (state.hour_from > 0) {
            --state.hour_from;
            --state.hour_to;
            state.dirty = true;
        }
    }
    if (key == 108) { // down
        if (state.hour_to < 24) {
            ++state.hour_from;
            ++state.hour_to;
            state.dirty = true;
        }
    }
    assert(state.hour_to > state.hour_from && state.hour_to <= 24
            && state.hour_from >= 0, "wrong from/to hour");
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
    res = save_event(&ev, cal, del);
    if (res >= 0) {
        calendar_calc_local_times(cal, state.zone);
        update_active_events();
        fit_events();
        state.dirty = true;
    } else {
        fprintf(stderr, "event creation failed\n");
    }
}

int
main(int argc, char **argv) {
    setenv("TZ", "UTC", 1); // fucking C time handling...

    state = (struct state){
        .n_cal = 0,
        .view_days = 7,
        .window_width = -1,
        .window_height = -1,
        .sp = NULL,
        .active_events = NULL,
        .layout_event_n = NULL,
        .mode_select = false
    };

    const char *ed = getenv("EDITOR");
    assert(ed, "please set EDITOR env variable");
    const char *editor[] = { "st", "st", ed, "{file}", NULL }; // TODO: config
    state.editor = editor;

    state.zone = new_timezone("Europe/Budapest");
    state.base = get_day_base(state.zone, state.view_days > 1);

    for (int i = 1; i < argc; i++) {
        struct calendar *cal = &state.cal[state.n_cal];

        // init
        init_calendar(cal);

        // read
        cal->storage = str_dup(argv[i]);
        update_calendar_from_storage(cal);
        calendar_calc_local_times(cal, state.zone);

        // set metadata
        if (!cal->name) cal->name = str_dup(argv[i]);
        cal->storage = str_dup(argv[i]);

        // init cal_info
        state.cal_info[state.n_cal] = (struct calendar_info) {
            .visible = (i == 1) // default to only the first being visible
        };

        // next
        if (++state.n_cal >= 16) break;
    }

    update_active_events();
    fit_events();

    struct display *display =
        create_display(&paint, &handle_key, &handle_child);
	struct window *window = create_window(display, 900, 700);
    if (!window) return 1;

    state.dirty = true;
    gui_run(window);

    discard_temp_structures();
    for (int i = 0; i < state.n_cal; i++) {
        free_calendar(&state.cal[i]);
    }
    free_timezone(state.zone);

	destroy_window(window);
	destroy_display(display);
    return 0;
}
