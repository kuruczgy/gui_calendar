#ifndef _UTIL_H_
#define _UTIL_H_
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

struct layout_event {
    int start, end;
    int max_n;
    int col;
};

int
os_create_anonymous_file(off_t size);
uint32_t
lookup_color(const char *name);

void calendar_layout(struct layout_event *e, int N);

void assert(bool, const char *);

char *str_dup(const char *s);

int min(int a, int b);
int max(int a, int b);

#endif
