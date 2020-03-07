#ifndef GUI_CALENDAR_VIEWS_H
#define GUI_CALENDAR_VIEWS_H
#include "datetime.h"

struct event;
struct event_recur_set;
struct active_event;
struct icaltimezone;

enum tobject_type {
    TOBJECT_EVENT
};

struct tobject {
    struct ts_ran time;
    int max_n, col;
    enum tobject_type type;
    union {
        struct {
            struct event_recur_set *ers;
            struct event *ev;
            struct active_event *aev;
        };
        struct {
            struct todo *td;
        };
    };
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
    struct tobject *objs;
    int max, n;
    char *header_label;
    struct tslice_lines lines;
    int max_overlap;
};

struct tview {
    struct tslice *s;
    int n;
    int max; // largest tslice size
    ts max_len, min_content, max_content;
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
    icaltimezone *zone;
};

/* sequence:
 * init_tview (or init_tview_range)
 * init_tview_slices
 * tview_try_put for each event
 * tview_update_layout after finished adding events
 * destruct_tview at the end
 */
void destruct_tview(struct tview *tv);
void init_tview_range(struct tview *tv, struct tview_spec *spec);
void init_tview(struct tview *tv, struct tview_spec *spec);
void init_tview_slices(struct tview *tv, int max);
bool tview_try_put(struct tview *tv, struct tobject obj);
void tview_update_layout(struct tview *tv);

#endif
