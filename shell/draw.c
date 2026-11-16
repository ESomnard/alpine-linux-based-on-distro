/* plume-shell — dessin de la barre et du dock (palette et motifs de plume-hud). */
#include "shell.h"
#include "theme.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define C_BG      0x02060b
#define C_TEXT    0xcdeefc
#define C_MUTED   0x5f8ea5
#define C_OK      0x4fe0a4
#define C_ERR     0xff5470
#define C_WARN    0xe6c86e
#define C_BRIGHT  0xe9f9ff
#define FONT      "JetBrains Mono, DejaVu Sans Mono"
#define FONT_SYM  "JetBrains Mono, DejaVu Sans, WenQuanYi Zen Hei"

static const char *JOURS[] = { "dim", "lun", "mar", "mer", "jeu", "ven", "sam" };
static const char *MOIS[] = { "janv", "févr", "mars", "avr", "mai", "juin", "juil",
			      "août", "sept", "oct", "nov", "déc" };

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

static void glow_stroke(cairo_t *cr, uint32_t col, double lw, double strength)
{
	static const double widths[] = { 8, 4.5, 2 };
	for (int i = 0; i < 3; i++) {
		rgba(cr, col, 0.07 * strength * (i + 1));
		cairo_set_line_width(cr, lw + widths[i]);
		cairo_stroke_preserve(cr);
	}
	rgba(cr, col, 1);
	cairo_set_line_width(cr, lw);
	cairo_stroke(cr);
}

static PangoLayout *mk(cairo_t *cr, const char *family, double px, int weight, double ls_em, const char *s)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(family);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_font_description_set_weight(fd, weight);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	if (ls_em) {
		PangoAttrList *al = pango_attr_list_new();
		pango_attr_list_insert(al, pango_attr_letter_spacing_new((int)(ls_em * px * PANGO_SCALE)));
		pango_layout_set_attributes(l, al);
		pango_attr_list_unref(al);
	}
	pango_layout_set_text(l, s, -1);
	return l;
}

/* Dessine un texte centré verticalement sur cy. align : 0 gauche, 1 centre, 2 droite.
 * Renvoie la largeur. maxw > 0 : coupe avec « … ». */
static double txt(cairo_t *cr, const char *family, double px, int weight, double ls, uint32_t col, double a,
		  double x, double cy, int align, double maxw, const char *s)
{
	PangoLayout *l = mk(cr, family, px, weight, ls, s);
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

static double txt_w(cairo_t *cr, const char *family, double px, int weight, double ls, const char *s)
{
	PangoLayout *l = mk(cr, family, px, weight, ls, s);
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	g_object_unref(l);
	return w;
}

static void power_icon(cairo_t *cr, double cx, double cy, double r, uint32_t col, double a)
{
	cairo_new_path(cr);
	cairo_arc(cr, cx, cy, r, -M_PI / 2 + 0.75, -M_PI / 2 - 0.75 + 2 * M_PI);
	cairo_move_to(cr, cx, cy - r - 1.5);
	cairo_line_to(cr, cx, cy - r * 0.15);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	rgba(cr, col, a * 0.25);
	cairo_set_line_width(cr, 5);
	cairo_stroke_preserve(cr);
	rgba(cr, col, a);
	cairo_set_line_width(cr, 1.8);
	cairo_stroke(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
}

static void bracket(cairo_t *cr, double x, double y, double dx, double dy, double L)
{
	cairo_move_to(cr, x, y + dy * L);
	cairo_line_to(cr, x, y);
	cairo_line_to(cr, x + dx * L, y);
	glow_stroke(cr, C_CORNER, 1.5, 0.6);
}

/* =============================== BARRE =============================== */

/* segment de droite : dessine à gauche de *x et recule *x */
static void sep(cairo_t *cr, double *x, double cy)
{
	*x -= 10;
	rgba(cr, C_LINEDIM, 1);
	cairo_rectangle(cr, *x, cy - 7, 1, 14);
	cairo_fill(cr);
	*x -= 10;
}

static double seg_text(cairo_t *cr, double *x, double cy, uint32_t col, const char *s)
{
	double w = txt(cr, FONT_SYM, 12, PANGO_WEIGHT_NORMAL, 0.04, col, 1, *x, cy, 2, 0, s);
	*x -= w;
	return w;
}

static double seg_key(cairo_t *cr, double *x, double cy, const char *s)
{
	*x -= 6;
	double w = txt(cr, FONT, 10, PANGO_WEIGHT_SEMIBOLD, 0.2, C_LINE, 1, *x, cy, 2, 0, s);
	*x -= w;
	return w + 6;
}

void draw_bar(struct output *o, cairo_t *cr, int W, int H)
{
	double cy = H / 2.0;
	/* fond translucide + filet lumineux en bas */
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 2 / 255.0, 6 / 255.0, 11 / 255.0, 0.90);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, W, 0);
	cairo_pattern_add_color_stop_rgba(g, 0, ACC_RGB, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 0.5, ACC_RGB, 0.55);
	cairo_pattern_add_color_stop_rgba(g, 1, ACC_RGB, 0.0);
	cairo_set_source(cr, g);
	cairo_rectangle(cr, 0, H - 1, W, 1);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	bracket(cr, 3.5, H - 3.5, 1, -1, 9);
	bracket(cr, W - 3.5, H - 3.5, -1, -1, 9);

	/* ◆ PLUME */
	double x = 16;
	double lw = txt(cr, FONT_SYM, 13, PANGO_WEIGHT_NORMAL, 0, C_LINE, 1, x, cy, 0, 0, "◆");
	double lw2 = txt(cr, FONT, 12, PANGO_WEIGHT_BOLD, 0.32, C_BRIGHT, 1, x + lw + 8, cy, 0, 0, "PLUME");
	o->hit_logo = (struct rect){ x - 6, 0, lw + 8 + lw2 + 12, H };
	x += lw + 8 + lw2 + 20;

	/* bureaux de cet écran (tous si les noms ne correspondent pas) */
	bool match = false;
	for (int i = 0; i < S.nws; i++)
		if (o->name[0] && !strcmp(S.ws[i].output, o->name))
			match = true;
	for (int i = 0; i < MAX_WS; i++)
		o->hit_ws[i] = (struct rect){ 0 };
	for (int i = 0; i < S.nws; i++) {
		struct workspace *w = &S.ws[i];
		if (match && strcmp(w->output, o->name))
			continue;
		double tw = txt_w(cr, FONT, 11, PANGO_WEIGHT_BOLD, 0, w->name);
		double pw = tw + 16 < 24 ? 24 : tw + 16, ph = 20;
		rrect(cr, x, cy - ph / 2, pw, ph, 5);
		uint32_t tc;
		if (w->urgent) {
			rgba(cr, C_ERR, 1);
			cairo_fill(cr);
			tc = C_BG;
		} else if (w->focused) {
			rgba(cr, C_LINE, 0.25);
			cairo_set_line_width(cr, 4);
			cairo_stroke_preserve(cr);
			rgba(cr, C_LINE, 1);
			cairo_fill(cr);
			tc = C_BG;
		} else {
			rgba(cr, w->visible ? C_LINE : C_LINEDIM, 1);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
			tc = w->visible ? C_TEXT : C_MUTED;
		}
		txt(cr, FONT, 11, PANGO_WEIGHT_BOLD, 0, tc, 1, x + pw / 2, cy, 1, 0, w->name);
		o->hit_ws[i] = (struct rect){ x, 0, pw, H };
		x += pw + 6;
	}
	double left_end = x + 12;

	/* ----- côté droit, de droite à gauche ----- */
	double rx = W - 16;
	power_icon(cr, rx - 8, cy, 6.5, C_MUTED, 1);
	o->hit_power = (struct rect){ rx - 22, 0, 30, H };
	rx -= 16;
	sep(cr, &rx, cy);

	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char hm[8], day[32];
	snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
	snprintf(day, sizeof day, "%s %d %s", JOURS[tm.tm_wday], tm.tm_mday, MOIS[tm.tm_mon]);
	double cw = txt(cr, FONT, 13, PANGO_WEIGHT_BOLD, 0.05, C_BRIGHT, 1, rx, cy, 2, 0, hm);
	rx -= cw + 10;
	rx -= txt(cr, FONT, 11, PANGO_WEIGHT_NORMAL, 0.08, C_MUTED, 1, rx, cy, 2, 0, day);
	sep(cr, &rx, cy);

	char buf[48];
	if (S.bat >= 0) {
		snprintf(buf, sizeof buf, "%s%d%%", S.charging ? "⚡" : "", S.bat);
		seg_text(cr, &rx, cy, S.bat <= 15 && !S.charging ? C_ERR : C_TEXT, buf);
		seg_key(cr, &rx, cy, "BAT");
		sep(cr, &rx, cy);
	}

	double vx0 = rx;
	if (S.vol >= 0) {
		if (S.muted)
			snprintf(buf, sizeof buf, "muet");
		else
			snprintf(buf, sizeof buf, "%d%%", S.vol);
		seg_text(cr, &rx, cy, S.muted ? C_MUTED : C_TEXT, buf);
		seg_key(cr, &rx, cy, "SON");
		o->hit_vol = (struct rect){ rx - 4, 0, vx0 - rx + 8, H };
		sep(cr, &rx, cy);
	} else {
		o->hit_vol = (struct rect){ 0 };
	}

	if (S.maj > 0 || S.maj_version[0]) {
		double mx0 = rx;
		if (S.maj > 0)
			snprintf(buf, sizeof buf, "⬆ %d", S.maj);
		else
			snprintf(buf, sizeof buf, "★ %s", S.maj_version);
		seg_text(cr, &rx, cy, C_CORNER, buf);
		seg_key(cr, &rx, cy, "MAJ");
		o->hit_maj = (struct rect){ rx - 4, 0, mx0 - rx + 8, H };
		sep(cr, &rx, cy);
	} else {
		o->hit_maj = (struct rect){ 0 };
	}

	if (S.bt) {
		double bx0 = rx;
		seg_text(cr, &rx, cy, C_TEXT, "ᛒ");
		seg_key(cr, &rx, cy, "BT");
		o->hit_bt = (struct rect){ rx - 4, 0, bx0 - rx + 8, H };
		sep(cr, &rx, cy);
	} else {
		o->hit_bt = (struct rect){ 0 };
	}

	double nx0 = rx;
	seg_text(cr, &rx, cy, S.online ? C_TEXT : C_ERR, S.online ? (S.wifi ? "WIFI" : "ETH") : "HORS LIGNE");
	rx -= 8;
	cairo_arc(cr, rx - 3, cy, 6, 0, 2 * M_PI);
	rgba(cr, S.online ? C_OK : C_ERR, 0.22);
	cairo_fill(cr);
	cairo_arc(cr, rx - 3, cy, 3, 0, 2 * M_PI);
	rgba(cr, S.online ? C_OK : C_ERR, 1);
	cairo_fill(cr);
	rx -= 6;
	o->hit_net = (struct rect){ rx - 4, 0, nx0 - rx + 8, H };
	sep(cr, &rx, cy);

	snprintf(buf, sizeof buf, "%d%%", S.ram);
	seg_text(cr, &rx, cy, S.ram >= 90 ? C_ERR : C_TEXT, buf);
	seg_key(cr, &rx, cy, "RAM");
	sep(cr, &rx, cy);

	/* CPU : mini-graphe */
	snprintf(buf, sizeof buf, "%d%%", S.cpu);
	seg_text(cr, &rx, cy, S.cpu >= 90 ? C_ERR : C_TEXT, buf);
	rx -= 8;
	double gw = CPU_HIST * 3, gh = 14, gx = rx - gw, gy = cy + gh / 2;
	for (int i = 0; i < CPU_HIST; i++) {
		double v = S.cpu_hist[i] / 100.0, bh = v * gh < 1 ? 1 : v * gh;
		rgba(cr, v > 0.85 ? C_ERR : C_LINE, 0.35 + 0.65 * (i + 1) / CPU_HIST);
		cairo_rectangle(cr, gx + i * 3, gy - bh, 2, bh);
		cairo_fill(cr);
	}
	rx = gx;
	seg_key(cr, &rx, cy, "CPU");
	sep(cr, &rx, cy);

	if (S.kb[0]) {
		double kx0 = rx;
		seg_text(cr, &rx, cy, C_LINE, S.kb);
		o->hit_kb = (struct rect){ rx - 4, 0, kx0 - rx + 8, H };
		sep(cr, &rx, cy);
	}

	/* ----- centre : fenêtre active ----- */
	double avail = rx - left_end - 20;
	if (avail > 80 && (S.title[0] || S.focused_app[0])) {
		char up[64];
		size_t i = 0;
		for (; S.focused_app[i] && i + 1 < sizeof up; i++)
			up[i] = (S.focused_app[i] >= 'a' && S.focused_app[i] <= 'z') ? S.focused_app[i] - 32 : S.focused_app[i];
		up[i] = '\0';
		double aw = up[0] ? txt_w(cr, FONT, 10, PANGO_WEIGHT_SEMIBOLD, 0.2, up) + 12 : 0;
		double tw = txt_w(cr, FONT_SYM, 11.5, PANGO_WEIGHT_NORMAL, 0, S.title);
		if (tw > avail - aw)
			tw = avail - aw;
		double total = aw + tw, cx = W / 2.0;
		double sx = cx - total / 2;
		if (sx < left_end)
			sx = left_end;
		if (sx + total > rx - 20)
			sx = rx - 20 - total;
		if (up[0])
			txt(cr, FONT, 10, PANGO_WEIGHT_SEMIBOLD, 0.2, C_LINE, 1, sx, cy, 0, 0, up);
		txt(cr, FONT_SYM, 11.5, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, sx + aw, cy, 0, tw + 1, S.title);
	}
}

/* =============================== DOCK ================================ */

void dock_layout(struct output *o, int W, int H, double *panel_x, double *panel_w)
{
	int seps = 0;
	for (int i = 0; i < S.napps; i++)
		if (S.apps[i].separator_before)
			seps++;
	double pw = S.napps * DOCK_TILE + (S.napps - 1) * DOCK_GAP + seps * (DOCK_GAP + 1) + 2 * DOCK_PAD;
	double px = (W - pw) / 2.0;
	double py = H - DOCK_MARGIN - DOCK_PANEL_H;
	o->dock_panel = (struct rect){ px, py, pw, DOCK_PANEL_H };
	double x = px + DOCK_PAD;
	for (int i = 0; i < S.napps; i++) {
		if (S.apps[i].separator_before)
			x += DOCK_GAP + 1;
		o->hit_app[i] = (struct rect){ x, py + DOCK_PAD, DOCK_TILE, DOCK_TILE };
		x += DOCK_TILE + DOCK_GAP;
	}
	if (panel_x) *panel_x = px;
	if (panel_w) *panel_w = pw;
}

void draw_dock(struct output *o, cairo_t *cr, int W, int H)
{
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	double px, pw;
	dock_layout(o, W, H, &px, &pw);
	struct rect p = o->dock_panel;

	/* panneau HUD */
	rrect(cr, p.x, p.y, p.w, p.h, 18);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, p.y, 0, p.y + p.h);
	cairo_pattern_add_color_stop_rgba(g, 0, 8 / 255.0, 20 / 255.0, 30 / 255.0, 0.78);
	cairo_pattern_add_color_stop_rgba(g, 1, 3 / 255.0, 8 / 255.0, 14 / 255.0, 0.94);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	glow_stroke(cr, C_LINE, 1.5, 0.9);
	bracket(cr, p.x + 5, p.y + 5, 1, 1, 18);
	bracket(cr, p.x + p.w - 5, p.y + p.h - 5, -1, -1, 18);

	for (int i = 0; i < S.napps; i++) {
		struct app *a = &S.apps[i];
		struct rect t = o->hit_app[i];
		if (a->separator_before) {
			rgba(cr, C_LINEDIM, 1);
			cairo_rectangle(cr, t.x - DOCK_GAP - 1, t.y + 6, 1, t.h - 12);
			cairo_fill(cr);
		}
		double hv = a->hover;
		double sc = 1 + 0.18 * hv, lift = 7 * hv;
		double tw = DOCK_TILE * sc;
		double tx = t.x + (DOCK_TILE - tw) / 2, ty = t.y + DOCK_TILE - tw - lift;
		bool run = app_is_running(a);

		rrect(cr, tx, ty, tw, tw, 12 * sc);
		cairo_set_source_rgba(cr, ACC_RGB, 0.05 + 0.10 * hv + (run ? 0.04 : 0));
		cairo_fill_preserve(cr);
		if (hv > 0.05)
			glow_stroke(cr, C_LINE, 1.2, hv);
		else {
			rgba(cr, run ? C_LINE : C_LINEDIM, run ? 0.7 : 1);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
		}
		uint32_t gc = hv > 0.5 ? C_BRIGHT : run ? C_TEXT : C_LINE;
		if (a->power)
			power_icon(cr, tx + tw / 2, ty + tw / 2, tw * 0.2, hv > 0.5 ? C_ERR : C_MUTED, 1);
		else {
			bool ascii = (unsigned char)a->glyph[0] < 0x80;
			txt(cr, FONT_SYM, (ascii ? 17 : 22) * sc, ascii ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
			    0, gc, 1, tx + tw / 2, ty + tw / 2, 1, 0, a->glyph);
		}
		/* point « ouvert » */
		if (run) {
			bool foc = S.focused_app[0] && !strcasecmp(S.focused_app, a->app_id);
			double dx = t.x + DOCK_TILE / 2.0, dy = p.y + p.h - 7;
			cairo_arc(cr, dx, dy, foc ? 5 : 4, 0, 2 * M_PI);
			rgba(cr, foc ? C_OK : C_LINE, 0.25);
			cairo_fill(cr);
			cairo_arc(cr, dx, dy, foc ? 2.5 : 2, 0, 2 * M_PI);
			rgba(cr, foc ? C_OK : C_LINE, 1);
			cairo_fill(cr);
		}
	}

	/* info-bulle */
	if (S.hover_app >= 0 && S.hover_app < S.napps && S.apps[S.hover_app].hover > 0.2) {
		struct app *a = &S.apps[S.hover_app];
		struct rect t = o->hit_app[S.hover_app];
		double al = a->hover;
		double lw = txt_w(cr, FONT, 12, PANGO_WEIGHT_SEMIBOLD, 0.08, a->label);
		double bw = lw + 24, bh = 26, cx = t.x + DOCK_TILE / 2.0;
		double bx = cx - bw / 2, by = p.y - 16 - bh - 6 * al;
		if (bx < 4) bx = 4;
		if (bx + bw > W - 4) bx = W - 4 - bw;
		cairo_push_group(cr);
		rrect(cr, bx, by, bw, bh, 7);
		cairo_set_source_rgba(cr, 3 / 255.0, 8 / 255.0, 14 / 255.0, 0.96);
		cairo_fill_preserve(cr);
		glow_stroke(cr, C_LINE, 1, 0.8);
		cairo_move_to(cr, cx - 6, by + bh);
		cairo_line_to(cr, cx, by + bh + 6);
		cairo_line_to(cr, cx + 6, by + bh);
		cairo_close_path(cr);
		rgba(cr, C_LINE, 1);
		cairo_fill(cr);
		txt(cr, FONT, 12, PANGO_WEIGHT_SEMIBOLD, 0.08, C_BRIGHT, 1, bx + bw / 2, by + bh / 2, 1, 0, a->label);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, al);
	}
}
