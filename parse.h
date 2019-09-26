#ifndef _PARSE_H_
#define _PARSE_H_
#include <stdio.h>
#include <stdint.h>
#include "date.h"

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
};

struct calendar {
    struct event *events;
    struct event **tail;
    int n_events;
    char *name;

    char *tzname;
};

void calendar_calc_local_times(struct calendar *cal, const char *location);

void parse_ics(FILE *f, struct calendar *cal);
void parse_dir(char *path, struct calendar *cal);
void libical_parse_ics(FILE *f, struct calendar *cal);

#endif
