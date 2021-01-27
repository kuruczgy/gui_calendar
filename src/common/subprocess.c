#define _GNU_SOURCE
#include "calendar.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <platform_utils/sys.h>

void subprocess_shell(const char *cmd, const char *const argv[]) {
	if (fork() == 0) {
		int len = 0; while (argv[len]) ++len;
		char **a = malloc(sizeof(char *) * (len + 4));
		a[0] = "sh";
		a[1] = "-c";
		a[2] = (/* mutable */ char *)cmd;
		for (int i = 0; i < len + 1; ++i) {
			a[i + 3] = (/* mutable */ char *)argv[i];
		}
		execv("/bin/sh", a);
		exit(1);
	}
}

struct subprocess_handle* subprocess_new_input(const char *file,
		const char *argv[], void (*cb)(void*, FILE*), void *ud) {
	char *name = create_tmpfile_template();
	int fd = set_cloexec_or_close(mkstemp(name));
	struct subprocess_handle *res = NULL;

	if (fd < 0) goto cleanup;

	FILE *f = fdopen(fd, "w");
	cb(ud, f);
	fclose(f);

	int pidfd = -1;
#if PU_SYS_HAS_CLONE3
	struct clone_args cl_args = {
		.flags = CLONE_PIDFD,
		.pidfd = (uint64_t)&pidfd,
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
	long pid = pu_clone3(cl_args);
#else
	long pid = -1;
#endif
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

