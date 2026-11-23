/* plume-lanceur — couche plein écran (wlr-layer-shell), clavier, souris, boucle. */
#include "lanceur.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-compose.h>
#ifdef PLUME_VOID
#include <wayland-cursor.h>
/* flèche de la souris (Plume Void) : sans image, le compositeur cacherait le curseur */
static struct wl_cursor_theme *cursor_theme;
static struct wl_surface *cursor_surface;
#endif

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct lanceur L;

static struct xkb_compose_state *compose_state;
static char pid_path[PATH_MAX];
static volatile sig_atomic_t stop_requested;

double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void on_term(int s)
{
	(void)s;
	stop_requested = 1;
}

/* une seule grille à la fois : relancer la commande ferme celle qui est ouverte */
static bool toggle_existing(void)
{
	const char *rt = getenv("XDG_RUNTIME_DIR");
	snprintf(pid_path, sizeof pid_path, "%s/plume-lanceur.pid", rt && *rt ? rt : "/tmp");
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
			if (!strcmp(comm, "plume-lanceur") && kill(pid, SIGTERM) == 0) {
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
		struct buffer *b = &L.buffers[i];
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
			int fd = memfd_create("plume-lanceur", MFD_CLOEXEC);
			if (fd < 0 || ftruncate(fd, size) < 0) {
				if (fd >= 0) close(fd);
				return NULL;
			}
			void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if (data == MAP_FAILED) {
				close(fd);
				return NULL;
			}
			struct wl_shm_pool *pool = wl_shm_create_pool(L.shm, fd, size);
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
	L.frame_cb = NULL;
}
static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void render(void)
{
	if (!L.configured || L.width <= 0 || L.height <= 0)
		return;
	struct buffer *b = get_buffer(L.width, L.height);
	if (!b)
		return;
	cairo_t *cr = cairo_create(b->cs);
	draw(cr, L.width, L.height);
	cairo_destroy(cr);
	cairo_surface_flush(b->cs);
	wl_surface_attach(L.surface, b->wl, 0, 0);
	wl_surface_damage_buffer(L.surface, 0, 0, b->w, b->h);
	L.frame_cb = wl_surface_frame(L.surface);
	wl_callback_add_listener(L.frame_cb, &frame_listener, NULL);
	wl_surface_commit(L.surface);
	b->busy = true;
	L.dirty = false;
}

/* ----------------------------- couche -------------------------------- */

static void layer_configure(void *d, struct zwlr_layer_surface_v1 *l, uint32_t serial, uint32_t w, uint32_t h)
{
	(void)d;
	L.width = w;
	L.height = h;
	zwlr_layer_surface_v1_ack_configure(l, serial);
	L.configured = true;
	L.dirty = true;
}
static void layer_closed(void *d, struct zwlr_layer_surface_v1 *l) { (void)d; (void)l; L.running = false; }
static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure, .closed = layer_closed,
};

/* ----------------------------- actions ------------------------------- */

static void launch_sel(void)
{
	if (L.sel < 0 || L.sel >= L.nview)
		return;
	app_launch(&L.apps[L.view[L.sel]]);
	L.running = false;
}

static void move_sel(int d)
{
	if (!L.nview)
		return;
	L.sel += d;
	if (L.sel < 0) L.sel = 0;
	if (L.sel >= L.nview) L.sel = L.nview - 1;
	if (L.per_page > 0)
		L.page = L.sel / L.per_page;
	L.dirty = true;
}

static void set_page(int p)
{
	if (p < 0 || p >= L.npages || p == L.page)
		return;
	L.page = p;
	L.sel = p * L.per_page;
	L.dirty = true;
}

static void utf8_pop(char *s)
{
	size_t n = strlen(s);
	while (n > 0 && ((unsigned char)s[--n] & 0xC0) == 0x80)
		;
	s[n] = '\0';
}

/* ----------------------------- clavier ------------------------------- */

static void disarm_repeat(void)
{
	struct itimerspec its = { 0 };
	timerfd_settime(L.repeat_fd, 0, &its, NULL);
	L.repeat_key = 0;
}

static void key_press(uint32_t key)
{
	xkb_keysym_t sym = xkb_state_key_get_one_sym(L.xkb_state, key + 8);
	char text[16] = "";
	if (compose_state && xkb_compose_state_feed(compose_state, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
		switch (xkb_compose_state_get_status(compose_state)) {
		case XKB_COMPOSE_COMPOSING:
			return;
		case XKB_COMPOSE_CANCELLED:
			xkb_compose_state_reset(compose_state);
			return;
		case XKB_COMPOSE_COMPOSED:
			xkb_compose_state_get_utf8(compose_state, text, sizeof text);
			sym = xkb_compose_state_get_one_sym(compose_state);
			xkb_compose_state_reset(compose_state);
			break;
		default:
			break;
		}
	}
	if (!text[0])
		xkb_state_key_get_utf8(L.xkb_state, key + 8, text, sizeof text);
	bool ctrl = xkb_state_mod_name_is_active(L.xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
	L.dirty = true;
	switch (sym) {
	case XKB_KEY_Escape:
		if (L.query[0]) {
			L.query[0] = '\0';
			apps_filter();
		} else {
			L.running = false;
		}
		return;
	case XKB_KEY_Return: case XKB_KEY_KP_Enter: launch_sel(); return;
	case XKB_KEY_Left: move_sel(-1); return;
	case XKB_KEY_Right: move_sel(1); return;
	case XKB_KEY_Up: move_sel(-L.cols); return;
	case XKB_KEY_Down: move_sel(L.cols); return;
	case XKB_KEY_Tab: move_sel(1); return;
	case XKB_KEY_ISO_Left_Tab: move_sel(-1); return;
	case XKB_KEY_Prior: set_page(L.page - 1); return;
	case XKB_KEY_Next: set_page(L.page + 1); return;
	case XKB_KEY_Home: L.sel = 0; L.page = 0; return;
	case XKB_KEY_End: move_sel(L.nview); return;
	case XKB_KEY_BackSpace:
		if (L.query[0]) {
			utf8_pop(L.query);
			apps_filter();
		}
		return;
	default:
		break;
	}
	if (ctrl && (sym == XKB_KEY_u || sym == XKB_KEY_U)) {
		L.query[0] = '\0';
		apps_filter();
		return;
	}
	if (!ctrl && (unsigned char)text[0] >= 0x20 && text[0] != 0x7f &&
	    strlen(L.query) + strlen(text) < sizeof L.query - 1) {
		strcat(L.query, text);
		apps_filter();
	}
}

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
	struct xkb_keymap *km = xkb_keymap_new_from_string(L.xkb_ctx, map, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(map, size);
	if (!km)
		return;
	if (L.xkb_state) xkb_state_unref(L.xkb_state);
	if (L.keymap) xkb_keymap_unref(L.keymap);
	L.keymap = km;
	L.xkb_state = xkb_state_new(km);
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
static void kb_key(void *dd, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	(void)dd; (void)k; (void)serial; (void)time;
	if (!L.xkb_state)
		return;
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		key_press(key);
		if (L.repeat_rate > 0 && xkb_keymap_key_repeats(L.keymap, key + 8)) {
			L.repeat_key = key;
			struct itimerspec its = { 0 };
			its.it_value.tv_sec = L.repeat_delay / 1000;
			its.it_value.tv_nsec = (L.repeat_delay % 1000) * 1000000L;
			long iv = 1000000000L / L.repeat_rate;
			its.it_interval.tv_sec = iv / 1000000000L;
			its.it_interval.tv_nsec = iv % 1000000000L;
			timerfd_settime(L.repeat_fd, 0, &its, NULL);
		}
	} else if (key == L.repeat_key) {
		disarm_repeat();
	}
}
static void kb_mods(void *dd, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lck,
		    uint32_t group)
{
	(void)dd; (void)k; (void)serial;
	if (L.xkb_state)
		xkb_state_update_mask(L.xkb_state, dep, lat, lck, 0, 0, group);
}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
	(void)d; (void)k;
	L.repeat_rate = rate;
	L.repeat_delay = delay;
}
static const struct wl_keyboard_listener kb_listener = {
	.keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave, .key = kb_key,
	.modifiers = kb_mods, .repeat_info = kb_repeat,
};

/* ------------------------------ souris ------------------------------- */

static bool in(struct rect r, double x, double y)
{
	return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)sf;
#ifdef PLUME_VOID
	struct wl_cursor *c = cursor_theme ? wl_cursor_theme_get_cursor(cursor_theme, "left_ptr") : NULL;
	if (c && c->image_count) {
		struct wl_cursor_image *img = c->images[0];
		wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
		wl_surface_damage(cursor_surface, 0, 0, img->width, img->height);
		wl_surface_commit(cursor_surface);
		wl_pointer_set_cursor(p, serial, cursor_surface, img->hotspot_x, img->hotspot_y);
	}
#else
	wl_pointer_set_cursor(p, serial, NULL, 0, 0); /* le compositeur garde sa flèche */
#endif
	L.px = wl_fixed_to_double(x);
	L.py = wl_fixed_to_double(y);
	L.dirty = true;
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)p; (void)s; (void)sf;
	L.px = L.py = -1;
	L.dirty = true;
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	L.px = wl_fixed_to_double(x);
	L.py = wl_fixed_to_double(y);
	L.dirty = true;
}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)serial; (void)t;
	if (state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;
	if (button == BTN_RIGHT) {
		L.running = false;
		return;
	}
	if (button != BTN_LEFT)
		return;
	for (int i = 0; i < L.ncells; i++)
		if (in(L.r_cells[i], L.px, L.py)) {
			L.sel = L.cell_idx[i];
			launch_sel();
			return;
		}
	for (int i = 0; i < L.ndots; i++)
		if (in(L.r_dots[i], L.px, L.py)) {
			set_page(i);
			return;
		}
	/* la barre de recherche ne ferme pas ; le reste du fond, si */
	double sw = L.width - 80 < 560 ? L.width - 80 : 560;
	struct rect search = { (L.width - sw) / 2, 52, sw, 46 };
	if (!in(search, L.px, L.py))
		L.running = false;
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
	(void)d; (void)p; (void)t;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
		return;
	L.axis_acc += wl_fixed_to_double(v);
	if (L.axis_acc >= 10) {
		set_page(L.page + 1);
		L.axis_acc = 0;
	} else if (L.axis_acc <= -10) {
		set_page(L.page - 1);
		L.axis_acc = 0;
	}
}
static const struct wl_pointer_listener ptr_listener = {
	.enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button, .axis = p_axis,
};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	(void)d;
	bool kb = caps & WL_SEAT_CAPABILITY_KEYBOARD, ptr = caps & WL_SEAT_CAPABILITY_POINTER;
	if (kb && !L.keyboard) {
		L.keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(L.keyboard, &kb_listener, NULL);
	} else if (!kb && L.keyboard) {
		disarm_repeat();
		wl_keyboard_release(L.keyboard);
		L.keyboard = NULL;
	}
	if (ptr && !L.pointer) {
		L.pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(L.pointer, &ptr_listener, NULL);
	} else if (!ptr && L.pointer) {
		wl_pointer_release(L.pointer);
		L.pointer = NULL;
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_listener = { .capabilities = seat_caps, .name = seat_name };

static uint32_t minv(uint32_t a, uint32_t b) { return a < b ? a : b; }

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	(void)d;
	if (!strcmp(iface, wl_compositor_interface.name))
		L.compositor = wl_registry_bind(r, name, &wl_compositor_interface, minv(ver, 4));
	else if (!strcmp(iface, wl_shm_interface.name))
		L.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, wl_seat_interface.name) && !L.seat) {
		L.seat = wl_registry_bind(r, name, &wl_seat_interface, minv(ver, 4));
		wl_seat_add_listener(L.seat, &seat_listener, NULL);
	} else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
		L.layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, minv(ver, 4));
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = { .global = reg_global, .global_remove = reg_remove };

/* ------------------------------ main --------------------------------- */

int main(int argc, char **argv)
{
	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		printf("Usage : %s\n  Toutes les applications installées, en grille (à la GNOME).\n"
		       "  Relancer la commande ferme la grille.\n", argv[0]);
		return 0;
	}
	setlocale(LC_ALL, "");
	if (toggle_existing())
		return 0;
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_DFL);

	L.display = wl_display_connect(NULL);
	if (!L.display) {
		fprintf(stderr, "plume-lanceur : pas de connexion Wayland\n");
		unlink(pid_path);
		return 1;
	}
	L.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	const char *loc = getenv("LC_ALL");
	if (!loc || !*loc) loc = getenv("LANG");
	struct xkb_compose_table *ct = xkb_compose_table_new_from_locale(L.xkb_ctx, loc && *loc ? loc : "C", 0);
	if (!ct)
		ct = xkb_compose_table_new_from_locale(L.xkb_ctx, "en_US.UTF-8", 0);
	if (ct)
		compose_state = xkb_compose_state_new(ct, 0);

	struct wl_registry *reg = wl_display_get_registry(L.display);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(L.display);
	if (!L.compositor || !L.shm || !L.layer_shell) {
		fprintf(stderr, "plume-lanceur : compositeur sans wlr-layer-shell (sway requis)\n");
		unlink(pid_path);
		return 1;
	}

	apps_load();

#ifdef PLUME_VOID
	const char *xs = getenv("XCURSOR_SIZE");
	cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), xs && atoi(xs) > 0 ? atoi(xs) : 24, L.shm);
	cursor_surface = wl_compositor_create_surface(L.compositor);
#endif
	L.surface = wl_compositor_create_surface(L.compositor);
	L.layer = zwlr_layer_shell_v1_get_layer_surface(L.layer_shell, L.surface, NULL,
							ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "plume-lanceur");
	zwlr_layer_surface_v1_add_listener(L.layer, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(L.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
							  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(L.layer, -1); /* recouvre aussi la barre et le dock */
	zwlr_layer_surface_v1_set_keyboard_interactivity(L.layer, 1); /* exclusif : on tape directement */
	wl_surface_commit(L.surface);
	L.repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

	L.start = now_s();
	L.running = true;
	while (L.running && !stop_requested) {
		bool anim = now_s() - L.start < 0.2;
		if ((L.dirty || anim) && !L.frame_cb && L.configured)
			render();
		while (wl_display_prepare_read(L.display) != 0)
			wl_display_dispatch_pending(L.display);
		wl_display_flush(L.display);
		struct pollfd fds[2] = {
			{ .fd = wl_display_get_fd(L.display), .events = POLLIN },
			{ .fd = L.repeat_fd, .events = POLLIN },
		};
		if (poll(fds, 2, anim || (L.dirty && !L.frame_cb) ? 16 : -1) < 0) {
			wl_display_cancel_read(L.display);
			if (errno == EINTR)
				continue;
			break;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(L.display) < 0)
				break;
		} else {
			wl_display_cancel_read(L.display);
		}
		if (fds[0].revents & (POLLERR | POLLHUP))
			break;
		if (wl_display_dispatch_pending(L.display) < 0)
			break;
		uint64_t exp;
		if ((fds[1].revents & POLLIN) && read(L.repeat_fd, &exp, sizeof exp) > 0 && L.repeat_key && L.xkb_state)
			key_press(L.repeat_key);
	}
	/* on retire la couche tout de suite (l'appli lancée prend le relais) */
	if (L.layer)
		zwlr_layer_surface_v1_destroy(L.layer);
	if (L.surface)
		wl_surface_destroy(L.surface);
	wl_display_flush(L.display);
	unlink(pid_path);
	return 0;
}
