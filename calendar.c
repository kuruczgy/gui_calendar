#include "calendar.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "util.h"

void init_calendar(struct calendar *cal) {
    cal->events = hashmap_new();
    cal->todos = hashmap_new();
    cal->num_events = 0;
    cal->name = NULL;
    cal->priv = false;
    cal->loaded.tv_sec = 0; // should work...
}

static const struct date default_date = { .timestamp = -1 };
static const struct event default_event = {
    .uid = NULL,
    .summary = NULL,
    .start = default_date,
    .end = default_date,
    .color = 0,
    .color_str = NULL,
    .location = NULL,
    .desc = NULL,
    .status = ICAL_STATUS_NONE,
    .clas = ICAL_CLASS_NONE,
    .recur = NULL
};
static const struct todo default_todo = {
    .uid = NULL,
    .summary = NULL,
    .desc = NULL,
    .start = default_date,
    .due = default_date,
    .status = ICAL_STATUS_NONE,
    .clas = ICAL_CLASS_NONE
};

void init_event(struct event *ev) { *ev = default_event; }
void init_todo(struct todo *td) { *td = default_todo; }

static void free_event_props(struct event *ev) {
    free(ev->summary); ev->summary = NULL;
    free(ev->location); ev->location = NULL;
    free(ev->desc); ev->desc = NULL;
    free(ev->color_str); ev->color_str = NULL;
}

void free_event(struct event *e) {
    if (e->recur) {
        struct event *r = e->recur;
        while (r) {
            struct event *next = r->recur;
            free(r);
            r = next;
        }
    }
    free_event_props(e); //TODO: somehow free uid too...
    free(e);
}
static int free_event_iter(void *u, void *ev) {
    free_event(ev);
    return MAP_OK;
}

void free_calendar(struct calendar *cal) {
    hashmap_iterate(cal->events, free_event_iter, NULL);
    // hashmap_iterate(cal->todos, free_task_iter, NULL); // TODO: free tasks
    hashmap_free(cal->events);
    hashmap_free(cal->todos);
    free(cal->name);
    free(cal->storage);
}


static void ev_to_comp(void *obj, icalcomponent *event) {
    struct event *ev = obj;
    if (ev->uid) icalcomponent_set_uid(event, ev->uid);
    if (ev->summary) icalcomponent_set_summary(event, ev->summary);
    icalcomponent_set_dtstart(event,
            icaltime_from_timet_with_zone(ev->start.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    icalcomponent_set_dtend(event,
            icaltime_from_timet_with_zone(ev->end.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    icalcomponent_set_color(event, ev->color_str);
    icalcomponent_set_class(event, ev->clas);
    icalcomponent_set_status(event, ev->status);
    if (ev->location) icalcomponent_set_location(event, ev->location);
    if (ev->desc) icalcomponent_set_description(event, ev->desc);
}

static void td_to_comp(void *obj, icalcomponent *todo) {
    struct todo *td = obj;
    if (td->uid) icalcomponent_set_uid(todo, td->uid);
    if (td->summary) icalcomponent_set_summary(todo, td->summary);
    if (td->start.timestamp > 0) icalcomponent_set_dtstart(todo,
            icaltime_from_timet_with_zone(td->start.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    if (td->due.timestamp > 0) icalcomponent_set_due(todo,
            icaltime_from_timet_with_zone(td->due.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    icalcomponent_set_class(todo, td->clas);
    if (td->status != ICAL_STATUS_NONE)
        icalcomponent_set_status(todo, td->status);
    if (td->desc) icalcomponent_set_description(todo, td->desc);
}

/* returns: 0: other, 1: updated, 2: created */
static int save_component(void *obj, char **uid_ref,
        struct calendar *cal, bool del,
        enum icalcomponent_kind type,
        void (*conv_cb)(void *obj, icalcomponent *comp)) {
    /* check if storage is directory */
    struct stat sb;
    char *path = cal->storage;
    assert(stat(path, &sb) == 0, "stat");
    assert(S_ISDIR(sb.st_mode), "saving to non-dir calendar not supported");

    /* construct the path to the specific .ics file */
    char buf[1024];
    char uid_buf[64];
    bool uid_gen = false;
    if (!(*uid_ref)) {
        generate_uid(uid_buf);
        *uid_ref = str_dup(uid_buf);
        uid_gen = true;
    }
    const char *uid = *uid_ref;
    assert(strlen(uid) >= 32, "uid sanity check");
    snprintf(buf, 1024, "%s/%s.ics", path, uid);

    if (del) { /* handle deletion */
        if (unlink(buf) < 0) {
            fprintf(stderr, "deletion failed\n");
            return -1;
        }
        return 0;
    } else if (access(buf, F_OK) == 0) { /* handle saving to existing file */
        /* load and parse the file */
        FILE *f = fopen(buf, "r");
        icalcomponent *root = libical_component_from_file(f);
        fclose(f);

        /* find the specific component we are interested in, using the uid */
        icalcomponent *c = icalcomponent_get_first_component(root, type);
        while (c) {
            const char *c_uid = icalcomponent_get_uid(c);
            if (strcmp(c_uid, uid) == 0) { // found it
                /* populate component with new values */
                conv_cb(obj, c);
                break;
            }
            c = icalcomponent_get_next_component(root, type);
        }

        /* serialize and write back the component */
        char *result = icalcomponent_as_ical_string(root);
        fprintf(stderr, "Writing existing %s\n", buf);
        f = fopen(buf, "w");
        fputs(result, f);
        fclose(f);
        icalcomponent_free(root);

        return 1;
    } else { /* handle saving to a new file */
        if (!uid_gen) {
            fprintf(stderr, "uid specified, but not found. aborting.\n");
            return -1;
        }

        /* create the component */
        icalcomponent *comp = icalcomponent_new(type);
        conv_cb(obj, comp);
        icalcomponent_set_uid(comp, uid);
        if (! icalcomponent_get_summary(comp)) return -1;
        if (! icalcomponent_get_uid(comp)) return -1;
        /*
        if (! icaltime_is_valid_time(icalcomponent_get_dtstart(comp))) return -1;
        if (! icaltime_is_valid_time(icalcomponent_get_dtend(comp))) return -1;
        */

        /* create a frame, serialize, and save to file */
        icalcomponent *calendar = icalcomponent_vanew(
            ICAL_VCALENDAR_COMPONENT,
            icalproperty_new_version("2.0"),
            icalproperty_new_prodid("-//ABC Corporation//gui_calendar//EN"),
            comp,
            NULL
        );
        char *result = icalcomponent_as_ical_string(calendar);
        fprintf(stderr, "Writing new %s\n", buf);
        FILE *f = fopen(buf, "w");
        fputs(result, f);
        fclose(f);
        icalcomponent_free(calendar);

        return 2;
    }
}

int save_event(struct event ev, struct calendar *cal, bool del) {
    int res = save_component(
        (void*)&ev, &(ev.uid), cal, del,
        ICAL_VEVENT_COMPONENT, &ev_to_comp);
    if (res < 0) return res;
    if (del) {
        assert(res == 0, "wrong result for delete");
        hashmap_remove(cal->events, ev.uid);
        // TODO: cal->num_events ??
        fprintf(stderr, "event deleted %s\n", ev.uid);
    } else if (res == 1) {
        /* update the component in memory */
        struct event *e;
        assert(hashmap_get(cal->events, ev.uid, (void**)&e) == MAP_OK,
                "uid not found");
        free_event_props(e);
        e->summary = ev.summary;
        e->start = ev.start;
        e->end = ev.end;
        e->color = ev.color;
        e->color_str = ev.color_str;
        e->location = ev.location;
        e->desc = ev.desc;
        e->status = ev.status;
        e->clas = ev.clas;
    } else if (res == 2) {
        assert(ev.uid, "did not get an uid");
        struct event *new_ev = malloc(sizeof(struct event));
        memcpy(new_ev, &ev, sizeof(struct event));
        hashmap_put(cal->events, new_ev->uid, new_ev);
    }
    return 0;
}

int save_todo(struct todo td, struct calendar *cal, bool del) {
    int res = save_component(
        (void*)&td, &(td.uid), cal, del,
        ICAL_VTODO_COMPONENT, &td_to_comp);
    if (res < 0) return res;
    if (del) {
        assert(res == 0, "wrong result for delete");
        hashmap_remove(cal->todos, td.uid);
        fprintf(stderr, "todo deleted %s\n", td.uid);
    } else if (res == 1) {
        /* update the component in memory */
        struct todo *t;
        assert(hashmap_get(cal->todos, td.uid, (void**)&t) == MAP_OK,
                "uid not found");
        // free_event_props(e); //TODO: memory leak
        t->summary = td.summary;
        t->start = td.start;
        t->due = td.due;
        t->desc = td.desc;
        t->clas = td.clas;
        t->status = td.status;
    } else if (res == 2) {
        assert(td.uid, "did not get an uid");
        struct event *new_td = malloc(sizeof(struct todo));
        memcpy(new_td, &td, sizeof(struct todo));
        hashmap_put(cal->todos, new_td->uid, new_td);
    }
    return 0;
}

static int todo_cmp(const void *pa, const void *pb) {
    struct todo *a = *(struct todo**)pa, *b = *(struct todo**)pb;
    if (a->due.timestamp > 0 || b->due.timestamp > 0) {
        if (a->due.timestamp < 0) return 1;
        else if (b->due.timestamp < 0) return -1;
        return a->due.timestamp - b->due.timestamp;
    }
    return 0;
}
void priority_sort_todos(struct todo **todos, int n) {
    qsort(todos, n, sizeof(struct todo*), todo_cmp);
}
