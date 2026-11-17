/* Logithèque Plume — dessin et interactions (style HUD). */
#include "logitheque.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define C_BG      0x02060b
#define C_PANEL   0x07121c
#define C_TEXT    0xcdeefc
#define C_MUTED   0x5f8ea5
#define C_OK      0x4fe0a4
#define C_ERR     0xff5470
#define C_WARN    0xe6c86e
#define C_PURP    0xb48cff
#define C_PINK    0xff8fb1
#define C_CYAN    0x6fe3ff
#define C_BRIGHT  0xe9f9ff
#define FONT      "JetBrains Mono, DejaVu Sans Mono, WenQuanYi Zen Hei"

#define HEADER_ROW 64.0
/* hauteur de l'en-tête : une ligne, ou deux si la fenêtre est étroite */
static double HEADER_H = 64.0;
#define STATUS_H 40.0
#define MARGIN   24.0

static const char *CATS[] = { "Internet", "Bureautique", "Multimédia", "Graphisme",
			      "Jeux", "Développement", "Utilitaires", "Système" };
static const char *CAT_GLYPH[] = { "◎", "▤", "▶", "▣", "◆", "⌘", "⚙", "◈" };
static const uint32_t CAT_COL[] = { 0x4fb8e0, C_OK, C_PURP, C_PINK, C_WARN, C_CYAN, 0x9fe4ff, C_MUTED };
#define NCATS (int)(sizeof CATS / sizeof CATS[0])

/* paquets dont Plume a besoin : jamais proposés à la suppression */
static char protected_pkgs[8192];

/* ------------------------------ outils ------------------------------- */

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

/* texte sur une ligne, centré verticalement sur cy ; renvoie la largeur */
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

/* paragraphe sur plusieurs lignes (au plus maxlines) à partir du haut y ; renvoie la hauteur */
static double para(cairo_t *cr, double px, uint32_t col, double x, double y, double w, int maxlines, const char *s)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(l, s, -1);
	pango_layout_set_width(l, (int)(w * PANGO_SCALE));
	pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
	if (maxlines > 0) {
		pango_layout_set_height(l, -maxlines);
		pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
	}
	int tw, th;
	pango_layout_get_pixel_size(l, &tw, &th);
	cairo_move_to(cr, x, y);
	rgba(cr, col, 1);
	pango_cairo_show_layout(cr, l);
	g_object_unref(l);
	return th;
}

static bool hover(struct rect r)
{
	return A.px >= r.x && A.px <= r.x + r.w && A.py >= r.y && A.py <= r.y + r.h;
}

/* zone de contenu visible (pour ne pas enregistrer de clics hors champ) */
static struct rect content_clip;

static void add_hit(struct rect r, enum action act, int arg, const struct item *it)
{
	if (A.nhits >= MAX_HITS)
		return;
	struct hit *h = &A.hits[A.nhits++];
	h->r = r;
	h->act = act;
	h->arg = arg;
	if (it)
		h->item = *it;
	else
		memset(&h->item, 0, sizeof h->item);
}

static void add_content_hit(struct rect r, enum action act, int arg, const struct item *it)
{
	/* coupe le rectangle à la zone visible */
	double y0 = fmax(r.y, content_clip.y), y1 = fmin(r.y + r.h, content_clip.y + content_clip.h);
	if (y1 <= y0)
		return;
	add_hit((struct rect){ r.x, y0, r.w, y1 - y0 }, act, arg, it);
}

/* bouton ; style 0 discret, 1 principal (lumineux), 2 danger */
static void button(cairo_t *cr, struct rect r, const char *label, int style, bool enabled, enum action act,
		   int arg, const struct item *it, bool in_content)
{
	bool hov = enabled && hover(r);
	uint32_t col = style == 2 ? C_ERR : C_LINE;
	rrect(cr, r.x, r.y, r.w, r.h, 8);
	rgba(cr, col, !enabled ? 0.03 : style == 1 ? (hov ? 0.34 : 0.22) : (hov ? 0.14 : 0.05));
	cairo_fill_preserve(cr);
	if (style == 1 && enabled)
		glow_stroke(cr, col, 1.2, hov ? 0.9 : 0.5);
	else {
		rgba(cr, enabled ? (hov ? col : style == 2 ? 0x7a2a3a : C_LINEDIM) : C_LINEDIM, enabled ? 1 : 0.5);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
	}
	txt(cr, 13, PANGO_WEIGHT_SEMIBOLD, 0.06, enabled ? (style == 1 ? C_BRIGHT : style == 2 ? C_ERR : C_TEXT) : C_MUTED,
	    enabled ? 1 : 0.6, r.x + r.w / 2, r.y + r.h / 2, 1, r.w - 10, label);
	if (enabled) {
		if (in_content)
			add_content_hit(r, act, arg, it);
		else
			add_hit(r, act, arg, it);
	}
}

static int cat_index(const char *cat)
{
	for (int i = 0; i < NCATS; i++)
		if (!strcmp(CATS[i], cat))
			return i;
	return NCATS - 1;
}

/* pastille avec les initiales, couleur de la catégorie */
static void badge(cairo_t *cr, double x, double y, double s, const char *name, uint32_t col)
{
	char ini[8] = "";
	size_t k = 0;
	bool word = true;
	for (const unsigned char *p = (const unsigned char *)name; *p && k < 2; p++) {
		if (*p == ' ' || *p == '-' || *p == '(') {
			word = true;
			continue;
		}
		if (word && (*p < 0x80) && ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) {
			ini[k++] = *p >= 'a' && *p <= 'z' ? *p - 32 : *p;
			ini[k] = '\0';
		}
		word = false;
	}
	if (!ini[0])
		snprintf(ini, sizeof ini, "◆");
	rrect(cr, x, y, s, s, s * 0.24);
	cairo_pattern_t *g = cairo_pattern_create_linear(x, y, x + s, y + s);
	cairo_pattern_add_color_stop_rgba(g, 0, ((col >> 16) & 255) / 255.0, ((col >> 8) & 255) / 255.0, (col & 255) / 255.0, 0.32);
	cairo_pattern_add_color_stop_rgba(g, 1, ((col >> 16) & 255) / 255.0, ((col >> 8) & 255) / 255.0, (col & 255) / 255.0, 0.08);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	glow_stroke(cr, col, 1.2, 0.5);
	txt(cr, s * 0.38, PANGO_WEIGHT_BOLD, 0.02, C_BRIGHT, 1, x + s / 2, y + s / 2, 1, 0, ini);
}

static double tag(cairo_t *cr, double x, double cy, const char *label, uint32_t col)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, 10 * PANGO_SCALE);
	pango_font_description_set_weight(fd, PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(l, label, -1);
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	g_object_unref(l);
	double tw = w + 12;
	rrect(cr, x, cy - 9, tw, 18, 5);
	rgba(cr, col, 0.12);
	cairo_fill_preserve(cr);
	rgba(cr, col, 0.8);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	txt(cr, 10, PANGO_WEIGHT_SEMIBOLD, 0.08, col, 1, x + tw / 2, cy, 1, 0, label);
	return tw;
}

static double section(cairo_t *cr, double x, double y, double w, const char *title, const char *right)
{
	double tw = txt(cr, 13, PANGO_WEIGHT_SEMIBOLD, 0.3, C_LINE, 1, x, y + 10, 0, 0, title);
	double rw = right ? txt(cr, 12, PANGO_WEIGHT_NORMAL, 0.05, C_MUTED, 1, x + w, y + 10, 2, 0, right) : 0;
	double lx = x + tw + 12, lx2 = x + w - (rw ? rw + 12 : 0);
	if (lx2 > lx) {
		cairo_pattern_t *g = cairo_pattern_create_linear(lx, 0, lx2, 0);
		cairo_pattern_add_color_stop_rgba(g, 0, DIM_RGB, 1);
		cairo_pattern_add_color_stop_rgba(g, 1, DIM_RGB, 0.1);
		cairo_set_source(cr, g);
		cairo_rectangle(cr, lx, y + 10, lx2 - lx, 1);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
	return 34;
}

static bool job_pending_for(const char *arg)
{
	if (!arg || !*arg)
		return false;
	if (A.job_running && !strcmp(A.current.arg, arg))
		return true;
	for (int i = 0; i < A.njobs; i++)
		if (!strcmp(A.jobs[i].arg, arg))
			return true;
	return false;
}

static void load_protected(void)
{
	static bool done;
	if (done)
		return;
	done = true;
	/* socle d'Alpine + listes de Plume (core et bureau) */
	snprintf(protected_pkgs, sizeof protected_pkgs, "%s", pm_xbps() ?
		 " base-system sudo opendoas glibc xbps linux linux-firmware runit-void dracut grub " :
		 " alpine-base sudo doas busybox musl apk-tools linux-lts linux-firmware openrc ");
	/* tout ce dont dépend le socle (selon le gestionnaire de paquets lui-même) */
	FILE *dep = popen(pm_xbps() ? "xbps-query -Rx base-system 2>/dev/null" :
				      "apk info -R alpine-base 2>/dev/null", "r");
	char dl[256];
	while (dep && fgets(dl, sizeof dl, dep)) {
		if (strchr(dl, ':'))
			continue; /* ligne de titre « alpine-base-x depends on: » */
		dl[strcspn(dl, "<>=~ \n")] = '\0';
		if (dl[0] && !strchr(dl, '/')) {
			size_t n = strlen(protected_pkgs);
			snprintf(protected_pkgs + n, sizeof protected_pkgs - n, "%s ", dl);
		}
	}
	if (dep)
		pclose(dep);
	const char *lists[] = { "/etc/plume/packages/core.list", "/etc/plume/packages/desktop.list" };
	for (size_t i = 0; i < 2; i++) {
		FILE *f = fopen(lists[i], "r");
		char line[512];
		while (f && fgets(line, sizeof line, f)) {
			char *hash = strchr(line, '#');
			if (hash)
				*hash = '\0';
			for (char *t = strtok(line, " \t\n"); t; t = strtok(NULL, " \t\n")) {
				size_t n = strlen(protected_pkgs);
				snprintf(protected_pkgs + n, sizeof protected_pkgs - n, "%s ", t);
			}
		}
		if (f)
			fclose(f);
	}
}

static bool is_protected(const char *name)
{
	load_protected();
	char k[160];
	snprintf(k, sizeof k, " %s ", name);
	static const char *prefixes[] = { "linux-", "alpine-", "apk-", "musl", "busybox", "openrc", "ca-certificates",
					  "libc-", "ifupdown", "mdev", "sudo", "doas", "greetd", "seatd", "eudev",
					  "base-", "xbps", "runit", "glibc", "void-", "opendoas", "plume-" };
	for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++)
		if (!strncmp(name, prefixes[i], strlen(prefixes[i])))
			return true;
	return strstr(protected_pkgs, k) != NULL;
}

/* ------------------------------ cartes ------------------------------- */

static bool entry_visible(const struct entry *e)
{
	struct item it = { .kind = K_CATALOG, .idx = (int)(e - A.catalog) };
	return item_has_apk(&it, NULL) || item_has_flatpak(&it, NULL);
}

static void card(cairo_t *cr, struct rect r, int ci)
{
	struct entry *e = &A.catalog[ci];
	struct item it = { .kind = K_CATALOG, .idx = ci };
	bool hov = hover(r) && hover(content_clip);
	bool inst = item_installed(&it, 0) || item_installed(&it, 1);
	uint32_t col = CAT_COL[cat_index(e->cat)];
	rrect(cr, r.x, r.y, r.w, r.h, 12);
	rgba(cr, C_PANEL, 0.9);
	cairo_fill_preserve(cr);
	rgba(cr, C_LINE, hov ? 0.10 : 0.03);
	cairo_fill_preserve(cr);
	if (hov)
		glow_stroke(cr, C_LINE, 1.1, 0.6);
	else {
		rgba(cr, C_LINEDIM, 0.9);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
	}
	double bs = 52;
	badge(cr, r.x + 16, r.y + 16, bs, e->name, col);
	double tx = r.x + 16 + bs + 14, tw = r.x + r.w - 16 - tx;
	txt(cr, 15, PANGO_WEIGHT_SEMIBOLD, 0.01, C_BRIGHT, 1, tx, r.y + 26, 0, tw, e->name);
	para(cr, 12, C_MUTED, tx, r.y + 38, tw, 2, e->summary);
	double ty = r.y + r.h - 18, x = tx;
	if (item_has_apk(&it, NULL))
		x += tag(cr, x, ty, "ALPINE", C_LINE) + 6;
	if (item_has_flatpak(&it, NULL))
		x += tag(cr, x, ty, "FLATHUB", C_PURP) + 6;
	if (job_pending_for(e->apk) || job_pending_for(e->flatpak))
		txt(cr, 11, PANGO_WEIGHT_SEMIBOLD, 0.08, C_WARN, 1, r.x + r.w - 14, ty, 2, 0, "EN COURS…");
	else if (inst)
		txt(cr, 11, PANGO_WEIGHT_SEMIBOLD, 0.08, C_OK, 1, r.x + r.w - 14, ty, 2, 0, "✓ INSTALLÉ");
	add_content_hit(r, A_DETAIL, 0, &it);
}

/* grille de cartes ; renvoie la hauteur occupée */
static double grid(cairo_t *cr, double x, double y, double w, const int *idx, int n)
{
	if (n == 0)
		return 0;
	int cols = (int)fmax(1, floor((w + 14) / 320));
	double gap = 14, cw = (w - gap * (cols - 1)) / cols, ch = 122;
	for (int i = 0; i < n; i++) {
		int c = i % cols, r = i / cols;
		struct rect rc = { x + c * (cw + gap), y + r * (ch + gap), cw, ch };
		if (rc.y + rc.h >= content_clip.y && rc.y <= content_clip.y + content_clip.h)
			card(cr, rc, idx[i]);
	}
	int rows = (n + cols - 1) / cols;
	return rows * ch + (rows - 1) * gap;
}

/* ligne de résultat (paquet Alpine ou appli Flathub) */
static double result_row(cairo_t *cr, double x, double y, double w, const struct result *res)
{
	struct item it = { .kind = res->kind, .idx = -1, .r = *res };
	struct rect r = { x, y, w, 58 };
	bool visible = r.y + r.h >= content_clip.y && r.y <= content_clip.y + content_clip.h;
	if (!visible)
		return 64;
	bool hov = hover(r) && hover(content_clip);
	rrect(cr, r.x, r.y, r.w, r.h, 10);
	rgba(cr, C_LINE, hov ? 0.08 : 0.025);
	cairo_fill_preserve(cr);
	rgba(cr, hov ? C_LINE : C_LINEDIM, hov ? 0.9 : 0.6);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	uint32_t col = res->kind == K_FLATPAK ? C_PURP : C_LINE;
	badge(cr, x + 12, y + 11, 36, res->name, col);
	double tx = x + 62, bw = 120;
	char title[200];
	snprintf(title, sizeof title, "%s", res->name);
	double nw = txt(cr, 14, PANGO_WEIGHT_SEMIBOLD, 0, C_BRIGHT, 1, tx, y + 20, 0, w * 0.4, title);
	if (res->version[0])
		txt(cr, 11, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, tx + nw + 10, y + 20, 0, 0, res->version);
	txt(cr, 12, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, tx, y + 40, 0, w - (tx - x) - bw - 30, res->summary);
	int src = res->kind == K_FLATPAK ? 1 : 0;
	bool inst = item_installed(&it, src);
	struct rect b = { x + w - bw - 12, y + 13, bw, 32 };
	/* le clic sur la ligne ouvre la fiche ; le bouton installe directement */
	add_content_hit(r, A_DETAIL, 0, &it);
	if (job_pending_for(res->id))
		txt(cr, 11, PANGO_WEIGHT_SEMIBOLD, 0.08, C_WARN, 1, b.x + b.w / 2, b.y + b.h / 2, 1, 0, "EN COURS…");
	else if (inst)
		txt(cr, 11, PANGO_WEIGHT_SEMIBOLD, 0.08, C_OK, 1, b.x + b.w / 2, b.y + b.h / 2, 1, 0, "✓ INSTALLÉ");
	else
		button(cr, b, "Installer", 0, true, A_INSTALL, src, &it, true);
	return 64;
}

/* ------------------------------- vues -------------------------------- */

static double flathub_banner(cairo_t *cr, double x, double y, double w)
{
	if (A.flathub_ready)
		return 0;
	struct rect r = { x, y, w, 64 };
	rrect(cr, r.x, r.y, r.w, r.h, 12);
	rgba(cr, C_PURP, 0.07);
	cairo_fill_preserve(cr);
	rgba(cr, C_PURP, 0.6);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	txt(cr, 14, PANGO_WEIGHT_SEMIBOLD, 0.02, C_BRIGHT, 1, x + 18, y + 22, 0, w - 220,
	    "Flathub n'est pas activé");
	txt(cr, 12, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, x + 18, y + 44, 0, w - 230,
	    A.flatpak_present ? "Steam, Discord, Spotify… viennent de Flathub. Activation sans mot de passe."
			      : "Steam, Discord, Spotify… viennent de Flathub. Installe Flatpak (~70 Mo, mot de passe demandé).");
	bool pending = job_pending_for("flatpak") || (A.job_running && (A.current.kind == J_ADD_FLATHUB || A.current.kind == J_FP_APPSTREAM));
	for (int i = 0; i < A.njobs; i++)
		pending |= A.jobs[i].kind == J_ADD_FLATHUB || A.jobs[i].kind == J_FP_APPSTREAM;
	struct rect b = { x + w - 190, y + 16, 172, 34 };
	if (pending)
		txt(cr, 12, PANGO_WEIGHT_SEMIBOLD, 0.08, C_WARN, 1, b.x + b.w / 2, b.y + b.h / 2, 1, 0, "ACTIVATION…");
	else
		button(cr, b, "Activer Flathub", 1, true, A_ENABLE_FLATHUB, 0, NULL, true);
	return 64 + 18;
}

static double view_explore(cairo_t *cr, double x, double y, double w)
{
	double y0 = y;
	/* catégories */
	double cx = x;
	for (int i = -1; i < NCATS; i++) {
		const char *label = i < 0 ? "Tout" : CATS[i];
		char buf[64];
		snprintf(buf, sizeof buf, "%s%s%s", i < 0 ? "" : CAT_GLYPH[i], i < 0 ? "" : " ", label);
		double cw = 18 + 8.2 * (double)strlen(buf);
		if (cx + cw > x + w) {
			cx = x;
			y += 40;
		}
		struct rect r = { cx, y, cw, 30 };
		bool on = i < 0 ? !A.category[0] : !strcmp(A.category, label);
		bool hov = hover(r) && hover(content_clip);
		rrect(cr, r.x, r.y, r.w, r.h, 15);
		rgba(cr, i < 0 ? C_LINE : CAT_COL[i], on ? 0.22 : hov ? 0.10 : 0.03);
		cairo_fill_preserve(cr);
		rgba(cr, on ? (i < 0 ? C_LINE : CAT_COL[i]) : C_LINEDIM, 1);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		txt(cr, 12, on ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, 0.03, on ? C_BRIGHT : C_TEXT, 1,
		    r.x + r.w / 2, r.y + 15, 1, 0, buf);
		add_content_hit(r, A_CATEGORY, i, NULL);
		cx += cw + 8;
	}
	y += 48;
	y += flathub_banner(cr, x, y, w);

	int idx[MAX_CATALOG];
	for (int c = 0; c < NCATS; c++) {
		if (A.category[0] && strcmp(A.category, CATS[c]))
			continue;
		int n = 0;
		for (int i = 0; i < A.ncatalog; i++)
			if (!strcmp(A.catalog[i].cat, CATS[c]) && entry_visible(&A.catalog[i]))
				idx[n++] = i;
		if (!n)
			continue;
		char right[32];
		snprintf(right, sizeof right, "%d appli%s", n, n > 1 ? "s" : "");
		char title[64];
		snprintf(title, sizeof title, "%s  %s", CAT_GLYPH[c], CATS[c]);
		for (char *p = title; *p; p++)
			if (*p >= 'a' && *p <= 'z') *p -= 32;
		y += section(cr, x, y, w, title, right);
		y += grid(cr, x, y, w, idx, n) + 26;
	}
	if (!A.index_ready)
		txt(cr, 13, PANGO_WEIGHT_NORMAL, 0.03, C_MUTED, 1, x, y + 10, 0, 0, "Chargement de la liste des paquets…");
	return y - y0 + 20;
}

static double view_search(cairo_t *cr, double x, double y, double w)
{
	double y0 = y;
	int idx[MAX_CATALOG], n = 0;
	for (int i = 0; i < A.ncatalog; i++) {
		struct entry *e = &A.catalog[i];
		if (!entry_visible(e))
			continue;
		if (strcasestr(e->name, A.query) || strcasestr(e->summary, A.query) || strcasestr(e->cat, A.query) ||
		    (e->apk[0] && strcasestr(e->apk, A.query)) || (e->flatpak[0] && strcasestr(e->flatpak, A.query)))
			idx[n++] = i;
	}
	if (n) {
		y += section(cr, x, y, w, "APPLICATIONS", NULL);
		y += grid(cr, x, y, w, idx, n) + 26;
	}
	char right[64];
	snprintf(right, sizeof right, "%d résultat%s", A.napk_results, A.napk_results > 1 ? "s" : "");
	y += section(cr, x, y, w, "PAQUETS ALPINE", A.index_ready ? right : "chargement…");
	if (A.index_ready && !A.napk_results) {
		txt(cr, 12, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, x, y + 8, 0, 0, "Aucun autre paquet");
		y += 30;
	}
	for (int i = 0; i < A.napk_results; i++)
		y += result_row(cr, x, y, w, &A.apk_results[i]);
	y += 20;
	if (!A.flathub_ready) {
		y += section(cr, x, y, w, "FLATHUB", NULL);
		y += flathub_banner(cr, x, y, w);
	} else {
		snprintf(right, sizeof right, "%d résultat%s", A.nfp_results, A.nfp_results > 1 ? "s" : "");
		bool waiting = A.fp_searching || strcmp(A.fp_query, A.query);
		y += section(cr, x, y, w, "FLATHUB", waiting ? "recherche…" : right);
		if (!waiting && !A.nfp_results)
			y += 30;
		for (int i = 0; i < A.nfp_results; i++)
			y += result_row(cr, x, y, w, &A.fp_results[i]);
	}
	return y - y0 + 20;
}

static char detail_size[48];
static char detail_size_for[128];

static double view_detail(cairo_t *cr, double x, double y, double w)
{
	double y0 = y;
	struct item *it = &A.detail;
	button(cr, (struct rect){ x, y, 110, 32 }, "← Retour", 0, true, A_BACK, 0, NULL, true);
	y += 52;
	char name[128], summary[256];
	item_title(it, name, sizeof name, summary, sizeof summary);
	const char *apk = NULL, *fp = NULL;
	bool has_apk = item_has_apk(it, &apk), has_fp = item_has_flatpak(it, &fp);
	if (!has_apk && A.detail_source == 0)
		A.detail_source = 1;
	if (!has_fp && A.detail_source == 1)
		A.detail_source = 0;
	int src = A.detail_source;
	const char *cat = it->kind == K_CATALOG ? A.catalog[it->idx].cat : "";
	uint32_t col = it->kind == K_CATALOG ? CAT_COL[cat_index(cat)] : it->kind == K_FLATPAK ? C_PURP : C_LINE;
	badge(cr, x, y, 104, name, col);
	double tx = x + 128;
	txt(cr, 26, PANGO_WEIGHT_BOLD, 0.01, C_BRIGHT, 1, tx, y + 22, 0, w - 128, name);
	double ph = para(cr, 14, C_TEXT, tx, y + 44, w - 128, 3, summary);
	if (cat[0])
		txt(cr, 12, PANGO_WEIGHT_NORMAL, 0.1, C_MUTED, 1, tx, y + 56 + ph, 0, 0, cat);
	y += 128;

	/* choix de la source quand les deux existent */
	if (has_apk && has_fp) {
		txt(cr, 12, PANGO_WEIGHT_SEMIBOLD, 0.2, C_LINEDIM, 1, x, y + 15, 0, 0, "SOURCE");
		struct rect a = { x + 90, y, 230, 30 }, b = { x + 330, y, 250, 30 };
		const char *la = pm_xbps() ? "Void · léger" : "Alpine · léger", *lb = "Flathub · isolé, plus lourd";
		for (int s = 0; s < 2; s++) {
			struct rect r = s ? b : a;
			bool on = src == s, hov = hover(r);
			rrect(cr, r.x, r.y, r.w, r.h, 15);
			rgba(cr, s ? C_PURP : C_LINE, on ? 0.22 : hov ? 0.08 : 0.02);
			cairo_fill_preserve(cr);
			rgba(cr, on ? (s ? C_PURP : C_LINE) : C_LINEDIM, 1);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
			txt(cr, 12, on ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, 0.02, on ? C_BRIGHT : C_TEXT, 1,
			    r.x + r.w / 2, r.y + 15, 1, 0, s ? lb : la);
			add_content_hit(r, A_SOURCE, s, NULL);
		}
		y += 48;
	}

	/* boutons */
	bool inst = item_installed(it, src);
	const char *key = src == 0 ? apk : fp;
	if (job_pending_for(key)) {
		txt(cr, 14, PANGO_WEIGHT_SEMIBOLD, 0.1, C_WARN, 1, x, y + 18, 0, 0, "EN COURS…");
	} else if (inst) {
		button(cr, (struct rect){ x, y, 150, 38 }, "Ouvrir", 1, true, A_LAUNCH, src, it, true);
		bool prot = src == 0 && apk && (is_protected(apk) || !apk_in_world(apk));
		button(cr, (struct rect){ x + 164, y, 150, 38 }, "Supprimer", 2, !prot, A_REMOVE, src, it, true);
		if (prot)
			txt(cr, 12, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, x + 330, y + 19, 0, w - 330,
			    is_protected(apk) ? "Fait partie de Plume : ne peut pas être retiré ici"
					      : "Installé comme dépendance d'un autre paquet");
	} else {
		bool can = src == 0 ? has_apk : (has_fp && A.flathub_ready);
		button(cr, (struct rect){ x, y, 180, 38 }, "Installer", 1, can, A_INSTALL, src, it, true);
		if (src == 1 && !A.flathub_ready)
			button(cr, (struct rect){ x + 194, y, 180, 38 }, "Activer Flathub", 0, true, A_ENABLE_FLATHUB, 0, NULL, true);
		txt(cr, 12, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, x + (src == 1 && !A.flathub_ready ? 390 : 196), y + 19, 0, w - 400,
		    src == 0 ? "Demande le mot de passe administrateur" : "Pour toi seul, sans mot de passe");
	}
	y += 62;

	/* informations */
	y += section(cr, x, y, w, "INFORMATIONS", NULL);
	char ver[64] = "—", size[48] = "—";
	if (src == 0 && apk) {
		const struct pkg *k = pkg_find(apk);
		if (k)
			snprintf(ver, sizeof ver, "%s", k->version);
		/* taille installée : demandée une fois à apk */
		if (strcmp(detail_size_for, apk)) {
			snprintf(detail_size_for, sizeof detail_size_for, "%s", apk);
			detail_size[0] = '\0';
			char cmd[200];
			if (pm_xbps())
				snprintf(cmd, sizeof cmd, "xbps-query -R -p installed_size '%s' 2>/dev/null", apk);
			else
				snprintf(cmd, sizeof cmd, "apk info -s '%s' 2>/dev/null", apk);
			FILE *f = popen(cmd, "r");
			char line[200];
			while (f && fgets(line, sizeof line, f))
				if (line[0] >= '0' && line[0] <= '9') {
					line[strcspn(line, "\n")] = '\0';
					snprintf(detail_size, sizeof detail_size, "%s", line);
				}
			if (f)
				pclose(f);
		}
		if (detail_size[0])
			snprintf(size, sizeof size, "%s", detail_size);
	} else if (src == 1 && it->kind == K_FLATPAK && it->r.version[0]) {
		snprintf(ver, sizeof ver, "%s", it->r.version);
	}
	struct kvrow { const char *k; const char *v; } rows[6];
	int nr = 0;
	rows[nr++] = (struct kvrow){ "SOURCE", src == 0 ? (pm_xbps() ? "Paquet Void (xbps)" : "Paquet Alpine (apk)") : "Flathub (Flatpak)" };
	rows[nr++] = (struct kvrow){ src == 0 ? "PAQUET" : "IDENTIFIANT", key ? key : "—" };
	rows[nr++] = (struct kvrow){ "VERSION", ver };
	if (src == 0)
		rows[nr++] = (struct kvrow){ "TAILLE", size };
	rows[nr++] = (struct kvrow){ "ÉTAT", inst ? "Installé" : "Non installé" };
	for (int i = 0; i < nr; i++) {
		txt(cr, 12, PANGO_WEIGHT_SEMIBOLD, 0.15, C_LINEDIM, 1, x, y + 12, 0, 0, rows[i].k);
		txt(cr, 13, PANGO_WEIGHT_NORMAL, 0.01, i == nr - 1 && inst ? C_OK : C_TEXT, 1, x + 170, y + 12, 0, w - 170, rows[i].v);
		y += 28;
	}
	if (src == 0 && apk) {
		const struct pkg *k = pkg_find(apk);
		if (k && k->desc[0] && strcmp(k->desc, summary)) {
			y += 12;
			y += section(cr, x, y, w, "DESCRIPTION DU PAQUET", NULL);
			y += para(cr, 13, C_MUTED, x, y, w, 6, k->desc) + 10;
		}
	}
	return y - y0 + 20;
}

static double installed_row(cairo_t *cr, double x, double y, double w, const char *name, const char *sub,
			    const struct item *it, int src, bool removable, const char *why, bool can_open)
{
	struct rect r = { x, y, w, 52 };
	if (r.y + r.h < content_clip.y || r.y > content_clip.y + content_clip.h)
		return 58;
	bool hov = hover(r) && hover(content_clip);
	rrect(cr, r.x, r.y, r.w, r.h, 10);
	rgba(cr, C_LINE, hov ? 0.07 : 0.02);
	cairo_fill_preserve(cr);
	rgba(cr, C_LINEDIM, 0.6);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	badge(cr, x + 10, y + 9, 34, name, src ? C_PURP : C_LINE);
	txt(cr, 14, PANGO_WEIGHT_SEMIBOLD, 0, C_BRIGHT, 1, x + 56, y + 17, 0, w * 0.45, name);
	txt(cr, 11, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, x + 56, y + 36, 0, w - 400, sub);
	struct rect bo = { x + w - 252, y + 10, 116, 32 }, br = { x + w - 128, y + 10, 116, 32 };
	if (it) {
		add_content_hit(r, A_DETAIL, 0, it);
		if (job_pending_for(src ? it->r.id : (it->kind == K_CATALOG ? A.catalog[it->idx].apk : it->r.id)))
			txt(cr, 11, PANGO_WEIGHT_SEMIBOLD, 0.08, C_WARN, 1, br.x + br.w / 2, br.y + 16, 1, 0, "EN COURS…");
		else {
			if (can_open)
				button(cr, bo, "Ouvrir", 0, true, A_LAUNCH, src, it, true);
			if (removable)
				button(cr, br, "Supprimer", 2, true, A_REMOVE, src, it, true);
			else
				txt(cr, 10, PANGO_WEIGHT_SEMIBOLD, 0.08, C_MUTED, 1, br.x + br.w / 2, br.y + 16, 1, br.w, why);
		}
	}
	return 58;
}

static double view_installed(cairo_t *cr, double x, double y, double w)
{
	double y0 = y;
	/* applications du catalogue installées (Alpine ou Flathub) */
	int n = 0;
	for (int i = 0; i < A.ncatalog; i++) {
		struct item it = { .kind = K_CATALOG, .idx = i };
		n += item_installed(&it, 0) + item_installed(&it, 1);
	}
	char right[48];
	snprintf(right, sizeof right, "%d", n);
	y += section(cr, x, y, w, "APPLICATIONS", right);
	for (int i = 0; i < A.ncatalog; i++) {
		struct item it = { .kind = K_CATALOG, .idx = i };
		struct entry *e = &A.catalog[i];
		for (int s = 0; s < 2; s++) {
			if (!item_installed(&it, s))
				continue;
			char sub[200];
			snprintf(sub, sizeof sub, "%s · %s", s ? "Flathub" : pm_distro(), e->summary);
			bool plume = s == 0 && is_protected(e->apk), dep = s == 0 && !apk_in_world(e->apk);
			y += installed_row(cr, x, y, w, e->name, sub, &it, s, !plume && !dep, plume ? "PLUME" : "DÉPENDANCE", true);
		}
	}
	/* autres applis Flathub */
	int nfp = 0;
	for (int i = 0; i < A.nfp_installed; i++) {
		bool dup = false;
		for (int c = 0; c < A.ncatalog && !dup; c++)
			dup = !strcmp(A.catalog[c].flatpak, A.fp_installed[i].id);
		if (dup)
			continue;
		if (!nfp++) {
			y += 16;
			y += section(cr, x, y, w, "AUTRES APPLIS FLATHUB", NULL);
		}
		struct item it = { .kind = K_FLATPAK, .idx = -1, .r = A.fp_installed[i] };
		char sub[200];
		snprintf(sub, sizeof sub, "Flathub · %s %s", it.r.id, it.r.version);
		y += installed_row(cr, x, y, w, it.r.name, sub, &it, 1, true, "", true);
	}
	/* paquets Alpine demandés explicitement (world), hors catalogue */
	y += 16;
	snprintf(right, sizeof right, "%d paquets installés au total", A.ninstalled);
	y += section(cr, x, y, w, "AUTRES PAQUETS ALPINE", right);
	for (int i = 0; i < A.nworld; i++) {
		const char *pn = A.world[i];
		bool dup = false;
		for (int c = 0; c < A.ncatalog && !dup; c++)
			dup = !strcmp(A.catalog[c].apk, pn);
		if (dup)
			continue;
		const struct pkg *k = pkg_find(pn);
		struct item it = { .kind = K_APK, .idx = -1 };
		snprintf(it.r.id, sizeof it.r.id, "%s", pn);
		snprintf(it.r.name, sizeof it.r.name, "%s", pn);
		if (k) {
			snprintf(it.r.version, sizeof it.r.version, "%s", k->version);
			snprintf(it.r.summary, sizeof it.r.summary, "%s", k->desc);
		}
		char sub[260];
		snprintf(sub, sizeof sub, "%s%s%s", k ? k->version : "", k ? " · " : "", k ? k->desc : "");
		bool prot = is_protected(pn);
		y += installed_row(cr, x, y, w, pn, sub, &it, 0, !prot, "PLUME", false);
	}
	return y - y0 + 20;
}

static double view_updates(cairo_t *cr, double x, double y, double w)
{
	double y0 = y;
	bool busy = A.job_running && (A.current.kind == J_APK_UPDATE || A.current.kind == J_APK_UPGRADE || A.current.kind == J_FP_UPDATE);
	button(cr, (struct rect){ x, y, 250, 38 }, "Rechercher les mises à jour", A.updates_checked ? 0 : 1, !busy,
	       A_CHECK_UPDATES, 0, NULL, true);
	char lab[64];
	snprintf(lab, sizeof lab, "Tout mettre à jour%s", A.nupdates ? "" : "");
	if (A.nupdates)
		snprintf(lab, sizeof lab, "Tout mettre à jour (%d)", A.nupdates);
	button(cr, (struct rect){ x + 264, y, 250, 38 }, lab, A.nupdates ? 1 : 0, !busy && (A.nupdates || !A.updates_checked),
	       A_UPDATE_ALL, 0, NULL, true);
	y += 56;
	if (!A.updates_checked) {
		para(cr, 13, C_MUTED, x, y, w, 4,
		     "« Rechercher » télécharge la liste à jour des paquets (mot de passe administrateur demandé), "
		     "puis affiche ce qui peut être mis à jour, système et applis Flathub.");
		return y - y0 + 80;
	}
	if (!A.nupdates) {
		txt(cr, 16, PANGO_WEIGHT_SEMIBOLD, 0.02, C_OK, 1, x, y + 14, 0, 0, "✓ Tout est à jour");
		return y - y0 + 50;
	}
	char right[32];
	snprintf(right, sizeof right, "%d", A.nupdates);
	y += section(cr, x, y, w, "À METTRE À JOUR", right);
	for (int i = 0; i < A.nupdates; i++) {
		struct update_row *u = &A.updates[i];
		if (y + 30 >= content_clip.y && y <= content_clip.y + content_clip.h) {
			txt(cr, 13, PANGO_WEIGHT_SEMIBOLD, 0, C_TEXT, 1, x + 4, y + 13, 0, w * 0.45, u->name);
			char v[120];
			snprintf(v, sizeof v, "%s%s%s", u->from, u->from[0] ? "  →  " : "", u->to);
			txt(cr, 12, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, x + w * 0.48, y + 13, 0, w * 0.4, v);
			tag(cr, x + w - 80, y + 13, u->kind == K_FLATPAK ? "FLATHUB" : "ALPINE", u->kind == K_FLATPAK ? C_PURP : C_LINE);
		}
		y += 28;
	}
	return y - y0 + 20;
}

/* ------------------------------ en-tête ------------------------------ */

static void header(cairo_t *cr, int W)
{
	double cy = HEADER_ROW / 2;
	double x = MARGIN;
	x += txt(cr, 20, PANGO_WEIGHT_NORMAL, 0, C_LINE, 1, x, cy, 0, 0, "◆") + 10;
	x += txt(cr, 15, PANGO_WEIGHT_BOLD, 0.3, C_BRIGHT, 1, x, cy, 0, 0, "LOGITHÈQUE") + 28;
	int ninst = 0;
	for (int i = 0; i < A.ncatalog; i++) {
		struct item it = { .kind = K_CATALOG, .idx = i };
		ninst += item_installed(&it, 0) + item_installed(&it, 1);
	}
	const char *labels[3] = { "Explorer", "Installées", "Mises à jour" };
	for (int t = 0; t < 3; t++) {
		char buf[48];
		if (t == 1)
			snprintf(buf, sizeof buf, "%s %d", labels[t], ninst);
		else if (t == 2 && A.nupdates)
			snprintf(buf, sizeof buf, "%s %d", labels[t], A.nupdates);
		else
			snprintf(buf, sizeof buf, "%s", labels[t]);
		double tw = 26 + 8.4 * (double)strlen(buf);
		struct rect r = { x, cy - 16, tw, 32 };
		bool on = A.tab == (enum tab)t && !A.detail_open && !(t == 0 && 0);
		bool hov = hover(r);
		rrect(cr, r.x, r.y, r.w, r.h, 16);
		rgba(cr, C_LINE, on ? 0.2 : hov ? 0.08 : 0);
		cairo_fill_preserve(cr);
		if (on)
			glow_stroke(cr, C_LINE, 1, 0.5);
		else {
			rgba(cr, hov ? C_LINE : C_LINEDIM, 1);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
		}
		txt(cr, 13, on ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, 0.04, on ? C_BRIGHT : C_TEXT, 1,
		    r.x + r.w / 2, cy, 1, 0, buf);
		add_hit(r, A_TAB, t, NULL);
		x += tw + 8;
	}
	/* recherche : à droite des onglets, ou sur une seconde ligne si la place manque */
	double sw = clampd(W * 0.3, 220, 380), sx = W - MARGIN - sw;
	if (sx < x + 12) {
		sx = MARGIN;
		sw = W - 2 * MARGIN;
		cy = HEADER_ROW + 22;
		HEADER_H = HEADER_ROW + 48;
	} else {
		HEADER_H = HEADER_ROW;
	}
	struct rect sr = { sx, cy - 18, sw, 36 };
	rrect(cr, sr.x, sr.y, sr.w, sr.h, 10);
	cairo_set_source_rgba(cr, 2 / 255.0, 10 / 255.0, 18 / 255.0, 0.9);
	cairo_fill_preserve(cr);
	glow_stroke(cr, A.query[0] ? C_LINE : C_LINEDIM, 1.1, A.query[0] ? 0.8 : 0.2);
	double qx = sr.x + 12;
	qx += txt(cr, 16, PANGO_WEIGHT_NORMAL, 0, C_LINE, 1, qx, cy, 0, 0, "⌕") + 8;
	double qw = 0;
	if (A.query[0])
		qw = txt(cr, 14, PANGO_WEIGHT_NORMAL, 0.01, C_BRIGHT, 1, qx, cy, 0, sw - (qx - sr.x) - 36, A.query);
	else
		txt(cr, 13, PANGO_WEIGHT_NORMAL, 0.01, C_MUTED, 0.8, qx, cy, 0, sw - (qx - sr.x) - 12,
		    "Rechercher (tape directement)");
	if (!A.pw_open && A.focused) {
		rgba(cr, C_LINE, 0.9);
		cairo_rectangle(cr, qx + qw + 2, cy - 9, 1.5, 18);
		cairo_fill(cr);
	}
	add_hit(sr, A_SEARCH, 0, NULL);
	if (A.query[0]) {
		struct rect cl = { sr.x + sr.w - 30, cy - 12, 24, 24 };
		txt(cr, 13, PANGO_WEIGHT_NORMAL, 0, hover(cl) ? C_ERR : C_MUTED, 1, cl.x + 12, cy, 1, 0, "✕");
		add_hit(cl, A_CLEAR_SEARCH, 0, NULL);
	}
	/* filet */
	cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, W, 0);
	cairo_pattern_add_color_stop_rgba(g, 0, ACC_RGB, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 0.5, ACC_RGB, 0.55);
	cairo_pattern_add_color_stop_rgba(g, 1, ACC_RGB, 0.0);
	cairo_set_source(cr, g);
	cairo_rectangle(cr, 0, HEADER_H - 1, W, 1);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

/* ---------------------------- barre d'état --------------------------- */

static void status_bar(cairo_t *cr, int W, int H)
{
	double y = H - STATUS_H, cy = y + STATUS_H / 2;
	rgba(cr, C_BG, 0.96);
	cairo_rectangle(cr, 0, y, W, STATUS_H);
	cairo_fill(cr);
	rgba(cr, C_LINEDIM, 1);
	cairo_rectangle(cr, 0, y, W, 1);
	cairo_fill(cr);
	double x = MARGIN;
	if (A.job_running) {
		/* spinner */
		double a = fmod((now_s() - A.job_start) * 5, 2 * M_PI);
		cairo_new_sub_path(cr);
		cairo_arc(cr, x + 8, cy, 7, a, a + 4.2);
		rgba(cr, C_LINE, 1);
		cairo_set_line_width(cr, 2);
		cairo_stroke(cr);
		x += 26;
		x += txt(cr, 13, PANGO_WEIGHT_SEMIBOLD, 0.02, C_BRIGHT, 1, x, cy, 0, W * 0.35, A.current.title) + 16;
		double bw = 160;
		if (A.job_progress >= 0) {
			double bx = W - MARGIN - bw;
			rrect(cr, bx, cy - 4, bw, 8, 4);
			rgba(cr, C_LINEDIM, 1);
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
			if (A.job_progress > 0.01) {
				rrect(cr, bx + 1, cy - 3, (bw - 2) * clampd(A.job_progress, 0, 1), 6, 3);
				rgba(cr, C_LINE, 1);
				cairo_fill(cr);
			}
		}
		txt(cr, 12, PANGO_WEIGHT_NORMAL, 0, C_MUTED, 1, x, cy, 0, W - MARGIN - bw - 16 - x, A.job_line);
		return;
	}
	if (A.toast[0] && now_s() - A.toast_t < 6) {
		txt(cr, 13, PANGO_WEIGHT_SEMIBOLD, 0.02, A.toast_err ? C_ERR : C_OK, 1, x, cy, 0, W - 2 * MARGIN,
		    A.toast);
		return;
	}
	char info[200];
	snprintf(info, sizeof info, "%d applications au catalogue · %d paquets %s · Flathub %s",
		 A.ncatalog, A.npkgs, pm_distro(), A.flathub_ready ? "activé" : A.flatpak_present ? "non configuré" : "non installé");
	txt(cr, 12, PANGO_WEIGHT_NORMAL, 0.02, C_MUTED, 1, x, cy, 0, W - 2 * MARGIN, info);
	if (A.njobs)
		txt(cr, 12, PANGO_WEIGHT_NORMAL, 0.02, C_WARN, 1, W - MARGIN, cy, 2, 0, "tâches en attente");
}

/* --------------------------- mot de passe ---------------------------- */

static void password_dialog(cairo_t *cr, int W, int H)
{
	rgba(cr, C_BG, 0.75);
	cairo_paint(cr);
	double w = 460, h = 240, x = (W - w) / 2, y = (H - h) / 2;
	rrect(cr, x, y, w, h, 14);
	rgba(cr, C_PANEL, 1);
	cairo_fill_preserve(cr);
	glow_stroke(cr, A.pw_error ? C_ERR : C_LINE, 1.4, 1);
	txt(cr, 13, PANGO_WEIGHT_SEMIBOLD, 0.3, C_LINE, 1, x + 24, y + 30, 0, 0, "MOT DE PASSE ADMINISTRATEUR");
	const char *what = A.njobs ? A.jobs[0].title : "";
	txt(cr, 13, PANGO_WEIGHT_NORMAL, 0.01, C_TEXT, 1, x + 24, y + 60, 0, w - 48, what);
	txt(cr, 12, PANGO_WEIGHT_NORMAL, 0.01, C_MUTED, 1, x + 24, y + 82, 0, w - 48,
	    "Gardé 5 min en mémoire, jamais enregistré");
	struct rect f = { x + 24, y + 102, w - 48, 40 };
	rrect(cr, f.x, f.y, f.w, f.h, 9);
	cairo_set_source_rgba(cr, 2 / 255.0, 10 / 255.0, 18 / 255.0, 1);
	cairo_fill_preserve(cr);
	glow_stroke(cr, A.pw_error ? C_ERR : C_LINE, 1.1, 0.7);
	char dots[200] = "";
	size_t n = 0;
	for (const unsigned char *p = (const unsigned char *)A.pw; *p && n + 4 < sizeof dots; p++)
		if ((*p & 0xC0) != 0x80) {
			memcpy(dots + n, "●", 3);
			n += 3;
		}
	dots[n] = '\0';
	double dw = txt(cr, 14, PANGO_WEIGHT_NORMAL, 0.15, C_BRIGHT, 1, f.x + 14, f.y + f.h / 2, 0, f.w - 28, dots);
	rgba(cr, C_LINE, 0.9);
	cairo_rectangle(cr, f.x + 16 + dw, f.y + 11, 1.5, 18);
	cairo_fill(cr);
	if (A.pw_error)
		txt(cr, 12, PANGO_WEIGHT_SEMIBOLD, 0.02, C_ERR, 1, x + 24, y + 160, 0, w - 48, "Mot de passe incorrect, réessaie");
	button(cr, (struct rect){ x + w - 24 - 130 - 12 - 120, y + h - 56, 120, 36 }, "Annuler", 0, true, A_PW_CANCEL, 0, NULL, false);
	button(cr, (struct rect){ x + w - 24 - 130, y + h - 56, 130, 36 }, "Valider", 1, true, A_PW_OK, 0, NULL, false);
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

	/* l'en-tête d'abord : il décide s'il lui faut une ou deux lignes */
	header(cr, W);
	/* contenu, avec défilement */
	content_clip = (struct rect){ 0, HEADER_H, W, H - HEADER_H - STATUS_H };
	double x = MARGIN, w = W - 2 * MARGIN;
	double max_scroll = fmax(0, A.content_h - content_clip.h + 24);
	A.scroll = clampd(A.scroll, 0, max_scroll);
	cairo_save(cr);
	cairo_rectangle(cr, content_clip.x, content_clip.y, content_clip.w, content_clip.h);
	cairo_clip(cr);
	double y = HEADER_H + 20 - A.scroll;
	double h;
	if (A.detail_open)
		h = view_detail(cr, x, y, w);
	else if (A.tab == T_INSTALLED)
		h = view_installed(cr, x, y, w);
	else if (A.tab == T_UPDATES)
		h = view_updates(cr, x, y, w);
	else if (A.query[0])
		h = view_search(cr, x, y, w);
	else
		h = view_explore(cr, x, y, w);
	A.content_h = h + 20;
	cairo_restore(cr);
	/* ascenseur */
	if (A.content_h > content_clip.h) {
		double th = fmax(30, content_clip.h * content_clip.h / A.content_h);
		double ty = content_clip.y + (content_clip.h - th) * (max_scroll > 0 ? A.scroll / max_scroll : 0);
		rgba(cr, C_LINE, 0.5);
		rrect(cr, W - 8, ty, 3, th, 1.5);
		cairo_fill(cr);
	}
	status_bar(cr, W, H);
	if (A.pw_open) {
		A.nhits = 0; /* la fenêtre de mot de passe capte tous les clics */
		password_dialog(cr, W, H);
	}
}

/* ------------------------------ entrées ------------------------------ */

static void open_detail(const struct item *it)
{
	A.detail = *it;
	A.detail_open = true;
	A.scroll = 0;
	/* source préférée : celle qui est installée, sinon Alpine (plus léger) */
	A.detail_source = item_installed(it, 1) && !item_installed(it, 0) ? 1 : item_has_apk(it, NULL) ? 0 : 1;
}

void ui_click(double x, double y)
{
	for (int i = A.nhits - 1; i >= 0; i--) {
		struct hit *h = &A.hits[i];
		if (x < h->r.x || x > h->r.x + h->r.w || y < h->r.y || y > h->r.y + h->r.h)
			continue;
		switch (h->act) {
		case A_TAB:
			A.tab = (enum tab)h->arg;
			A.detail_open = false;
			A.scroll = 0;
			if (A.tab == T_EXPLORE && A.query[0]) {
				A.query[0] = '\0';
				search_update();
			}
			break;
		case A_CATEGORY:
			snprintf(A.category, sizeof A.category, "%s", h->arg < 0 ? "" : CATS[h->arg]);
			A.scroll = 0;
			break;
		case A_DETAIL: open_detail(&h->item); break;
		case A_BACK: A.detail_open = false; A.scroll = 0; break;
		case A_SOURCE: A.detail_source = h->arg; break;
		case A_INSTALL: install_item(&h->item, h->arg); break;
		case A_REMOVE: remove_item(&h->item, h->arg); break;
		case A_LAUNCH: launch_item(&h->item, h->arg); break;
		case A_ENABLE_FLATHUB: enable_flathub(); break;
		case A_CHECK_UPDATES: check_updates(); break;
		case A_UPDATE_ALL: if (!A.updates_checked) check_updates(); else update_all(); break;
		case A_CLEAR_SEARCH: A.query[0] = '\0'; search_update(); A.scroll = 0; break;
		case A_PW_OK: password_submitted(); break;
		case A_PW_CANCEL: password_cancelled(); break;
		default: break;
		}
		request_redraw();
		return;
	}
}

void ui_scroll(double dy)
{
	A.scroll += dy;
	if (A.scroll < 0)
		A.scroll = 0;
	request_redraw();
}

static void utf8_pop(char *s)
{
	size_t n = strlen(s);
	while (n > 0 && ((unsigned char)s[--n] & 0xC0) == 0x80)
		;
	s[n] = '\0';
}

void ui_key(xkb_keysym_t sym, const char *text, bool ctrl)
{
	bool printable = text && (unsigned char)text[0] >= 0x20 && text[0] != 0x7f && !ctrl;
	if (A.pw_open) {
		if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter)
			password_submitted();
		else if (sym == XKB_KEY_Escape)
			password_cancelled();
		else if (sym == XKB_KEY_BackSpace)
			utf8_pop(A.pw);
		else if (ctrl && (sym == XKB_KEY_u || sym == XKB_KEY_U))
			A.pw[0] = '\0';
		else if (printable && strlen(A.pw) + strlen(text) < sizeof A.pw - 1)
			strcat(A.pw, text);
		return;
	}
	switch (sym) {
	case XKB_KEY_Escape:
		if (A.detail_open)
			A.detail_open = false;
		else if (A.query[0]) {
			A.query[0] = '\0';
			search_update();
		}
		A.scroll = 0;
		return;
	case XKB_KEY_BackSpace:
		if (A.query[0]) {
			utf8_pop(A.query);
			A.query_t = now_s();
			A.scroll = 0;
			search_update();
		} else if (A.detail_open) {
			A.detail_open = false;
		}
		return;
	case XKB_KEY_Up: ui_scroll(-60); return;
	case XKB_KEY_Down: ui_scroll(60); return;
	case XKB_KEY_Prior: ui_scroll(-400); return;
	case XKB_KEY_Next: ui_scroll(400); return;
	case XKB_KEY_Home: A.scroll = 0; return;
	default: break;
	}
	if (ctrl && (sym == XKB_KEY_u || sym == XKB_KEY_U)) {
		A.query[0] = '\0';
		search_update();
		return;
	}
	if (printable && strlen(A.query) + strlen(text) < sizeof A.query - 1) {
		/* taper n'importe où lance une recherche */
		strcat(A.query, text);
		A.query_t = now_s();
		A.tab = T_EXPLORE;
		A.detail_open = false;
		A.scroll = 0;
		search_update();
	}
}
