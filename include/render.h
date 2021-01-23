#ifndef GUI_CALENDAR_RENDER_H
#define GUI_CALENDAR_RENDER_H
#include <mgu/win.h>

typedef struct {
	int x, y, w, h;
} box;

bool render_application(void *env, struct mgu_win_surf *surf, uint64_t t);

#endif
