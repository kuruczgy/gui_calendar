#include "calendar.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
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

static void free_event_props(struct event *ev) {
    free(ev->uid); ev->uid = NULL;
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

static int parse_datetime_prop(char *s, struct date *res,
        icaltimezone *local_zone) {
    int year, month, day, hour, minute;
    int n = sscanf(s, "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute);
    if (n < 5) return -1;
    struct icaltimetype tt = {
        .year = year,
        .month = month,
        .day = day,
        .hour = hour,
        .minute = minute,
        .second = 0,
        .is_date = 0,
        .is_daylight = 0, // TODO: is this ok like this?
        .zone = local_zone
    };
    *res = date_from_icaltime(tt, local_zone);
    return 0;
}

static char *trim_start(char *s) {
    while (isspace(*s)) s++;
    return s;
}

static void print_time_prop(FILE *f, const struct tm *tim) {
    fprintf(f, "%04d-%02d-%02d %02d:%02d", tim->tm_year + 1900,
            tim->tm_mon + 1, tim->tm_mday, tim->tm_hour, tim->tm_min);
}
void print_event_template(FILE *f, const struct event *ev) {
    fprintf(f, "summary: %s\n", ev->summary ? ev->summary : "");
    fprintf(f, "start: ");
    print_time_prop(f, &ev->start.local_time);
    fprintf(f, "\nend: ");
    print_time_prop(f, &ev->end.local_time);
    fprintf(f, "\nlocation: %s\n", ev->location ? ev->location : "");
    fprintf(f, "desc: %s\n", ev->desc ? ev->desc : "");
    if (ev->uid) {
        fprintf(f, "uid: %s\n", ev->uid);
    }
    fprintf(f, "delete: \n");
}

static char* parse_prop(char *buf, const char *name) {
    int l = strlen(name);
    if (strncmp(name, buf, l) == 0) {
        char *c = strchr(buf + l, ':');
        if (!c) return NULL;
        return trim_start(c + 1);
    }
    return NULL;
}

void parse_event_template(FILE *f, struct event *ev, icaltimezone *zone,
        bool *del) {
    *del = false;
    ev->summary = ev->location = ev->desc = ev->uid = NULL;
    char buf[1024], *p;
    int len;
    while (get_line(f, buf, 1024, &len) >= 0) {
        icalproperty *prop = NULL;
        if (p = parse_prop(buf, "uid")) {
            if (*p) ev->uid = str_dup(p);
        } else if (p = parse_prop(buf, "summary")) {
            if (*p) ev->summary = str_dup(p);
        } else if (p = parse_prop(buf, "location")) {
            if (*p) ev->location = str_dup(p);
        } else if (p = parse_prop(buf, "desc")) {
            if (*p) ev->desc = str_dup(p);
        } else if (p = parse_prop(buf, "start")) {
            if (parse_datetime_prop(p, &ev->start, zone) < 0)
                ev->start.timestamp = -1;
        } else if (p = parse_prop(buf, "end")) {
            if (parse_datetime_prop(p, &ev->end, zone) < 0)
                ev->end.timestamp = -1;
        } else if (p = parse_prop(buf, "delete")) {
            if (strcmp(p, "true") == 0) *del = true;
            else *del = false;
        }
    }
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
        e->uid = ev->uid;
        e->summary = ev->summary;
        e->location = ev->location;
        e->desc = ev->desc;
        e->start = ev->start;
        e->end = ev->end;
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
