#ifndef GUI_CALENDAR_UTIL_H
#define GUI_CALENDAR_UTIL_H
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "vec.h"

int os_create_anonymous_file(off_t size);
char* create_tmpfile_template();
int set_cloexec_or_close(int fd);
uint32_t lookup_color(const char *name, size_t len);
char *str_dup(const char *s);
void trim_end(char *s);
void generate_uid(char buf[64]);
bool interval_overlap(time_t a1, time_t a2, time_t b1, time_t b2);
const char * most_frequent(const struct vec *source, const char *(*cb)(void*));

#endif
