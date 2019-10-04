
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "parse.h"

static struct tm tt_to_tm(icaltimetype tt) {
    return (struct tm){
        .tm_sec = tt.second,
        .tm_min = tt.minute,
        .tm_hour = tt.hour,
        .tm_mday = tt.day,
        .tm_mon = tt.month - 1,
        .tm_year = tt.year - 1900,
        .tm_wday = icaltime_day_of_week(tt) - 1,
        .tm_yday = icaltime_day_of_year(tt),
        .tm_isdst = tt.is_daylight
    };
}

struct tm time_now(struct timezone *zone) {
    return tt_to_tm(icaltime_current_time_with_zone(zone->impl));
}

time_t get_day_base(struct timezone *zone, bool week) {
    struct icaltimetype now = icaltime_current_time_with_zone(zone->impl);
    now.hour = now.minute = now.second = 0;
    if (week) {
        int dow = icaltime_day_of_week(now);
        int adjust = -((dow - 2 + 7) % 7);
        icaltime_adjust(&now, adjust, 0, 0, 0);
    }
    return icaltime_as_timet_with_zone(now, zone->impl);
}

struct timezone *new_timezone(const char *location) {
    struct timezone *zone = malloc(sizeof(struct timezone));
    zone->impl = icaltimezone_get_builtin_timezone(location);

    const char *tznames = icaltimezone_get_tznames(zone->impl);
    int l = strlen(location) + strlen(tznames) + 4;
    char *buf = malloc(l);
    snprintf(buf, l, "%s (%s)", location, tznames);
    zone->desc = buf;

    return zone;
}

const char *get_timezone_desc(struct timezone *zone) {
    return zone->desc;
}

static void set_local_time(struct date* date, icaltimezone *zone) {
    icaltimetype tt = icaltime_from_timet_with_zone(date->timestamp, 0, zone);
    date->local_time = tt_to_tm(tt);
}

void calendar_calc_local_times(struct calendar* cal, struct timezone *zone) {
    struct event *ev = cal->events;
    while (ev) {
        set_local_time(&ev->start, zone->impl);
        set_local_time(&ev->end, zone->impl);
        ev = ev->next;
    }
}

char* read_stream(char *s, size_t size, void *d)
{
    return fgets(s, size, (FILE*)d);
}

static struct date parse_date(icaltimetype tt) {
    // icaltimezone *utc = icaltimezone_get_utc_timezone();
    time_t t = icaltime_as_timet_with_zone(tt, icaltime_get_timezone(tt));
    struct tm tm = *gmtime(&t);
    return (struct date) { .utc_time = tm, .timestamp = t };
}

void libical_parse_ics(FILE *f, struct calendar *cal) {
    icalparser *parser = icalparser_new();
    icalparser_set_gen_data(parser, f);
    icalcomponent *root = icalparser_parse(parser, read_stream);
    assert(root, "parsing fucked up");
    
    icalcomponent *c = icalcomponent_get_first_component(
        root, ICAL_VEVENT_COMPONENT);
    struct event **last = cal->tail;
    while(c) {
        struct event *ev = malloc(sizeof(struct event));

        ev->start = parse_date(icalcomponent_get_dtstart(c));
        ev->end = parse_date(icalcomponent_get_dtend(c));
        assert(ev->start.timestamp <= ev->end.timestamp,
                "event ends before it begins");
        ev->summary = str_dup(icalcomponent_get_summary(c));
        ev->uid = str_dup(icalcomponent_get_uid(c));
        ev->color = 0;
        ev->location = str_dup(icalcomponent_get_location(c));

        icalproperty *p = icalcomponent_get_first_property(c,
                ICAL_COLOR_PROPERTY);
        if (p) {
            icalvalue *v = icalproperty_get_value(p);
            const char *text = icalvalue_get_text(v);
            ev->color = lookup_color(text);
        }

        *last = ev;
        last = &(ev->next);
        ev->next = NULL;
        cal->n_events++;

        c = icalcomponent_get_next_component(
            root, ICAL_VEVENT_COMPONENT);
    }
    cal->tail = last;

    icalcomponent_free(root);
    icalparser_free(parser);
}

static void free_event(struct event *e) {
    free(e->uid);
    free(e->summary);
    free(e->location);
    free(e);
}

void free_calendar(struct calendar *cal) {
    struct event *ev = cal->events;
    while (ev) {
        struct event *next = ev->next;
        free_event(ev);
        ev = next;
    }
    free(cal->name);
}

void free_timezone(struct timezone *zone) {
    free(zone->desc);
    free(zone);
}
