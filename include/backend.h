#ifndef GUI_CALENDAR_BACKEND_H
#define GUI_CALENDAR_BACKEND_H
#include <stdbool.h>
#include <stdint.h>
#include <cairo.h>
#include <sys/types.h>

enum mgu_input_ev {
	MGU_DOWN = 1 << 0,
	MGU_UP = 1 << 1,
	MGU_MOVE = 1 << 2,
	MGU_TOUCH = 1 << 3,
	MGU_POINTER = 1 << 4,
};
struct mgu_input_event_args {
	enum mgu_input_ev t;
	uint32_t time;
	union {
		struct {
			int id;
			union {
				struct {
					double p[2];
				} down_or_move;
				struct { } up;
			};
		} touch;
		union {
			struct {
				double p[2];
			} move;
		} pointer;
	};
};

struct backend;

typedef bool (*paint_cb)(void *ud, cairo_t*);
typedef void (*keyboard_cb)(void *ud, uint32_t key, uint32_t mods);
typedef void (*child_cb)(void *ud, pid_t pid);
typedef void (*input_cb)(void *ud, struct mgu_input_event_args ev);

struct backend_methods {
	void (*destroy)(struct backend*);
	void (*run)(struct backend*);
	void (*get_window_size)(struct backend*, int *width, int *height);
	void (*set_callbacks)(struct backend*,
			paint_cb, keyboard_cb, child_cb, input_cb, void*);
	bool (*is_interactive)(struct backend*);
};

struct backend {
	struct backend_methods *vptr;
	void *self;
};

struct backend backend_init_wayland();
struct backend backend_init_dummy();
struct backend backend_init_fbdev();
struct backend backend_init_svg(const char *filename, int width, int height);
struct backend backend_init_image(const char *filename, int width, int height);
#endif
