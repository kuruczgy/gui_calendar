#ifndef _GUI_H_
#define _GUI_H_
#include <stdbool.h>
#include <stdint.h>
#include <cairo.h>

struct window;
struct display;

typedef bool (*paint_cb)(struct window*, cairo_t*);
typedef void (*keyboard_cb)(struct display*, uint32_t key, uint32_t mods);

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct wl_shm *wl_shm;
    struct wl_shm_pool *pool;
    int pool_size;
    void *pool_data;
    struct wl_seat *wl_seat;
    struct wl_keyboard *keyboard;
    paint_cb p_cb;
    keyboard_cb k_cb;
};

struct buffer {
	struct wl_buffer *buffer;
    cairo_surface_t *cairo_surface;
	void *shm_data;
	int busy;
};

struct window {
	struct display *display;
	int width, height;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
    struct wl_shm_pool *shm_pool;
	struct buffer buffers[2];
	struct buffer *prev_buffer;
	struct wl_callback *callback;
	bool wait_for_configure;
    bool running;
};

struct window *
create_window(struct display *display, int width, int height);

void
destroy_window(struct window *window);

struct display *
create_display(paint_cb p_cb, keyboard_cb k_cb);

void
destroy_display(struct display *display);

void
gui_run(struct window *window);

#endif
