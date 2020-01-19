#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "application.h"
#include "util.h"
#include "keyboard.h"
#include "backend.h"
#include "render.h"

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
            aev->start.timestamp);
}
static void print_todo_template_callback(void *ud, FILE *f) {
    print_todo_template(f, (struct todo *)ud);
}
static void launch_event_editor(struct active_event *aev) {
    if (!state.sp) {
        state.sp_calendar = aev->tag.cal;
        state.sp_type = ICAL_VEVENT_COMPONENT;
        state.sp = subprocess_new_input(state.editor[0],
                state.editor + 1, &print_event_template_callback, (void *)aev);
    }
}
static void launch_todo_editor(struct todo *td, struct calendar *cal) {
    if (!state.sp) {
        state.sp_calendar = cal;
        state.sp_type = ICAL_VTODO_COMPONENT;
        state.sp = subprocess_new_input(state.editor[0],
                state.editor + 1, &print_todo_template_callback, (void *)td);
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

struct event_filterer {
    struct active_event *list;
    int n;
    time_t from, to;
    bool show_priv;
    struct calendar *cal; /* used for assigning tags */
};
static void init_event_filterer(struct event_filterer *f) {
    f->list = NULL;
    f->n = 0;
    f->from = -1;
    f->to = -1;
    f->show_priv = false;
}
static int filter_event_sets(void *cl, void *e_p) {
    struct event_filterer *f = cl;
    struct event_recur_set *ers = e_p;
    for (int i = 0; i < (ers->max != 0 ? ers->n : 1); ++i) {
        time_t start, end;
        struct event *ev = event_recur_set_get(ers, i, &start, &end);
        if (f->from != -1 && !interval_overlap(f->from, f->to, start, end))
            continue;
        if (!f->show_priv && ev->clas == ICAL_CLASS_PRIVATE) continue;

        if (f->list) f->list[f->n] = (struct active_event){
            .ers = ers,
            .start = date_from_timet(start, state.zone->impl),
            .end = date_from_timet(end, state.zone->impl),
            .ev = ev,
            .tag = (struct event_tag){ .cal = f->cal }
        };
        f->n++;
    }
    return MAP_OK;
}

static void discard_temp_structures() {
    // discard existing structures
    if (state.active_events) {
        free(state.active_events);
        free(state.active_event_layouts);
    }
}

static void update_active_events() {
    discard_temp_structures();

    /* clear any modes that depend on current event structures */
    if (state.keystate == KEYSTATE_SELECT) state.keystate = KEYSTATE_BASE;

    /* count active events */
    struct event_filterer filterer;
    init_event_filterer(&filterer);
    filterer.from = state.base,
    filterer.to = state.base + state.view_days * 3600 * 24;
    filterer.show_priv = true;
    for (int i = 0; i < state.n_cal; i++) {
        if (state.cal_info[i].visible) {
            hashmap_iterate(state.cal[i].event_sets,
                    filter_event_sets, &filterer);
        }
    }

    /* allocate memory based on counts */
    state.active_event_n = filterer.n;
    state.active_events =
        malloc_check(sizeof(struct active_event) * state.active_event_n);
    int aelmax = state.active_event_n * (state.view_days + 1);
    state.active_event_layouts =
        malloc_check(sizeof(struct active_event_layout) * aelmax);
    state.active_event_layout_n = 0;

    /* populate the lists */
    filterer.n = 0;
    filterer.list = state.active_events;
    for (int i = 0; i < state.n_cal; i++) {
        filterer.cal = &state.cal[i];
        if (state.cal_info[i].visible) {
            hashmap_iterate(state.cal[i].event_sets,
                    filter_event_sets, &filterer);
        }
    }

    /* create the 2D layout */
    struct layout_event *la =
        malloc(sizeof(struct layout_event) * state.active_event_n);
    int min_sec = 3600 * 24 + 1, max_sec = -1;
    state.all_day_max_n = 0;
    for (int d = -1; d < state.view_days; d++) {
        // TODO: what if day not 24h long?
        time_t slot_base = d == -1 ? state.base : state.base + 3600 * 24 * d;
        time_t slot_len = d == -1 ? 3600 * 24 * state.view_days : 3600 * 24;
        int l = 0;
        for (int k = 0; k < state.active_event_n; k++) {
            struct active_event *aev = &state.active_events[k];
            if (d == -1) { /* all day events */
                if (!aev->ev->all_day) continue;
            } else {
                if (! interval_overlap(slot_base, slot_base + slot_len,
                        aev->start.timestamp, aev->end.timestamp)) continue;
            }
            time_t start_sec = max(0, aev->start.timestamp - slot_base);
            time_t end_sec = min(slot_len, aev->end.timestamp - slot_base);
            if (d != -1 && aev->ev->all_day &&
                    start_sec == 0 && end_sec == slot_len) {
                continue;
            }

            assert(start_sec < end_sec, "bad layout start and end times");
            assert(l < state.active_event_n, "too many events");
            la[l++] = (struct layout_event){
                .start = start_sec,
                .end = end_sec,
                .idx = k
            };
        }
        calendar_layout(la, l);
        for (int i = 0; i < l; ++i) {
            struct layout_event layout = la[i];
            assert(state.active_event_layout_n < aelmax, "");
            state.active_event_layouts[state.active_event_layout_n++] =
                    (struct active_event_layout){
                .aev = &state.active_events[layout.idx],
                .start = layout.start,
                .end = layout.end,
                .max_n = layout.max_n,
                .col = layout.col,
                .day_i = d
            };
            if (d == -1) {
                state.all_day_max_n = max(state.all_day_max_n, layout.max_n);
            } else {
                min_sec = min(min_sec, layout.start);
                max_sec = max(max_sec, layout.end);
            }
        }
    }
    free(la);

    // do event fitting stuff
    if (min_sec > max_sec) min_sec = 0, max_sec = 3600 * 24;
    range r;
    r.from = max(0, min_sec / 3600);
    r.to = min(24, (max_sec + 3599) / 3600);
    if (r.to <= r.from) r = (range){ 0, 24 };
    state.hours_view_events = r;
    state.hours_view_manual = (range){ -1, -1 };
    update_actual_fit();
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
        hashmap_iterate(state.cal[i].todos, count_active_todos, &f);
    }
    int n = f.n;

    /* use the counts to allocate data structures */
    state.active_todos = malloc(sizeof(struct todo*) * n);
    state.active_todos_tag = malloc(sizeof(struct todo_tag) * n);
    state.active_todo_n = n;

    /* populate the data structures */
    f.n = 0;
    for (int i = 0; i < state.n_cal; ++i) {
        f.cal = &state.cal[i];
        hashmap_iterate(state.cal[i].todos, count_active_todos, &f);
    }
    assert(n == f.n, "todo count mismatch");
    priority_sort_todos(state.active_todos, state.active_todo_n);
}

void update_actual_fit() { 
    if (state.hours_view_manual.from != -1) {
        state.hours_view = state.hours_view_manual;
    } else {
        int min_sec = state.hours_view_events.from * 3600,
            max_sec = state.hours_view_events.to * 3600;
        struct tm t = timet_to_tm_with_zone(state.now, state.zone);
        int now_sec = day_sec(t);
        time_t diff = time(NULL) - state.base;
        if (!(diff > state.view_days * 3600 * 24 || diff < 0)) {
            // fprintf(stderr, "fit now_sec: %f\n", now_sec / 3600.0);
            if (now_sec < min_sec) min_sec = now_sec;
            if (now_sec > max_sec) max_sec = now_sec;
        }
        range r;
        r.from = max(0, min_sec / 3600);
        r.to = min(24, (max_sec + 3599) / 3600);
        state.hours_view = r;
    }
    assert(state.hours_view.to > state.hours_view.from
            && state.hours_view.to <= 24
            && state.hours_view.from >= 0, "wrong from/to hour");
}

static void reload_calendars() {
    for (int i = 0; i < state.n_cal; i++) {
        update_calendar_from_storage(&state.cal[i], state.zone->impl);
    }
    update_active_events();
    update_active_todos();
    state.dirty = true;
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
            timet_adjust_days(&state.base, state.zone, state.view_days);
            update_active_events();
            state.dirty = true;
            break;
        case 'h':
            timet_adjust_days(&state.base, state.zone, -state.view_days);
            update_active_events();
            state.dirty = true;
            break;
        case 't':
            state.base = get_day_base(state.zone, state.view_days > 1);
            update_active_events();
            state.dirty = true;
            break;
        case 'n':
            if (!state.sp) {
                time_t now = time(NULL);
                struct tm t = *gmtime(&now);

                if (state.main_view == VIEW_CALENDAR) {

                    /* bit of a hack constructing this template */
                    struct active_event template;
                    struct event_recur_set ers;
                    ers.uid = NULL;
                    init_event(&ers.base);
                    ers.base.start.local_time = t;
                    ers.base.end.local_time = t;
                    assert(state.n_cal > 0, "no calendars");
                    template.tag.cal = &state.cal[0];
                    template.ers = &ers;
                    template.start.timestamp = -1;

                    launch_event_editor(&template);
                } else if (state.main_view == VIEW_TODO) {
                    struct todo template;
                    init_todo(&template);
                    template.due.local_time = t;
                    template.start.local_time = t;
                    assert(state.n_cal > 0, "no calendars");
                    launch_todo_editor(&template, &state.cal[0]);
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
            update_active_events();
            state.dirty = true;
            break;
        case 'r':
            reload_calendars();
            break;
        case 'p':
            state.show_private_events = !state.show_private_events;
            update_active_events();
            update_active_todos();
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
                    update_active_events();
                    state.dirty = true;
                }
            } else if (mods & 1) { /* shift modifier */
                if (key == 13) { /* KEY_EQUALS */
                    if (state.hours_view_manual.from == -1) {
                        state.hours_view_manual = state.hours_view_events;
                        update_actual_fit();
                        state.dirty = true;
                    }
                    if (state.hours_view_manual.to > state.hours_view_manual.from + 1) {
                        --state.hours_view_manual.to;
                        update_actual_fit();
                        state.dirty = true;
                    }
                }
                if (key == 12) { /* KEY_MINUS */
                    if (state.hours_view_manual.from == -1) {
                        state.hours_view_manual = state.hours_view_events;
                        update_actual_fit();
                        state.dirty = true;
                    }
                    if (state.hours_view_manual.to < 24) {
                        ++state.hours_view_manual.to;
                        update_actual_fit();
                        state.dirty = true;
                    }
                    if (state.hours_view_manual.from > 0) {
                        --state.hours_view_manual.from;
                        update_actual_fit();
                        state.dirty = true;
                    }
                }
            } else if (key == 103) { /* KEY_UP */
                if (state.hours_view_manual.from == -1) {
                    state.hours_view_manual = state.hours_view_events;
                    update_actual_fit();
                    state.dirty = true;
                }
                if (state.hours_view_manual.from > 0) {
                    --state.hours_view_manual.from;
                    --state.hours_view_manual.to;
                    update_actual_fit();
                    state.dirty = true;
                }
            } else if (key == 108) { /* KEY_DOWN */
                if (state.hours_view_manual.from == -1) {
                    state.hours_view_manual = state.hours_view_events;
                    update_actual_fit();
                    state.dirty = true;
                }
                if (state.hours_view_manual.to < 24) {
                    ++state.hours_view_manual.from;
                    ++state.hours_view_manual.to;
                    update_actual_fit();
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
            state.view_days = 30;
            update = true;
            break;
        case 'j':
            state.view_days = 7;
            update = true;
            break;
        case 'k':
            state.view_days = 1;
            update = true;
            break;
        }
        if (update) {
            update_active_events();
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
    bool del = false;
    struct calendar *cal = state.sp_calendar;
    FILE *f = subprocess_get_result(&(state.sp), pid);
    if (!state.sp) state.sp_calendar = NULL;
    if (!f) return;

    if (state.sp_type == ICAL_VEVENT_COMPONENT) {
        struct event ev;
        char *uid = NULL;
        time_t recurrence_id = -1;
        res = parse_event_template(f, &ev, state.zone->impl, &del, &uid,
                &recurrence_id);
        if (res >= 0) {
            res = save_event(ev, &uid, cal, del, recurrence_id);
            if (uid) free(uid);
        }
        if (res >= 0) {
            update_active_events();
            state.dirty = true;
        } else {
            fprintf(stderr, "event creation failed\n");
        }
    } else if (state.sp_type == ICAL_VTODO_COMPONENT) {
        struct todo td;
        res = parse_todo_template(f, &td, state.zone->impl, &del);
        if (res >= 0) {
            res = save_todo(td, cal, del);
        }
        if (res >= 0) {
            update_active_todos();
            state.dirty = true;
        } else {
            fprintf(stderr, "todo saving failed\n");
        }
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
        .view_days = -1,
        .window_width = -1,
        .window_height = -1,
        .sp = NULL,
        .active_events = NULL,
        .active_event_layouts = NULL,
        .active_todos = NULL,
        .show_private_events = false,
        .keystate = KEYSTATE_BASE
    };

    state.view_days = opts.view_days;

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
    state.base = get_day_base(state.zone, state.view_days == 7);

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

        /* calculate most frequent color */
        const char *fc = most_frequent(
            cal->event_sets, &get_event_recur_set_color);
        uint32_t color = lookup_color(fc);
        if (!color) color = 0xFF20D0D0;
        state.cal_info[state.n_cal].color = color;

        /* next */
        if (++state.n_cal >= 16) break;
    }

    update_active_events();
    update_active_todos();

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

    discard_temp_structures();
    for (int i = 0; i < state.n_cal; i++) {
        destruct_calendar(&state.cal[i]);
    }
    free_timezone(state.zone);
    text_renderer_free(state.tr);

    backend->vptr->destroy(backend);
    return 0;
}
