#ifndef _GUI_H_
#define _GUI_H_
#include <stdbool.h>
#include <stdint.h>
#include <cairo.h>
#include <sys/types.h>

struct window;
struct display;

typedef bool (*paint_cb)(struct window*, cairo_t*);
typedef void (*keyboard_cb)(struct display*, uint32_t key, uint32_t mods);
typedef void (*child_cb)(struct display*, pid_t pid);

struct window *
create_window(struct display *display, int width, int height);

void
destroy_window(struct window *window);

struct display *
create_display(paint_cb p_cb, keyboard_cb k_cb, child_cb c_cb);

void
destroy_display(struct display *display);

void
gui_run(struct window *window);

void
get_window_size(struct window *window, int *width, int *height);

#endif
