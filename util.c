
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
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

static int
set_cloexec_or_close(int fd)
{
	if (os_fd_set_cloexec(fd) != 0) {
		close(fd);
		return -1;
	}
	return fd;
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
	static const char template[] = "/weston-shared-XXXXXX";
	const char *path;
	char *name;
	int fd;
	int ret;

	path = getenv("XDG_RUNTIME_DIR");
	if (!path) {
		errno = ENOENT;
		return -1;
	}

	name = malloc(strlen(path) + sizeof(template));
	if (!name)
		return -1;

	strcpy(name, path);
	strcat(name, template);

	fd = create_tmpfile_cloexec(name);

	free(name);

	if (fd < 0)
		return -1;

	do {
		ret = ftruncate(fd, size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

static int min(int a, int b) { return a < b ? a : b; }

char *str_dup(const char *s) {
    if (!s) return NULL;
    int len = strlen(s);
    char *n = malloc(len+1);
    memcpy(n, s, len);
    n[len] = '\0';
    return n;
}
