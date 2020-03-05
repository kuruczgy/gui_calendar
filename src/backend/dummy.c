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

struct self {
    paint_cb p_cb;
    void *ud;
    int width, height;
};

static void destroy(struct backend *backend) {
    struct self *self = backend->self;
    fprintf(stderr, "destroying dummy display\n");
    free(self);
}

static void gui_run(struct backend *backend) {
    struct self *self = backend->self;
    cairo_surface_t *surface;
    cairo_t *cr;

    surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 0, 0);
    cr = cairo_create(surface);

    assert(self->p_cb, "no paint callback!");
    self->p_cb(self->ud, cr);

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

struct backend backend_init_dummy() {
    fprintf(stderr, "creating dummy display\n");
    struct self *self;
    self = malloc(sizeof(struct self));
    assert(self, "oom");

    self->p_cb = NULL;
    self->ud = NULL;

    return (struct backend){ .vptr = &methods, .self = self };
}
