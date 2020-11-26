#include <stdlib.h>
#include <ds/vec.h>
#include <stdbool.h>
#include "loop.h"

struct loop_entry {
	void *env;
	loop_cb cb;
};
struct loop {
	struct vec pollfds; /* vec<struct pollfd> */
	struct vec entries; /* vec<struct loop_entry> */
	bool stop;
};

struct loop *loop_create() {
	struct loop *l = malloc(sizeof(struct loop));
	if (!l) {
		return NULL;
	}
	l->pollfds = vec_new_empty(sizeof(struct pollfd));
	l->entries = vec_new_empty(sizeof(struct loop_entry));
	l->stop = false;
	return l;
}
void loop_destroy(struct loop *l) {
	vec_free(&l->pollfds);
	vec_free(&l->entries);
	free(l);
}
void loop_add(struct loop *l, int fd, short evs, void *env, loop_cb cb) {
	struct pollfd pfd = { .fd = fd, .events = evs };
	vec_append(&l->pollfds, &pfd);

	struct loop_entry entry = { .env = env, .cb = cb };
	vec_append(&l->entries, &entry);
}
void loop_remove(struct loop *l, int fd) {
	for (int i = 0; i < l->pollfds.len; ++i) {
		struct pollfd *pfd = vec_get(&l->pollfds, i);
		if (pfd->fd == fd) {
			vec_remove(&l->pollfds, i);
			vec_remove(&l->entries, i);
			break;
		}
	}
}
void loop_stop(struct loop *l) {
	l->stop = true;
}
void loop_run(struct loop *l) {
	while (!l->stop && poll(l->pollfds.d, l->pollfds.len, -1) != -1) {
		for (int i = 0; i < l->pollfds.len; ++i) {
			struct pollfd *pfd = vec_get(&l->pollfds, i);
			if (pfd->revents != 0) {
				struct loop_entry *e = vec_get(&l->entries, i);
				e->cb(e->env, *pfd);
			}
		}
	}
}
