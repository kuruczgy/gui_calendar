#ifndef GUI_CALENDAR_DATETIME_H
#define GUI_CALENDAR_DATETIME_H
#include <time.h>
#include <stdbool.h>
#include <stdio.h>

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

/* struct cal_timezone */
struct cal_timezone;
struct cal_timezone *cal_timezone_new(const char *location);
void cal_timezone_destroy(struct cal_timezone *zone);
const char *cal_timezone_get_desc(const struct cal_timezone *zone);

void ts_adjust_days(ts *t, struct cal_timezone *zone, int n);
ts ts_get_day_base(ts t, struct cal_timezone *zone, bool week);

struct simple_date make_simple_date(int y, int mo, int d, int h, int m, int s);
bool simple_date_eq(struct simple_date a, struct simple_date b);

struct simple_date simple_date_now(struct cal_timezone *zone);
struct simple_date simple_date_from_ts(ts t, struct cal_timezone *zone);
ts simple_date_to_ts(struct simple_date sd, struct cal_timezone *zone);
void simple_date_normalize(struct simple_date *sd);
int simple_date_days_in_month(struct simple_date sd);
const char * simple_date_day_of_week_name(struct simple_date sd);
int simple_date_week_number(struct simple_date sd);

/* [a1, a2) and [b1, b2) intervals. */
bool ts_overlap(ts a1, ts a2, ts b1, ts b2);
ts ts_now();
bool ts_ran_overlap(struct ts_ran a, struct ts_ran b);
bool ts_ran_in(struct ts_ran a, ts t);
struct ts_ran ts_ran_hull(struct ts_ran a, struct ts_ran b);
ts max_ts(ts a, ts b);
ts min_ts(ts a, ts b);

struct simple_dur simple_dur_from_int(int v);
int simple_dur_to_int(struct simple_dur sdu);

void format_simple_date(char *buf, size_t size, struct simple_date sd);
void print_simple_dur(FILE *f, struct simple_dur sdu);

#endif
