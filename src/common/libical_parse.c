#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#include "core.h"
#include "util.h"
#include "calendar.h"

enum icalproperty_class icalcomponent_get_class(icalcomponent *c) {
    icalproperty *p =
        icalcomponent_get_first_property(c, ICAL_CLASS_PROPERTY);
    return p ? icalproperty_get_class(p) : ICAL_CLASS_NONE;
}

void icalcomponent_set_class(icalcomponent *c, enum icalproperty_class v) {
    icalproperty *p =
        icalcomponent_get_first_property(c, ICAL_CLASS_PROPERTY);
    if (v != ICAL_CLASS_NONE) {
        if (!p) {
            p = icalproperty_new_class(v);
            icalcomponent_add_property(c, p);
        } else {
            icalproperty_set_class(p, v);
        }
    } else {
        if (p) {
            icalcomponent_remove_property(c, p);
        }
    }
}

void icalcomponent_set_color(icalcomponent *c, const char *v) {
    icalproperty *p =
        icalcomponent_get_first_property(c, ICAL_COLOR_PROPERTY);
    if (v) {
        if (!p) {
            p = icalproperty_new_color(v);
            icalcomponent_add_property(c, p);
        } else {
            icalproperty_set_color(p, v);
        }
    } else {
        if (p) {
            icalcomponent_remove_property(c, p);
        }
    }
}

void icalcomponent_set_estimatedduration(icalcomponent *c,
        struct icaldurationtype v) {
    icalproperty *p = icalcomponent_get_first_property(c,
            ICAL_ESTIMATEDDURATION_PROPERTY);
    if (!p) {
        p = icalproperty_new_estimatedduration(v);
        icalcomponent_add_property(c, p);
    } else {
        icalproperty_set_estimatedduration(p, v);
    }
}

void icalcomponent_set_percentcomplete(icalcomponent *c, int v) {
    icalproperty *p = icalcomponent_get_first_property(c,
            ICAL_PERCENTCOMPLETE_PROPERTY);
    if (!p) {
        p = icalproperty_new_percentcomplete(v);
        icalcomponent_add_property(c, p);
    } else {
        icalproperty_set_percentcomplete(p, v);
    }
}

void icalcomponent_remove_properties(icalcomponent *c, icalproperty_kind kind) {
    icalproperty *p;
    while (p = icalcomponent_get_first_property(c, kind)) {
        icalcomponent_remove_property(c, p);
    }
}

struct cal_timezone *new_timezone(const char *location) {
    struct cal_timezone *zone = malloc(sizeof(struct cal_timezone));
    zone->impl = icaltimezone_get_builtin_timezone(location);
    asrt(zone->impl, "icaltimezone_get_builtin_timezone failed\n");

    const char *tznames = icaltimezone_get_tznames(zone->impl);
    int l = strlen(location) + strlen(tznames) + 4;
    char *buf = malloc(l);
    snprintf(buf, l, "%s (%s)", location, tznames);
    zone->desc = buf;

    return zone;
}

const char *get_timezone_desc(struct cal_timezone *zone) {
    return zone->desc;
}

char* read_stream(char *s, size_t size, void *d)
{
    return fgets(s, size, (FILE*)d);
}

static struct cats get_cats(icalcomponent *c) {
    char *s = NULL;
    int len = 0;
    icalproperty *p =
        icalcomponent_get_first_property(c, ICAL_CATEGORIES_PROPERTY);
    while (p) {
        const char *text = icalproperty_get_categories(p);
        int l = strlen(text);
        s = realloc(s, len + l + 2);
        if (len == 0) {
            memcpy(s, text, l);
            len += l;
        } else {
            s[len++] = ',';
            memcpy(s + len, text, l);
            len += l;
        }
        s[len] = 0;
        p = icalcomponent_get_next_property(c, ICAL_CATEGORIES_PROPERTY);
    }
    struct cats res;
    cats_init(&res, s);
    free(s);
    return res;
}

int libical_parse_event(icalcomponent *c, struct calendar *cal,
        icaltimezone *local_zone) {
    // DEP: struct event
    const char *uid = icalcomponent_get_uid(c);
    if (!uid) return -1;
    icalproperty *recurrenceid =
        icalcomponent_get_first_property(c,ICAL_RECURRENCEID_PROPERTY);
    icalproperty *rrule=icalcomponent_get_first_property(c,ICAL_RRULE_PROPERTY);
    icalproperty *rdate=icalcomponent_get_first_property(c,ICAL_RDATE_PROPERTY);
    icalproperty *exdate =
        icalcomponent_get_first_property(c,ICAL_EXDATE_PROPERTY);

    struct event_recur_set *ers = NULL;
    struct event *ev = NULL;
    if (hashmap_get(cal->event_sets, uid, (void**)&ers) == MAP_OK) {
        /* already have this uid */
        if (recurrenceid) {
            /* we have to change the recurrence-id instance */
            if (rrule || rdate || exdate) {
                fprintf(stderr, "error: unexpected property");
                return -1;
            }
            icaltimetype rid_tt = icalproperty_get_datetime_with_component(
                    recurrenceid, c);
            struct event_recur_instance *eri =
                malloc_check(sizeof(struct event_recur_instance));
            *eri = (struct event_recur_instance){
                .next = NULL,
                .recurrence_id = icaltime_as_timet_with_zone(
                    rid_tt, icaltime_get_timezone(rid_tt))
            };

            /* append to linked list */
            struct event_recur_instance **next = &ers->instances;
            while(*next) next = &(*next)->next;
            *next = eri;

            ev = &eri->ev;
        } else {
            /* we overwrite the whole recurrence set */
            hashmap_remove(cal->event_sets, uid);
            free_event_recur_set(ers);
            ers = NULL;
        }
    }

    if (!ers) {
        /* we create the whole recurrence set */
        ers = event_recur_set_create(uid, rrule ? 100 : 0);
        ev = &ers->base;
        hashmap_put(cal->event_sets, ers->uid, ers);
    }
    asrt(ev && ers, "");

    /* extract info into an event object */
    event_init(ev);
    struct icaltimetype
        dtstart = icalcomponent_get_dtstart(c),
        dtend = icalcomponent_get_dtend(c);
    ev->start = date_from_icaltime(dtstart, local_zone);
    ev->end = date_from_icaltime(dtend, local_zone);
    ev->all_day = icaltime_is_date(dtstart);
    if (ev->end.timestamp - ev->start.timestamp >= 3600 * 24) {
        // all day heuristic
        ev->all_day = true;
    }
    if (ev->start.timestamp < 0 || ev->end.timestamp < 0) {
        fprintf(stderr, "warning: invalid start or end date\n");
        goto err;
    }
    if (ev->start.timestamp >= ev->end.timestamp) {
        fprintf(stderr, "warning: event ends before it begins\n");
        goto err;
    }
    ev->summary = str_dup(icalcomponent_get_summary(c));
    ev->color = 0;
    ev->location = str_dup(icalcomponent_get_location(c));
    ev->desc = str_dup(icalcomponent_get_description(c));
    ev->clas = icalcomponent_get_class(c);
    icalproperty_status pstatus = icalcomponent_get_status(c);
    ev->status = pstatus;
    icalproperty *p = icalcomponent_get_first_property(c, ICAL_COLOR_PROPERTY);
    if (p) {
        icalvalue *v = icalproperty_get_value(p);
        const char *text = icalvalue_get_text(v);
        ev->color = lookup_color(text);
        ev->color_str = str_dup(text);
    }
    ev->cats = get_cats(c);

    if (rrule) {
        if (rdate || exdate) {
            fprintf(stderr, "warning: RDATE and EXDATE not supported\n");
        }
        struct icalrecurrencetype recur = icalproperty_get_rrule(rrule);
        icalrecur_iterator *ritr = icalrecur_iterator_new(recur, dtstart);
        struct icaltimetype next;
        int n = 0;
        while (next = icalrecur_iterator_next(ritr), ++n,
                (!icaltime_is_null_time(next) && n < 100)) {
            if (ers->n == ers->max) break;
            struct date start2 = date_from_icaltime(next, local_zone);
            ers->set[ers->n++] = start2.timestamp;
        }
        icalrecur_iterator_free(ritr);
    }
    return 0;
err:
    /* remove & cleanup */
    hashmap_remove(cal->event_sets, ers->uid);
    free_event_recur_set(ers);
    return -1;
}

int libical_parse_todo(icalcomponent *c, struct calendar *cal,
        icaltimezone *local_zone) {
    // DEP: struct todo
    struct todo *td = malloc(sizeof(struct todo));
    todo_init(td);

    struct icaltimetype
        dtstart = icalcomponent_get_dtstart(c),
        due = icalcomponent_get_due(c);
    td->start = date_from_icaltime(dtstart, local_zone);
    td->due = date_from_icaltime(due, local_zone);

    td->uid = str_dup(icalcomponent_get_uid(c));
    if (!td->uid) {
        free(td);
        fprintf(stderr, "warning: todo missing uid!\n");
        return -1;
    }
    td->summary = str_dup(icalcomponent_get_summary(c));
    td->desc = str_dup(icalcomponent_get_description(c));

    icalproperty_status s = icalcomponent_get_status(c);
    td->status = s;
    td->clas = icalcomponent_get_class(c);

    icalproperty *p = icalcomponent_get_first_property(c,
            ICAL_ESTIMATEDDURATION_PROPERTY);
    if (p) {
        struct icaldurationtype v = icalproperty_get_estimatedduration(p);
        td->estimated_duration = icaldurationtype_as_int(v);
    }

    p = icalcomponent_get_first_property(c, ICAL_PERCENTCOMPLETE_PROPERTY);
    if (p) {
        int v = icalproperty_get_percentcomplete(p);
        td->percent_complete = v;
    }

    td->cats = get_cats(c);

    hashmap_put(cal->todos, td->uid, td); // TODO: memory leak
    return 0;
}

icalcomponent* libical_component_from_file(FILE *f) {
    icalparser *parser = icalparser_new();
    icalparser_set_gen_data(parser, f);
    icalcomponent *root = icalparser_parse(parser, read_stream);
    icalparser_free(parser);
    return root;
}

int libical_parse_ics(FILE *f, struct calendar *cal,
        icaltimezone *local_zone) {
    icalcomponent *root = libical_component_from_file(f);
    if (!root) return -1;
    icalcomponent *c = icalcomponent_get_first_component(
        root, ICAL_ANY_COMPONENT);
    while(c) {
        if (icalcomponent_isa(c) == ICAL_VEVENT_COMPONENT) {
            if (libical_parse_event(c, cal, local_zone) < 0) return -1;
        } else if (icalcomponent_isa(c) == ICAL_VTODO_COMPONENT) {
            if (libical_parse_todo(c, cal, local_zone) < 0) return -1;
        }
        c = icalcomponent_get_next_component(root, ICAL_ANY_COMPONENT);
    }
    icalcomponent_free(root);
    return 0;
}

void free_timezone(struct cal_timezone *zone) {
    free(zone->desc);
    free(zone);
}

static bool timespec_leq(struct timespec a, struct timespec b) {
    if (a.tv_sec == b.tv_sec) return a.tv_nsec <= b.tv_nsec;
    return a.tv_sec <= b.tv_sec;
}

void update_calendar_from_storage(struct calendar *cal,
        icaltimezone *local_zone) {
    const char *path = cal->storage;
    struct stat sb;
    asrt(stat(path, &sb) == 0, "stat");

    struct timespec loaded = cal->loaded;
    clock_gettime(CLOCK_REALTIME, &cal->loaded);
    if (S_ISREG(sb.st_mode)) { // file
        FILE *f = fopen(path, "rb");
        if (libical_parse_ics(f, cal, local_zone) < 0) {
            fprintf(stderr, "warning: could not parse %s\n", path);
        }
        fclose(f);
    } else {
        asrt(S_ISDIR(sb.st_mode), "not dir");
        DIR *d;
        struct dirent *dir;
        int dir_fd;
        char buf[1024];
        asrt(d = opendir(path), "opendir");
        dir_fd = dirfd(d);
        while(dir = readdir(d)) {
            asrt(fstatat(dir_fd, dir->d_name, &sb, 0) == 0, "stat");
            if (!S_ISREG(sb.st_mode)) continue;
            if (!timespec_leq(loaded, sb.st_mtim)) continue;
            int l = strlen(dir->d_name);
            bool displayname = false;
            if (!( l >= 4 && strcmp(dir->d_name + l - 4, ".ics") == 0 )) {
                if (strcmp(dir->d_name, "displayname") == 0) {
                    displayname = true;
                } else if (strcmp(dir->d_name, "my_private") == 0) {
                    cal->priv = true;
                    continue;
                } else {
                    continue;
                }
            }
            if (displayname && cal->name) continue;
            snprintf(buf, 1024, "%s/%s", path, dir->d_name);
            FILE *f = fopen(buf, "rb");
            asrt(f, "could not open");
            if (displayname) {
                asrt(!cal->name, "calendar already has name");
                int cnt = fread(buf, 1, 1024, f);
                asrt(cnt > 0, "meta");
                asrt(cal->name == NULL, "name null");
                cal->name = malloc(cnt+1);
                memcpy(cal->name, buf, cnt);
                cal->name[cnt] = '\0';
            } else {
                if (libical_parse_ics(f, cal, local_zone) < 0) {
                    fprintf(stderr, "warning: could not parse %s\n", buf);
                }
            }
            fclose(f);
        }
        asrt(closedir(d) == 0, "closedir");
    }
}
