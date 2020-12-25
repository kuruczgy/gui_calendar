#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "calendar.h"
#include "editor.h"
#include "core.h"
#include "util.h"
#include "algo.h"
#include "views.h"

static void test_todo_schedule() {
	// 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20
	// (    )[     [   ]   ](    )( )[    ](     )
	int n = 3;
	struct ts_ran E[] = { { 3, 8 }, { 6, 10 }, { 13, 15 } };
	int k = 4;
	struct schedule_todo T[] = {
		{ .estimated_duration = 3 },
		{ .estimated_duration = 2 },
		{ .estimated_duration = 1 },
		{ .estimated_duration = 2 }
	};
	struct ts_ran *G = todo_schedule(0, n, E, k, T);
	struct ts_ran exp[] = { { 0, 3 }, { 10, 12 }, { 12, 13 }, { 15, 17 } };
	for (int i = 0; i < k; ++ i) {
		asrt(G[i].fr == exp[i].fr, "");
		asrt(G[i].to == exp[i].to, "");
	}

	free(G);
}

static void test_lookup_color() {
	asrt(lookup_color("cornflowerblue", 14) == 0xFF6495ED, "");
	asrt(lookup_color("yellowgreen", 11) == 0xFF9ACD32, "");
	asrt(lookup_color("aliceblue", 9) == 0xFFF0F8FF, "");
	asrt(lookup_color("black", 5) == 0xFF000000, "");
	asrt(lookup_color("aaa", 3) == 0, "");
	asrt(lookup_color("eee", 3) == 0, "");
	asrt(lookup_color("zzz", 3) == 0, "");
}

extern void test_editor_parser();

static void write_to_file(const char *path, const char *data) {
	FILE *f = fopen(path, "w");
	asrt(f, "");
	fprintf(f, "%s", data);
	fclose(f);
}
static void test_editor_do(
		const char *ics, const char *edit, const char *exp_ics) {
	struct cal_timezone *zone = cal_timezone_new("Europe/Budapest");
	system("mkdir -p /tmp/test_cal");
	write_to_file("/tmp/test_cal/uid.ics", ics);
	struct calendar cal;
	calendar_init(&cal);
	cal.storage = str_new_from_cstr("/tmp/test_cal");
	update_calendar_from_storage(&cal, zone);

	/* do edit */
	FILE *f = fmemopen((void*)edit, strlen(edit), "r");
	asrt(f, "");
	struct edit_spec es;
	edit_spec_init(&es);
	asrt(parse_edit_template(f, &es, zone) == 0, "cant parse edit spec");
	fclose(f);
	asrt(apply_edit_spec_to_calendar(&es, &cal) == 0, "cant apply edit spec");
	edit_spec_finish(&es);

	f = fopen("/tmp/test_cal/uid.ics", "r");
	asrt(f, "");
	struct comp c_file;
	comp_init_from_ics(&c_file, f);
	fclose(f);

	f = fmemopen((void*)exp_ics, strlen(exp_ics), "r");
	asrt(f, "");
	struct comp c_exp;
	comp_init_from_ics(&c_exp, f);
	fclose(f);

	struct comp *c_act = calendar_get_comp(&cal, 0);
	asrt(comp_equal(&c_file, c_act), "comps do not match");
	asrt(comp_equal(&c_exp, c_act), "comps do not match");

	comp_finish(&c_file);
	comp_finish(&c_exp);

	calendar_finish(&cal);
	cal_timezone_destroy(zone);
}
static void test_editor() {
	test_editor_do(
	/* base */
	"BEGIN:VCALENDAR\n"
	"VERSION:2.0\n"
	"BEGIN:VEVENT\n"
	"DTSTART:20200101T120000Z\n"
	"DTEND:20200101T140000Z\n"
	"UID:uid\n"
	"SUMMARY:sum\n"
	"END:VEVENT\n"
	"END:VCALENDAR\n",

	/* edit */
	"update event\n"
	"start 2020-01-01 13:01\n"
	"end 2020-01-01 15:02\n"
	"status cancelled\n"
	"class private\n"
	"color red\n"
	"summary edit with space\n"
	"location `loc`\n"
	"desc `dede`\n"
	"cats `a,s,d,f,g`\n"
	"uid `uid`\n",

	/* expected result */
	"BEGIN:VCALENDAR\n"
	"VERSION:2.0\n"
	"BEGIN:VEVENT\n"
	"DTSTART:20200101T120100Z\n"
	"DTEND:20200101T140200Z\n"
	"STATUS:CANCELLED\n"
	"CLASS:PRIVATE\n"
	"COLOR:red\n"
	"SUMMARY:edit with space\n"
	"LOCATION:loc\n"
	"DESCRIPTION:dede\n"
	"CATEGORIES:a,s,d,f,g\n"
	"UID:uid\n"
	"END:VEVENT\n"
	"END:VCALENDAR\n"
	);

	test_editor_do(
	/* base */
	"BEGIN:VCALENDAR\n"
	"VERSION:2.0\n"
	"BEGIN:VTODO\n"
	"DTSTART:20200101T120000Z\n"
	"DTDUE:20200101T140000Z\n"
	"UID:uid\n"
	"SUMMARY:sum\n"
	"END:VTODO\n"
	"END:VCALENDAR\n",

	/* edit */
	"update todo\n"
	"start 2020-01-01 13:01\n"
	"due 2020-01-01 15:02\n"
	"status inprocess\n"
	"class private\n"
	"est 1d2h3m4s\n"
	"perc 42\n"
	"color `red`\n"
	"summary `edit`\n"
	"location `loc`\n"
	"desc `dede`\n"
	"cats `a,s,d,f,g`\n"
	"uid `uid`\n",

	/* expected result */
	"BEGIN:VCALENDAR\n"
	"VERSION:2.0\n"
	"BEGIN:VTODO\n"
	"DTSTART:20200101T120100Z\n"
	"DUE:20200101T140200Z\n"
	"STATUS:IN-PROCESS\n"
	"CLASS:PRIVATE\n"
	"ESTIMATED-DURATION:P1DT2H3M4S\n"
	"PERCENT-COMPLETE:42\n"
	"COLOR:red\n"
	"SUMMARY:edit\n"
	"LOCATION:loc\n"
	"DESCRIPTION:dede\n"
	"CATEGORIES:a,s,d,f,g\n"
	"UID:uid\n"
	"END:VTODO\n"
	"END:VCALENDAR\n"
	);
}

extern int slicing_test_get_total_len(struct slicing *s);
static void vec_ts_ran_asrt(struct vec *v, struct ts_ran *a, int n) {
	asrt(v->len == n, "");
	for (int i = 0; i < n; ++i) {
		struct ts_ran *vi = vec_get(v, i);
		struct ts_ran *ai = &a[i];
		asrt(vi->fr == ai->fr, "");
		asrt(vi->to == ai->to, "");
	}
}
static void test_slicing_f(void *env, struct ts_ran ran,
		struct simple_date label) {
	struct vec *res = env;
	vec_append(res, &ran);
}
static void test_slicing() {
	struct cal_timezone *zone = cal_timezone_new("Europe/Budapest");
	struct slicing *s = slicing_create(zone);

	struct vec res = vec_new_empty(sizeof(struct ts_ran));

	slicing_iter_items(s, &res, test_slicing_f, SLICING_MONTH,
		(struct ts_ran){ 1594846758, 1597522424 });
	vec_ts_ran_asrt(&res, (struct ts_ran[]){
		{ 1593554400, 1596232800 },
		{ 1596232800, 1598911200 }
	}, 2);
	vec_clear(&res);

	slicing_iter_items(s, &res, test_slicing_f, SLICING_HOUR,
		(struct ts_ran){ 1577826660, 1577835180 });
	vec_ts_ran_asrt(&res, (struct ts_ran[]){
		{ 1577826000, 1577829600 },
		{ 1577829600, 1577833200 },
		{ 1577833200, 1577836800 }
	}, 3);
	vec_clear(&res);

	slicing_iter_items(s, &res, test_slicing_f, SLICING_DAY,
		(struct ts_ran){ 1577919599, 1609369200 });
	asrt(res.len == 365, "");
	vec_clear(&res);

	int start_n = slicing_test_get_total_len(s);
	for (int i = 0; i < 20; ++i) {
		slicing_iter_items(s, &res, test_slicing_f, SLICING_DAY,
			(struct ts_ran){ 1577919599, 1609369200 });
		vec_clear(&res);
	}
	asrt(start_n == slicing_test_get_total_len(s), "");

	vec_free(&res);
	slicing_destroy(s);
	cal_timezone_destroy(zone);
}

int main() {
	test_todo_schedule();
	test_lookup_color();
	test_editor_parser();
	test_editor();
	test_slicing();
}
