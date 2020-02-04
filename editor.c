#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "calendar.h"
#include "editor.h"
#include "util.h"

static void print_time_prop(char *buf, size_t size, struct simple_date sd) {
    if (sd.year == -1) {
        buf[0] = '\0';
    } else {
        snprintf(buf, size, "%04d-%02d-%02d %02d:%02d",
            sd.year, sd.month, sd.day, sd.hour, sd.minute);
    }
}
static const char * status_str(enum icalproperty_status v) {
    switch (v) {
    case ICAL_STATUS_TENTATIVE: return "tentative";
    case ICAL_STATUS_CONFIRMED: return "confirmed";
    case ICAL_STATUS_CANCELLED: return "cancelled";
    case ICAL_STATUS_COMPLETED: return "completed";
    case ICAL_STATUS_NEEDSACTION: return "needs-action";
    default: return "";
    }
}
static const char * class_str(enum icalproperty_class v) {
    switch (v) {
    case ICAL_CLASS_PRIVATE: return "private";
    case ICAL_CLASS_PUBLIC: return "public";
    default: return "";
    }
}
static void print_literal(FILE *f, const char *key, char *val) {
    if (val) {
        fprintf(f, "%s `%s`\n", key, val);
    } else {
        fprintf(f, "#%s\n", key);
    }
}

static const char *event_usage =
    "# USAGE:\n"
    "# status tentative/confirmed/cancelled\n"
    "# class public/private\n"
    "# summary/location/desc/color `...`\n";
static const char *todo_usage =
    "# USAGE:\n"
    "# status completed/needs-action\n"
    "# class public/private\n"
    "# summary/location/desc/color `...`\n";


void print_event_template(FILE *f, struct event *ev, const char *uid,
        time_t recurrence_id, icaltimezone *zone) {
    struct simple_date
        start_sd = simple_date_from_timet(ev->start.timestamp, zone),
        end_sd = simple_date_from_timet(ev->end.timestamp, zone);
    char start[32], end[32];
    print_time_prop(start, 32, start_sd);
    print_time_prop(end, 32, end_sd);

    fprintf(f, "update event\n");
    print_literal(f, "summary", ev->summary);
    fprintf(f, "start %s\n", start);
    fprintf(f, "end %s\n", end);
    print_literal(f, "location", ev->location);
    print_literal(f, "desc", ev->desc);
    print_literal(f, "color", ev->color_str);
    fprintf(f, "#class %s\n", class_str(ev->clas));
    fprintf(f, "#status %s\n", status_str(ev->status));
    if (recurrence_id != -1) {
        fprintf(f, "#instance: `%ld`\n", recurrence_id);
    }
    if (uid) {
        fprintf(f, "uid `%s`\n", uid);
    }
    fprintf(f, "%s", event_usage);
}
void print_todo_template(FILE *f, struct todo *td, icaltimezone *zone) {
    struct simple_date
        start_sd = simple_date_from_timet(td->start.timestamp, zone),
        due_sd = simple_date_from_timet(td->due.timestamp, zone);
    char start[32], due[32];
    print_time_prop(start, 32, start_sd);
    print_time_prop(due, 32, due_sd);

    fprintf(f, "update todo\n");
    print_literal(f, "summary", td->summary);
    fprintf(f, "#status %s\n", status_str(td->status));
    fprintf(f, "#due %s\n", due);
    fprintf(f, "#start %s\n", start);
    print_literal(f, "desc", td->desc);
    fprintf(f, "#class %s\n", class_str(td->clas));
    if (td->uid) {
        fprintf(f, "uid `%s`\n", td->uid);
    }
    fprintf(f, "%s", todo_usage);
}

void print_new_event_template(FILE *f, icaltimezone *zone) {
    char start[32];
    struct simple_date now = simple_date_now(zone);
    snprintf(start, 32, "%04d-%02d-%02d", now.year, now.month, now.day);

    fprintf(f,
        "create event\n"
        "summary\n"
        "start %s\n"
        "end \n"
        "#location\n"
        "#desc\n"
        "#color\n"
        "#class\n"
        "#status\n"
        "#calendar\n"
        "%s"
        "# calendar 1/2/...\n",
        start,
        event_usage
    );
}
void print_new_todo_template(FILE *f, icaltimezone *zone) {
    char start[32];
    struct simple_date now = simple_date_now(zone);
    snprintf(start, 32, "%04d-%02d-%02d", now.year, now.month, now.day);

    fprintf(f,
        "create todo\n"
        "summary\n"
        "#status\n"
        "#due %s\n"
        "#start %s\n"
        "#desc\n"
        "#class\n"
        "#calendar\n"
        "%s"
        "# calendar 1/2/...\n",
        start, start,
        todo_usage
    );
}

void init_edit_spec(struct edit_spec *es) {
    init_event(&es->ev);
    init_event(&es->rem_ev);

    init_todo(&es->td);
    init_todo(&es->rem_td);

    es->calendar_uid = NULL;
    es->calendar_num = -1;
    es->uid = NULL;
    es->recurrence_id = -1;
}

int check_edit_spec(struct edit_spec *es) {
    if (!es->uid) return -1;
    if (es->type == COMP_TYPE_EVENT) {
        if (es->ev.start.timestamp >= es->ev.end.timestamp) return -1;
    } else if (es->type == COMP_TYPE_TODO) {

    } else {
        return -1;
    }
    return 0;
}

void assign_str(char **dst, char *src, char *rem) {
    if (rem) {
        free(*dst);
        *dst = NULL;
    } else if (src) {
        free(*dst);
        *dst = str_dup(src);
    }
}
void assign_date(struct date *dst, struct date src, struct date rem) {
    if (rem.timestamp != -1) {
        dst->timestamp = -1;
    } else if (src.timestamp != -1) {
        dst->timestamp = src.timestamp;
    }
}
void assign_event_props(struct event *dst, struct event *src,
        struct event *rem) {
    // DEP: struct event
    assign_str(&dst->summary, src->summary, rem->summary);
    assign_date(&dst->start, src->start, rem->start);
    assign_date(&dst->end, src->end, rem->end);
    assign_str(&dst->color_str, src->color_str, rem->color_str);
    assign_str(&dst->location, src->location, rem->location);
    assign_str(&dst->desc, src->desc, rem->desc);
    if (rem->status != ICAL_STATUS_NONE) {
        dst->status = ICAL_STATUS_NONE;
    } else if (src->status != ICAL_STATUS_NONE) {
        dst->status = src->status;
    }
    if (rem->clas != ICAL_CLASS_NONE) {
        dst->clas = ICAL_CLASS_NONE;
    } else if (src->clas != ICAL_CLASS_NONE) {
        dst->clas = src->clas;
    }
    // all_day is ignored, since it is not nullable
}
void assign_todo_props(struct todo *dst, struct todo *src, struct todo *rem) {
    // DEP: struct todo
    // don't touch uid
    assign_str(&dst->summary, src->summary, rem->summary);
    assign_str(&dst->desc, src->desc, rem->desc);
    assign_date(&dst->start, src->start, rem->start);
    assign_date(&dst->due, src->due, rem->due);
    if (rem->status != ICAL_STATUS_NONE) {
        dst->status = ICAL_STATUS_NONE;
    } else if (src->status != ICAL_STATUS_NONE) {
        dst->status = src->status;
    }
    if (rem->clas != ICAL_CLASS_NONE) {
        dst->clas = ICAL_CLASS_NONE;
    } else if (src->clas != ICAL_CLASS_NONE) {
        dst->clas = src->clas;
    }
}

static void apply_to_memory(struct edit_spec *es, struct calendar *cal) {
    int res;
    if (es->type == COMP_TYPE_EVENT) {
        struct event_recur_set *ers;
        assert(es->uid, "no uid in edit_spec");
        res = hashmap_get(cal->event_sets, es->uid, (void**)&ers);
        switch (es->method) {
        case EDIT_METHOD_UPDATE:
            assert(res == MAP_OK, "not found");
            assign_event_props(&ers->base, &es->ev, &es->rem_ev);
            event_update_derived(&ers->base);
            fprintf(stderr, "[editor memory] updated event %s\n", es->uid);
            break;
        case EDIT_METHOD_CREATE:
            assert(res == MAP_MISSING, "creating, but found");
            ers = new_event_recur_set(es->uid, 0);
            copy_event(&ers->base, &es->ev);
            event_update_derived(&ers->base);
            res = hashmap_put(cal->event_sets, ers->uid, ers);
            assert(res == MAP_OK, "failed to put");
            fprintf(stderr, "[editor memory] created event %s\n", es->uid);
            break;
        case EDIT_METHOD_DELETE:
            assert(res == MAP_OK, "deleting, but not found");
            res = hashmap_remove(cal->event_sets, es->uid);
            assert(res == MAP_OK, "deleting error");
            free_event_recur_set(ers);
            fprintf(stderr, "[editor memory] deleted event %s\n", es->uid);
            break;
        default:
            assert(false, "");
            break;
        };
    } else if (es->type == COMP_TYPE_TODO) {
        struct todo *td;
        res = hashmap_get(cal->todos, es->uid, (void**)&td);
        switch (es->method) {
        case EDIT_METHOD_UPDATE:
            assert(res == MAP_OK, "todo not found");
            assign_todo_props(td, &es->td, &es->rem_td);
            fprintf(stderr, "[editor memory] updated todo %s\n", es->uid);
            break;
        case EDIT_METHOD_CREATE:
            assert(res == MAP_MISSING, "todo found");
            assert(es->uid, "uid missing");
            struct todo *new_td = malloc_check(sizeof(struct todo));
            copy_todo(new_td, &es->td);
            new_td->uid = str_dup(es->uid);
            hashmap_put(cal->todos, new_td->uid, new_td);
            fprintf(stderr, "[editor memory] created todo %s\n", es->uid);
            break;
        case EDIT_METHOD_DELETE:
            assert(res == MAP_OK, "todo not found");
            res = hashmap_remove(cal->todos, es->uid);
            assert(res == MAP_OK, "failed to remove");
            destruct_todo(td);
            free(td);
            fprintf(stderr, "[editor memory] deleted todo %s\n", es->uid);
            break;
        default:
            assert(false, "");
            break;
        }
    } else {
        assert(false, "");
    }
}


static void es_to_comp(struct edit_spec *es, icalcomponent *c) {
    if (es->type == COMP_TYPE_EVENT) {
        // DEP: struct event
        if (es->ev.summary) {
            icalcomponent_set_summary(c, es->ev.summary);
        }
        if (es->rem_ev.summary) {
            assert(false, "don't remove summary property!");
            icalcomponent_remove_properties(c, ICAL_SUMMARY_PROPERTY);
        }

        if (es->ev.start.timestamp != -1) {
            icalcomponent_set_dtstart(c,
                    icaltime_from_timet_with_zone(es->ev.start.timestamp, 0,
                        icaltimezone_get_utc_timezone()));
        }
        if (es->rem_ev.start.timestamp != -1) {
            assert(false, "don't remove start property!");
            icalcomponent_remove_properties(c, ICAL_DTSTART_PROPERTY);
        }

        if (es->ev.end.timestamp != -1) {
            icalcomponent_set_dtend(c,
                    icaltime_from_timet_with_zone(es->ev.end.timestamp, 0,
                        icaltimezone_get_utc_timezone()));
        }
        if (es->rem_ev.end.timestamp != -1) {
            assert(false, "don't remove end property!");
            icalcomponent_remove_properties(c, ICAL_DTEND_PROPERTY);
        }

        if (es->ev.color_str) {
            icalcomponent_set_color(c, es->ev.color_str);
        }
        if (es->rem_ev.color_str) {
            icalcomponent_remove_properties(c, ICAL_COLOR_PROPERTY);
        }

        if (es->ev.location) {
            icalcomponent_set_location(c, es->ev.location);
        }
        if (es->rem_ev.location) {
            icalcomponent_remove_properties(c, ICAL_LOCATION_PROPERTY);
        }

        if (es->ev.desc) {
            icalcomponent_set_description(c, es->ev.desc);
        }
        if (es->rem_ev.desc) {
            icalcomponent_remove_properties(c, ICAL_DESCRIPTION_PROPERTY);
        }

        if (es->ev.status != ICAL_STATUS_NONE) {
            icalcomponent_set_status(c, es->ev.status);
        }
        if (es->rem_ev.status != ICAL_STATUS_NONE) {
            icalcomponent_remove_properties(c, ICAL_STATUS_PROPERTY);
        }

        if (es->ev.clas != ICAL_CLASS_NONE) {
            icalcomponent_set_class(c, es->ev.clas);
        }
        if (es->rem_ev.clas != ICAL_CLASS_NONE) {
            icalcomponent_remove_properties(c, ICAL_CLASS_PROPERTY);
        }
    } else if (es->type == COMP_TYPE_TODO) {
        // DEP: struct todo

        if (es->td.summary) {
            icalcomponent_set_summary(c, es->td.summary);
        }
        if (es->rem_td.summary) {
            assert(false, "don't remove summary property!");
            icalcomponent_remove_properties(c, ICAL_SUMMARY_PROPERTY);
        }

        if (es->td.desc) {
            icalcomponent_set_description(c, es->td.desc);
        }
        if (es->rem_td.desc) {
            icalcomponent_remove_properties(c, ICAL_DESCRIPTION_PROPERTY);
        }

        if (es->td.start.timestamp != -1) {
            icalcomponent_set_dtstart(c,
                    icaltime_from_timet_with_zone(es->td.start.timestamp, 0,
                        icaltimezone_get_utc_timezone()));
        }
        if (es->rem_td.start.timestamp != -1) {
            icalcomponent_remove_properties(c, ICAL_DTSTART_PROPERTY);
        }

        if (es->td.due.timestamp != -1) {
            icalcomponent_set_due(c,
                    icaltime_from_timet_with_zone(es->td.due.timestamp, 0,
                        icaltimezone_get_utc_timezone()));
        }
        if (es->rem_td.due.timestamp != -1) {
            icalcomponent_remove_properties(c, ICAL_DUE_PROPERTY);
        }

        if (es->td.status != ICAL_STATUS_NONE) {
            icalcomponent_set_status(c, es->td.status);
        }
        if (es->rem_td.status != ICAL_STATUS_NONE) {
            icalcomponent_remove_properties(c, ICAL_STATUS_PROPERTY);
        }

        if (es->td.clas != ICAL_CLASS_NONE) {
            icalcomponent_set_class(c, es->td.clas);
        }
        if (es->rem_td.clas != ICAL_CLASS_NONE) {
            icalcomponent_remove_properties(c, ICAL_CLASS_PROPERTY);
        }
    } else {
        assert(false, "");
    }
    icalcomponent_set_uid(c, es->uid);
}

static int apply_to_storage(struct edit_spec *es, struct calendar *cal) {
    /* check if storage is directory */
    struct stat sb;
    FILE *f;
    char *result;
    char *path_base = cal->storage;
    assert(stat(path_base, &sb) == 0, "stat");
    assert(S_ISDIR(sb.st_mode), "saving to non-dir calendar not supported");

    /* construct the path to the specific .ics file */
    char path[1024];
    assert(strlen(es->uid) >= 32, "uid sanity check");
    snprintf(path, 1024, "%s/%s.ics", path_base, es->uid);

    enum icalcomponent_kind type;
    if (es->type == COMP_TYPE_EVENT) type = ICAL_VEVENT_COMPONENT;
    else if (es->type == COMP_TYPE_TODO) type = ICAL_VTODO_COMPONENT;
    else assert(false, "");

    switch(es->method) {
    case EDIT_METHOD_DELETE:
        if (unlink(path) < 0) {
            fprintf(stderr, "[editor storage] deletion failed\n");
            return -1;
        }
        fprintf(stderr, "[editor storage] deleted %s\n", path);
        return 0;
        break;
    case EDIT_METHOD_UPDATE:
        if (access(path, F_OK) != 0) {
            fprintf(stderr, "[editor storage] can't access existing file\n");
            return -1;
        }

        /* load and parse the file */
        f = fopen(path, "r");
        icalcomponent *root = libical_component_from_file(f);
        fclose(f);

        /* find the specific component we are interested in, using the uid */
        icalcomponent *c = icalcomponent_get_first_component(root, type);
        while (c) {
            const char *c_uid = icalcomponent_get_uid(c);
            if (strcmp(c_uid, es->uid) == 0) { // found it
                /* populate component with new values */
                es_to_comp(es, c);
                break;
            }
            c = icalcomponent_get_next_component(root, type);
        }

        /* serialize and write back the component */
        result = icalcomponent_as_ical_string(root);
        fprintf(stderr, "[editor storage] writing existing %s\n", path);
        f = fopen(path, "w");
        fputs(result, f);
        fclose(f);
        icalcomponent_free(root);

        return 0;
        break;
    case EDIT_METHOD_CREATE:
        ;
        /* create the component */
        icalcomponent *comp = icalcomponent_new(type);
        es_to_comp(es, comp);
        /* create a frame, serialize, and save to file */
        icalcomponent *calendar = icalcomponent_vanew(
            ICAL_VCALENDAR_COMPONENT,
            icalproperty_new_version("2.0"),
            icalproperty_new_prodid("-//ABC Corporation//gui_calendar//EN"),
            comp,
            NULL
        );
        result = icalcomponent_as_ical_string(calendar);
        fprintf(stderr, "[editor storage] writing new %s\n", path);
        f = fopen(path, "w");
        fputs(result, f);
        fclose(f);
        icalcomponent_free(calendar);

        return 0;
    default:
        assert(false, "");
        break;
    }
    assert(false, "");
    return 0;
}

int apply_edit_spec_to_calendar(struct edit_spec *es, struct calendar *cal) {
    assert(es->uid, "uid");
    fprintf(stderr, "[editor] saving %s\n", es->uid);
    if (apply_to_storage(es, cal) != 0) return -1;
    apply_to_memory(es, cal);
    return 0;
}
