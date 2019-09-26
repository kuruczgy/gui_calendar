
#include <stdio.h>

#include <cairo.h>
#include "util.h"
#include "parse.h"
#include "pango.h"
#include "gui.h"

struct calendar_info {
    bool visible;
};

struct state {
    struct calendar cal[16];
    struct calendar_info cal_info[16];
    int n_cal;
    struct timezone *zone;

    int week_offset;
    int hour_from, hour_to;

    int window_width, window_height;

    bool dirty;
};

static struct state state;

typedef struct {
    int x, y, w, h;
} box;

void draw_text(cairo_t *cr, int x, int y, char *text) {
    cairo_text_extents_t ex;
    cairo_text_extents(cr, text, &ex);
    cairo_move_to(cr, x - ex.width/2, y + ex.height/2);
    cairo_show_text(cr, text);
}

static void cairo_set_source_argb(cairo_t *cr, uint32_t c){
    cairo_set_source_rgba(cr,
            ((c >> 16) & 0xFF) / 255.0,
            ((c >> 8) & 0xFF) / 255.0,
            (c & 0xFF) / 255.0,
            1.0);
}

void paint_sidebar(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);
    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    for (int i = 0; i < state.n_cal; i++) {
        bool vis = state.cal_info[i].visible;
        cairo_set_source_argb(cr, vis ? 0xFF00FF00 : 0xFFFFFFFF);
        cairo_rectangle(cr, 0, i*20, b.w, 20);
        cairo_fill(cr);
    }

    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    for(int i = 1; i < state.n_cal; i++) {
        cairo_move_to(cr, 0, i*20);
        cairo_line_to(cr, b.w, i*20);
    }
    cairo_stroke(cr);

    char buf[64];
    for (int i = 0; i < state.n_cal; i++) {
        snprintf(buf, 64, "%i: %s", i+1, state.cal[i].name);
        draw_text(cr, b.w/2, i*20+10, buf);
    }

    cairo_set_source_rgba(cr, .3, .3, .3, 1);
    cairo_move_to(cr, 0, state.n_cal * 20 + 30);
    pango_printf(cr, "Monospace 8", 1.0, b.w, b.h, "%s", 
            "Usage:\r"
            " n: next\r"
            " p: previous\r"
            " t: today\r"
            " up/down: move 1 hour up/down\r"
            " +/-: inc./dec. vertical scale\r");

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

    int n = 7;
    int sw = b.w / n;
    for (int i = 1; i < n; i++) {
        cairo_move_to(cr, sw*i, 0);
        cairo_line_to(cr, sw*i, b.h);
    }
    cairo_stroke(cr);

    char *days[] = { "H", "K", "Sze", "Cs", "P", "Szo", "V" };
    char buf[64];
    for (int i = 0; i < n; i++) {
        time_t time_off = base + 3600 * 24 * i;
        struct tm *t = gmtime(&time_off);
        snprintf(buf, 64, "%s: %d-%d",
                days[(t->tm_wday+6)%7], t->tm_mon+1, t->tm_mday);
        draw_text(cr, i*sw+sw/2, b.h/2, buf);
    }

    cairo_identity_matrix(cr);
}

static int day_sec(struct tm t) {
    return 3600 * t.tm_hour + 60 * t.tm_min + t.tm_sec;
}

static time_t week_base() {
    time_t now = time(NULL);
    struct tm t = *gmtime(&now);
    t.tm_sec = t.tm_min = t.tm_hour = 0;
    t.tm_mday -= (t.tm_wday-1+7)%7;
    return mktime(&t);
}

void paint_event(cairo_t *cr, struct event *ev, time_t base, box b,
        int max_n, int col) {
    time_t diff = ev->start.timestamp - base;
    if (diff > 3600 * 24 * 7 || diff < 0) return;

    int start_sec = day_sec(ev->start.local_time);
    int end_sec = day_sec(ev->end.local_time);
    int day_sec = 24 * 3600;
    int day_i = diff / day_sec;

    int from_sec = state.hour_from * 3600;
    int to_sec = state.hour_to * 3600;
    int interval_sec = to_sec - from_sec;

    int pad = 2;
    int sw = b.w / 7;
    int dw = sw / max_n;
    int x = sw * day_i + dw * col + pad;
    int y = b.h * (start_sec - from_sec) / interval_sec;
    int w = dw - 2*pad;
    int h = b.h * (end_sec - start_sec) / interval_sec;

    uint32_t color = ev->color;
    if (!color) color = 0xFF00FF00;
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

    // cairo_move_to(cr, x, y+h-loc_h);
    // cairo_line_to(cr, x+w, y+h-loc_h);
    // cairo_stroke(cr);
}

void paint_calendar_events(cairo_t *cr, box b, time_t base) {
    cairo_translate(cr, b.x, b.y);

    int num_days = 7;
    int sw = b.w / num_days;

    int n_all_events = 0;
    for (int i = 0; i < state.n_cal; i++)
        n_all_events += state.cal[i].n_events;
    struct event **active = malloc(sizeof(struct event*) * n_all_events);
    struct layout_event *layout_events =
        malloc(sizeof(struct layout_event*) * n_all_events);
    for (int d = 0; d < num_days; d++) {
        int active_n = 0;
        time_t day_base = base + 3600 * 24 * d;
        for (int i = 0; i < state.n_cal; i++) {
            if (!state.cal_info[i].visible) continue;
            struct event *ev = state.cal[i].events;
            while (ev) {
                time_t diff = ev->start.timestamp - day_base;
                if (diff > 3600 * 24 || diff < 0) goto next;
                active[active_n++] = ev;
next:
                ev = ev->next;
            }
        }
        for (int k = 0; k < active_n; k++) {
            struct event *ev = active[k];
            int start_sec = day_sec(ev->start.local_time);
            int end_sec = day_sec(ev->end.local_time);
            layout_events[k] = (struct layout_event){
                .start = start_sec,
                .end = end_sec
            };
        }
        calendar_layout(layout_events, active_n);
        for (int k = 0; k < active_n; k++) {
            struct event *ev = active[k];
            struct layout_event *le = &layout_events[k];
            paint_event(cr, ev, base, b, le->max_n, le->col);
        }
    }
    free(layout_events);
    free(active);
    cairo_translate(cr, -b.x, -b.y);
}

void paint_calendar(cairo_t *cr, box b, time_t base) {
    cairo_translate(cr, b.x, b.y);

    int num_days = 7;
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
    paint_calendar_events(cr, (box){ time_strip_w, 0, b.w-time_strip_w, b.h },
            base);

    // draw time marker red line
    cairo_translate(cr, time_strip_w, 0);
    time_t now = time(NULL); //TODO: fix this
    struct tm t = time_now(state.zone);
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
    if (state.window_width != w ||
            state.window_height != h) {
        state.window_width = w;
        state.window_height = h;
        state.dirty = true;
    }
    if (!state.dirty) return false;


    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
            CAIRO_FONT_WEIGHT_NORMAL); //TODO: this leaks??
    cairo_set_font_size(cr, 12);
    cairo_set_line_width(cr, 2);

	cairo_set_source_rgba(cr, 255, 255, 255, 255);
	cairo_paint(cr);

    int time_strip_w = 30;
    int sidebar_w = 120;
    int header_h = 60;

    time_t base = week_base() + state.week_offset * 7 * 24 * 3600;
    paint_calendar(cr,  (box){ sidebar_w, header_h, w-sidebar_w, h-header_h },
            base);
    paint_sidebar(cr,   (box){ 0, header_h, sidebar_w, h-header_h });
    paint_header(cr,    (box){ sidebar_w + time_strip_w, 0,
            w-sidebar_w-time_strip_w, header_h }, base);

    if (state.n_cal > 0) {
        cairo_move_to(cr, 0, 0);
        pango_printf(cr, "Monospace 8", 1.0, sidebar_w, header_h, "%s",
                get_timezone_desc(state.zone));
    }

    state.dirty = false;
    return true;
}

static void
handle_key(struct display *display, uint32_t key, uint32_t mods) {
    if (key == 49) {// n
        state.week_offset++;
        state.dirty = true;
    }
    if (key == 25) { // p
        state.week_offset--;
        state.dirty = true;
    }
    if (key == 20) { // t
        state.week_offset = 0;
        state.dirty = true;
    }
    if (key >= 0x02 && key <= 0x0A) {
        int n = key - 2; /* key 1->0 .. key 9->8 */
        if (n < state.n_cal) {
            state.cal_info[n].visible = ! state.cal_info[n].visible;
            state.dirty = true;
        }
    }
    if (mods & 1) { // shift
        if (key == 13) {
            if (state.hour_to > state.hour_from + 1) --state.hour_to;
            state.dirty = true;
        }
        if (key == 12) {
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

int
main(int argc, char **argv) {
    setenv("TZ", "UTC", 1); // fucking C time handling...

    state = (struct state){
        .n_cal = 0,
        .week_offset = 0,
        .window_width = -1,
        .window_height = -1
    };

    state.zone = new_timezone("Europe/Budapest");
    for (int i = 1; i < argc; i++) {
        struct calendar *cal = &state.cal[state.n_cal];
        cal->events = NULL;
        parse_dir(argv[i], cal);
        calendar_calc_local_times(cal, state.zone);
        if (!cal->name) cal->name = str_dup(argv[i]);
        state.cal_info[state.n_cal] = (struct calendar_info) {
            .visible = true
        };
        if (++state.n_cal >= 16) break;
    }

    state.hour_from = 5;
    state.hour_to = 17;

    struct display *display = create_display(&paint, &handle_key);
	struct window *window = create_window(display, 900, 700);
    if (!window) return 1;

    state.dirty = true;
    gui_run(window);

    for (int i = 0; i < state.n_cal; i++) {
        free_calendar(&state.cal[i]);
    }
    free_timezone(state.zone);

	destroy_window(window);
	destroy_display(display);
    return 0;
}
