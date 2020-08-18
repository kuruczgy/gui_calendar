#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libical/ical.h>

#include "core.h"
#include "util.h"
#include "calendar.h"
#include "editor.h"

static ts ts_from_icaltime(icaltimetype tt) {
	if (icaltime_is_null_time(tt)) return -1;
	time_t t = icaltime_as_timet_with_zone(tt, icaltime_get_timezone(tt));
	return (ts)t;
}

/* struct recurrence */
struct recurrence {
	icalcomponent *comp;
	struct vec rdate, exdate; /* vec<ts> */
	struct icalrecurrencetype rrule;
	icalrecur_iterator *ritr;
	struct icaltimetype dtstart;
	ts last;
};
static bool recurrence_exdate(struct recurrence *recur, ts t) {
	for (int i = 0; i < recur->exdate.len; ++i) {
		ts *ti = vec_get(&recur->exdate, i);
		if (*ti == t) return true;
	}
	return false;
}
static void ical_get_dtperiod_set(struct vec *res /* vec<ts> */,
		icalcomponent *ic, enum icalproperty_kind kind) {
	for (icalproperty *rdate = icalcomponent_get_first_property(ic, kind);
			rdate;
			rdate = icalcomponent_get_next_property(ic, kind)) {
		struct icaldatetimeperiodtype rdate_period =
			icalproperty_get_rdate(rdate);
		if (icaltime_is_null_time(rdate_period.time)) continue;
		ts t = ts_from_icaltime(rdate_period.time);
		vec_append(res, &t);
	}
}
static bool recurrence_init(struct recurrence *recur, icalcomponent *ic) {
	/* this function is partly based on libical's
	 * icalcomponent_foreach_recurrence */

	/* get recurrence related properties */
	icalproperty *rrule =
		icalcomponent_get_first_property(ic, ICAL_RRULE_PROPERTY);

	if (!rrule) return false;

	icalcomponent *root = icalcomponent_get_parent(ic);
	root = icalcomponent_new_clone(root);
	const char *ic_uid = icalcomponent_get_uid(ic);
	asrt(ic_uid, "ic_uid");
	icalcomponent *i =
		icalcomponent_get_first_component(root, ICAL_ANY_COMPONENT);
	while (i) {
		const char *i_uid = icalcomponent_get_uid(i);
		if (i_uid && strcmp(i_uid, ic_uid) == 0) {
			ic = i;
			goto cloned;
		}
		i = icalcomponent_get_next_component(root, ICAL_ANY_COMPONENT);
	}
	asrt(false, "clone failed");
cloned:
	recur->comp = root;
	recur->rdate = vec_new_empty(sizeof(ts));
	recur->exdate = vec_new_empty(sizeof(ts));
	recur->rrule = icalproperty_get_rrule(rrule);
	recur->ritr = NULL;
	recur->dtstart = icalcomponent_get_dtstart(ic); /* this is why we clone */
	recur->last = -1;

	ical_get_dtperiod_set(&recur->rdate, ic, ICAL_RDATE_PROPERTY);
	ical_get_dtperiod_set(&recur->exdate, ic, ICAL_EXDATE_PROPERTY);
	return true;
}
void recurrence_destroy(struct recurrence *recur) {
	if (recur) {
		vec_free(&recur->rdate);
		vec_free(&recur->exdate);
		if (recur->ritr) icalrecur_iterator_free(recur->ritr);
		icalcomponent_free(recur->comp);

		free(recur);
	}
}
void recurrence_reset(struct recurrence *recur) {
	if (recur->ritr) {
		icalrecur_iterator_free(recur->ritr);
		recur->ritr = NULL;
	}
	recur->last = -1;
}
void recurrence_expand(struct recurrence *recur, ts to, recur_cb cb, void *cl) {
	if (!recur->ritr) {
		for (int i = 0; i < recur->rdate.len; ++i) {
			ts *ti = vec_get(&recur->rdate, i);
			if (recurrence_exdate(recur, *ti)) continue;
			cb(cl, *ti);
		}
		recur->ritr = icalrecur_iterator_new(recur->rrule, recur->dtstart);
	}

	if (recur->last != -1) {
		if (recur->last > to) return;
		cb(cl, recur->last);
		recur->last = -1;
	}

	for (struct icaltimetype tt = icalrecur_iterator_next(recur->ritr);
			!icaltime_is_null_time(tt);
			tt = icalrecur_iterator_next(recur->ritr)) {
		ts t = ts_from_icaltime(tt);
		if (t > to) {
			recur->last = t;
			break;
		}
		cb(cl, t);
	}
}

static enum icalproperty_class icalcomponent_get_class(icalcomponent *c) {
	icalproperty *p =
		icalcomponent_get_first_property(c, ICAL_CLASS_PROPERTY);
	return p ? icalproperty_get_class(p) : ICAL_CLASS_NONE;
}

static void icalcomponent_remove_properties(icalcomponent *c,
		icalproperty_kind kind) {
	icalproperty *p;
	while (p = icalcomponent_get_first_property(c, kind)) {
		icalcomponent_remove_property(c, p);
	}
}

static bool ical_status_to_prop_status(icalproperty_status ical_status,
		enum prop_status *s) {
	switch (ical_status) {
	case ICAL_STATUS_TENTATIVE: *s = PROP_STATUS_TENTATIVE; break;
	case ICAL_STATUS_CONFIRMED: *s = PROP_STATUS_CONFIRMED; break;
	case ICAL_STATUS_CANCELLED: *s = PROP_STATUS_CANCELLED; break;
	case ICAL_STATUS_COMPLETED: *s = PROP_STATUS_COMPLETED; break;
	case ICAL_STATUS_NEEDSACTION: *s = PROP_STATUS_NEEDSACTION; break;
	case ICAL_STATUS_INPROCESS: *s = PROP_STATUS_INPROCESS; break;
	default: return false;
	}
	return true;
}
static bool ical_class_to_prop_class(icalproperty_class ical_class,
		enum prop_class *s) {
	switch (ical_class) {
	case ICAL_CLASS_PRIVATE: *s = PROP_CLASS_PRIVATE; break;
	case ICAL_CLASS_PUBLIC: *s = PROP_CLASS_PUBLIC; break;
	default: return false;
	}
	return true;
}

char* read_stream(char *s, size_t size, void *d)
{
	return fgets(s, size, (FILE*)d);
}

static enum icalproperty_status prop_status_to_ical_status(
		enum prop_status status) {
	switch (status) {
	case PROP_STATUS_TENTATIVE: return ICAL_STATUS_TENTATIVE;
	case PROP_STATUS_CONFIRMED: return ICAL_STATUS_CONFIRMED;
	case PROP_STATUS_CANCELLED: return ICAL_STATUS_CANCELLED;
	case PROP_STATUS_COMPLETED: return ICAL_STATUS_COMPLETED;
	case PROP_STATUS_NEEDSACTION: return ICAL_STATUS_NEEDSACTION;
	case PROP_STATUS_INPROCESS: return ICAL_STATUS_INPROCESS;
	}
	return ICAL_STATUS_NONE;
}
static enum icalproperty_class prop_class_to_ical_class(enum prop_class class) {
	switch (class) {
	case PROP_CLASS_PRIVATE: return ICAL_CLASS_PRIVATE;
	case PROP_CLASS_PUBLIC: return ICAL_CLASS_PUBLIC;
	}
	return ICAL_CLASS_NONE;
}

static void assign_val(icalcomponent *c, enum icalproperty_kind kind,
		icalvalue *val) {
	icalproperty *p = icalcomponent_get_first_property(c, kind);
	if (!p) {
		p = icalproperty_new(kind);
		icalproperty_set_value(p, val);
		icalcomponent_add_property(c, p);
	} else {
		icalproperty_set_value(p, val);
	}
}
static void comp_assign_text(icalcomponent *c, enum icalproperty_kind kind,
		const char *src, bool rem) {
	if (rem) {
		icalcomponent_remove_properties(c, kind);
	} else if (src) {
		icalvalue *val = icalvalue_new_text(src);
		assign_val(c, kind, val);
	}
}
static void comp_assign_status(icalcomponent *c,
		bool has, enum prop_status src, bool rem) {
	enum icalproperty_kind kind = ICAL_STATUS_PROPERTY;
	if (rem) {
		icalcomponent_remove_properties(c, kind);
	} else if (has) {
		enum icalproperty_status status = prop_status_to_ical_status(src);
		icalvalue *val = icalvalue_new_status(status);
		assign_val(c, kind, val);
	}
}
static void comp_assign_class(icalcomponent *c,
		bool has, int src, bool rem) {
	enum icalproperty_kind kind = ICAL_CLASS_PROPERTY;
	if (rem) {
		icalcomponent_remove_properties(c, kind);
	} else if (has) {
		enum icalproperty_class class = prop_class_to_ical_class(src);
		icalvalue *val = icalvalue_new_class(class);
		assign_val(c, kind, val);
	}
}
static void comp_assign_estimated_duration(icalcomponent *c,
		bool has, int src, bool rem) {
	enum icalproperty_kind kind = ICAL_ESTIMATEDDURATION_PROPERTY;
	if (rem) {
		icalcomponent_remove_properties(c, kind);
	} else if (has) {
		struct icaldurationtype val = icaldurationtype_from_int(src);
		icalproperty *p = icalcomponent_get_first_property(c, kind);
		if (!p) {
			p = icalproperty_new_estimatedduration(val);
			icalcomponent_add_property(c, p);
		} else {
			icalproperty_set_estimatedduration(p, val);
		}
	}
}
static void comp_assign_percent_complete(icalcomponent *c,
		bool has, int src, bool rem) {
	enum icalproperty_kind kind = ICAL_PERCENTCOMPLETE_PROPERTY;
	if (rem) {
		icalcomponent_remove_properties(c, kind);
	} else if (has) {
		icalproperty *p = icalcomponent_get_first_property(c, kind);
		if (!p) {
			p = icalproperty_new_percentcomplete(src);
			icalcomponent_add_property(c, p);
		} else {
			icalproperty_set_percentcomplete(p, src);
		}
	}
}
static void comp_assign_ts(icalcomponent *c, enum icalproperty_kind kind,
		bool has, ts src, bool rem) {
	if (rem) {
		icalcomponent_remove_properties(c, kind);
	} else if (has) {
		icalvalue *val =
			icalvalue_new_datetime(icaltime_from_timet_with_zone(src, 0,
						icaltimezone_get_utc_timezone()));
		assign_val(c, kind, val);
	}
}
static void comp_assign_categories(icalcomponent *ic,
		const struct vec *src, bool rem) {
	if (rem || src->len > 0) {
		icalcomponent_remove_properties(ic, ICAL_CATEGORIES_PROPERTY);
	}
	if (!rem) {
		if (src->len > 0) {
			struct str cats_str = str_empty;
			for (int i = 0; i < src->len; ++i) {
				/* const cast */
				const struct str *s = vec_get((struct vec*)src, i);
				str_append(&cats_str, str_cstr(s), s->v.len);
				if (i < src->len - 1) str_append_char(&cats_str, ',');
			}
			icalproperty *p = icalproperty_new_categories(str_cstr(&cats_str));
			icalcomponent_add_property(ic, p);
			str_free(&cats_str);
		}
	}
}
static void apply_edit_spec_to_icalcomponent(struct edit_spec *es,
		icalcomponent *ic) {
	comp_assign_text(ic, ICAL_COLOR_PROPERTY,
		props_get_color(&es->p), props_mask_get(&es->rem, PROP_COLOR));
	comp_assign_text(ic, ICAL_SUMMARY_PROPERTY,
		props_get_summary(&es->p), props_mask_get(&es->rem, PROP_SUMMARY));
	comp_assign_text(ic, ICAL_LOCATION_PROPERTY,
		props_get_location(&es->p), props_mask_get(&es->rem, PROP_LOCATION));
	comp_assign_text(ic, ICAL_DESCRIPTION_PROPERTY,
		props_get_desc(&es->p), props_mask_get(&es->rem, PROP_DESC));
	comp_assign_categories(ic, props_get_categories(&es->p),
		props_mask_get(&es->rem, PROP_CATEGORIES));

	bool has;

	ts ts_src;
	has = props_get_start(&es->p, &ts_src);
	comp_assign_ts(ic, ICAL_DTSTART_PROPERTY, has, ts_src,
		props_mask_get(&es->rem, PROP_START));
	has = props_get_end(&es->p, &ts_src);
	comp_assign_ts(ic, ICAL_DTEND_PROPERTY, has, ts_src,
		props_mask_get(&es->rem, PROP_END));
	has = props_get_due(&es->p, &ts_src);
	comp_assign_ts(ic, ICAL_DUE_PROPERTY, has, ts_src,
		props_mask_get(&es->rem, PROP_DUE));

	enum prop_status status;
	has = props_get_status(&es->p, &status);
	comp_assign_status(ic, has, status, props_mask_get(&es->rem, PROP_STATUS));

	enum prop_class class;
	has = props_get_class(&es->p, &class);
	comp_assign_class(ic, has, class, props_mask_get(&es->rem, PROP_CLASS));

	int int_src;
	has = props_get_estimated_duration(&es->p, &int_src);
	comp_assign_estimated_duration(ic, has, int_src,
		props_mask_get(&es->rem, PROP_ESTIMATED_DURATION));
	has = props_get_percent_complete(&es->p, &int_src);
	comp_assign_percent_complete(ic, has, int_src,
		props_mask_get(&es->rem, PROP_PERCENT_COMPLETE));
}

static icalcomponent* libical_component_from_file(FILE *f) {
	icalparser *parser = icalparser_new();
	icalparser_set_gen_data(parser, f);
	icalcomponent *root = icalparser_parse(parser, read_stream);
	icalparser_free(parser);
	return root;
}

/* struct comp */
static void props_init_from_ical(struct props *p, icalcomponent *ic) {
	// DEP: struct props
	struct icaltimetype
		dtstart = icalcomponent_get_dtstart(ic),
		dtend = icalcomponent_get_dtend(ic),
		due = icalcomponent_get_due(ic);
	if (!icaltime_is_null_time(dtstart))
		props_set_start(p, ts_from_icaltime(dtstart));
	if (!icaltime_is_null_time(dtend))
		props_set_end(p, ts_from_icaltime(dtend));
	if (!icaltime_is_null_time(due))
		props_set_due(p, ts_from_icaltime(due));

	icalproperty_status ical_status = icalcomponent_get_status(ic);
	enum prop_status status;
	if (ical_status_to_prop_status(ical_status, &status)) {
		props_set_status(p, status);
	}

	icalproperty_class ical_class = icalcomponent_get_class(ic);
	enum prop_class class;
	if (ical_class_to_prop_class(ical_class, &class)) {
		props_set_class(p, class);
	}

	icalproperty *ip = icalcomponent_get_first_property(ic,
			ICAL_ESTIMATEDDURATION_PROPERTY);
	if (ip) {
		struct icaldurationtype v = icalproperty_get_estimatedduration(ip);
		int est = icaldurationtype_as_int(v);
		props_set_estimated_duration(p, est);
	}

	ip = icalcomponent_get_first_property(ic, ICAL_PERCENTCOMPLETE_PROPERTY);
	if (ip) {
		int perc = icalproperty_get_percentcomplete(ip);
		props_set_percent_complete(p, perc);
	}

	ip = icalcomponent_get_first_property(ic, ICAL_COLOR_PROPERTY);
	if (ip) {
		icalvalue *v = icalproperty_get_value(ip);
		const char *text = icalvalue_get_text(v);
		props_set_color(p, text);
	}

	const char *summary = icalcomponent_get_summary(ic);
	if (summary) props_set_summary(p, summary);

	const char *location = icalcomponent_get_location(ic);
	if (location) props_set_location(p, location);

	const char *desc = icalcomponent_get_description(ic);
	if (desc) props_set_desc(p, desc);

	struct vec categories = vec_new_empty(sizeof(struct str));
	ip = icalcomponent_get_first_property(ic, ICAL_CATEGORIES_PROPERTY);
	while (ip) {
		const char *text = icalproperty_get_categories(ip);
		struct str s = str_new_from_cstr(text);
		vec_append(&categories, &s);
		ip = icalcomponent_get_next_property(ic, ICAL_CATEGORIES_PROPERTY);
	}
	props_set_categories(p, categories);
}
static bool comp_init_from_ical(struct comp *c, icalcomponent *ic) {
	if (icalcomponent_isa(ic) == ICAL_VEVENT_COMPONENT) {
		c->type = COMP_TYPE_EVENT;
	} else if (icalcomponent_isa(ic) == ICAL_VTODO_COMPONENT) {
		c->type = COMP_TYPE_TODO;
	} else {
		return false;
	}

	const char *uid = icalcomponent_get_uid(ic);
	if (!uid) return false;
	c->uid = str_empty;
	str_append(&c->uid, uid, strlen(uid));

	c->p = props_empty;
	props_init_from_ical(&c->p, ic);
	if (!props_valid_for_type(&c->p, c->type)) {
		fprintf(stderr, "WARNING: component `%s` is invalid. skipping\n",
			str_cstr(&c->uid));
		str_free(&c->uid);
		props_finish(&c->p);
		return false;
	}

	c->recur_insts = vec_new_empty(sizeof(struct comp_recur_inst));

	struct recurrence recur;
	if (recurrence_init(&recur, ic)) {
		c->recur = malloc_check(sizeof(struct recurrence));
		memcpy(c->recur, &recur, sizeof(struct recurrence));
	} else {
		c->recur = NULL;
	}

	c->all_expanded = false;

	return true;
}

int libical_parse_ics(FILE *f, struct calendar *cal) {
	icalcomponent *root = libical_component_from_file(f);
	if (!root) return -1;
	icalcomponent *ic = icalcomponent_get_first_component(
		root, ICAL_ANY_COMPONENT);
	while(ic) {
		enum icalcomponent_kind kind = icalcomponent_isa(ic);
		const char *uid = icalcomponent_get_uid(ic);
		icalproperty *recurrenceid =
			icalcomponent_get_first_property(ic, ICAL_RECURRENCEID_PROPERTY);
		if ((kind == ICAL_VEVENT_COMPONENT || kind == ICAL_VTODO_COMPONENT)
				&& uid && recurrenceid) {
			/* we got a recurrence instance here */
			struct comp_recur_inst cri;
			cri.recurrence_id =
				ts_from_icaltime(icalproperty_get_recurrenceid(recurrenceid));
			cri.p = props_empty;
			props_init_from_ical(&cri.p, ic);

			int idx = calendar_find_comp(cal, uid);
			struct comp *c = idx == -1 ? NULL : calendar_get_comp(cal, idx);
			if (c && props_valid_for_type(&cri.p, c->type)) {
				vec_append(&c->recur_insts, &cri);
			} else {
				fprintf(stderr,
				"WARNING: component instance for `%s` is invalid. skipping\n",
				uid);
				props_finish(&cri.p);
			}
		} else {
			struct comp c;
			if (comp_init_from_ical(&c, ic)) calendar_add_comp(cal, c);
		}
		ic = icalcomponent_get_next_component(root, ICAL_ANY_COMPONENT);
	}
	icalcomponent_free(root);
	return 0;
}

int comp_init_from_ics(struct comp *c, FILE *f) {
	icalcomponent *root = libical_component_from_file(f);
	if (!root) return -1;

	icalcomponent *ic = icalcomponent_get_first_component(
		root, ICAL_ANY_COMPONENT);
	while (ic) {
		if (comp_init_from_ical(c, ic)) break;
		ic = icalcomponent_get_next_component(root, ICAL_ANY_COMPONENT);
	}
	icalcomponent_free(root);
	return 0;
}

static bool timespec_leq(struct timespec a, struct timespec b) {
	if (a.tv_sec == b.tv_sec) return a.tv_nsec <= b.tv_nsec;
	return a.tv_sec <= b.tv_sec;
}

void update_calendar_from_storage(struct calendar *cal,
		struct cal_timezone *local_zone) {
	const char *path = str_cstr(&cal->storage);
	struct stat sb;
	asrt(stat(path, &sb) == 0, "stat");

	struct timespec loaded = cal->loaded;
	clock_gettime(CLOCK_REALTIME, &cal->loaded);
	if (S_ISREG(sb.st_mode)) { // file
		FILE *f = fopen(path, "rb");
		if (libical_parse_ics(f, cal) < 0) {
			fprintf(stderr, "warning: could not parse %s\n", path);
		}
		fclose(f);
	} else {
		asrt(S_ISDIR(sb.st_mode), "not dir");
		DIR *d;
		struct dirent *dir;
		int dir_fd;
		char buf[1024];
		asrt(d = opendir(path), "opendir");
		dir_fd = dirfd(d);
		while(dir = readdir(d)) {
			asrt(fstatat(dir_fd, dir->d_name, &sb, 0) == 0, "stat");
			if (!S_ISREG(sb.st_mode)) continue;
			if (!timespec_leq(loaded, sb.st_mtim)) continue;
			int l = strlen(dir->d_name);
			bool displayname = false;
			if (!( l >= 4 && strcmp(dir->d_name + l - 4, ".ics") == 0 )) {
				if (strcmp(dir->d_name, "displayname") == 0) {
					displayname = true;
				} else if (strcmp(dir->d_name, "my_private") == 0) {
					cal->priv = true;
					continue;
				} else {
					continue;
				}
			}
			if (displayname && str_any(&cal->name)) continue;
			snprintf(buf, 1024, "%s/%s", path, dir->d_name);
			FILE *f = fopen(buf, "rb");
			asrt(f, "could not open");
			if (displayname) {
				asrt(!str_any(&cal->name), "calendar already has name");
				int cnt = fread(buf, 1, 1024, f);
				asrt(cnt > 0, "meta");
				cal->name = str_empty;
				str_append(&cal->name, buf, cnt);
			} else {
				if (libical_parse_ics(f, cal) < 0) {
					fprintf(stderr, "warning: could not parse %s\n", buf);
				}
			}
			fclose(f);
		}
		asrt(closedir(d) == 0, "closedir");
	}
}

int edit_spec_apply_to_storage(struct edit_spec *es,
		struct calendar *cal) {
	/* check if storage is directory */
	struct stat sb;
	FILE *f;
	char *result;
	const char *path_base = str_cstr(&cal->storage);
	asrt(stat(path_base, &sb) == 0, "stat");
	asrt(S_ISDIR(sb.st_mode), "saving to non-dir calendar not supported");

	/* construct the path to the specific .ics file */
	char path[1024];
	asrt(str_any(&es->uid), "uid sanity check");
	snprintf(path, 1024, "%s/%s.ics", path_base, str_cstr(&es->uid));

	enum icalcomponent_kind type;
	if (es->type == COMP_TYPE_EVENT) type = ICAL_VEVENT_COMPONENT;
	else if (es->type == COMP_TYPE_TODO) type = ICAL_VTODO_COMPONENT;
	else asrt(false, "");

	switch(es->method) {
	case EDIT_METHOD_DELETE:
		if (unlink(path) < 0) {
			fprintf(stderr, "[editor storage] deletion failed\n");
			return -1;
		}
		fprintf(stderr, "[editor storage] deleted %s\n", path);
		return 0;
		break;
	case EDIT_METHOD_UPDATE:
		if (access(path, F_OK) != 0) {
			fprintf(stderr, "[editor storage] can't access existing file\n");
			return -1;
		}

		/* load and parse the file */
		f = fopen(path, "r");
		icalcomponent *root = libical_component_from_file(f);
		fclose(f);

		/* find the specific component we are interested in, using the uid */
		icalcomponent *c = icalcomponent_get_first_component(root, type);
		while (c) {
			const char *c_uid = icalcomponent_get_uid(c);
			if (strcmp(c_uid, str_cstr(&es->uid)) == 0) { // found it
				/* populate component with new values */
				apply_edit_spec_to_icalcomponent(es, c);
				break;
			}
			c = icalcomponent_get_next_component(root, type);
		}

		/* serialize and write back the component */
		result = icalcomponent_as_ical_string(root);
		fprintf(stderr, "[editor storage] writing existing %s\n", path);
		f = fopen(path, "w");
		fputs(result, f);
		fclose(f);
		icalcomponent_free(root);

		return 0;
		break;
	case EDIT_METHOD_CREATE:
		;
		/* create the component */
		icalcomponent *comp = icalcomponent_new(type);
		icalcomponent_set_uid(comp, str_cstr(&es->uid));
		apply_edit_spec_to_icalcomponent(es, comp);
		/* create a frame, serialize, and save to file */
		icalcomponent *calendar = icalcomponent_vanew(
			ICAL_VCALENDAR_COMPONENT,
			icalproperty_new_version("2.0"),
			icalproperty_new_prodid("-//ABC Corporation//gui_calendar//EN"),
			comp,
			NULL
		);
		result = icalcomponent_as_ical_string(calendar);
		fprintf(stderr, "[editor storage] writing new %s\n", path);
		f = fopen(path, "w");
		fputs(result, f);
		fclose(f);
		icalcomponent_free(calendar);

		return 0;
	default:
		asrt(false, "");
		break;
	}
	asrt(false, "");
	return 0;
}

