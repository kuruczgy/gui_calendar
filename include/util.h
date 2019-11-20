#ifndef _UTIL_H_
#define _UTIL_H_
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

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

int min(int a, int b);
int max(int a, int b);

void generate_uid(char buf[64]);

#endif
