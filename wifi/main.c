/* plume-wifi — fenêtre Wayland, clavier, souris, boucle (iwctl en arrière-plan). */
#include "wifi.h"

#include <errno.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-cursor.h>

#include "xdg-shell-client-protocol.h"
#include "fenetre.h"

struct app A;
static struct wl_cursor_theme *cursor_theme;
static struct wl_surface *cursor_surface;

double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

void set_toast(bool err, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(A.toast, sizeof A.toast, fmt, ap);
	va_end(ap);
	A.toast_err = err;
	A.toast_t = now_s();
	A.dirty = true;
}

/* ----------------------------- tampons ------------------------------- */

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
			int fd = memfd_create("plume-wifi", MFD_CLOEXEC);
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
	ui_draw(cr, A.width, A.height);
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
		A.width = 560;
		A.height = 680;
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
	struct xkb_keymap *km = xkb_keymap_new_from_string(A.xkb_ctx, map, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
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
	xkb_keysym_t sym = xkb_state_key_get_one_sym(A.xkb_state, key + 8);
	bool ctrl = xkb_state_mod_name_is_active(A.xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
	char utf8[16] = "";
	xkb_state_key_get_utf8(A.xkb_state, key + 8, utf8, sizeof utf8);
	ui_key(sym, utf8, ctrl);
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
	.keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave, .key = kb_key,
	.modifiers = kb_mods, .repeat_info = kb_repeat,
};

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)sf;
	A.px = wl_fixed_to_double(x);
	A.py = wl_fixed_to_double(y);
	if (cursor_theme) {
		struct wl_cursor *c = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
		if (c && c->image_count) {
			struct wl_cursor_image *img = c->images[0];
			wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
			wl_surface_damage(cursor_surface, 0, 0, img->width, img->height);
			wl_surface_commit(cursor_surface);
			wl_pointer_set_cursor(p, serial, cursor_surface, img->hotspot_x, img->hotspot_y);
		}
	}
	A.dirty = true;
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)p; (void)s; (void)sf;
	A.px = A.py = -1;
	A.dirty = true;
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	A.px = wl_fixed_to_double(x);
	A.py = wl_fixed_to_double(y);
	if (pf_mouvement(A.px, A.py)) /* en-tête tenu et souris bougée : la fenêtre suit */
		xdg_toplevel_move(A.toplevel, A.seat, pf_serie);
	A.dirty = true;
}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)serial; (void)t;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED && button == BTN_LEFT) {
		pf_serie = serial;
		ui_click(A.px, A.py);
	} else if (button == BTN_LEFT) {
		pf_relache();
	}
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
	(void)d; (void)p; (void)t;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		ui_scroll(wl_fixed_to_double(v) * 4);
}
static const struct wl_pointer_listener ptr_listener = {
	.enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button, .axis = p_axis,
};

/* clavier ou souris débranchés puis rebranchés : on reprend un objet neuf */
static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	(void)d;
	bool kb = caps & WL_SEAT_CAPABILITY_KEYBOARD, ptr = caps & WL_SEAT_CAPABILITY_POINTER;
	if (kb && !A.keyboard) {
		A.keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(A.keyboard, &kb_listener, NULL);
	} else if (!kb && A.keyboard) {
		wl_keyboard_release(A.keyboard);
		A.keyboard = NULL;
	}
	if (ptr && !A.pointer) {
		A.pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(A.pointer, &ptr_listener, NULL);
	} else if (!ptr && A.pointer) {
		wl_pointer_release(A.pointer);
		A.pointer = NULL;
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

/* ------------------------------ main --------------------------------- */

int main(int argc, char **argv)
{
	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		printf("Usage : %s    panneau wifi (iwd)\n", argv[0]);
		return 0;
	}
	setlocale(LC_ALL, "");
	signal(SIGPIPE, SIG_IGN);

	A.display = wl_display_connect(NULL);
	if (!A.display) {
		fprintf(stderr, "plume-wifi : pas de connexion Wayland (lancez-le depuis le bureau)\n");
		return 1;
	}
	if (pf_deja_ouvert("plume-wifi"))
		return 0;
	A.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct wl_registry *reg = wl_display_get_registry(A.display);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(A.display);
	if (!A.compositor || !A.shm || !A.wm_base) {
		fprintf(stderr, "plume-wifi : compositeur incomplet\n");
		return 1;
	}
	A.sel = -1;
	iwd_refresh(true);

	A.surface = wl_compositor_create_surface(A.compositor);
	A.xdg_surface = xdg_wm_base_get_xdg_surface(A.wm_base, A.surface);
	xdg_surface_add_listener(A.xdg_surface, &xs_listener, NULL);
	A.toplevel = xdg_surface_get_toplevel(A.xdg_surface);
	xdg_toplevel_add_listener(A.toplevel, &tl_listener, NULL);
	xdg_toplevel_set_app_id(A.toplevel, "plume-wifi");
	xdg_toplevel_set_title(A.toplevel, "Wifi — Plume");
	wl_surface_commit(A.surface);
	cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), 24, A.shm);
	cursor_surface = wl_compositor_create_surface(A.compositor);

	A.running = true;
	while (A.running) {
		iwd_tick();
		if (A.dirty && !A.frame_cb && A.configured)
			render();
		while (wl_display_prepare_read(A.display) != 0)
			wl_display_dispatch_pending(A.display);
		wl_display_flush(A.display);
		struct pollfd fds[2] = { { .fd = wl_display_get_fd(A.display), .events = POLLIN },
					 { .fd = iwd_fd(), .events = POLLIN } };
		bool anim = A.busy || A.scanning || (A.toast[0] && now_s() - A.toast_t < 5.5);
		double wait = iwd_next_timeout();
		if (anim && wait > 0.25)
			wait = 0.25;
		int timeout = A.dirty && !A.frame_cb ? 0 : wait < 0 ? -1 : (int)(wait * 1000);
		if (poll(fds, fds[1].fd >= 0 ? 2 : 1, timeout) < 0) {
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
		if (fds[1].fd >= 0 && fds[1].revents)
			iwd_read();
		if (anim)
			A.dirty = true;
	}
	return 0;
}
