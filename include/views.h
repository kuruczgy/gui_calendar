#ifndef GUI_CALENDAR_VIEWS_H
#define GUI_CALENDAR_VIEWS_H
#include <ds/vec.h>
#include "datetime.h"

struct active_comp;
struct icaltimezone;

enum tobject_type {
	TOBJECT_EVENT,
	TOBJECT_TODO
};

struct tobject {
	struct ts_ran time;
	int max_n, col;
	enum tobject_type type;
	struct active_comp *ac;
};

struct tslice_lines {
	ts *s;
	int n;
};

/* represents a timeslice, along with the events, and their layouts to be
 * displayed in it.  events might be partially outside slice, but generally they
 * should at least partially overlap with it. */
struct tslice {
	struct ts_ran ran;
	struct vec objs; /* vec<struct tobject> */
	char *header_label;
	struct tslice_lines lines;

	/* maximum number of concurrent objects.
	 * calculated by tview_update_layout */
	int max_overlap;
};

struct tview {
	struct tslice *s;
	int n;
	int max; // largest tslice size

	/* the lenght of the longest slice */
	ts max_len;

	/* the min and max offset content is contained in any slice */
	ts min_content, max_content;

	/* hull of the ranges of all slices */
	struct ts_ran ran_hull;
};

enum tview_type {
	TVIEW_RANGE,
	TVIEW_DAYS,
	TVIEW_WEEKS,
	TVIEW_MONTHS,
	TVIEW_YEARS
};

struct tview_spec {
	ts base;
	enum tview_type type;
	int n; // number of slices
	int h1, h2; // from/to hour
	ts to; // range to (returned by init_tview, used by init_tview_range)
	struct cal_timezone *zone;
};

/* sequence:
 * tview_init (or tview_init_range)
 * tview_try_put for each event
 * tview_update_layout after finished adding events
 * tview_finish at the end
 */
void tview_finish(struct tview *tv);
void tview_init_range(struct tview *tv, struct tview_spec *spec);
void tview_init(struct tview *tv, struct tview_spec *spec);
bool tview_try_put(struct tview *tv, struct tobject obj);
void tview_update_layout(struct tview *tv);

#endif
