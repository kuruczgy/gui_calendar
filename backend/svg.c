#include <cairo/cairo.h>
#include <cairo/cairo-svg.h>
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
    int width, height;
    paint_cb p_cb;
    void *ud;
    const char *filename;
};

static void destroy(struct backend *backend) {
    free(backend->self);
}

static void gui_run(struct backend *backend) {
    struct self *self = backend->self;
    cairo_surface_t *surface;
    cairo_t *cr;

    surface = cairo_svg_surface_create(self->filename,
            self->width, self->height);
    cairo_svg_surface_set_document_unit(surface, CAIRO_SVG_UNIT_PX);
    cr = cairo_create(surface);
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

static struct backend_methods methods = {
    .destroy = &destroy,
    .run = &gui_run,
    .get_window_size = &get_window_size,
    .set_callbacks = &set_callbacks
};

struct backend backend_init_svg(const char *filename, int width, int height) {
    struct self *self = malloc_check(sizeof(struct self));
    *self = (struct self){
        .width = width,
        .height = height,
        .p_cb = NULL,
        .ud = NULL,
        .filename = filename
    };
    return (struct backend){ .vptr = &methods, .self = self };
}
