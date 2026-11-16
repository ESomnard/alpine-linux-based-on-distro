/* plume-term — dessin : cadre HUD, barre d'onglets, grille de caractères. */
#include "term.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define C_BG      0x02060b
#define C_TEXT    0xcdeefc
#define C_MUTED   0x5f8ea5
#define C_BRIGHT  0xe9f9ff
#define C_WARN    0xe6c86e
#define C_ERR     0xff5470
#define FONT      "JetBrains Mono, DejaVu Sans Mono, WenQuanYi Zen Hei"
#define FRAME     6.0     /* marge entre le bord de la fenêtre et le cadre */
#define BG_ALPHA  0.93

/* ------------------------------ outils ------------------------------- */

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

/* texte d'interface centré verticalement sur cy ; renvoie la largeur */
static double ui_text(cairo_t *cr, double px, int weight, uint32_t col, double a, double x, double cy,
		      int align, double maxw, const char *s)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_font_description_set_weight(fd, weight);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(l, s, -1);
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

/* ------------------------------ police ------------------------------- */

void font_setup(double px)
{
	if (px < 7) px = 7;
	if (px > 48) px = 48;
	A.font_px = px;
	for (int i = 0; i < 4; i++) {
		if (A.font[i])
			pango_font_description_free(A.font[i]);
		A.font[i] = pango_font_description_from_string(FONT);
		pango_font_description_set_absolute_size(A.font[i], px * PANGO_SCALE);
		pango_font_description_set_weight(A.font[i], (i & 1) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
		pango_font_description_set_style(A.font[i], (i & 2) ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
	}
	PangoFontMap *fm = pango_cairo_font_map_get_default();
	PangoContext *ctx = pango_font_map_create_context(fm);
	PangoFontMetrics *m = pango_context_get_metrics(ctx, A.font[0], NULL);
	double cw = pango_font_metrics_get_approximate_char_width(m) / (double)PANGO_SCALE;
	double asc = pango_font_metrics_get_ascent(m) / (double)PANGO_SCALE;
	double desc = pango_font_metrics_get_descent(m) / (double)PANGO_SCALE;
	pango_font_metrics_unref(m);
	g_object_unref(ctx);
	A.cw = fmax(1, round(cw));
	A.ch = ceil(asc + desc);
	A.ascent = asc;
	A.pad = round(px * 0.9);
}

static double tabbar_h(void)
{
	return round(A.font_px * 2.3);
}

/* zone du terminal : toute la fenêtre, ou le panneau central en mode HUD */
static struct rect term_area(int W, int H)
{
	if (A.hud)
		return hud_term_area(W, H);
	return (struct rect){ 0, 0, W, H };
}

void layout_grid(int W, int H, int *rows, int *cols)
{
	struct rect ta = term_area(W, H);
	double x = ta.x + FRAME + A.pad, y = ta.y + FRAME + tabbar_h() + A.pad * 0.6;
	double w = ta.x + ta.w - x - FRAME - A.pad, h = ta.y + ta.h - y - FRAME - A.pad * 0.8;
	int c = (int)(w / A.cw), r = (int)(h / A.ch);
	if (c < 2) c = 2;
	if (r < 2) r = 2;
	A.r_grid = (struct rect){ x, y, c * A.cw, r * A.ch };
	*rows = r;
	*cols = c;
}

/* ------------------------------ cadre -------------------------------- */

static void frame(cairo_t *cr, struct rect ta, double bell)
{
	double x = ta.x + FRAME, y = ta.y + FRAME, w = ta.w - 2 * FRAME, h = ta.h - 2 * FRAME;
	uint32_t col = bell > 0 ? C_WARN : term_focused() ? C_LINE : C_LINEDIM;
	rrect(cr, x, y, w, h, 12);
	glow_stroke(cr, col, 1.3, term_focused() ? 0.9 : 0.3);
	/* coins accentués (haut gauche, bas droit) */
	const double L = 26, rad = 8;
	double bx = x + 4, by = y + 4;
	cairo_move_to(cr, bx, by + L);
	cairo_line_to(cr, bx, by + rad);
	cairo_arc(cr, bx + rad, by + rad, rad, M_PI, 1.5 * M_PI);
	cairo_line_to(cr, bx + L, by);
	glow_stroke(cr, term_focused() ? C_CORNER : C_LINEDIM, 1.8, 0.7);
	double ex = x + w - 4, ey = y + h - 4;
	cairo_move_to(cr, ex, ey - L);
	cairo_line_to(cr, ex, ey - rad);
	cairo_arc(cr, ex - rad, ey - rad, rad, 0, 0.5 * M_PI);
	cairo_line_to(cr, ex - L, ey);
	glow_stroke(cr, term_focused() ? C_CORNER : C_LINEDIM, 1.8, 0.7);
}

static bool hover(struct rect r)
{
	return r.w > 0 && A.px >= r.x && A.px <= r.x + r.w && A.py >= r.y && A.py <= r.y + r.h;
}

static void tabbar(cairo_t *cr, struct rect ta)
{
	double th = tabbar_h(), y0 = ta.y + FRAME, cy = y0 + th / 2 + 1;
	double fs = A.font_px * 0.92;
	double x = ta.x + FRAME + A.pad;
	x += ui_text(cr, fs * 1.15, PANGO_WEIGHT_NORMAL, C_LINE, 1, x, cy, 0, 0, "◆") + fs * 0.6;

	/* à droite : heure, taille après redimensionnement */
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char hm[8];
	snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
	double rx = ta.x + ta.w - FRAME - A.pad;
	if (!A.hud) /* en mode HUD, l'heure est dans l'en-tête */
		rx -= ui_text(cr, fs, PANGO_WEIGHT_SEMIBOLD, C_MUTED, 1, rx, cy, 2, 0, hm) + fs;
	struct tab *at = tab_active();
	if (at && now_s() - A.resized_t < 1.5) {
		char sz[32];
		snprintf(sz, sizeof sz, "%d×%d", at->cols, at->rows);
		rx -= ui_text(cr, fs * 0.9, PANGO_WEIGHT_NORMAL, C_WARN, 1, rx, cy, 2, 0, sz) + fs;
	}

	/* onglets */
	int n = tab_count();
	double room = rx - x - th;
	double tw = n ? fmin(A.font_px * 17, room / n) : 0;
	double h = th - 10, ty = y0 + 6;
	for (int i = 0, k = 0; i < MAX_TABS; i++) {
		struct tab *t = &A.tabs[i];
		A.r_tabs[i] = A.r_tab_close[i] = (struct rect){ 0 };
		if (!t->used)
			continue;
		double tx = x + k * (tw + 4);
		k++;
		struct rect r = { tx, ty, tw, h };
		A.r_tabs[i] = r;
		bool act = i == A.active, hov = hover(r);
		rrect(cr, tx, ty, tw, h, 7);
		rgba(cr, C_LINE, act ? 0.16 : hov ? 0.07 : 0.0);
		cairo_fill_preserve(cr);
		if (act)
			glow_stroke(cr, C_LINE, 1.1, 0.6);
		else {
			rgba(cr, C_LINEDIM, hov ? 1 : 0.7);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
		}
		bool belled = now_s() - t->bell_t < 1.2 && !act;
		char num[4];
		snprintf(num, sizeof num, "%d", i + 1);
		double nx = tx + fs * 0.7;
		nx += ui_text(cr, fs * 0.85, PANGO_WEIGHT_BOLD, belled ? C_WARN : act ? C_LINE : C_LINEDIM, 1, nx, cy, 0, 0, num);
		double close_w = (act || hov) && n > 1 ? fs * 1.6 : 0;
		ui_text(cr, fs, act ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, act ? C_BRIGHT : C_TEXT, act ? 1 : 0.75,
			nx + fs * 0.6, cy, 0, tw - (nx - tx) - fs * 1.2 - close_w, t->label);
		if (close_w > 0) {
			struct rect cr_ = { tx + tw - close_w - 2, ty + 2, close_w, h - 4 };
			A.r_tab_close[i] = cr_;
			bool ch = hover(cr_);
			ui_text(cr, fs * 0.9, PANGO_WEIGHT_NORMAL, ch ? C_ERR : C_MUTED, 1, cr_.x + close_w / 2, cy, 1, 0, "✕");
		}
	}
	/* bouton « + » */
	double bx = x + n * (tw + 4);
	A.r_new = (struct rect){ 0 };
	if (n < MAX_TABS) {
		A.r_new = (struct rect){ bx, ty, h, h };
		bool hv = hover(A.r_new);
		rrect(cr, bx, ty, h, h, 7);
		rgba(cr, C_LINE, hv ? 0.1 : 0);
		cairo_fill_preserve(cr);
		rgba(cr, hv ? C_LINE : C_LINEDIM, 1);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		ui_text(cr, fs * 1.1, PANGO_WEIGHT_NORMAL, hv ? C_BRIGHT : C_MUTED, 1, bx + h / 2, cy, 1, 0, "+");
	}
	/* filet sous la barre */
	cairo_pattern_t *g = cairo_pattern_create_linear(ta.x + FRAME, 0, ta.x + ta.w - FRAME, 0);
	cairo_pattern_add_color_stop_rgba(g, 0, ACC_RGB, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 0.5, ACC_RGB, 0.45);
	cairo_pattern_add_color_stop_rgba(g, 1, ACC_RGB, 0.0);
	cairo_set_source(cr, g);
	cairo_rectangle(cr, ta.x + FRAME + 10, y0 + th + 2, ta.w - 2 * FRAME - 20, 1);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

/* ------------------------------ grille ------------------------------- */

static bool in_selection(int vrow, int col)
{
	struct selection *s = &A.sel;
	if (!s->active)
		return false;
	struct vpos a = s->a, b = s->b;
	if (a.row > b.row || (a.row == b.row && a.col > b.col)) {
		struct vpos x = a; a = b; b = x;
	}
	if (vrow < a.row || vrow > b.row)
		return false;
	if (vrow == a.row && col < a.col)
		return false;
	if (vrow == b.row && col > b.col)
		return false;
	return true;
}

/* caractère dessinable dans une suite (même chasse que la police) */
static bool simple_ch(uint32_t c)
{
	return c == 0 || (c >= 0x20 && c < 0x2000);
}

struct run_layouts {
	PangoLayout *l[4];
};

static void draw_text(cairo_t *cr, struct run_layouts *rl, int variant, uint32_t fg, double x, double y,
		      const char *s, int len)
{
	PangoLayout *l = rl->l[variant];
	pango_layout_set_text(l, s, len);
	/* chaque caractère occupe exactement une cellule : on impose la chasse
	 * des glyphes (sinon les arrondis décaleraient la grille) */
	int cwp = (int)(A.cw * PANGO_SCALE);
	PangoLayoutIter *it = pango_layout_get_iter(l);
	do {
		PangoLayoutRun *run = pango_layout_iter_get_run_readonly(it);
		if (!run)
			continue;
		PangoGlyphString *gs = run->glyphs;
		for (int i = 0; i < gs->num_glyphs; i++) {
			PangoGlyphGeometry *gg = &gs->glyphs[i].geometry;
			bool start = i == 0 || gs->log_clusters[i] != gs->log_clusters[i - 1];
			if (start) {
				gg->x_offset += (cwp - gg->width) / 2;
				gg->width = cwp;
			} else {
				gg->width = 0;
			}
		}
	} while (pango_layout_iter_next_run(it));
	pango_layout_iter_free(it);
	rgba(cr, fg, 1);
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, l);
}

/* caractère isolé, centré dans sa (ses) cellule(s) */
static void draw_single(cairo_t *cr, struct run_layouts *rl, int variant, uint32_t fg, double x, double y,
			double cellw, const char *s, int len)
{
	PangoLayout *l = rl->l[variant];
	pango_layout_set_text(l, s, len);
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	rgba(cr, fg, 1);
	double sx = 1;
	if (w > cellw + 1) /* trop large (police de secours) : on resserre */
		sx = cellw / w;
	cairo_save(cr);
	cairo_translate(cr, x + (cellw - w * sx) / 2, y + (A.ch - h) / 2);
	cairo_scale(cr, sx, 1);
	cairo_move_to(cr, 0, 0);
	pango_cairo_show_layout(cr, l);
	cairo_restore(cr);
}

static size_t utf8_enc(char *o, uint32_t c)
{
	if (c < 0x80) { o[0] = c; return 1; }
	if (c < 0x800) { o[0] = 0xC0 | (c >> 6); o[1] = 0x80 | (c & 63); return 2; }
	if (c < 0x10000) { o[0] = 0xE0 | (c >> 12); o[1] = 0x80 | ((c >> 6) & 63); o[2] = 0x80 | (c & 63); return 3; }
	o[0] = 0xF0 | (c >> 18); o[1] = 0x80 | ((c >> 12) & 63); o[2] = 0x80 | ((c >> 6) & 63); o[3] = 0x80 | (c & 63);
	return 4;
}

static void grid(cairo_t *cr, struct tab *t)
{
	struct rect g = A.r_grid;
	int rows = t->rows, cols = t->cols;
	struct cell *line = malloc(sizeof(struct cell) * cols);
	uint32_t *fgs = malloc(sizeof(uint32_t) * cols);
	char *buf = malloc((size_t)cols * 4 + 8);
	if (!line || !fgs || !buf) {
		free(line); free(fgs); free(buf);
		return;
	}
	struct run_layouts rl;
	PangoAttrList *al = pango_attr_list_new();
	pango_attr_list_insert(al, pango_attr_font_features_new("liga 0, calt 0"));
	for (int i = 0; i < 4; i++) {
		rl.l[i] = pango_cairo_create_layout(cr);
		pango_layout_set_font_description(rl.l[i], A.font[i]);
		pango_layout_set_attributes(rl.l[i], al);
	}
	pango_attr_list_unref(al);

	cairo_save(cr);
	cairo_rectangle(cr, g.x - 2, g.y - 1, g.w + 4, g.h + 2);
	cairo_clip(cr);
	for (int r = 0; r < rows; r++) {
		int vrow = r - t->view;
		double y = g.y + r * A.ch;
		for (int c = 0; c < cols; c++)
			tab_cell(t, vrow, c, &line[c]);
		/* fonds */
		for (int c = 0; c < cols; c++) {
			struct cell *cl = &line[c];
			uint32_t fg = cl->fg, bg = cl->bg;
			bool bg_def = cl->attrs & A_BG_DEF;
			if (cl->attrs & A_FG_DEF) fg = C_TEXT;
			if (cl->attrs & A_REVERSE) {
				uint32_t x = fg; fg = bg_def ? C_BG : bg; bg = x;
				bg_def = false;
			}
			if (in_selection(vrow, c)) {
				fg = C_BG;
				bg = C_LINE;
				bg_def = false;
			}
			fgs[c] = fg;
			if (!bg_def) {
				rgba(cr, bg, 1);
				cairo_rectangle(cr, g.x + c * A.cw, y, A.cw, A.ch);
				cairo_fill(cr);
			}
		}
		/* textes, par suites de même style */
		int c = 0;
		while (c < cols) {
			struct cell *cl = &line[c];
			if (cl->ch == CONT_CELL || cl->ch == 0 || cl->ch == ' ' || (cl->attrs & A_CONCEAL)) {
				c++;
				continue;
			}
			int variant = ((cl->attrs & A_BOLD) ? 1 : 0) | ((cl->attrs & A_ITALIC) ? 2 : 0);
			uint32_t fg = fgs[c];
			if (!simple_ch(cl->ch) || cl->width > 1) {
				size_t n = utf8_enc(buf, cl->ch);
				draw_single(cr, &rl, variant, fg, g.x + c * A.cw, y, A.cw * cl->width, buf, (int)n);
				c += cl->width > 1 ? cl->width : 1;
				continue;
			}
			int start = c;
			size_t n = 0;
			while (c < cols) {
				struct cell *d = &line[c];
				int dv = ((d->attrs & A_BOLD) ? 1 : 0) | ((d->attrs & A_ITALIC) ? 2 : 0);
				if (!simple_ch(d->ch) || d->width > 1 || dv != variant || (d->attrs & A_CONCEAL) ||
				    (fgs[c] != fg && d->ch != 0 && d->ch != ' '))
					break;
				n += utf8_enc(buf + n, d->ch ? d->ch : ' ');
				c++;
			}
			/* espaces de fin inutiles */
			while (n > 0 && buf[n - 1] == ' ')
				n--;
			if (n)
				draw_text(cr, &rl, variant, fg, g.x + start * A.cw, y, buf, (int)n);
		}
		/* soulignés et barrés */
		for (int k = 0; k < cols; k++) {
			struct cell *cl = &line[k];
			if (!(cl->attrs & (A_UNDER | A_STRIKE)) || cl->ch == CONT_CELL)
				continue;
			rgba(cr, fgs[k], 1);
			double w = A.cw * (cl->width > 1 ? cl->width : 1);
			if (cl->attrs & A_UNDER)
				cairo_rectangle(cr, g.x + k * A.cw, y + A.ascent + 2, w, 1);
			if (cl->attrs & A_STRIKE)
				cairo_rectangle(cr, g.x + k * A.cw, y + A.ch * 0.52, w, 1);
			cairo_fill(cr);
		}
		/* curseur */
		if (t->view == 0 && r == t->cursor.row && t->cursor_visible) {
			int cc = t->cursor.col < cols ? t->cursor.col : cols - 1;
			struct cell *cl = &line[cc];
			double cx = g.x + cc * A.cw, w = A.cw * (cl->width > 1 ? cl->width : 1);
			if (!term_focused()) {
				rgba(cr, C_LINE, 0.9);
				cairo_set_line_width(cr, 1);
				cairo_rectangle(cr, cx + 0.5, y + 0.5, w - 1, A.ch - 1);
				cairo_stroke(cr);
			} else if (t->cursor_shape == VTERM_PROP_CURSORSHAPE_BLOCK) {
				rgba(cr, C_LINE, 1);
				cairo_rectangle(cr, cx, y, w, A.ch);
				cairo_fill(cr);
				if (cl->ch && cl->ch != ' ' && cl->ch != CONT_CELL) {
					size_t n = utf8_enc(buf, cl->ch);
					int variant = ((cl->attrs & A_BOLD) ? 1 : 0) | ((cl->attrs & A_ITALIC) ? 2 : 0);
					draw_single(cr, &rl, variant, C_BG, cx, y, w, buf, (int)n);
				}
			} else if (t->cursor_shape == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
				rgba(cr, C_LINE, 1);
				cairo_rectangle(cr, cx, y + A.ch - 2, w, 2);
				cairo_fill(cr);
			} else {
				cairo_rectangle(cr, cx, y + 1, 2, A.ch - 2);
				rgba(cr, C_LINE, 0.25);
				cairo_fill(cr);
				rgba(cr, C_LINE, 1);
				cairo_rectangle(cr, cx, y + 1, 2, A.ch - 2);
				cairo_fill(cr);
			}
		}
	}
	cairo_restore(cr);

	/* historique : ascenseur + repère */
	if (t->view > 0) {
		int total = t->sb_count + rows;
		double th = fmax(20, g.h * rows / (double)total);
		double ty = g.y + (g.h - th) * (t->sb_count - t->view) / (double)(t->sb_count ? t->sb_count : 1);
		rgba(cr, C_LINE, 0.7);
		rrect(cr, g.x + g.w + A.pad * 0.35, ty, 3, th, 1.5);
		cairo_fill(cr);
		char info[48];
		snprintf(info, sizeof info, "↑ %d ligne%s · Maj+Fin pour revenir", t->view, t->view > 1 ? "s" : "");
		double fs = A.font_px * 0.85;
		double bw = fs * 0.62 * (double)strlen(info) + fs;
		rrect(cr, g.x + g.w - bw, g.y + 4, bw, fs * 1.9, 6);
		cairo_set_source_rgba(cr, 2 / 255.0, 6 / 255.0, 11 / 255.0, 0.9);
		cairo_fill_preserve(cr);
		rgba(cr, C_LINE, 0.8);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		ui_text(cr, fs, PANGO_WEIGHT_NORMAL, C_LINE, 1, g.x + g.w - bw / 2, g.y + 4 + fs * 0.95, 1, bw - 8, info);
	}
	for (int i = 0; i < 4; i++)
		g_object_unref(rl.l[i]);
	free(line);
	free(fgs);
	free(buf);
}

/* ------------------------------ image -------------------------------- */

void render_frame(cairo_t *cr, int W, int H)
{
	struct rect ta = term_area(W, H);
	if (A.hud) {
		hud_draw_back(cr, W, H); /* fond, en-tête, panneaux (en cache) */
	} else {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		rgba(cr, C_BG, BG_ALPHA);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		/* léger halo bleu en haut */
		cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, H * 0.6);
		cairo_pattern_add_color_stop_rgba(g, 0, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0.10);
		cairo_pattern_add_color_stop_rgba(g, 1, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0);
		cairo_set_source(cr, g);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
	}

	struct tab *t = tab_active();
	double bell = t ? 1.0 - (now_s() - t->bell_t) / 0.35 : 0;
	frame(cr, ta, bell);
	tabbar(cr, ta);
	if (t)
		grid(cr, t);
	if (A.hud)
		hud_draw_front(cr, W, H); /* clavier (s'allume à chaque frappe) */
}
