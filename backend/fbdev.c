/*
 * code based on: https://github.com/toradex/cairo-fb-examples
 */
#include <cairo/cairo.h>
#include <fcntl.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/fb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "backend.h"
#include "util.h"

typedef struct _cairo_linuxfb_device {
    int fb_fd;
    unsigned char *fb_data;
    long fb_screensize;
    struct fb_var_screeninfo fb_vinfo;
    struct fb_fix_screeninfo fb_finfo;
} cairo_linuxfb_device_t;

struct display {
    cairo_linuxfb_device_t *dev;
    cairo_surface_t *fbsurface;
    cairo_t *fbcr;
    bool rotate;
    int width, height;

    paint_cb p_cb;
    void *ud;
};

/* Destroy a cairo surface */
static void cairo_linuxfb_surface_destroy(void *device)
{
    cairo_linuxfb_device_t *dev = (cairo_linuxfb_device_t *)device;

    if (dev == NULL)
        return;

    munmap(dev->fb_data, dev->fb_screensize);
    close(dev->fb_fd);
    free(dev);
}

/* Create a cairo surface using the specified framebuffer */
static cairo_surface_t *cairo_linuxfb_surface_create(cairo_linuxfb_device_t *device, const char *fb_name)
{
    cairo_surface_t *surface;

    // Open the file for reading and writing
    device->fb_fd = open(fb_name, O_RDWR);
    if (device->fb_fd == -1) {
        perror("Error: cannot open framebuffer device");
        goto handle_allocate_error;
    }

    // Get variable screen information
    if (ioctl(device->fb_fd, FBIOGET_VSCREENINFO, &device->fb_vinfo) == -1) {
        perror("Error: reading variable information");
        goto handle_ioctl_error;
    }

    // Get fixed screen information
    if (ioctl(device->fb_fd, FBIOGET_FSCREENINFO, &device->fb_finfo) == -1) {
        perror("Error reading fixed information");
        goto handle_ioctl_error;
    }

    // Map the device to memory
    device->fb_data = (unsigned char *)mmap(0, device->fb_finfo.smem_len,
            PROT_READ | PROT_WRITE, MAP_SHARED,
            device->fb_fd, 0);
    if (device->fb_data == MAP_FAILED) {
        perror("Error: failed to map framebuffer device to memory");
        goto handle_ioctl_error;
    }


    /* Create the cairo surface which will be used to draw to */
    surface = cairo_image_surface_create_for_data(device->fb_data,
            CAIRO_FORMAT_RGB24,
            device->fb_vinfo.xres,
            device->fb_vinfo.yres,
            cairo_format_stride_for_width(CAIRO_FORMAT_RGB24,
                device->fb_vinfo.xres));
    cairo_surface_set_user_data(surface, NULL, device,
            &cairo_linuxfb_surface_destroy);

    return surface;

handle_ioctl_error:
    close(device->fb_fd);
handle_allocate_error:
    free(device);
    exit(1);
}

static void destroy_display(struct backend *backend) {
    struct display *display = backend->self;
    cairo_destroy(display->fbcr);
    cairo_surface_destroy(display->fbsurface);
    free(display->dev);
    free(display);
}

static void gui_run(struct backend *backend) {
    fprintf(stderr, "fbdev::gui_run called\n");
    struct display *display = backend->self;
    int fbsizex = display->width;
    int fbsizey = display->height;
    cairo_surface_t *surface;
    cairo_t *cr;

    surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
            fbsizex, fbsizey);
    cr = cairo_create(surface);

    while (true) {
        display->p_cb(display->ud, cr);

        if (display->rotate) {
            cairo_identity_matrix(display->fbcr);
            cairo_translate(display->fbcr, fbsizey / 2.0, fbsizex / 2.0);
            cairo_rotate(display->fbcr, M_PI/2);
            cairo_translate(display->fbcr, -fbsizex / 2.0, -fbsizey / 2.0);
        }
        cairo_set_source_surface(display->fbcr, surface, 0, 0);
        cairo_paint(display->fbcr);
        struct timespec req = { .tv_sec = 0, .tv_nsec = 2000000L };
        nanosleep(&req, NULL);
    }

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

struct backend backend_init_fbdev() {
    fprintf(stderr, "fbdev::create_display called\n");
    struct display *display;
    display = malloc(sizeof *display);
    assert(display, "oom");

    display->dev = malloc(sizeof(cairo_linuxfb_device_t));
    assert(display->dev, "oom");
    char fb_node[16] = "/dev/fb0";
    display->fbsurface = cairo_linuxfb_surface_create(display->dev, fb_node);
    display->fbcr = cairo_create(display->fbsurface);

    display->p_cb = NULL;
    display->ud = NULL;

    display->rotate = true;

    if (!display->rotate) {
        display->width = display->dev->fb_vinfo.xres;
        display->height = display->dev->fb_vinfo.yres;
    } else {
        display->width = display->dev->fb_vinfo.yres;
        display->height = display->dev->fb_vinfo.xres;
    }

    fprintf(stderr, "dev: %s, x: %d, y: %d, bits per pixel: %d\n",
        fb_node,
        display->dev->fb_vinfo.xres, display->dev->fb_vinfo.yres,
        display->dev->fb_vinfo.bits_per_pixel);

    return (struct backend){ .vptr = &methods, .self = display };
}
