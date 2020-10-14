#ifndef GUI_CALENDAR_RENDER_H
#define GUI_CALENDAR_RENDER_H
typedef struct {
	int x, y, w, h;
} box;

bool render_application(void *env, float t);

#endif
