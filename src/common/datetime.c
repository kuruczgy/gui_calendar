#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <libical/ical.h>

#include "datetime.h"
#include "core.h"
#include "calendar.h"

/* struct cal_timezone */
struct cal_timezone {
	icaltimezone *impl;
	char *desc;
};
struct cal_timezone *cal_timezone_new(const char *location) {
	struct cal_timezone *zone = malloc(sizeof(struct cal_timezone));
	zone->impl = icaltimezone_get_builtin_timezone(location);
	asrt(zone->impl, "icaltimezone_get_builtin_timezone failed\n");

	const char *tznames = icaltimezone_get_tznames(zone->impl);
	int l = strlen(location) + strlen(tznames) + 4;
	char *buf = malloc(l);
	snprintf(buf, l, "%s (%s)", location, tznames);
	zone->desc = buf;

	return zone;
}
void cal_timezone_destroy(struct cal_timezone *zone) {
	free(zone->desc);
	free(zone);
}
const char *cal_timezone_get_desc(const struct cal_timezone *zone) {
	return zone->desc;
}

static bool valid_simple_date(struct simple_date sd) {
	return
		sd.year >= 0 &&
		sd.month >= 0 &&
		sd.day >= 0 &&
		sd.hour >= 0 &&
		sd.minute >= 0 &&
		sd.second >= 0;
}
static icaltimetype ts_to_icaltime(ts t, struct cal_timezone *zone) {
	icaltimetype tt = icaltime_from_timet_with_zone((time_t)t, 0, zone->impl);
	return tt;
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
	asrt(valid_simple_date(sd), "invalid simple_date");
	struct icaltimetype tt = {
		.year = sd.year,
		.month = sd.month,
		.day = sd.day,
		.hour = sd.hour,
		.minute = sd.minute,
		.second = sd.second,
		.is_date = 0,
		.is_daylight = 0, // is this ok like this?
		// .zone = local_zone
	};
	tt = icaltime_normalize(tt);
	return tt;
}
static int simple_date_day_of_week(struct simple_date sd) {
	icaltimetype t = simple_date_to_icaltime(sd);
	return icaltime_day_of_week(t);
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

void ts_adjust_days(ts *t, struct cal_timezone *zone, int n) {
	icaltimetype tt =
		icaltime_from_timet_with_zone((time_t)*t, false, zone->impl);
	icaltime_adjust(&tt, n, 0, 0, 0);
	*t = (ts)icaltime_as_timet_with_zone(tt, zone->impl);
}

ts ts_get_day_base(ts t, struct cal_timezone *zone, bool week) {
	struct icaltimetype now = icaltime_current_time_with_zone(zone->impl);
	now.hour = now.minute = now.second = 0;
	if (week) {
		int dow = icaltime_day_of_week(now);
		int adjust = -((dow - 2 + 7) % 7);
		icaltime_adjust(&now, adjust, 0, 0, 0);
	}
	return icaltime_as_timet_with_zone(now, zone->impl);
}

struct simple_date simple_date_now(struct cal_timezone *zone) {
	struct icaltimetype tt = icaltime_current_time_with_zone(zone->impl);
	return simple_date_from_icaltime(tt);
}
ts ts_now() {
	return (ts)time(NULL);
}

struct simple_date simple_date_from_ts(ts t, struct cal_timezone *zone) {
	if (t == -1) {
		return make_simple_date(-1, -1, -1, -1, -1, -1);
	}
	icaltimetype tt = ts_to_icaltime(t, zone);
	return simple_date_from_icaltime(tt);
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

ts simple_date_to_ts(struct simple_date sd, struct cal_timezone *zone) {
	if (!valid_simple_date(sd)) return -1;
	icaltimetype tt = simple_date_to_icaltime(sd);
	return (ts)icaltime_as_timet_with_zone(tt, zone->impl);
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
		.is_daylight = 0, // is this ok like this?
	};
	tt = icaltime_normalize(tt);
	*sd = simple_date_from_icaltime(tt);
}

int simple_date_days_in_month(struct simple_date sd) {
	return icaltime_days_in_month(sd.month, sd.year);
}

bool ts_overlap(ts a1, ts a2, ts b1, ts b2) {
	return a1 < b2 && a2 > b1;
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
struct ts_ran ts_ran_hull(struct ts_ran a, struct ts_ran b) {
	a.fr = min_ts(a.fr, b.fr);
	a.to = max_ts(a.to, b.to);
	return a;
}

ts min_ts(ts a, ts b) { return a < b ? a : b; }
ts max_ts(ts a, ts b) { return a < b ? b : a; }

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
