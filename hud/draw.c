/*
 * plume-hud — dessin du HUD (cairo + pango).
 * Reprend fidèlement renderer/style.css du projet d'origine : mêmes couleurs,
 * même grille 5 panneaux, mêmes équerres, lueurs et animations.
 */
#include "hud.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* --- palette (:root du CSS) --- */
#define C_BG      0x02060b
#define C_PANEL   0x03080e
#define C_LINE    0x4fb8e0
#define C_LINEDIM 0x1d5a78
#define C_TEXT    0xcdeefc
#define C_MUTED   0x5f8ea5
#define C_OK      0x4fe0a4
#define C_ERR     0xff5470
#define C_BRIGHT  0xe9f9ff
#define C_CORNER  0x9fe4ff

#define FONT_MONO "JetBrains Mono, DejaVu Sans Mono"
#define FONT_CLOCK "DejaVu Sans"

static const char *JOURS[] = { "DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI", "VENDREDI", "SAMEDI" };
static const char *MOIS[] = { "JANVIER", "FÉVRIER", "MARS", "AVRIL", "MAI", "JUIN", "JUILLET",
			      "AOÛT", "SEPTEMBRE", "OCTOBRE", "NOVEMBRE", "DÉCEMBRE" };

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

/* Trait avec halo lumineux (équivalent box-shadow 0 0 Npx) puis trait net. */
static void glow_stroke(cairo_t *cr, uint32_t col, double lw, double strength)
{
	static const double widths[] = { 9, 5, 2.5 };
	for (int i = 0; i < 3; i++) {
		rgba(cr, col, 0.07 * strength * (i + 1));
		cairo_set_line_width(cr, lw + widths[i]);
		cairo_stroke_preserve(cr);
	}
	rgba(cr, col, 1);
	cairo_set_line_width(cr, lw);
	cairo_stroke(cr);
}

/* ------------------------------ texte ------------------------------- */

static PangoLayout *layout(cairo_t *cr, const char *family, double px, int weight, double ls_em, const char *s)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_new();
	pango_font_description_set_family(fd, family);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_font_description_set_weight(fd, weight);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	if (ls_em != 0) {
		PangoAttrList *al = pango_attr_list_new();
		pango_attr_list_insert(al, pango_attr_letter_spacing_new((int)(ls_em * px * PANGO_SCALE)));
		pango_layout_set_attributes(l, al);
		pango_attr_list_unref(al);
	}
	pango_layout_set_text(l, s, -1);
	return l;
}

struct tstyle {
	const char *family;
	double px;
	int weight;
	double ls;       /* letter-spacing en em */
	uint32_t col;
	double alpha;
};

static void measure(cairo_t *cr, struct tstyle st, const char *s, double *w, double *h)
{
	PangoLayout *l = layout(cr, st.family, st.px, st.weight, st.ls, s);
	int iw, ih;
	pango_layout_get_pixel_size(l, &iw, &ih);
	g_object_unref(l);
	if (w) *w = iw;
	if (h) *h = ih;
}

/* align : 0 = x est le bord gauche, 1 = x est le centre, 2 = x est le bord droit */
static double text(cairo_t *cr, struct tstyle st, double x, double y, int align, double maxw, const char *s)
{
	PangoLayout *l = layout(cr, st.family, st.px, st.weight, st.ls, s);
	if (maxw > 0) {
		pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
		pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
	}
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	double tx = align == 0 ? x : align == 1 ? x - w / 2.0 : x - w;
	if (align == 1)
		tx += st.ls * st.px / 2; /* compense l'espacement après la dernière lettre */
	cairo_move_to(cr, tx, y);
	rgba(cr, st.col, st.alpha > 0 ? st.alpha : 1);
	pango_cairo_show_layout(cr, l);
	g_object_unref(l);
	return h;
}

static struct tstyle ts(double px, uint32_t col) { return (struct tstyle){ FONT_MONO, px, PANGO_WEIGHT_NORMAL, 0, col, 1 }; }
static struct tstyle tsl(double px, uint32_t col, double ls) { return (struct tstyle){ FONT_MONO, px, PANGO_WEIGHT_NORMAL, ls, col, 1 }; }

static void upper_ascii(char *dst, size_t cap, const char *src)
{
	size_t i = 0;
	for (; src[i] && i + 1 < cap; i++)
		dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
	dst[i] = '\0';
}

/* Titre de panneau « OPÉRATEUR ────── » ; renvoie la hauteur consommée. */
static double panel_title(cairo_t *cr, double x, double y, double w, double vh, const char *t)
{
	double px = clampd(1.2 * vh, 10, 14);
	struct tstyle st = { FONT_MONO, px, PANGO_WEIGHT_SEMIBOLD, 0.3, C_LINE, 1 };
	double tw, th;
	measure(cr, st, t, &tw, &th);
	text(cr, st, x, y, 0, 0, t);
	double lx = x + tw + 0.6 * px, ly = y + th / 2.0;
	if (lx < x + w) {
		cairo_pattern_t *g = cairo_pattern_create_linear(lx, 0, x + w, 0);
		cairo_pattern_add_color_stop_rgba(g, 0, 0x1d / 255.0, 0x5a / 255.0, 0x78 / 255.0, 1);
		cairo_pattern_add_color_stop_rgba(g, 1, 0x1d / 255.0, 0x5a / 255.0, 0x78 / 255.0, 0);
		cairo_set_source(cr, g);
		cairo_rectangle(cr, lx, ly, x + w - lx, 1);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
	return th + 1.4 * vh;
}

/* ----------------------------- décor -------------------------------- */

static void draw_background(cairo_t *cr, double W, double H)
{
	rgba(cr, C_BG, 1);
	cairo_paint(cr);
	/* radial-gradient(ellipse at 50% 35%, rgba(30,90,130,.12), transparent 60%) */
	cairo_save(cr);
	cairo_translate(cr, W * 0.5, H * 0.35);
	cairo_scale(cr, W * 0.6, H * 0.6);
	cairo_pattern_t *g = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(g, 0, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0.12);
	cairo_pattern_add_color_stop_rgba(g, 1, 30 / 255.0, 90 / 255.0, 130 / 255.0, 0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
	/* lignes de balayage */
	cairo_set_source_rgba(cr, 1, 1, 1, 0.012);
	for (double y = 0; y < H; y += 3)
		cairo_rectangle(cr, 0, y, W, 1);
	cairo_fill(cr);
}

static void draw_panel_frame(cairo_t *cr, struct rect r)
{
	rrect(cr, r.x, r.y, r.w, r.h, 14);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
	cairo_pattern_add_color_stop_rgba(g, 0, 8 / 255.0, 20 / 255.0, 30 / 255.0, 0.55);
	cairo_pattern_add_color_stop_rgba(g, 1, 3 / 255.0, 8 / 255.0, 14 / 255.0, 1);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	glow_stroke(cr, C_LINE, 1.5, 1.0);

	/* équerres dans les coins (::before / ::after) */
	const double L = 34, rad = 10;
	double bx = r.x + 4, by = r.y + 4;
	cairo_move_to(cr, bx, by + L);
	cairo_line_to(cr, bx, by + rad);
	cairo_arc(cr, bx + rad, by + rad, rad, M_PI, 1.5 * M_PI);
	cairo_line_to(cr, bx + L, by);
	glow_stroke(cr, C_CORNER, 2, 0.8);
	double ex = r.x + r.w - 4, ey = r.y + r.h - 4;
	cairo_move_to(cr, ex, ey - L);
	cairo_line_to(cr, ex, ey - rad);
	cairo_arc(cr, ex - rad, ey - rad, rad, 0, 0.5 * M_PI);
	cairo_line_to(cr, ex - L, ey);
	glow_stroke(cr, C_CORNER, 2, 0.8);
}

/* Grille : colonnes 15.5 / 66 / 15.5, lignes 39 / 25 / 33 */
static void grid(double W, double H, struct rect p[5])
{
	double vh = H / 100.0, gap = 1.1 * vh;
	double aw = W - 2 * gap, ah = H - 2 * gap;
	double cw = aw - 2 * gap, rh = ah - 2 * gap;
	double c0 = cw * 15.5 / 97, c1 = cw * 66 / 97, c2 = cw * 15.5 / 97;
	double r0 = rh * 39 / 97, r1 = rh * 25 / 97, r2 = rh * 33 / 97;
	double x0 = gap, x1 = x0 + c0 + gap, x2 = x1 + c1 + gap;
	double y0 = gap, y1 = y0 + r0 + gap, y2 = y1 + r1 + gap;
	p[0] = (struct rect){ x0, y0, c0, r0 + gap + r1 };   /* gauche */
	p[1] = (struct rect){ x1, y0, c1, r0 + gap + r1 };   /* centre */
	p[2] = (struct rect){ x2, y0, c2, r0 };              /* haut droite */
	p[3] = (struct rect){ x2, y1, c2, r1 };              /* bas droite */
	p[4] = (struct rect){ x0, y2, aw, r2 };              /* bas */
}

static void ensure_bg_cache(struct output *o, cairo_t *target, int W, int H, struct rect p[5])
{
	int s = o->scale > 0 ? o->scale : 1;
	if (o->bg_cache && o->bg_w == W && o->bg_h == H && o->bg_scale == s)
		return;
	if (o->bg_cache)
		cairo_surface_destroy(o->bg_cache);
	(void)target;
	o->bg_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W * s, H * s);
	cairo_surface_set_device_scale(o->bg_cache, s, s);
	o->bg_w = W;
	o->bg_h = H;
	o->bg_scale = s;
	cairo_t *cr = cairo_create(o->bg_cache);
	draw_background(cr, W, H);
	for (int i = 0; i < 5; i++)
		draw_panel_frame(cr, p[i]);
	cairo_destroy(cr);
}

/* --------------------------- panneaux -------------------------------- */

static void fmt_uptime(long s, char *out, size_t cap)
{
	long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
	if (d)
		snprintf(out, cap, "%ldj %ldh %ldm", d, h, m);
	else
		snprintf(out, cap, "%ldh %02ldm", h, m);
}

static void meter(cairo_t *cr, double x, double y, double w, double vh, const char *label, double pct, int shown)
{
	char v[16];
	snprintf(v, sizeof v, "%d%%", shown);
	double lh = text(cr, tsl(1.2 * vh, C_MUTED, 0.2), x, y, 0, 0, label);
	text(cr, ts(1.2 * vh, C_TEXT), x + w, y, 2, 0, v);
	double by = y + lh + 0.6 * vh;
	rrect(cr, x, by, w, 6, 3);
	rgba(cr, C_LINEDIM, 1);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	double fw = (w - 2) * clampd(pct, 0, 100) / 100.0;
	if (fw > 0.5) {
		cairo_pattern_t *g = cairo_pattern_create_linear(x, 0, x + w, 0);
		cairo_pattern_add_color_stop_rgb(g, 0, 0x1d / 255.0, 0x5a / 255.0, 0x78 / 255.0);
		cairo_pattern_add_color_stop_rgb(g, 1, 0x4f / 255.0, 0xb8 / 255.0, 0xe0 / 255.0);
		rgba(cr, C_LINE, 0.25);
		rrect(cr, x + 1, by - 1, fw, 8, 3);
		cairo_fill(cr);
		cairo_set_source(cr, g);
		rrect(cr, x + 1, by + 1, fw, 4, 2);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
}

static void draw_left(cairo_t *cr, struct hud *h, struct rect r, double vh, double vw)
{
	double x = r.x + 1.4 * vw, w = r.w - 2.8 * vw, y = r.y + 2.2 * vh, cx = r.x + r.w / 2;
	y += panel_title(cr, x, y, w, vh, "OPÉRATEUR");

	/* avatar avec initiales */
	double d = 9 * vh, cy = y + vh + d / 2;
	cairo_arc(cr, cx, cy, d / 2, 0, 2 * M_PI);
	cairo_pattern_t *g = cairo_pattern_create_radial(cx, cy, d * 0.25, cx, cy, d / 2);
	cairo_pattern_add_color_stop_rgba(g, 0, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0);
	cairo_pattern_add_color_stop_rgba(g, 1, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.3);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	glow_stroke(cr, C_LINE, 2, 1.2);
	char ini[8] = "--", up[64];
	upper_ascii(up, sizeof up, h->user[0] ? h->user : "--");
	snprintf(ini, sizeof ini, "%.2s", up);
	double iw, ih;
	struct tstyle ist = tsl(3.4 * vh, C_BRIGHT, 0.05);
	measure(cr, ist, ini, &iw, &ih);
	text(cr, ist, cx, cy - ih / 2, 1, 0, ini);
	y = cy + d / 2 + 1.2 * vh;

	y += text(cr, tsl(2 * vh, C_TEXT, 0.12), cx, y, 1, w, h->user[0] ? up : "—");
	y += 0.4 * vh;
	y += text(cr, ts(1.25 * vh, C_MUTED), cx, y, 1, w, h->sys.host);

	y += 2.6 * vh;
	y += panel_title(cr, x, y, w, vh, "SYSTÈME");

	char cores[16], upt[32];
	snprintf(cores, sizeof cores, "%d", h->sys.cores);
	fmt_uptime(h->sys.uptime, upt, sizeof upt);
	const char *kv[4][2] = { { "OS", h->sys.os }, { "ARCH", h->sys.arch }, { "CŒURS", cores }, { "UPTIME", upt } };
	for (int i = 0; i < 4; i++) {
		double kh = text(cr, tsl(1.3 * vh, C_MUTED, 0.15), x, y, 0, 0, kv[i][0]);
		text(cr, ts(1.3 * vh, C_TEXT), x + w, y, 2, w * 0.62, kv[i][1]);
		y += kh + 0.8 * vh;
	}
	y += 1.2 * vh;
	meter(cr, x, y, w, vh, "CPU", h->sys.cpu_disp, h->sys.cpu);
	y += 1.2 * vh * 1.3 + 0.6 * vh + 6 + 2 * vh;
	meter(cr, x, y, w, vh, "RAM", h->sys.ram_disp, h->sys.ram);
}

/* Champ de saisie (utilisateur ou mot de passe) ; renvoie sa hauteur. */
static double field(cairo_t *cr, struct hud *h, struct output *o, int idx, double cx, double y, double fw,
		    double vh, double shake, bool focused, bool error, bool disabled, bool with_button)
{
	double px = 1.9 * vh;
	double th;
	measure(cr, tsl(px, C_TEXT, 0.35), "Ag", NULL, &th);
	double fh = th + 3 * vh;
	double x = cx - fw / 2 + shake;
	o->field_rect[idx] = (struct rect){ x, y, fw, fh };

	cairo_save(cr);
	if (disabled)
		cairo_push_group(cr);
	rrect(cr, x, y, fw, fh, 10);
	cairo_set_source_rgba(cr, 2 / 255.0, 10 / 255.0, 18 / 255.0, 0.8);
	cairo_fill_preserve(cr);
	uint32_t bc = error ? C_ERR : (focused || h->mode != MODE_GREETER) ? C_LINE : C_LINEDIM;
	glow_stroke(cr, bc, 1.5, error ? 1.6 : 1.0);

	double bw = with_button ? 56 : 0;
	if (with_button) {
		double bx = x + fw - bw;
		o->button_rect = (struct rect){ bx, y, bw, fh };
		cairo_save(cr);
		rrect(cr, x, y, fw, fh, 10);
		cairo_clip(cr);
		cairo_set_source_rgba(cr, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.08);
		cairo_rectangle(cr, bx, y, bw, fh);
		cairo_fill(cr);
		rgba(cr, C_LINEDIM, 1);
		cairo_rectangle(cr, bx, y + 1, 1, fh - 2);
		cairo_fill(cr);
		cairo_restore(cr);
		struct tstyle ast = ts(2.2 * vh, C_LINE);
		double ah;
		const char *arrow = h->authenticating ? "…" : "➜";
		measure(cr, ast, arrow, NULL, &ah);
		text(cr, ast, bx + bw / 2, y + (fh - ah) / 2, 1, 0, arrow);
	}

	/* contenu du champ */
	double tx = x + (fw - bw) / 2;
	const char *value = idx == 0 ? h->user : h->pw;
	char shown[128];
	if (idx == 1) {
		size_t n = 0, chars = 0;
		for (const char *p = h->pw; *p; p++)
			if (((unsigned char)*p & 0xC0) != 0x80)
				chars++;
		if (chars > 24)
			chars = 24;
		for (size_t i = 0; i < chars && n + 4 < sizeof shown; i++) {
			memcpy(shown + n, "•", 3);
			n += 3;
		}
		shown[n] = '\0';
	} else {
		snprintf(shown, sizeof shown, "%s", value);
	}
	double ty = y + 1.5 * vh;
	if (!value[0]) {
		text(cr, tsl(px, C_MUTED, 0.3), tx, ty, 1, fw - bw - 20, idx == 0 ? "UTILISATEUR" : "MOT DE PASSE");
	} else {
		text(cr, tsl(px, C_TEXT, idx == 1 ? 0.35 : 0.15), tx, ty, 1, fw - bw - 20, shown);
	}
	/* curseur clignotant */
	if (focused && !disabled && fmod(now_s(), 1.0) < 0.55) {
		double sw = 0;
		if (shown[0] && value[0])
			measure(cr, tsl(px, C_TEXT, idx == 1 ? 0.35 : 0.15), shown, &sw, NULL);
		double cxp = value[0] ? tx + sw / 2 + 2 : tx;
		if (!value[0])
			cxp = x + 18;
		rgba(cr, C_LINE, 0.9);
		cairo_rectangle(cr, cxp, ty, 1.5, th);
		cairo_fill(cr);
	}
	if (disabled) {
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, 0.4);
	}
	cairo_restore(cr);
	return fh;
}

static void draw_center(cairo_t *cr, struct hud *h, struct output *o, struct rect r, double vh, double t)
{
	double cx = r.x + r.w / 2, cy = r.y + r.h / 2;

	/* anneaux tournants */
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, 2 * M_PI * fmod(t, 60.0) / 60.0);
	double dash[] = { 4, 4 };
	cairo_set_dash(cr, dash, 2, 0);
	cairo_arc(cr, 0, 0, 31 * vh, 0, 2 * M_PI);
	cairo_set_source_rgba(cr, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.18);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	cairo_restore(cr);

	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, -2 * M_PI * fmod(t, 18.0) / 18.0);
	cairo_set_line_width(cr, 2);
	cairo_arc(cr, 0, 0, 25 * vh, -0.75 * M_PI, -0.25 * M_PI);
	cairo_set_source_rgba(cr, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.35);
	cairo_stroke(cr);
	cairo_arc(cr, 0, 0, 25 * vh, 0.25 * M_PI, 0.75 * M_PI);
	cairo_set_source_rgba(cr, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.15);
	cairo_stroke(cr);
	cairo_restore(cr);

	/* textes */
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char hm[8], sec[4], date[96];
	snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
	snprintf(sec, sizeof sec, "%02d", tm.tm_sec);
	snprintf(date, sizeof date, "%s %d %s %d", JOURS[tm.tm_wday], tm.tm_mday, MOIS[tm.tm_mon], tm.tm_year + 1900);

	const char *status = h->mode == MODE_GREETER ? "CONNEXION REQUISE" : "SYSTÈME VERROUILLÉ";
	struct tstyle sst = tsl(1.4 * vh, C_LINE, 0.35);
	struct tstyle cst = { FONT_CLOCK, 14 * vh, PANGO_WEIGHT_ULTRALIGHT, 0, C_BRIGHT, 1 };
	struct tstyle sec_st = { FONT_CLOCK, 4 * vh, PANGO_WEIGHT_LIGHT, 0, C_LINE, 1 };
	struct tstyle dst = tsl(1.8 * vh, C_MUTED, 0.3);
	struct tstyle mst = tsl(1.3 * vh, h->msg_kind == MSG_ERR ? C_ERR : h->msg_kind == MSG_OK ? C_OK : C_MUTED, 0.15);
	double sw, sh, cw, ch, scw, sch, dh, mh, fh;
	measure(cr, sst, status, &sw, &sh);
	measure(cr, cst, hm, &cw, &ch);
	measure(cr, sec_st, sec, &scw, &sch);
	measure(cr, dst, date, NULL, &dh);
	measure(cr, mst, "Ag", NULL, &mh);
	measure(cr, tsl(1.9 * vh, C_TEXT, 0.35), "Ag", NULL, &fh);
	fh += 3 * vh;
	int nfields = h->mode == MODE_GREETER ? 2 : 1;
	double form_h = nfields * fh + (nfields - 1) * 1.2 * vh;
	double total = sh + 2 * vh + ch + 1 * vh + dh + 5 * vh + form_h + 1.4 * vh + mh * 1.6;
	double y = cy - total / 2;

	/* statut + voyant clignotant (vert au déverrouillage) */
	double dotd = 8, gapx = 0.7 * 1.4 * vh, stw = dotd + gapx + sw;
	double sx = cx - stw / 2;
	double blink = h->unlocking ? 1 : (fmod(t, 1.4) < 0.7 ? 1 : 0.25 + 0.75 * fabs(fmod(t, 1.4) - 1.05) / 0.35);
	uint32_t dotc = h->unlocking ? C_OK : C_ERR;
	cairo_arc(cr, sx + dotd / 2, y + sh / 2, dotd / 2 + 3, 0, 2 * M_PI);
	rgba(cr, dotc, 0.25 * blink);
	cairo_fill(cr);
	cairo_arc(cr, sx + dotd / 2, y + sh / 2, dotd / 2, 0, 2 * M_PI);
	rgba(cr, dotc, blink);
	cairo_fill(cr);
	text(cr, sst, sx + dotd + gapx, y, 0, 0, status);
	y += sh + 2 * vh;

	/* horloge HH:MM + secondes, avec halo */
	double gx = cx - (cw + 0.4 * 4 * vh + scw) / 2;
	PangoLayout *cl = layout(cr, cst.family, cst.px, cst.weight, 0, hm);
	cairo_move_to(cr, gx, y);
	pango_cairo_layout_path(cr, cl);
	cairo_set_source_rgba(cr, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.10);
	cairo_set_line_width(cr, 14);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgba(cr, 79 / 255.0, 184 / 255.0, 224 / 255.0, 0.18);
	cairo_set_line_width(cr, 6);
	cairo_stroke_preserve(cr);
	rgba(cr, C_BRIGHT, 1);
	cairo_fill(cr);
	g_object_unref(cl);
	text(cr, sec_st, gx + cw + 0.4 * 4 * vh, y + ch * 0.12, 0, 0, sec);
	y += ch + 1 * vh;

	text(cr, dst, cx, y, 1, 0, date);
	y += dh + 5 * vh;

	/* formulaire */
	double fw = fmin(380, fmin(560, r.w * 0.8));
	double shake = 0, st = t - h->shake_start;
	if (h->shake_start > 0 && st < 0.4)
		shake = -8 * sin(st / 0.4 * 4 * M_PI);
	bool err = h->shake_start > 0 && st < 0.9;
	bool disabled = now_s() < h->locked_until;
	if (h->mode == MODE_GREETER) {
		y += field(cr, h, o, 0, cx, y, fw, vh, 0, h->focus == 0, false, disabled, false);
		y += 1.2 * vh;
	} else {
		o->field_rect[0] = (struct rect){ 0 };
	}
	y += field(cr, h, o, 1, cx, y, fw, vh, shake, h->focus == 1 || h->mode != MODE_GREETER, err, disabled, true);
	y += 1.4 * vh;
	text(cr, mst, cx, y, 1, r.w * 0.9, h->msg);
}

static void draw_right_top(cairo_t *cr, struct hud *h, struct rect r, double vh, double vw, double t)
{
	double x = r.x + 1.4 * vw, w = r.w - 2.8 * vw, y = r.y + 2.2 * vh;
	y += panel_title(cr, x, y, w, vh, "ÉNERGIE");

	char val[16];
	if (h->sys.bat >= 0)
		snprintf(val, sizeof val, "%d%%", h->sys.bat);
	else
		snprintf(val, sizeof val, "--%%");
	struct tstyle vst = ts(3 * vh, C_TEXT);
	double vw_, vh_;
	measure(cr, vst, val, &vw_, &vh_);
	double cellh = 3 * vh, cellw = w - vw_ - 0.8 * vw - 6;
	if (cellw < 20)
		cellw = 20;
	double cy = y + (vh_ - cellh) / 2;
	rrect(cr, x, cy, cellw, cellh, 4);
	rgba(cr, C_LINE, 1);
	cairo_set_line_width(cr, 1.5);
	cairo_stroke(cr);
	cairo_rectangle(cr, x + cellw + 1, cy + cellh * 0.3, 4, cellh * 0.4);
	cairo_fill(cr);
	double fw = (cellw - 6) * clampd(h->sys.bat_disp, 0, 100) / 100.0;
	if (fw > 0.5) {
		rgba(cr, C_LINE, 0.3);
		rrect(cr, x + 2, cy + 2, fw + 2, cellh - 4, 3);
		cairo_fill(cr);
		rgba(cr, C_LINE, 1);
		rrect(cr, x + 3, cy + 3, fw, cellh - 6, 2);
		cairo_fill(cr);
	}
	text(cr, vst, x + w, y, 2, 0, val);
	y += vh_ + 1 * vh;
	const char *state = h->sys.bat < 0 ? "NON DISPONIBLE" : h->sys.charging ? "EN CHARGE ⚡" : "SUR BATTERIE";
	y += text(cr, tsl(clampd(1.2 * vh, 10, 13), C_MUTED, 0.1), x, y, 0, w, state);

	y += 2.6 * vh;
	y += panel_title(cr, x, y, w, vh, "RÉSEAU");
	struct tstyle nst = tsl(1.6 * vh, C_TEXT, 0.15);
	double nh;
	measure(cr, nst, "EN LIGNE", NULL, &nh);
	bool on = h->sys.online;
	double a = on ? 1 : (fmod(t, 1.4) < 0.7 ? 1 : 0.25);
	cairo_arc(cr, x + 4, y + nh / 2, 7, 0, 2 * M_PI);
	rgba(cr, on ? C_OK : C_ERR, 0.25 * a);
	cairo_fill(cr);
	cairo_arc(cr, x + 4, y + nh / 2, 4, 0, 2 * M_PI);
	rgba(cr, on ? C_OK : C_ERR, a);
	cairo_fill(cr);
	text(cr, nst, x + 8 + 0.7 * 1.6 * vh, y, 0, w, on ? "EN LIGNE" : "HORS LIGNE");
}

static void draw_calendar(cairo_t *cr, struct rect r, double vh, double vw)
{
	double x = r.x + 1.4 * vw, w = r.w - 2.8 * vw, y = r.y + 2.2 * vh;
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char title[48];
	snprintf(title, sizeof title, "%s %d", MOIS[tm.tm_mon], tm.tm_year + 1900);
	y += panel_title(cr, x, y, w, vh, title);

	struct tm first = tm;
	first.tm_mday = 1;
	mktime(&first);
	int offset = (first.tm_wday + 6) % 7; /* lundi = 0 */
	struct tm last = tm;
	last.tm_mon += 1;
	last.tm_mday = 0;
	mktime(&last);
	int days = last.tm_mday;

	double cw = w / 7, px = 1.2 * vh;
	double th;
	measure(cr, ts(px, C_TEXT), "30", NULL, &th);
	double rowh = th + 0.7 * vh, rgap = 0.35 * vh;
	static const char *H7[] = { "L", "M", "M", "J", "V", "S", "D" };
	for (int i = 0; i < 7; i++)
		text(cr, tsl(px, C_MUTED, 0.05), x + cw * i + cw / 2, y, 1, 0, H7[i]);
	y += th + rgap;
	for (int d = 1; d <= days; d++) {
		int idx = offset + d - 1;
		int col = idx % 7, row = idx / 7;
		double ccx = x + cw * col + cw / 2, cy = y + row * (rowh + rgap);
		char num[12];
		snprintf(num, sizeof num, "%d", d);
		if (d == tm.tm_mday) {
			rrect(cr, x + cw * col + 1, cy, cw - 2, rowh, 4);
			rgba(cr, C_LINE, 0.35);
			cairo_set_line_width(cr, 4);
			cairo_stroke_preserve(cr);
			rgba(cr, C_LINE, 1);
			cairo_fill(cr);
			struct tstyle b = { FONT_MONO, px, PANGO_WEIGHT_BOLD, 0, C_BG, 1 };
			text(cr, b, ccx, cy + 0.35 * vh, 1, 0, num);
		} else {
			text(cr, ts(px, C_TEXT), ccx, cy + 0.35 * vh, 1, 0, num);
		}
	}
}

static void draw_bottom(cairo_t *cr, struct hud *h, struct rect r, double vh, double vw, double t)
{
	double x = r.x + 1.4 * vw, w = r.w - 2.8 * vw, y = r.y + 2.2 * vh;
	y += panel_title(cr, x, y, w, vh, "JOURNAL D'ÉVÉNEMENTS");

	/* pied : « verrouillé depuis » + indication */
	struct tstyle fst = tsl(1.2 * vh, C_MUTED, 0.2);
	double fh;
	measure(cr, fst, "Ag", NULL, &fh);
	double fy = r.y + r.h - 2.2 * vh - fh;
	rgba(cr, C_LINEDIM, 1);
	cairo_rectangle(cr, x, fy - 1 * vh, w, 1);
	cairo_fill(cr);
	char since[64];
	long mins = (time(NULL) - h->locked_at) / 60;
	if (h->mode == MODE_GREETER)
		snprintf(since, sizeof since, "EN ATTENTE DEPUIS %ld MIN", mins);
	else
		snprintf(since, sizeof since, "VERROUILLÉ DEPUIS %ld MIN", mins);
	text(cr, fst, x, fy, 0, 0, since);
	text(cr, fst, x + w, fy, 2, 0, h->mode == MODE_DEV ? "MODE TEST — ÉCHAP POUR QUITTER" : "ACCÈS RESTREINT");

	/* journal aligné en bas, comme `justify-content: flex-end` */
	double lpx = 1.4 * vh, lh = lpx * 1.9;
	double bottom = fy - 2 * vh;
	int fit = (int)((bottom - y) / lh);
	if (fit < 0)
		fit = 0;
	int startn = h->log_n > fit ? h->log_n - fit : 0;
	double ly = bottom - (h->log_n - startn) * lh;
	struct tstyle tsst = ts(lpx, C_LINE);
	for (int i = startn; i < h->log_n; i++, ly += lh) {
		struct log_entry *e = &h->log[i];
		double age = t - e->born, p = clampd(age / 0.3, 0, 1);
		double off = -10 * (1 - p);
		cairo_save(cr);
		cairo_push_group(cr);
		char ts_[16];
		snprintf(ts_, sizeof ts_, "[%s]", e->ts);
		double tw;
		measure(cr, tsst, ts_, &tw, NULL);
		double ty = ly + (lh - lpx * 1.2) / 2;
		text(cr, tsst, x + off, ty, 0, 0, ts_);
		uint32_t col = e->kind == MSG_ERR ? C_ERR : e->kind == MSG_OK ? C_OK : C_MUTED;
		text(cr, ts(lpx, col), x + off + tw + lpx, ty, 0, w - tw - lpx, e->text);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, p);
		cairo_restore(cr);
	}
}

/* ------------------------------ image -------------------------------- */

static void draw_panel_content(cairo_t *cr, struct hud *h, struct output *o, int i, struct rect r,
			       double vh, double vw, double t)
{
	cairo_save(cr);
	rrect(cr, r.x, r.y, r.w, r.h, 14);
	cairo_clip(cr);
	switch (i) {
	case 0: draw_left(cr, h, r, vh, vw); break;
	case 1: draw_center(cr, h, o, r, vh, t); break;
	case 2: draw_right_top(cr, h, r, vh, vw, t); break;
	case 3: draw_calendar(cr, r, vh, vw); break;
	case 4: draw_bottom(cr, h, r, vh, vw, t); break;
	}
	cairo_restore(cr);
}

void draw_frame(struct hud *h, struct output *o, cairo_t *cr, int W, int Hpx)
{
	double t = now_s();
	double vh = Hpx / 100.0, vw = W / 100.0;
	struct rect p[5];
	grid(W, Hpx, p);
	ensure_bg_cache(o, cr, W, Hpx, p);

	double unlock_p = h->unlocking ? clampd((t - h->unlock_start) / 0.7, 0, 1) : 0;
	if (h->unlocking)
		cairo_push_group(cr);

	double boot = t - h->start;
	static const double delays[5] = { 0.05, 0.15, 0.25, 0.35, 0.45 };
	if (boot >= 1.3) {
		cairo_set_source_surface(cr, o->bg_cache, 0, 0);
		cairo_paint(cr);
		for (int i = 0; i < 5; i++)
			draw_panel_content(cr, h, o, i, p[i], vh, vw, t);
	} else {
		/* animation « boot » : apparition, léger zoom et flash lumineux */
		draw_background(cr, W, Hpx);
		for (int i = 0; i < 5; i++) {
			double k = clampd((boot - delays[i]) / 0.8, 0, 1);
			if (k <= 0)
				continue;
			double alpha = clampd(k / 0.6, 0, 1);
			double ease = 1 - pow(1 - k, 3);
			double sc = 0.98 + 0.02 * ease;
			struct rect r = p[i];
			cairo_save(cr);
			cairo_push_group(cr);
			cairo_translate(cr, r.x + r.w / 2, r.y + r.h / 2);
			cairo_scale(cr, sc, sc);
			cairo_translate(cr, -(r.x + r.w / 2), -(r.y + r.h / 2));
			draw_panel_frame(cr, r);
			draw_panel_content(cr, h, o, i, r, vh, vw, t);
			double flash = 0.55 * (1 - ease);
			if (flash > 0.01) {
				rrect(cr, r.x, r.y, r.w, r.h, 14);
				cairo_set_source_rgba(cr, 0.75, 0.93, 1.0, flash);
				cairo_fill(cr);
			}
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, alpha);
			cairo_restore(cr);
		}
	}

	if (h->unlocking) {
		/* sortie : fondu, zoom 1.03, éclaircissement */
		cairo_pattern_t *pat = cairo_pop_group(cr);
		rgba(cr, C_BG, 1);
		cairo_paint(cr);
		cairo_save(cr);
		double sc = 1 + 0.03 * unlock_p;
		cairo_translate(cr, W / 2.0, Hpx / 2.0);
		cairo_scale(cr, sc, sc);
		cairo_translate(cr, -W / 2.0, -Hpx / 2.0);
		cairo_set_source(cr, pat);
		cairo_paint_with_alpha(cr, 1 - unlock_p);
		cairo_restore(cr);
		cairo_pattern_destroy(pat);
		cairo_set_source_rgba(cr, 0.75, 0.93, 1.0, 0.25 * unlock_p * (1 - unlock_p));
		cairo_paint(cr);
	}
}
