#include "calendar.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "util.h"

/* new object functions: also allocate memory */
struct event_recur_set * new_event_recur_set(const char *uid, int max) {
    struct event_recur_set *ers = malloc_check(
        sizeof(struct event_recur_set) + max * sizeof(time_t));
    *ers = (struct event_recur_set){
        .uid = str_dup(uid), .max = max, .n = 0, .instances = NULL
    };
    return ers;
}

/* object init functions */

void init_calendar(struct calendar *cal) {
    cal->event_sets = hashmap_new();
    cal->todos = hashmap_new();
    cal->name = NULL;
    cal->storage = NULL;
    cal->priv = false;
    cal->loaded.tv_sec = 0; // should work...
}

static const struct date default_date = { .timestamp = -1 };
static const struct event default_event = {
    .summary = NULL,
    .start = default_date,
    .end = default_date,
    .color = 0,
    .color_str = NULL,
    .location = NULL,
    .desc = NULL,
    .status = ICAL_STATUS_NONE,
    .clas = ICAL_CLASS_NONE,
    .all_day = false
};
static const struct todo default_todo = {
    .uid = NULL,
    .summary = NULL,
    .desc = NULL,
    .start = default_date,
    .due = default_date,
    .status = ICAL_STATUS_NONE,
    .clas = ICAL_CLASS_NONE
};

void init_event(struct event *ev) { *ev = default_event; }
void init_todo(struct todo *td) { *td = default_todo; }

/* object destruct functions */

static int iter_destruct_free_event_recur_set(void *cl, void *ers) {
    free_event_recur_set(ers);
    return MAP_OK;
}
static int iter_destruct_free_todo(void *cl, void *td) {
    destruct_todo(td);
    free(td);
    return MAP_OK;
}

void destruct_calendar(struct calendar *cal) {
    hashmap_iterate(cal->event_sets, &iter_destruct_free_event_recur_set, NULL);
    hashmap_iterate(cal->todos, &iter_destruct_free_todo, NULL);
    hashmap_free(cal->event_sets);
    hashmap_free(cal->todos);
    free(cal->name);
    free(cal->storage);
}

void destruct_event(struct event *ev) {
    free(ev->summary); ev->summary = NULL;
    free(ev->location); ev->location = NULL;
    free(ev->desc); ev->desc = NULL;
    free(ev->color_str); ev->color_str = NULL;
}

void destruct_todo(struct todo *td) {
    free(td->uid); td->uid = NULL;
    free(td->summary); td->summary = NULL;
    free(td->desc); td->desc = NULL;
}

/* object free functions: also free memory */
void free_event_recur_set(struct event_recur_set *ers) {
    destruct_event(&ers->base);
    struct event_recur_instance *eri2, *eri = ers->instances;
    while (eri) {
        eri2 = eri;
        eri = eri->next;
        destruct_event(&eri2->ev);
        free(eri2);
    }
    free(ers->uid);
    free(ers);
}

/* object copy functions */
void copy_event(struct event *ev_dest, const struct event *ev_src) {
    // DEP: struct event
    ev_dest->summary = str_dup(ev_src->summary);
    ev_dest->start = ev_src->start;
    ev_dest->end = ev_src->end;
    ev_dest->color = ev_src->color;
    ev_dest->color_str = str_dup(ev_src->color_str);
    ev_dest->location = str_dup(ev_src->location);
    ev_dest->desc = str_dup(ev_src->desc);
    ev_dest->status = ev_src->status;
    ev_dest->clas = ev_src->clas;
    ev_dest->all_day = ev_src->all_day;
}

void copy_todo(struct todo *dst, const struct todo *src) {
    // DEP: struct todo
    dst->uid = str_dup(src->uid);
    dst->summary = str_dup(src->summary);
    dst->desc = str_dup(src->desc);
    dst->start = src->start;
    dst->due = src->due;
    dst->status = src->status;
    dst->clas = src->clas;
}

/* object methods */
struct event * event_recur_set_get(struct event_recur_set *ers, int i,
        time_t *start, time_t *end) {
    /* special case if only a single event */
    if (ers->max == 0) {
        assert(i == 0, "");
        *start = ers->base.start.timestamp;
        *end = ers->base.end.timestamp;
        return &ers->base;
    }
    assert(i >= 0 && i < ers->max, "");

    /* check for customized recurrence-id instances */
    time_t recurrence_id = ers->set[i];
    struct event_recur_instance *eri = ers->instances;
    while (eri) {
        if (eri->recurrence_id == recurrence_id) {
            *start = eri->ev.start.timestamp;
            *end = eri->ev.end.timestamp;
            return &eri->ev;
        }
        eri = eri->next;
    }

    /* generic instance */
    time_t len = ers->base.end.timestamp - ers->base.start.timestamp;
    *start = recurrence_id;
    *end = recurrence_id + len;
    return &ers->base;
}
void event_update_derived(struct event *ev) {
    ev->color = lookup_color(ev->color_str);
    if (ev->end.timestamp - ev->start.timestamp >= 3600 * 24) {
        // all day heuristic
        ev->all_day = true;
    }
}
