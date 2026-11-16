/* plume-lanceur — dessin : recherche, grille d'icônes en pages, points de page. */
#include "lanceur.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define C_BG      0x02060b
#define C_TEXT    0xcdeefc
#define C_MUTED   0x5f8ea5
#define C_BRIGHT  0xe9f9ff
#define FONT      "JetBrains Mono, DejaVu Sans Mono, WenQuanYi Zen Hei"

#define CELL_W 156.0
#define CELL_H 158.0
#define ICON   84

static const uint32_t BADGE_COLS[] = { 0x4fb8e0, 0x4fe0a4, 0xb48cff, 0xff8fb1, 0xe6c86e, 0x6fe3ff, 0x9fe4ff };

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static void rgba(cairo_t *cr, uint32_t c, double a)
{
	cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r)
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

static void glow_stroke(cairo_t *cr, uint32_t col, double lw, double k)
{
	static const double wd[] = { 8, 4.5, 2 };
	for (int i = 0; i < 3; i++) {
		rgba(cr, col, 0.07 * k * (i + 1));
		cairo_set_line_width(cr, lw + wd[i]);
		cairo_stroke_preserve(cr);
	}
	rgba(cr, col, 1);
	cairo_set_line_width(cr, lw);
	cairo_stroke(cr);
}

static PangoLayout *layout(cairo_t *cr, double px, int weight, const char *s)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_font_description_set_weight(fd, weight);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(l, s, -1);
	return l;
}

static double txt(cairo_t *cr, double px, int weight, uint32_t col, double a, double x, double cy, int align,
		  double maxw, const char *s)
{
	PangoLayout *l = layout(cr, px, weight, s);
	if (maxw > 0) {
		pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
		pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
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

/* nom sous l'icône : centré, deux lignes au plus */
static void label(cairo_t *cr, double cx, double y, double w, uint32_t col, double a, const char *s)
{
	PangoLayout *l = layout(cr, 13, PANGO_WEIGHT_NORMAL, s);
	pango_layout_set_width(l, (int)(w * PANGO_SCALE));
	pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
	pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_height(l, -2);
	pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
	cairo_move_to(cr, cx - w / 2, y);
	rgba(cr, col, a);
	pango_cairo_show_layout(cr, l);
	g_object_unref(l);
}

/* pastille HUD (initiales) quand l'appli n'a pas d'icône */
static void badge(cairo_t *cr, double x, double y, double s, const char *name, double a)
{
	unsigned h = 0;
	for (const unsigned char *p = (const unsigned char *)name; *p; p++)
		h = h * 31 + *p;
	uint32_t col = BADGE_COLS[h % (sizeof BADGE_COLS / sizeof BADGE_COLS[0])];
	char ini[4] = "";
	int k = 0;
	bool word = true;
	for (const unsigned char *p = (const unsigned char *)name; *p && k < 2; p++) {
		if (*p == ' ' || *p == '-') {
			word = true;
			continue;
		}
		if (word && *p < 0x80 && ((*p | 32) >= 'a' && (*p | 32) <= 'z')) {
			ini[k++] = (char)(*p >= 'a' ? *p - 32 : *p);
			ini[k] = '\0';
		}
		word = false;
	}
	if (!ini[0])
		snprintf(ini, sizeof ini, "%c", '*');
	rrect(cr, x, y, s, s, s * 0.24);
	cairo_pattern_t *g = cairo_pattern_create_linear(x, y, x + s, y + s);
	cairo_pattern_add_color_stop_rgba(g, 0, ((col >> 16) & 255) / 255.0, ((col >> 8) & 255) / 255.0, (col & 255) / 255.0, 0.35 * a);
	cairo_pattern_add_color_stop_rgba(g, 1, ((col >> 16) & 255) / 255.0, ((col >> 8) & 255) / 255.0, (col & 255) / 255.0, 0.08 * a);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	glow_stroke(cr, col, 1.3, 0.5 * a);
	txt(cr, s * 0.36, PANGO_WEIGHT_BOLD, C_BRIGHT, a, x + s / 2, y + s / 2, 1, 0, ini);
}

static bool hovering(struct rect r)
{
	return L.px >= r.x && L.px <= r.x + r.w && L.py >= r.y && L.py <= r.y + r.h;
}

void draw(cairo_t *cr, int W, int H)
{
	double t = now_s() - L.start;
	double a = clampd(t / 0.16, 0, 1); /* apparition en fondu */
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	rgba(cr, C_BG, 0.88 * a);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_pattern_t *g = cairo_pattern_create_radial(W / 2.0, H * 0.25, 0, W / 2.0, H * 0.25, W * 0.7);
	cairo_pattern_add_color_stop_rgba(g, 0, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0.16 * a);
	cairo_pattern_add_color_stop_rgba(g, 1, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	/* le contenu glisse un peu vers le bas en apparaissant */
	cairo_translate(cr, 0, (1 - a) * -14);

	/* recherche */
	double sw = fmin(560, W - 80), sh = 46, sx = (W - sw) / 2, sy = 52;
	rrect(cr, sx, sy, sw, sh, sh / 2);
	cairo_set_source_rgba(cr, 2 / 255.0, 10 / 255.0, 18 / 255.0, 0.95 * a);
	cairo_fill_preserve(cr);
	glow_stroke(cr, L.query[0] ? C_LINE : C_LINEDIM, 1.3, (L.query[0] ? 0.9 : 0.4) * a);
	double qx = sx + 22;
	qx += txt(cr, 18, PANGO_WEIGHT_NORMAL, C_LINE, a, qx, sy + sh / 2, 0, 0, "⌕") + 12;
	double qw = 0;
	if (L.query[0])
		qw = txt(cr, 16, PANGO_WEIGHT_NORMAL, C_BRIGHT, a, qx, sy + sh / 2, 0, sw - (qx - sx) - 24, L.query);
	else
		txt(cr, 15, PANGO_WEIGHT_NORMAL, C_MUTED, 0.85 * a, qx, sy + sh / 2, 0, sw - (qx - sx) - 24,
		    "Rechercher une application…");
	rgba(cr, C_LINE, 0.9 * a);
	cairo_rectangle(cr, qx + qw + 2, sy + 13, 1.5, sh - 26);
	cairo_fill(cr);
	char count[64];
	snprintf(count, sizeof count, L.query[0] ? "%d résultat%s" : "%d application%s", L.nview, L.nview > 1 ? "s" : "");
	txt(cr, 12, PANGO_WEIGHT_NORMAL, C_MUTED, a, W / 2.0, sy + sh + 20, 1, 0, count);

	/* grille */
	double top = sy + sh + 50, bottom = H - 90;
	int cols = (int)clampd(floor((W - 120) / CELL_W), 1, 8);
	int rows = (int)fmax(1, floor((bottom - top) / CELL_H));
	L.cols = cols;
	L.per_page = cols * rows;
	L.npages = L.nview ? (L.nview + L.per_page - 1) / L.per_page : 1;
	L.page = (int)clampd(L.page, 0, L.npages - 1);
	double gx = (W - cols * CELL_W) / 2, gy = top + ((bottom - top) - rows * CELL_H) / 2;
	L.ncells = 0;
	if (!L.nview) {
		txt(cr, 16, PANGO_WEIGHT_NORMAL, C_MUTED, a, W / 2.0, (top + bottom) / 2, 1, 0,
		    L.napps ? "Aucune application ne correspond" : "Aucune application trouvée");
	}
	int first = L.page * L.per_page;
	for (int k = 0; k < L.per_page && first + k < L.nview; k++) {
		int vi = first + k;
		struct app_entry *ap = &L.apps[L.view[vi]];
		double cx = gx + (k % cols) * CELL_W, cy = gy + (k / cols) * CELL_H;
		struct rect r = { cx + 6, cy + 4, CELL_W - 12, CELL_H - 8 };
		L.r_cells[L.ncells] = r;
		L.cell_idx[L.ncells++] = vi;
		bool sel = vi == L.sel, hov = hovering(r);
		if (sel || hov) {
			rrect(cr, r.x, r.y, r.w, r.h, 16);
			rgba(cr, C_LINE, (sel ? 0.14 : 0.07) * a);
			cairo_fill_preserve(cr);
			if (sel)
				glow_stroke(cr, C_LINE, 1.2, 0.6 * a);
			else {
				rgba(cr, C_LINEDIM, a);
				cairo_set_line_width(cr, 1);
				cairo_stroke(cr);
			}
		}
		double ix = cx + (CELL_W - ICON) / 2, iy = cy + 16;
		cairo_surface_t *ic = app_icon(ap, ICON);
		if (ic) {
			cairo_set_source_surface(cr, ic, ix, iy);
			cairo_paint_with_alpha(cr, a);
		} else {
			badge(cr, ix + 6, iy + 6, ICON - 12, ap->name, a);
		}
		label(cr, cx + CELL_W / 2, iy + ICON + 10, CELL_W - 18, sel ? C_BRIGHT : C_TEXT, a, ap->name);
	}

	/* points de page */
	L.ndots = 0;
	if (L.npages > 1) {
		int nd = L.npages > 64 ? 64 : L.npages;
		double dx = W / 2.0 - (nd - 1) * 11.0, dy = H - 52;
		for (int i = 0; i < nd; i++) {
			double x = dx + i * 22;
			struct rect r = { x - 9, dy - 9, 18, 18 };
			L.r_dots[L.ndots++] = r;
			cairo_new_sub_path(cr);
			cairo_arc(cr, x, dy, i == L.page ? 5 : 4, 0, 2 * M_PI);
			if (i == L.page) {
				rgba(cr, C_LINE, a);
				cairo_fill_preserve(cr);
				glow_stroke(cr, C_LINE, 1, 0.6 * a);
			} else {
				rgba(cr, hovering(r) ? C_LINE : C_LINEDIM, a);
				cairo_fill(cr);
			}
		}
	}
	txt(cr, 11, PANGO_WEIGHT_NORMAL, C_LINEDIM, a, W - 30, H - 24, 2, 0,
	    "tape pour chercher · ⏎ lancer · ← → ↑ ↓ choisir · Page↑↓ pages · Échap fermer");
}
