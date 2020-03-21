#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "util.h"

int main() {
    struct calendar cal;
    calendar_init(&cal);
    libical_parse_ics(stdin, &cal, NULL);
}
