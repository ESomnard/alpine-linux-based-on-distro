/* plume-fonds — dessin (aperçu du bureau, miniatures) et clics. */
#include "fonds.h"
#include "theme.h"
#include "fenetre.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define C_BG      0x02060b
#define C_PANEL   0x07121c
#define C_TEXT    0xcdeefc
#define C_MUTED   0x5f8ea5
#define C_OK      0x4fe0a4
#define C_ERR     0xff5470
#define C_BRIGHT  0xe9f9ff
#define FONT      "JetBrains Mono, DejaVu Sans Mono, WenQuanYi Zen Hei"

#define HEADER_H 64.0
#define STATUS_H 40.0
#define MARGIN   24.0

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

static double txt(cairo_t *cr, double px, int weight, double ls, uint32_t col, double a,
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

static const struct { const char *name, *label; uint32_t accent; } THEMES[] = {
	{ "cyan", "Cyan", 0x4fb8e0 }, { "emeraude", "Émeraude", 0x4fe0a4 }, { "violet", "Violet", 0xb48cff },
	{ "ambre", "Ambre", 0xe6b450 }, { "rouge", "Rouge", 0xff5470 }, { "rose", "Rose", 0xff8fb1 },
};
#define NTHEMES (int)(sizeof THEMES / sizeof THEMES[0])

/* plume-theme NOM : réécrit les configs et recharge sway */
static void apply_theme(int t)
{
	char cmd[128];
	snprintf(cmd, sizeof cmd, "plume-theme %s >/dev/null 2>&1", THEMES[t].name);
	int rc = system(cmd);
	plume_theme_reset();
	if (rc == 0)
		set_toast(false, "Thème : %s", THEMES[t].label);
	else
		set_toast(true, "Impossible d'appliquer le thème (plume-theme manquant ?)");
}

static bool hover(struct rect r)
{
	return A.px >= r.x && A.px <= r.x + r.w && A.py >= r.y && A.py <= r.y + r.h;
}

static struct rect clip;

static void add_hit(struct rect r, int act, int arg, bool clipped)
{
	if (A.nhits >= MAX_HITS)
		return;
	if (clipped) {
		double y0 = fmax(r.y, clip.y), y1 = fmin(r.y + r.h, clip.y + clip.h);
		if (y1 <= y0)
			return;
		r.y = y0;
		r.h = y1 - y0;
	}
	A.hits[A.nhits++] = (struct hit){ r, act, arg };
}

static double section(cairo_t *cr, double x, double y, double w, const char *title, const char *right)
{
	double tw = txt(cr, 13, PANGO_WEIGHT_SEMIBOLD, 0.3, C_LINE, 1, x, y + 10, 0, 0, title);
	double rw = right ? txt(cr, 12, PANGO_WEIGHT_NORMAL, 0.05, C_MUTED, 1, x + w, y + 10, 2, 0, right) : 0;
	double lx = x + tw + 12, lx2 = x + w - (rw ? rw + 12 : 0);
	if (lx2 > lx) {
		rgba(cr, C_LINEDIM, 0.8);
		cairo_rectangle(cr, lx, y + 10, lx2 - lx, 1);
		cairo_fill(cr);
	}
	return 34;
}

/* image dessinée dans un rectangle selon le mode (remplir, ajuster…) */
static void paint_mode(cairo_t *cr, cairo_surface_t *img, struct rect r, enum mode m, double scale_hint)
{
	int iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
	cairo_save(cr);
	cairo_rectangle(cr, r.x, r.y, r.w, r.h);
	cairo_clip(cr);
	if (m == M_TILE) {
		/* mosaïque : l'image à sa taille réelle, réduite comme l'écran */
		double sc = scale_hint;
		cairo_translate(cr, r.x, r.y);
		cairo_scale(cr, sc, sc);
		cairo_pattern_t *p = cairo_pattern_create_for_surface(img);
		cairo_pattern_set_extend(p, CAIRO_EXTEND_REPEAT);
		cairo_set_source(cr, p);
		cairo_paint(cr);
		cairo_pattern_destroy(p);
	} else {
		double sc = m == M_FILL ? fmax(r.w / iw, r.h / ih) : m == M_FIT ? fmin(r.w / iw, r.h / ih) : scale_hint;
		cairo_translate(cr, r.x + (r.w - iw * sc) / 2, r.y + (r.h - ih * sc) / 2);
		cairo_scale(cr, sc, sc);
		cairo_set_source_surface(cr, img, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
	}
	cairo_restore(cr);
}

/* ------------------------------ aperçu ------------------------------- */

static void preview(cairo_t *cr, struct rect r)
{
	rrect(cr, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 12);
	glow_stroke(cr, C_LINE, 1.2, 0.6);
	cairo_save(cr);
	rrect(cr, r.x, r.y, r.w, r.h, 10);
	cairo_clip(cr);
	rgba(cr, C_BG, 1);
	cairo_paint(cr);
	struct item *it = A.current >= 0 ? &A.items[A.current] : NULL;
	if (it && it->kind == K_COLOR) {
		rgba(cr, it->color, 1);
		cairo_paint(cr);
	} else if (it) {
		/* l'aperçu est chargé à la taille du cadre (au plus 2 fois pour la netteté) */
		if (A.preview_for != A.current) {
			if (A.preview)
				cairo_surface_destroy(A.preview);
			int lw = (int)(r.w * 2), lh = (int)(r.h * 2);
			A.preview = load_scaled(it->path, lw, lh, A.mode == M_FILL, &it->img_w, &it->img_h);
			A.preview_for = A.current;
		}
		if (A.preview) {
			/* centrer / mosaïque : taille réelle ramenée à un écran 1920 px de large */
			double screen_sc = r.w / 1920.0;
			double hint = screen_sc * (it->img_w > 0 ? (double)it->img_w / cairo_image_surface_get_width(A.preview) : 1);
			paint_mode(cr, A.preview, r, A.mode, hint);
		}
	}
	/* barre et dock de Plume, pour se rendre compte */
	rgba(cr, C_BG, 0.85);
	cairo_rectangle(cr, r.x, r.y, r.w, r.h * 0.045);
	cairo_fill(cr);
	rgba(cr, C_LINE, 0.9);
	cairo_rectangle(cr, r.x + r.w * 0.015, r.y + r.h * 0.016, r.w * 0.06, r.h * 0.014);
	cairo_fill(cr);
	rgba(cr, C_TEXT, 0.7);
	cairo_rectangle(cr, r.x + r.w * 0.86, r.y + r.h * 0.016, r.w * 0.12, r.h * 0.014);
	cairo_fill(cr);
	double dw = r.w * 0.34, dh = r.h * 0.085, dx = r.x + (r.w - dw) / 2, dy = r.y + r.h - dh - r.h * 0.03;
	rrect(cr, dx, dy, dw, dh, dh * 0.3);
	rgba(cr, C_BG, 0.85);
	cairo_fill_preserve(cr);
	rgba(cr, C_LINE, 0.8);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	for (int i = 0; i < 8; i++) {
		double s = dh * 0.6, ix = dx + dh * 0.25 + i * (dw - dh * 0.5) / 8;
		rrect(cr, ix, dy + (dh - s) / 2, s, s, s * 0.25);
		rgba(cr, C_LINE, 0.35);
		cairo_fill(cr);
	}
	cairo_restore(cr);
}

/* ---------------------------- miniatures ----------------------------- */

static void tile(cairo_t *cr, struct rect r, int idx)
{
	struct item *it = &A.items[idx];
	bool on = idx == A.current, hov = hover(r) && hover(clip);
	it->visible = r.y + r.h >= clip.y && r.y <= clip.y + clip.h;
	if (!it->visible)
		return;
	double th = r.h - 28;
	struct rect img = { r.x, r.y, r.w, th };
	cairo_save(cr);
	rrect(cr, img.x, img.y, img.w, img.h, 10);
	cairo_clip(cr);
	if (it->kind == K_COLOR) {
		rgba(cr, it->color, 1);
		cairo_paint(cr);
	} else if (it->thumb) {
		paint_mode(cr, it->thumb, img, M_FILL, 1);
	} else {
		rgba(cr, C_PANEL, 1);
		cairo_paint(cr);
		txt(cr, 12, PANGO_WEIGHT_NORMAL, 0.05, C_MUTED, 1, img.x + img.w / 2, img.y + img.h / 2, 1, 0,
		    it->thumb_tried ? "illisible" : "…");
	}
	cairo_restore(cr);
	rrect(cr, img.x, img.y, img.w, img.h, 10);
	if (on)
		glow_stroke(cr, C_LINE, 2, 1);
	else {
		rgba(cr, hov ? C_LINE : C_LINEDIM, 1);
		cairo_set_line_width(cr, hov ? 1.5 : 1);
		cairo_stroke(cr);
	}
	if (on) {
		cairo_arc(cr, img.x + img.w - 16, img.y + 16, 11, 0, 2 * M_PI);
		rgba(cr, C_LINE, 1);
		cairo_fill(cr);
		txt(cr, 13, PANGO_WEIGHT_BOLD, 0, C_BG, 1, img.x + img.w - 16, img.y + 16, 1, 0, "✓");
	}
	txt(cr, 12, on ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, 0.02, on ? C_BRIGHT : C_TEXT, 1,
	    r.x + 2, r.y + th + 15, 0, r.w - 4, it->name);
	add_hit(r, 1, idx, true);
}

static double tiles(cairo_t *cr, double x, double y, double w, enum kind k, double tw, double th)
{
	int cols = (int)fmax(1, floor((w + 14) / (tw + 14)));
	double gap = (w - cols * tw) / (cols > 1 ? cols - 1 : 1);
	if (cols == 1)
		gap = 0;
	int n = 0;
	for (int i = 0; i < A.nitems; i++) {
		if (A.items[i].kind != k)
			continue;
		struct rect r = { x + (n % cols) * (tw + gap), y + (n / cols) * (th + 16), tw, th };
		tile(cr, r, i);
		n++;
	}
	if (!n)
		return 0;
	int rows = (n + cols - 1) / cols;
	return rows * (th + 16);
}

/* ------------------------------- image ------------------------------- */

void ui_draw(cairo_t *cr, int W, int H)
{
	A.nhits = 0;
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	rgba(cr, C_BG, 1);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_pattern_t *g = cairo_pattern_create_radial(W * 0.5, H * 0.1, 0, W * 0.5, H * 0.1, W * 0.7);
	cairo_pattern_add_color_stop_rgba(g, 0, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0.12);
	cairo_pattern_add_color_stop_rgba(g, 1, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);

	clip = (struct rect){ 0, HEADER_H, W, H - HEADER_H - STATUS_H };
	double x = MARGIN, w = W - 2 * MARGIN;
	A.scroll = clampd(A.scroll, 0, fmax(0, A.content_h - clip.h + 20));
	cairo_save(cr);
	cairo_rectangle(cr, clip.x, clip.y, clip.w, clip.h);
	cairo_clip(cr);
	double y0 = HEADER_H + 20 - A.scroll, y = y0;

	/* aperçu + informations */
	double pw = fmin(w * 0.55, 560), ph = pw * 9 / 16;
	preview(cr, (struct rect){ x, y, pw, ph });
	double ix = x + pw + 32, iw = w - pw - 32;
	if (iw > 180) {
		struct item *it = A.current >= 0 ? &A.items[A.current] : NULL;
		txt(cr, 12, PANGO_WEIGHT_SEMIBOLD, 0.25, C_LINEDIM, 1, ix, y + 12, 0, 0, "FOND ACTUEL");
		txt(cr, 22, PANGO_WEIGHT_BOLD, 0.01, C_BRIGHT, 1, ix, y + 44, 0, iw, it ? it->name : "—");
		char info[PATH_MAX + 40] = "";
		if (it && it->kind == K_COLOR)
			snprintf(info, sizeof info, "Couleur unie #%06x", it->color);
		else if (it && it->img_w > 0)
			snprintf(info, sizeof info, "%d × %d px", it->img_w, it->img_h);
		txt(cr, 13, PANGO_WEIGHT_NORMAL, 0.01, C_MUTED, 1, ix, y + 74, 0, iw, info);
		if (it && it->kind != K_COLOR)
			txt(cr, 11, PANGO_WEIGHT_NORMAL, 0.01, C_LINEDIM, 1, ix, y + 96, 0, iw, it->path);
		txt(cr, 12, PANGO_WEIGHT_SEMIBOLD, 0.25, C_LINEDIM, 1, ix, y + 136, 0, 0, "AFFICHAGE");
		static const char *labels[] = { "Remplir", "Ajuster", "Centrer", "Mosaïque" };
		bool color = it && it->kind == K_COLOR;
		double bx = ix;
		double by = y + 152;
		for (int m = 0; m < 4; m++) {
			double bw = 104;
			if (bx + bw > ix + iw) {
				bx = ix;
				by += 40;
			}
			struct rect r = { bx, by, bw, 32 };
			bool on = A.mode == (enum mode)m && !color, hv = hover(r) && !color;
			rrect(cr, r.x, r.y, r.w, r.h, 16);
			rgba(cr, C_LINE, on ? 0.22 : hv ? 0.08 : 0.02);
			cairo_fill_preserve(cr);
			rgba(cr, on ? C_LINE : C_LINEDIM, color ? 0.5 : 1);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
			txt(cr, 12, on ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, 0.03, on ? C_BRIGHT : C_TEXT,
			    color ? 0.4 : 1, r.x + r.w / 2, r.y + 16, 1, 0, labels[m]);
			if (!color)
				add_hit(r, 2, m, true);
			bx += bw + 8;
		}
		/* couleur du thème (barre, dock, terminal, applis Plume, bordures) */
		txt(cr, 12, PANGO_WEIGHT_SEMIBOLD, 0.25, C_LINEDIM, 1, ix, by + 66, 0, 0, "COULEUR DU THÈME");
		for (int t = 0; t < NTHEMES; t++) {
			double cx = ix + 18 + t * 52, cy = by + 104;
			struct rect r = { cx - 18, cy - 18, 36, 36 };
			bool on = THEMES[t].accent == C_LINE, hv = hover(r);
			cairo_new_sub_path(cr);
			cairo_arc(cr, cx, cy, on ? 14 : 12, 0, 2 * M_PI);
			rgba(cr, THEMES[t].accent, 1);
			cairo_fill(cr);
			if (on || hv) {
				cairo_new_sub_path(cr);
				cairo_arc(cr, cx, cy, 18, 0, 2 * M_PI);
				rgba(cr, on ? THEMES[t].accent : C_MUTED, 1);
				cairo_set_line_width(cr, 2);
				cairo_stroke(cr);
			}
			add_hit(r, 3, t, true);
		}
		for (int t = 0; t < NTHEMES; t++)
			if (THEMES[t].accent == C_LINE)
				txt(cr, 12, PANGO_WEIGHT_NORMAL, 0.02, C_TEXT, 1, ix + NTHEMES * 52 + 8, by + 104, 0, 0, THEMES[t].label);
		txt(cr, 11, PANGO_WEIGHT_NORMAL, 0.02, C_MUTED, 1, ix, by + 142, 0, iw,
		    "Un clic applique tout de suite ; le choix est gardé au prochain démarrage.");
	}
	y += ph + 30;

	int nimg = 0;
	for (int i = 0; i < A.nitems; i++)
		nimg += A.items[i].kind == K_IMAGE;
	y += section(cr, x, y, w, "FONDS PLUME", NULL);
	y += tiles(cr, x, y, w, K_PRESET, 220, 152) + 16;
	y += section(cr, x, y, w, "COULEURS UNIES", NULL);
	y += tiles(cr, x, y, w, K_COLOR, 120, 96) + 16;
	char right[64];
	snprintf(right, sizeof right, "%d image%s", nimg, nimg > 1 ? "s" : "");
	y += section(cr, x, y, w, "MES IMAGES", right);
	if (!nimg) {
		txt(cr, 13, PANGO_WEIGHT_NORMAL, 0.01, C_MUTED, 1, x, y + 10, 0, w,
		    "Aucune image trouvée : mets tes photos dans le dossier Images (ou Bureau, Téléchargements).");
		y += 40;
	} else {
		y += tiles(cr, x, y, w, K_IMAGE, 220, 152);
	}
	A.content_h = y - y0 + 40;
	cairo_restore(cr);

	/* ascenseur */
	if (A.content_h > clip.h) {
		double max = A.content_h - clip.h + 20;
		double th = fmax(30, clip.h * clip.h / A.content_h), ty = clip.y + (clip.h - th) * A.scroll / max;
		rgba(cr, C_LINE, 0.5);
		rrect(cr, W - 8, ty, 3, th, 1.5);
		cairo_fill(cr);
	}

	/* en-tête */
	pf_draw(cr, MARGIN, HEADER_H / 2, A.px, A.py);
	txt(cr, 20, PANGO_WEIGHT_NORMAL, 0, C_LINE, 1, MARGIN + PF_LARGEUR, HEADER_H / 2, 0, 0, "◆");
	txt(cr, 15, PANGO_WEIGHT_BOLD, 0.3, C_BRIGHT, 1, MARGIN + PF_LARGEUR + 30, HEADER_H / 2, 0, 0, "APPARENCE");
	cairo_pattern_t *f = cairo_pattern_create_linear(0, 0, W, 0);
	cairo_pattern_add_color_stop_rgba(f, 0, ACC_RGB, 0.0);
	cairo_pattern_add_color_stop_rgba(f, 0.5, ACC_RGB, 0.55);
	cairo_pattern_add_color_stop_rgba(f, 1, ACC_RGB, 0.0);
	cairo_set_source(cr, f);
	cairo_rectangle(cr, 0, HEADER_H - 1, W, 1);
	cairo_fill(cr);
	cairo_pattern_destroy(f);

	/* barre d'état */
	double sy = H - STATUS_H;
	rgba(cr, C_BG, 0.96);
	cairo_rectangle(cr, 0, sy, W, STATUS_H);
	cairo_fill(cr);
	rgba(cr, C_LINEDIM, 1);
	cairo_rectangle(cr, 0, sy, W, 1);
	cairo_fill(cr);
	if (A.toast[0] && now_s() - A.toast_t < 5)
		txt(cr, 13, PANGO_WEIGHT_SEMIBOLD, 0.02, A.toast_err ? C_ERR : C_OK, 1, MARGIN, sy + STATUS_H / 2, 0,
		    W - 2 * MARGIN, A.toast);
	else
		txt(cr, 12, PANGO_WEIGHT_NORMAL, 0.02, C_MUTED, 1, MARGIN, sy + STATUS_H / 2, 0, W - 2 * MARGIN,
		    "L'écran de connexion et de verrouillage garde son propre décor HUD.");
}

void ui_click(double x, double y)
{
	switch (pf_clic(x, y)) {
	case 0: break;
	case 1: A.running = false; return;
	default: return;
	}
	for (int i = A.nhits - 1; i >= 0; i--) {
		struct hit *h = &A.hits[i];
		if (x < h->r.x || x > h->r.x + h->r.w || y < h->r.y || y > h->r.y + h->r.h)
			continue;
		if (h->act == 1)
			apply_item(h->arg);
		else if (h->act == 2)
			apply_mode((enum mode)h->arg);
		else if (h->act == 3)
			apply_theme(h->arg);
		A.dirty = true;
		return;
	}
	if (y < HEADER_H) /* double-clic dans le vide de l'en-tête : plein écran */
		pf_entete_clic(x, y);
}

void ui_scroll(double dy)
{
	A.scroll += dy;
	if (A.scroll < 0)
		A.scroll = 0;
	A.dirty = true;
}
