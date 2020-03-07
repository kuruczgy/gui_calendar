#ifndef GUI_CALENDAR_DATETIME_H
#define GUI_CALENDAR_DATETIME_H
#include <time.h>
#include <stdbool.h>
#include <libical/ical.h>

typedef long long int ts;

/* range: [fr, to) */
struct ts_ran { ts fr, to; };

struct simple_date {
    union {
        struct {
            int year, month, day, hour, minute, second;
        };
        int t[6];
    };
};

struct simple_dur {
    int d, h, m, s;
};

struct date {
    /* the core of this structure: represents a UNIX timestamp.
     * the other `struct simple_date` values are calculated form this
     * to represent an invalid date, set timestamp to -1 */
    time_t timestamp;

    // struct tm utc_time;
    // struct tm local_time;
};

struct tm timet_to_tm_with_zone(time_t t, icaltimezone *zone);
void timet_adjust_days(time_t *t, icaltimezone *zone, int n);
time_t get_day_base(icaltimezone *zone, bool week);
struct date date_from_timet(time_t t, icaltimezone *local_zone);
struct date date_from_icaltime(icaltimetype tt, icaltimezone *local_zone);

struct simple_date make_simple_date(int y, int mo, int d, int h, int m, int s);
bool simple_date_eq(struct simple_date a, struct simple_date b);

struct simple_date simple_date_now(icaltimezone *zone);
struct simple_date simple_date_from_timet(time_t t, icaltimezone *zone);
struct simple_date simple_date_from_ts(ts t, icaltimezone *zone);
time_t simple_date_to_timet(struct simple_date sd, icaltimezone *zone);
ts simple_date_to_ts(struct simple_date sd, icaltimezone *zone);
void simple_date_normalize(struct simple_date *sd);
int simple_date_days_in_month(struct simple_date sd);
const char * simple_date_day_of_week_name(struct simple_date sd);
int simple_date_week_number(struct simple_date sd);

/* [a1, a2) and [b1, b2) intervals. */
bool ts_overlap(time_t a1, time_t a2, time_t b1, time_t b2);
ts ts_now();
bool ts_ran_overlap(struct ts_ran a, struct ts_ran b);
bool ts_ran_in(struct ts_ran a, ts t);

struct simple_dur simple_dur_from_int(int v);
int simple_dur_to_int(struct simple_dur sdu);

void format_simple_date(char *buf, size_t size, struct simple_date sd);
void print_simple_dur(FILE *f, struct simple_dur sdu);


#endif
