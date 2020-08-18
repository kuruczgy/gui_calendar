#include "backend.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <sys/mman.h>
#include <poll.h>
#include <sys/wait.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <cairo.h>

#include "xdg-shell-client-protocol.h"

#include "config.h"
#include "core.h"
#include "util.h"

struct window;

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
	struct wl_pointer *pointer;
	paint_cb p_cb;
	keyboard_cb k_cb;
	child_cb c_cb;
	void *ud;

	struct window *window;
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
	struct buffer buffers[2];
	struct buffer *prev_buffer;
	struct wl_callback *callback;
	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	struct wl_cursor_image *cursor_image;
	bool wait_for_configure;
	bool running;
};

static void
redraw(void *data, struct wl_callback *callback, uint32_t time);

// wl_buffer_listener

static void
handle_wl_buffer_release(void *data, struct wl_buffer *buffer)
{
	struct buffer *mybuf = data;
	mybuf->busy = 0;
}

static const struct wl_buffer_listener wl_buffer_listener = {
	handle_wl_buffer_release
};

static int
create_shm_pool(struct display *display, int size) {
	int fd = os_create_anonymous_file(size);
	asrt(fd >= 0, "anon file");
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %s\n",
			size, strerror(errno));
		return -1;
	}

	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	display->pool = wl_shm_create_pool(display->wl_shm, fd, size);
	display->pool_data = data;
	display->pool_size = size;
	close(fd);
	return 0;
}

static int
create_shm_buffer(struct display *display, struct buffer *buffer,
		int width, int height, uint32_t format)
{
	int size, stride;

	cairo_format_t cairo_format;
	if (format == WL_SHM_FORMAT_XRGB8888) {
		cairo_format = CAIRO_FORMAT_ARGB32;
	} else {
		fprintf(stderr, "bad format\n");
		return -1;
	}

	stride = cairo_format_stride_for_width(cairo_format, width);
	size = stride * height;
	if (size > display->pool_size) return -1;

	buffer->buffer = wl_shm_pool_create_buffer(display->pool, 0,
		width, height, stride, format);
	wl_buffer_add_listener(buffer->buffer, &wl_buffer_listener, buffer);

	buffer->shm_data = display->pool_data;
	buffer->cairo_surface = cairo_image_surface_create_for_data(
		display->pool_data,
		cairo_format,
		width,
		height,
		stride
	);

	return 0;
}

void
destroy_buffer(struct buffer *buffer) {
	if (buffer->buffer) wl_buffer_destroy(buffer->buffer);
	if (buffer->cairo_surface) cairo_surface_destroy(buffer->cairo_surface);

	buffer->buffer = NULL;
	buffer->cairo_surface = NULL;
	buffer->shm_data = NULL;
	buffer->busy = 0;
}

// xdg_surface_listener

static void
handle_xdg_surface_configure(void *data, struct xdg_surface *surface,
		uint32_t serial)
{
	struct window *window = data;
	xdg_surface_ack_configure(surface, serial);
	if (window->wait_for_configure) {
		redraw(window, NULL, 0);
		window->wait_for_configure = false;
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_xdg_surface_configure,
};

// xdg_toplevel_listener

static void
handle_xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
		int32_t width, int32_t height, struct wl_array *state)
{
	struct window *window = data;

	if (height != 0 && width != 0 &&
			(window->width != width || window->height != height)) {
		window->width = width;
		window->height = height;

		destroy_buffer(&window->buffers[0]);
		destroy_buffer(&window->buffers[1]);

		// fprintf(stderr, "xdg_toplevel_configure: %dx%d\n", width, height);
	}
}

static void
handle_xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	((struct window*)data)->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	handle_xdg_toplevel_configure,
	handle_xdg_toplevel_close,
};

static struct window *
create_window(struct display *display, int width, int height) {
	struct window *window;

	window = malloc(sizeof(struct window));
	asrt(window, "oom");

	window->callback = NULL;
	window->running = true;
	window->display = display;
	window->width = width;
	window->height = height;
	window->surface = wl_compositor_create_surface(display->compositor);
	window->buffers[0] = (struct buffer){ NULL, NULL, NULL, 0 };
	window->buffers[1] = (struct buffer){ NULL, NULL, NULL, 0 };

	window->xdg_surface = xdg_wm_base_get_xdg_surface(display->wm_base,
			window->surface);
	asrt(window->xdg_surface, "xdg_surface");
	xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener,
			window);

	window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
	asrt(window->xdg_toplevel, "xdg_toplevel");
	xdg_toplevel_add_listener(window->xdg_toplevel, &xdg_toplevel_listener,
			window);

	xdg_toplevel_set_title(window->xdg_toplevel, CONFIG_TITLE);
	wl_surface_commit(window->surface);
	window->wait_for_configure = true;

	// load cursor stuff
	window->cursor_theme =
		wl_cursor_theme_load(NULL, 24, display->wl_shm);
	asrt(window->cursor_theme, "cursor theme");
	struct wl_cursor *cursor =
		wl_cursor_theme_get_cursor(window->cursor_theme, "left_ptr");
	asrt(cursor->image_count > 0, "wrong cursor->image_count");
	window->cursor_image = cursor->images[0];
	struct wl_buffer *cursor_buffer =
		wl_cursor_image_get_buffer(window->cursor_image);
	window->cursor_surface = wl_compositor_create_surface(display->compositor);
	wl_surface_attach(window->cursor_surface, cursor_buffer, 0, 0);
	wl_surface_commit(window->cursor_surface);

	return window;
}

static void
destroy_window(struct window *window)
{
	if (window->cursor_surface) wl_surface_destroy(window->cursor_surface);
	if (window->cursor_theme) wl_cursor_theme_destroy(window->cursor_theme);
	if (window->callback) wl_callback_destroy(window->callback);
	destroy_buffer(&window->buffers[0]);
	destroy_buffer(&window->buffers[1]);
	if (window->xdg_toplevel) xdg_toplevel_destroy(window->xdg_toplevel);
	if (window->xdg_surface) xdg_surface_destroy(window->xdg_surface);
	if (window->surface) wl_surface_destroy(window->surface);
	free(window);
}

static struct buffer *
window_next_buffer(struct window *window)
{
	struct buffer *buffer;
	int ret = 0;

	if (!window->buffers[0].busy) buffer = &window->buffers[0];
	else if (!window->buffers[1].busy) buffer = &window->buffers[1];
	else return NULL;

	if (!buffer->buffer) {
		ret = create_shm_buffer(window->display, buffer,
					window->width, window->height,
					WL_SHM_FORMAT_XRGB8888);
		if (ret < 0) return NULL;

		/* paint the padding */
		memset(buffer->shm_data, 0xff, window->width * window->height * 4);
	}

	return buffer;
}

static const struct wl_callback_listener frame_listener;
static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	struct buffer *buffer;

	buffer = window_next_buffer(window);
	asrt(buffer, !callback ? "Failed to create the first buffer.\n" :
			"Both buffers busy at redraw(). Server bug?\n");

	cairo_t *cr = cairo_create(buffer->cairo_surface);
	asrt(window->display->p_cb, "no paint callback!");
	bool damage = window->display->p_cb(window->display->ud, cr);
	cairo_destroy(cr);

	wl_surface_attach(window->surface, buffer->buffer, 0, 0);
	if (damage)
		wl_surface_damage(window->surface, 0, 0, window->width, window->height);

	if (callback) wl_callback_destroy(callback);

	window->callback = wl_surface_frame(window->surface);
	wl_callback_add_listener(window->callback, &frame_listener, window);
	wl_surface_commit(window->surface);
	buffer->busy = 1;
}
static const struct wl_callback_listener frame_listener = { redraw };

// xdg_wm_base_listener

static void
handle_xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	handle_xdg_wm_base_ping,
};

// wl_keyboard_listener

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	// Who cares
}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {
	// Who cares
}

static uint32_t mods_state = 0;
static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t _key_state) {
	struct display *d = data;
	enum wl_keyboard_key_state key_state = _key_state;
	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		// fprintf(stderr, "pressed: %d\n", key);
		if (d->k_cb) d->k_cb(d->ud, key, mods_state);
	}
}

static void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	/* printf("keyboard_modifiers: dep: %x, lat: %x, loc: %x\n",
			mods_depressed, mods_latched, mods_locked); */
	mods_state = mods_depressed;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
		int32_t rate, int32_t delay) {
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

// wl_pointer_listener
static void pointer_enter(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t x, wl_fixed_t y) {
	struct display *d = data;
	struct wl_cursor_image *img = d->window->cursor_image;
	wl_pointer_set_cursor(pointer, serial, d->window->cursor_surface,
		img->hotspot_x, img->hotspot_y);
}
static void pointer_leave(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface) {
	// Who cares
}
static void pointer_motion(void *data, struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	// Who cares
}
void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
		uint32_t time, uint32_t button, uint32_t state) {
	// Who cares
}
void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
		uint32_t axis, wl_fixed_t value) {
	// Who cares
}
const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis
};

// wl_seat_listener

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct display *d = data;
	if (d->keyboard) {
		wl_keyboard_release(d->keyboard);
		d->keyboard = NULL;
	}
	if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
		d->keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(d->keyboard, &keyboard_listener, d);
	}
	if (caps & WL_SEAT_CAPABILITY_POINTER) {
		d->pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(d->pointer, &pointer_listener, d);
	}
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat,
		const char *name) {
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

// wl_registry_listener

static void
handle_wl_registry_global(void *data, struct wl_registry *registry, uint32_t
		id, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor = wl_registry_bind(registry, id,
				&wl_compositor_interface, 1);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		d->wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(d->wm_base, &xdg_wm_base_listener, d);
	} else if (strcmp(interface, "wl_shm") == 0) {
		d->wl_shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0) {
		if (!d->wl_seat) {
			d->wl_seat = wl_registry_bind(
					registry, id, &wl_seat_interface, 3);
			wl_seat_add_listener(d->wl_seat, &seat_listener, d);
		}
	} else {
		// fprintf(stderr, "reg: %s\n", interface);
	}
}

static void
handle_wl_registry_global_remove(void *data, struct wl_registry *registry,
		uint32_t name)
{
}

static const struct wl_registry_listener wl_registry_listener = {
	handle_wl_registry_global,
	handle_wl_registry_global_remove
};

static void destroy_display(struct backend *backend)
{
	struct display *display = backend->self;
	destroy_window(display->window);

	if (display->keyboard) wl_keyboard_destroy(display->keyboard);
	if (display->pointer) wl_pointer_destroy(display->pointer);
	if (display->wl_seat) wl_seat_destroy(display->wl_seat);
	if (display->pool) wl_shm_pool_destroy(display->pool);
	if (display->wl_shm) wl_shm_destroy(display->wl_shm);
	if (display->wm_base) xdg_wm_base_destroy(display->wm_base);
	if (display->compositor) wl_compositor_destroy(display->compositor);
	wl_registry_destroy(display->registry);
	wl_display_flush(display->display);
	wl_display_disconnect(display->display);
	free(display);
}

static void handle_children(struct display *display) {
	pid_t pid;
	while((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
		fprintf(stderr, "handle_children got pid: %d\n", pid);
		if (display->c_cb) display->c_cb(display->ud, pid);
	}
}

static void gui_run(struct backend *backend) {
	struct window *window = ((struct display*)backend->self)->window;
	wl_surface_damage(window->surface, 0, 0, window->width, window->height);
	if (!window->wait_for_configure) redraw(window, NULL, 0);

	struct wl_display *display = window->display->display;

	int ret = 0;
	// polling code from
	// https://git.sr.ht/~sircmpwn/wlroots/tree/b9b397ef8094b221bc1042aedf0dbbbb5d9a5f1e/examples/dmabuf-capture.c#L631
	while (window->running && ret != -1) {
		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);

		struct pollfd fds[1];
		fds[0].fd = wl_display_get_fd(display);
		fds[0].events = POLLIN | POLLERR | POLLHUP;
		ret = poll(fds, 1, 500);

		handle_children(window->display);

		if (!(fds[0].revents & POLLIN)) wl_display_cancel_read(display);
		if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(display) < 0) break;
			wl_display_dispatch_pending(display);
		}
	}
}

static void get_window_size(struct backend *backend, int *width, int *height) {
	struct window *window = ((struct display*)backend->self)->window;
	*width = window->width;
	*height = window->height;
}

static void set_callbacks(struct backend *backend,
		paint_cb p_cb, keyboard_cb k_cb, child_cb c_cb, void *ud) {
	struct display *display = backend->self;
	display->k_cb = k_cb;
	display->p_cb = p_cb;
	display->c_cb = c_cb;
	display->ud = ud;
}

static bool is_interactive(struct backend *backend) {
	return true;
}

static struct backend_methods methods = {
	.destroy = &destroy_display,
	.run = &gui_run,
	.get_window_size = &get_window_size,
	.set_callbacks = &set_callbacks,
	.is_interactive = &is_interactive
};

struct backend backend_init_wayland() {
	struct display *display;

	display = malloc(sizeof *display);
	asrt(display, "oom");
	display->display = wl_display_connect(NULL);
	asrt(display->display, "wl_display_connect");

	display->wl_seat = NULL;
	display->keyboard = NULL;

	display->k_cb = NULL;
	display->p_cb = NULL;
	display->c_cb = NULL;
	display->ud = NULL;

	display->registry = wl_display_get_registry(display->display);
	wl_registry_add_listener(display->registry, &wl_registry_listener, display);
	wl_display_roundtrip(display->display);
	asrt(display->wl_shm, "wl_shm");
	asrt(display->wm_base, "xdg_wm_base");
	asrt(display->compositor, "wl_compositor");

	// TODO: maximum screen size
	asrt(create_shm_pool(display, 1920 * 1200 * 4) == 0, "create_shm_pool");
	asrt(display->pool, "pool");

	wl_display_roundtrip(display->display);

	display->window = create_window(display, 100, 100);

	return (struct backend){ .vptr = &methods, .self = display };
}
