/*
 * plume-alimentation — Verrouiller · Veille · Déconnexion · Redémarrer · Éteindre (Plume Void).
 *
 * Écran plein, assombri, au-dessus de tout (wlr-layer-shell « overlay ») : ton nom, l'heure,
 * cinq gros boutons ronds. Souris ou clavier : ← → (ou Tab) pour choisir, Entrée pour valider,
 * Échap ou clic dans le vide pour annuler. Le relancer pendant qu'il est ouvert le referme.
 * Remplace la barre orange de swaynag (bouton ⏻ du dock et de la barre, Super + Maj + E).
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <errno.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#pragma GCC diagnostic ignored "-Wformat-truncation"

#define FONT "Inter, DejaVu Sans"
#define BTN 104.0   /* diamètre d'un bouton */
#define GAP 44.0

enum { B_LOCK, B_SLEEP, B_LOGOUT, B_REBOOT, B_OFF, NB };
static const char *LABEL[NB] = { "Verrouiller", "Veille", "Déconnexion", "Redémarrer", "Éteindre" };
static const char *CMD[NB] = {
	"plume-hud -f || swaylock -f -c 02060b",
	"sudo -n /usr/sbin/zzz",
	"swaymsg exit",
	"sudo -n /sbin/reboot",
	"sudo -n /sbin/poweroff",
};
/* couleur de chaque bouton (dégradé haut -> bas) */
static const uint32_t COL1[NB] = { 0x4fb8e0, 0x8b7cf6, 0x38bdf8, 0xf59e0b, 0xff5f57 };
static const uint32_t COL2[NB] = { 0x1d5a78, 0x4c3aa8, 0x0e6a96, 0xb45309, 0xb42318 };

struct buffer { struct wl_buffer *wl; void *data; size_t size; cairo_surface_t *cs; int w, h; bool busy; };

static struct {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct wl_callback *frame_cb;
	struct buffer buffers[2];
	int width, height;
	bool configured, dirty, running;
	struct xkb_context *xkb;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	double px, py;
	int hover, sel;         /* bouton survolé (souris) / choisi (clavier) */
	double anim[NB];        /* 0..1 : agrandissement au survol */
	double appear;          /* 0..1 : apparition en fondu */
	double t0;
	char name[128];
	struct { double x, y; } btn[NB];
} A = { .hover = -1, .sel = -1 };

static char pid_path[256];

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void rgb(cairo_t *cr, uint32_t c, double a)
{
	cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
}

/* une seule fois à l'écran : relancer la commande referme celui qui est ouvert */
static bool toggle_existing(void)
{
	const char *rt = getenv("XDG_RUNTIME_DIR");
	snprintf(pid_path, sizeof pid_path, "%s/plume-alimentation.pid", rt && *rt ? rt : "/tmp");
	FILE *f = fopen(pid_path, "r");
	if (f) {
		int pid = 0;
		if (fscanf(f, "%d", &pid) == 1 && pid > 1 && pid != getpid()) {
			char p[64], comm[32] = "";
			snprintf(p, sizeof p, "/proc/%d/comm", pid);
			FILE *c = fopen(p, "r");
			if (c) {
				if (fgets(comm, sizeof comm, c))
					comm[strcspn(comm, "\n")] = '\0';
				fclose(c);
			}
			if (!strncmp(comm, "plume-alimenta", 14) && kill(pid, SIGTERM) == 0) {
				fclose(f);
				return true;
			}
		}
		fclose(f);
	}
	f = fopen(pid_path, "w");
	if (f) {
		fprintf(f, "%d\n", getpid());
		fclose(f);
	}
	return false;
}

static void on_term(int s)
{
	(void)s;
	A.running = false;
}

/* prénom : le nom complet donné à l'installation (champ GECOS), sinon l'identifiant */
static void user_name(void)
{
	struct passwd *pw = getpwuid(getuid());
	const char *n = pw ? pw->pw_name : "";
	if (pw && pw->pw_gecos && pw->pw_gecos[0] && pw->pw_gecos[0] != ',')
		n = pw->pw_gecos;
	snprintf(A.name, sizeof A.name, "%s", n);
	char *c = strchr(A.name, ',');
	if (c)
		*c = '\0';
}

static void run(int b)
{
	A.running = false;
	unlink(pid_path);
	pid_t p = fork();
	if (p == 0) {
		setsid();
		if (fork() == 0) {
			/* l'écran se ferme d'abord (verrouillage : sinon on verrait ce voile par-dessus) */
			usleep(150000);
			execl("/bin/sh", "sh", "-c", CMD[b], (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0)
		waitpid(p, NULL, 0);
}

/* ------------------------------ dessin ------------------------------- */

static double text(cairo_t *cr, const char *s, double px, int weight, double x, double y, double a, uint32_t col)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string(FONT);
	pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
	pango_font_description_set_weight(fd, weight);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(l, s, -1);
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	cairo_move_to(cr, x - w / 2.0, y - h / 2.0);
	rgb(cr, col, a);
	pango_cairo_show_layout(cr, l);
	cairo_new_path(cr);
	g_object_unref(l);
	return w;
}

/* pictogrammes, dessinés au trait blanc dans un cercle de rayon r */
static void icon(cairo_t *cr, int b, double cx, double cy, double r)
{
	double s = r / 52.0; /* dessinés pour un bouton de 104 px */
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_scale(cr, s, s);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_line_width(cr, 4.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_new_path(cr);
	switch (b) {
	case B_LOCK:
		cairo_rectangle(cr, -14, -4, 28, 22);
		cairo_fill(cr);
		cairo_arc(cr, 0, -6, 9, M_PI, 2 * M_PI);
		cairo_line_to(cr, 9, -4);
		cairo_move_to(cr, -9, -4);
		cairo_line_to(cr, -9, -6);
		cairo_stroke(cr);
		break;
	case B_SLEEP: /* croissant de lune : un disque dont on retire un disque décalé */
		cairo_push_group(cr);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_arc(cr, -2, 2, 18, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_arc(cr, 9, -6, 15, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_pop_group_to_source(cr);
		cairo_paint(cr);
		break;
	case B_LOGOUT: /* porte et flèche qui sort */
		cairo_move_to(cr, -2, -17);
		cairo_line_to(cr, -16, -17);
		cairo_line_to(cr, -16, 17);
		cairo_line_to(cr, -2, 17);
		cairo_move_to(cr, -4, 0);
		cairo_line_to(cr, 18, 0);
		cairo_move_to(cr, 10, -8);
		cairo_line_to(cr, 18, 0);
		cairo_line_to(cr, 10, 8);
		cairo_stroke(cr);
		break;
	case B_REBOOT: /* flèche en cercle */
		cairo_arc(cr, 0, 0, 16, -M_PI / 2 + 0.5, 1.5 * M_PI - 0.1);
		cairo_stroke(cr);
		cairo_move_to(cr, -1, -24);
		cairo_line_to(cr, 8, -16);
		cairo_line_to(cr, -1, -8);
		cairo_stroke(cr);
		break;
	case B_OFF: /* ⏻ */
		cairo_arc(cr, 0, 2, 16, -M_PI / 2 + 0.7, 1.5 * M_PI - 0.7);
		cairo_stroke(cr);
		cairo_move_to(cr, 0, -20);
		cairo_line_to(cr, 0, -2);
		cairo_stroke(cr);
		break;
	}
	cairo_restore(cr);
}

static void draw(cairo_t *cr)
{
	int W = A.width, H = A.height;
	double ap = A.appear;
	/* voile sombre aux couleurs du thème */
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_pattern_t *g = cairo_pattern_create_radial(W / 2.0, H / 2.0, 0, W / 2.0, H / 2.0, fmax(W, H) * 0.7);
	cairo_pattern_add_color_stop_rgba(g, 0, 0.02, 0.05, 0.09, 0.80 * ap);
	cairo_pattern_add_color_stop_rgba(g, 1, 0.0, 0.0, 0.0, 0.92 * ap);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* heure, puis le nom */
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	char hm[8];
	snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
	double cy = H / 2.0, lift = (1 - ap) * 20;
	text(cr, hm, 64, PANGO_WEIGHT_LIGHT, W / 2.0, cy - 175 + lift, ap, 0xffffff);
	char hello[200];
	snprintf(hello, sizeof hello, "À bientôt, %s", A.name);
	text(cr, hello, 20, PANGO_WEIGHT_NORMAL, W / 2.0, cy - 112 + lift, 0.85 * ap, 0xcdeefc);

	/* boutons */
	double total = NB * BTN + (NB - 1) * GAP, x0 = W / 2.0 - total / 2 + BTN / 2;
	for (int b = 0; b < NB; b++) {
		double bx = x0 + b * (BTN + GAP), by = cy + 10 + lift;
		A.btn[b].x = bx;
		A.btn[b].y = by;
		double k = A.anim[b], r = BTN / 2 * (1 + 0.08 * k);
		bool active = b == A.hover || b == A.sel;
		cairo_new_path(cr);
		/* halo */
		if (k > 0.02) {
			cairo_pattern_t *h = cairo_pattern_create_radial(bx, by, r * 0.8, bx, by, r * 1.6);
			cairo_pattern_add_color_stop_rgba(h, 0, ((COL1[b] >> 16) & 255) / 255.0, ((COL1[b] >> 8) & 255) / 255.0,
							  (COL1[b] & 255) / 255.0, 0.45 * k * ap);
			cairo_pattern_add_color_stop_rgba(h, 1, 0, 0, 0, 0);
			cairo_set_source(cr, h);
			cairo_arc(cr, bx, by, r * 1.6, 0, 2 * M_PI);
			cairo_fill(cr);
			cairo_pattern_destroy(h);
		}
		/* disque : verre sombre, qui prend la couleur du bouton au survol */
		cairo_pattern_t *f = cairo_pattern_create_linear(bx, by - r, bx, by + r);
		double m = 0.25 + 0.75 * k;
		cairo_pattern_add_color_stop_rgba(f, 0, ((COL1[b] >> 16) & 255) / 255.0 * m, ((COL1[b] >> 8) & 255) / 255.0 * m,
						  (COL1[b] & 255) / 255.0 * m, (0.55 + 0.45 * k) * ap);
		cairo_pattern_add_color_stop_rgba(f, 1, ((COL2[b] >> 16) & 255) / 255.0 * m, ((COL2[b] >> 8) & 255) / 255.0 * m,
						  (COL2[b] & 255) / 255.0 * m, (0.55 + 0.45 * k) * ap);
		cairo_new_path(cr);
		cairo_arc(cr, bx, by, r, 0, 2 * M_PI);
		cairo_set_source(cr, f);
		cairo_fill_preserve(cr);
		cairo_pattern_destroy(f);
		rgb(cr, active ? 0xffffff : COL1[b], (active ? 0.9 : 0.55) * ap);
		cairo_set_line_width(cr, active ? 2 : 1.5);
		cairo_stroke(cr);
		cairo_push_group(cr);
		icon(cr, b, bx, by, r);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, ap);
		text(cr, LABEL[b], 15, active ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, bx, by + BTN / 2 + 30, ap,
		     active ? 0xffffff : 0xcdeefc);
	}
	text(cr, "← → pour choisir · Entrée pour valider · Échap pour annuler", 13, PANGO_WEIGHT_NORMAL, W / 2.0,
	     H - 60, 0.55 * ap, 0xcdeefc);
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
			int fd = memfd_create("plume-alimentation", MFD_CLOEXEC);
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
	draw(cr);
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

static bool animate(double dt)
{
	bool moving = false;
	double k = fmin(1, dt / 0.12);
	for (int b = 0; b < NB; b++) {
		double target = (b == A.hover || b == A.sel) ? 1 : 0;
		double v = A.anim[b] + (target - A.anim[b]) * k;
		if (fabs(v - target) < 0.01)
			v = target;
		if (v != A.anim[b]) {
			A.anim[b] = v;
			moving = true;
		}
	}
	double ap = fmin(1, (now_s() - A.t0) / 0.18);
	if (ap != A.appear) {
		A.appear = ap;
		moving = true;
	}
	return moving;
}

/* ------------------------------ entrées ------------------------------ */

static int button_at(double x, double y)
{
	for (int b = 0; b < NB; b++) {
		double dx = x - A.btn[b].x, dy = y - A.btn[b].y;
		if (dx * dx + dy * dy <= (BTN / 2 + 6) * (BTN / 2 + 6) ||
		    (fabs(dx) <= BTN / 2 + 10 && dy > 0 && dy <= BTN / 2 + 44)) /* le texte aussi */
			return b;
	}
	return -1;
}

static struct wl_cursor_theme *cursor_theme;
static struct wl_surface *cursor_surface;

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)sf;
	A.px = wl_fixed_to_double(x);
	A.py = wl_fixed_to_double(y);
	A.hover = button_at(A.px, A.py);
	struct wl_cursor *c = cursor_theme ? wl_cursor_theme_get_cursor(cursor_theme, "left_ptr") : NULL;
	if (c && c->image_count) {
		struct wl_cursor_image *img = c->images[0];
		wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
		wl_surface_damage(cursor_surface, 0, 0, img->width, img->height);
		wl_surface_commit(cursor_surface);
		wl_pointer_set_cursor(p, serial, cursor_surface, img->hotspot_x, img->hotspot_y);
	}
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)p; (void)s; (void)sf;
	A.hover = -1;
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	A.px = wl_fixed_to_double(x);
	A.py = wl_fixed_to_double(y);
	int h = button_at(A.px, A.py);
	if (h != A.hover) {
		A.hover = h;
		if (h >= 0)
			A.sel = -1; /* la souris reprend la main sur le clavier */
	}
}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)serial; (void)t;
	if (state != WL_POINTER_BUTTON_STATE_PRESSED || button != BTN_LEFT)
		return;
	int b = button_at(A.px, A.py);
	if (b >= 0)
		run(b);
	else
		A.running = false; /* clic dans le vide : annuler */
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
	(void)d; (void)p; (void)t; (void)a; (void)v;
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
	int cur = A.sel >= 0 ? A.sel : A.hover;
	if (s == XKB_KEY_Escape) {
		A.running = false;
	} else if (s == XKB_KEY_Right || s == XKB_KEY_Tab || s == XKB_KEY_Down) {
		A.sel = cur < 0 ? 0 : (cur + 1) % NB;
		A.hover = -1;
	} else if (s == XKB_KEY_Left || s == XKB_KEY_ISO_Left_Tab || s == XKB_KEY_Up) {
		A.sel = cur < 0 ? NB - 1 : (cur + NB - 1) % NB;
		A.hover = -1;
	} else if ((s == XKB_KEY_Return || s == XKB_KEY_KP_Enter || s == XKB_KEY_space) && cur >= 0) {
		run(cur);
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

static void layer_configure(void *d, struct zwlr_layer_surface_v1 *l, uint32_t serial, uint32_t w, uint32_t h)
{
	(void)d;
	zwlr_layer_surface_v1_ack_configure(l, serial);
	A.width = w;
	A.height = h;
	A.configured = true;
	A.dirty = true;
}
static void layer_closed(void *d, struct zwlr_layer_surface_v1 *l)
{
	(void)d; (void)l;
	A.running = false;
}
static const struct zwlr_layer_surface_v1_listener layer_listener = { .configure = layer_configure, .closed = layer_closed };

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
	} else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
		A.layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, minv(ver, 4));
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = { .global = reg_global, .global_remove = reg_remove };

int main(int argc, char **argv)
{
	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		printf("Usage : %s    Verrouiller · Veille · Déconnexion · Redémarrer · Éteindre\n", argv[0]);
		return 0;
	}
	setlocale(LC_ALL, "");
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	if (toggle_existing())
		return 0;
	user_name();
	A.display = wl_display_connect(NULL);
	if (!A.display) {
		fprintf(stderr, "plume-alimentation : pas de connexion Wayland\n");
		unlink(pid_path);
		return 1;
	}
	A.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct wl_registry *reg = wl_display_get_registry(A.display);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(A.display);
	if (!A.compositor || !A.shm || !A.layer_shell) {
		fprintf(stderr, "plume-alimentation : le compositeur ne gère pas wlr-layer-shell\n");
		unlink(pid_path);
		return 1;
	}
	const char *xs = getenv("XCURSOR_SIZE");
	cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), xs && atoi(xs) > 0 ? atoi(xs) : 24, A.shm);
	cursor_surface = wl_compositor_create_surface(A.compositor);

	A.surface = wl_compositor_create_surface(A.compositor);
	A.layer = zwlr_layer_shell_v1_get_layer_surface(A.layer_shell, A.surface, NULL,
							ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "plume-alimentation");
	zwlr_layer_surface_v1_add_listener(A.layer, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(A.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
						  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(A.layer, -1); /* par-dessus la barre et le dock */
	zwlr_layer_surface_v1_set_keyboard_interactivity(A.layer, 1);
	wl_surface_commit(A.surface);

	int tick = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	struct itimerspec its = { { 0, 16000000 }, { 0, 16000000 } };
	timerfd_settime(tick, 0, &its, NULL);
	A.t0 = now_s();
	double last = A.t0;
	A.running = true;
	while (A.running) {
		if (A.dirty && !A.frame_cb && A.configured)
			render();
		while (wl_display_prepare_read(A.display) != 0)
			wl_display_dispatch_pending(A.display);
		wl_display_flush(A.display);
		struct pollfd fds[2] = { { .fd = wl_display_get_fd(A.display), .events = POLLIN },
					 { .fd = tick, .events = POLLIN } };
		if (poll(fds, 2, -1) < 0) {
			wl_display_cancel_read(A.display);
			if (errno == EINTR)
				continue;
			break;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(A.display) < 0)
				break;
		} else {
			wl_display_cancel_read(A.display);
		}
		if (fds[0].revents & (POLLERR | POLLHUP))
			break;
		if (wl_display_dispatch_pending(A.display) < 0)
			break;
		if (fds[1].revents & POLLIN) {
			uint64_t e;
			if (read(tick, &e, sizeof e) < 0) { /* rien */ }
			double t = now_s();
			if (animate(t - last))
				A.dirty = true;
			last = t;
			static int min = -1;
			time_t w = time(NULL);
			struct tm tm;
			localtime_r(&w, &tm);
			if (tm.tm_min != min) {
				min = tm.tm_min;
				A.dirty = true;
			}
		}
	}
	unlink(pid_path);
	return 0;
}
