#ifndef GUI_CALENDAR_APPLICATION_H
#define GUI_CALENDAR_APPLICATION_H
#include <ds/tree.h>
#include <libtouch.h>
#include <mgu/win.h>
#include <mgu/sr.h>
#include "calendar.h"
#include "datetime.h"
#include "views.h"
#include "uexpr.h"

struct calendar_info {
	uint32_t color;
};

/* contains a struct comp_inst, and some other data we use during viewing */
struct active_comp {
	struct comp_inst *ci;
	int cal_index;
	bool fade, hide, vis;
	struct calendar *cal;
	char code[33];

	struct interval_node node;
};

struct app;

enum app_view {
	VIEW_CALENDAR = 0,
	VIEW_TODO = 1,
	VIEW_N
};

struct tap_area {
	float aabb[4];
	int action_idx;
};

struct filter {
	struct str desc;
	int def_cal;
	int uexpr_fn;
};

struct action {
	char key_sym;
	struct str label;
	struct {
		enum app_view view;
	} cond;
	int uexpr_fn;
};

struct proj_item {
	struct comp_inst *ci;
	int cal_index;
	struct calendar *cal;
};
struct proj {
	void *self;
	void (*add)(void *self, struct proj_item pi);
	void (*done)(void *self);
	void (*clear)(void *self);
	bool (*ran)(void *self, struct ts_ran *ran);
	enum comp_type type;
};
struct proj_active_events {
	struct app *app;
	struct ts_ran ran;
	int n;
	struct rb_tree tree; /* items: struct active_comp */
};
struct proj_active_todos {
	struct app *app;
	struct vec v; /* vec<struct active_comp> */
};

struct app {
	/* calendars */
	struct vec cals; /* vec<struct calendar> */
	struct vec cal_infos; /* vec<struct calendar_info> */

	/* projections */
	struct proj_active_events active_events;
	struct proj_active_todos active_todos;
	struct vec projs; /* vec<struct proj> */

	ts expand_to;

	/* global UI state, user input handling */
	enum app_view main_view;
	enum {
		KEYSTATE_BASE,
		KEYSTATE_VIEW_SWITCH,
		KEYSTATE_SELECT
	} keystate;
	bool show_private_events;

	char mode_select_code[33];
	int mode_select_code_n;
	int mode_select_len;
	int mode_select_uexpr_fn;

	struct libtouch_surface *touch_surf;
	float touch_aabb[4];
	struct libtouch_area *touch_area;
	struct vec tap_areas; /* ve<struct tap_area> */

	/* calendar widgets and state in VIEW_CALENDAR mode */
	struct ts_ran view;
	struct slicing *slicing;

	/* editor subprocess info */
	struct subprocess_handle *sp;

	/* current time */
	ts now;

	/* rendered view dirtiness handling */
	int window_width, window_height;
	bool dirty;

	/* config */
	struct cal_timezone *zone;

	struct vec editor_args; /* vec<struct str> */

	struct uexpr uexpr;
	struct uexpr_ctx *uexpr_ctx;
	int uexpr_builtin_fn;

	struct vec filters; /* vec<struct filter> */
	int current_filter;

	struct vec actions; /* vec<struct action> */

	struct mgu_win *win;
	struct sr *sr;
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

/* cal_uexpr stuff */
enum cal_uexpr_kind {
	CAL_UEXPR_CONFIG = (1 << 0),
	CAL_UEXPR_FILTER = (1 << 1),
	CAL_UEXPR_ACTION = (1 << 2),
	CAL_UEXPR_SELECT_COMP = (1 << 3),
};
struct cal_uexpr_env {
	struct app *app;
	enum cal_uexpr_kind kind;
	struct active_comp *ac;
	struct props set_props;
	bool set_edit;
};
bool cal_uexpr_get(void *_env, const char *key, struct uexpr_value *v);
bool cal_uexpr_set(void *env, const char *key, struct uexpr_value v);

void app_init(struct app *app, struct application_options opts,
	struct mgu_win *win);
void app_main(struct app *app);
void app_finish(struct app *app);

/* update app->active_events for view */
void app_set_view(struct app *app, struct ts_ran view);

void app_update_projections(struct app *app);
void app_get_editor_template(struct app *app, struct comp_inst *ci, FILE *out);

/* commands directly accessible for the user */
void app_cmd_select_comp_uexpr(struct app *app, int uexpr_fn);
void app_cmd_launch_editor_new(struct app *app, enum comp_type t);
void app_cmd_launch_editor(struct app *app, struct active_comp *ac);
void app_cmd_editor(struct app *app, FILE *in);
void app_cmd_reload(struct app *app);
void app_cmd_activate_filter(struct app *app, int n);
void app_cmd_view_today(struct app *app, int n);
void app_cmd_toggle_show_private(struct app *app, int n);
void app_cmd_switch_view(struct app *app, int n);
void app_cmd_move_view_discrete(struct app *app, int n);

int app_add_cal(struct app *app, const char *path);
void app_add_uexpr_filter(struct app *app, const char *key, int def_cal, int uexpr_fn);
void app_add_action(struct app *app, struct action act);
void app_add_uexpr_config(struct app *app, const char *path);

#endif
