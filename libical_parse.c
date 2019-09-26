
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "parse.h"
#include <libical/ical.h>
#undef assert

static void set_local_time(struct date* date, icaltimezone *zone) {
    icaltimetype tt = icaltime_from_timet_with_zone(date->timestamp, 0, zone);
    date->local_time = (struct tm){
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

void calendar_calc_local_times(struct calendar* cal, const char *location) {
    icaltimezone *zone = icaltimezone_get_builtin_timezone(location);
    const char *tznames = icaltimezone_get_tznames(zone);

    int l = strlen(location) + strlen(tznames) + 4;
    char *buf = malloc(l);
    snprintf(buf, l, "%s (%s)", location, tznames);
    cal->tzname = buf;

    struct event *ev = cal->events;
    while (ev) {
        set_local_time(&ev->start, zone);
        set_local_time(&ev->end, zone);
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
        ev->summary = str_dup(icalcomponent_get_summary(c));
        ev->uid = str_dup(icalcomponent_get_uid(c));
        ev->color = 0;

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

    icalparser_free(parser);
}

static int not_main(int argc, char **argv) {
    char* line;
    icalcomponent *component;
    icalparser *parser = icalparser_new();

    // open file (first command-line argument)
    FILE* stream = fopen(argv[1], "r");

    // associate the FILE with the parser so that read_stream
    // will have access to it
    icalparser_set_gen_data(parser, stream);

    // parse the opened file
    component = icalparser_parse(parser, read_stream);
    assert(component != 0, "cant parse component");

    icaltimezone *utc = icaltimezone_get_utc_timezone();

    for (icalcomponent *c = icalcomponent_get_first_component(component,
                ICAL_VEVENT_COMPONENT); c != 0; c =
            icalcomponent_get_next_component(component, ICAL_VEVENT_COMPONENT))
    {
        icalproperty *p = icalcomponent_get_first_property(c,
                ICAL_SUMMARY_PROPERTY);
        icalvalue *v = icalproperty_get_value(p);
        const char * text = icalvalue_get_text(v);

        icaltimetype dtstart = icalcomponent_get_dtstart(c);
        const char *time = icaltime_as_ical_string(dtstart);

        const char *uid = icalcomponent_get_uid(c);

        fprintf(stderr, "event: %s\n", uid);
        fprintf(stderr, "\tsummary: %s\n", text);
        fprintf(stderr, "\tdtstart: %s\n", time);
    }

    icalcomponent_free(component);
    icalparser_free(parser);

    return 0;
}
