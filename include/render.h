#ifndef GUI_CALENDAR_RENDER_H
#define GUI_CALENDAR_RENDER_H
#include <cairo.h>
#include "backend.h"

typedef struct {
    int x, y, w, h;
} box;

bool render_application(void *ud, cairo_t *cr);
void render_calendar(cairo_t *cr, box b);

#endif
