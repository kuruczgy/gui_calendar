#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "util.h"

int main() {
    struct calendar cal;
    init_calendar(&cal);
    libical_parse_ics(stdin, &cal, NULL);
}
