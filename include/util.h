#ifndef GUI_CALENDAR_UTIL_H
#define GUI_CALENDAR_UTIL_H
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "hashmap.h"

struct layout_event {
    int start, end;
    int max_n;
    int col;
    int idx;
};

int os_create_anonymous_file(off_t size);
char* create_tmpfile_template();
int set_cloexec_or_close(int fd);
uint32_t lookup_color(const char *name);

void calendar_layout(struct layout_event *e, int N);

void assert(bool, const char *);

char *str_dup(const char *s);
int get_line(FILE *f, char *buf, int s, int *n);

time_t min(time_t a, time_t b);
time_t max(time_t a, time_t b);

void generate_uid(char buf[64]);

bool interval_overlap(time_t a1, time_t a2, time_t b1, time_t b2);
int day_sec(struct tm t);

const char * most_frequent(map_t source, char *(*cb)(void*));

inline void * malloc_check(size_t size) {
    void *p = malloc(size);
    assert(p, "oom");
    return p;
}

#endif
