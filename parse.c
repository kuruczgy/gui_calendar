#include "parse.h"

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

int parse_datetime(char *s, icaltimezone *zone, struct date *res) {
    int year, month, day, hour, minute;
    int n = sscanf(s, "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute);
    if (n < 5) return -1;
    // TODO icaltimetype is redundant here
    struct icaltimetype tt = {
        .year = year,
        .month = month,
        .day = day,
        .hour = hour,
        .minute = minute,
        .second = 0,
        .is_date = 0,
        .is_daylight = 0, // TODO
        .zone = zone
    };
    time_t t = icaltime_as_timet_with_zone(tt, zone);
    struct tm tm = *gmtime(&t);
    *res = (struct date) { .utc_time = tm, .timestamp = t };
    return 0;
}

static char *trim_start(char *s) {
    while (isspace(*s)) s++;
    return s;
}

static void print_time(FILE *f, const struct tm *tim) {
    fprintf(f, "%04d-%02d-%02d %02d:%02d", tim->tm_year + 1900,
            tim->tm_mon + 1, tim->tm_mday, tim->tm_hour, tim->tm_min);
}
void print_event_template(FILE *f, const struct event *ev) {
    fprintf(f, "summary: %s\n", ev->summary ? ev->summary : "");
    fprintf(f, "start: ");
    print_time(f, &ev->start.local_time);
    fprintf(f, "\nend: ");
    print_time(f, &ev->end.local_time);
    fprintf(f, "\nlocation: %s\n", ev->location ? ev->location : "");
    fprintf(f, "desc: %s\n", ev->desc ? ev->desc : "");
    if (ev->uid) {
        fprintf(f, "uid: %s\n", ev->uid);
    }
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

void parse_event_template(FILE *f, struct event *ev, icaltimezone *zone) {
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
            parse_datetime(p, zone, &ev->start); // TODO: check error
        } else if (p = parse_prop(buf, "end")) {
            parse_datetime(p, zone, &ev->end); // TODO: check error
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

int save_event(struct event *ev, struct calendar *cal) {
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

    if (access(buf, F_OK) == 0) { // event file already exists?
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
        e->summary = ev->summary; // TODO: memory leak in all of these
        e->start = ev->start;
        e->end = ev->end;
        e->location = ev->location;
        e->desc = ev->desc;
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
        libical_parse_event(event, cal);

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
