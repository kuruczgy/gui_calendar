#ifndef _APPLICATION_H_
#define _APPLICATION_H_
#include "calendar.h"
#include "backend.h"
#include "pango.h"

struct calendar_info {
    bool visible;
    uint32_t color;
};

struct event_tag {
    struct calendar *cal;
    char code[33];
};

struct active_event {
    struct event_recur_set *ers;
    struct date start, end;
    struct event *ev;
    struct event_tag tag;
};

struct active_event_layout {
    struct active_event *aev;
    int start, end, max_n, col, day_i;
};

struct todo_tag {
    struct todo *td;
    struct calendar *cal;
    char code[33];
};

typedef struct {
    int from, to;
} range;

struct state {
    struct text_renderer *tr;

    struct calendar cal[16];
    struct calendar_info cal_info[16];
    bool cal_default_visible[16];
    int n_cal;

    struct active_event *active_events;
    struct active_event_layout *active_event_layouts;
    int active_event_n, active_event_layout_n;

    struct todo **active_todos;
    struct todo_tag *active_todos_tag;
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
    struct calendar *sp_calendar;
    enum icalcomponent_kind sp_type;

    const char **editor;
    char mode_select_code[33];
    int mode_select_code_n;
    int mode_select_len;

    bool show_private_events;

    struct backend *backend;

    enum {
        VIEW_CALENDAR,
        VIEW_TODO
    } main_view;
    enum {
        KEYSTATE_BASE,
        KEYSTATE_VIEW_SWITCH,
        KEYSTATE_SELECT
    } keystate;

    bool dirty;
};

struct application_options {
    bool show_private_events;
    unsigned int default_vis;
    int view_days;
    char *editor;
    char *terminal;
    int argc;
    char **argv;
};

extern struct state state;

void update_actual_fit();
int application_main(struct application_options opts, struct backend *backend);

#endif
