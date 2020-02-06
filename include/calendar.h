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
#include "datetime.h"

struct cal_timezone {
    icaltimezone *impl;
    char *desc;
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
    // DEP: struct event
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
    // DEP: struct todo
};

struct calendar {
    map_t event_sets;
    map_t todos;
    char *name;
    char *storage;
    bool priv;
    struct timespec loaded;
};

enum comp_type {
    COMP_TYPE_EVENT,
    COMP_TYPE_TODO
};

struct cal_timezone *new_timezone(const char *location);
void free_timezone(struct cal_timezone *zone);
const char *get_timezone_desc(struct cal_timezone *zone);

void update_calendar_from_storage(struct calendar *cal,
        icaltimezone *local_zone);
int libical_parse_event(icalcomponent *c, struct calendar *cal,
        icaltimezone *local_zone);
int libical_parse_todo(icalcomponent *c, struct calendar *cal,
        icaltimezone *local_zone);
int libical_parse_ics(FILE *f, struct calendar *cal, icaltimezone *local_zone);
icalcomponent* libical_component_from_file(FILE *f);

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

/* object copy functions: dst must be uninitialized */
void copy_event(struct event *ev_dest, const struct event *ev_src);
void copy_todo(struct todo *dst, const struct todo *src);

/* object methods */
struct event * event_recur_set_get(struct event_recur_set *ers, int i,
        time_t *start, time_t *end);
void event_update_derived(struct event *ev);

/* takes ownership of event */
int save_event(struct event ev, char **uid_ptr, struct calendar *cal, bool del,
        time_t recurrence_id);
int save_todo(struct todo td, struct calendar *cal, bool del);

/* missing libical stuff */
enum icalproperty_class icalcomponent_get_class(icalcomponent *c);
void icalcomponent_set_class(icalcomponent *c, enum icalproperty_class v);
void icalcomponent_set_color(icalcomponent *c, const char *v);
void icalcomponent_remove_properties(icalcomponent *c, icalproperty_kind kind);

/* provides a partial ordering over todos */
int todo_priority_cmp(const struct todo *a, const struct todo *b);

/* subprocess stuff */
struct subprocess_handle;
struct subprocess_handle* subprocess_new_input(const char *file,
        const char *argv[], void (*cb)(void*, FILE*), void *ud);
FILE *subprocess_get_result(struct subprocess_handle **handle, pid_t pid);

#endif
