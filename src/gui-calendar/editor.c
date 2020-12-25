#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>

#include "calendar.h"
#include "editor.h"
#include "core.h"
#include "util.h"

const char * cal_status_str(enum prop_status v) {
	switch (v) {
	case PROP_STATUS_TENTATIVE: return "tentative";
	case PROP_STATUS_CONFIRMED: return "confirmed";
	case PROP_STATUS_CANCELLED: return "cancelled";
	case PROP_STATUS_COMPLETED: return "completed";
	case PROP_STATUS_NEEDSACTION: return "needsaction";
	case PROP_STATUS_INPROCESS: return "inprocess";
	default: return "";
	}
}
const char * cal_class_str(enum prop_class v) {
	switch (v) {
	case PROP_CLASS_PRIVATE: return "private";
	case PROP_CLASS_PUBLIC: return "public";
	default: return "";
	}
}
const char * cal_reltype_str(enum prop_reltype v) {
	switch (v) {
	case PROP_RELTYPE_PARENT: return "parent";
	case PROP_RELTYPE_CHILD: return "child";
	case PROP_RELTYPE_SIBLING: return "sibling";
	default: return "";
	}
}
static void print_literal(FILE *f, const char *key, const char *val) {
	if (val) {
		fprintf(f, "%s `%s`\n", key, val);
	} else {
		// fprintf(f, "#%s\n", key);
	}
}

static void print_vec_str(FILE *f, const struct vec *cats) {
	for (int i = 0; i < cats->len; ++i) {
		const struct str *s = vec_get((struct vec*)cats, i); // const cast
		fprintf(f, "%s", str_cstr(s));
		if (i < cats->len - 1) fprintf(f, ",");
	}
}

static const char *event_usage =
	"# USAGE:\n"
	"# status tentative/confirmed/cancelled\n"
	"# class public/private\n"
	"# summary/location/desc/color/cats `...`\n";
static const char *todo_usage =
	"# USAGE:\n"
	"# status completed/needsaction/inprocess\n"
	"# class public/private\n"
	"# summary/location/desc/color/cats `...`\n"
	"# est 1d2h3m4s\n"
	"# perc 39\n";


void print_template(FILE *f, struct comp_inst *ci,
		struct cal_timezone *zone, int cal) {
	bool is_event = ci->c->type == COMP_TYPE_EVENT;
	bool is_todo = ci->c->type == COMP_TYPE_TODO;
	struct props *p = &ci->c->p;

	ts start, end, due;
	bool has_start = props_get_start(p, &start);
	bool has_end = props_get_end(p, &end);
	bool has_due = props_get_due(p, &due);

	enum prop_class class;
	bool has_class = props_get_class(p, &class);

	enum prop_status status;
	bool has_status = props_get_status(p, &status);

	int est, perc;
	bool has_est = props_get_estimated_duration(p, &est);
	bool has_perc = props_get_percent_complete(p, &perc);

	const struct vec *cats = props_get_categories(p);
	const struct vec *rels = props_get_related_to(p);

	if (is_event) fprintf(f, "update event\n");
	if (is_todo) fprintf(f, "update todo\n");

	print_literal(f, "summary", props_get_summary(p));

	char buf[32];
	if (has_due) {
		struct simple_date sd = simple_date_from_ts(due, zone);
		format_simple_date(buf, 32, sd);
		fprintf(f, "due %s\n", buf);
	}
	if (has_start) {
		struct simple_date sd = simple_date_from_ts(start, zone);
		format_simple_date(buf, 32, sd);
		fprintf(f, "start %s\n", buf);
	}
	if (has_end) {
		struct simple_date sd = simple_date_from_ts(end, zone);
		format_simple_date(buf, 32, sd);
		fprintf(f, "end %s\n", buf);
	}

	if (has_est) {
		fprintf(f, "est ");
		print_simple_dur(f, simple_dur_from_int(est));
		fprintf(f, "\n");
	}
	if (has_perc) {
		fprintf(f, "perc %d\n", perc);
	}

	print_literal(f, "location", props_get_location(p));
	print_literal(f, "desc", props_get_desc(p));
	print_literal(f, "color", props_get_color(p));

	if (cats->len > 0) {
		fprintf(f, "cats `");
		print_vec_str(f, cats);
		fprintf(f, "`\n");
	}

	if (rels->len > 0) {
		fprintf(f, "rel `");
		for (int i = 0; i < rels->len; ++i) {
			const struct prop_related_to *rel = vec_get_c(rels, i);
			fprintf(f, "%s:%s",
				cal_reltype_str(rel->reltype),
				str_cstr(&rel->uid));
			if (i < rels->len - 1) fprintf(f, ",");
		}
		fprintf(f, "`\n");
	}

	if (has_class) fprintf(f, "class %s\n", cal_class_str(class));
	if (has_status) fprintf(f, "status %s\n", cal_status_str(status));

	// TODO: recurrence id
	// if (recurrence_id != -1) {
	//	 fprintf(f, "#instance: `%ld`\n", recurrence_id);
	// }
	if (str_any(&ci->c->uid)) {
		fprintf(f, "uid `%s`\n", str_cstr(&ci->c->uid));
	}
	fprintf(f, "calendar %d\n", cal);
	if (is_event) fprintf(f, "%s", event_usage);
	if (is_todo) fprintf(f, "%s", todo_usage);
}

void print_new_event_template(FILE *f, struct cal_timezone *zone, int cal) {
	// DEP: struct event
	char start[32];
	struct simple_date now = simple_date_now(zone);
	snprintf(start, 32, "%04d-%02d-%02d", now.year, now.month, now.day);

	fprintf(f,
		"create event\n"
		"summary\n"
		"start %s\n"
		"end \n"
		"#location\n"
		"#desc\n"
		"#cats\n"
		"#color\n"
		"#class\n"
		"#status\n"
		"calendar %d\n"
		"%s"
		"# calendar 1/2/...\n",
		start, cal,
		event_usage
	);
}
void print_new_todo_template(FILE *f, struct cal_timezone *zone, int cal) {
	// DEP: struct todo
	char start[32];
	struct simple_date now = simple_date_now(zone);
	snprintf(start, 32, "%04d-%02d-%02d", now.year, now.month, now.day);

	fprintf(f,
		"create todo\n"
		"summary\n"
		"#status\n"
		"#due %s\n"
		"#start %s\n"
		"#est\n"
		"#perc\n"
		"#desc\n"
		"#cats\n"
		"#class\n"
		"calendar %d\n"
		"%s"
		"# calendar 1/2/...\n",
		start, start, cal,
		todo_usage
	);
}

void edit_spec_init(struct edit_spec *es) {
	es->p = props_empty;
	es->rem = props_mask_empty;

	es->calendar_uid = str_empty;
	es->calendar_num = -1;
	es->uid = str_empty;
	es->recurrence_id = -1;
}
void edit_spec_finish(struct edit_spec *es) {
	props_finish(&es->p);
	str_free(&es->uid);
	str_free(&es->calendar_uid);
}

static void assign_props(struct props *p, const struct props *rhs,
		const struct props_mask *rem) {
	props_union(p, rhs);
	props_apply_mask(p, rem);
}
static bool can_apply_to_memory(const struct edit_spec *es,
		struct calendar *cal) {
	int idx;
	struct comp *c;
	struct props pu = props_empty;
	bool res = true;
	if (!str_any(&es->uid)) return false;
	switch (es->method) {
	case EDIT_METHOD_UPDATE:
		idx = calendar_find_comp(cal, str_cstr(&es->uid));
		if (idx == -1) return false;
		c = calendar_get_comp(cal, idx);
		asrt(c, "");
		if (es->type != c->type) return false;
		props_union(&pu, &c->p);
		assign_props(&pu, &es->p, &es->rem);
		if (!props_valid_for_type(&pu, es->type)) res = false;
		break;
	case EDIT_METHOD_CREATE:
		assign_props(&pu, &es->p, &es->rem);
		if (!props_valid_for_type(&pu, es->type)) res = false;
		break;
	case EDIT_METHOD_DELETE:
		/* meh... should we check if the comp exists in the calendar? */
		break;
	default:
		return false;
	}
	props_finish(&pu);
	return res;
}

static void apply_to_memory(struct edit_spec *es, struct calendar *cal) {
	int idx;
	struct comp *c;
	switch (es->method) {
	case EDIT_METHOD_UPDATE:
		idx = calendar_find_comp(cal, str_cstr(&es->uid));
		c = calendar_get_comp(cal, idx);
		asrt(c, "calendar_get_comp failed");
		assign_props(&c->p, &es->p, &es->rem);
		fprintf(stderr, "[editor memory] updated comp %s\n",
				str_cstr(&es->uid));
		break;
	case EDIT_METHOD_CREATE:
		idx = calendar_new_comp(cal, str_copy(&es->uid), es->type);
		c = calendar_get_comp(cal, idx);
		asrt(c, "calendar_new_comp failed");
		assign_props(&c->p, &es->p, &es->rem);
		fprintf(stderr, "[editor memory] created comp %s\n",
				str_cstr(&es->uid));
		break;
	case EDIT_METHOD_DELETE:
		idx = calendar_find_comp(cal, str_cstr(&es->uid));
		calendar_delete_comp(cal, idx);
		fprintf(stderr, "[editor memory] deleted comp %s\n",
				str_cstr(&es->uid));
		break;
	default:
		asrt(false, "");
		break;
	};
	cal->cis_dirty[es->type] = true;
}

int apply_edit_spec_to_calendar(struct edit_spec *es, struct calendar *cal) {
	if (!can_apply_to_memory(es, cal)) return -1;
	fprintf(stderr, "[editor] saving %s\n", str_cstr(&es->uid));
	if (edit_spec_apply_to_storage(es, cal) != 0) return -1;
	apply_to_memory(es, cal);
	return 0;
}
