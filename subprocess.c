#include "calendar.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

struct subprocess_handle {
    pid_t pid;
    char *name;
};

struct subprocess_handle* subprocess_new_event_input(
        const char *file, const char *argv[], const struct event *template_ev) {
    char *name = create_tmpfile_template();
    int fd = set_cloexec_or_close(mkstemp(name));
    struct subprocess_handle *res = NULL;
    if (fd >= 0) {
        FILE *f = fdopen(fd, "w");
        print_event_template(f, template_ev);
        fclose(f);
        pid_t pid = fork();
        if (pid > 0) { // parent
            fprintf(stderr, "new subprocess pid: %d\n", pid);
            res = malloc(sizeof(struct subprocess_handle));
            res->pid = pid;
            res->name = name;
            name = NULL;
        } else if (pid == 0) { // child
            for (const char **arg = argv; *arg; ++arg) {
                if (strcmp(*arg, "{file}") == 0) {
                    *arg = name;
                }
            }
            execvp(file, (char**)argv); // TODO: casting away constness...
            exit(1); // exec failed
        } else { // failure
            res = NULL;
        }
    }
    free(name);
    return res;
}

FILE *subprocess_get_result(struct subprocess_handle **handle, pid_t pid) {
    if (pid != (*handle)->pid) return NULL;

    char *name = (*handle)->name;
    int fd = open(name, O_RDONLY);
    char buf[1024];
    size_t n = pread(fd, buf, 1024, 0);
    fprintf(stderr, "file read:\n%.*s===\n", n, buf);
    FILE *res = fdopen(fd, "r");
    
    unlink(name);
    free(name);
    free(*handle);
    *handle = NULL;
    return res;
}

