
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "parse.h"
#include <sys/stat.h>
#include <dirent.h>

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

static void cal_append(struct calendar *cal, struct event *ev) {
    assert(*(cal->tail) == NULL, "tail not null");
    *(cal->tail) = ev;
    cal->tail = &(ev->next);
    ev->next = NULL;
    cal->n_events++;
}

void libical_parse_event(icalcomponent *c, struct calendar *cal) {
    struct event *ev = malloc(sizeof(struct event));

    struct icaltimetype
        dtstart = icalcomponent_get_dtstart(c),
        dtend = icalcomponent_get_dtend(c);
    ev->start = parse_date(dtstart);
    ev->end = parse_date(dtend);
    assert(ev->start.timestamp <= ev->end.timestamp,
            "event ends before it begins");
    ev->summary = str_dup(icalcomponent_get_summary(c));
    ev->uid = str_dup(icalcomponent_get_uid(c));
    ev->color = 0;
    ev->location = str_dup(icalcomponent_get_location(c));

    ev->tentative = false;
    icalproperty_status pstatus = icalcomponent_get_status(c);
    if (pstatus == ICAL_STATUS_TENTATIVE || pstatus == ICAL_STATUS_CANCELLED) {
        ev->tentative = true;
    }

    icalproperty *p = icalcomponent_get_first_property(c,
            ICAL_COLOR_PROPERTY);
    if (p) {
        icalvalue *v = icalproperty_get_value(p);
        const char *text = icalvalue_get_text(v);
        ev->color = lookup_color(text);
    }

    icalproperty *rrule=icalcomponent_get_first_property(c,ICAL_RRULE_PROPERTY);
    if (rrule) {
        struct icalrecurrencetype recur = icalproperty_get_rrule(rrule);
        icalrecur_iterator *ritr = icalrecur_iterator_new(recur, dtstart);
        struct icaldurationtype dur = icaltime_subtract(dtend, dtstart);
        struct icaltimetype next;
        int n = 0;
        while (next = icalrecur_iterator_next(ritr), ++n,
                (!icaltime_is_null_time(next) && n < 128)) {
            struct event *ev2 = malloc(sizeof(struct event));
            memcpy(ev2, ev, sizeof(struct event));
            ev2->start = parse_date(next);
            next = icaltime_add(next, dur);
            ev2->end = parse_date(next);
            cal_append(cal, ev2);
        }
        free(ev);
    } else {
        cal_append(cal, ev);
    }

}

void libical_parse_ics(FILE *f, struct calendar *cal) {
    icalparser *parser = icalparser_new();
    icalparser_set_gen_data(parser, f);
    icalcomponent *root = icalparser_parse(parser, read_stream);
    assert(root, "parsing fucked up");
    
    icalcomponent *c = icalcomponent_get_first_component(
        root, ICAL_VEVENT_COMPONENT);
    while(c) {
        libical_parse_event(c, cal);
        c = icalcomponent_get_next_component(
            root, ICAL_VEVENT_COMPONENT);
    }

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
    free(cal->storage);
}

void free_timezone(struct timezone *zone) {
    free(zone->desc);
    free(zone);
}

void parse_dir(char *path, struct calendar *cal) {
    struct stat sb;
    assert(stat(path, &sb) == 0, "stat");

    cal->events = NULL;
    cal->name = NULL;
    cal->tail = &(cal->events);
    if (S_ISREG(sb.st_mode)) { // file
        FILE *f = fopen(path, "rb");
        libical_parse_ics(f, cal);
        fclose(f);
    } else {
        assert(S_ISDIR(sb.st_mode), "not dir");
        DIR *d;
        struct dirent *dir;
        char buf[1024];
        assert(d = opendir(path), "opendir");
        while(dir = readdir(d)) {
            if (dir->d_type != DT_REG) continue;
            int l = strlen(dir->d_name);
            bool displayname = false;
            if (!( l >= 4 && strcmp(dir->d_name + l - 4, ".ics") == 0 )) {
                if (strcmp(dir->d_name, "displayname") == 0) {
                    displayname = true;
                } else {
                    continue;
                }
            }
            snprintf(buf, 1024, "%s/%s", path, dir->d_name);
            FILE *f = fopen(buf, "rb");
            assert(f, "could not open");
            if (displayname) {
                int cnt = fread(buf, 1, 1024, f);
                assert(cnt > 0, "meta");
                assert(cal->name == NULL, "name null");
                cal->name = malloc(cnt+1);
                memcpy(cal->name, buf, cnt);
                cal->name[cnt] = '\0';
            } else {
                libical_parse_ics(f, cal);
            }
            fclose(f);
        }
        assert(closedir(d) == 0, "closedir");
    }
}
