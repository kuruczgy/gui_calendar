#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "application.h"
#include "util.h"
#include "keyboard.h"
#include "gui.h"
#include "render.h"

struct state state = { 0 };

static void switch_mode_select() {
    struct key_gen g;

    if (state.keystate == KEYSTATE_SELECT) return;
    state.mode_select_code_n = 0;
    state.keystate = KEYSTATE_SELECT;

    if (state.main_view == VIEW_CALENDAR) {
        key_gen_init(state.active_n, &g);
        state.mode_select_len = g.k;
        for (int i = 0; i < state.active_n; ++i) {
            const char *code = key_gen_get(&g);
            assert(code, "not enough codes");
            strcpy(state.active_events_tag[i].code, code);
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

static void print_event_template_callback(void *ud, FILE *f) {
    print_event_template(f, (struct event *)ud);
}
static void print_todo_template_callback(void *ud, FILE *f) {
    print_todo_template(f, (struct todo *)ud);
}
static void launch_event_editor(struct event *ev, struct calendar *cal) {
    if (!state.sp) {
        state.sp_calendar = cal;
        state.sp_type = ICAL_VEVENT_COMPONENT;
        state.sp = subprocess_new_input(state.editor[0],
                state.editor + 1, &print_event_template_callback, (void *)ev);
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
        for (int i = 0; i < state.active_n; ++i) {
            if (strncmp(
                    state.active_events_tag[i].code, state.mode_select_code,
                    state.mode_select_code_n) == 0) {
                fprintf(stderr, "selected event: %s\n",
                        state.active_events[i]->summary);
                launch_event_editor(
                        state.active_events[i], state.active_events_tag[i].cal);
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
    struct event **list;
    struct event_tag *tags;
    int n;
    time_t from, to;
    bool show_priv;
    struct calendar *cal; /* used for assigning tags */
};
static void init_event_filterer(struct event_filterer *f) {
    f->list = NULL;
    f->tags = NULL;
    f->n = 0;
    f->from = -1;
    f->to = -1;
    f->show_priv = false;
}
static int filter_events(void *f_p, void *e_p) {
    struct event_filterer *f = f_p;
    struct event *e = e_p;
    do {
        if (f->from != -1 && !interval_overlap(f->from, f->to,
                e->start.timestamp, e->end.timestamp)) continue;
        if (!f->show_priv && e->clas == ICAL_CLASS_PRIVATE) continue;

        if (f->list) {
            f->list[f->n] = e;
            if (f->tags) {
                f->tags[f->n] = (struct event_tag){
                    .ev = e,
                    .cal = f->cal
                };
            }
        }
        f->n++;
    } while (e = e->recur);
    return MAP_OK;
}

static void discard_temp_structures() {
    // discard existing structures
    if (state.active_events) {
        free(state.active_events);
        free(state.active_events_tag);
    }
    if (state.layout_event_n) {
        for (int d = 0; state.layout_event_n[d] >= 0; d++)
            free(state.layout_events[d]);
        free(state.layout_events);
        free(state.layout_event_n);
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
            hashmap_iterate(state.cal[i].events, filter_events, &filterer);
        }
    }

    /* allocate memory based on counts */
    state.active_n = filterer.n;
    state.active_events_tag = malloc(sizeof(struct event_tag) * state.active_n);
    struct event **active = malloc(sizeof(struct event*) * state.active_n);

    /* populate the lists */
    filterer.n = 0;
    filterer.list = active;
    filterer.tags = state.active_events_tag;
    for (int i = 0; i < state.n_cal; i++) {
        filterer.cal = &state.cal[i];
        if (state.cal_info[i].visible) {
            hashmap_iterate(state.cal[i].events, filter_events, &filterer);
        }
    }

    /* create the 2D layout */
    struct layout_event **layout_events =
        malloc(sizeof(struct layout_event*) * state.view_days);
    int *layout_event_n = malloc(sizeof(int) * (state.view_days + 1));
    layout_event_n[state.view_days] = -1;
    for (int d = 0; d < state.view_days; d++) {
        // TODO: what if day not 24h long?
        time_t day_base = state.base + 3600 * 24 * d;
        int l = 0;
        struct layout_event *la = layout_events[d]
            = malloc(sizeof(struct layout_event) * state.active_n);
        for (int k = 0; k < state.active_n; k++) {
            struct event *ev = active[k];
            if ( ! interval_overlap(
                    day_base, day_base + 3600 * 24,
                    ev->start.timestamp, ev->end.timestamp)
                ) continue;
            int start_sec = max(0, ev->start.timestamp - day_base);
            int end_sec = min(3600 * 24, ev->end.timestamp - day_base);
            assert(l < state.active_n, "too many events");
            la[l++] = (struct layout_event){
                .start = start_sec,
                .end = end_sec,
                .idx = k
            };
        }
        calendar_layout(la, l);
        layout_event_n[d] = l;
    }

    state.active_events = active;
    state.layout_events = layout_events;
    state.layout_event_n = layout_event_n;
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

static bool different_day(struct tm a, struct tm b) {
    return a.tm_mday != b.tm_mday || a.tm_mon != b.tm_mon || a.tm_year !=
        b.tm_year;
}
static void fit_events() {
    int min_sec = 3600 * 24 + 1, max_sec = -1;
    for (int i = 0; i < state.active_n; ++i) {
        struct event *ev = state.active_events[i];
        if (different_day(ev->start.local_time, ev->end.local_time)) {
            /* TODO: maybe could do better when the event begins on the
             * last day of the view range. */
            min_sec = 0;
            max_sec = 3600 * 24;
            break;
        }
        int start = day_sec(ev->start.local_time),
            end = day_sec(ev->end.local_time);
        if (start < min_sec) min_sec = start;
        if (end > max_sec) max_sec = end;
    }

    if (min_sec > max_sec) min_sec = 0, max_sec = 3600 * 24;

    range r;
    r.from = max(0, min_sec / 3600);
    r.to = min(24, (max_sec + 3599) / 3600);
    if (r.to <= r.from) r = (range){ 0, 24 };
    state.hours_view_events = r;
    state.hours_view_manual = (range){ -1, -1 };

    update_actual_fit();

    // fprintf(stderr, "fit: %f-%f\n", min_sec / 3600.0, max_sec / 3600.0);
}

static void reload_calendars() {
    for (int i = 0; i < state.n_cal; i++) {
        update_calendar_from_storage(&state.cal[i], state.zone->impl);
    }
    update_active_events();
    fit_events();
    update_active_todos();
    state.dirty = true;
}

void application_handle_key(struct display *display,
        uint32_t key, uint32_t mods) {
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
            fit_events();
            state.dirty = true;
            break;
        case 'h':
            timet_adjust_days(&state.base, state.zone, -state.view_days);
            update_active_events();
            fit_events();
            state.dirty = true;
            break;
        case 't':
            state.base = get_day_base(state.zone, state.view_days > 1);
            update_active_events();
            fit_events();
            state.dirty = true;
            break;
        case 'n':
            if (!state.sp) {
                time_t now = time(NULL);
                struct tm t = *gmtime(&now);

                if (state.main_view == VIEW_CALENDAR) {
                    struct event template;
                    init_event(&template);
                    template.start.local_time = t;
                    template.end.local_time = t;
                    assert(state.n_cal > 0, "no calendars");
                    launch_event_editor(&template, &state.cal[0]);
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
            fit_events();
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
                    fit_events();
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
            fit_events();
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

static void application_handle_child(struct display *display, pid_t pid) {
    int res;
    bool del = false;
    struct calendar *cal = state.sp_calendar;
    FILE *f = subprocess_get_result(&(state.sp), pid);
    if (!state.sp) state.sp_calendar = NULL;
    if (!f) return;

    if (state.sp_type == ICAL_VEVENT_COMPONENT) {
        struct event ev;
        res = parse_event_template(f, &ev, state.zone->impl, &del);
        if (res >= 0) {
            res = save_event(ev, cal, del);
        }
        if (res >= 0) {
            update_active_events();
            fit_events();
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

int application_main(int argc, char **argv) {
    state = (struct state){
        .n_cal = 0,
        .view_days = 7,
        .window_width = -1,
        .window_height = -1,
        .sp = NULL,
        .active_events = NULL,
        .active_todos = NULL,
        .layout_event_n = NULL,
        .show_private_events = false,
        .keystate = KEYSTATE_BASE
    };

    for (int i = 0; i < 16; ++i) {
        /* init cal_info */
        state.cal_info[i] = (struct calendar_info) {
            .visible = false
        };
        state.cal_default_visible[i] = false;
    }

    char editor_buffer[128], term_buffer[128];
    editor_buffer[0] = term_buffer[0] = '\0';
    const char *editor_env = getenv("EDITOR");
    if (editor_env) snprintf(editor_buffer, 128, "%s", editor_env);
    snprintf(term_buffer, 128, "st");

    int opt, d;
    while ((opt = getopt(argc, argv, "pd:e:")) != -1) {
        switch (opt) {
        case 'p':
            fprintf(stderr, "setting show_private_events = true\n");
            state.show_private_events = true;
            break;
        case 'd':
            d = atoi(optarg) - 1;
            if (d < 0 || d >= 16) {
                fprintf(stderr, "bad -d option index");
                exit(1);
            }
            state.cal_info[d].visible = true;
            state.cal_default_visible[d] = true;
            break;
        case 'e':
            snprintf(editor_buffer, 128, "%s", optarg);
            break;
        case 't':
            snprintf(term_buffer, 128, "%s", optarg);
            break;
        }
    }

    assert(editor_buffer[0], "please set editor!");
    assert(term_buffer[0], "please set terminal emulator!");
    const char *editor[] = { term_buffer, term_buffer,
        editor_buffer, "{file}", NULL };
    fprintf(stderr, "editor command: %s, term command: %s\n",
        editor_buffer, term_buffer);
    state.editor = editor;

    state.zone = new_timezone("Europe/Budapest");
    state.base = get_day_base(state.zone, state.view_days > 1);

    for (int i = optind; i < argc; i++) {
        struct calendar *cal = &state.cal[state.n_cal];

        // init
        init_calendar(cal);

        // read
        cal->storage = str_dup(argv[i]);
        fprintf(stderr, "loading %s\n", cal->storage);
        update_calendar_from_storage(cal, state.zone->impl);

        // set metadata
        if (!cal->name) cal->name = str_dup(cal->storage);
        cal->storage = str_dup(argv[i]);

        // next
        if (++state.n_cal >= 16) break;
    }

    update_active_events();
    fit_events();
    update_active_todos();

    struct display *display = create_display(&render_application,
            &application_handle_key, &application_handle_child);
    struct window *window = create_window(display, 900, 700);
    if (!window) return 1;

    state.tr = text_renderer_new("Monospace 8");

    state.dirty = true;
    gui_run(window);

    discard_temp_structures();
    for (int i = 0; i < state.n_cal; i++) {
        free_calendar(&state.cal[i]);
    }
    free_timezone(state.zone);
    text_renderer_free(state.tr);

    destroy_window(window);
    destroy_display(display);
    return 0;
}
