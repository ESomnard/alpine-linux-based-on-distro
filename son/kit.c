/* Panneau Plume — dessin HUD commun (repris de plume-wifi). */
#include "panel.h"
#include "theme.h"
#include "fenetre.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FONT "JetBrains Mono, DejaVu Sans Mono, WenQuanYi Zen Hei"

double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

void rgba(cairo_t *cr, uint32_t c, double a)
{
	cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
}

void rrect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_close_path(cr);
}

void glow_stroke(cairo_t *cr, uint32_t col, double lw, double k)
{
	static const double wd[] = { 7, 4, 2 };
	for (int i = 0; i < 3; i++) {
		rgba(cr, col, 0.07 * k * (i + 1));
		cairo_set_line_width(cr, lw + wd[i]);
		cairo_stroke_preserve(cr);
	}
	rgba(cr, col, 1);
	cairo_set_line_width(cr, lw);
	cairo_stroke(cr);
}

double txt(cairo_t *cr, double px, int weight, double ls, uint32_t col, double a,
		  double x, double cy, int align, double maxw, const char *s)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_font_description_set_weight(fd, weight);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	if (ls) {
		PangoAttrList *al = pango_attr_list_new();
		pango_attr_list_insert(al, pango_attr_letter_spacing_new((int)(ls * px * PANGO_SCALE)));
		pango_layout_set_attributes(l, al);
		pango_attr_list_unref(al);
	}
	pango_layout_set_text(l, s, -1);
	if (maxw > 0) {
		pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
		pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_MIDDLE);
	}
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	double tx = align == 0 ? x : align == 1 ? x - w / 2.0 : x - w;
	cairo_move_to(cr, tx, cy - h / 2.0);
	rgba(cr, col, a);
	pango_cairo_show_layout(cr, l);
	g_object_unref(l);
	return w;
}

bool hover(struct rect r)
{
	return A.px >= r.x && A.px <= r.x + r.w && A.py >= r.y && A.py <= r.y + r.h;
}

double list_top, list_bot = 1e9;
void kit_clip(double top, double bot) { list_top = top; list_bot = bot; }

void add_hit(struct rect r, int act, int arg, bool clipped)
{
	if (A.nhits >= MAX_HITS)
		return;
	if (clipped) {
		double y0 = fmax(r.y, list_top), y1 = fmin(r.y + r.h, list_bot);
		if (y1 <= y0)
			return;
		r.y = y0;
		r.h = y1 - y0;
	}
	A.hits[A.nhits++] = (struct hit){ r, act, arg };
}

/* bouton HUD : cadre lumineux au survol */
void button(cairo_t *cr, struct rect r, const char *label, bool on, bool danger, int act, int arg, bool clipped)
{
	bool h = hover(r);
	uint32_t c = danger ? C_ERR : C_LINE;
	rrect(cr, r.x, r.y, r.w, r.h, 3);
	rgba(cr, on ? c : C_PANEL, on ? 0.22 : h ? 1 : 0.85);
	cairo_fill_preserve(cr);
	if (h || on)
		glow_stroke(cr, c, 1.2, 0.8);
	else {
		rgba(cr, danger ? C_ERR : C_LINEDIM, 1);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
	}
	txt(cr, 12, 700, 0.12, h || on ? C_BRIGHT : danger ? C_ERR : C_TEXT, 1, r.x + r.w / 2, r.y + r.h / 2, 1, 0, label);
	add_hit(r, act, arg, clipped);
}

/* barres de signal : 4 niveaux d'après les dBm */



void tag(cairo_t *cr, double *x, double cy, const char *s, uint32_t col)
{
	double w = txt(cr, 10, 700, 0.12, col, 0, 0, -100, 0, 0, s) + 14; /* mesure (dessin invisible) */
	*x -= w;
	rrect(cr, *x, cy - 9, w, 18, 2);
	rgba(cr, col, 0.12);
	cairo_fill_preserve(cr);
	rgba(cr, col, 0.7);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	txt(cr, 10, 700, 0.12, col, 1, *x + w / 2, cy, 1, 0, s);
	*x -= 6;
}

void spinner(cairo_t *cr, double cx, double cy, double r)
{
	double a = fmod(now_s() * 5, 2 * M_PI);
	cairo_new_path(cr);
	cairo_arc(cr, cx, cy, r, a, a + 4.2);
	rgba(cr, C_LINE, 1);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);
}

void message(cairo_t *cr, double W, double y, const char *t1, const char *t2)
{
	txt(cr, 15, 700, 0.12, C_TEXT, 1, W / 2, y, 1, W - 2 * MARGIN, t1);
	if (t2)
		txt(cr, 12, 400, 0, C_MUTED, 1, W / 2, y + 26, 1, W - 2 * MARGIN, t2);
}


void header(cairo_t *cr, double W, const char *title, const char *sub)
{
	cairo_rectangle(cr, 0, 0, W, HEADER_H);
	rgba(cr, C_PANEL, 1);
	cairo_fill(cr);
	cairo_move_to(cr, 0, HEADER_H - 0.5);
	cairo_line_to(cr, W, HEADER_H - 0.5);
	glow_stroke(cr, C_LINE, 1, 0.6);
	double w = txt(cr, 20, 800, 0.2, C_BRIGHT, 1, MARGIN + PF_LARGEUR, HEADER_H / 2, 0, 0, title);
	if (sub && *sub)
		txt(cr, 11, 400, 0.1, C_MUTED, 1, MARGIN + PF_LARGEUR + w + 14, HEADER_H / 2 + 2, 0, 0, sub);
}

void statusbar(cairo_t *cr, double W, double H, const char *hint)
{
	cairo_rectangle(cr, 0, H - STATUS_H, W, STATUS_H);
	rgba(cr, C_PANEL, 1);
	cairo_fill(cr);
	cairo_move_to(cr, 0, H - STATUS_H + 0.5);
	cairo_line_to(cr, W, H - STATUS_H + 0.5);
	rgba(cr, C_LINEDIM, 1);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	if (A.toast[0] && now_s() - A.toast_t < 5.5)
		txt(cr, 12, 600, 0.04, A.toast_err ? C_ERR : C_OK, 1, MARGIN, H - STATUS_H / 2, 0, W - 2 * MARGIN, A.toast);
	else
		txt(cr, 11, 400, 0.06, C_MUTED, 1, MARGIN, H - STATUS_H / 2, 0, W - 2 * MARGIN, hint);
}

int hit_at(double x, double y, int *arg)
{
	for (int k = A.nhits - 1; k >= 0; k--) {
		struct hit *h = &A.hits[k];
		if (x >= h->r.x && x <= h->r.x + h->r.w && y >= h->r.y && y <= h->r.y + h->r.h) {
			if (arg)
				*arg = h->arg;
			return h->act;
		}
	}
	return 0;
}
