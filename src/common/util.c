#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/random.h>
#include <stdio.h>
#include <ds/vec.h>
#include <ds/hashmap.h>

#include "core.h"
#include "util.h"

/* start weston section: most of this is from the weston examples */

int
os_fd_set_cloexec(int fd)
{
	long flags;
	if (fd == -1) return -1;
	flags = fcntl(fd, F_GETFD);
	if (flags == -1) return -1;
	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) return -1;
	return 0;
}

int set_cloexec_or_close(int fd) {
	if (os_fd_set_cloexec(fd) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

char* create_tmpfile_template() {
	static const char template[] = "/tmpfile-cal-XXXXXX";
	const char *path;
	char *name;

	path = getenv("XDG_RUNTIME_DIR");
	if (!path) {
		errno = ENOENT;
		return NULL;
	}

	name = malloc(strlen(path) + sizeof(template));
	if (!name) return NULL;

	strcpy(name, path);
	strcat(name, template);
	return name;
}

static int
create_tmpfile_cloexec(char *tmpname)
{
	int fd = mkstemp(tmpname);
	if (fd >= 0) {
		fd = set_cloexec_or_close(fd);
		unlink(tmpname);
	}
	return fd;
}

int
os_create_anonymous_file(off_t size)
{
	int fd;
	int ret;

	char *name = create_tmpfile_template();
	if (!name) return -1;
	fd = create_tmpfile_cloexec(name);
	free(name);

	if (fd < 0) return -1;

	do {
		ret = ftruncate(fd, size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

/* end weston section */

/* string handling stuff */

char *str_dup(const char *s) {
	if (!s) return NULL;
	int len = strlen(s);
	char *n = malloc(len+1);
	memcpy(n, s, len);
	n[len] = '\0';
	return n;
}

void trim_end(char *s) {
	asrt(s, "trim_end NULL");
	int i = strlen(s);
	while (i > 0 && isspace(s[i - 1])) --i;
	s[i] = '\0';
}

void generate_uid(char buf[64]) {
	static char rnd[16];
	int n = getrandom(rnd, 16, 0);
	asrt(n == 16, "getrandom failed");
	for (int i = 0; i < 16; ++i)
		sprintf(buf + 2 * i, "%02x", (unsigned char)rnd[i]);
	buf[32] = '\0';
}

const char * most_frequent(const struct vec *source, const char *(*cb)(void*)) {
	struct hashmap map; /* hashmap<int*> */
	hashmap_init(&map, sizeof(int*));
	struct vec cnt = vec_new_empty(sizeof(int));
	struct vec keys = vec_new_empty(sizeof(const char*));

	for (int i = 0; i < source->len; ++i) {
		const char *key = cb(vec_get((struct vec *)source, i)); /* const cast */
		if (!key) continue;
		int *k;
		int **res;
		if (hashmap_get(&map, key, (void**)&res) != MAP_OK) {
			int zero = 0;
			k = vec_get(&cnt, vec_append(&cnt, &zero));
			vec_append(&keys, &key);
			hashmap_put(&map, key, &k);
		} else {
			k = *res;
		}
		++*k;
	}


	if (cnt.len == 0) return NULL;
	int maxi = 0, val_maxi = *(int*)vec_get(&cnt, 0);
	for (int i = 1; i < cnt.len; ++i) {
		int val_i = *(int*)vec_get(&cnt, i);
		if (val_maxi < val_i) {
			maxi = i;
			val_maxi = val_i;
		}
	}
	const char *res =  *(const char **)vec_get(&keys, maxi);

	hashmap_finish(&map);
	vec_free(&cnt);
	vec_free(&keys);

	return res;
}

void vec_sort(struct vec *v, sort_lt lt, void *cl) {
	heapsort(v->d, v->len, v->itemsize, lt, cl);
}
