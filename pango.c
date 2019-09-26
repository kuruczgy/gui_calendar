#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cairo.h"

/* adopted from sway (MIT license) */

PangoLayout *get_pango_layout(cairo_t *cairo, const char *font,
		const char *text, double scale) {
	PangoLayout *layout = pango_cairo_create_layout(cairo);
	PangoAttrList *attrs = pango_attr_list_new();
    pango_layout_set_text(layout, text, -1);

	pango_attr_list_insert(attrs, pango_attr_scale_new(scale));
    pango_attr_list_insert(attrs, pango_attr_insert_hyphens_new(false));
	PangoFontDescription *desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	// pango_layout_set_single_paragraph_mode(layout, 1);
    pango_layout_set_wrap(layout, PANGO_WRAP_CHAR);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
	pango_layout_set_attributes(layout, attrs);
	pango_attr_list_unref(attrs);
	pango_font_description_free(desc);
	return layout;
}

void get_text_size(cairo_t *cairo, const char *font, int width, int *height,
        double scale, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	// Add one since vsnprintf excludes null terminator.
	int length = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	char *buf = malloc(length);
	if (buf == NULL) {
        // error... lol
		return;
	}
	va_start(args, fmt);
	vsnprintf(buf, length, fmt, args);
	va_end(args);

	PangoLayout *layout = get_pango_layout(cairo, font, buf, scale);
    pango_layout_set_width(layout, width * PANGO_SCALE);
	pango_cairo_update_layout(cairo, layout);
	pango_layout_get_pixel_size(layout, &width, height);
	g_object_unref(layout);
	free(buf);
}

void pango_printf(cairo_t *cairo, const char *font,
		double scale, int width, int height, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	// Add one since vsnprintf excludes null terminator.
	int length = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	char *buf = malloc(length);
	if (buf == NULL) {
        // error... lol
		return;
	}
	va_start(args, fmt);
	vsnprintf(buf, length, fmt, args);
	va_end(args);

	PangoLayout *layout = get_pango_layout(cairo, font, buf, scale);
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_height(layout, height * PANGO_SCALE);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_get_font_options(cairo, fo);
	pango_cairo_context_set_font_options(pango_layout_get_context(layout), fo);
	cairo_font_options_destroy(fo);
	pango_cairo_update_layout(cairo, layout);
	pango_cairo_show_layout(cairo, layout);
	g_object_unref(layout);
	free(buf);
}
