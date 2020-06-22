#ifndef GUI_CALENDAR_CALENDAR_H
#define GUI_CALENDAR_CALENDAR_H
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>

#include "hashmap.h"
#include "datetime.h"
#include "vec.h"
#include "props.h"

struct recurrence;

enum comp_type {
    COMP_TYPE_EVENT,
    COMP_TYPE_TODO
};
struct comp_recur_inst {
    ts recurrence_id;
    struct props p;
};
struct comp {
    struct str uid;
    enum comp_type type;
    struct props p;
    struct vec recur_insts; /* vec<struct comp_recur_inst> */
    struct recurrence *recur;
    bool all_expanded;
};
void comp_init(struct comp *c, struct str uid, enum comp_type type);
int comp_init_from_ics(struct comp *c, FILE *f);
void comp_finish(struct comp *c);
bool comp_equal(const struct comp *a, const struct comp *b);

typedef void (*comp_recur_cb)(void *cl, struct ts_ran time, struct props *p);
struct props * comp_recur_expand(struct comp *c, ts to,
        comp_recur_cb cb, void *cl);

struct comp_inst {
    struct comp *c;
    int comp_idx;
    struct props *p;
    struct ts_ran time;
};

struct calendar {
    struct vec comps_vec; /* vec<struct comp> */
    struct hashmap comps_map; /* hashmap<int> */
    struct vec comp_infos; /* vec<struct comp_info> */
    struct vec cis; /* vec<struct comp_inst> */
    struct str name;
    struct str storage;
    bool priv;
    struct timespec loaded;
    bool comps_dirty;
};
void calendar_init(struct calendar* cal);
void calendar_finish(struct calendar *cal);

/* returns -1 if uid already exists */
int calendar_new_comp(struct calendar *cal, struct str uid,
        enum comp_type type);
/* returns -1 if uid already exists */
int calendar_add_comp(struct calendar *cal, struct comp c);

/* returns -1 if not found */
int calendar_find_comp(struct calendar *cal, const char *uid);
void calendar_delete_comp(struct calendar *cal, int idx);

struct comp * calendar_get_comp(struct calendar *cal, int idx);

void calendar_expand_instances_to(struct calendar *cal, ts to);

void update_calendar_from_storage(struct calendar *cal,
        struct cal_timezone *local_zone);
int libical_parse_ics(FILE *f, struct calendar *cal);

/* subprocess stuff */
struct subprocess_handle;
struct subprocess_handle* subprocess_new_input(const char *file,
        const char *argv[], void (*cb)(void*, FILE*), void *ud);
FILE *subprocess_get_result(struct subprocess_handle **handle, pid_t pid);

/* calendar utility functions */
const char * cal_status_str(enum prop_status v);
const char * cal_class_str(enum prop_class v);

/* struct recurrence */
typedef void (*recur_cb)(void *cl, ts t);
void recurrence_expand(struct recurrence *recur, ts to, recur_cb cb, void *cl);
void recurrence_destroy(struct recurrence *recur);
void recurrence_reset(struct recurrence *recur);

/* struct props */
bool props_valid_for_type(const struct props *p, enum comp_type type);

#endif
