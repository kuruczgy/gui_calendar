#ifndef GUI_CALENDAR_APPLICATION_H
#define GUI_CALENDAR_APPLICATION_H
#include "calendar.h"
#include "backend.h"
#include "pango.h"
#include "datetime.h"
#include "views.h"
#include "uexpr.h"

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
    struct ts_ran time;
    struct event *ev;
    struct event_tag tag;
    int cal_index;
    bool fade, hide, vis;
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
    int active_event_n;

    enum tview_type tview_type;
    struct tview tview, top_tview;
    int tview_n;

    struct todo **active_todos;
    struct todo_tag *active_todos_tag;
    int active_todo_n;

    struct cal_timezone *zone;
    time_t base;
    time_t now;

    int window_width, window_height;

    struct subprocess_handle *sp;
    struct calendar *sp_calendar;
    bool sp_expr;

    const char **editor;
    char mode_select_code[33];
    int mode_select_code_n;
    int mode_select_len;

    uexpr expr;
    uexpr builtin_expr;
    uexpr_ctx builtin_expr_ctx;

    uexpr config_expr;
    uexpr_ctx config_ctx;
    char **config_fns;
    const char *current_fn;

    bool show_private_events;

    struct backend *backend;
    bool interactive;

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
    char *config_file;
    int argc;
    char **argv;
};

extern struct state state;

void update_actual_fit();

/* cl is expected to be a struct active_event * */
uexpr_val uexpr_cal_get(void *cl, const char *key);
bool uexpr_cal_set(void *cl, const char *key, uexpr_val val);

int application_main(struct application_options opts, struct backend *backend);

#endif
