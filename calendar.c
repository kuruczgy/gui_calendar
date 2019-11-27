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
    .location = NULL,
    .tentative = false,
    .desc = NULL,
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
    free_event_props(e);
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


static void ev_to_comp(struct event *ev, icalcomponent *event) {
    if (ev->uid) {
        icalcomponent_set_uid(event, ev->uid);
    }
    if (ev->summary) icalcomponent_set_summary(event, ev->summary);
    icalcomponent_set_dtstart(event,
            icaltime_from_timet_with_zone(ev->start.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    icalcomponent_set_dtend(event,
            icaltime_from_timet_with_zone(ev->end.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    icalcomponent_set_class(event, ev->clas);
    if (ev->location) icalcomponent_set_location(event, ev->location);
    if (ev->desc) icalcomponent_set_description(event, ev->desc);
}

int save_event(struct event *ev, struct calendar *cal, bool del,
        icaltimezone *local_zone) {
    // check if directory
    struct stat sb;
    char *path = cal->storage;
    assert(stat(path, &sb) == 0, "stat");
    assert(S_ISDIR(sb.st_mode), "saving to non-dir calendar not supported");

    // save event to file
    char buf[1024];
    const char *uid = ev->uid;
    char uid_buf[64];
    if (!uid) {
        generate_uid(uid_buf);
        uid = uid_buf;
    }
    assert(strlen(uid) >= 32, "uid sanity check");
    snprintf(buf, 1024, "%s/%s.ics", path, uid);

    if (del) {
        if (unlink(buf) < 0) {
            fprintf(stderr, "deletion failed\n");
            return -1;
        }
        hashmap_remove(cal->events, uid);
        fprintf(stderr, "deleted %s\n", buf);
    } else if (access(buf, F_OK) == 0) { // event file already exists
        FILE *f = fopen(buf, "r");
        icalcomponent *root = libical_component_from_file(f);
        fclose(f);

        icalcomponent *c = icalcomponent_get_first_component(
            root, ICAL_VEVENT_COMPONENT);
        while(c) {
            const char *c_uid = icalcomponent_get_uid(c);
            if (strcmp(c_uid, uid) == 0) { // found it
                ev_to_comp(ev, c);
                break;
            }

            c = icalcomponent_get_next_component(
                root, ICAL_VEVENT_COMPONENT);
        }
        char *result = icalcomponent_as_ical_string(root);
        fprintf(stderr, "Writing existing %s\n", buf);

        f = fopen(buf, "w");
        fputs(result, f);
        fclose(f);

        icalcomponent_free(root);

        struct event *e;
        assert(hashmap_get(cal->events, uid, (void**)&e) == MAP_OK,
                "uid not found");
        free_event_props(e);
        e->summary = ev->summary;
        e->start = ev->start;
        e->end = ev->end;
        e->location = ev->location;
        e->desc = ev->desc;
        e->clas = ev->clas;
    } else {
        if (ev->uid) {
            fprintf(stderr, "uid specified, but not found. aborting.\n");
            return -1;
        }
        // validate event
        icalcomponent *event = icalcomponent_new(ICAL_VEVENT_COMPONENT);
        ev_to_comp(ev, event);
        icalcomponent_set_uid(event, uid);
        if (! icalcomponent_get_summary(event)) return -1;
        if (! icalcomponent_get_uid(event)) return -1;
        if (! icaltime_is_valid_time(icalcomponent_get_dtstart(event))) return -1;
        if (! icaltime_is_valid_time(icalcomponent_get_dtend(event))) return -1;

        // append event to the in-memory calendar
        libical_parse_event(event, cal, local_zone);

        icalcomponent *calendar = icalcomponent_vanew(
            ICAL_VCALENDAR_COMPONENT,
            icalproperty_new_version("2.0"),
            icalproperty_new_prodid("-//ABC Corporation//gui_calendar//EN"),
            event,
            NULL
        );
        char *result = icalcomponent_as_ical_string(calendar);
        fprintf(stderr, "Writing new %s\n", buf);
        FILE *f = fopen(buf, "w");
        fputs(result, f);
        fclose(f);
        icalcomponent_free(calendar);
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
