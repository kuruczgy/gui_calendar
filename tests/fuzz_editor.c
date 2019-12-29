#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "util.h"

int main() {
    struct event ev;
    bool del = (bool)123;
    char *uid;
    parse_event_template(stdin, &ev, NULL, &del, &uid);
}
