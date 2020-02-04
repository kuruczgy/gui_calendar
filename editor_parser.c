
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "editor.h"
#include "util.h"

/*

(* basic definitions *)
digit = ? digits ? ;
char = ? any character ? ;
ws = " ", { " " } ;
newline = "\n", { "\n" } ;

(* function `header` *)
header = ( "create" | "update" | "delete" ), ws, ( "event" | "todo" ) ;

(* function `dt` *)
year = digit, digit, digit, digit ;
twodigit = digit | digit, digit ;
date = [ [ year, "-" ], twodigit, "-" ], twodigit ;
time = twodigit, ":", twodigit ;
dt = [ date, " " ], time ;

(* function `literal` *)
literal = "`", { char - "`" }, "`" ;
(* function `integer` *)
integer = digit, { digit } ;

(* function `prop` *)
status-val = "tentative" | "confirmed" |
    "cancelled" | "needs-action" | "completed" ;
uprop =
    ( ( "start" | "end" | "due" ), ws, dt ) |
    ( ( "summary" | "location" | "desc" | "color" ), ws, literal ) |
    ( ( "uid" | "instance" ), ws, literal ) |
    ( "class", ws, ( "private" | "public" ) ) |
    ( "status", ws, status-val ) |
    ( "calendar", ws, ( integer | literal ) ) ;
remprop = "-",
    ( "start", "due", "location", "desc", "color", "class", "status" ),
    [ ws, { char - "\n" } ];
prop = uprop | remprop

(* function `grammar` *)
comment = "#", { char - "\n" } ;
grammar = header, { newline, ( prop | comment ) } ;

*/

struct parser_state {
    FILE *f;
    struct simple_date start, end, due;
};

typedef struct parser_state *st;
typedef enum {
    OK, ERROR
} res;

static int peek(st s) {
    int c = getc(s->f);
    ungetc(c, s->f);
    return c;
}

static int get(st s) {
    return getc(s->f);
}

static res eat(st s, char c) {
    int a = get(s);
    return a == c ? OK : ERROR;
}

static int get_digit(st s) {
    int c = peek(s);
    if (isdigit(c)) {
        get(s);
        return c - '0';
    }
    return -1;
}

static void get_digits(st s, int *num, int *len) {
    int d;
    *num = 0;
    *len = 0;

    while (1) {
        if ((d = get_digit(s)) < 0) break;
        *num = 10 * (*num) + d;
        *len += 1;
        if (*len > 4) return;
    }
}

static res dt(st s, struct simple_date *sd) {
    int num, len, c;
    int *t = sd->t;
    t[0] = t[1] = t[2] = t[3] = t[4] = t[5] = -1;

    get_digits(s, &num, &len);
    if (len == 1 || len == 2) {
        c = peek(s);
        if (c == '-') { /* month */
            assert(eat(s, '-') == OK, "");
            t[1] = num;
            get_digits(s, &num, &len);
            if (len != 1 && len != 2) return ERROR;
            t[2] = num;
            goto time;
        } else if (c == ':') { /* time */
            assert(eat(s, ':') == OK, "");
            t[3] = num;
            get_digits(s, &num, &len);
            if (len != 1 && len != 2) return ERROR;
            t[4] = num;
            goto done;
        } else if (c == ' ' || c == EOF) { /* day */
            t[2] = num;
            goto time;
        } else {
            return ERROR;
        }
    } else if (len == 4) { /* year */
        t[0] = num;
        if (fscanf(s->f, "-%2d-%2d", &t[1], &t[2]) != 2) return ERROR;
        if (t[1] < 0 || t[2] < 0) return ERROR;
        goto time;
    } else {
        return ERROR;
    }

    assert(false, "bad control flow");
time:
    c = peek(s);
    if (c != ' ') return OK;
    assert(eat(s, ' ') == OK, "");
    get_digits(s, &num, &len);
    if (len != 1 && len != 2) return ERROR;
    t[3] = num;
    if (eat(s, ':') != OK) return ERROR;
    get_digits(s, &num, &len);
    if (len != 1 && len != 2) return ERROR;
    t[4] = num;
    goto done;

done:
    return OK;
}

static res header(st s, enum edit_method *method, enum comp_type *type) {
    char buf1[16], buf2[16];
    if (fscanf(s->f, "%15s %15s", buf1, buf2) != 2) return ERROR;

    if (strcmp("create", buf1) == 0) {
        *method = EDIT_METHOD_CREATE;
    } else if (strcmp("update", buf1) == 0) {
        *method = EDIT_METHOD_UPDATE;
    } else if (strcmp("delete", buf1) == 0) {
        *method = EDIT_METHOD_DELETE;
    } else {
        return ERROR;
    }

    if (strcmp("event", buf2) == 0) {
        *type = COMP_TYPE_EVENT;
    } else if (strcmp("todo", buf2) == 0) {
        *type = COMP_TYPE_TODO;
    } else {
        return ERROR;
    }

    return OK;
}

static res literal(st s, char **out) {
    static char buf[16384];
    if (fscanf(s->f, "`%16383[^`]`", buf) != 1) return ERROR;
    int len = strlen(buf);
    *out = malloc_check(len + 1);
    memcpy(*out, buf, len + 1);
    return OK;
}

static res integer(st s, int *out) {
    if (fscanf(s->f, "%d", out) != 1) return ERROR;
    return OK;
}

static res parse_class(st s, enum icalproperty_class *clas) {
    char key[16];
    fscanf(s->f, "%15s", key);
    if (strcmp(key, "private") == 0) {
        *clas = ICAL_CLASS_PRIVATE;
    } else if (strcmp(key, "public") == 0) {
        *clas = ICAL_CLASS_PUBLIC;
    } else {
        return ERROR;
    }
    return OK;
}

static res parse_status(st s, enum icalproperty_status *status) {
    char key[16];
    fscanf(s->f, "%15s", key);
    if (strcmp(key, "tentative") == 0) {
        *status = ICAL_STATUS_TENTATIVE;
    } else if (strcmp(key, "confirmed") == 0) {
        *status = ICAL_STATUS_CONFIRMED;
    } else if (strcmp(key, "cancelled") == 0) {
        *status = ICAL_STATUS_CANCELLED;
    } else if (strcmp(key, "needs-action") == 0) {
        *status = ICAL_STATUS_NEEDSACTION ;
    } else if (strcmp(key, "completed") == 0) {
        *status = ICAL_STATUS_COMPLETED ;
    } else {
        return ERROR;
    }
    return OK;
}

static res prop(st s, struct edit_spec *es) {
    struct simple_date sd;
    char *str, *key;
    char buf[16];
    bool ev = es->type == COMP_TYPE_EVENT, rem = false;
    fscanf(s->f, "%15s ", buf);
    if (buf[0] == '-') rem = true, key = buf + 1;
    else key = buf;
    if (strcmp(key, "start") == 0) {
        if (rem) return es->rem_ev.start = (struct date){ 1 }, OK;
        if (dt(s, &sd) != OK) return ERROR;
        s->start = sd;
    } else if (strcmp(key, "end") == 0) {
        if (rem) return es->rem_ev.end = (struct date){ 1 }, OK;
        if (dt(s, &sd) != OK) return ERROR;
        s->end = sd;
    } else if (strcmp(key, "due") == 0) {
        if (ev) return ERROR;
        if (rem) return es->rem_td.due = (struct date){ 1 }, OK;
        if (dt(s, &sd) != OK) return ERROR;
        s->due = sd;
    } else if (strcmp(key, "summary") == 0) {
        if (rem) {
            if (ev) es->rem_ev.summary = "";
            else es->rem_td.summary = "";
            return OK;
        }
        if (literal(s, &str) != OK) return ERROR;
        if (ev) es->ev.summary = str; else es->td.summary = str;
    } else if (strcmp(key, "location") == 0) {
        if (!ev) return ERROR;
        if (rem) return es->rem_ev.location = "", OK;
        if (literal(s, &str) != OK) return ERROR;
        es->ev.location = str;
    } else if (strcmp(key, "desc") == 0) {
        if (rem) {
            if (ev) es->rem_ev.desc = "";
            else es->rem_td.desc = "";
            return OK;
        }
        if (literal(s, &str) != OK) return ERROR;
        if (ev) es->ev.desc = str; else es->td.desc = str;
    } else if (strcmp(key, "color") == 0) {
        if (rem) return es->rem_ev.color_str = "", OK;
        if (literal(s, &str) != OK) return ERROR;
        if (!ev) return ERROR;
        es->ev.color_str = str;
    } else if (strcmp(key, "uid") == 0) {
        if (rem) return ERROR;
        if (literal(s, &str) != OK) return ERROR;
        es->uid = str;
    } else if (strcmp(key, "instance") == 0) {
        if (rem) return ERROR;
        if (literal(s, &str) != OK) return ERROR;
        es->recurrence_id = atol(str);
    } else if (strcmp(key, "class") == 0) {
        if (rem) {
            if (ev) es->rem_ev.clas = ICAL_CLASS_PUBLIC;
            else es->rem_td.clas = ICAL_CLASS_PUBLIC;
            return OK;
        }
        enum icalproperty_class clas;
        if (parse_class(s, &clas) != OK) return ERROR;
        if (ev) es->ev.clas = clas; else es->td.clas = clas;
    } else if (strcmp(key, "status") == 0) {
        if (rem) {
            if (ev) es->rem_ev.status = ICAL_STATUS_CONFIRMED;
            else es->rem_td.status = ICAL_STATUS_CONFIRMED;
            return OK;
        }
        enum icalproperty_status status;
        if (parse_status(s, &status) != OK) return ERROR;
        if (ev) es->ev.status = status; else es->td.status = status;
    } else if (strcmp(key, "calendar") == 0) {
        if (rem) return ERROR;
        if (peek(s) != '`') {
            if (integer(s, &es->calendar_num) != OK) return ERROR;
        } else {
            if (literal(s, &es->calendar_uid) != OK) return ERROR;
        }
    } else {
        return ERROR;
    }

    return OK;
}

static res grammar(st s, struct edit_spec *es) {
    int c;
    if (header(s, &es->method, &es->type) != OK) return ERROR;
    while (1) {
        while ((c = peek(s)) == '\n') assert(get(s) == '\n', "");
        if (c == EOF) break;
        if (c == '#') {
            while ((c = peek(s)) != '\n') get(s);
        } else {
            if (prop(s, es) != OK) return ERROR;
        }
    }
    return OK;
}

int parse_edit_template(FILE *f, struct edit_spec *es, icaltimezone *zone) {
    struct parser_state s = { f };
    s.start = s.end = s.due = make_simple_date(-1, -1, -1, -1, -1, -1);
    init_edit_spec(es);
    if (grammar(&s, es) != OK) return -1;

    if (s.start.second == -1) s.start.second = 0;
    if (s.end.second == -1) s.end.second = 0;
    if (s.due.second == -1) s.due.second = 0;

    if (es->type == COMP_TYPE_EVENT) {
        if (s.start.year == -1 ||
            s.start.month == -1 ||
            s.start.day == -1) return -1;
        if (s.end.year == -1) s.end.year = s.start.year;
        if (s.end.month == -1) s.end.month = s.start.month;
        if (s.end.day == -1) s.end.day = s.start.day;

        es->ev.start = (struct date){
            .timestamp = simple_date_to_timet(s.start, zone)
        };
        es->ev.end = (struct date){
            .timestamp = simple_date_to_timet(s.end, zone)
        };
    }
    if (es->type == COMP_TYPE_TODO) {
        es->td.start = (struct date){
            .timestamp = simple_date_to_timet(s.start, zone)
        };
        es->td.due = (struct date){
            .timestamp = simple_date_to_timet(s.due, zone)
        };
    }

    return 0;
}

/* tests */

static void test_dt(char *in, int out[5]) {
    FILE *f = fmemopen(in, strlen(in), "r");
    struct parser_state s = { f };
    struct simple_date sd;
    res r = dt(&s, &sd);
    if (r != OK) {
        fprintf(stderr, "res not ok\n");
        goto bad;
    }
    for (int i = 0; i < 5; ++i) {
        if (sd.t[i] != out[i]) goto bad;
    }
    fclose(f);
    return;
bad:
    fprintf(stderr,
        "in: `%s`, out: %d %d %d %d %d, t: %d %d %d %d %d\n",
        in,
        out[0], out[1], out[2], out[3], out[4],
        sd.t[0], sd.t[1], sd.t[2], sd.t[3], sd.t[4]);
    assert(false, "test_dt error");
}

static void test_header(char *in, enum edit_method m, enum comp_type t) {
    enum edit_method method;
    enum comp_type type;
    FILE *f = fmemopen(in, strlen(in), "r");
    struct parser_state s = { f };

    fprintf(stderr, "test_header `%s`\n", in);
    res r = header(&s, &method, &type);
    assert(r == OK, "res not ok");

    assert(method == m, "method not ok");
    assert(type == t, "type not ok");
    fclose(f);
}

static void test_literal(char *in, char *out) {
    FILE *f = fmemopen(in, strlen(in), "r");
    struct parser_state s = { f };
    char *o = NULL;
    res r = literal(&s, &o);
    assert(r == OK, "test_literal error");
    assert(strcmp(out, o) == 0, "test_literal not equal");
    free(o);
    fclose(f);
}

static void test_prop(st s, char *in, struct edit_spec *es) {
    FILE *f = fmemopen(in, strlen(in), "r");
    s->f = f;
    prop(s, es);
    fclose(f);
}

static void test_grammar(st s, struct edit_spec *es, char *in) {
    FILE *f = fmemopen(in, strlen(in), "r");
    s->f = f;
    grammar(s, es);
    fclose(f);
}

void test_editor_parser() {
    test_dt("5", (int[5]){ -1, -1, 5, -1, -1 });
    test_dt("05", (int[5]){ -1, -1, 5, -1, -1 });
    test_dt("5-3", (int[5]){ -1, 5, 3, -1, -1 });
    test_dt("1234-5-3", (int[5]){ 1234, 5, 3, -1, -1 });
    test_dt("4321-9-13", (int[5]){ 4321, 9, 13, -1, -1 });
    test_dt("1122-94-52", (int[5]){ 1122, 94, 52, -1, -1 });

    test_dt("9:4", (int[5]){ -1, -1, -1, 9, 4 });
    test_dt("9:04", (int[5]){ -1, -1, -1, 9, 4 });
    test_dt("09:4", (int[5]){ -1, -1, -1, 9, 4 });
    test_dt("09:04", (int[5]){ -1, -1, -1, 9, 4 });

    test_dt("05 9:4", (int[5]){ -1, -1, 5, 9, 4 });
    test_dt("5-3 9:4", (int[5]){ -1, 5, 3, 9, 4 });
    test_dt("1234-56-78 90:12", (int[5]){ 1234, 56, 78, 90, 12 });

    test_header("create event", EDIT_METHOD_CREATE, COMP_TYPE_EVENT);
    test_header("update todo", EDIT_METHOD_UPDATE, COMP_TYPE_TODO);
    test_header("delete event", EDIT_METHOD_DELETE, COMP_TYPE_EVENT);

    test_literal("`asd`", "asd");
    test_literal("`a\n\nsd\n`", "a\n\nsd\n");
    test_literal("`\nhello\nworld`", "\nhello\nworld");

    struct edit_spec es;
    struct parser_state s;
    es.type = COMP_TYPE_EVENT;

    init_edit_spec(&es);
    test_prop(&s, "start 05-3 12:45", &es);
    assert(simple_date_eq(s.start, make_simple_date(-1, 5, 3, 12, 45, -1)),
        "prop start");

    init_edit_spec(&es);
    test_prop(&s, "end 4", &es);
    assert(simple_date_eq(s.end, make_simple_date(-1, -1, 4, -1, -1, -1)),
        "prop end");

    init_edit_spec(&es);
    test_prop(&s, "location `a\n\ns\n`", &es);
    assert(strcmp(es.ev.location, "a\n\ns\n") == 0, "prop location");

    init_edit_spec(&es);
    test_prop(&s, "uid `asdfg`", &es);
    assert(strcmp(es.uid, "asdfg") == 0, "prop uid");

    init_edit_spec(&es);
    test_prop(&s, "instance `12345`", &es);
    assert(es.recurrence_id == 12345, "prop instance");

    init_edit_spec(&es);
    test_prop(&s, "class private", &es);
    assert(es.ev.clas == ICAL_CLASS_PRIVATE, "prop class");

    init_edit_spec(&es);
    es.type = COMP_TYPE_TODO;
    test_prop(&s, "status needs-action", &es);
    assert(es.td.status == ICAL_STATUS_NEEDSACTION, "prop status");


    init_edit_spec(&es);
    test_grammar(&s, &es,
        "create event\n"
        "start 2020-4-5 12:00\n"
        "end 13:00\n\n"
        "calendar 12345\n"
        "-status\n"
        "summary `lol`\n"
        "# test comment\n"
        "location `somewhere`\n"
        "desc `some\nsome\ndesc`\n"
        "calendar `asdfg`\n"
    );
    assert(es.type == COMP_TYPE_EVENT, "");
    assert(simple_date_eq(s.start, make_simple_date(2020, 4, 5, 12, 0, -1)),
        "grammar prop start");
    assert(simple_date_eq(s.end, make_simple_date(-1, -1, -1, 13, 0, -1)),
        "grammar prop end");
    assert(strcmp(es.ev.summary, "lol") == 0, "");
    assert(strcmp(es.ev.location, "somewhere") == 0, "");
    assert(strcmp(es.ev.desc, "some\nsome\ndesc") == 0, "");
    assert(es.rem_ev.status != ICAL_STATUS_NONE, "");
    assert(strcmp(es.calendar_uid, "asdfg") == 0, "");
    assert(es.calendar_num == 12345, "");
}
