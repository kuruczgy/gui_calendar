#ifndef _PANGO_H_
#define _PANGO_H_
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

/**
 * Utility function which escape characters a & < > ' ".
 *
 * The function returns the length of the escaped string, optionally writing the
 * escaped string to dest if provided.
 */
PangoLayout *get_pango_layout(cairo_t *cairo, const char *font,
		const char *text, double scale);
void get_text_size(cairo_t *cairo, const char *font, int *width, int *height,
		int *baseline, double scale, const char *fmt, ...);
void pango_printf(cairo_t *cairo, const char *font,
		double scale, int width, int height, const char *fmt, ...);

#endif
