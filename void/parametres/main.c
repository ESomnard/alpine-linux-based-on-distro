/*
 * plume-parametres — tous les réglages de Plume au même endroit (comme les Réglages du Mac).
 *
 * Rangés par thème : Réseau · Écran et apparence · Son et notifications · Clavier et accessibilité ·
 * Système. Un clic ouvre le réglage ; taper au clavier cherche tout de suite (Entrée ouvre le premier
 * trouvé, Échap efface puis ferme). Les réglages ne sont plus en double dans « Toutes les applications ».
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <errno.h>
#include <librsvg/rsvg.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-client-protocol.h"
#include "fenetre.h"

#define FONT "Inter, DejaVu Sans"
#define HEADER_H 64.0
#define MARGIN 28.0
#define TILE_H 76.0
#define COLS_MIN_W 270.0

struct entry {
	const char *section, *name, *desc, *icon, *cmd, *keys;
};

static const struct entry E[] = {
	{ "Réseau", "Wifi", "Réseaux sans fil, mot de passe", "plume-wifi", "plume-wifi", "internet wifi réseau sans fil" },
	{ "Réseau", "Bluetooth", "Casques, souris, manettes", "plume-bluetooth", "plume-bluetooth", "casque écouteurs appareil" },
	{ "Réseau", "VPN", "WireGuard, OpenVPN", "plume-vpn", "plume-vpn", "vpn tunnel wireguard openvpn" },
	{ "Écran et apparence", "Écrans", "Résolution, plusieurs écrans, HDR", "plume-ecrans", "plume-ecrans", "moniteur résolution hdr taille" },
	{ "Écran et apparence", "Apparence", "Fond d'écran, couleur du thème", "plume-fonds", "plume-fonds", "fond thème couleur wallpaper" },
	{ "Écran et apparence", "Lumière du soir", "Moins de lumière bleue le soir", "plume-nuit", "plume-nuit", "nuit soir bleu yeux" },
	{ "Son et notifications", "Son", "Volume, haut-parleurs, micro", "plume-son", "plume-son", "audio volume micro haut-parleur" },
	{ "Son et notifications", "Notifications", "Les messages reçus", "plume-notifications", "plume-notifications", "alertes messages historique" },
	{ "Clavier et accessibilité", "Raccourcis", "Tous les raccourcis clavier", "plume-raccourcis", "plume-raccourcis", "clavier touches raccourcis" },
	{ "Clavier et accessibilité", "Accessibilité", "Loupe, lecteur d'écran, clavier à l'écran", "plume-accessibilite", "plume-accessibilite", "loupe zoom lecteur orca malvoyant" },
	{ "Système", "Mises à jour", "Système et applications", "plume-maj",
	  "PLUME_MAJ_FENETRE=1 exec plume-terminal -a plume-maj -T 'Mises à jour' plume-maj", "maj mise à jour update" },
	{ "Système", "Énergie", "Batterie, mode économie", "plume-batterie", "plume-batterie", "batterie énergie économie performance" },
	{ "Système", "Sauvegarde", "Copier tes fichiers sur un disque", "plume-sauvegarde", "plume-sauvegarde", "sauvegarde backup disque copie" },
	{ "Système", "Retour en arrière", "Revenir avant une mise à jour", "plume-retour",
	  "plume-terminal -T 'Retour en arrière' sh -c \"sudo plume-retour; echo; echo 'Pour revenir : sudo plume-retour restaurer N'; exec bash\"",
	  "restaurer instantané snapshot annuler" },
};
#define NE ((int)(sizeof E / sizeof E[0]))

struct buffer { struct wl_buffer *wl; void *data; size_t size; cairo_surface_t *cs; int w, h; bool busy; };
struct rect { double x, y, w, h; };

static struct {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_callback *frame_cb;
	struct buffer buffers[2];
	int width, height;
	bool configured, dirty, running;
	struct xkb_context *xkb;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	double px, py, scroll, content_h;
	char query[64];
	struct rect hit[NE];
	bool shown[NE];
	int hover;
	cairo_surface_t *icons[NE];
} A = { .hover = -1 };

static void rgba(cairo_t *cr, uint32_t c, double a)
{
	cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_close_path(cr);
}

static double text(cairo_t *cr, const char *s, double px, int weight, double x, double y, uint32_t col, double a,
		   double maxw)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_font_description_set_weight(fd, weight);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	if (maxw > 0) {
		pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
		pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
	}
	pango_layout_set_text(l, s, -1);
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	cairo_move_to(cr, x, y - h / 2.0);
	rgba(cr, col, a);
	pango_cairo_show_layout(cr, l);
	cairo_new_path(cr);
	g_object_unref(l);
	return w;
}

static cairo_surface_t *icon(int i)
{
	if (A.icons[i])
		return A.icons[i];
	char p[256];
	snprintf(p, sizeof p, "/usr/share/icons/hicolor/scalable/apps/%s.svg", E[i].icon);
	RsvgHandle *h = rsvg_handle_new_from_file(p, NULL);
	if (!h)
		return NULL;
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 112, 112);
	cairo_t *cr = cairo_create(s);
	RsvgRectangle vp = { 0, 0, 112, 112 };
	rsvg_handle_render_document(h, cr, &vp, NULL);
	cairo_destroy(cr);
	g_object_unref(h);
	A.icons[i] = s;
	return s;
}

/* recherche sans accents ni majuscules (« ecran » trouve « Écrans ») */
static void fold(const char *in, char *out, size_t cap)
{
	static const char *from[] = { "é", "è", "ê", "ë", "à", "â", "î", "ï", "ô", "ù", "û", "ç", "É", "È", "À", NULL };
	static const char to[] = "eeeeaaiioouucEEA";
	size_t k = 0;
	while (*in && k + 1 < cap) {
		int m = -1;
		for (int i = 0; from[i]; i++)
			if (!strncmp(in, from[i], strlen(from[i]))) {
				m = i;
				break;
			}
		char c;
		if (m >= 0) {
			c = to[m];
			in += strlen(from[m]);
		} else {
			c = *in++;
		}
		out[k++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
	}
	out[k] = '\0';
}

static bool matches(int i)
{
	if (!A.query[0])
		return true;
	char q[64], hay[512], all[512];
	fold(A.query, q, sizeof q);
	snprintf(all, sizeof all, "%s %s %s %s", E[i].name, E[i].desc, E[i].section, E[i].keys);
	fold(all, hay, sizeof hay);
	return strstr(hay, q) != NULL;
}

static void launch(int i)
{
	pid_t p = fork();
	if (p == 0) {
		setsid();
		if (fork() == 0) {
			execl("/bin/sh", "sh", "-c", E[i].cmd, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0)
		waitpid(p, NULL, 0);
}

/* ------------------------------ dessin ------------------------------- */

static void draw(cairo_t *cr, double W, double H)
{
	rgba(cr, 0x050a10, 1);
	cairo_paint(cr);

	/* contenu (sous l'en-tête), qui défile */
	int cols = (int)((W - 2 * MARGIN + 16) / (COLS_MIN_W + 16));
	if (cols < 1) cols = 1;
	if (cols > 3) cols = 3;
	double tw = (W - 2 * MARGIN - (cols - 1) * 16) / cols;
	double y = HEADER_H + 18 - A.scroll;
	cairo_save(cr);
	cairo_rectangle(cr, 0, HEADER_H, W, H - HEADER_H);
	cairo_clip(cr);
	const char *sec = NULL;
	int col = 0, nshown = 0;
	for (int i = 0; i < NE; i++) {
		A.shown[i] = matches(i);
		A.hit[i] = (struct rect){ 0 };
		if (!A.shown[i])
			continue;
		nshown++;
		if (!sec || strcmp(sec, E[i].section)) {
			if (col) y += TILE_H + 14;
			col = 0;
			sec = E[i].section;
			y += 14;
			text(cr, sec, 12, PANGO_WEIGHT_BOLD, MARGIN, y, 0x4fb8e0, 1, 0);
			y += 18;
		}
		double x = MARGIN + col * (tw + 16);
		struct rect r = { x, y, tw, TILE_H };
		A.hit[i] = r;
		bool hv = i == A.hover;
		rrect(cr, r.x, r.y, r.w, r.h, 12);
		cairo_set_source_rgba(cr, 1, 1, 1, hv ? 0.09 : 0.045);
		cairo_fill_preserve(cr);
		rgba(cr, 0x4fb8e0, hv ? 0.9 : 0.18);
		cairo_set_line_width(cr, hv ? 1.5 : 1);
		cairo_stroke(cr);
		cairo_surface_t *ic = icon(i);
		if (ic) {
			cairo_save(cr);
			cairo_translate(cr, r.x + 10, r.y + (TILE_H - 56) / 2);
			cairo_scale(cr, 56.0 / 112, 56.0 / 112);
			cairo_set_source_surface(cr, ic, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint(cr);
			cairo_restore(cr);
		}
		text(cr, E[i].name, 15, PANGO_WEIGHT_SEMIBOLD, r.x + 78, r.y + TILE_H / 2 - 10, 0xffffff, 1, r.w - 90);
		text(cr, E[i].desc, 12, PANGO_WEIGHT_NORMAL, r.x + 78, r.y + TILE_H / 2 + 11, 0x9fc3d6, 1, r.w - 90);
		if (++col == cols) {
			col = 0;
			y += TILE_H + 14;
		}
	}
	if (col)
		y += TILE_H + 14;
	if (!nshown)
		text(cr, "Aucun réglage ne correspond à ta recherche.", 15, PANGO_WEIGHT_NORMAL, MARGIN, HEADER_H + 60,
		     0x9fc3d6, 1, 0);
	A.content_h = y + A.scroll - HEADER_H + 10;
	cairo_restore(cr);

	/* en-tête : pastilles, titre, recherche */
	cairo_rectangle(cr, 0, 0, W, HEADER_H);
	rgba(cr, 0x08131d, 1);
	cairo_fill(cr);
	cairo_rectangle(cr, 0, HEADER_H - 1, W, 1);
	rgba(cr, 0x4fb8e0, 0.35);
	cairo_fill(cr);
	pf_draw(cr, MARGIN, HEADER_H / 2, A.px, A.py);
	text(cr, "Paramètres", 20, PANGO_WEIGHT_BOLD, MARGIN + PF_LARGEUR, HEADER_H / 2, 0xffffff, 1, 0);
	double sw = fmin(320, W * 0.38), sx = W - MARGIN - sw, sy = HEADER_H / 2 - 17;
	rrect(cr, sx, sy, sw, 34, 17);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
	cairo_fill_preserve(cr);
	rgba(cr, 0x4fb8e0, A.query[0] ? 0.9 : 0.3);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	char buf[96];
	if (A.query[0])
		snprintf(buf, sizeof buf, "⌕  %s▏", A.query);
	text(cr, A.query[0] ? buf : "⌕  Rechercher (tape directement)", 13, PANGO_WEIGHT_NORMAL, sx + 14, HEADER_H / 2,
	     A.query[0] ? 0xffffff : 0x7f9fb2, 1, sw - 24);
}

/* ------------------------------ tampons ------------------------------ */

static void buffer_release(void *data, struct wl_buffer *b)
{
	(void)b;
	((struct buffer *)data)->busy = false;
}
static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static struct buffer *get_buffer(int w, int h)
{
	for (int i = 0; i < 2; i++) {
		struct buffer *b = &A.buffers[i];
		if (b->busy)
			continue;
		if (b->wl && (b->w != w || b->h != h)) {
			cairo_surface_destroy(b->cs);
			wl_buffer_destroy(b->wl);
			munmap(b->data, b->size);
			memset(b, 0, sizeof *b);
		}
		if (!b->wl) {
			int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
			size_t size = (size_t)stride * h;
			int fd = memfd_create("plume-parametres", MFD_CLOEXEC);
			if (fd < 0 || ftruncate(fd, size) < 0) {
				if (fd >= 0) close(fd);
				return NULL;
			}
			void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if (data == MAP_FAILED) {
				close(fd);
				return NULL;
			}
			struct wl_shm_pool *pool = wl_shm_create_pool(A.shm, fd, size);
			b->wl = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
			wl_shm_pool_destroy(pool);
			close(fd);
			b->data = data;
			b->size = size;
			b->w = w;
			b->h = h;
			b->cs = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
			wl_buffer_add_listener(b->wl, &buffer_listener, b);
		}
		return b;
	}
	return NULL;
}

static void frame_done(void *d, struct wl_callback *cb, uint32_t t)
{
	(void)d; (void)t;
	wl_callback_destroy(cb);
	A.frame_cb = NULL;
}
static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void render(void)
{
	if (!A.configured || A.width <= 0 || A.height <= 0)
		return;
	struct buffer *b = get_buffer(A.width, A.height);
	if (!b)
		return;
	cairo_t *cr = cairo_create(b->cs);
	draw(cr, A.width, A.height);
	cairo_destroy(cr);
	cairo_surface_flush(b->cs);
	wl_surface_attach(A.surface, b->wl, 0, 0);
	wl_surface_damage_buffer(A.surface, 0, 0, b->w, b->h);
	A.frame_cb = wl_surface_frame(A.surface);
	wl_callback_add_listener(A.frame_cb, &frame_listener, NULL);
	wl_surface_commit(A.surface);
	b->busy = true;
	A.dirty = false;
}

/* ------------------------------ xdg ---------------------------------- */

static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t s) { (void)d; xdg_wm_base_pong(b, s); }
static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void xs_configure(void *d, struct xdg_surface *x, uint32_t serial)
{
	(void)d;
	xdg_surface_ack_configure(x, serial);
	if (A.width <= 0 || A.height <= 0) {
		A.width = 960;
		A.height = 660;
	}
	A.configured = true;
	A.dirty = true;
}
static const struct xdg_surface_listener xs_listener = { .configure = xs_configure };

static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	(void)d; (void)t; (void)st;
	if (w > 0 && h > 0) {
		A.width = w;
		A.height = h;
	}
	A.dirty = true;
}
static void tl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; A.running = false; }
static const struct xdg_toplevel_listener tl_listener = { .configure = tl_configure, .close = tl_close };

/* ------------------------------ entrées ------------------------------ */

static int tile_at(double x, double y)
{
	if (y < HEADER_H)
		return -1;
	for (int i = 0; i < NE; i++) {
		struct rect r = A.hit[i];
		if (A.shown[i] && r.w > 0 && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h)
			return i;
	}
	return -1;
}

static void clamp_scroll(void)
{
	double max = A.content_h - (A.height - HEADER_H);
	if (A.scroll > max) A.scroll = max;
	if (A.scroll < 0) A.scroll = 0;
}

static struct wl_cursor_theme *cursor_theme;
static struct wl_surface *cursor_surface;

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)sf;
	A.px = wl_fixed_to_double(x);
	A.py = wl_fixed_to_double(y);
	struct wl_cursor *c = cursor_theme ? wl_cursor_theme_get_cursor(cursor_theme, "left_ptr") : NULL;
	if (c && c->image_count) {
		struct wl_cursor_image *img = c->images[0];
		wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
		wl_surface_damage(cursor_surface, 0, 0, img->width, img->height);
		wl_surface_commit(cursor_surface);
		wl_pointer_set_cursor(p, serial, cursor_surface, img->hotspot_x, img->hotspot_y);
	}
	A.dirty = true;
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)p; (void)s; (void)sf;
	A.px = A.py = -1;
	A.hover = -1;
	A.dirty = true;
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	A.px = wl_fixed_to_double(x);
	A.py = wl_fixed_to_double(y);
	if (pf_mouvement(A.px, A.py)) /* en-tête tenu et souris bougée : la fenêtre suit */
		xdg_toplevel_move(A.toplevel, A.seat, pf_serie);
	A.hover = tile_at(A.px, A.py);
	A.dirty = true;
}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)t;
	if (button != BTN_LEFT)
		return;
	if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
		pf_relache();
		return;
	}
	pf_serie = serial;
	switch (pf_clic(A.px, A.py)) {
	case 0: break;
	case 1: A.running = false; return;
	default: return;
	}
	int i = tile_at(A.px, A.py);
	if (i >= 0)
		launch(i);
	else if (A.py < HEADER_H)
		pf_entete_clic(A.px, A.py);
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
	(void)d; (void)p; (void)t;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
		return;
	A.scroll += wl_fixed_to_double(v) * 4;
	clamp_scroll();
	A.hover = tile_at(A.px, A.py);
	A.dirty = true;
}
static const struct wl_pointer_listener pointer_listener = {
	.enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button, .axis = p_axis,
};

static void kb_keymap(void *dd, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size)
{
	(void)dd; (void)k;
	if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED)
		return;
	struct xkb_keymap *km = xkb_keymap_new_from_string(A.xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(map, size);
	if (!km)
		return;
	if (A.xkb_state) xkb_state_unref(A.xkb_state);
	if (A.keymap) xkb_keymap_unref(A.keymap);
	A.keymap = km;
	A.xkb_state = xkb_state_new(km);
}
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys)
{
	(void)d; (void)k; (void)s; (void)sf; (void)keys;
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)k; (void)s; (void)sf;
}
static void kb_key(void *dd, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	(void)dd; (void)k; (void)serial; (void)time;
	if (!A.xkb_state || state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;
	xkb_keysym_t s = xkb_state_key_get_one_sym(A.xkb_state, key + 8);
	A.dirty = true;
	if (s == XKB_KEY_Escape) {
		if (A.query[0])
			A.query[0] = '\0';
		else
			A.running = false;
	} else if (s == XKB_KEY_BackSpace) {
		size_t n = strlen(A.query);
		while (n && (A.query[n - 1] & 0xc0) == 0x80)
			n--;
		if (n)
			n--;
		A.query[n] = '\0';
	} else if (s == XKB_KEY_Return || s == XKB_KEY_KP_Enter) {
		for (int i = 0; i < NE; i++)
			if (matches(i)) {
				launch(i);
				break;
			}
	} else {
		char u[16] = "";
		xkb_state_key_get_utf8(A.xkb_state, key + 8, u, sizeof u);
		if ((unsigned char)u[0] >= 0x20 && u[0] != 0x7f && strlen(A.query) + strlen(u) < sizeof A.query - 1) {
			strcat(A.query, u);
			A.scroll = 0;
		}
	}
}
static void kb_mods(void *dd, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lck,
		    uint32_t group)
{
	(void)dd; (void)k; (void)serial;
	if (A.xkb_state)
		xkb_state_update_mask(A.xkb_state, dep, lat, lck, 0, 0, group);
}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t dl) { (void)d; (void)k; (void)r; (void)dl; }
static const struct wl_keyboard_listener kb_listener = {
	.keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave, .key = kb_key, .modifiers = kb_mods,
	.repeat_info = kb_repeat,
};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	(void)d;
	bool kb = caps & WL_SEAT_CAPABILITY_KEYBOARD, ptr = caps & WL_SEAT_CAPABILITY_POINTER;
	if (ptr && !A.pointer) {
		A.pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(A.pointer, &pointer_listener, NULL);
	} else if (!ptr && A.pointer) {
		wl_pointer_release(A.pointer);
		A.pointer = NULL;
	}
	if (kb && !A.keyboard) {
		A.keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(A.keyboard, &kb_listener, NULL);
	} else if (!kb && A.keyboard) {
		wl_keyboard_release(A.keyboard);
		A.keyboard = NULL;
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_listener = { .capabilities = seat_caps, .name = seat_name };

static uint32_t minv(uint32_t a, uint32_t b) { return a < b ? a : b; }

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	(void)d;
	if (!strcmp(iface, wl_compositor_interface.name))
		A.compositor = wl_registry_bind(r, name, &wl_compositor_interface, minv(ver, 4));
	else if (!strcmp(iface, wl_shm_interface.name))
		A.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, wl_seat_interface.name) && !A.seat) {
		A.seat = wl_registry_bind(r, name, &wl_seat_interface, minv(ver, 4));
		wl_seat_add_listener(A.seat, &seat_listener, NULL);
	} else if (!strcmp(iface, xdg_wm_base_interface.name)) {
		A.wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(A.wm_base, &wm_listener, NULL);
	}
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = { .global = reg_global, .global_remove = reg_remove };

int main(int argc, char **argv)
{
	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		printf("Usage : %s    tous les réglages de Plume\n", argv[0]);
		return 0;
	}
	setlocale(LC_ALL, "");
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_DFL);
	A.display = wl_display_connect(NULL);
	if (!A.display) {
		fprintf(stderr, "plume-parametres : pas de connexion Wayland (lancez-le depuis le bureau)\n");
		return 1;
	}
	if (pf_deja_ouvert("plume-parametres"))
		return 0;
	A.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct wl_registry *reg = wl_display_get_registry(A.display);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(A.display);
	if (!A.compositor || !A.shm || !A.wm_base) {
		fprintf(stderr, "plume-parametres : compositeur incomplet\n");
		return 1;
	}
	A.surface = wl_compositor_create_surface(A.compositor);
	A.xdg_surface = xdg_wm_base_get_xdg_surface(A.wm_base, A.surface);
	xdg_surface_add_listener(A.xdg_surface, &xs_listener, NULL);
	A.toplevel = xdg_surface_get_toplevel(A.xdg_surface);
	xdg_toplevel_add_listener(A.toplevel, &tl_listener, NULL);
	xdg_toplevel_set_app_id(A.toplevel, "plume-parametres");
	xdg_toplevel_set_title(A.toplevel, "Paramètres");
	wl_surface_commit(A.surface);
	const char *xs = getenv("XCURSOR_SIZE");
	cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), xs && atoi(xs) > 0 ? atoi(xs) : 24, A.shm);
	cursor_surface = wl_compositor_create_surface(A.compositor);

	A.running = true;
	while (A.running) {
		if (A.dirty && !A.frame_cb && A.configured)
			render();
		while (wl_display_prepare_read(A.display) != 0)
			wl_display_dispatch_pending(A.display);
		wl_display_flush(A.display);
		struct pollfd fd = { .fd = wl_display_get_fd(A.display), .events = POLLIN };
		if (poll(&fd, 1, A.dirty && !A.frame_cb ? 0 : -1) < 0) {
			wl_display_cancel_read(A.display);
			if (errno == EINTR)
				continue;
			break;
		}
		if (fd.revents & POLLIN) {
			if (wl_display_read_events(A.display) < 0)
				break;
		} else {
			wl_display_cancel_read(A.display);
		}
		if (fd.revents & (POLLERR | POLLHUP))
			break;
		if (wl_display_dispatch_pending(A.display) < 0)
			break;
	}
	return 0;
}
