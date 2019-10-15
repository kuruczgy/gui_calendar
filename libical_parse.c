
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

struct tm timet_to_tm_with_zone(time_t t, struct cal_timezone *zone) {
    return tt_to_tm(icaltime_from_timet_with_zone(t, false, zone->impl));
}

time_t get_day_base(struct cal_timezone *zone, bool week) {
    struct icaltimetype now = icaltime_current_time_with_zone(zone->impl);
    now.hour = now.minute = now.second = 0;
    if (week) {
        int dow = icaltime_day_of_week(now);
        int adjust = -((dow - 2 + 7) % 7);
        icaltime_adjust(&now, adjust, 0, 0, 0);
    }
    return icaltime_as_timet_with_zone(now, zone->impl);
}

struct cal_timezone *new_timezone(const char *location) {
    struct cal_timezone *zone = malloc(sizeof(struct cal_timezone));
    zone->impl = icaltimezone_get_builtin_timezone(location);

    const char *tznames = icaltimezone_get_tznames(zone->impl);
    int l = strlen(location) + strlen(tznames) + 4;
    char *buf = malloc(l);
    snprintf(buf, l, "%s (%s)", location, tznames);
    zone->desc = buf;

    return zone;
}

const char *get_timezone_desc(struct cal_timezone *zone) {
    return zone->desc;
}

static void set_local_time(struct date* date, icaltimezone *zone) {
    icaltimetype tt = icaltime_from_timet_with_zone(date->timestamp, 0, zone);
    date->local_time = tt_to_tm(tt);
}

static int calc_local_time(void *zone_p, void *ev_p) {
    struct cal_timezone *zone = zone_p;
    struct event *ev = ev_p;
    while (ev) {
        set_local_time(&ev->start, zone->impl);
        set_local_time(&ev->end, zone->impl);
        ev = ev->recur;
    }
    return MAP_OK;
}

void calendar_calc_local_times(struct calendar* cal,
        struct cal_timezone *zone) {
    hashmap_iterate(cal->events, calc_local_time, zone);
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

static int free_event(void *u, void *ev) {
    struct event *e = ev;
    if (e->recur) {
        struct event *r = e->recur;
        while (r) {
            struct event *next = r->recur;
            free(r);
            r = next;
        }
    }
    free(e->uid);
    free(e->summary);
    free(e->location);
    free(e);
    return MAP_OK;
}

static int cal_append(struct calendar *cal, struct event *ev) {
    assert(ev->uid, "uid missing at insert");
    struct event *e;
    int res = 1;
    if (hashmap_get(cal->events, ev->uid, (void**)&e) == MAP_OK) {
        hashmap_remove(cal->events, ev->uid);
        free_event(NULL, e);
        res = 0;
    }
    if (ev->recur)
        assert(!ev->recur, "recur not null in cal_append");
    hashmap_put(cal->events, ev->uid, ev);
    return res;
}

void libical_parse_event(icalcomponent *c, struct calendar *cal) {
    struct event *ev = malloc(sizeof(struct event));
    ev->recur = NULL;

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
    ev->desc = str_dup(icalcomponent_get_description(c));

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
        struct event *ev2 = NULL;
        while (next = icalrecur_iterator_next(ritr), ++n,
                (!icaltime_is_null_time(next) && n < 128)) {
            if (!ev2) {
                ev2 = malloc(sizeof(struct event));
                memcpy(ev2, ev, sizeof(struct event));
                cal->num_events += cal_append(cal, ev2);
            } else {
                ev2 = ev2->recur = malloc(sizeof(struct event));
                memcpy(ev2, ev, sizeof(struct event));
                cal->num_events++;
            }

            assert(!ev2->recur, "recur not null");
            ev2->start = parse_date(next);
            next = icaltime_add(next, dur);
            ev2->end = parse_date(next);
        }
        icalrecur_iterator_free(ritr);
        free(ev);
    } else {
        cal->num_events += cal_append(cal, ev);
    }

}

icalcomponent* libical_component_from_file(FILE *f) {
    icalparser *parser = icalparser_new();
    icalparser_set_gen_data(parser, f);
    icalcomponent *root = icalparser_parse(parser, read_stream);
    icalparser_free(parser);
    return root;
}

void libical_parse_ics(FILE *f, struct calendar *cal) {
    icalcomponent *root = libical_component_from_file(f);
    assert(root, "parsing fucked up");
    icalcomponent *c = icalcomponent_get_first_component(
        root, ICAL_VEVENT_COMPONENT);
    while(c) {
        libical_parse_event(c, cal);
        c = icalcomponent_get_next_component(
            root, ICAL_VEVENT_COMPONENT);
    }

    icalcomponent_free(root);
}

void init_calendar(struct calendar *cal) {
    cal->events = hashmap_new();
    cal->num_events = 0;
    cal->name = NULL;
    cal->loaded.tv_sec = 0; // should work...
}

void free_calendar(struct calendar *cal) {
    hashmap_iterate(cal->events, free_event, NULL);
    hashmap_free(cal->events);
    free(cal->name);
    free(cal->storage);
}

void free_timezone(struct cal_timezone *zone) {
    free(zone->desc);
    free(zone);
}

static bool timespec_leq(struct timespec a, struct timespec b) {
    if (a.tv_sec == b.tv_sec) return a.tv_nsec <= b.tv_nsec;
    return a.tv_sec <= b.tv_sec;
}

void update_calendar_from_storage(struct calendar *cal) {
    const char *path = cal->storage;
    struct stat sb;
    assert(stat(path, &sb) == 0, "stat");

    struct timespec loaded = cal->loaded;
    clock_gettime(CLOCK_REALTIME, &cal->loaded);
    if (S_ISREG(sb.st_mode)) { // file
        FILE *f = fopen(path, "rb");
        libical_parse_ics(f, cal);
        fclose(f);
    } else {
        assert(S_ISDIR(sb.st_mode), "not dir");
        DIR *d;
        struct dirent *dir;
        int dir_fd;
        char buf[1024];
        assert(d = opendir(path), "opendir");
        dir_fd = dirfd(d);
        while(dir = readdir(d)) {
            if (dir->d_type != DT_REG) continue;
            assert(fstatat(dir_fd, dir->d_name, &sb, 0) == 0, "stat");
            if (!timespec_leq(loaded, sb.st_mtim)) continue;
            int l = strlen(dir->d_name);
            bool displayname = false;
            if (!( l >= 4 && strcmp(dir->d_name + l - 4, ".ics") == 0 )) {
                if (strcmp(dir->d_name, "displayname") == 0) {
                    displayname = true;
                } else {
                    continue;
                }
            }
            if (displayname && cal->name) continue;
            snprintf(buf, 1024, "%s/%s", path, dir->d_name);
            FILE *f = fopen(buf, "rb");
            assert(f, "could not open");
            if (displayname) {
                assert(!cal->name, "calendar already has name");
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
