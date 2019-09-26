#ifndef _PANGO_H_
#define _PANGO_H_
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

PangoLayout *get_pango_layout(cairo_t *cairo, const char *font,
		const char *text, double scale);
void get_text_size(cairo_t *cairo, const char *font, int width, int *height,
        double scale, const char *fmt, ...);
void pango_printf(cairo_t *cairo, const char *font,
		double scale, int width, int height, const char *fmt, ...);

#endif
