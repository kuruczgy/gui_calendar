#include <cairo/cairo.h>
#include <fcntl.h>
#include <linux/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "gui.h"
#include "util.h"

struct display;
struct window {
    struct display *display;
};
struct display {
    struct window window;
    paint_cb p_cb;
    keyboard_cb k_cb;
    child_cb c_cb;
    int width, height;
};

struct display *
create_display(paint_cb p_cb, keyboard_cb k_cb, child_cb c_cb) {
    fprintf(stderr, "creating dummy display\n");
    struct display *display;
    display = malloc(sizeof *display);
    assert(display, "oom");

    display->k_cb = k_cb;
    display->p_cb = p_cb;
    display->c_cb = c_cb;

    return display;
}

void
destroy_display(struct display *display) {
    fprintf(stderr, "destroying dummy display\n");
    free(display);
}

struct window *
create_window(struct display *display, int width, int height) {
    display->width = width;
    display->height = height;
    display->window.display = display;
    return &(display->window);
}

void
destroy_window(struct window *window) {
    // no-op
}

void gui_run(struct window *window) {
    struct display *display = window->display;
    cairo_surface_t *surface;
    cairo_t *cr;

    /* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
            display->width, display->height); */
    surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 0, 0);
    cr = cairo_create(surface);

    display->p_cb(window, cr);
    // cairo_surface_write_to_png(surface, "out.png");

    /* Destroy and release all cairo related contexts */
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

void
get_window_size(struct window *window, int *width, int *height) {
    *width = window->display->width;
    *height = window->display->height;
}
