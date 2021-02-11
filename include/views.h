#ifndef GUI_CALENDAR_VIEWS_H
#define GUI_CALENDAR_VIEWS_H
#include <ds/vec.h>
#include "datetime.h"

enum slicing_type {
	SLICING_YEAR = 0,
	SLICING_MONTH = 1,
	SLICING_DAY = 2,
	SLICING_HOUR = 3
};
struct slicing;
struct slicing *slicing_create(struct cal_timezone *zone);
void slicing_destroy(struct slicing *s);
void slicing_iter_items(struct slicing *s, void *env,
	void (*f)(void *env, struct ts_ran ran, struct simple_date label),
	enum slicing_type type, struct ts_ran ran);
struct ts_ran slicing_get_bounds(struct slicing *s, enum slicing_type type,
	struct ts_ran ran);

struct active_comp;

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

void tobject_layout(struct vec *tobjs, int *max_overlap);

#endif
