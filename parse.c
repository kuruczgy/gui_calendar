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

static icaltimetype parse_datetime(char *s, icaltimezone *zone) {
    int year, month, day, hour, minute;
    int n = sscanf(s, "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute);
    if (n < 5) return icaltime_null_time();
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
    return icaltime_convert_to_zone(tt, icaltimezone_get_utc_timezone());
}

static char *trim_start(char *s) {
    while (isspace(*s)) s++;
    return s;
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

icalcomponent* parse_event_template(FILE *f, icaltimezone *zone) {
    icalcomponent *event = icalcomponent_new(ICAL_VEVENT_COMPONENT);

    char buf[1024], *p;
    int len;
    while (get_line(f, buf, 1024, &len) >= 0) {
        icalproperty *prop = NULL;
        if (p = parse_prop(buf, "summary")) {
            if (*p) prop = icalproperty_new_summary(p);
        } else if (p = parse_prop(buf, "location")) {
            if (*p) prop = icalproperty_new_location(p);
        } else if (p = parse_prop(buf, "desc")) {
            if (*p) prop = icalproperty_new_description(p);
        } else if (p = parse_prop(buf, "start")) {
            prop = icalproperty_new_dtstart(parse_datetime(p, zone));
        } else if (p = parse_prop(buf, "end")) {
            prop = icalproperty_new_dtend(parse_datetime(p, zone));
        }
        if (prop) icalcomponent_add_property(event, prop);
    }

    return event;
}

int save_event(icalcomponent *event, struct calendar *cal) {
    // validate event
    if (! icalcomponent_get_summary(event)) return -1;
    if (! icalcomponent_get_uid(event)) return -1;
    if (! icaltime_is_valid_time(icalcomponent_get_dtstart(event))) return -1;
    if (! icaltime_is_valid_time(icalcomponent_get_dtend(event))) return -1;

    // append event to the in-memory calendar
    libical_parse_event(event, cal);

    // check if directory
    struct stat sb;
    char *path = cal->storage;
    assert(stat(path, &sb) == 0, "stat");
    assert(S_ISDIR(sb.st_mode), "saving to non-dir calendar not supported");

    // save event to file
    char buf[1024];
    const char *uid = icalcomponent_get_uid(event);
    assert(strlen(uid) >= 32, "uid sanity check");
    snprintf(buf, 1024, "%s/%s.ics", path, uid);
    icalcomponent *calendar = icalcomponent_vanew(
        ICAL_VCALENDAR_COMPONENT,
        icalproperty_new_version("2.0"),
        icalproperty_new_prodid("-//ABC Corporation//gui_calendar//EN"),
        event,
        NULL
    );
    char *result = icalcomponent_as_ical_string(calendar);
    fprintf(stderr, "Writing %s\n", buf);
    FILE *f = fopen(buf, "w");
    fputs(result, f);
    fclose(f);

    return 0;
}
