
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/random.h>
#include "util.h"

// Most of this is from the weston examples

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
    static const char template[] = "/tmpfile-XXXXXX";
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


time_t min(time_t a, time_t b) { return a < b ? a : b; }
time_t max(time_t a, time_t b) { return a < b ? b : a; }

char *str_dup(const char *s) {
    if (!s) return NULL;
    int len = strlen(s);
    char *n = malloc(len+1);
    memcpy(n, s, len);
    n[len] = '\0';
    return n;
}

int get_line(FILE *f, char *buf, int s, int *n) {
    int i = 0, c;
    while((c = fgetc(f)) != EOF) {
        if (c == '\r') continue;
        if (c == '\n') {
            *n = min(s-1, i);
            buf[*n] = '\0';
            return 0;
        }
        if (i < s) buf[i] = c;
        ++i;
    }
    return -1;
}

void generate_uid(char buf[64]) {
    static char rnd[16];
    int n = getrandom(rnd, 16, 0);
    assert(n == 16, "getrandom failed");
    for (int i = 0; i < 16; ++i)
        sprintf(buf + 2 * i, "%02x", (unsigned char)rnd[i]);
    buf[32] = '\0';
}

void
assert(bool b, const char *msg) {
    if (!b) {
        fprintf(stderr, "assert error msg: %s\n", msg);
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
        abort();
#else
        exit(1);
#endif
    }
}

/* From the iCalendar RFC: The "DTEND" property for a "VEVENT" calendar
 * component specifies the non-inclusive end of the event.
 *
 * To reflect this, we assumes [a1, a2) and [b1, b2) intervals.
 */
bool interval_overlap(time_t a1, time_t a2, time_t b1, time_t b2) {
    return a1 < b2 && a2 > b1;
}

int day_sec(struct tm t) {
    return 3600 * t.tm_hour + 60 * t.tm_min + t.tm_sec;
}
