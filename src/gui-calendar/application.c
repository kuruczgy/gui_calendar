#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "application.h"
#include "core.h"
#include "util.h"
#include "algo.h"
#include "keyboard.h"
#include "backend.h"
#include "render.h"
#include "editor.h"

/* provides a partial ordering over todos; implements the a < b comparison */
static bool todo_priority_cmp(
        struct app *app,
        const struct comp_inst *a,
        const struct comp_inst *b,
        bool consider_started
    ) {
    ts start;
    bool a_started = true, b_started = true;
    if (props_get_start(a->p, &start)) a_started = start <= app->now;
    if (props_get_start(b->p, &start)) b_started = start <= app->now;

    enum prop_status status;
    bool a_inprocess = false, b_inprocess = false;
    if (props_get_status(a->p, &status))
        a_inprocess = status == PROP_STATUS_INPROCESS;
    if (props_get_status(b->p, &status))
        b_inprocess = status == PROP_STATUS_INPROCESS;

    int ed;
    const int sh = 60 * 10; // 10m
    bool a_short = false, b_short = false;
    if (props_get_estimated_duration(a->p, &ed)) a_short = ed <= sh;
    if (props_get_estimated_duration(b->p, &ed)) b_short = ed <= sh;

    ts a_due, b_due;
    bool a_has_due, b_has_due;
    a_has_due = props_get_due(a->p, &a_due);
    b_has_due = props_get_due(b->p, &b_due);

    if (a_inprocess != b_inprocess) {
        if (a_inprocess) return true;
        else return false;
    } else if (consider_started && a_started != b_started) {
        if (a_started) return true;
        else return false;
    } else if (a_short != b_short) {
        if (a_short) return true;
        else return false;
    } else if (a_has_due || b_has_due) {
        if (!a_has_due) return false;
        else if (!b_has_due) return true;
        return a_due < b_due;
    }
    return false;
}

static void app_switch_mode_select(struct app *app) {
    struct key_gen g;

    if (app->keystate == KEYSTATE_SELECT) return;
    app->mode_select_code_n = 0;
    app->keystate = KEYSTATE_SELECT;

    //TODO: code duplication
    if (app->main_view == VIEW_CALENDAR) {
        key_gen_init(app->active_events.len, &g);
        app->mode_select_len = g.k;
        for (int i = 0; i < app->active_events.len; ++i) {
            struct active_comp *ac = vec_get(&app->active_events, i);
            const char *code = key_gen_get(&g);
            asrt(code, "not enough codes");
            strcpy(ac->code, code);
        }
    } else if (app->main_view == VIEW_TODO) {
        struct key_gen g;
        key_gen_init(app->active_todos.len, &g);
        app->mode_select_len = g.k;
        for (int i = 0; i < app->active_todos.len; ++i) {
            struct active_comp *ac = vec_get(&app->active_todos, i);
            const char *code = key_gen_get(&g);
            asrt(code, "not enough codes");
            strcpy(ac->code, code);
        }
    } else {
        asrt(false, "unknown view");
    }
}

struct print_template_cl {
    struct app *app;
    struct active_comp *ac;
};
static void print_template_callback(void *_cl, FILE *f) {
    struct print_template_cl *cl = _cl;
    print_template(f, cl->ac->ci, cl->app->zone, cl->ac->cal_index + 1);
}
static int get_first_visible_cal_index(struct app *app) {
    for (int i = 0; i < app->cals.len; ++i) {
        // struct calendar *cal = vec_get(&app->cals, i);
        struct calendar_info *cal_info = vec_get(&app->cal_infos, i);
        if (cal_info->visible) return i;
    }
    asrt(app->cals.len > 0, "no calendars");
    return 0;
}
static void print_new_event_template_callback(void *cl, FILE *f) {
    struct app *app = cl;
    int cal = get_first_visible_cal_index(app);
    print_new_event_template(f, app->zone, cal + 1);
}
static void print_new_todo_template_callback(void *cl, FILE *f) {
    struct app *app = cl;
    int cal = get_first_visible_cal_index(app);
    print_new_todo_template(f, app->zone, cal + 1);
}
static void print_str_callback(void *cl, FILE *f) {
    const struct str *str = cl;
    fprintf(f, "%s", str_cstr(str));
}
static const char ** new_editor_args(struct app *app) {
    int len = app->editor_args.len;
    asrt(len > 1, "editor args len");
    const char **args = malloc_check(sizeof(char*) * len);
    for (int i = 0; i < len - 1; ++i) {
        struct str *s = vec_get(&app->editor_args, i + 1);
        args[i] = str_cstr(s);
    }
    args[len - 1] = NULL;
    return args;
}
static void app_launch_editor(struct app *app, struct active_comp *ac) {
    if (!app->sp) {
        struct print_template_cl cl = { .app = app, .ac = ac };
        const char **args = new_editor_args(app);
        struct str *s = vec_get(&app->editor_args, 0);
        app->sp = subprocess_new_input(str_cstr(s),
            args, &print_template_callback, &cl);
        free(args);
    }
}
static void app_launch_editor_str(struct app *app, const struct str *str) {
    if (!app->sp) {
        const char **args = new_editor_args(app);
        struct str *s = vec_get(&app->editor_args, 0);
        app->sp = subprocess_new_input(str_cstr(s),
            args, &print_str_callback, (void*)str);
        free(args);
    }
}
static void app_launch_new_editor(struct app *app) {
    void (*cb)(void*, FILE*);
    if (app->main_view == VIEW_CALENDAR) {
        cb = &print_new_event_template_callback;
    } else {
        cb = &print_new_todo_template_callback;
    }
    if (!app->sp) {
        const char **args = new_editor_args(app);
        struct str *s = vec_get(&app->editor_args, 0);
        app->sp = subprocess_new_input(str_cstr(s), args, cb, app);
        free(args);
    }
}

static void app_mode_select_finish(struct app *app) {
    //TODO: fix code duplication
    app->keystate = KEYSTATE_BASE;
    app->dirty = true;
    if (app->main_view == VIEW_CALENDAR) {
        for (int i = 0; i < app->active_events.len; ++i) {
            struct active_comp *ac = vec_get(&app->active_events, i);
            if (strncmp(ac->code, app->mode_select_code,
                    app->mode_select_code_n) == 0) {
                fprintf(stderr, "selected comp: %s\n",
                    props_get_summary(ac->ci->p));
                app_launch_editor(app, ac);
                break;
            }
        }
    } else if (app->main_view == VIEW_TODO) {
        for (int i = 0; i < app->active_todos.len; ++i) {
            struct active_comp *ac = vec_get(&app->active_todos, i);
            if (strncmp(ac->code, app->mode_select_code,
                    app->mode_select_code_n) == 0) {
                fprintf(stderr, "selected comp: %s\n",
                    props_get_summary(ac->ci->p));
                app_launch_editor(app, ac);
                break;
            }
        }
    } else {
        asrt(false, "unknown mode");
    }
}

static void app_mode_select_append_sym(struct app *app, char sym) {
    app->mode_select_code[app->mode_select_code_n++] = sym;
    if (app->mode_select_code_n >= app->mode_select_len) {
        app_mode_select_finish(app);
    }
}

static void schedule_active_todos(struct app *app) {
    struct vec E = vec_new_empty(sizeof(struct ts_ran));
    for (int i = 0; i < app->cals.len; ++i) {
        struct calendar *cal = vec_get(&app->cals, i);
        for (int j = 0; j < cal->cis.len; ++j) {
            struct comp_inst *ci = vec_get(&cal->cis, j);
            enum prop_status status;
            bool has_status = props_get_status(ci->p, &status);
            if (has_status && status == PROP_STATUS_CONFIRMED) {
                vec_append(&E, &ci->time);
            }
        }
    }

    struct vec T = vec_new_empty(sizeof(struct schedule_todo));
    struct vec acs = vec_new_empty(sizeof(struct active_comp *));
    for (int i = 0; i < app->active_todos.len; ++i) {
        struct active_comp *ac = vec_get(&app->active_todos, i);

        ts start;
        bool has_start = props_get_start(ac->ci->p, &start);
        int est;
        bool has_est = props_get_estimated_duration(ac->ci->p, &est);
        if (has_est) {
            struct schedule_todo st = {
                .start = has_start ? start : -1, .estimated_duration = est };
            vec_append(&T, &st);
            vec_append(&acs, &ac);
        }
    }

    struct ts_ran *G = todo_schedule(app->now, E.len, E.d, T.len, T.d);

    vec_free(&E);
    vec_free(&T);

    for (int i = 0; i < acs.len; ++i) {
        struct active_comp **acp = vec_get(&acs, i);
        struct tobject obj = (struct tobject){
            .time = G[i],
            .type = TOBJECT_TODO,
            .ac = *acp
        };
        tview_try_put(&app->tview, obj);
    }

    vec_free(&acs);
    free(G);
}

static void app_update_views(struct app *app) {
    /* free existing structures */
    tview_finish(&app->tview);
    tview_finish(&app->top_tview);

    /* construct tview time ranges */
    struct tview_spec spec = {
        .base = app->base,
        .type = app->tview_type,
        .n = app->tview_n,
        .h1 = 0, .h2 = 24,
        .zone = app->zone
    };

    tview_init(&app->tview, &spec);
    tview_init_range(&app->top_tview, &spec);
}

static void app_populate_views(struct app *app) {
    /* populate tviews from the active_events list */
    for (int i = 0; i < app->active_events.len; ++i) {
        struct active_comp *ac = vec_get(&app->active_events, i);
        struct tobject obj = {
            .time = ac->ci->time,
            .type = TOBJECT_EVENT,
            .ac = ac
        };
        if (!ac->all_day) {
            tview_try_put(&app->tview, obj);
        } else {
            tview_try_put(&app->top_tview, obj);
        }
    }

    /* populate tviews with scheduled todos */
    schedule_active_todos(app);

    /* calculate layouts */
    tview_update_layout(&app->tview);
    tview_update_layout(&app->top_tview);
}

static void execute_filters(struct app *app, struct active_comp *ac) {
    /* apply builtin filter */
    if (app->builtin_expr && ac->ci->c->type == COMP_TYPE_EVENT) {
        uexpr_fn fn = uexpr_ctx_get_fn(app->builtin_expr_ctx, "filter_0");
        uexpr_ctx_set_handlers(app->builtin_expr_ctx, &uexpr_cal_ac_get,
                &uexpr_cal_ac_set, ac);
        uexpr_eval_fn(app->builtin_expr_ctx, fn);
    }

    /* apply current_filter_fn */
    if (app->current_filter_fn && app->config_expr) {
        uexpr_fn fn = uexpr_ctx_get_fn(app->config_ctx, app->current_filter_fn);
        uexpr_ctx_set_handlers(app->config_ctx, &uexpr_cal_ac_get,
                &uexpr_cal_ac_set, ac);
        uexpr_eval_fn(app->config_ctx, fn);
    }

    /* apply runtime configured filter */
    if (app->expr) {
        uexpr_ctx ctx = uexpr_ctx_create(app->expr);
        uexpr_ctx_set_handlers(ctx, &uexpr_cal_ac_get, &uexpr_cal_ac_set, ac);
        uexpr_eval(ctx);
        uexpr_ctx_destroy(ctx);
    }
}

static bool active_comp_todo_cmp(void *pa, void *pb, void *cl) {
    struct app *app = cl;
    const struct active_comp *a = pa, *b = pb;
    return todo_priority_cmp(app, a->ci, b->ci, true);
}
static void update_active_comps(struct app *app) {
    /* clear lists */
    vec_clear(&app->active_todos);
    vec_clear(&app->active_events);

    struct ts_ran ran_hull =
        ts_ran_hull(app->tview.ran_hull, app->top_tview.ran_hull);

    /* populate the list */
    for (int i = 0; i < app->cals.len; ++i) {
        struct calendar *cal = vec_get(&app->cals, i);
        struct calendar_info *cal_info = vec_get(&app->cal_infos, i);
        if (!cal_info->visible) continue;

        for (int j = 0; j < cal->cis.len; ++j) {
            struct comp_inst *ci = vec_get(&cal->cis, j);

            /* retrieve some properties */
            enum comp_type type = ci->c->type;
            enum prop_status status;
            bool has_status = props_get_status(ci->p, &status);
            enum prop_class class;
            bool has_class = props_get_class(ci->p, &class);

            if (type == COMP_TYPE_TODO
                && has_status
                && (status == PROP_STATUS_COMPLETED
                    || status == PROP_STATUS_CANCELLED)) continue;

            if (has_class && class == PROP_CLASS_PRIVATE &&
                    !app->show_private_events) continue;

            if (type == COMP_TYPE_EVENT && !ts_ran_overlap(ran_hull, ci->time))
                continue;

            struct active_comp ac = {
                .ci = ci,
                .cal_index = i,
                .all_day = (ci->time.to - ci->time.fr) > 60 * 60 * 24,
                .fade = false, .hide = false, .vis = true,
                .cal = cal
            };

            /* execute filter expressions */
            execute_filters(app, &ac);
            if (!ac.vis) continue;

            if (type == COMP_TYPE_EVENT) {
                vec_append(&app->active_events, &ac);
            } else if (type == COMP_TYPE_TODO) {
                vec_append(&app->active_todos, &ac);
            }
        }
    }

    /* sort todos according to priority */
    vec_sort(&app->active_todos, &active_comp_todo_cmp, app);
}

void app_update_active_objects(struct app *app) {
    struct stopwatch sw = sw_start();
    /* clear any modes that depend on current event structures */
    if (app->keystate == KEYSTATE_SELECT) app->keystate = KEYSTATE_BASE;

    /* setup calendar view widgets */
    app_update_views(app);

    /* determine expansion upper bound */
    ts expand_to = app->now;
    for (int i = 0; i < app->tview.n; ++i)
        expand_to = max_ts(expand_to, app->tview.s[i].ran.to);
    for (int i = 0; i < app->top_tview.n; ++i)
        expand_to = max_ts(expand_to, app->top_tview.s[i].ran.to);

    /* expand all comps in all calendars */
    int total_cis = 0;
    for (int i = 0; i < app->cals.len; ++i) {
        struct calendar *cal = vec_get(&app->cals, i);
        calendar_expand_instances_to(cal, expand_to);
        total_cis += cal->cis.len;
    }

    /* update active_comp's derived from the expanded comp_insts's */
    update_active_comps(app);

    /* display the active_comp's on the widgets */
    app_populate_views(app);

    sw_end_print(sw, "app_update_active_objects");
    fprintf(stderr,
        "cis: %d, active_events: %d, active_todos: %d, expand_to: %lld\n",
        total_cis, app->active_events.len, app->active_todos.len, expand_to);
}

static void app_reload_calendars(struct app *app) {
    for (int i = 0; i < app->cals.len; ++i) {
        struct calendar *cal = vec_get(&app->cals, i);
        update_calendar_from_storage(cal, app->zone);
    }
    app_update_active_objects(app);
    app->dirty = true;
}

static void adjust_base(struct app *app, int n) {
    struct simple_date sd = simple_date_from_ts(app->base, app->zone);
    sd.second = sd.minute = sd.hour = 0;
    switch (app->tview_type) {
    case TVIEW_DAYS: sd.day += app->tview.n * n; break;
    case TVIEW_WEEKS: sd.day += 7 * 4 * n; break;
    case TVIEW_MONTHS: sd.day = 1; sd.month = 1; sd.year += n; break;
    case TVIEW_YEARS: sd.day = 1; sd.month = 1; sd.year += n; break;
    default: asrt(false, "wrong tview_type");
    }
    simple_date_normalize(&sd);
    app->base = simple_date_to_ts(sd, app->zone);
}

void app_cmd_editor(struct app *app, FILE *in) {
    struct str in_s = str_empty;
    char c;
    while ((c = getc(in)) != EOF) str_append_char(&in_s, c);
    fclose(in);

    if (!str_any(&in_s)) return;

    FILE *f = fmemopen((void*)in_s.v.d, in_s.v.len, "r");
    asrt(f, "");

    struct edit_spec es;
    edit_spec_init(&es);
    int res = parse_edit_template(f, &es, app->zone);
    fclose(f);
    if (res != 0) {
        fprintf(stderr, "[editor] can't parse edit template.\n");
        app_launch_editor_str(app, &in_s); /* relaunch editor */
        goto cleanup;
    }

    /* generate uid */
    if (es.method == EDIT_METHOD_CREATE && !str_any(&es.uid)) {
        char uid_buf[64];
        generate_uid(uid_buf);
        str_append(&es.uid, uid_buf, strlen(uid_buf));
    }

    /* determine calendar */
    struct calendar *cal;
    if (es.calendar_num > 0) {
        if (es.calendar_num >= 1 && es.calendar_num <= app->cals.len) {
            cal = vec_get(&app->cals, es.calendar_num - 1);
        }
    }
    if (!cal) {
        fprintf(stderr, "[editor] calendar not specified.\n");
        app_launch_editor_str(app, &in_s); /* relaunch editor */
        goto cleanup;
    }

    /* apply edit */
    if (apply_edit_spec_to_calendar(&es, cal) == 0) {
        app_update_active_objects(app);
        app->dirty = true;
    } else {
        fprintf(stderr, "[editor] error: could not save edit\n");
        app_launch_editor_str(app, &in_s); /* relaunch editor */
        goto cleanup;
    }

cleanup:
    edit_spec_finish(&es);
    str_free(&in_s);
}
void app_cmd_reload(struct app *app) {
    app_reload_calendars(app);
}
void app_cmd_activate_filter(struct app *app, int n) {
    const char **k = app->config_fns;
    for (int i = 0; i < n; ++i) {
        if (*k) ++k;
        else break;
    }
    app->current_filter_fn = *k;
    app_update_active_objects(app);
    app->dirty = true;
}

static void application_handle_key(void *ud, uint32_t key, uint32_t mods) {
    struct app *app = ud;
    int n;
    bool shift = mods & 1;
    char sym = key_get_sym(key);
    switch (app->keystate) {
    case KEYSTATE_SELECT:
        if (key_is_gen(key)) {
            app_mode_select_append_sym(app, key_get_sym(key));
        } else {
            app->keystate = KEYSTATE_BASE;
            app->dirty = true;
        }
        break;
    case KEYSTATE_BASE:
        switch (sym) {
        case 'a':
            app->main_view = VIEW_CALENDAR;
            app->dirty = true;
            break;
        case 's':
            app->main_view = VIEW_TODO;
            app->dirty = true;
            break;
        case 'l':
            adjust_base(app, 1);
            app_update_active_objects(app);
            app->dirty = true;
            break;
        case 'h':
            adjust_base(app, -1);
            app_update_active_objects(app);
            app->dirty = true;
            break;
        case 't':
            app->base = ts_get_day_base(app->now, app->zone, true);
            app_update_active_objects(app);
            app->dirty = true;
            break;
        case 'n':
            app_launch_new_editor(app);
            break;
        case 'e':
            app_switch_mode_select(app);
            app->dirty = true;
            break;
        case 'c':
            for (int i = 0; i < app->cals.len; ++i) {
                struct calendar_info *cal_info = vec_get(&app->cal_infos, i);
                cal_info->visible = cal_info->default_visible;
            }
            app_update_active_objects(app);
            app->dirty = true;
            break;
        case 'r':
            app_cmd_reload(app);
            break;
        case 'p':
            app->show_private_events = !app->show_private_events;
            app_update_active_objects(app);
            app->dirty = true;
            break;
        case 'i':
            app->keystate = KEYSTATE_VIEW_SWITCH;
            break;
        case '\0':
            if ((n = key_fn(key)) > 0) { /* numeric key */
                --n; /* key 1->0 .. key 9->8 */
                if (shift) n += 9;
                if (n < app->cals.len) {
                    struct calendar_info *cal_info = vec_get(&app->cal_infos,n);
                    cal_info->visible = !cal_info->visible;
                    app_update_active_objects(app);
                    app->dirty = true;
                }
            }
            if ((n = key_num(key)) > 0) {
                app_cmd_activate_filter(app, n - 1);
            }
            break;
        default:
            asrt(key_is_sym(key), "bad key symbol");
            break;
        }
        break;
    case KEYSTATE_VIEW_SWITCH: {
        bool update = false;
        switch(sym) {
        case 'h':
            app->tview_n = 12;
            app->tview_type = TVIEW_MONTHS;
            update = true;
            break;
        case 'j':
            app->tview_n = 4;
            app->tview_type = TVIEW_WEEKS;
            update = true;
            break;
        case 'k':
            app->tview_n = 7;
            app->tview_type = TVIEW_DAYS;
            update = true;
            break;
        case 'l':
            app->tview_n = 1;
            app->tview_type = TVIEW_DAYS;
            update = true;
            break;
        }
        if (update) {
            app_update_active_objects(app);
            app->dirty = true;
        }
        app->keystate = KEYSTATE_BASE;
        break;
    }
    default:
        asrt(false, "bad keystate");
        break;
    }
}

static void application_handle_child(void *ud, pid_t pid) {
    struct app *app = ud;

    bool sp_expr = app->sp_expr; app->sp_expr = false;

    FILE *f = subprocess_get_result(&app->sp, pid);
    if (!f) return;

    if (sp_expr) {
        // editing expr
        if (app->expr) uexpr_destroy(app->expr);
        app->expr = uexpr_parse(f);
        return;
    }

    app_cmd_editor(app, f);
}

static const char * get_comp_color(void *ptr) {
    /* we only get the color from the base instance of the recur set */
    struct comp *c = ptr;
    return props_get_color(&c->p);
}

void app_init(struct app *app, struct application_options opts,
        struct backend *backend) {
    struct stopwatch sw = sw_start();

    *app = (struct app){
        .cals = VEC_EMPTY(sizeof(struct calendar)),
        .cal_infos = VEC_EMPTY(sizeof(struct calendar_info)),
        .active_events = VEC_EMPTY(sizeof(struct active_comp)),
        .active_todos = VEC_EMPTY(sizeof(struct active_comp)),
        .main_view = VIEW_CALENDAR,
        .keystate = KEYSTATE_BASE,
        .show_private_events = opts.show_private_events,
        .tview_type = TVIEW_DAYS,
        .tview = (struct tview){ .n = -1, .s = NULL },
        .top_tview = (struct tview){ .n = -1, .s = NULL },
        .tview_n = 7,
        .sp = NULL,
        .sp_expr = false,
        .window_width = -1, .window_height = -1,
        .backend = backend,
        .interactive = backend->vptr->is_interactive(backend),
        .editor_args = VEC_EMPTY(sizeof(struct str)),
        .expr = NULL,
        .builtin_expr = NULL,
        .config_expr = NULL,
        .config_ctx = NULL,
        .config_fns = NULL,
        .current_filter_fn = NULL,
    };

    /* load all uexpr stuff */
    const char *builtin_expr = "{"
        "def(filter_0, {"
            "($st % [ tentative, cancelled ]) & let($fade, a=a);"
            "let($hide, ($clas = private) & ~$show_priv)"
        "})"
        "}";
    FILE *f = fmemopen((void*)builtin_expr, strlen(builtin_expr), "r");
    app->builtin_expr = uexpr_parse(f);
    fclose(f);
    asrt(app->builtin_expr, "builtin_expr parsing failed");
    app->builtin_expr_ctx = uexpr_ctx_create(app->builtin_expr);
    uexpr_eval(app->builtin_expr_ctx);

    if (opts.config_file) {
        f = fopen(opts.config_file, "r");
        app->config_expr = uexpr_parse(f);
        fclose(f);
        if (app->config_expr) {
            app->config_ctx = uexpr_ctx_create(app->config_expr);
            uexpr_eval(app->config_ctx);
            app->config_fns = uexpr_get_all_fns(app->config_ctx);
            const char **k = app->config_fns;
            while (*k) {
                fprintf(stderr, "fn: %s\n", *k);
                k++;
            }
        } else {
            fprintf(stderr, "WARNING: could not parse config script!\n");
        }
    }

    // TODO: state.view_days = opts.view_days;

    const char *editor_buffer = "", *term_buffer = "st";
    const char *editor_env = getenv("EDITOR");
    if (editor_env) editor_buffer = editor_env;

    if (opts.editor) editor_buffer = opts.editor;
    if (opts.terminal) term_buffer = opts.terminal;

    asrt(editor_buffer[0], "please set editor!");
    asrt(term_buffer[0], "please set terminal emulator!");

    struct str s;
    s = str_new_from_cstr(term_buffer); vec_append(&app->editor_args, &s);
    s = str_new_from_cstr(term_buffer); vec_append(&app->editor_args, &s);
    s = str_new_from_cstr(editor_buffer); vec_append(&app->editor_args, &s);
    s = str_new_from_cstr("{file}"); vec_append(&app->editor_args, &s);

    fprintf(stderr, "editor command: %s, term command: %s\n",
        editor_buffer, term_buffer);

    app->zone = cal_timezone_new("Europe/Budapest");
    app->now = ts_now();
    app->base = ts_get_day_base(app->now, app->zone, true);

    for (int i = 0; i < opts.argc; i++) {
        struct calendar cal;
        struct calendar_info cal_info;

        cal_info.visible = cal_info.default_visible =
            opts.default_vis & (1U << i);

        /* init */
        calendar_init(&cal);

        /* read */
        cal.storage = str_empty;
        str_append(&cal.storage, opts.argv[i], strlen(opts.argv[i]));
        fprintf(stderr, "loading %s\n", str_cstr(&cal.storage));
        update_calendar_from_storage(&cal, app->zone);

        /* set metadata */
        // TODO: set calendar name from storage name
        // if (!cal->name) cal->name = str_dup(cal->storage);
        // trim_end(cal->name);

        /* calculate most frequent color */
        const char *fc = most_frequent(&cal.comps_vec, &get_comp_color);
        uint32_t color = fc ? lookup_color(fc, strlen(fc)) : 0;
        if (!color) color = 0xFF20D0D0;
        cal_info.color = color;

        vec_append(&app->cals, &cal);
        vec_append(&app->cal_infos, &cal_info);
    }

    app_update_active_objects(app);

    app->backend->vptr->set_callbacks(app->backend,
        &render_application,
        &application_handle_key,
        &application_handle_child,
        app
    );

    app->tr = text_renderer_new("Monospace 8");

    app->dirty = true;
    sw_end_print(sw, "initialization");
}

void app_main(struct app *app) {
    app->backend->vptr->run(app->backend);
}

void app_finish(struct app *app) {
    for (int i = 0; i < app->cals.len; ++i) {
        struct calendar *cal = vec_get(&app->cals, i);
        calendar_finish(cal);
    }
    vec_free(&app->cals);
    vec_free(&app->cal_infos);

    for (int i = 0; i < app->editor_args.len; ++i) {
        struct str *s = vec_get(&app->editor_args, i);
        str_free(s);
    }
    vec_free(&app->editor_args);

    tview_finish(&app->tview);
    tview_finish(&app->top_tview);

    vec_free(&app->active_events);
    vec_free(&app->active_todos);

    cal_timezone_destroy(app->zone);
    text_renderer_free(app->tr);

    if (app->config_ctx) uexpr_ctx_destroy(app->config_ctx);
    if (app->builtin_expr_ctx) uexpr_ctx_destroy(app->builtin_expr_ctx);
    if (app->builtin_expr) uexpr_destroy(app->builtin_expr);
    if (app->expr) uexpr_destroy(app->expr);
    free(app->config_fns);

    app->backend->vptr->destroy(app->backend);
}
