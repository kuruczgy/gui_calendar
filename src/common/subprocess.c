#define _GNU_SOURCE
#include "calendar.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <signal.h>

typedef unsigned long long int u64;
_Static_assert(sizeof(u64) == 8, "");
struct clone_args {
	u64 flags;
	u64 pidfd;
	u64 child_tid;
	u64 parent_tid;
	u64 exit_signal;
	u64 stack;
	u64 stack_size;
	u64 tls;
	u64 set_tid;
	u64 set_tid_size;
	u64 cgroup;
};

struct subprocess_handle* subprocess_new_input(const char *file,
		const char *argv[], void (*cb)(void*, FILE*), void *ud) {
	char *name = create_tmpfile_template();
	int fd = set_cloexec_or_close(mkstemp(name));
	struct subprocess_handle *res = NULL;

	if (fd < 0) goto cleanup;

	FILE *f = fdopen(fd, "w");
	cb(ud, f);
	fclose(f);

	int pidfd;
	struct clone_args cl_args = {
		.flags = CLONE_PIDFD,
		.pidfd = (u64)&pidfd,
		.child_tid = 0,
		.parent_tid = 0,
		.exit_signal = SIGCHLD,
		.stack = 0,
		.stack_size = 0,
		.tls = 0,
		.set_tid = 0,
		.set_tid_size = 0,
		.cgroup = 0,
	};
	long pid = syscall(__NR_clone3, &cl_args, sizeof(cl_args));
	if (pid > 0) {
		// parent
		fprintf(stderr, "new subprocess pidfd: %d\n", pidfd);
		res = malloc(sizeof(struct subprocess_handle));
		res->pidfd = pidfd;
		res->name = name;
		name = NULL;
	} else if (pid == 0) {
		// child
		for (char **arg = (char **)argv; *arg; ++arg) {
			if (strcmp(*arg, "{file}") == 0) {
				*arg = name;
			}
		}
		execvp(file, (char**)argv);
		exit(1);
	} else if (pid < 0) {
		// parent error
		fprintf(stderr, "new subprocess error!\n");
	}

cleanup:
	free(name);
	return res;
}

FILE *subprocess_get_result(struct subprocess_handle **handle) {
	close((*handle)->pidfd);

	char *name = (*handle)->name;
	int fd = open(name, O_RDONLY);
	FILE *res = fdopen(fd, "r");

	unlink(name);
	free(name);
	free(*handle);
	*handle = NULL;
	return res;
}

