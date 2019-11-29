#ifndef _APPLICATION_H_
#define _APPLICATION_H_
#include "calendar.h"
#include "gui.h"
#include "pango.h"

struct calendar_info {
    bool visible;
};

struct event_tag {
    struct event *ev;
    struct calendar *cal;
    char code[33];
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
    struct event **active_events;
    struct event_tag *active_events_tag;
    struct layout_event **layout_events;
    int *layout_event_n;
    int active_n;
    int n_cal;

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

extern struct state state;

void update_actual_fit();
int application_main(int argc, char **argv);

#endif