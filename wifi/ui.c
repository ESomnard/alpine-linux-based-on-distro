/* plume-wifi — dessin du panneau et clics. */
#include "wifi.h"
#include "theme.h"
#include "fenetre.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

static bool hover(struct rect r)
{
	return A.px >= r.x && A.px <= r.x + r.w && A.py >= r.y && A.py <= r.y + r.h;
}

static double list_top, list_bot;

static void add_hit(struct rect r, int act, int arg, bool clipped)
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
static void button(cairo_t *cr, struct rect r, const char *label, bool on, bool danger, int act, int arg, bool clipped)
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
static void bars(cairo_t *cr, double x, double cy, int dbm, bool lit)
{
	int lvl = dbm >= -60 ? 4 : dbm >= -67 ? 3 : dbm >= -75 ? 2 : 1;
	for (int i = 0; i < 4; i++) {
		double h = 5 + i * 4;
		cairo_rectangle(cr, x + i * 6, cy + 9 - h, 4, h);
		rgba(cr, i < lvl ? (lit ? C_CORNER : C_LINE) : C_LINEDIM, i < lvl ? 1 : 0.6);
		cairo_fill(cr);
	}
}

static const char *sec_label(const char *s)
{
	if (!strcmp(s, "open")) return "OUVERT";
	if (!strcmp(s, "psk")) return "WPA";
	if (!strcmp(s, "8021x")) return "ENTREPRISE";
	if (!strcmp(s, "wep")) return "WEP";
	return s;
}

static bool needs_pass(const struct net *n)
{
	return !n->known && strcmp(n->sec, "open");
}

static void tag(cairo_t *cr, double *x, double cy, const char *s, uint32_t col)
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

static void spinner(cairo_t *cr, double cx, double cy, double r)
{
	double a = fmod(now_s() * 5, 2 * M_PI);
	cairo_new_path(cr);
	cairo_arc(cr, cx, cy, r, a, a + 4.2);
	rgba(cr, C_LINE, 1);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);
}

static void message(cairo_t *cr, double W, double y, const char *t1, const char *t2)
{
	txt(cr, 15, 700, 0.12, C_TEXT, 1, W / 2, y, 1, W - 2 * MARGIN, t1);
	if (t2)
		txt(cr, 12, 400, 0, C_MUTED, 1, W / 2, y + 26, 1, W - 2 * MARGIN, t2);
}

#define ROW_H 54.0
#define OPEN_H 104.0

void ui_draw(cairo_t *cr, int Wi, int Hi)
{
	double W = Wi, H = Hi;
	A.nhits = 0;
	rgba(cr, C_BG, 1);
	cairo_paint(cr);

	/* en-tête */
	cairo_rectangle(cr, 0, 0, W, HEADER_H);
	rgba(cr, C_PANEL, 1);
	cairo_fill(cr);
	cairo_move_to(cr, 0, HEADER_H - 0.5);
	cairo_line_to(cr, W, HEADER_H - 0.5);
	glow_stroke(cr, C_LINE, 1, 0.6);
	pf_draw(cr, MARGIN, HEADER_H / 2, A.px, A.py);
	txt(cr, 20, 800, 0.2, C_BRIGHT, 1, MARGIN + PF_LARGEUR, HEADER_H / 2, 0, 0, "◇ WIFI");
	if (A.dev[0])
		txt(cr, 11, 400, 0.1, C_MUTED, 1, MARGIN + PF_LARGEUR + 106, HEADER_H / 2 + 2, 0, 0, A.dev);
	double bx = W - MARGIN;
	if (A.have_dev) {
		struct rect r = { bx - 112, HEADER_H / 2 - 15, 112, 30 };
		button(cr, r, A.powered ? "● ACTIVÉ" : "○ DÉSACTIVÉ", A.powered, false, A_POWER, 0, false);
		bx = r.x - 10;
		if (A.powered) {
			struct rect s = { bx - 44, r.y, 44, 30 };
			if (A.scanning) {
				rrect(cr, s.x, s.y, s.w, s.h, 3);
				rgba(cr, C_LINEDIM, 1);
				cairo_set_line_width(cr, 1);
				cairo_stroke(cr);
				spinner(cr, s.x + s.w / 2, s.y + s.h / 2, 7);
			} else {
				button(cr, s, "⟳", false, false, A_SCAN, 0, false);
			}
		}
	}

	/* état de la connexion */
	double y = HEADER_H + 14;
	char line[300];
	uint32_t col = C_MUTED;
	if (A.no_iwctl)
		snprintf(line, sizeof line, "iwd n'est pas installé");
	else if (!A.loaded)
		snprintf(line, sizeof line, "Lecture de l'état du wifi…");
	else if (!A.have_dev)
		snprintf(line, sizeof line, "Aucune carte wifi détectée");
	else if (!A.powered)
		snprintf(line, sizeof line, "○ Wifi désactivé");
	else if (A.cur[0]) {
		snprintf(line, sizeof line, "● CONNECTÉ — %s%s%s", A.cur, A.ip[0] ? "  ·  " : "", A.ip);
		col = C_OK;
	} else if (!strcmp(A.state, "connecting"))
		snprintf(line, sizeof line, "◌ Connexion en cours…");
	else
		snprintf(line, sizeof line, "○ Non connecté");
	rrect(cr, MARGIN, y, W - 2 * MARGIN, 40, 3);
	rgba(cr, C_PANEL, 1);
	cairo_fill_preserve(cr);
	rgba(cr, col == C_OK ? C_OK : C_LINEDIM, col == C_OK ? 0.8 : 1);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	txt(cr, 13, 700, 0.06, col == C_OK ? C_OK : C_TEXT, 1, MARGIN + 14, y + 20, 0, W - 2 * MARGIN - 50, line);
	if (A.busy)
		spinner(cr, W - MARGIN - 20, y + 20, 7);
	y += 56;

	txt(cr, 11, 700, 0.2, C_LINE, 1, MARGIN, y + 6, 0, 0, "RÉSEAUX DISPONIBLES");
	y += 22;
	list_top = y;
	list_bot = H - STATUS_H;

	if (A.no_iwctl) {
		message(cr, W, y + 60, "Le wifi de Plume passe par iwd.", access("/var/db/xbps", F_OK) == 0 ?
			"Installe-le : sudo xbps-install -y iwd && sudo ln -s /etc/sv/iwd /var/service/" : "Installe-le : sudo apk add iwd && sudo rc-service iwd start");
	} else if (!A.loaded) {
		spinner(cr, W / 2, y + 60, 12);
	} else if (!A.have_dev) {
		message(cr, W, y + 60, "Pas de carte wifi sur cette machine.", "Une prise réseau (câble) fonctionne sans réglage.");
	} else if (!A.powered) {
		message(cr, W, y + 60, "Le wifi est désactivé.", "Clique sur « DÉSACTIVÉ » en haut pour le rallumer.");
	} else if (A.nnets == 0) {
		message(cr, W, y + 60, A.scanning ? "Recherche des réseaux…" : "Aucun réseau trouvé.", A.scanning ? NULL : "Clique sur ⟳ pour chercher à nouveau.");
	}

	/* liste des réseaux (défile) */
	cairo_save(cr);
	cairo_rectangle(cr, 0, list_top, W, list_bot - list_top);
	cairo_clip(cr);
	double cy = list_top - A.scroll;
	for (int i = 0; A.have_dev && A.powered && i < A.nnets; i++) {
		struct net *n = &A.nets[i];
		bool open = A.sel == i;
		double h = open ? OPEN_H + (A.ask_pass ? 46 : 0) : ROW_H;
		struct rect r = { MARGIN, cy, W - 2 * MARGIN, h - 6 };
		bool hv = hover((struct rect){ r.x, r.y, r.w, ROW_H - 6 }) && A.py >= list_top && A.py < list_bot;
		rrect(cr, r.x, r.y, r.w, r.h, 3);
		rgba(cr, n->connected ? C_LINE : C_PANEL, n->connected ? 0.10 : 1);
		cairo_fill_preserve(cr);
		if (open || hv)
			glow_stroke(cr, C_LINE, 1, 0.6);
		else {
			rgba(cr, n->connected ? C_LINE : C_LINEDIM, n->connected ? 0.7 : 1);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
		}
		double my = r.y + (ROW_H - 6) / 2;
		bars(cr, r.x + 14, my - 3, n->dbm, n->connected);
		double tx = r.x + r.w - 12;
		if (n->connected)
			tag(cr, &tx, my, "CONNECTÉ", C_OK);
		else if (n->known)
			tag(cr, &tx, my, "★ CONNU", C_LINE);
		tag(cr, &tx, my, sec_label(n->sec), strcmp(n->sec, "open") ? C_MUTED : C_LINE);
		txt(cr, 14, n->connected ? 700 : 500, 0, n->connected ? C_BRIGHT : C_TEXT, 1, r.x + 52, my, 0,
		    tx - r.x - 60, n->name);
		add_hit((struct rect){ r.x, r.y, r.w, ROW_H - 6 }, A_NET, i, true);

		if (open) {
			double by = r.y + ROW_H;
			if (A.ask_pass) {
				struct rect f = { r.x + 14, by, r.w - 28 - 70, 34 };
				rrect(cr, f.x, f.y, f.w, f.h, 3);
				rgba(cr, C_BG, 1);
				cairo_fill_preserve(cr);
				glow_stroke(cr, C_LINE, 1, 0.5);
				char shown[260] = "";
				if (A.show_pass)
					snprintf(shown, sizeof shown, "%s", A.pass);
				else
					for (size_t k = 0, m = strlen(A.pass); k < m && k < 120; k++)
						if (((unsigned char)A.pass[k] & 0xc0) != 0x80)
							strcat(shown, "•");
				if (!A.pass[0])
					txt(cr, 13, 400, 0, C_MUTED, 1, f.x + 10, f.y + f.h / 2, 0, f.w - 20, "Mot de passe du réseau");
				double w = A.pass[0] ? txt(cr, 13, 500, 0.04, C_BRIGHT, 1, f.x + 10, f.y + f.h / 2, 0, f.w - 24, shown) : 0;
				if (fmod(now_s(), 1.0) < 0.6) {
					cairo_rectangle(cr, f.x + 11 + fmin(w, f.w - 24), f.y + 8, 2, f.h - 16);
					rgba(cr, C_CORNER, 1);
					cairo_fill(cr);
				}
				add_hit(f, A_FIELD, i, true);
				button(cr, (struct rect){ f.x + f.w + 8, f.y, 62, 34 }, A.show_pass ? "CACHER" : "VOIR", A.show_pass, false,
				       A_SHOWPASS, i, true);
				by += 46;
			}
			double bw = 150;
			struct rect b1 = { r.x + 14, by, bw, 34 };
			if (n->connected)
				button(cr, b1, "DÉCONNECTER", false, false, A_DISCONNECT, i, true);
			else
				button(cr, b1, A.busy ? "CONNEXION…" : "SE CONNECTER", false, false, A_CONNECT, i, true);
			if (n->known)
				button(cr, (struct rect){ b1.x + bw + 10, by, 120, 34 }, "OUBLIER", false, true, A_FORGET, i, true);
		}
		cy += h;
	}
	A.content_h = cy + A.scroll - list_top;
	cairo_restore(cr);

	/* barre d'état */
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
		txt(cr, 11, 400, 0.06, C_MUTED, 1, MARGIN, H - STATUS_H / 2, 0, W - 2 * MARGIN,
		    "Clic : choisir  ·  Entrée : connecter  ·  Échap : fermer");
}

static void clamp_scroll(void)
{
	double max = A.content_h - (list_bot - list_top);
	A.scroll = clampd(A.scroll, 0, max > 0 ? max : 0);
}

void ui_scroll(double dy)
{
	A.scroll += dy;
	clamp_scroll();
	A.dirty = true;
}

static void do_connect(int i)
{
	if (i < 0 || i >= A.nnets || A.busy)
		return;
	struct net *n = &A.nets[i];
	if (!strcmp(n->sec, "8021x") && !n->known) {
		set_toast(true, "Réseau d'entreprise : à configurer dans /var/lib/iwd (voir iwd.network)");
		return;
	}
	if (needs_pass(n) && !A.pass[0]) {
		A.ask_pass = true;
		set_toast(false, "Tape le mot de passe puis Entrée");
		return;
	}
	iwd_connect(n->name, A.ask_pass ? A.pass : NULL);
	set_toast(false, "Connexion à %s…", n->name);
}

void ui_click(double x, double y)
{
	switch (pf_clic(x, y)) {
	case 0: break;
	case 1: A.running = false; return;
	default: return;
	}
	for (int k = A.nhits - 1; k >= 0; k--) {
		struct hit *h = &A.hits[k];
		if (x < h->r.x || x > h->r.x + h->r.w || y < h->r.y || y > h->r.y + h->r.h)
			continue;
		switch (h->act) {
		case A_NET:
			if (A.sel == h->arg) {
				A.sel = -1;
			} else {
				A.sel = h->arg;
				A.ask_pass = !A.nets[h->arg].connected && needs_pass(&A.nets[h->arg]);
			}
			memset(A.pass, 0, sizeof A.pass);
			A.show_pass = false;
			break;
		case A_CONNECT: do_connect(h->arg); break;
		case A_DISCONNECT: if (!A.busy) iwd_disconnect(); break;
		case A_FORGET: iwd_forget(A.nets[h->arg].name); break;
		case A_POWER: if (!A.busy) iwd_power(!A.powered); break;
		case A_SCAN: iwd_refresh(true); break;
		case A_SHOWPASS: A.show_pass = !A.show_pass; break;
		case A_FIELD: break;
		}
		A.dirty = true;
		return;
	}
	if (y < HEADER_H) /* double-clic dans le vide de l'en-tête : plein écran */
		pf_entete_clic(x, y);
}

void ui_key(xkb_keysym_t sym, const char *utf8, bool ctrl)
{
	A.dirty = true;
	if (ctrl && (sym == XKB_KEY_q || sym == XKB_KEY_Q)) {
		A.running = false;
		return;
	}
	if (sym == XKB_KEY_Escape) {
		if (A.sel >= 0) {
			A.sel = -1;
			A.ask_pass = false;
			memset(A.pass, 0, sizeof A.pass);
		} else {
			A.running = false;
		}
		return;
	}
	if (sym == XKB_KEY_F5 || (ctrl && (sym == XKB_KEY_r || sym == XKB_KEY_R))) {
		iwd_refresh(true);
		return;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (A.sel >= 0 && !A.nets[A.sel].connected)
			do_connect(A.sel);
		return;
	}
	if (A.sel >= 0 && A.ask_pass) {
		size_t n = strlen(A.pass);
		if (sym == XKB_KEY_BackSpace) {
			while (n > 0 && ((unsigned char)A.pass[n - 1] & 0xc0) == 0x80)
				A.pass[--n] = 0;
			if (n > 0)
				A.pass[--n] = 0;
		} else if (!ctrl && utf8[0] && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f &&
			   n + strlen(utf8) < sizeof A.pass - 1) {
			strcat(A.pass, utf8);
		}
		return;
	}
	/* sans champ ouvert : flèches pour choisir un réseau */
	if (sym == XKB_KEY_Down || sym == XKB_KEY_Up) {
		int s = A.sel + (sym == XKB_KEY_Down ? 1 : -1);
		if (A.sel < 0)
			s = 0;
		if (s >= 0 && s < A.nnets) {
			A.sel = s;
			A.ask_pass = false;
		}
	} else if (sym == XKB_KEY_Next)
		ui_scroll(300);
	else if (sym == XKB_KEY_Prior)
		ui_scroll(-300);
}
