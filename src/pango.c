#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pango.h"
#include "util.h"

struct text_renderer* text_renderer_new(const char *font) {
    struct text_renderer *tr = malloc(sizeof(struct text_renderer));
    tr->desc = pango_font_description_from_string(font);
    tr->p.scale = 1.0;
    tr->p.hyphens = false;
    tr->p.wrap_char = true;
    return tr;
}

void text_renderer_free(struct text_renderer *tr) {
    pango_font_description_free(tr->desc);
    free(tr);
}

static PangoLayout *create_pango_layout(cairo_t *cr, struct text_renderer *tr) {
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoAttrList *a = pango_attr_list_new();
    assert(tr->p.text, "text");
    pango_layout_set_text(l, tr->p.text, -1);

    pango_attr_list_insert(a, pango_attr_scale_new(tr->p.scale));
    pango_attr_list_insert(a, pango_attr_insert_hyphens_new(tr->p.hyphens));
    pango_layout_set_attributes(l, a);
    pango_attr_list_unref(a);

    pango_layout_set_font_description(l, tr->desc);

    pango_layout_set_wrap(l,
        tr->p.wrap_char ? PANGO_WRAP_CHAR : PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);

    pango_layout_set_width(l, tr->p.width * PANGO_SCALE);
    pango_layout_set_height(l, tr->p.height * PANGO_SCALE);

    /* pango_layout_set_alignment(l, tr->p.center ?
            PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT); */

    /* cairo_get_font_options(cairo, fo);
    pango_cairo_context_set_font_options(pango_layout_get_context(layout), fo);
    cairo_font_options_destroy(fo); */
    pango_cairo_update_layout(cr, l);

    return l;
}

void text_get_size(cairo_t *cr, struct text_renderer *tr, const char *text) {
    tr->p.text = text;
    PangoLayout *l = create_pango_layout(cr, tr);
    pango_layout_get_pixel_size(l, &tr->p.width, &tr->p.height);
    g_object_unref(l);
}

void text_print_free(cairo_t *cr, struct text_renderer *tr, char *text) {
    text_print_own(cr, tr, text);
    free(text);
}
void text_print_own(cairo_t *cr, struct text_renderer *tr, const char *text) {
    tr->p.text = text;
    PangoLayout *l = create_pango_layout(cr, tr);
    pango_layout_get_pixel_size(l, &tr->p.width, &tr->p.height);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    tr->p.text = NULL;
}

char* text_format(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    // Add one since vsnprintf excludes null terminator.
    int length = vsnprintf(NULL, 0, fmt, args) + 1;
    va_end(args);

    char *buf = malloc(length);
    assert(buf, "malloc error");
    va_start(args, fmt);
    vsnprintf(buf, length, fmt, args);
    va_end(args);

    return buf;
}
