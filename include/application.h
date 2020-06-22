#ifndef GUI_CALENDAR_APPLICATION_H
#define GUI_CALENDAR_APPLICATION_H
#include "calendar.h"
#include "backend.h"
#include "pango.h"
#include "datetime.h"
#include "views.h"
#include "uexpr.h"

struct calendar_info {
    bool visible, default_visible;
    uint32_t color;
};

/* contains a struct comp_inst, and some other data we use during viewing */
struct active_comp {
    struct comp_inst *ci;
    int cal_index;
    bool all_day;
    bool fade, hide, vis;
    struct calendar *cal;
    char code[33];
};

struct app {
    /* calendars */
    struct vec cals; /* vec<struct calendar> */
    struct vec cal_infos; /* vec<struct calendar_info> */

    /* active comps */
    struct vec active_events; /* vec<struct active_comp> */
    struct vec active_todos; /* vec<struct active_comp> */

    /* global UI state */
    enum {
        VIEW_CALENDAR,
        VIEW_TODO
    } main_view;
    enum {
        KEYSTATE_BASE,
        KEYSTATE_VIEW_SWITCH,
        KEYSTATE_SELECT
    } keystate;
    bool show_private_events;

    char mode_select_code[33];
    int mode_select_code_n;
    int mode_select_len;

    const char *current_filter_fn;

    /* calendar widgets and state in VIEW_CALENDAR mode */
    enum tview_type tview_type;
    struct tview tview, top_tview;
    int tview_n;
    ts base;

    /* editor subprocess info */
    struct subprocess_handle *sp;
    bool sp_expr;

    /* current time */
    ts now;

    /* rendered view dirtiness handling */
    int window_width, window_height;
    bool dirty;

    /* config */
    struct cal_timezone *zone;
    struct backend *backend;
    bool interactive;

    struct vec editor_args; /* vec<struct str> */

    uexpr expr;
    uexpr builtin_expr;
    uexpr_ctx builtin_expr_ctx;

    uexpr config_expr;
    uexpr_ctx config_ctx;
    const char **config_fns;

    /* utility objects */
    struct text_renderer *tr;
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

void update_actual_fit();

uexpr_val uexpr_cal_ac_get(void *cl, const char *key);
bool uexpr_cal_ac_set(void *cl, const char *key, uexpr_val val);

void app_init(struct app *app, struct application_options opts,
        struct backend *backend);
void app_main(struct app *app);
void app_finish(struct app *app);

void app_update_active_objects(struct app *app);
void app_get_editor_template(struct app *app, struct comp_inst *ci, FILE *out);

/* commands directly accessible for the user */
void app_cmd_editor(struct app *app, FILE *in);
void app_cmd_reload(struct app *app);
void app_cmd_activate_filter(struct app *app, int n);

#endif
