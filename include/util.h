#ifndef GUI_CALENDAR_UTIL_H
#define GUI_CALENDAR_UTIL_H
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ds/vec.h>

#include "algo.h"

int os_create_anonymous_file(off_t size);
char* create_tmpfile_template();
int set_cloexec_or_close(int fd);
uint32_t lookup_color(const char *name, size_t len);
char *str_dup(const char *s);
void trim_end(char *s);
void generate_uid(char buf[64]);
const char * most_frequent(const struct vec *source, const char *(*cb)(void*));
void vec_sort(struct vec *v, sort_lt lt, void *cl);
struct str str_wordexp(const char *in);

/* subprocess stuff */
void subprocess_shell(const char *cmd, const char *const argv[]);
struct subprocess_handle {
	int pidfd;
	char *name;
};
struct subprocess_handle* subprocess_new_input(const char *file,
		const char *argv[], void (*cb)(void*, FILE*), void *ud);
FILE *subprocess_get_result(struct subprocess_handle **handle);

#define container_of(ptr, type, member) \
    (type *)((char *)(ptr) - offsetof(type, member))

#endif
