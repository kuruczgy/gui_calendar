#ifndef GUI_CALENDAR_RENDER_H
#define GUI_CALENDAR_RENDER_H
#include <mgu/gl.h>
#include <mgu/win.h>
#include <ds/vec.h>

struct app;

struct w_sidebar {
	struct vec cal_texts; /* vec<struct mgu_texture> */
	struct vec filter_texts; /* vec<struct mgu_texture> */
	struct vec action_texts; /* vec<struct mgu_texture> */

	float width;
};

void w_sidebar_init(struct w_sidebar *w, struct app *app);
void w_sidebar_finish(struct w_sidebar *w);

struct float4 {
	union {
		float a[4];
		struct { float x, y, w, h; };
	};
};
typedef struct {
	int x, y, w, h;
} box;

bool render_application(void *env, struct mgu_win_surf *surf, uint64_t t);

#endif
