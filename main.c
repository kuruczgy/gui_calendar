
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

    int week_offset;

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
            " t: today");

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
    t.tm_mday -= (t.tm_wday-1)%7;
    return mktime(&t);
}

void paint_event(cairo_t *cr, struct event *ev, time_t base, box b,
        int max_n, int col) {
    time_t diff = ev->start.timestamp - base;
    if (diff > 3600 * 24 * 7 || diff < 0) return;

    int start_sec = day_sec(ev->start.time);
    int end_sec = day_sec(ev->end.time);
    int day_sec = 24 * 3600;
    int day_i = diff / (3600 * 24);

    int pad = 2;
    int sw = b.w / 7;
    int dw = sw / max_n;
    int x = sw * day_i + dw * col + pad;
    int y = b.h * start_sec / day_sec;
    int w = dw - 2*pad;
    int h = b.h * (end_sec - start_sec) / day_sec;

    if (ev->color) cairo_set_source_argb(cr, ev->color);
    else cairo_set_source_rgba(cr, 0, 255, 0, 255);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    cairo_move_to(cr, x, y);
    pango_printf(cr, "Monospace 8", 1.0, w, h, "%s", ev->summary);
}

void paint_calendar(cairo_t *cr, box b, time_t base) {
    cairo_translate(cr, b.x, b.y);

    int n = 7;
    int sw = b.w / n;

    int n_all_events = 0;
    for (int i = 0; i < state.n_cal; i++) n_all_events += state.cal[i].n_events;

    struct event **active = malloc(sizeof(struct event*) * n_all_events);
    struct layout_event *layout_events =
        malloc(sizeof(struct layout_event*) * n_all_events);
    for (int d = 0; d < 7; d++) {
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
            int start_sec = day_sec(ev->start.time);
            int end_sec = day_sec(ev->end.time);
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

    time_t now = time(NULL);
    struct tm t = *gmtime(&now);
    int now_sec = day_sec(t);
    int day_sec = 24 * 3600;
    int day_i = (now - base) / day_sec;
    int y = b.h * now_sec / day_sec;
    cairo_move_to(cr, day_i * sw, y);
    cairo_line_to(cr, (day_i+1) * sw, y);
    cairo_set_source_rgba(cr, 255, 0, 0, 255);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    for (int i = 1; i < n; i++) {
        cairo_move_to(cr, sw*i, 0);
        cairo_line_to(cr, sw*i, b.h);
    }
    cairo_stroke(cr);

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
            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_set_line_width(cr, 2);

	cairo_set_source_rgba(cr, 255, 255, 255, 255);
	cairo_paint(cr);

    time_t base = week_base() + state.week_offset * 7 * 24 * 3600;
    paint_calendar(cr,  (box){ 120, 60, w-120, h-60 }, base);
    paint_sidebar(cr,   (box){ 0, 60, 120, h-60 });
    paint_header(cr,    (box){ 120, 0, w-120, 60 }, base);

    state.dirty = false;
    return true;
}

static void
handle_key(struct display *display, uint32_t key) {
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

    for (int i = 1; i < argc; i++) {
        state.cal[i].events = NULL;
        FILE *f = fopen(argv[i], "rb");
        parse_ics(f, &state.cal[state.n_cal]);
        state.cal[state.n_cal].name = argv[i];
        state.cal_info[state.n_cal] = (struct calendar_info) {
            .visible = true
        };
        if (++state.n_cal >= 16) break;
    }

    struct display *display = create_display(&paint, &handle_key);
	struct window *window = create_window(display, 900, 700);
    if (!window) return 1;

    state.dirty = true;
    gui_run(window);

	destroy_window(window);
	destroy_display(display);
    return 0;
}
