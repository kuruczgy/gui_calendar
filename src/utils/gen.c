#include <libical/ical.h>

#include "util.h"
#include "datetime.h"
#include "editor.h"
#include "calendar.h"

/* gen <due> <start> <incr> <n> <summary> <out dir> */
int main(int argc, char **argv) {
	if (argc < 7) return 1;
	struct simple_date due = parse_date(argv[1]);
	struct simple_date start = parse_date(argv[2]);
	if (start.year == -1) start.year = due.year;
	if (start.month == -1) start.month = due.month;
	if (start.day == -1) start.day = due.day;
	due.second = start.second = 0;
	if (due.year < 0) return 1;
	int incr = atoi(argv[3]);
	int n = atoi(argv[4]);
	const char *summary = argv[5];
	const char *out_dir = argv[6];

	struct cal_timezone *zone  = cal_timezone_new("Europe/Budapest");

	char path[1024];
	for (int i = 0; i < n; i += incr) {
		ts due_base = simple_date_to_ts(due, zone);
		ts start_base = simple_date_to_ts(start, zone);
		ts_adjust_days(&due_base, zone, i);
		ts_adjust_days(&start_base, zone, i);
		icaltimetype tdue = icaltime_from_timet_with_zone(
			(time_t)due_base, 0, icaltimezone_get_utc_timezone());
		icaltimetype tstart = icaltime_from_timet_with_zone(
			(time_t)start_base, 0, icaltimezone_get_utc_timezone());

		char uid_buf[64];
		generate_uid(uid_buf);
		icalcomponent *calendar = icalcomponent_vanew(ICAL_VCALENDAR_COMPONENT,
			icalproperty_new_version("2.0"),
			icalproperty_new_prodid("gen"),
			icalcomponent_vanew(
				ICAL_VTODO_COMPONENT,
				icalproperty_new_uid(uid_buf),
				icalproperty_new_summary(summary),
				icalproperty_new_dtstart(tstart),
				icalproperty_new_due(tdue),
				NULL),
			NULL);

		snprintf(path, 1024, "%s/%s.ics", out_dir, uid_buf);
		char *result = icalcomponent_as_ical_string(calendar);
		FILE *f = fopen(path, "w");
		fputs(result, f);
		fclose(f);
		icalcomponent_free(calendar);
	}
}
