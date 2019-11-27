#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "util.h"

void test_parse_event(
        const char *summary,
        const char *location,
        const char *desc,
        const char *uid,
        const char *class,
        const char *delete
) {
    const char *a =
        "summary: %s\n"
        "start: 2123-01-01 08:00\n"
        "end: 2123-01-01 10:00\n"
        "location: %s\n"
        "desc: %s\n"
        "uid: %s\n"
        "class: %s\n"
        "delete: %s\n";
    char buf[1024];
    FILE *f = fmemopen(buf, 1024, "r");

    sprintf(buf, a, summary, location, desc, uid, class, delete);

    struct event ev;
    bool del = (bool)123;
    parse_event_template(f, &ev, NULL, &del);

    fprintf(stderr, "testing with:\n%s\n", buf);

    assert(strcmp(ev.summary, summary) == 0, "summary");
    assert(strcmp(ev.location, location) == 0, "location");
    assert(strcmp(ev.desc, desc) == 0, "desc");
    assert(strcmp(ev.uid, uid) == 0, "uid");

    assert(ev.start.utc_time.tm_year == 223, "start year");
    assert(ev.end.utc_time.tm_hour == 10, "end hour");

    assert(del || strcmp(delete, "true") != 0, "delete");
    assert(ev.clas == ICAL_CLASS_PRIVATE || strcmp(class, "private") != 0,
        "class");
}

void test_parse_todo(
        const char *summary,
        const char *desc,
        const char *uid,
        const char *class,
        const char *status,
        const char *delete
) {
    const char *a =
        "summary: %s\n"
        "start: 2123-01-01 08:00\n"
        "due: 2123-01-01 10:00\n"
        "desc: %s\n"
        "uid: %s\n"
        "class: %s\n"
        "status: %s\n"
        "delete: %s\n";
    char buf[1024];
    FILE *f = fmemopen(buf, 1024, "r");

    sprintf(buf, a, summary, desc, uid, class, status, delete);

    struct todo td;
    bool del = (bool)123;
    parse_todo_template(f, &td, NULL, &del);

    fprintf(stderr, "testing with:\n%s\n", buf);

    assert(strcmp(td.summary, summary) == 0, "summary");
    assert(strcmp(td.desc, desc) == 0, "desc");
    assert(strcmp(td.uid, uid) == 0, "uid");

    assert(td.start.utc_time.tm_year == 223, "start year");
    assert(td.due.utc_time.tm_hour == 10, "due hour");

    assert(del || strcmp(delete, "true") != 0, "delete");
    assert(td.clas == ICAL_CLASS_PRIVATE || strcmp(class, "private") != 0,
        "class");
    assert(td.status == ICAL_STATUS_COMPLETED || strcmp(status, "1") != 0,
        "status");
}

int main() {
    test_parse_event("sum1", "loc1", "desc1", "swso0jltwsx", "private", "true");
    test_parse_event("sum2", "asdfg", "desff", "swso0jltwsx2zpoows", "asd", "");
    test_parse_todo("sum1", "desc1", "swso0jltwsx2zpo", "private", "1", "true");
    test_parse_todo("sum2", "desff", "swso0jltwsx2zpoows5r", "pub", "0", "asd");
}
