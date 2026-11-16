/* plume-bluetooth — dessin du panneau et interactions. */
#include "panel.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void bt_connect(struct dev *d);
void bt_disconnect(struct dev *d);
void bt_pair(struct dev *d);
void bt_forget(struct dev *d);
void bt_power(bool on);

#define ROW_H 54.0
#define OPEN_H 100.0

static double list_top, list_bot;

/* symbole selon le type d'appareil (champ Icon de bluez) */
static const char *glyph(const char *icon)
{
	if (strstr(icon, "audio") || strstr(icon, "headset") || strstr(icon, "headphone"))
		return "♫";
	if (strstr(icon, "keyboard"))
		return "⌨";
	if (strstr(icon, "mouse") || strstr(icon, "tablet"))
		return "◉";
	if (strstr(icon, "gaming") || strstr(icon, "joystick"))
		return "◈";
	if (strstr(icon, "phone"))
		return "▯";
	if (strstr(icon, "computer"))
		return "▭";
	return "◇";
}

static double section(cairo_t *cr, double y, const char *t)
{
	txt(cr, 11, 700, 0.2, C_LINE, 1, MARGIN, y + 8, 0, 0, t);
	return y + 24;
}

static double rows(cairo_t *cr, double W, double y, bool paired)
{
	bool any = false;
	for (int i = 0; i < A.ndevs; i++) {
		struct dev *d = &A.devs[i];
		if (d->paired != paired)
			continue;
		any = true;
		bool open = A.sel == i;
		double h = open ? OPEN_H : ROW_H;
		struct rect r = { MARGIN, y, W - 2 * MARGIN, h - 6 };
		struct rect top = { r.x, r.y, r.w, ROW_H - 6 };
		bool hv = hover(top) && A.py >= list_top && A.py < list_bot;
		rrect(cr, r.x, r.y, r.w, r.h, 3);
		rgba(cr, d->connected ? C_LINE : C_PANEL, d->connected ? 0.10 : 1);
		cairo_fill_preserve(cr);
		if (open || hv)
			glow_stroke(cr, C_LINE, 1, 0.6);
		else {
			rgba(cr, d->connected ? C_LINE : C_LINEDIM, d->connected ? 0.7 : 1);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
		}
		double my = r.y + (ROW_H - 6) / 2;
		txt(cr, 18, 400, 0, d->connected ? C_CORNER : C_LINE, 1, r.x + 24, my, 1, 0, glyph(d->icon));
		double tx = r.x + r.w - 12;
		if (!strcmp(A.busy_mac, d->mac)) {
			spinner(cr, tx - 8, my, 7);
			tx -= 26;
		} else if (d->connected)
			tag(cr, &tx, my, "CONNECTÉ", C_OK);
		if (d->battery >= 0) {
			char b[16];
			snprintf(b, sizeof b, "▮ %d %%", d->battery);
			tag(cr, &tx, my, b, d->battery <= 15 ? C_ERR : C_MUTED);
		}
		txt(cr, 14, d->connected ? 700 : 500, 0, d->connected ? C_BRIGHT : C_TEXT, 1, r.x + 46, my, 0,
		    tx - r.x - 52, d->name);
		add_hit(top, A_DEV, i, true);
		if (open) {
			struct rect b1 = { r.x + 14, r.y + ROW_H, 160, 34 };
			if (!d->paired)
				button(cr, b1, A.busy ? "ASSOCIATION…" : "ASSOCIER", false, false, A_PAIR, i, true);
			else if (d->connected)
				button(cr, b1, "DÉCONNECTER", false, false, A_DISCONNECT, i, true);
			else
				button(cr, b1, A.busy ? "CONNEXION…" : "CONNECTER", false, false, A_CONNECT, i, true);
			if (d->paired)
				button(cr, (struct rect){ b1.x + b1.w + 10, b1.y, 120, 34 }, "OUBLIER", false, true, A_FORGET, i, true);
		}
		y += h;
	}
	if (!any) {
		txt(cr, 12, 400, 0, C_MUTED, 1, MARGIN + 4, y + 10, 0, W - 2 * MARGIN,
		    paired ? "Aucun appareil associé" :
		    A.scanning ? "Recherche en cours…" : "Mets l'appareil en mode association, puis ⟳");
		y += 30;
	}
	return y + 8;
}

void ui_draw(cairo_t *cr, int Wi, int Hi)
{
	double W = Wi, H = Hi;
	A.nhits = 0;
	kit_clip(0, 1e9);
	rgba(cr, C_BG, 1);
	cairo_paint(cr);
	header(cr, W, "◇ BLUETOOTH", A.adapter);

	if (!A.no_ctl && !A.no_adapter && A.loaded) {
		struct rect r = { W - MARGIN - 112, HEADER_H / 2 - 15, 112, 30 };
		button(cr, r, A.powered ? "● ACTIVÉ" : "○ DÉSACTIVÉ", A.powered, false, A_POWER, 0, false);
		if (A.powered) {
			struct rect s = { r.x - 54, r.y, 44, 30 };
			if (A.scanning) {
				rrect(cr, s.x, s.y, s.w, s.h, 3);
				rgba(cr, C_LINEDIM, 1);
				cairo_set_line_width(cr, 1);
				cairo_stroke(cr);
				spinner(cr, s.x + s.w / 2, s.y + s.h / 2, 7);
			} else
				button(cr, s, "⟳", false, false, A_SCAN, 0, false);
		}
	}

	list_top = HEADER_H + 8;
	list_bot = H - STATUS_H;
	kit_clip(list_top, list_bot);
	if (A.no_ctl)
		message(cr, W, list_top + 80, "bluez (bluetoothctl) est introuvable.",
			"Installe-le : sudo apk add bluez && sudo rc-update add bluetooth && sudo rc-service bluetooth start");
	else if (!A.loaded)
		spinner(cr, W / 2, list_top + 80, 12);
	else if (A.no_adapter)
		message(cr, W, list_top + 80, "Pas d'adaptateur Bluetooth.",
			"Ou le service est arrêté : sudo rc-service bluetooth start");
	else if (!A.powered)
		message(cr, W, list_top + 80, "Le Bluetooth est désactivé.",
			A.blocked ? "Bloqué (touche avion ?) : « DÉSACTIVÉ » le débloque" : "Clique sur « DÉSACTIVÉ » pour l'allumer.");
	else {
		cairo_save(cr);
		cairo_rectangle(cr, 0, list_top, W, list_bot - list_top);
		cairo_clip(cr);
		double y = list_top + 8 - A.scroll;
		y = rows(cr, W, section(cr, y, "MES APPAREILS"), true);
		y = rows(cr, W, section(cr, y, "À PROXIMITÉ"), false);
		A.content_h = y + A.scroll - list_top;
		cairo_restore(cr);
	}
	statusbar(cr, W, H, "Clic : choisir  ·  Entrée : connecter  ·  Échap : fermer");
}

static void clamp_scroll(void)
{
	double max = A.content_h - (list_bot - list_top);
	A.scroll = clampd(A.scroll, 0, max > 0 ? max : 0);
}

static void activate(int i)
{
	if (i < 0 || i >= A.ndevs || A.busy)
		return;
	struct dev *d = &A.devs[i];
	if (!d->paired) {
		bt_pair(d);
		set_toast(false, "Association avec %s…", d->name);
	} else if (!d->connected) {
		bt_connect(d);
		set_toast(false, "Connexion à %s…", d->name);
	}
}

void ui_click(double x, double y)
{
	int arg = 0;
	int act = hit_at(x, y, &arg);
	if (!act)
		return;
	if (act >= A_DEV && act <= A_FORGET && (arg < 0 || arg >= A.ndevs))
		return;
	switch (act) {
	case A_DEV: A.sel = A.sel == arg ? -1 : arg; break;
	case A_PAIR:
	case A_CONNECT: activate(arg); break;
	case A_DISCONNECT: if (!A.busy) bt_disconnect(&A.devs[arg]); break;
	case A_FORGET: bt_forget(&A.devs[arg]); break;
	case A_POWER: if (!A.busy) bt_power(!A.powered); break;
	case A_SCAN: back_refresh(true); break;
	}
	A.dirty = true;
}

void ui_drag(double x, double y) { (void)x; (void)y; }
void ui_release(void) {}

void ui_scroll(double dy)
{
	A.scroll += dy;
	clamp_scroll();
	A.dirty = true;
}

void ui_key(xkb_keysym_t sym, const char *utf8, bool ctrl)
{
	(void)utf8;
	A.dirty = true;
	if (ctrl && (sym == XKB_KEY_q || sym == XKB_KEY_Q))
		A.running = false;
	else if (sym == XKB_KEY_Escape) {
		if (A.sel >= 0)
			A.sel = -1;
		else
			A.running = false;
	} else if (sym == XKB_KEY_F5 || (ctrl && (sym == XKB_KEY_r || sym == XKB_KEY_R)))
		back_refresh(true);
	else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter)
		activate(A.sel);
	else if (sym == XKB_KEY_Down || sym == XKB_KEY_Up) {
		int s = A.sel < 0 ? 0 : A.sel + (sym == XKB_KEY_Down ? 1 : -1);
		if (s >= 0 && s < A.ndevs)
			A.sel = s;
	} else if (sym == XKB_KEY_Next)
		ui_scroll(300);
	else if (sym == XKB_KEY_Prior)
		ui_scroll(-300);
}
