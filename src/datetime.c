#include <time.h>

#include "datetime.h"
#undef assert
#include "util.h"

static bool valid_simple_date(struct simple_date sd) {
    return
        sd.year >= 0 &&
        sd.month >= 0 &&
        sd.day >= 0 &&
        sd.hour >= 0 &&
        sd.minute >= 0 &&
        sd.second >= 0;
}

struct simple_date make_simple_date(int y, int mo, int d, int h, int m, int s) {
    return (struct simple_date) {
        .year = y,
        .month = mo,
        .day = d,
        .hour = h,
        .minute = m,
        .second = s
    };
}

bool simple_date_eq(struct simple_date a, struct simple_date b) {
    return
        a.year == b.year &&
        a.month == b.month &&
        a.day == b.day &&
        a.hour == b.hour &&
        a.minute == b.minute &&
        a.second == b.second;
}

void timet_adjust_days(time_t *t, icaltimezone *zone, int n) {
    icaltimetype tt = icaltime_from_timet_with_zone(*t, false, zone);
    icaltime_adjust(&tt, n, 0, 0, 0);
    *t = icaltime_as_timet_with_zone(tt, zone);
}

static struct tm tt_to_tm(icaltimetype tt) {
    return (struct tm){
        .tm_sec = tt.second,
        .tm_min = tt.minute,
        .tm_hour = tt.hour,
        .tm_mday = tt.day,
        .tm_mon = tt.month - 1,
        .tm_year = tt.year - 1900,
        .tm_wday = icaltime_day_of_week(tt) - 1,
        .tm_yday = icaltime_day_of_year(tt),
        .tm_isdst = tt.is_daylight
    };
}

struct tm timet_to_tm_with_zone(time_t t, icaltimezone *zone) {
    return tt_to_tm(icaltime_from_timet_with_zone(t, false, zone));
}

time_t get_day_base(icaltimezone *zone, bool week) {
    struct icaltimetype now = icaltime_current_time_with_zone(zone);
    now.hour = now.minute = now.second = 0;
    if (week) {
        int dow = icaltime_day_of_week(now);
        int adjust = -((dow - 2 + 7) % 7);
        icaltime_adjust(&now, adjust, 0, 0, 0);
    }
    return icaltime_as_timet_with_zone(now, zone);
}

struct date date_from_timet(time_t t, icaltimezone *local_zone) {
    if (t < 0 || t > (time_t)(1LL << 60)) { /* sanity check */
        return (struct date){ .timestamp = -1 };
    }
    // struct tm tm = *gmtime(&t);
    // icaltimetype tt2 = icaltime_from_timet_with_zone(t, 0, local_zone);
    return (struct date) {
        // .utc_time = tm,
        // .local_time = tt_to_tm(tt2),
        .timestamp = t
    };
}

struct date date_from_icaltime(icaltimetype tt, icaltimezone *local_zone) {
    if (icaltime_is_null_time(tt)) return (struct date){ .timestamp = -1 };
    time_t t = icaltime_as_timet_with_zone(tt, icaltime_get_timezone(tt));
    return date_from_timet(t, local_zone);
}

static struct simple_date simple_date_from_icaltime(icaltimetype t) {
    return (struct simple_date){
        .year = t.year,
        .month = t.month,
        .day = t.day,
        .hour = t.hour,
        .minute = t.minute,
        .second = t.second
    };
}

static icaltimetype simple_date_to_icaltime(struct simple_date sd) {
    assert(valid_simple_date(sd), "invalid simple_date");
    struct icaltimetype tt = {
        .year = sd.year,
        .month = sd.month,
        .day = sd.day,
        .hour = sd.hour,
        .minute = sd.minute,
        .second = sd.second,
        .is_date = 0,
        .is_daylight = 0, // TODO: is this ok like this?
        // .zone = local_zone
    };
    tt = icaltime_normalize(tt);
    return tt;
}

struct simple_date simple_date_now(icaltimezone *zone) {
    struct icaltimetype tt = icaltime_current_time_with_zone(zone);
    return simple_date_from_icaltime(tt);
}
ts ts_now() {
    return (ts)time(NULL);
}

struct simple_date simple_date_from_timet(time_t t, icaltimezone *zone) {
    if (t == -1) {
        return make_simple_date(-1, -1, -1, -1, -1, -1);
    }
    icaltimetype tt = icaltime_from_timet_with_zone(t, 0, zone);
    return simple_date_from_icaltime(tt);
}
struct simple_date simple_date_from_ts(ts t, icaltimezone *zone) {
    return simple_date_from_timet((time_t)t, zone);
}

time_t simple_date_to_timet(struct simple_date sd, icaltimezone *zone) {
    if (!valid_simple_date(sd)) return -1;
    icaltimetype tt = simple_date_to_icaltime(sd);
    return icaltime_as_timet_with_zone(tt, zone);
}

struct simple_dur simple_dur_from_int(int v) {
    struct simple_dur sdu;
    sdu.d = v / (3600 * 24); v %= 3600 * 24;
    sdu.h = v / 3600; v %= 3600;
    sdu.m = v / 60; v %= 60;
    sdu.s = v;
    return sdu;
}

int simple_dur_to_int(struct simple_dur sdu) {
    return sdu.d * 3600 * 24 + sdu.h * 3600 + sdu.m * 60 + sdu.s;
}

ts simple_date_to_ts(struct simple_date sd, icaltimezone *zone) {
    return (ts)simple_date_to_timet(sd, zone);
}

void simple_date_normalize(struct simple_date *sd) {
    struct icaltimetype tt = {
        .year = sd->year,
        .month = sd->month,
        .day = sd->day,
        .hour = sd->hour,
        .minute = sd->minute,
        .second = sd->second,
        .is_date = 0,
        .is_daylight = 0, // TODO: is this ok like this?
    };
    tt = icaltime_normalize(tt);
    *sd = simple_date_from_icaltime(tt);
}

int simple_date_days_in_month(struct simple_date sd) {
    return icaltime_days_in_month(sd.month, sd.year);
}

bool ts_overlap(time_t a1, time_t a2, time_t b1, time_t b2) {
    return a1 < b2 && a2 > b1;
}

static int simple_date_day_of_week(struct simple_date sd) {
    icaltimetype t = simple_date_to_icaltime(sd);
    return icaltime_day_of_week(t);
}

const char * simple_date_day_of_week_name(struct simple_date sd) {
    const char *days[] =
        { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    int dow = simple_date_day_of_week(sd);
    return days[(dow+5)%7];
}
int simple_date_week_number(struct simple_date sd) {
    icaltimetype t = simple_date_to_icaltime(sd);
    return icaltime_week_number(t);
}

bool ts_ran_overlap(struct ts_ran a, struct ts_ran b) {
    return ts_overlap(a.fr, a.to, b.fr, b.to);
}
bool ts_ran_in(struct ts_ran a, ts t) {
    return a.fr <= t && t < a.to;
}

void format_simple_date(char *buf, size_t size, struct simple_date sd) {
    if (sd.year == -1) {
        buf[0] = '\0';
    } else {
        snprintf(buf, size, "%04d-%02d-%02d %02d:%02d",
            sd.year, sd.month, sd.day, sd.hour, sd.minute);
    }
}
void print_simple_dur(FILE *f, struct simple_dur sdu) {
    if (sdu.d != 0) fprintf(f, "%dd", sdu.d);
    if (sdu.h != 0) fprintf(f, "%dh", sdu.h);
    if (sdu.m != 0) fprintf(f, "%dm", sdu.m);
    if (sdu.s != 0) fprintf(f, "%ds", sdu.s);
    if (sdu.d == 0 && sdu.h == 0 && sdu.m == 0 && sdu.s == 0)
        fprintf(f, "0s");
}
