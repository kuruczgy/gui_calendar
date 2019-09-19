
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

#include <wayland-client.h>
#include <cairo.h>
#include <pango/pango.h>
#include "util.h"
#include "xdg-shell-client-protocol.h"
#include "parse.h"
#include "pango.h"

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct wl_shm *wl_shm;
    struct wl_seat *wl_seat;
    struct wl_keyboard *keyboard;
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
};

static struct calendar cal[16];
static int n_cal;

static int running = 1;
static int week_offset = 0;

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

static void assert(bool b, const char *msg) {
    if (!b) {
        fprintf(stderr, "assert error msg: %s\n", msg);
        exit(1);
    }
}

static int
create_shm_buffer(struct display *display, struct buffer *buffer,
		  int width, int height, uint32_t format)
{
	struct wl_shm_pool *pool;
	int fd, size, stride;
	void *data;

    cairo_format_t cairo_format;
    if (format == WL_SHM_FORMAT_XRGB8888) {
        cairo_format = CAIRO_FORMAT_ARGB32;
    } else {
        fprintf(stderr, "bad format\n");
        return -1;
    }

    stride = cairo_format_stride_for_width(cairo_format, width);
	size = stride * height;

	fd = os_create_anonymous_file(size);
    assert(fd >= 0, "anon file");
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %s\n",
			size, strerror(errno));
		return -1;
	}

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	pool = wl_shm_create_pool(display->wl_shm, fd, size);
	buffer->buffer = wl_shm_pool_create_buffer(pool, 0,
						   width, height,
						   stride, format);
	wl_buffer_add_listener(buffer->buffer, &wl_buffer_listener, buffer);
	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->shm_data = data;
    buffer->cairo_surface = cairo_image_surface_create_for_data(
        data,
        cairo_format,
        width,
        height,
        stride
    );

	return 0;
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
    // fprintf(stderr, "xdg_toplevel_configure: %dx%d\n", width, height);
}

static void
handle_xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	handle_xdg_toplevel_configure,
	handle_xdg_toplevel_close,
};

static struct window *
create_window(struct display *display, int width, int height)
{
	struct window *window;

	window = malloc(sizeof *window);
    assert(window, "oom");

	window->callback = NULL;
	window->display = display;
	window->width = width;
	window->height = height;
	window->surface = wl_compositor_create_surface(display->compositor);
    window->buffers[0] = (struct buffer){ NULL, NULL, 0 };
    window->buffers[1] = (struct buffer){ NULL, NULL, 0 };

    window->xdg_surface = xdg_wm_base_get_xdg_surface(display->wm_base,
            window->surface);
    assert(window->xdg_surface, "xdg_surface");
    xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener,
            window);

    window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
    assert(window->xdg_toplevel, "xdg_toplevel");
    xdg_toplevel_add_listener(window->xdg_toplevel, &xdg_toplevel_listener,
            window);

    xdg_toplevel_set_title(window->xdg_toplevel, "simple-shm");
    wl_surface_commit(window->surface);
    window->wait_for_configure = true;

	return window;
}

static void
destroy_window(struct window *window)
{
    if (window->callback) wl_callback_destroy(window->callback);
    if (window->buffers[0].buffer)
        wl_buffer_destroy(window->buffers[0].buffer);
    if (window->buffers[1].buffer)
        wl_buffer_destroy(window->buffers[1].buffer);
    if (window->xdg_toplevel) xdg_toplevel_destroy(window->xdg_toplevel);
    if (window->xdg_surface) xdg_surface_destroy(window->xdg_surface);
	wl_surface_destroy(window->surface);
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

static void
paint_pixels(void *image, int padding, int width, int height, uint32_t time)
{
	const int halfh = padding + (height - padding * 2) / 2;
	const int halfw = padding + (width  - padding * 2) / 2;
	int ir, or;
	uint32_t *pixel = image;
	int y;

	/* squared radii thresholds */
	or = (halfw < halfh ? halfw : halfh) - 8;
	ir = or - 32;
	or *= or;
	ir *= ir;

	pixel += padding * width;
	for (y = padding; y < height - padding; y++) {
		int x;
		int y2 = (y - halfh) * (y - halfh);

		pixel += padding;
		for (x = padding; x < width - padding; x++) {
			uint32_t v;

			/* squared distance from center */
			int r2 = (x - halfw) * (x - halfw) + y2;

			if (r2 < ir)
				v = (r2 / 32 + time / 64) * 0x0080401;
			else if (r2 < or)
				v = (y + time / 32) * 0x0080401;
			else
				v = (x + time / 16) * 0x0080401;
			v &= 0x00ffffff;

			/* cross if compositor uses X from XRGB as alpha */
			if (abs(x - y) > 6 && abs(x + y - height) > 6)
				v |= 0xff000000;

			*pixel++ = v;
		}

		pixel += padding;
	}
}

typedef struct {
    int x, y, w, h;
} box;

void draw_text(cairo_t *cr, int x, int y, char *text) {
    cairo_text_extents_t ex;
    cairo_text_extents(cr, text, &ex);
    cairo_move_to(cr, x - ex.width/2, y + ex.height/2);
    cairo_show_text(cr, text);
}

void paint_sidebar(cairo_t *cr, box b) {
    cairo_translate(cr, b.x, b.y);
    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    int n_cal = 5;
    for (int i = 0; i < n_cal; i++) {
        cairo_set_source_rgba(cr, 255*(i%2), 255*((i+1)%2), 0, 255);
        cairo_rectangle(cr, 0, i*20, b.w, 20);
        cairo_fill(cr);
    }

    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    for(int i = 1; i < n_cal; i++) {
        cairo_move_to(cr, 0, i*20);
        cairo_line_to(cr, b.w, i*20);
    }
    cairo_stroke(cr);

    char buf[64];
    for (int i = 0; i < n_cal; i++) {
        snprintf(buf, 64, "Calendar %d", i+1);
        draw_text(cr, b.w/2, i*20+10, buf);
    }

    cairo_set_source_rgba(cr, .3, .3, .3, 1);
    cairo_move_to(cr, 0, n_cal * 20 + 30);
    pango_printf(cr, "Monospace 8", 1.0, b.w, b.h, "%s", 
            "Usage:\r"
            " n: next\r"
            " p: previous\r"
            " t: today");


    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    cairo_move_to(cr, b.w, 0);
    cairo_line_to(cr, b.w, b.h);
    cairo_stroke(cr);
    cairo_identity_matrix(cr);
}

void paint_header(cairo_t *cr, box b, time_t base) {
    cairo_translate(cr, b.x, b.y);

    cairo_set_source_rgba(cr, 0, 0, 0, 255);

    cairo_move_to(cr, 0, b.h);
    cairo_line_to(cr, b.w, b.h);
    cairo_stroke(cr);

    int n = 7;
    int sw = b.w / n;
    for (int i = 1; i < n; i++) {
        cairo_move_to(cr, sw*i, 0);
        cairo_line_to(cr, sw*i, b.h);
    }
    cairo_stroke(cr);

    char *days[] = { "H", "K", "Sze", "Cs", "P", "Szo", "V" };
    char buf[64];
    for (int i = 0; i < n; i++) {
        time_t time_off = base + 3600 * 24 * i;
        struct tm *t = gmtime(&time_off);
        snprintf(buf, 64, "%s: %d-%d",
                days[(t->tm_wday+6)%7], t->tm_mon+1, t->tm_mday);
        draw_text(cr, i*sw+sw/2, b.h/2, buf);
    }

    cairo_identity_matrix(cr);
}

static int day_sec(struct tm t) {
    return 3600 * t.tm_hour + 60 * t.tm_min + t.tm_sec;
}

static time_t week_base() {
    time_t now = time(NULL);
    struct tm t = *gmtime(&now);
    t.tm_sec = t.tm_min = t.tm_hour = 0;
    t.tm_mday -= (t.tm_wday-1)%7;
    return mktime(&t);
}

static void cairo_set_source_argb(cairo_t *cr, uint32_t c){
    cairo_set_source_rgba(cr,
            ((c >> 16) & 0xFF) / 255.0,
            ((c >> 8) & 0xFF) / 255.0,
            (c & 0xFF) / 255.0,
            1.0);
}

void paint_event(cairo_t *cr, struct event *ev, time_t base, box b) {
    time_t diff = ev->start.timestamp - base;
    if (diff > 3600 * 24 * 7 || diff < 0) return;

    int start_sec = day_sec(ev->start.time);
    int end_sec = day_sec(ev->end.time);
    int day_sec = 24 * 3600;
    int day_i = diff / (3600 * 24);

    int sw = b.w / 7;
    int x = sw * day_i;
    int y = b.h * start_sec / day_sec;
    int w = sw;
    int h = b.h * (end_sec - start_sec) / day_sec;

    if (ev->color) cairo_set_source_argb(cr, ev->color);
    else cairo_set_source_rgba(cr, 0, 255, 0, 255);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    cairo_move_to(cr, x, y);
    pango_printf(cr, "Monospace 8", 1.0, w, h, "%s", ev->summary);
}

void paint_calendar(cairo_t *cr, box b, time_t base) {
    cairo_translate(cr, b.x, b.y);

    int n = 7;
    int sw = b.w / n;

    for (int i = 0; i < n_cal; i++) {
        struct event *ev = cal[i].events;
        while (ev) {
            paint_event(cr, ev, base, b);
            ev = ev->next;
        }
    }

    time_t now = time(NULL);
    struct tm t = *gmtime(&now);
    int now_sec = day_sec(t);
    int day_sec = 24 * 3600;
    int day_i = (now - base) / day_sec;
    int y = b.h * now_sec / day_sec;
    cairo_move_to(cr, day_i * sw, y);
    cairo_line_to(cr, (day_i+1) * sw, y);
    cairo_set_source_rgba(cr, 255, 0, 0, 255);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 0, 0, 0, 255);
    for (int i = 1; i < n; i++) {
        cairo_move_to(cr, sw*i, 0);
        cairo_line_to(cr, sw*i, b.h);
    }
    cairo_stroke(cr);

    cairo_identity_matrix(cr);
}

void
paint(struct window *window, cairo_surface_t *cairo_surface) {
    cairo_t *cr = cairo_create(cairo_surface);
    int w = window->width, h = window->height;

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_set_line_width(cr, 2);

	cairo_set_source_rgba(cr, 255, 255, 255, 255);
	cairo_paint(cr);

    time_t base = week_base() + week_offset * 7 * 24 * 3600;
    paint_calendar(cr,  (box){ 120, 60, w-120, h-60 }, base);
    paint_sidebar(cr,   (box){ 0, 60, 120, h-60 });
    paint_header(cr,    (box){ 120, 0, w-120, 60 }, base);

    cairo_destroy(cr);
}

static const struct wl_callback_listener frame_listener;
static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	struct buffer *buffer;

	buffer = window_next_buffer(window);
    assert(buffer, !callback ? "Failed to create the first buffer.\n" :
            "Both buffers busy at redraw(). Server bug?\n");

	// paint_pixels(buffer->shm_data, 20, window->width, window->height, time);
    paint(window, buffer->cairo_surface);

	wl_surface_attach(window->surface, buffer->buffer, 0, 0);
	wl_surface_damage(window->surface,
			  20, 20, window->width - 40, window->height - 40);

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

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t _key_state) {
    enum wl_keyboard_key_state key_state = _key_state;
    if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        fprintf(stderr, "pressed: %d\n", key);
        if (key == 49) // n
            week_offset++;
        if (key == 25) // p
            week_offset--;
        if (key == 20) // t
            week_offset = 0;
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
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

// wl_seat_listener

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct display *d = data;
	if (d->keyboard) {
		wl_keyboard_release(d->keyboard);
		d->keyboard = NULL;
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		d->keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(d->keyboard, &keyboard_listener, NULL);
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
        fprintf(stderr, "reg: %s\n", interface);
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

static struct display *
create_display()
{
	struct display *display;

	display = malloc(sizeof *display);
    assert(display, "oom");
	display->display = wl_display_connect(NULL);
	assert(display->display, "wl_display_connect");

    display->wl_seat = NULL;
    display->keyboard = NULL;

	display->registry = wl_display_get_registry(display->display);
    wl_registry_add_listener(display->registry, &wl_registry_listener, display);
	wl_display_roundtrip(display->display);
    assert(display->wl_shm, "wl_shm");
    assert(display->wm_base, "xdg_wm_base");
    assert(display->compositor, "wl_compositor");

	wl_display_roundtrip(display->display);
    return display;
}

static void
destroy_display(struct display *display)
{
    if (display->wl_shm) wl_shm_destroy(display->wl_shm);
    if (display->wm_base) xdg_wm_base_destroy(display->wm_base);
    if (display->compositor) wl_compositor_destroy(display->compositor);
	wl_registry_destroy(display->registry);
	wl_display_flush(display->display);
	wl_display_disconnect(display->display);
    free(display);
}

int
main(int argc, char **argv)
{
    setenv("TZ", "UTC", 1); // fucking C time handling...
    for (int i = 0; i < argc; i++) {
        cal[i].events = NULL;
        FILE *f = fopen(argv[i], "rb");
        parse_ics(f, cal+i);
        if (++n_cal >= 16) break;
    }

    int ret = 0;
    struct display *display;
	struct window *window;
    display = create_display();
    window = create_window(display, 600, 900);
    assert(window, "window");
    wl_surface_damage(window->surface, 0, 0, window->width, window->height);
    
    if (!window->wait_for_configure) redraw(window, NULL, 0);
    while (running && ret != -1) ret = wl_display_dispatch(display->display);
	fprintf(stderr, "simple-shm exiting\n");

	destroy_window(window);
	destroy_display(display);
    return 0;
}
