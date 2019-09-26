#include "parse.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#include "util.h"

static struct date no_date = { .timestamp = -1 };

static void print_date(FILE *f, struct date d) {
    /* fprintf(f, "%d-%d-%d %d:%d:%d%s",
            d.y, d.m, d.d,
            d.h, d.min, d.s,
            d.utc ? " UTC" : ""); */
}

void parse_dir(char *path, struct calendar *cal) {
    struct stat sb;
    assert(stat(path, &sb) == 0, "stat");

    cal->events = NULL;
    cal->name = NULL;
    cal->tail = &(cal->events);
    if (S_ISREG(sb.st_mode)) { // file
        FILE *f = fopen(path, "rb");
        libical_parse_ics(f, cal);
        fclose(f);
    } else {
        assert(S_ISDIR(sb.st_mode), "not dir");
        DIR *d;
        struct dirent *dir;
        char buf[1024];
        assert(d = opendir(path), "opendir");
        while(dir = readdir(d)) {
            if (dir->d_type != DT_REG) continue;
            int l = strlen(dir->d_name);
            bool displayname = false;
            if (!( l >= 4 && strcmp(dir->d_name + l - 4, ".ics") == 0 )) {
                if (strcmp(dir->d_name, "displayname") == 0) {
                    displayname = true;
                } else {
                    continue;
                }
            }
            snprintf(buf, 1024, "%s/%s", path, dir->d_name);
            FILE *f = fopen(buf, "rb");
            assert(f, "could not open");
            if (displayname) {
                int cnt = fread(buf, 1, 1024, f);
                assert(cnt > 0, "meta");
                assert(cal->name == NULL, "name null");
                cal->name = malloc(cnt+1);
                memcpy(cal->name, buf, cnt);
                cal->name[cnt] = '\0';
            } else {
                libical_parse_ics(f, cal);
            }
            fclose(f);
        }
        assert(closedir(d) == 0, "closedir");
    }
}

