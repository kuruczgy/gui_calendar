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
    " i{h,j,k}: select view mode\r"
    " [1-9]: toggle calendar visibility\r"
    " +/-: inc./dec. vertical scale\r"
    " up/down: move 1 hour up/down\r";

static char* natural_date_format(const struct date *d) {
    time_t now = state.now;
    struct tm t = timet_to_tm_with_zone(now, state.zone->impl);
    struct tm lt = timet_to_tm_with_zone(d->timestamp, state.zone->impl);
    if (t.tm_year == lt.tm_year && t.tm_yday == lt.tm_yday)
        return text_format("%02d:%02d", lt.tm_hour, lt.tm_min);
    else
        return text_format("%04d-%02d-%02d %02d:%02d", lt.tm_year + 1900,
            lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
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

static void cairo_set_source_argb(cairo_t *cr, uint32_t c){
    cairo_set_source_rgba(cr,
            ((c >> 16) & 0xFF) / 255.0,
            ((c >> 8) & 0xFF) / 255.0,
            (c & 0xFF) / 255.0,
            ((c >> 24) & 0xFF) / 255.0);
}

static void render_event(cairo_t *cr, box b, box all_day_b,
        struct active_event_layout *ael) {
    struct active_event *aev = ael->aev;
    struct event *ev = aev->ev;

    int num_days = state.view_days;

    // TODO: what if day not 24h long?
    time_t slot_base = ael->day_i == -1 ?
            state.base : state.base + 3600 * 24 * ael->day_i;
    time_t slot_len = ael->day_i == -1 ? 3600 * 24 * num_days : 3600 * 24;
    assert(interval_overlap(
                aev->start.timestamp, aev->end.timestamp,
                slot_base, slot_base + slot_len
    ), "event does not overlap with slot");

    int from_sec = state.hours_view.from * 3600;
    int to_sec = state.hours_view.to * 3600;
    int interval_sec = to_sec - from_sec;

    int pad = 2;
    int x, y, w, h;
    box ab;
    if (ael->day_i == -1) {
        ab = all_day_b;
        x = all_day_b.w * ael->start / slot_len + pad;
        y = all_day_b.h * ael->col / ael->max_n + pad;
        w = all_day_b.w * (ael->end - ael->start) / slot_len - 2 * pad;
        h = all_day_b.h / ael->max_n - 2 * pad;
    } else {
        ab = b;
        int sw = b.w / num_days;
        int dw = sw / ael->max_n;
        x = sw * ael->day_i + dw * ael->col + pad;
        y = b.h * (ael->start - from_sec) / interval_sec;
        w = dw - 2*pad;
        h = b.h * (ael->end - ael->start) / interval_sec;
    }
    cairo_translate(cr, ab.x, ab.y);

    uint32_t color = ev->color;
    if (!color) color = 0xFF20D0D0;
    if (ev->status == ICAL_STATUS_TENTATIVE ||
            ev->status == ICAL_STATUS_CANCELLED) {
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
                aev->local_start.hour, aev->local_start.minute,
                aev->local_end.hour, aev->local_end.minute,
                ev->summary);
        text_print_free(cr, state.tr, text);

        if (ev->location) {
            cairo_set_source_argb(cr, light ? 0xFFA0A0A0 : 0xFF606060);
            cairo_move_to(cr, x, y+h-loc_h);

            state.tr->p.width = w; state.tr->p.height = loc_h;
            text_print_own(cr, state.tr, ev->location);
        }
    }

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

    cairo_translate(cr, -ab.x, -ab.y);
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

        char *text = text_format("%i: %s", i + 1, name);
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

static void render_header(cairo_t *cr, box b) {
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

    char *days[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
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

static void render_calendar_events(cairo_t *cr, box b, box all_day_b) {
    for (int i = 0; i < state.active_event_layout_n; ++i) {
        struct active_event_layout *ael = &state.active_event_layouts[i];
        render_event(cr, b, all_day_b, ael);
    }
}

void render_calendar(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);

    int num_days = state.view_days;
    int time_strip_w = 30, top_strip_h = 50 * state.all_day_max_n;
    int sw = (b.w - time_strip_w) / num_days;

    cairo_set_source_argb(cr, 0xFF000000);

    // draw separator lines
    if (top_strip_h > 0) {
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, time_strip_w, top_strip_h + 0.5);
        cairo_line_to(cr, b.w, top_strip_h + 0.5);
        cairo_stroke(cr);

        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, time_strip_w, 0);
        cairo_line_to(cr, time_strip_w, top_strip_h);
        cairo_stroke(cr);
    }

    // draw vertical lines
    cairo_translate(cr, time_strip_w, top_strip_h);
    for (int i = 0; i < num_days; i++) {
        cairo_move_to(cr, sw*i, 0);
        cairo_line_to(cr, sw*i, b.h - top_strip_h);
    }
    cairo_stroke(cr);

    // draw horizontal lines
    cairo_set_line_width(cr, 1);
    cairo_set_source_argb(cr, 0xFF555555);
    range r = state.hours_view;
    for (int i = r.from + 1; i < r.to; i++) {
        int y = (b.h - top_strip_h) * (i - r.from) / (r.to - r.from);
        cairo_move_to(cr, 0, y + 0.5);
        cairo_line_to(cr, b.w - time_strip_w, y + 0.5);
    }
    cairo_stroke(cr);
    cairo_set_line_width(cr, 2);

    // draw hour labels
    cairo_translate(cr, -time_strip_w, 0);
    char buf[64];
    for (int i = r.from + 1; i < r.to; i++) {
        int y = (b.h - top_strip_h) * (i - r.from) / (r.to - r.from);
        snprintf(buf, 64, "%02d", i);
        draw_text(cr, time_strip_w / 2, y, buf);
    }

    // draw all the events
    cairo_translate(cr, 0, -top_strip_h);
    render_calendar_events(cr,
        (box){ time_strip_w, top_strip_h, b.w-time_strip_w, b.h-top_strip_h },
        (box){ time_strip_w, 0, b.w - time_strip_w, top_strip_h }
    );

    // draw time marker red line
    cairo_translate(cr, time_strip_w, top_strip_h);
    time_t now = state.now;
    struct tm t = timet_to_tm_with_zone(now, state.zone->impl);
    int now_sec = day_sec(t);
    int interval_sec = (r.to - r.from) * 3600;
    int day_sec = 24 * 3600;
    int day_i = (now - state.base) / day_sec;
    int y = (b.h - top_strip_h) * (now_sec - r.from * 3600) / interval_sec;
    if (now >= state.base) {
        cairo_move_to(cr, day_i * sw, y);
        cairo_line_to(cr, (day_i+1) * sw, y);
        cairo_set_source_rgba(cr, 255, 0, 0, 255);
        cairo_stroke(cr);
    }

    cairo_identity_matrix(cr);
}

static void render_todo_item(cairo_t *cr, struct todo *td, int index, box b) {
    cairo_translate(cr, b.x, b.y);
    cairo_set_source_argb(cr, 0xFF000000);
    struct todo_tag *todo_tag = &state.active_todos_tag[index];

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
        text_get_size(cr, state.tr, td->desc);
        cairo_move_to(cr, w/n, b.h/2 - state.tr->p.height/2);
        text_print_own(cr, state.tr, td->desc);
    }

    cairo_set_line_width(cr, 1);
    for (int i = 1; i <= n; ++i) {
        cairo_move_to(cr, w*i/n +.5, 0);
        cairo_line_to(cr, w*i/n +.5, b.h);
        cairo_stroke(cr);
    }

    if (state.keystate == KEYSTATE_SELECT) {
        uint32_t c = 0xFFFF00FF;
        cairo_set_source_argb(cr, c);
        char *text = todo_tag->code;
        state.tr->p.scale = 3.0;
        state.tr->p.width = b.w; state.tr->p.height = -1;
        text_get_size(cr, state.tr, text);
        cairo_move_to(cr, b.w/2 - state.tr->p.width/2,
                          b.h/2 - state.tr->p.height/2);
        text_print_own(cr, state.tr, todo_tag->code);
        state.tr->p.scale = 1.0;
    }

    cairo_set_source_argb(cr, 0xFF000000);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, b.h);
    cairo_line_to(cr, b.w, b.h);
    cairo_stroke(cr);
    cairo_translate(cr, -b.x, -b.y);
}

static void render_todo_list(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);
    for (int i = 0; i < state.active_todo_n; ++i) {
        render_todo_item(cr, state.active_todos[i], i, (box){0,i*40,b.w,40});
        if (i*40 > b.h) break;
    }
    cairo_identity_matrix(cr);
}

bool render_application(void *ud, cairo_t *cr) {
    int w, h;
    state.backend->vptr->get_window_size(state.backend, &w, &h);
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
        render_calendar(cr,
            (box){ sidebar_w, header_h, w-sidebar_w, h-header_h });
        render_header(cr, (box){
            sidebar_w + time_strip_w, 0,
            w-sidebar_w-time_strip_w, header_h
        });
        view_name = "calendar";
        break;
    case VIEW_TODO:
        render_todo_list(cr,
            (box){ sidebar_w, header_h, w-sidebar_w, h-header_h });
        view_name = "todo";
    }
    render_sidebar(cr, (box){ 0, header_h, sidebar_w, h-header_h });

    if (state.n_cal > 0) {
        cairo_move_to(cr, 0, 0);

        struct tm t = timet_to_tm_with_zone(state.now, state.zone->impl);
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
