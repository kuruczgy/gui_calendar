#include "parse.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

struct subprocess_handle {
    pid_t pid;
    char *name;
};

static void print_time(FILE *f, struct tm *tim) {
    fprintf(f, "%04d-%02d-%02d %02d:%02d", tim->tm_year + 1900,
            tim->tm_mon + 1, tim->tm_mday, tim->tm_hour, tim->tm_min);
}

static void print_event_template(FILE *f) {
    time_t t = time(NULL);
    struct tm *tim = gmtime(&t);
    tim->tm_min = 0;
    fprintf(f, "summary:\n");
    fprintf(f, "start: ");
    print_time(f, tim);
    fprintf(f, "\nend: ");
    tim->tm_hour++;
    mktime(tim);
    print_time(f, tim);
    fprintf(f, "\nlocation:\n");
    fprintf(f, "desc:\n");
}

struct subprocess_handle* subprocess_new_event_input(
        const char *file, const char *argv[]) {
    char *name = create_tmpfile_template();
    int fd = set_cloexec_or_close(mkstemp(name));
    struct subprocess_handle *res = NULL;
    if (fd >= 0) {
        FILE *f = fdopen(fd, "w");
        print_event_template(f);
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

