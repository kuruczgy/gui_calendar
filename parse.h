#ifndef _PARSE_H_
#define _PARSE_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <libical/ical.h>
#undef assert
#include "date.h"
#include "util/hashmap.h"

struct timezone {
    icaltimezone *impl;
    char *desc;
};

struct date {
    time_t timestamp;

    struct tm utc_time;
    struct tm local_time;
};

struct event {
    char *uid, *summary;
    struct date start, end;
    uint32_t color;
    char *location;
    bool tentative;
    char *desc;
};

struct calendar {
    map_t events;
    char *name;
    char *storage;
};

void calendar_calc_local_times(struct calendar *cal, struct timezone *zone);
struct tm timet_to_tm_with_zone(time_t t, struct timezone *zone);
struct timezone *new_timezone(const char *location);
void free_timezone(struct timezone *zone);
const char *get_timezone_desc(struct timezone *zone);
time_t get_day_base(struct timezone *zone, bool week);

void parse_dir(char *path, struct calendar *cal);
void libical_parse_event(icalcomponent *c, struct calendar *cal);
void libical_parse_ics(FILE *f, struct calendar *cal);
icalcomponent* libical_component_from_file(FILE *f);
void free_calendar(struct calendar *cal);

void print_event_template(FILE *f, const struct event *ev);
void parse_event_template(FILE *f, struct event *ev, icaltimezone *zone);
int save_event(struct event *ev, struct calendar *cal);

// subprocess stuff
struct subprocess_handle;
struct subprocess_handle* subprocess_new_event_input(
        const char *file, const char *argv[], const struct event *template_ev);
FILE *subprocess_get_result(struct subprocess_handle **handle, pid_t pid);

#endif
