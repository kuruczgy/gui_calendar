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

static int min(int a, int b) { return a < b ? a : b; }

char *str_dup(const char *s) {
    int len = strlen(s);
    char *n = malloc(len+1);
    memcpy(n, s, len);
    n[len] = '\0';
    return n;
}

/*
static int get_line(FILE *f, char *buf, int s, int *n) {
    int i = 0, c, e = 0;
    while((c = fgetc(f)) != EOF) {
        if (c == '\r') { e = 1; continue; }
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

static int atoin(const char *s, int n) {
    char buf[16];
    int l = min(n, 15);
    memcpy(buf, s, l);
    buf[l] = '\0';
    return atoi(buf);
}

static struct date parse_date(const char *buf) {
    int len = strlen(buf);
    if (len < 8) return no_date;
    int y = atoin(buf, 4), m = atoin(buf+4, 2), d = atoin(buf+6, 2);
    int h = 0, min = 0, s = 0;
    int utc = 0;
    if (len >= 15 && buf[8] == 'T') {
        const char *b = buf+9;
        h = atoin(b, 2), min = atoin(b+2, 2), s = atoin(b+4, 2);
        if (len >= 16 && buf[15] == 'Z') utc = 1;
    }
    struct tm tm = {
        .tm_sec = s,
        .tm_min = min,
        .tm_hour = h,
        .tm_mday = d,
        .tm_mon = m - 1,
        .tm_year = y - 1900,
        .tm_isdst = -1
    };
    time_t t = mktime(&tm);
    return (struct date){
        .time = tm,
        .timestamp = t,
        .utc = utc
    };
}

static uint32_t parse_color(char *buf) {
    return lookup_color(buf);
}

char * parse_prop(char *buf, const char *name) {
    int l = strlen(name);
    if (strncmp(name, buf, l) == 0) {
        char *c = strchr(buf + l, ':');
        if (!c) return NULL;
        return c + 1;
    }
    return NULL;
}

static void parse_event(FILE *f, struct event *ev) {
    char buf[1024];
    int s = 1024, n;
    ev->uid = ev->summary = NULL;
    ev->start = ev->end = no_date;
    ev->color = 0;
    while(1) {
        char *p;
        if (get_line(f, buf, s, &n) < 0) break;
        if (strncmp("END:VEVENT", buf, n) == 0) break;
        if (p = parse_prop(buf, "DTSTART")) ev->start = parse_date(p);
        if (p = parse_prop(buf, "DTEND")) ev->end = parse_date(p);
        if (p = parse_prop(buf, "SUMMARY")) ev->summary = str_dup(p);
        if (p = parse_prop(buf, "UID")) ev->uid = str_dup(p);
        if (p = parse_prop(buf, "COLOR")) ev->color = parse_color(p);
    }
}

static void parse_calendar(FILE *f, struct calendar *cal) {
    char buf[1024];
    int s = 1024, n;
    struct event **last = cal->tail;
    while (1) {
        if (get_line(f, buf, s, &n) < 0) break;
        if (strncmp("END:VCALENDAR", buf, n) == 0) continue;
        if (strncmp("BEGIN:VEVENT", buf, n) == 0) {
            struct event *ev = malloc(sizeof(struct event));
            parse_event(f, ev);
            *last = ev;
            last = &(ev->next);
            ev->next = NULL;
            cal->n_events++;
        }
    }
    cal->tail = last;
}

void parse_ics(FILE *f, struct calendar *cal) {
    char buf[1024];
    int s = 1024, n;

    while (1) {
        if (get_line(f, buf, s, &n) < 0) break;
        if (strncmp("BEGIN:VCALENDAR", buf, n) == 0) {
            parse_calendar(f, cal);
            break;
        }
    }
}
*/

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
                cal->name[cnt+1] = '\0';
            } else {
                libical_parse_ics(f, cal);
            }
            fclose(f);
        }
        assert(closedir(d) == 0, "closedir");
    }
}

