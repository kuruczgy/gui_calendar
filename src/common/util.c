#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/random.h>
#include <stdio.h>

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

/* From the iCalendar RFC: The "DTEND" property for a "VEVENT" calendar
 * component specifies the non-inclusive end of the event.
 *
 * To reflect this, we assumes [a1, a2) and [b1, b2) intervals.
 */
bool interval_overlap(time_t a1, time_t a2, time_t b1, time_t b2) {
    return a1 < b2 && a2 > b1;
}

struct iter_cl {
    map_t map;
    int cnt[150];
    const char * key[150];
    int cnt_n;
    char *(*cb)(void*);
};
static int iter(void *_cl, void *data) {
    struct iter_cl *cl = _cl;
    char *key = cl->cb(data);
    int *out;
    if (!key) return MAP_OK;
    if (hashmap_get(cl->map, key, (void**)&out) == MAP_OK) {
        ++(*out);
    } else {
        if (cl->cnt_n < 150) {
            out = &cl->cnt[cl->cnt_n];
            cl->key[cl->cnt_n] = key;
            ++cl->cnt_n;
            *out = 1;
            hashmap_put(cl->map, key, out);
        }
    }
    return MAP_OK;
}

const char * most_frequent(map_t source, char *(*cb)(void*)) {
    struct iter_cl cl = { .map = hashmap_new(), .cnt_n = 0, .cb = cb };
    hashmap_iterate(source, &iter, &cl);
    hashmap_free(cl.map);

    if (cl.cnt_n == 0) return NULL;
    int maxi = 0;
    for (int i = 1; i < cl.cnt_n; ++i) {
        if (cl.cnt[maxi] < cl.cnt[i]) maxi = i;
    }
    return cl.key[maxi];
}
