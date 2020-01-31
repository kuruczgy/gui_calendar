#ifndef GUI_CALENDAR_CALENDAR_H
#define GUI_CALENDAR_CALENDAR_H
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>
#include <libical/ical.h>
#undef assert
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
    char *summary;
    struct date start, end;
    uint32_t color;
    char *color_str;
    char *location;
    char *desc;
    enum icalproperty_status status;
    enum icalproperty_class clas;
    bool all_day;
};

struct event_recur_instance {
    struct event_recur_instance *next;
    struct event ev;
    time_t recurrence_id;
};

struct event_recur_set {
    char *uid;
    struct event base;
    int max, n; /* max = 0 means no recurrence */
    struct event_recur_instance *instances;
    time_t set[];
};

struct todo {
    char *uid, *summary, *desc;
    struct date start, due;
    enum icalproperty_status status;
    enum icalproperty_class clas;
};

struct calendar {
    map_t event_sets;
    map_t todos;
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
struct date date_from_timet(time_t t, icaltimezone *local_zone);
struct date date_from_icaltime(icaltimetype tt, icaltimezone *local_zone);

void update_calendar_from_storage(struct calendar *cal,
        icaltimezone *local_zone);
int libical_parse_event(icalcomponent *c, struct calendar *cal,
        icaltimezone *local_zone);
int libical_parse_todo(icalcomponent *c, struct calendar *cal,
        icaltimezone *local_zone);
int libical_parse_ics(FILE *f, struct calendar *cal, icaltimezone *local_zone);
icalcomponent* libical_component_from_file(FILE *f);

/* new object functions: also allocate memory */

/* note: you must initialize base before calling free_event_recur_set */
struct event_recur_set * new_event_recur_set(const char *uid, int max);

/* object init functions */
void init_calendar(struct calendar* cal);
void init_event(struct event *ev);
void init_todo(struct todo *td);

/* object destruct functions */
void destruct_calendar(struct calendar *cal);
void destruct_event(struct event *ev);
void destruct_todo(struct todo *td);

/* object free functions: also free memory */
void free_event_recur_set(struct event_recur_set *ers);

/* object copy functions: ev_dest must be uninitialized */
void copy_event(struct event *ev_dest, const struct event *ev_src);

/* object methods */
struct event * event_recur_set_get(struct event_recur_set *ers, int i,
        time_t *start, time_t *end);

/* takes ownership of event */
int save_event(struct event ev, char **uid_ptr, struct calendar *cal, bool del,
        time_t recurrence_id);
int save_todo(struct todo td, struct calendar *cal, bool del);

/* missing libical stuff */
enum icalproperty_class icalcomponent_get_class(icalcomponent *c);
void icalcomponent_set_class(icalcomponent *c, enum icalproperty_class v);
void icalcomponent_set_color(icalcomponent *c, const char *v);

/* editor stuff */
void print_event_template(FILE *f, struct event *ev, const char *uid,
        time_t recurrence_id);
void print_todo_template(FILE *f, const struct todo *td);
int parse_event_template(FILE *f, struct event *ev, icaltimezone *zone,
        bool *del, char **uid_ptr, time_t *recurrence_id);
int parse_todo_template(FILE *f, struct todo *td, icaltimezone *zone,
        bool *del);

void priority_sort_todos(struct todo **todos, int n);

/* subprocess stuff */
struct subprocess_handle;
struct subprocess_handle* subprocess_new_input(const char *file,
        const char *argv[], void (*cb)(void*, FILE*), void *ud);
FILE *subprocess_get_result(struct subprocess_handle **handle, pid_t pid);

#endif
