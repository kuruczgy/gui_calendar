#ifndef _PANGO_H_
#define _PANGO_H_
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

struct layout_params {
    const char *text;
    double scale;
    bool hyphens;
    int width, height;
};

struct text_renderer {
    PangoFontDescription *desc;
    struct layout_params p;
};

struct text_renderer* text_renderer_new(const char *font);
char* text_format(const char *fmt, ...);
void text_get_size(cairo_t *cr, struct text_renderer *tr, const char *text);
void text_print_free(cairo_t *cr, struct text_renderer *tr, char *text);
void text_print_own(cairo_t *cr, struct text_renderer *tr, const char *text);

#endif
