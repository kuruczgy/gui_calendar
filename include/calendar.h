#ifndef _PARSE_H_
#define _PARSE_H_
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>
#include <libical/ical.h>
#undef assert
#include "date.h"
#include "hashmap.h"

struct cal_timezone {
    icaltimezone *impl;
    char *desc;
};

struct date {
    // to represent an invalid date, set timestamp to -1
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
    enum icalproperty_class clas;
    struct event *recur;
};

struct todo {
    char *uid, *summary, *desc;
    struct date start, due;
    enum icalproperty_status status;
    enum icalproperty_class clas;
};

struct calendar {
    map_t events;
    map_t todos;
    int num_events;
    char *name;
    char *storage;
    bool priv;
    struct timespec loaded;
};

struct tm timet_to_tm_with_zone(time_t t, struct cal_timezone *zone);
void timet_adjust_days(time_t *t, struct cal_timezone *zone, int n);
struct cal_timezone *new_timezone(const char *location);
void free_timezone(struct cal_timezone *zone);
const char *get_timezone_desc(struct cal_timezone *zone);
time_t get_day_base(struct cal_timezone *zone, bool week);
struct date date_from_icaltime(icaltimetype tt, icaltimezone *local_zone);

void update_calendar_from_storage(struct calendar *cal,
        icaltimezone *local_zone);
void libical_parse_event(icalcomponent *c, struct calendar *cal,
        icaltimezone *local_zone);
void libical_parse_ics(FILE *f, struct calendar *cal, icaltimezone *local_zone);
icalcomponent* libical_component_from_file(FILE *f);
void init_calendar(struct calendar* cal);
void free_calendar(struct calendar *cal);
void free_event(struct event *e);

void init_event(struct event *ev);
void init_todo(struct todo *td);

/* takes ownership of event */
int save_event(struct event ev, struct calendar *cal, bool del);
int save_todo(struct todo td, struct calendar *cal, bool del);

/* missing libical stuff */
enum icalproperty_class icalcomponent_get_class(icalcomponent *c);
void icalcomponent_set_class(icalcomponent *c, enum icalproperty_class v);

/* editor stuff */
void print_event_template(FILE *f, const struct event *ev);
void print_todo_template(FILE *f, const struct todo *td);
int parse_event_template(FILE *f, struct event *ev, icaltimezone *zone,
        bool *del);
int parse_todo_template(FILE *f, struct todo *td, icaltimezone *zone,
        bool *del);

void priority_sort_todos(struct todo **todos, int n);

// subprocess stuff
struct subprocess_handle;
struct subprocess_handle* subprocess_new_input(const char *file,
        const char *argv[], void (*cb)(void*, FILE*), void *ud);
FILE *subprocess_get_result(struct subprocess_handle **handle, pid_t pid);

#endif
