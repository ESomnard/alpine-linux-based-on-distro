/* plume-son — dessin du panneau et interactions. */
#include "panel.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void son_set_volume(struct node *n, double v);
void son_toggle_mute(struct node *n);
void son_set_default(struct node *n);

#define ROW_H 70.0

static double list_top, list_bot;
static struct rect sliders[MAX_NODES];

static void slider(cairo_t *cr, struct rect r, double v, bool muted, bool lit)
{
	double cy = r.y + r.h / 2;
	rrect(cr, r.x, cy - 3, r.w, 6, 3);
	rgba(cr, C_LINEDIM, 0.7);
	cairo_fill(cr);
	if (v > 0) {
		rrect(cr, r.x, cy - 3, r.w * v, 6, 3);
		rgba(cr, muted ? C_MUTED : C_LINE, 1);
		cairo_fill(cr);
	}
	double kx = r.x + r.w * v;
	cairo_arc(cr, kx, cy, lit ? 9 : 7, 0, 2 * M_PI);
	rgba(cr, muted ? C_MUTED : C_LINE, lit ? 0.35 : 0.2);
	cairo_fill(cr);
	cairo_arc(cr, kx, cy, 5, 0, 2 * M_PI);
	rgba(cr, muted ? C_MUTED : C_CORNER, 1);
	cairo_fill(cr);
}

static double section(cairo_t *cr, double y, const char *t)
{
	txt(cr, 11, 700, 0.2, C_LINE, 1, MARGIN, y + 8, 0, 0, t);
	return y + 24;
}

static double rows(cairo_t *cr, double W, double y, int kind)
{
	bool any = false;
	for (int i = 0; i < A.nnodes; i++) {
		struct node *n = &A.nodes[i];
		if (n->kind != kind)
			continue;
		any = true;
		struct rect r = { MARGIN, y, W - 2 * MARGIN, ROW_H - 8 };
		bool hv = hover(r) && A.py >= list_top && A.py < list_bot;
		rrect(cr, r.x, r.y, r.w, r.h, 3);
		rgba(cr, n->def ? C_LINE : C_PANEL, n->def ? 0.10 : 1);
		cairo_fill_preserve(cr);
		if (hv || A.drag == i)
			glow_stroke(cr, C_LINE, 1, 0.5);
		else {
			rgba(cr, n->def ? C_LINE : C_LINEDIM, n->def ? 0.7 : 1);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
		}
		/* la ligne d'abord : le bouton et le curseur, ajoutés après, passent devant au clic */
		if (kind != N_STREAM)
			add_hit(r, A_ROW, i, true);
		double ty = r.y + 18;
		double nx = r.x + 14;
		if (kind != N_STREAM) {
			/* pastille « par défaut » : clic sur la ligne pour choisir cet appareil */
			cairo_arc(cr, r.x + 20, ty, 6, 0, 2 * M_PI);
			rgba(cr, n->def ? C_LINE : C_LINEDIM, 1);
			cairo_set_line_width(cr, 1.5);
			cairo_stroke(cr);
			if (n->def) {
				cairo_arc(cr, r.x + 20, ty, 3, 0, 2 * M_PI);
				rgba(cr, C_CORNER, 1);
				cairo_fill(cr);
			}
			nx = r.x + 36;
		}
		struct rect mb = { r.x + r.w - 76, r.y + 6, 64, 24 };
		double tx = mb.x - 8;
		if (n->def)
			tag(cr, &tx, ty, "PAR DÉFAUT", C_OK);
		txt(cr, 13, n->def ? 700 : 500, 0, n->def ? C_BRIGHT : C_TEXT, 1, nx, ty, 0, tx - nx - 6, n->name);
		button(cr, mb, n->muted ? "MUET" : "SON", n->muted, n->muted, A_MUTE, i, true);

		char pc[16];
		double v = n->vol < 0 ? 0 : n->vol;
		snprintf(pc, sizeof pc, "%d %%", (int)lround(v * 100));
		struct rect s = { r.x + 14, r.y + 34, r.w - 28 - 58, 22 };
		slider(cr, s, v, n->muted, A.drag == i || hover(s));
		txt(cr, 12, 700, 0.04, n->muted ? C_MUTED : C_TEXT, 1, r.x + r.w - 14, s.y + s.h / 2, 2, 0, pc);
		sliders[i] = s;
		add_hit((struct rect){ s.x - 8, s.y - 4, s.w + 16, s.h + 8 }, A_SLIDER, i, true);
		y += ROW_H;
	}
	if (!any) {
		txt(cr, 12, 400, 0, C_MUTED, 1, MARGIN + 4, y + 10, 0, W - 2 * MARGIN,
		    kind == N_STREAM ? "Aucune application ne joue de son" :
		    kind == N_SINK ? "Aucune sortie son" : "Aucun micro");
		y += 30;
	}
	return y + 8;
}

void ui_draw(cairo_t *cr, int Wi, int Hi)
{
	double W = Wi, H = Hi;
	A.nhits = 0;
	rgba(cr, C_BG, 1);
	cairo_paint(cr);
	header(cr, W, "◇ SON", "PipeWire");

	list_top = HEADER_H + 8;
	list_bot = H - STATUS_H;
	kit_clip(list_top, list_bot);
	if (A.no_wpctl) {
		message(cr, W, list_top + 80, "PipeWire (wpctl) est introuvable.", access("/var/db/xbps", F_OK) == 0 ?
			"Installe-le : sudo xbps-install -y pipewire wireplumber" : "Installe-le : sudo apk add pipewire wireplumber");
	} else if (!A.loaded) {
		spinner(cr, W / 2, list_top + 80, 12);
	} else {
		cairo_save(cr);
		cairo_rectangle(cr, 0, list_top, W, list_bot - list_top);
		cairo_clip(cr);
		double y = list_top + 8 - A.scroll;
		y = rows(cr, W, section(cr, y, "SORTIE"), N_SINK);
		y = rows(cr, W, section(cr, y, "ENTRÉE (MICRO)"), N_SOURCE);
		y = rows(cr, W, section(cr, y, "APPLICATIONS"), N_STREAM);
		A.content_h = y + A.scroll - list_top;
		cairo_restore(cr);
	}
	statusbar(cr, W, H, "Glisser ou molette : volume  ·  M : muet  ·  Échap : fermer");
}

static void clamp_scroll(void)
{
	double max = A.content_h - (list_bot - list_top);
	A.scroll = clampd(A.scroll, 0, max > 0 ? max : 0);
}

static void drag_to(double x)
{
	if (A.drag < 0 || A.drag >= A.nnodes)
		return;
	son_set_volume(&A.nodes[A.drag], (x - A.drag_r.x) / A.drag_r.w);
	A.dirty = true;
}

void ui_click(double x, double y)
{
	int arg = 0;
	int act = hit_at(x, y, &arg);
	if (!act || arg < 0 || arg >= A.nnodes)
		return;
	struct node *n = &A.nodes[arg];
	switch (act) {
	case A_SLIDER:
		A.drag = arg;
		A.drag_r = sliders[arg];
		drag_to(x);
		break;
	case A_MUTE:
		son_toggle_mute(n);
		break;
	case A_ROW:
		if (!n->def)
			son_set_default(n);
		break;
	}
	A.dirty = true;
}

void ui_drag(double x, double y)
{
	(void)y;
	drag_to(x);
}

void ui_release(void)
{
	A.drag = -1;
}

void ui_scroll(double dy)
{
	/* molette sur un curseur : volume ; ailleurs : défilement */
	int arg = 0;
	if (hit_at(A.px, A.py, &arg) == A_SLIDER && arg < A.nnodes) {
		struct node *n = &A.nodes[arg];
		son_set_volume(n, (n->vol < 0 ? 0 : n->vol) + (dy < 0 ? 0.05 : -0.05));
	} else {
		A.scroll += dy;
		clamp_scroll();
	}
	A.dirty = true;
}

static struct node *default_sink(void)
{
	for (int i = 0; i < A.nnodes; i++)
		if (A.nodes[i].kind == N_SINK && A.nodes[i].def)
			return &A.nodes[i];
	return NULL;
}

void ui_key(xkb_keysym_t sym, const char *utf8, bool ctrl)
{
	(void)utf8;
	A.dirty = true;
	struct node *d = default_sink();
	if (sym == XKB_KEY_Escape || (ctrl && (sym == XKB_KEY_q || sym == XKB_KEY_Q)))
		A.running = false;
	else if (d && (sym == XKB_KEY_Up || sym == XKB_KEY_Right || sym == XKB_KEY_plus || sym == XKB_KEY_KP_Add))
		son_set_volume(d, d->vol + 0.05);
	else if (d && (sym == XKB_KEY_Down || sym == XKB_KEY_Left || sym == XKB_KEY_minus || sym == XKB_KEY_KP_Subtract))
		son_set_volume(d, d->vol - 0.05);
	else if (d && (sym == XKB_KEY_m || sym == XKB_KEY_M))
		son_toggle_mute(d);
	else if (sym == XKB_KEY_Next)
		ui_scroll(300);
	else if (sym == XKB_KEY_Prior)
		ui_scroll(-300);
}
