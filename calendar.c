#include "calendar.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

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
    ev_dest->summary = str_dup(ev_src->summary);
    ev_dest->start = ev_src->start;
    ev_dest->end = ev_src->end;
    ev_dest->color = ev_src->color;
    ev_dest->color_str = str_dup(ev_src->color_str);
    ev_dest->location = str_dup(ev_src->location);
    ev_dest->desc = str_dup(ev_src->desc);
    ev_dest->status = ev_src->status;
    ev_dest->clas = ev_src->clas;
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

static void ev_to_comp(void *obj, icalcomponent *event) {
    struct event *ev = obj;
    if (ev->summary) icalcomponent_set_summary(event, ev->summary);
    icalcomponent_set_dtstart(event,
            icaltime_from_timet_with_zone(ev->start.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    icalcomponent_set_dtend(event,
            icaltime_from_timet_with_zone(ev->end.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    icalcomponent_set_color(event, ev->color_str);
    icalcomponent_set_class(event, ev->clas);
    icalcomponent_set_status(event, ev->status);
    if (ev->location) icalcomponent_set_location(event, ev->location);
    if (ev->desc) icalcomponent_set_description(event, ev->desc);
}

static void td_to_comp(void *obj, icalcomponent *todo) {
    struct todo *td = obj;
    if (td->summary) icalcomponent_set_summary(todo, td->summary);
    if (td->start.timestamp > 0) icalcomponent_set_dtstart(todo,
            icaltime_from_timet_with_zone(td->start.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    if (td->due.timestamp > 0) icalcomponent_set_due(todo,
            icaltime_from_timet_with_zone(td->due.timestamp, 0,
                icaltimezone_get_utc_timezone()));
    icalcomponent_set_class(todo, td->clas);
    if (td->status != ICAL_STATUS_NONE)
        icalcomponent_set_status(todo, td->status);
    if (td->desc) icalcomponent_set_description(todo, td->desc);
}

/* returns: 0: other, 1: updated, 2: created */
static int save_component(void *obj, char **uid_ref,
        struct calendar *cal, bool del,
        enum icalcomponent_kind type,
        void (*conv_cb)(void *obj, icalcomponent *comp)) {
    /* check if storage is directory */
    struct stat sb;
    char *path = cal->storage;
    assert(stat(path, &sb) == 0, "stat");
    assert(S_ISDIR(sb.st_mode), "saving to non-dir calendar not supported");

    /* construct the path to the specific .ics file */
    char buf[1024];
    char uid_buf[64];
    bool uid_gen = false;
    if (!(*uid_ref)) {
        generate_uid(uid_buf);
        *uid_ref = str_dup(uid_buf);
        uid_gen = true;
    }
    const char *uid = *uid_ref;
    assert(strlen(uid) >= 32, "uid sanity check");
    snprintf(buf, 1024, "%s/%s.ics", path, uid);

    if (del) { /* handle deletion */
        if (unlink(buf) < 0) {
            fprintf(stderr, "deletion failed\n");
            return -1;
        }
        return 0;
    } else if (access(buf, F_OK) == 0) { /* handle saving to existing file */
        /* load and parse the file */
        FILE *f = fopen(buf, "r");
        icalcomponent *root = libical_component_from_file(f);
        fclose(f);

        /* find the specific component we are interested in, using the uid */
        icalcomponent *c = icalcomponent_get_first_component(root, type);
        while (c) {
            const char *c_uid = icalcomponent_get_uid(c);
            if (strcmp(c_uid, uid) == 0) { // found it
                /* populate component with new values */
                conv_cb(obj, c);
                icalcomponent_set_uid(c, uid);
                break;
            }
            c = icalcomponent_get_next_component(root, type);
        }

        /* serialize and write back the component */
        char *result = icalcomponent_as_ical_string(root);
        fprintf(stderr, "Writing existing %s\n", buf);
        f = fopen(buf, "w");
        fputs(result, f);
        fclose(f);
        icalcomponent_free(root);

        return 1;
    } else { /* handle saving to a new file */
        if (!uid_gen) {
            fprintf(stderr, "uid specified, but not found. aborting.\n");
            return -1;
        }

        /* create the component */
        icalcomponent *comp = icalcomponent_new(type);
        conv_cb(obj, comp);
        icalcomponent_set_uid(comp, uid);
        if (! icalcomponent_get_summary(comp)) return -1;
        if (! icalcomponent_get_uid(comp)) return -1;
        /*
        if (! icaltime_is_valid_time(icalcomponent_get_dtstart(comp))) return -1;
        if (! icaltime_is_valid_time(icalcomponent_get_dtend(comp))) return -1;
        */

        /* create a frame, serialize, and save to file */
        icalcomponent *calendar = icalcomponent_vanew(
            ICAL_VCALENDAR_COMPONENT,
            icalproperty_new_version("2.0"),
            icalproperty_new_prodid("-//ABC Corporation//gui_calendar//EN"),
            comp,
            NULL
        );
        char *result = icalcomponent_as_ical_string(calendar);
        fprintf(stderr, "Writing new %s\n", buf);
        FILE *f = fopen(buf, "w");
        fputs(result, f);
        fclose(f);
        icalcomponent_free(calendar);

        return 2;
    }
}

int save_event(struct event ev, char **uid_ptr, struct calendar *cal, bool del,
        time_t recurrence_id) {
    int res = save_component(
        (void*)&ev, uid_ptr, cal, del,
        ICAL_VEVENT_COMPONENT, &ev_to_comp);
    if (res < 0) {
        destruct_event(&ev);
        return res;
    }
    struct event_recur_set *ers = NULL;
    int hres = hashmap_get(cal->event_sets, *uid_ptr, (void**)&ers);
    if (del) {
        assert(res == 0, "wrong result for delete");
        assert(hres == MAP_OK, "event missing on delete");
        hashmap_remove(cal->event_sets, *uid_ptr);
        free_event_recur_set(ers);
        destruct_event(&ev);
        fprintf(stderr, "event_recur_set deleted %s\n", *uid_ptr);
    } else if (res == 1) {
        /* update the component in memory */
        assert(hres == MAP_OK, "uid not found");
        destruct_event(&ers->base);
        ers->base = ev;
    } else if (res == 2) {
        assert(*uid_ptr, "did not get an uid");
        assert(hres == MAP_MISSING && ers == NULL, "event exists");
        ers = new_event_recur_set(*uid_ptr, 0);
        ers->base = ev;
        hashmap_put(cal->event_sets, ers->uid, ers);
    } else {
        assert(false, "unexpected return code");
    }
    return 0;
}

int save_todo(struct todo td, struct calendar *cal, bool del) {
    int res = save_component(
        (void*)&td, &(td.uid), cal, del,
        ICAL_VTODO_COMPONENT, &td_to_comp);
    if (res < 0) return res;
    if (del) {
        assert(res == 0, "wrong result for delete");
        hashmap_remove(cal->todos, td.uid);
        fprintf(stderr, "todo deleted %s\n", td.uid);
    } else if (res == 1) {
        /* update the component in memory */
        struct todo *t;
        assert(hashmap_get(cal->todos, td.uid, (void**)&t) == MAP_OK,
                "uid not found");
        // free_event_props(e); //TODO: memory leak
        t->summary = td.summary;
        t->start = td.start;
        t->due = td.due;
        t->desc = td.desc;
        t->clas = td.clas;
        t->status = td.status;
    } else if (res == 2) {
        assert(td.uid, "did not get an uid");
        struct todo *new_td = malloc(sizeof(struct todo));
        memcpy(new_td, &td, sizeof(struct todo));
        hashmap_put(cal->todos, new_td->uid, new_td);
    }
    return 0;
}

static int todo_cmp(const void *pa, const void *pb) {
    struct todo *a = *(struct todo**)pa, *b = *(struct todo**)pb;
    if (a->due.timestamp > 0 || b->due.timestamp > 0) {
        if (a->due.timestamp < 0) return 1;
        else if (b->due.timestamp < 0) return -1;
        return a->due.timestamp - b->due.timestamp;
    }
    return 0;
}
void priority_sort_todos(struct todo **todos, int n) {
    qsort(todos, n, sizeof(struct todo*), todo_cmp);
}
