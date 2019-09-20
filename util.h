#ifndef _UTIL_H_
#define _UTIL_H_
#include <sys/types.h>
#include <stdint.h>

struct layout_event {
    int start, end;
    int max_n;
    int col;
};

int
os_create_anonymous_file(off_t size);
uint32_t
lookup_color(char *name);

void calendar_layout(struct layout_event *e, int N);

#endif
