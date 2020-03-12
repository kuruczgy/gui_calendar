#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "application.h"
#include "util.h"
#include "algo.h"
#include "keyboard.h"
#include "backend.h"
#include "render.h"
#include "editor.h"

struct state state = { 0 };

static void switch_mode_select() {
    struct key_gen g;

    if (state.keystate == KEYSTATE_SELECT) return;
    state.mode_select_code_n = 0;
    state.keystate = KEYSTATE_SELECT;

    if (state.main_view == VIEW_CALENDAR) {
        key_gen_init(state.active_event_n, &g);
        state.mode_select_len = g.k;
        for (int i = 0; i < state.active_event_n; ++i) {
            const char *code = key_gen_get(&g);
            assert(code, "not enough codes");
            strcpy(state.active_events[i].tag.code, code);
        }
    } else if (state.main_view == VIEW_TODO) {
        struct key_gen g;
        key_gen_init(state.active_todo_n, &g);
        state.mode_select_len = g.k;
        for (int i = 0; i < state.active_todo_n; ++i) {
            const char *code = key_gen_get(&g);
            assert(code, "not enough codes");
            strcpy(state.active_todos_tag[i].code, code);
        }
    } else {
        assert(false, "unknown view");
    }
}

static void print_event_template_callback(void *cl, FILE *f) {
    struct active_event *aev = cl;
    print_event_template(f, &aev->ers->base, aev->ers->uid,
            (time_t)aev->time.fr, state.zone->impl);
}
static void print_todo_template_callback(void *ud, FILE *f) {
    print_todo_template(f, (struct todo *)ud, state.zone->impl);
}
static int get_first_visible_cal_index() {
    for (int i = 0; i < state.n_cal; ++i) {
        if (state.cal_info[i].visible) {
            return i;
        }
    }
    assert(state.n_cal > 0, "no calendars");
    return 0;
}
static void print_new_event_template_callback(void *cl, FILE *f) {
    int cal = get_first_visible_cal_index();
    print_new_event_template(f, state.zone->impl, cal + 1);
}
static void print_new_todo_template_callback(void *cl, FILE *f) {
    int cal = get_first_visible_cal_index();
    print_new_todo_template(f, state.zone->impl, cal + 1);
}
static void launch_event_editor(struct active_event *aev) {
    if (!state.sp) {
        state.sp_calendar = aev->tag.cal;
        state.sp = subprocess_new_input(state.editor[0],
            state.editor + 1, &print_event_template_callback, (void *)aev);
    }
}
static void launch_todo_editor(struct todo *td, struct calendar *cal) {
    if (!state.sp) {
        state.sp_calendar = cal;
        state.sp = subprocess_new_input(state.editor[0],
            state.editor + 1, &print_todo_template_callback, (void *)td);
    }
}
static void launch_new_event_editor() {
    if (!state.sp) {
        state.sp_calendar = NULL;
        state.sp = subprocess_new_input(state.editor[0],
            state.editor + 1, &print_new_event_template_callback, NULL);
    }
}
static void launch_new_todo_editor() {
    if (!state.sp) {
        state.sp_calendar = NULL;
        state.sp = subprocess_new_input(state.editor[0],
            state.editor + 1, &print_new_todo_template_callback, NULL);
    }
}

static void mode_select_finish() {
    state.keystate = KEYSTATE_BASE;
    state.dirty = true;
    if (state.main_view == VIEW_CALENDAR) {
        for (int i = 0; i < state.active_event_n; ++i) {
            if (strncmp(
                    state.active_events[i].tag.code, state.mode_select_code,
                    state.mode_select_code_n) == 0) {
                fprintf(stderr, "selected event: %s\n",
                    state.active_events[i].ev->summary);
                launch_event_editor(&state.active_events[i]);
                break;
            }
        }
    } else if (state.main_view == VIEW_TODO) {
        for (int i = 0; i < state.active_todo_n; ++i) {
            if (strncmp(
                    state.active_todos_tag[i].code, state.mode_select_code,
                    state.mode_select_code_n) == 0) {
                fprintf(stderr, "selected todo: %s\n",
                        state.active_todos[i]->summary);
                launch_todo_editor(
                        state.active_todos[i], state.active_todos_tag[i].cal);
                break;
            }
        }
    } else {
        assert(false, "unknown mode");
    }
}

static void mode_select_append_sym(char sym) {
    state.mode_select_code[state.mode_select_code_n++] = sym;
    if (state.mode_select_code_n >= state.mode_select_len) {
        mode_select_finish();
    }
}

struct hashmap_counter_cl {
    int cnt;
    int (*cb)(void *data, void *cl);
    void *cb_cl;
};
static int hashmap_counter(void *_cl, void *data) {
    struct hashmap_counter_cl *cl = _cl;
    cl->cnt += cl->cb(data, cl->cb_cl);
    return MAP_OK;
}
static int events_in_range_cnt(void *data, void *_cl) {
    struct event_recur_set *ers = data;
    struct ts_ran *ran = _cl;
    int cnt = 0;
    for (int i = 0; i < (ers->max != 0 ? ers->n : 1); ++i) {
        time_t start, end;
        event_recur_set_get(ers, i, &start, &end);
        if (ran->fr == -1) {
            ++cnt;
        } else if (ts_ran_overlap(*ran, (struct ts_ran){(ts)start,(ts)end})) {
            ++cnt;
        }
    }
    return cnt;
}
struct create_active_events_cl {
    int max;
    struct ts_ran ran;
    struct calendar *cal;
};
static int create_active_events(void *_cl, void *data) {
    struct create_active_events_cl *cl = _cl;
    struct event_recur_set *ers = data;
    for (int i = 0; i < (ers->max != 0 ? ers->n : 1); ++i) {
        time_t start, end;
        struct event *ev = event_recur_set_get(ers, i, &start, &end);
        if (ts_ran_overlap(cl->ran, (struct ts_ran){ (ts)start, (ts)end })) {
            assert(state.active_event_n < cl->max, "too many active_events");
            state.active_events[state.active_event_n++] =
                    (struct active_event){
                .ers = ers,
                .time = (struct ts_ran){ (ts)start, (ts)end },
                .ev = ev,
                .tag = (struct event_tag){ .cal = cl->cal }
            };
        }
    }
    return MAP_OK;
}
struct get_event_ranges_cl {
    struct ts_ran *E;
    int n, max;
};
static int get_event_ranges(void *_cl, void *data) {
    struct get_event_ranges_cl *cl = _cl;
    struct event_recur_set *ers = data;
    for (int i = 0; i < (ers->max != 0 ? ers->n : 1); ++i) {
        time_t start, end;
        struct event *ev = event_recur_set_get(ers, i, &start, &end);
        if (ev->status != ICAL_STATUS_CONFIRMED) continue;
        assert(cl->n < cl->max, "too many ranges");
        cl->E[cl->n++] = (struct ts_ran){ (ts)start, (ts)end };
    }
    return MAP_OK;
}

static void iter_visible_cals(int (*cb)(void*, void*), void* cl) {
    for (int i = 0; i < state.n_cal; ++i) {
        if (state.cal_info[i].visible) {
            hashmap_iterate(state.cal[i].event_sets, cb, cl);
        }
    }
}
static void iter_all_cals(int (*cb)(void*, void*), void* cl) {
    for (int i = 0; i < state.n_cal; ++i) {
        hashmap_iterate(state.cal[i].event_sets, cb, cl);
    }
}

static struct ts_ran * schedule_active_todos() {
    /* count all events */
    struct ts_ran ran = { -1, -1 };
    struct hashmap_counter_cl ccl = { 0, &events_in_range_cnt, &ran };
    iter_all_cals(&hashmap_counter, &ccl);

    struct ts_ran *E = malloc_check(sizeof(struct ts_ran) * ccl.cnt);
    struct get_event_ranges_cl ercl = { E, 0, ccl.cnt };
    iter_all_cals(&get_event_ranges, &ercl);

    struct ts_ran *G = todo_schedule(state.now, ercl.n, E,
            state.active_todo_n, state.active_todos);

    free(E);

    /* debug */
    char buf1[32], buf2[32];
    fprintf(stderr, "> todo schedule\n");
    for (int i = 0; i < state.active_todo_n; ++i) {
        struct todo *td = state.active_todos[i];
        format_simple_date(buf1, 32,
            simple_date_from_ts(G[i].fr, state.zone->impl));
        format_simple_date(buf2, 32,
            simple_date_from_ts(G[i].to, state.zone->impl));
        fprintf(stderr, "- todo[%d] %s - %s\tsum: `%s`\n",
            i, buf1, buf2, td->summary);
    }

    return G;
}

static void update_views() {
    /* free existing structures */
    free(state.active_events);
    destruct_tview(&state.tview);
    destruct_tview(&state.top_tview);

    /* construct tview time ranges */
    struct tview_spec spec = {
        .base = state.base,
        .type = state.tview_type,
        .n = state.tview_n,
        .h1 = 0, .h2 = 24,
        .zone = state.zone->impl
    };
    init_tview(&state.tview, &spec);
    init_tview_range(&state.top_tview, &spec);

    /* count overlapping events */
    struct ts_ran ran = { spec.base, spec.to };
    struct hashmap_counter_cl ccl = { 0, &events_in_range_cnt, &ran };
    iter_visible_cals(hashmap_counter, &ccl);

    /* schedule todos */
    struct ts_ran *G = schedule_active_todos();
    int max_n = ccl.cnt + state.active_todo_n;

    /* allocate slices */
    init_tview_slices(&state.tview, max_n);
    init_tview_slices(&state.top_tview, max_n);
    state.active_events = malloc_check(sizeof(struct active_event) * max_n);
    state.active_event_n = 0;

    /* create active_event structs */
    struct create_active_events_cl crcl = {
        .max = ccl.cnt,
        .ran = ran
    };
    for (int i = 0; i < state.n_cal; ++i) {
        if (state.cal_info[i].visible) {
            crcl.cal = &state.cal[i];
            hashmap_iterate(state.cal[i].event_sets,
                &create_active_events, &crcl);
        }
    }

    /* populate tviews from the active_events list */
    for (int i = 0; i < state.active_event_n; ++i) {
        struct active_event *aev = &state.active_events[i];
        struct tobject obj = (struct tobject){
            .time = aev->time,
            .type = TOBJECT_EVENT,
            .ers = aev->ers, // TODO: not much point in this...
            .ev = aev->ev,
            .aev = aev
        };
        if (!aev->ev->all_day) {
            tview_try_put(&state.tview, obj);
        } else {
            tview_try_put(&state.top_tview, obj);
        }
    }

    /* populate tviews with scheduled todos */
    for (int i = 0; i < state.active_todo_n; ++i) {
        struct tobject obj = (struct tobject){
            .time = G[i],
            .type = TOBJECT_TODO,
            .td = state.active_todos[i]
        };
        tview_try_put(&state.tview, obj);
    }

    free(G);

    /* calculate layouts */
    tview_update_layout(&state.tview);
    tview_update_layout(&state.top_tview);

    /* debug info */
    struct tview *tv = &state.tview;
    char buf1[32], buf2[32];
    format_simple_date(buf1, 32,
        simple_date_from_ts(state.base, state.zone->impl));
    fprintf(stderr, "=== current view ===\n");
    fprintf(stderr, "state.base: %s;\t", buf1);
    switch (state.tview_type) {
    case TVIEW_DAYS: fprintf(stderr, "DAYS"); break;
    case TVIEW_WEEKS: fprintf(stderr, "WEEKS"); break;
    case TVIEW_MONTHS: fprintf(stderr, "MONTHS"); break;
    case TVIEW_YEARS: fprintf(stderr, "YEARS"); break;
    default: assert(false, "wrong tview_type");
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "min_content: ");
    print_simple_dur(stderr, simple_dur_from_int((int)tv->min_content));
    fprintf(stderr, "; max_content: ");
    print_simple_dur(stderr, simple_dur_from_int((int)tv->max_content));
    fprintf(stderr, "; max_len: ");
    print_simple_dur(stderr, simple_dur_from_int((int)tv->max_len));
    fprintf(stderr, "\n");

    for (int i = 0; i < tv->n; ++i) {
        struct tslice *tsl = &tv->s[i];
        format_simple_date(buf1, 32,
            simple_date_from_ts(tsl->ran.fr, state.zone->impl));
        format_simple_date(buf2, 32,
            simple_date_from_ts(tsl->ran.to, state.zone->impl));
        fprintf(stderr, "- s[%d]: %s - %s\t%d/%d objs\n",
            i, buf1, buf2, tsl->n, tsl->max);
    }
}

/* provides a partial ordering over todos */
static int todo_priority_cmp(const struct todo *a, const struct todo *b) {
    /* -1: a first, 1: b first, 0: equal */
    bool a_started = a->start.timestamp == -1
        || a->start.timestamp <= state.now;
    bool b_started = b->start.timestamp == -1
        || b->start.timestamp <= state.now;

    bool a_inprocess = a->status == ICAL_STATUS_INPROCESS;
    bool b_inprocess = b->status == ICAL_STATUS_INPROCESS;

    const int sh = 60 * 10; // 10m
    bool a_short = a->estimated_duration != -1 && a->estimated_duration <= sh;
    bool b_short = b->estimated_duration != -1 && b->estimated_duration <= sh;

    if (a_inprocess != b_inprocess) {
        if (a_inprocess) return -1;
        else return 1;
    } else if (a_short != b_short) {
        if (a_short) return -1;
        else return 1;
    } else if (a->due.timestamp > 0 || b->due.timestamp > 0) {
        if (a->due.timestamp < 0) return 1;
        else if (b->due.timestamp < 0) return -1;
        return a->due.timestamp - b->due.timestamp;
    }
    return 0;
}
static int todo_tag_cmp(const void *pa, const void *pb) {
    const struct todo_tag *a = pa, *b = pb;
    return todo_priority_cmp(a->td, b->td);
}

struct todo_filterer {
    int n;
    struct calendar *cal;
};
static int count_active_todos(void *f_p, void *t_p) {
    struct todo *td = t_p;
    struct todo_filterer *f = f_p;
    if (td->status != ICAL_STATUS_COMPLETED &&
        td->status != ICAL_STATUS_CANCELLED &&
        (state.show_private_events || td->clas != ICAL_CLASS_PRIVATE)) {
        if (state.active_todos) {
            state.active_todos[f->n] = td;
            state.active_todos_tag[f->n] = (struct todo_tag) {
                .td = td,
                .cal = f->cal
            };
        }
        f->n++;
    }
    return MAP_OK;
}

static void update_active_todos() {
    /* free up existing structures */
    if (state.active_todos) {
        free(state.active_todos);
        free(state.active_todos_tag);
        state.active_todos = NULL;
        state.active_todos_tag = NULL;
    }

    /* count the matching todos */
    struct todo_filterer f = { .n = 0 };
    for (int i = 0; i < state.n_cal; ++i) {
        if (state.cal_info[i].visible) {
            hashmap_iterate(state.cal[i].todos, count_active_todos, &f);
        }
    }
    int n = f.n;

    /* use the counts to allocate data structures */
    state.active_todos = malloc(sizeof(struct todo*) * n);
    state.active_todos_tag = malloc(sizeof(struct todo_tag) * n);
    state.active_todo_n = n;

    /* populate the data structures */
    f.n = 0;
    for (int i = 0; i < state.n_cal; ++i) {
        if (state.cal_info[i].visible) {
            f.cal = &state.cal[i];
            hashmap_iterate(state.cal[i].todos, count_active_todos, &f);
        }
    }
    assert(n == f.n, "todo count mismatch");
    qsort(state.active_todos_tag, state.active_todo_n,
            sizeof(struct todo_tag), &todo_tag_cmp);
    for (int i = 0; i < state.active_todo_n; ++i)
        state.active_todos[i] = state.active_todos_tag[i].td;
}

static void update_active_objects() {
    /* clear any modes that depend on current event structures */
    if (state.keystate == KEYSTATE_SELECT) state.keystate = KEYSTATE_BASE;

    update_active_todos();

    /* this depends on update_active_todos */
    update_views();
}

static void reload_calendars() {
    for (int i = 0; i < state.n_cal; i++) {
        update_calendar_from_storage(&state.cal[i], state.zone->impl);
    }
    update_active_objects();
    state.dirty = true;
}

static void adjust_base(int n) {
    struct simple_date sd = simple_date_from_ts(state.base, state.zone->impl);
    sd.second = sd.minute = sd.hour = 0;
    switch (state.tview_type) {
    case TVIEW_DAYS: sd.day += state.tview.n * n; break;
    case TVIEW_WEEKS: sd.day += 7 * 4 * n; break;
    case TVIEW_MONTHS: sd.day = 1; sd.month = 1; sd.year += n; break;
    case TVIEW_YEARS: sd.day = 1; sd.month = 1; sd.year += n; break;
    default: assert(false, "wrong tview_type");
    }
    simple_date_normalize(&sd);
    state.base = simple_date_to_ts(sd, state.zone->impl);
}

static void application_handle_key(void *ud, uint32_t key, uint32_t mods) {
    int n;
    char sym = key_get_sym(key);
    switch (state.keystate) {
    case KEYSTATE_SELECT:
        if (key_is_gen(key)) {
            mode_select_append_sym(key_get_sym(key));
        } else {
            state.keystate = KEYSTATE_BASE;
            state.dirty = true;
        }
        break;
    case KEYSTATE_BASE:
        switch (sym) {
        case 'a':
            state.main_view = VIEW_CALENDAR;
            state.dirty = true;
            break;
        case 's':
            state.main_view = VIEW_TODO;
            state.dirty = true;
            break;
        case 'l':
            adjust_base(1);
            update_active_objects();
            state.dirty = true;
            break;
        case 'h':
            adjust_base(-1);
            update_active_objects();
            state.dirty = true;
            break;
        case 't':
            state.base = get_day_base(state.zone->impl, true);
            update_active_objects();
            state.dirty = true;
            break;
        case 'n':
            if (!state.sp) {
                if (state.main_view == VIEW_CALENDAR) {
                    launch_new_event_editor();
                } else if (state.main_view == VIEW_TODO) {
                    launch_new_todo_editor();
                }
            }
            break;
        case 'e':
            switch_mode_select();
            state.dirty = true;
            break;
        case 'c':
            for (int i = 0; i < state.n_cal; ++i) {
                state.cal_info[i].visible = state.cal_default_visible[i];
            }
            update_active_objects();
            state.dirty = true;
            break;
        case 'r':
            reload_calendars();
            break;
        case 'p':
            state.show_private_events = !state.show_private_events;
            update_active_objects();
            state.dirty = true;
            break;
        case 'i':
            state.keystate = KEYSTATE_VIEW_SWITCH;
            break;
        case '\0':
            if ((n = key_num(key)) >= 0) { /* numeric key */
                --n; /* key 1->0 .. key 9->8 */
                if (n < state.n_cal) {
                    state.cal_info[n].visible = ! state.cal_info[n].visible;
                    update_active_objects();
                    state.dirty = true;
                }
            }
            break;
        default:
            assert(key_is_sym(key), "bad key symbol");
            break;
        }
        break;
    case KEYSTATE_VIEW_SWITCH: {
        bool update = false;
        switch(sym) {
        case 'h':
            state.tview_n = 12;
            state.tview_type = TVIEW_MONTHS;
            update = true;
            break;
        case 'j':
            state.tview_n = 4;
            state.tview_type = TVIEW_WEEKS;
            update = true;
            break;
        case 'k':
            state.tview_n = 7;
            state.tview_type = TVIEW_DAYS;
            update = true;
            break;
        case 'l':
            state.tview_n = 1;
            state.tview_type = TVIEW_DAYS;
            update = true;
            break;
        }
        if (update) {
            update_active_objects();
            state.dirty = true;
        }
        state.keystate = KEYSTATE_BASE;
        break;
    }
    default:
        assert(false, "bad keystate");
        break;
    }
}

static void application_handle_child(void *ud, pid_t pid) {
    int res;
    struct calendar *cal = state.sp_calendar;
    FILE *f = subprocess_get_result(&(state.sp), pid);
    if (!state.sp) state.sp_calendar = NULL;
    if (!f) return;

    struct edit_spec es;
    init_edit_spec(&es);
    res = parse_edit_template(f, &es, state.zone->impl);

    if (res != 0) {
        fprintf(stderr, "[editor] can't parse edit template. aborting edit!\n");
        return;
    }

    if (es.method == EDIT_METHOD_CREATE) {
        assert(!es.uid, "uid already exists");
        char uid_buf[64];
        generate_uid(uid_buf);
        es.uid = str_dup(uid_buf);
    }

    if (check_edit_spec(&es) != 0) {
        fprintf(stderr, "[editor] edit_spec failed sanity check."
                "aborting edit!\n");
        return;
    }

    if (es.method == EDIT_METHOD_CREATE && es.calendar_num > 0) {
        if (es.calendar_num >= 1 && es.calendar_num <= state.n_cal) {
            cal = &state.cal[es.calendar_num - 1];
        }
    }

    if (!cal) {
        fprintf(stderr, "[editor] calendar not specified. aborting edit!\n");
        return;
    }

    if (apply_edit_spec_to_calendar(&es, cal) == 0) {
        if (es.type == COMP_TYPE_EVENT) {
            update_active_objects();
        } else if (es.type == COMP_TYPE_TODO) {
            update_active_objects();
        } else {
            assert(false, "");
        }
        state.dirty = true;
    } else {
        fprintf(stderr, "[editor] error: could not save edit\n");
    }
}

static char * get_event_recur_set_color(void * p) {
    //TODO: we only get the color from the base instance of the recur set
    struct event_recur_set *ers = p;
    return ers->base.color_str;
}

int application_main(struct application_options opts, struct backend *backend) {
    state = (struct state){
        .n_cal = 0,
        .window_width = -1,
        .window_height = -1,
        .sp = NULL,
        .active_events = NULL,
        .active_todos = NULL,
        .show_private_events = false,
        .keystate = KEYSTATE_BASE,
        .now = time(NULL),
        .interactive = backend->vptr->is_interactive(backend),
        .tview = (struct tview){ .n = -1, .s = NULL },
        .top_tview = (struct tview){ .n = -1, .s = NULL },
        .tview_n = 7,
        .tview_type = TVIEW_DAYS,
    };

    // TODO: state.view_days = opts.view_days;

    for (int i = 0; i < 16; ++i) {
        /* init cal_info */
        state.cal_info[i] = (struct calendar_info) {
            .visible = false
        };
        state.cal_default_visible[i] = false;
    }

    const char *editor_buffer, *term_buffer = "st";
    const char *editor_env = getenv("EDITOR");
    if (editor_env) editor_buffer = editor_env;

    state.show_private_events = opts.show_private_events;
    for (int i = 0; i < 16; ++i) {
        bool v = opts.default_vis & (1U << i);
        state.cal_info[i].visible = v;
        state.cal_default_visible[i] = v;
    }

    if (opts.editor) editor_buffer = opts.editor;
    if (opts.terminal) term_buffer = opts.terminal;

    assert(editor_buffer[0], "please set editor!");
    assert(term_buffer[0], "please set terminal emulator!");
    const char *editor[] = { term_buffer, term_buffer,
        editor_buffer, "{file}", NULL };
    fprintf(stderr, "editor command: %s, term command: %s\n",
        editor_buffer, term_buffer);
    state.editor = editor;

    state.zone = new_timezone("Europe/Budapest");
    state.base = get_day_base(state.zone->impl, true);

    for (int i = 0; i < opts.argc; i++) {
        struct calendar *cal = &state.cal[state.n_cal];

        /* init */
        init_calendar(cal);

        /* read */
        cal->storage = str_dup(opts.argv[i]);
        fprintf(stderr, "loading %s\n", cal->storage);
        update_calendar_from_storage(cal, state.zone->impl);

        /* set metadata */
        if (!cal->name) cal->name = str_dup(cal->storage);
        trim_end(cal->name);

        /* calculate most frequent color */
        const char *fc = most_frequent(
            cal->event_sets, &get_event_recur_set_color);
        uint32_t color = lookup_color(fc);
        if (!color) color = 0xFF20D0D0;
        state.cal_info[state.n_cal].color = color;

        /* next */
        if (++state.n_cal >= 16) break;
    }

    update_active_objects();

    state.backend = backend;
    state.backend->vptr->set_callbacks(state.backend,
        &render_application,
        &application_handle_key,
        &application_handle_child,
        NULL
    );

    state.tr = text_renderer_new("Monospace 8");

    state.dirty = true;
    backend->vptr->run(backend);

    for (int i = 0; i < state.n_cal; i++) {
        destruct_calendar(&state.cal[i]);
    }
    free_timezone(state.zone);
    text_renderer_free(state.tr);

    backend->vptr->destroy(backend);
    return 0;
}
