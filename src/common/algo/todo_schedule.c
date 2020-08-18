#include <stdlib.h>

#include "algo.h"
#include "core.h"

typedef struct ts_ran ran;
typedef struct { ts v; bool s; int i; } point;

static int cmp_point(const void *pa, const void *pb) {
	const point *a = pa, *b = pb;
	ts v = a->v - b->v;
	if (v == 0) {
		if (!a->s && b->s) return -1;
		if (a->s && !b->s) return 1;
	}
	return v < 0 ? -1 : 1;
}

/* b == -1 means infinity */
static struct schedule_todo *next_todo(int *j, int k,
		struct schedule_todo *T, ran *G, ts a, ts b) {
	for (; *j < k; ++*j) {
		if (G[*j].fr != -1) continue; // skip if already scheduled
		struct schedule_todo *td = &T[*j];
		if (td->estimated_duration <= 0) continue; // skip if no est
		if (b == -1) return td; // we can schedule anything with infinite space
		ts l = b - max_ts(a, (ts)td->start);
		if (td->estimated_duration > l) continue;
		return td;
	}
	return NULL;
}

/* b == -1 means infinity */
static void schedule_slot(ts a, ts b, int k,
		struct schedule_todo *T, ran *G, int *f) {
	struct schedule_todo *td;
	int j = 0; // index of todo returned by next_todo
	while (td = next_todo(&j, k, T, G, a, b)) {
		if (!td) break;
		a = max_ts(a, (ts)td->start);
		G[j] = (ran){ a, a + td->estimated_duration };
		if (++*f == k) return;
		a += td->estimated_duration;
	}
}

struct ts_ran * todo_schedule(ts base, int n, struct ts_ran *E,
		int k, struct schedule_todo *T) {
	asrt(k >= 0, "k negative");
	if (k == 0) return NULL;
	point *p = malloc_check(sizeof(point) * 2 * n);
	ran *G = malloc_check(sizeof(ran) * k);
	int f = 0; // number of todos scheduled
	for (int i = 0; i < k; ++i) G[i] = (ran){ -1, -1 };
	for (int i = 0; i < n; ++i) {
		p[2 * i] = (point){ .v = E[i].fr, .s = true,  .i = i };
		p[2*i+1] = (point){ .v = E[i].to, .s = false, .i = i };
	}
	qsort(p, n * 2, sizeof(point), &cmp_point);

	int d = 0; // number of current overlapping events
	asrt(p[0].s, "p[0] start");
	if (base < p[0].v) schedule_slot(base, p[0].v, k, T, G, &f);
	for (int i = 0; i < 2 * n - 1; ++i) {
		point c = p[i], cc = p[i + 1];
		d += c.s ? 1 : -1;
		asrt(d >= 0, "d negative");
		if (d == 0) {
			asrt(!c.s, "how did d become zero?");
			asrt(cc.s, "d would become negative");
			// here we are in a slot [c.v, cc.v)
			ts a = max_ts(c.v, base), b = cc.v;
			if (a > b) continue;
			schedule_slot(a, b, k, T, G, &f);
			if (f == k) break;
		}
	}
	if (f != k) schedule_slot(p[2 * n - 1].v, -1, k, T, G, &f);

	free(p);

	return G;
}
