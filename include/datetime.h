#ifndef GUI_CALENDAR_DATETIME_H
#define GUI_CALENDAR_DATETIME_H
#include <time.h>
#include <stdbool.h>
#include <libical/ical.h>

struct simple_date {
    union {
        struct {
            int year, month, day, hour, minute, second;
        };
        int t[6];
    };
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
time_t simple_date_to_timet(struct simple_date sd, icaltimezone *zone);

#endif
