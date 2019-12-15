#ifndef _BACKEND_H_
#define _BACKEND_H_
#include <stdbool.h>
#include <stdint.h>
#include <cairo.h>
#include <sys/types.h>

struct backend;

typedef bool (*paint_cb)(void *ud, cairo_t*);
typedef void (*keyboard_cb)(void *ud, uint32_t key, uint32_t mods);
typedef void (*child_cb)(void *ud, pid_t pid);

struct backend_methods {
    void (*destroy)(struct backend*);
    void (*run)(struct backend*);
    void (*get_window_size)(struct backend*, int *width, int *height);
    void (*set_callbacks)(struct backend*,
            paint_cb, keyboard_cb, child_cb, void*);
};

struct backend {
    struct backend_methods *vptr;
    void *self;
};

struct backend backend_init_wayland();
struct backend backend_init_dummy();
struct backend backend_init_fbdev();
#endif
