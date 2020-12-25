
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "editor.h"
#include "core.h"
#include <ds/vec.h>
#include <ds/iter.h>

/*

(* basic definitions *)
digit = ? digits ? ;
char = ? any character ? ;
ws = " ", { " " } ;
newline = "\n", { "\n" } ;

(* function `header` *)
header = ( "create" | "update" | "delete" ), ws, ( "event" | "todo" ) ;

(* function `dt` *)
year = digit, digit, digit, digit ;
twodigit = digit | digit, digit ;
date = [ [ year, "-" ], twodigit, "-" ], twodigit ;
time = twodigit, ":", twodigit ;
dt = [ date, " " ], time ;

(* function `literal` *)
literal = "`", { char - "`" }, "`" ;
(* function `integer` *)
integer = digit, { digit } ;

(* function `dur` *)
dur-comp = integer, ( "d" | "h" | "m" | "s" ) ;
dur = dur-comp, { dur-comp } ;

(* function `prop` *)
status-val = "tentative" | "confirmed" |
	"cancelled" | "needsaction" | "completed" | "inprocess" ;
uprop =
	( ( "start" | "end" | "due" ), ws, dt ) |
	( ( "summary" | "location" | "desc" | "color" | "cats" ), ws, literal ) |
	( ( "uid" | "instance" ), ws, literal ) |
	( "rel", ws, literal ) |
	( "est", ws, dur ) |
	( "perc", ws, integer ) |
	( "class", ws, ( "private" | "public" ) ) |
	( "status", ws, status-val ) |
	( "calendar", ws, ( integer | literal ) ) ;
remprop = "-",
	( "start", "due", "location", "desc", "color", "class", "status", "est" ),
	[ ws, { char - "\n" } ] ;
prop = uprop | remprop

(* function `grammar` *)
comment = "#", { char - "\n" } ;
grammar = header, { newline, ( prop | comment ) } ;

*/

struct parser_state {
	FILE *f;
	struct simple_date start, end, due;
};

typedef struct parser_state *st;
typedef enum {
	OK, ERROR
} res;

static int peek(st s) {
	int c = getc(s->f);
	ungetc(c, s->f);
	return c;
}

static int get(st s) {
	return getc(s->f);
}

static res eat(st s, char c) {
	int a = get(s);
	return a == c ? OK : ERROR;
}

static int get_digit(st s) {
	int c = peek(s);
	if (isdigit(c)) {
		get(s);
		return c - '0';
	}
	return -1;
}

static void get_digits(st s, int *num, int *len) {
	int d;
	*num = 0;
	*len = 0;

	while (1) {
		if ((d = get_digit(s)) < 0) break;
		*num = 10 * (*num) + d;
		*len += 1;
		if (*len > 4) return;
	}
}

static res dt(st s, struct simple_date *sd) {
	int num, len, c;
	int *t = sd->t;
	t[0] = t[1] = t[2] = t[3] = t[4] = t[5] = -1;

	get_digits(s, &num, &len);
	if (len == 1 || len == 2) {
		c = peek(s);
		if (c == '-') { /* month */
			asrt(eat(s, '-') == OK, "");
			t[1] = num;
			get_digits(s, &num, &len);
			if (len != 1 && len != 2) return ERROR;
			t[2] = num;
			goto time;
		} else if (c == ':') { /* time */
			asrt(eat(s, ':') == OK, "");
			t[3] = num;
			get_digits(s, &num, &len);
			if (len != 1 && len != 2) return ERROR;
			t[4] = num;
			goto done;
		} else if (c == ' ' || c == EOF) { /* day */
			t[2] = num;
			goto time;
		} else {
			return ERROR;
		}
	} else if (len == 4) { /* year */
		t[0] = num;
		if (fscanf(s->f, "-%2d-%2d", &t[1], &t[2]) != 2) return ERROR;
		if (t[1] < 0 || t[2] < 0) return ERROR;
		goto time;
	} else {
		return ERROR;
	}

	asrt(false, "bad control flow");
time:
	c = peek(s);
	if (c != ' ') return OK;
	asrt(eat(s, ' ') == OK, "");
	get_digits(s, &num, &len);
	if (len != 1 && len != 2) return ERROR;
	t[3] = num;
	if (eat(s, ':') != OK) return ERROR;
	get_digits(s, &num, &len);
	if (len != 1 && len != 2) return ERROR;
	t[4] = num;
	goto done;

done:
	return OK;
}

static res header(st s, enum edit_method *method, enum comp_type *type) {
	char buf1[16], buf2[16];
	if (fscanf(s->f, "%15s %15s", buf1, buf2) != 2) return ERROR;

	if (strcmp("create", buf1) == 0) {
		*method = EDIT_METHOD_CREATE;
	} else if (strcmp("update", buf1) == 0) {
		*method = EDIT_METHOD_UPDATE;
	} else if (strcmp("delete", buf1) == 0) {
		*method = EDIT_METHOD_DELETE;
	} else {
		return ERROR;
	}

	if (strcmp("event", buf2) == 0) {
		*type = COMP_TYPE_EVENT;
	} else if (strcmp("todo", buf2) == 0) {
		*type = COMP_TYPE_TODO;
	} else {
		return ERROR;
	}

	return OK;
}

static res literal(st s, struct str *out) {
	static char buf[16384];
	if (fscanf(s->f, "`%16383[^`]`", buf) != 1) return ERROR;
	int len = strlen(buf);
	str_clear(out);
	str_append(out, buf, len);
	return OK;
}

static res integer(st s, int *out) {
	if (fscanf(s->f, "%d", out) != 1) return ERROR;
	return OK;
}

static res dur(st s, int *out) {
	struct simple_dur sdu = { 0, 0, 0, 0 };
	int o;
	char c;
	bool first = true;
	do {
		if (integer(s, &o) != OK) goto out;
		c = get(s);
		switch (c) {
		case 'd': sdu.d = o; break;
		case 'h': sdu.h = o; break;
		case 'm': sdu.m = o; break;
		case 's': sdu.s = o; break;
		default: return ERROR;
		}
		first = false;
	} while (1);
out:
	*out = simple_dur_to_int(sdu);
	return first ? ERROR : OK;
}

static res parse_class(st s, enum prop_class *clas) {
	char key[16];
	fscanf(s->f, "%15s", key);
	if (strcmp(key, "private") == 0) {
		*clas = PROP_CLASS_PRIVATE;
	} else if (strcmp(key, "public") == 0) {
		*clas = PROP_CLASS_PUBLIC;
	} else {
		return ERROR;
	}
	return OK;
}

bool cal_parse_status(const char *key, enum prop_status *status) {
	if (strcmp(key, "tentative") == 0) {
		*status = PROP_STATUS_TENTATIVE;
	} else if (strcmp(key, "confirmed") == 0) {
		*status = PROP_STATUS_CONFIRMED;
	} else if (strcmp(key, "cancelled") == 0) {
		*status = PROP_STATUS_CANCELLED;
	} else if (strcmp(key, "needsaction") == 0) {
		*status = PROP_STATUS_NEEDSACTION;
	} else if (strcmp(key, "completed") == 0) {
		*status = PROP_STATUS_COMPLETED;
	} else if (strcmp(key, "inprocess") == 0) {
		*status = PROP_STATUS_INPROCESS;
	} else {
		return false;
	}
	return true;
}
static res parse_status(st s, enum prop_status *status) {
	char key[16];
	fscanf(s->f, "%15s", key);
	if (!cal_parse_status(key, status)) return ERROR;
	return OK;
}

static bool cal_parse_reltype(struct str_slice s, enum prop_reltype *reltype) {
	if (strncmp(s.d, "parent", s.len) == 0) {
		*reltype = PROP_RELTYPE_PARENT;
	} else if (strncmp(s.d, "child", s.len) == 0) {
		*reltype = PROP_RELTYPE_CHILD;
	} else if (strncmp(s.d, "sibling", s.len) == 0) {
		*reltype = PROP_RELTYPE_SIBLING;
	} else {
		return false;
	}
	return true;
}

static res prop(st s, struct edit_spec *es) {
	struct simple_date sd;
	struct str str = str_new_empty();
	char *key;
	char buf[16];
	int d;
	bool rem = false;
	fscanf(s->f, "%15s ", buf);
	if (buf[0] == '-') rem = true, key = buf + 1;
	else key = buf;
	if (strcmp(key, "start") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_START), OK;
		if (dt(s, &sd) != OK) return ERROR;
		s->start = sd;
	} else if (strcmp(key, "end") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_END), OK;
		if (dt(s, &sd) != OK) return ERROR;
		s->end = sd;
	} else if (strcmp(key, "due") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_DUE), OK;
		if (dt(s, &sd) != OK) return ERROR;
		s->due = sd;
	} else if (strcmp(key, "summary") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_SUMMARY), OK;
		if (literal(s, &str) != OK) return ERROR;
		props_set_summary(&es->p, str_cstr(&str));
	} else if (strcmp(key, "location") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_LOCATION), OK;
		if (literal(s, &str) != OK) return ERROR;
		props_set_location(&es->p, str_cstr(&str));
	} else if (strcmp(key, "desc") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_DESC), OK;
		if (literal(s, &str) != OK) return ERROR;
		props_set_desc(&es->p, str_cstr(&str));
	} else if (strcmp(key, "color") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_COLOR), OK;
		if (literal(s, &str) != OK) return ERROR;
		props_set_color(&es->p, str_cstr(&str));
	} else if (strcmp(key, "cats") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_CATEGORIES), OK;
		if (literal(s, &str) != OK) return ERROR;

		struct vec cats = vec_new_empty(sizeof(struct str));
		const char *i = str_cstr(&str);
		while (true) {
			struct str cat = str_empty;
			const char *next = strchr(i, ',');
			if (!next) {
				str_append(&cat, i, strlen(i));
				vec_append(&cats, &cat);
				break;
			}
			str_append(&cat, i, next - i);
			vec_append(&cats, &cat);
			i = next + 1;
		}
		props_set_categories(&es->p, cats);
	} else if (strcmp(key, "uid") == 0) {
		if (rem) return ERROR;
		if (literal(s, &str) != OK) return ERROR;
		es->uid = str_empty;
		str_append(&es->uid, str_cstr(&str), str.v.len);
	} else if (strcmp(key, "instance") == 0) {
		if (rem) return ERROR;
		if (literal(s, &str) != OK) return ERROR;
		es->recurrence_id = atol(str_cstr(&str));
	} else if (strcmp(key, "rel") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_RELATED_TO), OK;
		if (literal(s, &str) != OK) return ERROR;

		struct vec rels = vec_new_empty(sizeof(struct prop_related_to));
		struct str_slice s_comma;
		struct str_gen g_comma = str_gen_split(str_as_slice(&str), ',');
		while (str_gen_next(&g_comma, &s_comma)) {
			struct str_slice s_colon;
			struct str_gen g_colon = str_gen_split(s_comma, ':');
			struct prop_related_to rel;
			for (int i = 0; i < 2; ++i) {
				if (!str_gen_next(&g_colon, &s_colon)) return ERROR;
				if (i == 0) {
					if (!cal_parse_reltype(s_colon, &rel.reltype)) return ERROR;
				} else if (i == 1) {
					rel.uid = str_new_from_slice(s_colon);
				}
			}
			vec_append(&rels, &rel);
		}

		props_set_related_to(&es->p, rels);
	} else if (strcmp(key, "est") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_ESTIMATED_DURATION), OK;
		if (dur(s, &d) != OK) return ERROR;
		props_set_estimated_duration(&es->p, d);
	} else if (strcmp(key, "perc") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_PERCENT_COMPLETE), OK;
		if (integer(s, &d) != OK) return ERROR;
		props_set_percent_complete(&es->p, d);
	} else if (strcmp(key, "class") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_CLASS), OK;
		enum prop_class class;
		if (parse_class(s, &class) != OK) return ERROR;
		props_set_class(&es->p, class);
	} else if (strcmp(key, "status") == 0) {
		if (rem) return props_mask_add(&es->rem, PROP_STATUS), OK;
		enum prop_status status;
		if (parse_status(s, &status) != OK) return ERROR;
		props_set_status(&es->p, status);
	} else if (strcmp(key, "calendar") == 0) {
		if (rem) return ERROR;
		if (peek(s) != '`') {
			if (integer(s, &es->calendar_num) != OK) return ERROR;
		} else {
			if (literal(s, &str) != OK) return ERROR;
			es->calendar_uid = str_empty;
			str_append(&es->calendar_uid, str_cstr(&str), str.v.len);
		}
	} else {
		return ERROR;
	}

	str_free(&str);
	return OK;
}

static res grammar(st s, struct edit_spec *es) {
	int c;
	if (header(s, &es->method, &es->type) != OK) return ERROR;
	while (1) {
		while ((c = peek(s)) == '\n') asrt(get(s) == '\n', "");
		if (c == EOF) break;
		if (c == '#') {
			while ((c = peek(s)) != '\n') get(s);
		} else {
			if (prop(s, es) != OK) return ERROR;
		}
	}
	return OK;
}

int parse_edit_template(FILE *f, struct edit_spec *es,
		struct cal_timezone *zone) {
	struct parser_state s = { f };
	s.start = s.end = s.due = make_simple_date(-1, -1, -1, -1, -1, -1);
	edit_spec_init(es);
	if (grammar(&s, es) != OK) return -1;

	if (s.start.second == -1) s.start.second = 0;
	if (s.end.second == -1) s.end.second = 0;
	if (s.due.second == -1) s.due.second = 0;

	if (es->type == COMP_TYPE_EVENT) {
		if (s.start.year == -1 ||
			s.start.month == -1 ||
			s.start.day == -1) return -1;
		if (s.end.year == -1) s.end.year = s.start.year;
		if (s.end.month == -1) s.end.month = s.start.month;
		if (s.end.day == -1) s.end.day = s.start.day;

		ts start = simple_date_to_ts(s.start, zone);
		ts end = simple_date_to_ts(s.end, zone);
		if (start != -1) props_set_start(&es->p, start);
		if (end != -1) props_set_end(&es->p, end);
	}
	if (es->type == COMP_TYPE_TODO) {
		ts start = simple_date_to_ts(s.start, zone);
		ts due = simple_date_to_ts(s.due, zone);
		if (start != -1) props_set_start(&es->p, start);
		if (due != -1) props_set_due(&es->p, due);
	}

	return 0;
}

struct simple_date parse_date(const char *str) {
	FILE *f = fmemopen((void*)str, strlen(str), "r");
	struct parser_state s = { f };
	struct simple_date res = make_simple_date(-1, -1, -1, -1, -1, -1);
	dt(&s, &res);
	fclose(f);
	return res;
}

/* tests */

static void test_dt(char *in, int out[5]) {
	FILE *f = fmemopen(in, strlen(in), "r");
	struct parser_state s = { f };
	struct simple_date sd;
	res r = dt(&s, &sd);
	if (r != OK) {
		fprintf(stderr, "res not ok\n");
		goto bad;
	}
	for (int i = 0; i < 5; ++i) {
		if (sd.t[i] != out[i]) goto bad;
	}
	fclose(f);
	return;
bad:
	fprintf(stderr,
		"in: `%s`, out: %d %d %d %d %d, t: %d %d %d %d %d\n",
		in,
		out[0], out[1], out[2], out[3], out[4],
		sd.t[0], sd.t[1], sd.t[2], sd.t[3], sd.t[4]);
	asrt(false, "test_dt error");
}

static void test_header(char *in, enum edit_method m, enum comp_type t) {
	enum edit_method method;
	enum comp_type type;
	FILE *f = fmemopen(in, strlen(in), "r");
	struct parser_state s = { f };

	fprintf(stderr, "test_header `%s`\n", in);
	res r = header(&s, &method, &type);
	asrt(r == OK, "res not ok");

	asrt(method == m, "method not ok");
	asrt(type == t, "type not ok");
	fclose(f);
}

static void test_literal(char *in, char *out) {
	FILE *f = fmemopen(in, strlen(in), "r");
	struct parser_state s = { f };
	struct str o = str_empty;
	res r = literal(&s, &o);
	asrt(r == OK, "test_literal error");
	asrt(strcmp(out, str_cstr(&o)) == 0, "test_literal not equal");
	str_free(&o);
	fclose(f);
}

static void test_prop(st s, char *in, struct edit_spec *es) {
	FILE *f = fmemopen(in, strlen(in), "r");
	s->f = f;
	prop(s, es);
	fclose(f);
}

static void test_grammar(st s, struct edit_spec *es, char *in) {
	FILE *f = fmemopen(in, strlen(in), "r");
	s->f = f;
	grammar(s, es);
	fclose(f);
}

void test_editor_parser() {
	test_dt("5", (int[5]){ -1, -1, 5, -1, -1 });
	test_dt("05", (int[5]){ -1, -1, 5, -1, -1 });
	test_dt("5-3", (int[5]){ -1, 5, 3, -1, -1 });
	test_dt("1234-5-3", (int[5]){ 1234, 5, 3, -1, -1 });
	test_dt("4321-9-13", (int[5]){ 4321, 9, 13, -1, -1 });
	test_dt("1122-94-52", (int[5]){ 1122, 94, 52, -1, -1 });

	test_dt("9:4", (int[5]){ -1, -1, -1, 9, 4 });
	test_dt("9:04", (int[5]){ -1, -1, -1, 9, 4 });
	test_dt("09:4", (int[5]){ -1, -1, -1, 9, 4 });
	test_dt("09:04", (int[5]){ -1, -1, -1, 9, 4 });

	test_dt("05 9:4", (int[5]){ -1, -1, 5, 9, 4 });
	test_dt("5-3 9:4", (int[5]){ -1, 5, 3, 9, 4 });
	test_dt("1234-56-78 90:12", (int[5]){ 1234, 56, 78, 90, 12 });

	test_header("create event", EDIT_METHOD_CREATE, COMP_TYPE_EVENT);
	test_header("update todo", EDIT_METHOD_UPDATE, COMP_TYPE_TODO);
	test_header("delete event", EDIT_METHOD_DELETE, COMP_TYPE_EVENT);

	test_literal("`asd`", "asd");
	test_literal("`a\n\nsd\n`", "a\n\nsd\n");
	test_literal("`\nhello\nworld`", "\nhello\nworld");

	struct edit_spec es;
	struct parser_state s;
	es.type = COMP_TYPE_EVENT;

	edit_spec_init(&es);
	test_prop(&s, "start 05-3 12:45", &es);
	asrt(simple_date_eq(s.start, make_simple_date(-1, 5, 3, 12, 45, -1)),
		"prop start");
	edit_spec_finish(&es);

	edit_spec_init(&es);
	test_prop(&s, "end 4", &es);
	asrt(simple_date_eq(s.end, make_simple_date(-1, -1, 4, -1, -1, -1)),
		"prop end");
	edit_spec_finish(&es);

	edit_spec_init(&es);
	test_prop(&s, "location `a\n\ns\n`", &es);
	asrt(strcmp(props_get_location(&es.p), "a\n\ns\n") == 0, "prop location");
	edit_spec_finish(&es);

	edit_spec_init(&es);
	test_prop(&s, "uid `asdfg`", &es);
	asrt(strcmp(str_cstr(&es.uid), "asdfg") == 0, "prop uid");
	edit_spec_finish(&es);

	// TODO: recurrence id
	// edit_spec_init(&es);
	// test_prop(&s, "instance `12345`", &es);
	// asrt(es.recurrence_id == 12345, "prop instance");

	edit_spec_init(&es);
	test_prop(&s, "class private", &es);
	enum prop_class class = (enum prop_class)123;
	props_get_class(&es.p, &class);
	asrt(class == PROP_CLASS_PRIVATE, "prop class");
	edit_spec_finish(&es);

	edit_spec_init(&es);
	es.type = COMP_TYPE_TODO;
	test_prop(&s, "status needsaction", &es);
	enum prop_status status = (enum prop_status)123;
	props_get_status(&es.p, &status);
	asrt(status == PROP_STATUS_NEEDSACTION, "prop status");
	edit_spec_finish(&es);

	edit_spec_init(&es);
	es.type = COMP_TYPE_TODO;
	test_prop(&s, "est 5s4d1h", &es);
	int est = -1;
	props_get_estimated_duration(&es.p, &est);
	asrt(est == 5 + 4*3600*24 + 1*3600, "prop est");
	edit_spec_finish(&es);

	edit_spec_init(&es);
	es.type = COMP_TYPE_TODO;
	test_prop(&s, "rel parent:a,child:b,sibling:c", &es);
	const struct vec *rels = props_get_related_to(&es.p);
	asrt(rels->len == 3, "");

	const struct prop_related_to *rel;

	rel = vec_get_c(rels, 0);
	asrt(rel->reltype == PROP_RELTYPE_PARENT, "");
	asrt(strcmp(str_cstr(&rel->uid), "a") == 0, "");

	rel = vec_get_c(rels, 1);
	asrt(rel->reltype == PROP_RELTYPE_CHILD, "");
	asrt(strcmp(str_cstr(&rel->uid), "b") == 0, "");

	rel = vec_get_c(rels, 2);
	asrt(rel->reltype == PROP_RELTYPE_SIBLING, "");
	asrt(strcmp(str_cstr(&rel->uid), "c") == 0, "");

	edit_spec_finish(&es);

	edit_spec_init(&es);
	test_grammar(&s, &es,
		"create event\n"
		"start 2020-4-5 12:00\n"
		"end 13:00\n\n"
		"calendar 12345\n"
		"-status\n"
		"summary `lol`\n"
		"# test comment\n"
		"location `somewhere`\n"
		"desc `some\nsome\ndesc`\n"
		"calendar `asdfg`\n"
	);

	asrt(es.type == COMP_TYPE_EVENT, "");
	asrt(simple_date_eq(s.start, make_simple_date(2020, 4, 5, 12, 0, -1)),
		"grammar prop start");
	asrt(simple_date_eq(s.end, make_simple_date(-1, -1, -1, 13, 0, -1)),
		"grammar prop end");
	asrt(strcmp(props_get_summary(&es.p), "lol") == 0, "");
	asrt(strcmp(props_get_location(&es.p), "somewhere") == 0, "");
	asrt(strcmp(props_get_desc(&es.p), "some\nsome\ndesc") == 0, "");
	asrt(props_mask_get(&es.rem, PROP_STATUS), "");
	asrt(strcmp(str_cstr(&es.calendar_uid), "asdfg") == 0, "");
	asrt(es.calendar_num == 12345, "");

	edit_spec_finish(&es);
}
