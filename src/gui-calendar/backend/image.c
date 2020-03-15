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
#include "core.h"

struct self {
    paint_cb p_cb;
    void *ud;
    int width, height;
    const char *filename;
};

static void destroy(struct backend *backend) {
    struct self *self = backend->self;
    free(self);
}

static void gui_run(struct backend *backend) {
    struct self *self = backend->self;
    cairo_surface_t *surface;
    cairo_t *cr;

    surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
            self->width, self->height);
    cr = cairo_create(surface);

    asrt(self->p_cb, "no paint callback!");
    self->p_cb(self->ud, cr);
    cairo_surface_write_to_png(surface, self->filename);

    /* Destroy and release all cairo related contexts */
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

static void get_window_size(struct backend *backend, int *width, int *height) {
    struct self *self = backend->self;
    *width = self->width;
    *height = self->height;
}

static void set_callbacks(struct backend *backend,
        paint_cb p_cb, keyboard_cb k_cb, child_cb c_cb, void *ud) {
    struct self *self = backend->self;
    self->p_cb = p_cb;
    self->ud = ud;
}

static bool is_interactive(struct backend *backend) {
    return false;
}

static struct backend_methods methods = {
    .destroy = &destroy,
    .run = &gui_run,
    .get_window_size = &get_window_size,
    .set_callbacks = &set_callbacks,
    .is_interactive = &is_interactive
};

struct backend backend_init_image(const char *filename, int width, int height) {
    struct self *self = malloc_check(sizeof(struct self));
    *self = (struct self){
        .p_cb = NULL,
        .ud = NULL,
        .width = width,
        .height = height,
        .filename = filename
    };
    return (struct backend){ .vptr = &methods, .self = self };
}
