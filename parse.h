#ifndef _PARSE_H_
#define _PARSE_H_
#include <stdio.h>
#include <stdint.h>
#include "date.h"

struct date {
    struct tm time;
    time_t timestamp;
    int utc;
};

struct event {
    struct event *next;
    char *uid, *summary;
    struct date start, end;
    uint32_t color;
};

struct calendar {
    struct event *events;
};

void parse_ics(FILE *f, struct calendar *cal);

#endif
