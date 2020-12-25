#ifndef GUI_CALENDAR_EDITOR_H
#define GUI_CALENDAR_EDITOR_H
#include "calendar.h"

/* This struct specifies an edit to the calendar, applicable to
 * both the in-memory, and the filesystem representation unambiguously */

enum edit_method {
	EDIT_METHOD_CREATE,
	EDIT_METHOD_UPDATE,
	EDIT_METHOD_DELETE
};

struct edit_spec {
	/* one of these optionally specify the calendar */
	struct str calendar_uid; /* use this, if not null */
	int calendar_num; /* use this, if nonnegative */

	/* these together specify the component */
	struct str uid;
	time_t recurrence_id; /* valid if greater than 0 */

	/* this specifies the action
	 * CREATE:
	 *	 IF uid is empty, create a brand new component
	 *	 IF uid is specified, recurrence_id must be valid, but the recurrence
	 *	 set must not contain it. add an RDATE, and create the special
	 *	 instance
	 *	 ACTION: set all props from p
	 * UPDATE:
	 *	 uid must not be empty
	 *	 IF recurrence_id is invalid, update the base instance
	 *	 IF recurrence_id is valid, update the special instance (possibly
	 *	 creating it). recurrence_id must be contained in the recurrence set
	 *	 `update` member is valid
	 *	 ACTION: update all props from p
	 *	 ACTION: remove all props in rem
	 * DELETE:
	 *	 uid must not be empty
	 *	 IF recurrence_id is invalid, delete the entire recurrence set
	 *	 IF recurrence_id is valid, delete the special instance if it exists,
	 *	 and add an EXDATE to the recurrence set
	 */
	enum edit_method method;

	enum comp_type type;
	struct props p;
	struct props_mask rem;
};

/* edit spec object */
void edit_spec_init(struct edit_spec *es);
void edit_spec_finish(struct edit_spec *es);

/* editor stuff */
void print_template(FILE *f, struct comp_inst *ci,
		struct cal_timezone *zone, int cal);
void print_new_event_template(FILE *f, struct cal_timezone *zone, int cal);
void print_new_todo_template(FILE *f, struct cal_timezone *zone, int cal);
int edit_spec_init_parse(struct edit_spec *es, FILE *f,
	struct cal_timezone *zone, ts now);

int edit_spec_apply_to_storage(struct edit_spec *es,
		struct calendar *cal);
int apply_edit_spec_to_calendar(struct edit_spec *es, struct calendar *cal);

/* other utils */
struct simple_date parse_date(const char *str);

#endif
