/* plume-hud — connexion Wayland, verrouillage, clavier/souris et boucle principale. */
#include "hud.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-cursor.h>

#include "xdg-shell-client-protocol.h"
#include "ext-session-lock-v1-client-protocol.h"

static struct hud H;

double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

void hud_log(struct hud *h, int kind, const char *fmt, ...)
{
	if (h->log_n == HUD_LOG_MAX) {
		memmove(&h->log[0], &h->log[1], sizeof(h->log[0]) * (HUD_LOG_MAX - 1));
		h->log_n--;
	}
	struct log_entry *e = &h->log[h->log_n++];
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(e->ts, sizeof e->ts, "%H:%M:%S", &tm);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(e->text, sizeof e->text, fmt, ap);
	va_end(ap);
	e->kind = kind;
	e->born = now_s();
}

void hud_set_msg(struct hud *h, int kind, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(h->msg, sizeof h->msg, fmt, ap);
	va_end(ap);
	h->msg_kind = kind;
}

static void default_msg(void)
{
	if (H.mode == MODE_GREETER)
		hud_set_msg(&H, MSG_NORMAL, "Identifiez-vous pour ouvrir une session");
	else
		hud_set_msg(&H, MSG_NORMAL, "Entrez votre mot de passe pour continuer");
}

/* ------------------------------------------------------------------ */
/* Tampons partagés (wl_shm)                                           */
/* ------------------------------------------------------------------ */

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	(void)wl_buffer;
	((struct buffer *)data)->busy = false;
}

static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static void buffer_destroy(struct buffer *b)
{
	if (b->cs)
		cairo_surface_destroy(b->cs);
	if (b->wl)
		wl_buffer_destroy(b->wl);
	if (b->data)
		munmap(b->data, b->size);
	memset(b, 0, sizeof *b);
}

static struct buffer *get_buffer(struct output *o, int w, int h)
{
	for (int i = 0; i < 2; i++) {
		struct buffer *b = &o->buffers[i];
		if (b->busy)
			continue;
		if (b->wl && (b->w != w || b->h != h))
			buffer_destroy(b);
		if (!b->wl) {
			int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
			size_t size = (size_t)stride * h;
			int fd = memfd_create("plume-hud", MFD_CLOEXEC);
			if (fd < 0)
				return NULL;
			if (ftruncate(fd, size) < 0) {
				close(fd);
				return NULL;
			}
			void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if (data == MAP_FAILED) {
				close(fd);
				return NULL;
			}
			struct wl_shm_pool *pool = wl_shm_create_pool(H.shm, fd, size);
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
	return NULL; /* les deux tampons sont chez le compositeur : on saute cette image */
}

static void render_output(struct output *o)
{
	if (!o->configured || o->width <= 0 || o->height <= 0)
		return;
	int s = o->scale > 0 ? o->scale : 1;
	struct buffer *b = get_buffer(o, o->width * s, o->height * s);
	if (!b)
		return;
	cairo_t *cr = cairo_create(b->cs);
	cairo_scale(cr, s, s);
	draw_frame(&H, o, cr, o->width, o->height);
	cairo_destroy(cr);
	cairo_surface_flush(b->cs);
	wl_surface_set_buffer_scale(o->surface, s);
	wl_surface_attach(o->surface, b->wl, 0, 0);
	wl_surface_damage_buffer(o->surface, 0, 0, b->w, b->h);
	wl_surface_commit(o->surface);
	b->busy = true;
}

static void render_all(void)
{
	struct output *o;
	wl_list_for_each(o, &H.outputs, link)
		render_output(o);
}

/* ------------------------------------------------------------------ */
/* Verrouillage (ext-session-lock-v1)                                   */
/* ------------------------------------------------------------------ */

static void lock_surface_configure(void *data, struct ext_session_lock_surface_v1 *ls,
				   uint32_t serial, uint32_t w, uint32_t h)
{
	struct output *o = data;
	o->width = w;
	o->height = h;
	ext_session_lock_surface_v1_ack_configure(ls, serial);
	o->configured = true;
	render_output(o);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
	.configure = lock_surface_configure,
};

static void create_lock_surface(struct output *o)
{
	if (o->lock_surface || !H.lock || !o->wl_output)
		return;
	o->surface = wl_compositor_create_surface(H.compositor);
	o->lock_surface = ext_session_lock_v1_get_lock_surface(H.lock, o->surface, o->wl_output);
	ext_session_lock_surface_v1_add_listener(o->lock_surface, &lock_surface_listener, o);
}

static void lock_locked(void *data, struct ext_session_lock_v1 *lock)
{
	(void)data; (void)lock;
	H.locked = true;
	if (H.daemonize) {
		/* comme `swaylock -f` : on rend la main une fois l'écran vraiment verrouillé */
		pid_t p = fork();
		if (p > 0)
			_exit(0);
		if (p == 0)
			setsid();
	}
}

static void lock_finished(void *data, struct ext_session_lock_v1 *lock)
{
	(void)data; (void)lock;
	if (!H.locked)
		fprintf(stderr, "plume-hud : verrouillage refusé (session déjà verrouillée ?)\n");
	H.exit_code = H.locked ? 0 : 1;
	H.running = false;
}

static const struct ext_session_lock_v1_listener lock_listener = {
	.locked = lock_locked,
	.finished = lock_finished,
};

/* ------------------------------------------------------------------ */
/* Fenêtre (greeter / test) via xdg-shell                              */
/* ------------------------------------------------------------------ */

static void wm_ping(void *data, struct xdg_wm_base *b, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(b, serial);
}

static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void xsurf_configure(void *data, struct xdg_surface *x, uint32_t serial)
{
	struct output *o = data;
	xdg_surface_ack_configure(x, serial);
	if (o->width <= 0 || o->height <= 0) {
		o->width = 1280;
		o->height = 720;
	}
	o->configured = true;
	render_output(o);
}

static const struct xdg_surface_listener xsurf_listener = { .configure = xsurf_configure };

static void top_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	(void)t; (void)st;
	struct output *o = data;
	if (w > 0 && h > 0) {
		o->width = w;
		o->height = h;
	}
}

static void top_close(void *data, struct xdg_toplevel *t)
{
	(void)data; (void)t;
	if (H.mode == MODE_DEV)
		H.running = false;
}

static const struct xdg_toplevel_listener top_listener = {
	.configure = top_configure,
	.close = top_close,
};

static void create_window(void)
{
	struct output *o = calloc(1, sizeof *o);
	o->scale = 1;
	o->surface = wl_compositor_create_surface(H.compositor);
	o->xdg_surface = xdg_wm_base_get_xdg_surface(H.wm_base, o->surface);
	xdg_surface_add_listener(o->xdg_surface, &xsurf_listener, o);
	o->xdg_toplevel = xdg_surface_get_toplevel(o->xdg_surface);
	xdg_toplevel_add_listener(o->xdg_toplevel, &top_listener, o);
	xdg_toplevel_set_app_id(o->xdg_toplevel, "plume-hud");
	if (H.mode == MODE_GREETER) {
		xdg_toplevel_set_title(o->xdg_toplevel, "Plume — connexion");
		xdg_toplevel_set_fullscreen(o->xdg_toplevel, NULL);
	} else {
		xdg_toplevel_set_title(o->xdg_toplevel, "Plume HUD (mode test)");
		xdg_toplevel_set_min_size(o->xdg_toplevel, 960, 540);
	}
	wl_surface_commit(o->surface);
	wl_list_insert(&H.outputs, &o->link);
}

/* ------------------------------------------------------------------ */
/* Écrans (wl_output)                                                  */
/* ------------------------------------------------------------------ */

static void out_geometry(void *d, struct wl_output *w, int32_t x, int32_t y, int32_t pw, int32_t ph,
			 int32_t sp, const char *make, const char *model, int32_t tr)
{
	(void)d; (void)w; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)make; (void)model; (void)tr;
}
static void out_mode(void *d, struct wl_output *w, uint32_t f, int32_t wd, int32_t ht, int32_t r)
{
	(void)d; (void)w; (void)f; (void)wd; (void)ht; (void)r;
}
static void out_done(void *d, struct wl_output *w) { (void)d; (void)w; }
static void out_scale(void *d, struct wl_output *w, int32_t s)
{
	(void)w;
	struct output *o = d;
	o->scale = s;
	if (o->configured)
		render_output(o);
}
static void out_name(void *d, struct wl_output *w, const char *n) { (void)d; (void)w; (void)n; }
static void out_desc(void *d, struct wl_output *w, const char *n) { (void)d; (void)w; (void)n; }

static const struct wl_output_listener output_listener = {
	.geometry = out_geometry,
	.mode = out_mode,
	.done = out_done,
	.scale = out_scale,
	.name = out_name,
	.description = out_desc,
};

static void output_destroy(struct output *o)
{
	for (int i = 0; i < 2; i++)
		buffer_destroy(&o->buffers[i]);
	if (o->bg_cache)
		cairo_surface_destroy(o->bg_cache);
	if (o->lock_surface)
		ext_session_lock_surface_v1_destroy(o->lock_surface);
	if (o->xdg_toplevel)
		xdg_toplevel_destroy(o->xdg_toplevel);
	if (o->xdg_surface)
		xdg_surface_destroy(o->xdg_surface);
	if (o->surface)
		wl_surface_destroy(o->surface);
	if (o->wl_output)
		wl_output_destroy(o->wl_output);
	wl_list_remove(&o->link);
	free(o);
}

/* ------------------------------------------------------------------ */
/* Clavier                                                             */
/* ------------------------------------------------------------------ */

static void disarm_repeat(void)
{
	struct itimerspec its = { 0 };
	timerfd_settime(H.repeat_fd, 0, &its, NULL);
	H.repeat_key = 0;
}

static void arm_repeat(void)
{
	if (H.repeat_rate <= 0)
		return;
	struct itimerspec its = { 0 };
	its.it_value.tv_sec = H.repeat_delay / 1000;
	its.it_value.tv_nsec = (H.repeat_delay % 1000) * 1000000L;
	long iv = 1000000000L / H.repeat_rate;
	its.it_interval.tv_sec = iv / 1000000000L;
	its.it_interval.tv_nsec = iv % 1000000000L;
	timerfd_settime(H.repeat_fd, 0, &its, NULL);
}

static char *active_field(size_t *cap)
{
	if (H.mode == MODE_GREETER && H.focus == 0) {
		*cap = sizeof H.user;
		return H.user;
	}
	*cap = sizeof H.pw;
	return H.pw;
}

static void utf8_pop(char *s)
{
	size_t n = strlen(s);
	while (n > 0) {
		n--;
		if (((unsigned char)s[n] & 0xC0) != 0x80)
			break;
	}
	s[n] = '\0';
}

static void submit(void)
{
	if (H.mode == MODE_GREETER) {
		if (!H.user[0]) {
			H.focus = 0;
			hud_set_msg(&H, MSG_ERR, "Entrez un nom d'utilisateur");
			return;
		}
		if (H.focus == 0) {
			H.focus = 1;
			if (!H.pw[0])
				return;
		}
	}
	if (!H.pw[0])
		return;
	if (auth_start(&H) == 0)
		hud_set_msg(&H, MSG_NORMAL, "Vérification…");
	else
		hud_set_msg(&H, MSG_ERR, "Erreur interne (fork)");
}

static void handle_key(xkb_keysym_t sym, const char *utf8)
{
	if (H.unlocking || H.authenticating)
		return;
	if (sym == XKB_KEY_Escape) {
		if (H.mode == MODE_DEV) {
			H.running = false;
			return;
		}
		explicit_bzero(H.pw, sizeof H.pw);
		return;
	}
	if (now_s() < H.locked_until)
		return;
	if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) {
		if (H.mode == MODE_GREETER)
			H.focus ^= 1;
		return;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		submit();
		return;
	}
	size_t cap;
	char *f = active_field(&cap);
	if (sym == XKB_KEY_BackSpace) {
		utf8_pop(f);
		return;
	}
	if (H.xkb_state && xkb_state_mod_name_is_active(H.xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) {
		if (sym == XKB_KEY_u || sym == XKB_KEY_U)
			explicit_bzero(f, cap);
		return;
	}
	if ((unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f) {
		size_t n = strlen(f), m = strlen(utf8);
		if (n + m < cap) {
			memcpy(f + n, utf8, m + 1);
			if (H.msg_kind == MSG_ERR)
				default_msg();
		}
	}
}

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size)
{
	(void)d; (void)k;
	if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED)
		return;
	struct xkb_keymap *km = xkb_keymap_new_from_string(H.xkb_ctx, map, XKB_KEYMAP_FORMAT_TEXT_V1,
							  XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	if (!km)
		return;
	if (H.xkb_state)
		xkb_state_unref(H.xkb_state);
	if (H.keymap)
		xkb_keymap_unref(H.keymap);
	H.keymap = km;
	H.xkb_state = xkb_state_new(km);
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys)
{
	(void)d; (void)k; (void)s; (void)sf; (void)keys;
}

static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)k; (void)s; (void)sf;
	disarm_repeat();
}

static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state)
{
	(void)d; (void)k; (void)serial; (void)t;
	if (!H.xkb_state)
		return;
	uint32_t code = key + 8;
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		xkb_keysym_t sym = xkb_state_key_get_one_sym(H.xkb_state, code);
		char utf8[8] = { 0 };
		xkb_state_key_get_utf8(H.xkb_state, code, utf8, sizeof utf8);
		handle_key(sym, utf8);
		bool repeatable = sym != XKB_KEY_Return && sym != XKB_KEY_KP_Enter && sym != XKB_KEY_Escape &&
				  sym != XKB_KEY_Tab && xkb_keymap_key_repeats(H.keymap, code);
		if (repeatable) {
			H.repeat_key = key;
			H.repeat_sym = sym;
			memcpy(H.repeat_utf8, utf8, sizeof utf8);
			arm_repeat();
		}
	} else if (key == H.repeat_key) {
		disarm_repeat();
	}
	render_all();
}

static void kb_mods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat,
		    uint32_t lck, uint32_t group)
{
	(void)d; (void)k; (void)serial;
	if (H.xkb_state)
		xkb_state_update_mask(H.xkb_state, dep, lat, lck, 0, 0, group);
}

static void kb_repeat_info(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
	(void)d; (void)k;
	H.repeat_rate = rate;
	H.repeat_delay = delay;
}

static const struct wl_keyboard_listener kb_listener = {
	.keymap = kb_keymap,
	.enter = kb_enter,
	.leave = kb_leave,
	.key = kb_key,
	.modifiers = kb_mods,
	.repeat_info = kb_repeat_info,
};

/* ------------------------------------------------------------------ */
/* Souris                                                              */
/* ------------------------------------------------------------------ */

static bool in_rect(struct rect r, double x, double y)
{
	return r.w > 0 && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	(void)d;
	H.pointer_output = NULL;
	struct output *o;
	wl_list_for_each(o, &H.outputs, link)
		if (o->surface == sf)
			H.pointer_output = o;
	H.px = wl_fixed_to_double(x);
	H.py = wl_fixed_to_double(y);
	if (H.cursor_theme && H.cursor_surface) {
		struct wl_cursor *c = wl_cursor_theme_get_cursor(H.cursor_theme, "left_ptr");
		if (c && c->image_count > 0) {
			struct wl_cursor_image *img = c->images[0];
			wl_surface_attach(H.cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
			wl_surface_damage(H.cursor_surface, 0, 0, img->width, img->height);
			wl_surface_commit(H.cursor_surface);
			wl_pointer_set_cursor(p, serial, H.cursor_surface, img->hotspot_x, img->hotspot_y);
		}
	}
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)p; (void)s; (void)sf;
	H.pointer_output = NULL;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	H.px = wl_fixed_to_double(x);
	H.py = wl_fixed_to_double(y);
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)serial; (void)t; (void)button;
	if (state != WL_POINTER_BUTTON_STATE_PRESSED || !H.pointer_output)
		return;
	struct output *o = H.pointer_output;
	if (in_rect(o->button_rect, H.px, H.py)) {
		if (!H.unlocking && !H.authenticating && now_s() >= H.locked_until)
			submit();
	} else if (H.mode == MODE_GREETER && in_rect(o->field_rect[0], H.px, H.py)) {
		H.focus = 0;
	} else {
		H.focus = 1; /* comme l'original : un clic remet le focus sur le mot de passe */
	}
	render_all();
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
	(void)d; (void)p; (void)t; (void)a; (void)v;
}

static const struct wl_pointer_listener ptr_listener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
};

/* ------------------------------------------------------------------ */
/* Seat & registre                                                     */
/* ------------------------------------------------------------------ */

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	(void)d;
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !H.keyboard) {
		H.keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(H.keyboard, &kb_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !H.pointer) {
		H.pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(H.pointer, &ptr_listener, NULL);
	}
}

static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps,
	.name = seat_name,
};

static uint32_t minv(uint32_t a, uint32_t b) { return a < b ? a : b; }

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	(void)d;
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		H.compositor = wl_registry_bind(r, name, &wl_compositor_interface, minv(ver, 4));
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		H.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0 && !H.seat) {
		H.seat = wl_registry_bind(r, name, &wl_seat_interface, minv(ver, 4));
		wl_seat_add_listener(H.seat, &seat_listener, NULL);
	} else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
		H.wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(H.wm_base, &wm_listener, NULL);
	} else if (strcmp(iface, ext_session_lock_manager_v1_interface.name) == 0) {
		H.lock_manager = wl_registry_bind(r, name, &ext_session_lock_manager_v1_interface, 1);
	} else if (strcmp(iface, wl_output_interface.name) == 0 && H.mode == MODE_LOCK) {
		struct output *o = calloc(1, sizeof *o);
		o->scale = 1;
		o->global_name = name;
		o->wl_output = wl_registry_bind(r, name, &wl_output_interface, minv(ver, 3));
		wl_output_add_listener(o->wl_output, &output_listener, o);
		wl_list_insert(&H.outputs, &o->link);
		create_lock_surface(o); /* écran branché après le verrouillage */
	}
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t name)
{
	(void)d; (void)r;
	struct output *o, *tmp;
	wl_list_for_each_safe(o, tmp, &H.outputs, link)
		if (o->global_name == name && o->wl_output)
			output_destroy(o);
}

static const struct wl_registry_listener reg_listener = {
	.global = reg_global,
	.global_remove = reg_remove,
};

/* ------------------------------------------------------------------ */
/* Authentification terminée / animation / boucle                      */
/* ------------------------------------------------------------------ */

void hud_auth_done(struct hud *h, int result, const char *desc)
{
	h->authenticating = false;
	explicit_bzero(h->pw, sizeof h->pw);
	double t = now_s();
	if (result == 0) {
		hud_set_msg(h, MSG_OK, "ACCÈS AUTORISÉ");
		if (h->mode == MODE_GREETER)
			hud_log(h, MSG_OK, "Accès autorisé — ouverture de la session de %s", h->user);
		else
			hud_log(h, MSG_OK, "Accès autorisé — déverrouillage");
		h->unlocking = true;
		h->unlock_start = t;
		return;
	}
	h->shake_start = t;
	if (result == 2) {
		hud_set_msg(h, MSG_ERR, "%s", desc && *desc ? desc : "Erreur d'authentification");
		hud_log(h, MSG_ERR, "Erreur : %s", desc && *desc ? desc : "authentification impossible");
		return;
	}
	h->failed++;
	if (h->failed >= HUD_MAX_ATTEMPTS) {
		h->failed = 0;
		h->locked_until = t + HUD_LOCKOUT_SECONDS;
		hud_log(h, MSG_ERR, "Trop de tentatives — verrouillage %ds", HUD_LOCKOUT_SECONDS);
	} else {
		hud_set_msg(h, MSG_ERR, "Mot de passe incorrect — %d essai(s) restant(s)",
			    HUD_MAX_ATTEMPTS - h->failed);
		hud_log(h, MSG_ERR, "Tentative d'accès refusée");
	}
}

static double last_tick;
static bool in_lockout;

static void on_tick(void)
{
	double t = now_s();
	double dt = last_tick > 0 ? t - last_tick : 0;
	last_tick = t;

	if (t - H.last_sys >= 2.0) {
		sys_update(&H);
		H.last_sys = t;
	}
	sys_animate(&H, dt);

	if (t < H.locked_until) {
		in_lockout = true;
		hud_set_msg(&H, MSG_ERR, "Verrouillage de sécurité : %ds", (int)(H.locked_until - t + 0.999));
	} else if (in_lockout) {
		in_lockout = false;
		default_msg();
	}

	if (H.unlocking && t - H.unlock_start >= 0.7) {
		if (H.mode == MODE_LOCK) {
			if (!H.locked) { /* on attend la confirmation du verrouillage */
				render_all();
				return;
			}
			ext_session_lock_v1_unlock_and_destroy(H.lock);
			H.lock = NULL;
			wl_display_roundtrip(H.display);
		}
		H.exit_code = 0;
		H.running = false;
		return;
	}
	render_all();
}

static void usage(const char *argv0)
{
	printf("Usage : %s [--lock|-l] [--greeter|-g] [--dev|-d] [-f]\n"
	       "  --lock, -l     verrouille la session (par défaut)\n"
	       "  -f             rend la main une fois verrouillé (pour swayidle)\n"
	       "  --greeter, -g  écran de connexion pour greetd\n"
	       "  --dev, -d      fenêtre de test (Échap pour quitter)\n", argv0);
}

int main(int argc, char **argv)
{
	H.mode = MODE_LOCK;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--lock") || !strcmp(a, "-l"))
			H.mode = MODE_LOCK;
		else if (!strcmp(a, "--greeter") || !strcmp(a, "-g"))
			H.mode = MODE_GREETER;
		else if (!strcmp(a, "--dev") || !strcmp(a, "-d"))
			H.mode = MODE_DEV;
		else if (!strcmp(a, "-f") || !strcmp(a, "--daemonize"))
			H.daemonize = true;
		else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(argv[0]);
			return 0;
		} else {
			usage(argv[0]);
			return 2;
		}
	}
	signal(SIGPIPE, SIG_IGN);
	H.auth_fd = -1;
	wl_list_init(&H.outputs);

	H.display = wl_display_connect(NULL);
	if (!H.display) {
		fprintf(stderr, "plume-hud : impossible de se connecter à Wayland\n");
		return 1;
	}
	H.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	H.registry = wl_display_get_registry(H.display);
	wl_registry_add_listener(H.registry, &reg_listener, NULL);
	wl_display_roundtrip(H.display);
	wl_display_roundtrip(H.display);

	if (!H.compositor || !H.shm) {
		fprintf(stderr, "plume-hud : compositeur incomplet (wl_compositor / wl_shm)\n");
		return 1;
	}
	if (H.mode == MODE_LOCK && !H.lock_manager) {
		fprintf(stderr, "plume-hud : le compositeur ne gère pas ext-session-lock-v1\n");
		return 1;
	}
	if (H.mode != MODE_LOCK && !H.wm_base) {
		fprintf(stderr, "plume-hud : le compositeur ne gère pas xdg-shell\n");
		return 1;
	}

	/* état initial */
	if (H.mode == MODE_GREETER) {
		first_human_user(H.user, sizeof H.user);
		H.focus = H.user[0] ? 1 : 0;
	} else {
		struct passwd *pw = getpwuid(getuid());
		snprintf(H.user, sizeof H.user, "%s", pw ? pw->pw_name : "?");
		H.focus = 1;
	}
	sys_init(&H);
	H.start = now_s();
	H.locked_at = time(NULL);
	H.last_sys = H.start;
	default_msg();
	hud_log(&H, MSG_NORMAL, "Initialisation de l'interface HUD… OK");
	if (H.mode == MODE_GREETER)
		hud_log(&H, MSG_NORMAL, "Écran de connexion prêt sur %s", H.sys.host);
	else
		hud_log(&H, MSG_NORMAL, "Session de %s verrouillée sur %s", H.user, H.sys.host);

	if (H.mode == MODE_LOCK) {
		H.lock = ext_session_lock_manager_v1_lock(H.lock_manager);
		ext_session_lock_v1_add_listener(H.lock, &lock_listener, NULL);
		struct output *o;
		wl_list_for_each(o, &H.outputs, link)
			create_lock_surface(o);
	} else {
		create_window();
	}

	H.cursor_theme = wl_cursor_theme_load(NULL, 24, H.shm);
	H.cursor_surface = wl_compositor_create_surface(H.compositor);

	int tick_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	struct itimerspec its = { .it_interval = { 0, 1000000000L / HUD_FPS }, .it_value = { 0, 1000000000L / HUD_FPS } };
	timerfd_settime(tick_fd, 0, &its, NULL);
	H.repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

	H.running = true;
	H.exit_code = 0;
	while (H.running) {
		while (wl_display_prepare_read(H.display) != 0)
			wl_display_dispatch_pending(H.display);
		if (wl_display_flush(H.display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(H.display);
			H.exit_code = 1;
			break;
		}
		struct pollfd fds[4] = {
			{ .fd = wl_display_get_fd(H.display), .events = POLLIN },
			{ .fd = tick_fd, .events = POLLIN },
			{ .fd = H.repeat_fd, .events = POLLIN },
			{ .fd = H.auth_fd, .events = POLLIN },
		};
		int n = poll(fds, H.auth_fd >= 0 ? 4 : 3, -1);
		if (n < 0) {
			wl_display_cancel_read(H.display);
			if (errno == EINTR)
				continue;
			H.exit_code = 1;
			break;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(H.display) < 0) {
				H.exit_code = 1;
				break;
			}
		} else {
			wl_display_cancel_read(H.display);
		}
		if (fds[0].revents & (POLLERR | POLLHUP)) {
			H.exit_code = 1;
			break;
		}
		if (wl_display_dispatch_pending(H.display) < 0) {
			H.exit_code = 1;
			break;
		}
		uint64_t exp;
		if (fds[1].revents & POLLIN) {
			if (read(tick_fd, &exp, sizeof exp) > 0)
				on_tick();
		}
		if (fds[2].revents & POLLIN) {
			if (read(H.repeat_fd, &exp, sizeof exp) > 0 && H.repeat_key)
				handle_key(H.repeat_sym, H.repeat_utf8);
		}
		if (H.auth_fd >= 0 && (fds[3].revents & (POLLIN | POLLHUP)))
			auth_finish(&H);
	}
	explicit_bzero(H.pw, sizeof H.pw);
	wl_display_flush(H.display);
	return H.exit_code;
}
