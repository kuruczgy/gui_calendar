#ifndef _PARSE_H_
#define _PARSE_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <libical/ical.h>
#undef assert
#include "date.h"

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
    struct event *next;
    char *uid, *summary;
    struct date start, end;
    uint32_t color;
    char *location;
};

struct calendar {
    struct event *events;
    struct event **tail;
    int n_events;
    char *name;
    char *storage;
};

void calendar_calc_local_times(struct calendar *cal, struct timezone *zone);
struct tm time_now(struct timezone *zone);
struct timezone *new_timezone(const char *location);
void free_timezone(struct timezone *zone);
const char *get_timezone_desc(struct timezone *zone);
time_t get_day_base(struct timezone *zone, bool week);

void parse_dir(char *path, struct calendar *cal);
void libical_parse_event(icalcomponent *c, struct calendar *cal);
void libical_parse_ics(FILE *f, struct calendar *cal);
void free_calendar(struct calendar *cal);

icalcomponent* parse_event_template(FILE *f, icaltimezone *zone);
int save_event(icalcomponent *event, struct calendar *cal);

// subprocess stuff
struct subprocess_handle;
struct subprocess_handle* subprocess_new_event_input(
        const char *file, const char *argv[]);
FILE *subprocess_get_result(struct subprocess_handle **handle, pid_t pid);

#endif
