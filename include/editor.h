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
    char *calendar_uid; /* use this, if not null */
    int calendar_num; /* use this, if nonnegative */

    /* these together specify the component */
    char *uid;
    time_t recurrence_id; /* valid if greater than 0 */
    /* NOTE: the uid property of struct todo is not used! */

    /* this specifies the action
     * CREATE:
     *     IF uid is NULL, create a brand new component
     *     IF uid is specified, recurrence_id must be valid, but the recurrence
     *     set must not contain it. add an RDATE, and create the special
     *     instance
     *     ACTION: set all editable props from ev/td
     * UPDATE:
     *     uid must not be NULL
     *     IF recurrence_id is invalid, update the base instance
     *     IF recurrence_id is valid, update the special instance (possibly
     *     creating it). recurrence_id must be contained in the recurrence set
     *     `update` member is valid
     *     ACTION: update all non-null (but nullable) props from ev/td
     *     ACTION: remove all non-null (but nullable) props from rem_ev/rem_td
     * DELETE:
     *     uid must not be NULL
     *     IF recurrence_id is invalid, delete the entire recurrence set
     *     IF recurrence_id is valid, delete the special instance if it exists,
     *     and add an EXDATE to the recurrence set
     */
    enum edit_method method;

    enum comp_type type;

    struct event ev;
    struct todo td;
    struct event rem_ev;
    struct todo rem_td;
};

/* edit spec object */
void init_edit_spec(struct edit_spec *es);
int check_edit_spec(struct edit_spec *es);

/* editor stuff */
void print_event_template(FILE *f, struct event *ev, const char *uid,
        time_t recurrence_id, icaltimezone *zone);
void print_todo_template(FILE *f, struct todo *td, icaltimezone *zone);
void print_new_event_template(FILE *f, icaltimezone *zone, int cal);
void print_new_todo_template(FILE *f, icaltimezone *zone, int cal);
int parse_edit_template(FILE *f, struct edit_spec *es, icaltimezone *zone);

int apply_edit_spec_to_calendar(struct edit_spec *es, struct calendar *cal);

#endif
