#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "calendar.h"
#include "util.h"

struct parser_param {
    void *obj;
    icaltimezone *zone;
    bool del;
};

struct prop_parser {
    const char *key;
    int (*parse)(struct prop_parser*,struct parser_param*,const char *value);
    void (*assign)(void *obj, void *val);
};

static int parse_datetime_prop(const char *s, struct date *res,
        icaltimezone *local_zone) {
    int year, month, day, hour, minute;
    int n = sscanf(s, "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute);
    if (n < 5) return -1;
    struct icaltimetype tt = {
        .year = year,
        .month = month,
        .day = day,
        .hour = hour,
        .minute = minute,
        .second = 0,
        .is_date = 0,
        .is_daylight = 0, // TODO: is this ok like this?
        .zone = local_zone
    };
    tt = icaltime_normalize(tt);
    *res = date_from_icaltime(tt, local_zone);
    return 0;
}

int parse_identity(struct prop_parser *parser, struct parser_param *p,
        const char *val) {
    char *s = strlen(val) > 0 ? str_dup(val) : NULL;
    parser->assign(p->obj, s);
    return 0;
}
int parse_datetime(struct prop_parser *parser, struct parser_param *p,
        const char *val) {
    struct date res;
    if (parse_datetime_prop(val, &res, p->zone) < 0) {
        return -1;
    }
    parser->assign(p->obj, &res);
    return 0;
}
int parse_delete(struct prop_parser *parser, struct parser_param *p,
        const char *val) {
    if (strcmp(val, "true") == 0) p->del = true;
    else p->del = false;
    return 0;
}
int parse_class(struct prop_parser *parser, struct parser_param *p,
        const char *val) {
    enum icalproperty_class clas = ICAL_CLASS_NONE;
    if (strcmp(val, "private") == 0) {
        clas = ICAL_CLASS_PRIVATE;
        parser->assign(p->obj, &clas);
    }
    return 0;
}
int parse_status(struct prop_parser *parser, struct parser_param *p,
        const char *val) {
    enum icalproperty_status status = ICAL_STATUS_NONE;
    if (strcmp(val, "1") == 0) {
        status = ICAL_STATUS_COMPLETED;
        parser->assign(p->obj, &status);
    }
    return 0;
}
int parse_event_status(struct prop_parser *parser, struct parser_param *p,
        const char *val) {
    enum icalproperty_status status = ICAL_STATUS_NONE;
    if (strcmp(val, "tentative") == 0) status = ICAL_STATUS_TENTATIVE;
    if (strcmp(val, "confirmed") == 0) status = ICAL_STATUS_CONFIRMED;
    if (strcmp(val, "cancelled") == 0) status = ICAL_STATUS_CANCELLED;

    parser->assign(p->obj, &status);
    return 0;
}

/* assignment functions */
static void assign_event_summary(void *ud, void *val) {
    ((struct event *)ud)->summary = (char *)val; }
static void assign_event_uid(void *ud, void *val) {
    ((struct event *)ud)->uid = (char *)val; }
static void assign_event_location(void *ud, void *val) {
    ((struct event *)ud)->location = (char *)val; }
static void assign_event_desc(void *ud, void *val) {
    ((struct event *)ud)->desc = (char *)val; }
static void assign_event_start(void *ud, void *val) {
    ((struct event *)ud)->start = *(struct date*)val; }
static void assign_event_end(void *ud, void *val) {
    ((struct event *)ud)->end = *(struct date*)val; }
static void assign_event_class(void *ud, void *val) {
    ((struct event *)ud)->clas = *(enum icalproperty_class*)val; }
static void assign_event_status(void *ud, void *val) {
    ((struct event *)ud)->status = *(enum icalproperty_status*)val; }

static void assign_todo_summary(void *ud, void *val) {
    ((struct todo *)ud)->summary = (char*)val; }
static void assign_todo_uid(void *ud, void *val) {
    ((struct todo *)ud)->uid = (char*)val; }
static void assign_todo_desc(void *ud, void *val) {
    ((struct todo *)ud)->desc = (char*)val; }
static void assign_todo_start(void *ud, void *val) {
    ((struct todo *)ud)->start = *(struct date*)val; }
static void assign_todo_due(void *ud, void *val) {
    ((struct todo *)ud)->due = *(struct date*)val; }
static void assign_todo_class(void *ud, void *val) {
    ((struct todo *)ud)->clas = *(enum icalproperty_class*)val; }
static void assign_todo_status(void *ud, void *val) {
    ((struct todo *)ud)->status = *(enum icalproperty_status*)val; }


static struct prop_parser event_parsers[] = {
    { "uid", &parse_identity, &assign_event_uid },
    { "summary", &parse_identity, &assign_event_summary },
    { "location", &parse_identity, &assign_event_location },
    { "desc", &parse_identity, &assign_event_desc },
    { "start", &parse_datetime, &assign_event_start },
    { "end", &parse_datetime, &assign_event_end },
    { "class", &parse_class, &assign_event_class },
    { "status", &parse_event_status, &assign_event_status },
    { "delete", &parse_delete, NULL },
    { NULL, NULL, NULL }
};

static struct prop_parser todo_parsers[] = {
    { "uid", &parse_identity, &assign_todo_uid },
    { "summary", &parse_identity, &assign_todo_summary },
    { "desc", &parse_identity, &assign_todo_desc },
    { "start", &parse_datetime, &assign_todo_start },
    { "due", &parse_datetime, &assign_todo_due },
    { "class", &parse_class, &assign_todo_class },
    { "status", &parse_status, &assign_todo_status },
    { "delete", &parse_delete, NULL },
    { NULL, NULL, NULL }
};

static char *trim_start(char *s) {
    while (isspace(*s)) s++;
    return s;
}

static char* parse_prop(char *buf, const char *name) {
    int l = strlen(name);
    if (strncmp(name, buf, l) == 0) {
        char *c = strchr(buf + l, ':');
        if (!c) return NULL;
        return trim_start(c + 1);
    }
    return NULL;
}

static int parse(FILE *f, struct prop_parser *parsers,
        struct parser_param *params) {
    char buf[1024], *p;
    int len;
    while (get_line(f, buf, 1024, &len) >= 0) {
        if (len <= 0 || buf[0] == '#') continue;
        for (int i = 0; parsers[i].key; ++i) {
            if (p = parse_prop(buf, parsers[i].key)) {
                if (parsers[i].parse(&parsers[i], params, p) < 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

int parse_event_template(FILE *f, struct event *ev, icaltimezone *zone,
        bool *del) {
    init_event(ev);
    *del = false;
    struct parser_param params = {
        .obj = (void *)ev,
        .zone = zone,
        .del = false
    };
    if (parse(f, event_parsers, &params) < 0) {
        return -1;
    }
    *del = params.del;
    return 0;
}

int parse_todo_template(FILE *f, struct todo *td, icaltimezone *zone,
        bool *del) {
    init_todo(td);
    *del = false;
    struct parser_param params = {
        .obj = (void *)td,
        .zone = zone,
        .del = false
    };
    if (parse(f, todo_parsers, &params) < 0) {
        return -1;
    }
    *del = params.del;
    return 0;
}

/* template creation */
static void print_time_prop(FILE *f, const struct tm *tim) {
    fprintf(f, "%04d-%02d-%02d %02d:%02d", tim->tm_year + 1900,
            tim->tm_mon + 1, tim->tm_mday, tim->tm_hour, tim->tm_min);
}
void print_event_template(FILE *f, const struct event *ev) {
    fprintf(f, "summary: %s\n", ev->summary ? ev->summary : "");
    fprintf(f, "start: ");
    print_time_prop(f, &ev->start.local_time);
    fprintf(f, "\nend: ");
    print_time_prop(f, &ev->end.local_time);
    fprintf(f, "\nlocation: %s\n", ev->location ? ev->location : "");
    fprintf(f, "desc: %s\n", ev->desc ? ev->desc : "");
    fprintf(f, "class: %s\n", ev->clas == ICAL_CLASS_PRIVATE ? "private" : "");
    const char *s_status;
    switch (ev->status) {
    case ICAL_STATUS_TENTATIVE: s_status = "tentative"; break;
    case ICAL_STATUS_CONFIRMED: s_status = "confirmed"; break;
    case ICAL_STATUS_CANCELLED: s_status = "cancelled"; break;
    default: s_status = ""; break;
    }
    fprintf(f, "# status: tentative / confirmed / cancelled\n");
    fprintf(f, "status: %s\n", s_status);
    if (ev->uid) {
        fprintf(f, "uid: %s\n", ev->uid);
    }
    fprintf(f, "delete: \n");
}
void print_todo_template(FILE *f, const struct todo *td) {
    fprintf(f, "summary: %s\n", td->summary ? td->summary : "");
    fprintf(f, "status: %s\n", td->status == ICAL_STATUS_COMPLETED ? "1" : "0");
    fprintf(f, "due: ");
    print_time_prop(f, &td->due.local_time);
    fprintf(f, "\n# start: ");
    print_time_prop(f, &td->start.local_time);
    fprintf(f, "\ndesc: %s\n", td->desc ? td->desc : "");
    fprintf(f, "class: %s\n", td->clas == ICAL_CLASS_PRIVATE ? "private" : "");
    if (td->uid) {
        fprintf(f, "uid: %s\n", td->uid);
    }
    fprintf(f, "delete: \n");
}
