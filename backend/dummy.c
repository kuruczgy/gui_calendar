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

#include "backend.h"
#include "util.h"

struct display {
    paint_cb p_cb;
    void *ud;
    int width, height;
};

static void destroy_display(struct backend *backend) {
    struct display *display = backend->self;
    fprintf(stderr, "destroying dummy display\n");
    free(display);
}

static void gui_run(struct backend *backend) {
    struct display *display = backend->self;
    cairo_surface_t *surface;
    cairo_t *cr;

    /* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
            display->width, display->height); */
    surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 0, 0);
    cr = cairo_create(surface);

    assert(display->p_cb, "no paint callback!");
    display->p_cb(display->ud, cr);
    // cairo_surface_write_to_png(surface, "out.png");

    /* Destroy and release all cairo related contexts */
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

static void get_window_size(struct backend *backend, int *width, int *height) {
    struct display *display = backend->self;
    *width = display->width;
    *height = display->height;
}

static void set_callbacks(struct backend *backend, 
        paint_cb p_cb, keyboard_cb k_cb, child_cb c_cb, void *ud) {
    struct display *display = backend->self;
    display->p_cb = p_cb;
    display->ud = ud;
}

static struct backend_methods methods = {
    .destroy = &destroy_display,
    .run = &gui_run,
    .get_window_size = &get_window_size,
    .set_callbacks = &set_callbacks
};

struct backend backend_init_dummy(paint_cb p_cb, void *ud) {
    fprintf(stderr, "creating dummy display\n");
    struct display *display;
    display = malloc(sizeof *display);
    assert(display, "oom");

    display->p_cb = NULL;
    display->ud = NULL;

    return (struct backend){ .vptr = &methods, .self = display };
}
