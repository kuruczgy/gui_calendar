#include "parse.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static struct date no_date = { .timestamp = -1 };

static void print_date(FILE *f, struct date d) {
    /* fprintf(f, "%d-%d-%d %d:%d:%d%s",
            d.y, d.m, d.d,
            d.h, d.min, d.s,
            d.utc ? " UTC" : ""); */
}

static int min(int a, int b) { return a < b ? a : b; }

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

static char *str_dup(const char *s) {
    int len = strlen(s);
    char *n = malloc(len+1);
    memcpy(n, s, len);
    n[len] = '\0';
    return n;
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
    if (strcmp(buf, "magenta") == 0) return 0xFFFF00FF;
    return 0;
}

static void parse_event(FILE *f, struct event *ev) {
    char buf[1024];
    int s = 1024, n;
    ev->uid = ev->summary = NULL;
    ev->start = ev->end = no_date;
    ev->color = 0;
    while(1) {
        if (get_line(f, buf, s, &n) < 0) break;
        if (strncmp("END:VEVENT", buf, n) == 0) break;
        if (strncmp("DTSTART:", buf, 8) == 0) ev->start = parse_date(buf+8);
        if (strncmp("DTEND:", buf, 6) == 0) ev->end = parse_date(buf+6);
        if (strncmp("SUMMARY:", buf, 8) == 0) ev->summary = str_dup(buf+8);
        if (strncmp("UID:", buf, 4) == 0) ev->uid = str_dup(buf);
        if (strncmp("COLOR:", buf, 6) == 0) ev->color = parse_color(buf+6);
    }
}

static void parse_calendar(FILE *f, struct calendar *cal) {
    char buf[1024];
    int s = 1024, n;
    struct event **last = &(cal->events);
    while (1) {
        if (get_line(f, buf, s, &n) < 0) break;
        if (strncmp("END:VCALENDAR", buf, n) == 0) continue;
        if (strncmp("BEGIN:VEVENT", buf, n) == 0) {
            struct event *ev = malloc(sizeof(struct event));
            parse_event(f, ev);
            *last = ev;
            last = &(ev->next);
            ev->next = NULL;
        }
    }
}

void parse_ics(FILE *f, struct calendar *cal) {
    char buf[1024];
    int s = 1024, n;

    while (1) {
        if (get_line(f, buf, s, &n) < 0) break;
        if (strncmp("BEGIN:VCALENDAR", buf, n) == 0) {
            cal->events = NULL;
            parse_calendar(f, cal);
            break;
        }
    }
}

static int not_main() {
    struct calendar cal;
    parse_ics(stdin, &cal);

    struct event *ev = cal.events;
    while (ev = ev->next) {
        fprintf(stderr, "summary: %s\nuid: %s\nstart: ",
                ev->summary, ev->uid);
        print_date(stderr, ev->start);
        fprintf(stderr, "\n\n");
    }

    ev = cal.events;
    while (ev) {
        struct event *n = ev->next;
        free(ev);
        ev = n;
    }

    return 0;
}
