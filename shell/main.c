/* plume-shell — surfaces layer-shell (barre + dock), souris, événements sway, boucle. */
#include "shell.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-cursor.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct shell S;

double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

void mark_dirty(enum surf_kind kind)
{
	struct output *o;
	wl_list_for_each(o, &S.outputs, link) {
		if (kind == SURF_BAR)
			o->bar.dirty = true;
		else
			o->dock.dirty = true;
	}
}

void spawn(const char *cmd)
{
	if (!cmd || !*cmd)
		return;
	pid_t p = fork();
	if (p == 0) {
		setsid();
		/* double fork : l'appli ne reste pas rattachée à plume-shell */
		if (fork() == 0) {
			execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0)
		while (waitpid(p, NULL, 0) < 0 && errno == EINTR)
			;
}

/* ------------------------------ tampons ------------------------------ */

static void buffer_release(void *data, struct wl_buffer *b)
{
	(void)b;
	((struct buffer *)data)->busy = false;
}
static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static void buffer_destroy(struct buffer *b)
{
	if (b->cs) cairo_surface_destroy(b->cs);
	if (b->wl) wl_buffer_destroy(b->wl);
	if (b->data) munmap(b->data, b->size);
	memset(b, 0, sizeof *b);
}

static struct buffer *get_buffer(struct surf *s, int w, int h)
{
	for (int i = 0; i < 2; i++) {
		struct buffer *b = &s->buffers[i];
		if (b->busy)
			continue;
		if (b->wl && (b->w != w || b->h != h))
			buffer_destroy(b);
		if (!b->wl) {
			int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
			size_t size = (size_t)stride * h;
			int fd = memfd_create("plume-shell", MFD_CLOEXEC);
			if (fd < 0 || ftruncate(fd, size) < 0) {
				if (fd >= 0) close(fd);
				return NULL;
			}
			void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if (data == MAP_FAILED) {
				close(fd);
				return NULL;
			}
			struct wl_shm_pool *pool = wl_shm_create_pool(S.shm, fd, size);
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

static void render(struct surf *s)
{
	if (!s->configured || s->width <= 0 || s->height <= 0)
		return;
	int sc = s->out->scale > 0 ? s->out->scale : 1;
	struct buffer *b = get_buffer(s, s->width * sc, s->height * sc);
	if (!b)
		return;
	cairo_t *cr = cairo_create(b->cs);
	cairo_scale(cr, sc, sc);
	if (s->kind == SURF_BAR)
		draw_bar(s->out, cr, s->width, s->height);
	else
		draw_dock(s->out, cr, s->width, s->height);
	cairo_destroy(cr);
	cairo_surface_flush(b->cs);
	wl_surface_set_buffer_scale(s->surface, sc);
	wl_surface_attach(s->surface, b->wl, 0, 0);
	wl_surface_damage_buffer(s->surface, 0, 0, b->w, b->h);
	wl_surface_commit(s->surface);
	b->busy = true;
	s->dirty = false;
}

/* --------------------------- layer surfaces -------------------------- */

static void set_dock_input_region(struct surf *s)
{
	dock_layout(s->out, s->width, s->height, NULL, NULL);
	struct rect p = s->out->dock_panel;
	struct wl_region *r = wl_compositor_create_region(S.compositor);
#ifdef PLUME_VOID
	if (M.open && M.out == s->out) /* menu ouvert : tout le dock agrandi reçoit les clics */
		wl_region_add(r, 0, 0, s->width, s->height);
	else
#endif
	wl_region_add(r, (int)p.x, (int)p.y - 12, (int)p.w, (int)p.h + 12);
	wl_surface_set_input_region(s->surface, r);
	wl_region_destroy(r);
}

#ifdef PLUME_VOID
void dock_relayout(void)
{
	struct output *o;
	wl_list_for_each(o, &S.outputs, link)
		if (o->dock.configured)
			set_dock_input_region(&o->dock);
	if (S.hover_app >= S.napps)
		S.hover_app = -1;
	mark_dirty(SURF_DOCK);
}
#endif

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *l, uint32_t serial, uint32_t w, uint32_t h)
{
	struct surf *s = data;
	s->width = w;
	s->height = h;
	zwlr_layer_surface_v1_ack_configure(l, serial);
	s->configured = true;
	if (s->kind == SURF_DOCK)
		set_dock_input_region(s);
	render(s);
}

static void destroy_surf(struct surf *s)
{
	for (int i = 0; i < 2; i++)
		buffer_destroy(&s->buffers[i]);
	if (s->layer) zwlr_layer_surface_v1_destroy(s->layer);
	if (s->surface) wl_surface_destroy(s->surface);
	s->layer = NULL;
	s->surface = NULL;
	s->configured = false;
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *l)
{
	(void)l;
	destroy_surf(data);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

static void create_surf(struct output *o, struct surf *s, enum surf_kind kind)
{
	s->kind = kind;
	s->out = o;
	s->surface = wl_compositor_create_surface(S.compositor);
	s->layer = zwlr_layer_shell_v1_get_layer_surface(S.layer_shell, s->surface, o->wl_output,
							 ZWLR_LAYER_SHELL_V1_LAYER_TOP,
							 kind == SURF_BAR ? "plume-bar" : "plume-dock");
	zwlr_layer_surface_v1_add_listener(s->layer, &layer_listener, s);
	if (kind == SURF_BAR) {
		zwlr_layer_surface_v1_set_anchor(s->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
							   ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
							   ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		zwlr_layer_surface_v1_set_size(s->layer, 0, BAR_H);
		zwlr_layer_surface_v1_set_exclusive_zone(s->layer, BAR_H);
	} else {
		zwlr_layer_surface_v1_set_anchor(s->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
							   ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
							   ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		zwlr_layer_surface_v1_set_size(s->layer, 0, DOCK_SURF_H);
		zwlr_layer_surface_v1_set_exclusive_zone(s->layer, DOCK_PANEL_H + DOCK_MARGIN);
	}
	wl_surface_commit(s->surface);
}

static void create_output_surfaces(struct output *o)
{
	if (!S.layer_shell || o->bar.surface)
		return;
	create_surf(o, &o->bar, SURF_BAR);
	create_surf(o, &o->dock, SURF_DOCK);
}

/* ------------------------------ écrans ------------------------------- */

static void o_geom(void *d, struct wl_output *w, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sp,
		   const char *mk, const char *md, int32_t tr)
{
	(void)d; (void)w; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)mk; (void)md; (void)tr;
}
static void o_mode(void *d, struct wl_output *w, uint32_t f, int32_t wd, int32_t h, int32_t r)
{
	(void)d; (void)w; (void)f; (void)wd; (void)h; (void)r;
}
static void o_done(void *d, struct wl_output *w) { (void)d; (void)w; }
static void o_scale(void *d, struct wl_output *w, int32_t s)
{
	(void)w;
	struct output *o = d;
	o->scale = s;
	o->bar.dirty = o->dock.dirty = true;
}
static void o_name(void *d, struct wl_output *w, const char *n)
{
	(void)w;
	struct output *o = d;
	snprintf(o->name, sizeof o->name, "%s", n);
}
static void o_desc(void *d, struct wl_output *w, const char *n) { (void)d; (void)w; (void)n; }

static const struct wl_output_listener output_listener = {
	.geometry = o_geom, .mode = o_mode, .done = o_done, .scale = o_scale, .name = o_name, .description = o_desc,
};

/* ------------------------------ souris ------------------------------- */

static struct surf *find_surf(struct wl_surface *sf)
{
	struct output *o;
	wl_list_for_each(o, &S.outputs, link) {
		if (o->bar.surface == sf) return &o->bar;
		if (o->dock.surface == sf) return &o->dock;
	}
	return NULL;
}

static bool in_rect(struct rect r, double x, double y)
{
	return r.w > 0 && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

static void update_hover(void)
{
	int h = -1;
	if (S.ptr_surf && S.ptr_surf->kind == SURF_DOCK) {
		struct output *o = S.ptr_surf->out;
		for (int i = 0; i < S.napps; i++) {
			struct rect r = o->hit_app[i];
			r.y -= 10;
			r.h += 14;
			if (in_rect(r, S.px, S.py))
				h = i;
		}
	}
	if (h != S.hover_app) {
		S.hover_app = h;
		mark_dirty(SURF_DOCK);
	}
}

static struct wl_cursor_theme *cursor_theme;
static struct wl_surface *cursor_surface;

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	(void)d;
	S.ptr_surf = find_surf(sf);
	S.px = wl_fixed_to_double(x);
	S.py = wl_fixed_to_double(y);
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
	update_hover();
}

static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)p; (void)s; (void)sf;
#ifdef PLUME_VOID
	if (M.open && S.ptr_surf && S.ptr_surf == &M.out->dock)
		menu_close(); /* la souris quitte le dock : le menu se referme */
#endif
	S.ptr_surf = NULL;
	update_hover();
}

static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	S.px = wl_fixed_to_double(x);
	S.py = wl_fixed_to_double(y);
#ifdef PLUME_VOID
	if (M.open) {
		if (S.ptr_surf && S.ptr_surf == &M.out->dock)
			menu_motion(S.px, S.py);
		return;
	}
#endif
	update_hover();
}

static const char *power_cmd(void)
{
	for (int i = 0; i < S.napps; i++)
		if (S.apps[i].power)
			return S.apps[i].cmd;
#ifdef PLUME_VOID
	return "plume-alimentation";
#else
	return "swaynag -t warning -m 'Que faire ?' -B 'Éteindre' 'sudo -n /sbin/poweroff' "
	       "-B 'Redémarrer' 'sudo -n /sbin/reboot' -B 'Déconnexion' 'swaymsg exit'";
#endif
}

static void click_bar(struct output *o, uint32_t button)
{
	double x = S.px, y = S.py;
	if (in_rect(o->hit_logo, x, y)) {
		spawn("plume-menu");
		return;
	}
	for (int i = 0; i < S.nws; i++)
		if (in_rect(o->hit_ws[i], x, y)) {
			ipc_command("workspace \"%s\"", S.ws[i].name);
			return;
		}
#ifdef PLUME_VOID
	if (in_rect(o->hit_clock, x, y)) { /* heure : les notifications reçues (comme sur Mac) */
		spawn("plume-notifications");
		return;
	}
#endif
	if (in_rect(o->hit_power, x, y))
		spawn(power_cmd());
	else if (in_rect(o->hit_vol, x, y))
		/* clic : panneau son ; clic du milieu : muet (la molette règle le volume) */
		spawn(button == BTN_MIDDLE ? "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"
					   : "command -v plume-son >/dev/null && exec plume-son || exec wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle");
	else if (in_rect(o->hit_bt, x, y))
		spawn("command -v plume-bluetooth >/dev/null && exec plume-bluetooth || exec plume-terminal -a plume-btctl -T Bluetooth bluetoothctl");
	else if (in_rect(o->hit_net, x, y))
		/* wifi : panneau plume-wifi, sinon iwctl dans le terminal */
		spawn("command -v plume-wifi >/dev/null && exec plume-wifi || exec plume-terminal -a plume-iwctl -T 'Wifi — iwctl' iwctl");
	else if (in_rect(o->hit_maj, x, y))
		spawn("PLUME_MAJ_FENETRE=1 exec plume-terminal -a plume-maj -T 'Mises à jour' plume-maj");
	else if (in_rect(o->hit_kb, x, y))
		ipc_command("input type:keyboard xkb_switch_layout next");
}

static void click_dock(struct output *o, uint32_t button)
{
	for (int i = 0; i < S.napps; i++) {
		struct app *a = &S.apps[i];
		if (!in_rect(o->hit_app[i], S.px, S.py))
			continue;
#ifdef PLUME_VOID
		if (button == BTN_RIGHT && !a->power && a->app_id[0]) { /* clic droit : menu comme sur Mac */
			menu_open(o, i);
			return;
		}
#endif
		if (!a->power && button == BTN_LEFT && app_is_running(a)) {
			/* déjà ouvert : on l'affiche */
			ipc_command("[app_id=\"^%s$\"] focus", a->app_id);
			ipc_command("[class=\"^%s$\"] focus", a->app_id);
		} else {
			spawn(a->cmd); /* clic du milieu = nouvelle fenêtre */
		}
		return;
	}
}

static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)serial; (void)t;
	if (state != WL_POINTER_BUTTON_STATE_PRESSED || !S.ptr_surf)
		return;
#ifdef PLUME_VOID
	if (M.open) { /* menu ouvert : le clic est pour lui (ou le referme) */
		if (S.ptr_surf == &M.out->dock)
			menu_click(S.px, S.py);
		else
			menu_close();
		return;
	}
#endif
	if (S.ptr_surf->kind == SURF_BAR)
		click_bar(S.ptr_surf->out, button);
	else
		click_dock(S.ptr_surf->out, button);
	sys_update();
}

static double axis_acc;
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
	(void)d; (void)p; (void)t;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || !S.ptr_surf || S.ptr_surf->kind != SURF_BAR)
		return;
	axis_acc += wl_fixed_to_double(v);
	if (fabs(axis_acc) < 8)
		return;
	bool up = axis_acc < 0;
	axis_acc = 0;
	struct output *o = S.ptr_surf->out;
	if (in_rect(o->hit_vol, S.px, S.py)) {
		spawn(up ? "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+" : "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-");
		sys_update();
	} else {
		ipc_command(up ? "workspace prev_on_output" : "workspace next_on_output");
	}
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button, .axis = p_axis,
};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	(void)d;
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !S.pointer) {
		S.pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(S.pointer, &pointer_listener, NULL);
	}
#ifdef PLUME_VOID
	/* plus aucune souris (débranchée, Bluetooth déconnecté) : on lâche l'ancienne, sinon la
	 * barre et le dock restent sourds quand une souris revient */
	else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && S.pointer) {
		wl_pointer_release(S.pointer);
		S.pointer = NULL;
		S.ptr_surf = NULL;
	}
#endif
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_listener = { .capabilities = seat_caps, .name = seat_name };

/* ------------------------------ registre ----------------------------- */

static uint32_t minv(uint32_t a, uint32_t b) { return a < b ? a : b; }

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	(void)d;
	if (!strcmp(iface, wl_compositor_interface.name))
		S.compositor = wl_registry_bind(r, name, &wl_compositor_interface, minv(ver, 4));
	else if (!strcmp(iface, wl_shm_interface.name))
		S.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, wl_seat_interface.name) && !S.seat) {
		S.seat = wl_registry_bind(r, name, &wl_seat_interface, minv(ver, 4));
		wl_seat_add_listener(S.seat, &seat_listener, NULL);
	} else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
		S.layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, minv(ver, 4));
	else if (!strcmp(iface, wl_output_interface.name)) {
		struct output *o = calloc(1, sizeof *o);
		o->scale = 1;
		o->global_name = name;
		o->wl_output = wl_registry_bind(r, name, &wl_output_interface, minv(ver, 4));
		wl_output_add_listener(o->wl_output, &output_listener, o);
		wl_list_insert(&S.outputs, &o->link);
		if (S.layer_shell)
			create_output_surfaces(o); /* écran branché plus tard */
	}
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t name)
{
	(void)d; (void)r;
	struct output *o, *tmp;
	wl_list_for_each_safe(o, tmp, &S.outputs, link) {
		if (o->global_name != name)
			continue;
		if (S.ptr_surf == &o->bar || S.ptr_surf == &o->dock)
			S.ptr_surf = NULL;
		destroy_surf(&o->bar);
		destroy_surf(&o->dock);
		wl_output_destroy(o->wl_output);
		wl_list_remove(&o->link);
		free(o);
	}
}

static const struct wl_registry_listener reg_listener = { .global = reg_global, .global_remove = reg_remove };

/* ------------------------------ boucle ------------------------------- */

static bool animate(double dt)
{
	bool moving = false;
	double k = dt / 0.12;
	if (k > 1) k = 1;
	for (int i = 0; i < S.napps; i++) {
		double target = i == S.hover_app ? 1 : 0;
		double v = S.apps[i].hover + (target - S.apps[i].hover) * k;
		if (fabs(v - target) < 0.01)
			v = target;
		if (v != S.apps[i].hover) {
			S.apps[i].hover = v;
			moving = true;
		}
	}
	if (moving)
		mark_dirty(SURF_DOCK);
	return moving;
}

static void handle_ipc_event(void)
{
	uint32_t type;
	char *ev = ipc_recv(S.ipc_ev, &type);
	if (!ev) {
		close(S.ipc_ev);
		S.ipc_ev = -1;
		return;
	}
	free(ev);
	type &= 0x7fffffff;
	if (type == 0) {               /* workspace */
		ipc_refresh_workspaces();
		ipc_refresh_tree();
	} else if (type == 3) {        /* window */
		ipc_refresh_tree();
	} else if (type == 15) {       /* input */
		ipc_refresh_inputs();
	}
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv; /* sway passe « -b <bar_id> » : ignoré */
	signal(SIGPIPE, SIG_IGN);
	wl_list_init(&S.outputs);
	S.hover_app = -1;
	S.vol = -1;

	S.display = wl_display_connect(NULL);
	if (!S.display) {
		fprintf(stderr, "plume-shell : pas de connexion Wayland\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(S.display);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(S.display);
	if (!S.compositor || !S.shm || !S.layer_shell) {
		fprintf(stderr, "plume-shell : le compositeur ne gère pas wlr-layer-shell\n");
		return 1;
	}
	wl_display_roundtrip(S.display); /* noms et échelles des écrans */

	cursor_theme = wl_cursor_theme_load(NULL, 24, S.shm);
	cursor_surface = wl_compositor_create_surface(S.compositor);

	S.ipc_cmd = ipc_connect();
	S.ipc_ev = ipc_connect();
	if (S.ipc_ev >= 0) {
		ipc_send(S.ipc_ev, 2, "[\"workspace\",\"window\",\"input\"]"); /* SUBSCRIBE */
		free(ipc_recv(S.ipc_ev, NULL));
	}
	apps_load();
	if (getenv("PLUME_SHELL_HOVER")) { /* aperçu/tests : survol simulé */
		S.hover_app = atoi(getenv("PLUME_SHELL_HOVER"));
		if (S.hover_app >= 0 && S.hover_app < S.napps)
			S.apps[S.hover_app].hover = 1;
	}
	sys_update();
	ipc_refresh_workspaces();
	ipc_refresh_tree();

	struct output *o;
	wl_list_for_each(o, &S.outputs, link)
		create_output_surfaces(o);

	int tick = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	struct itimerspec its = { { 0, 33000000 }, { 0, 33000000 } };
	timerfd_settime(tick, 0, &its, NULL);

	double last = now_s(), last_sys = last;
	int last_min = -1;
	S.running_loop = true;
	while (S.running_loop) {
		while (wl_display_prepare_read(S.display) != 0)
			wl_display_dispatch_pending(S.display);
		wl_display_flush(S.display);
		struct pollfd fds[3] = {
			{ .fd = wl_display_get_fd(S.display), .events = POLLIN },
			{ .fd = tick, .events = POLLIN },
			{ .fd = S.ipc_ev, .events = POLLIN },
		};
		if (poll(fds, S.ipc_ev >= 0 ? 3 : 2, -1) < 0) {
			wl_display_cancel_read(S.display);
			if (errno == EINTR)
				continue;
			break;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(S.display) < 0)
				break;
		} else {
			wl_display_cancel_read(S.display);
		}
		if (fds[0].revents & (POLLERR | POLLHUP))
			break;
		if (wl_display_dispatch_pending(S.display) < 0)
			break;
		if (S.ipc_ev >= 0 && (fds[2].revents & (POLLIN | POLLHUP)))
			handle_ipc_event();
		if (fds[1].revents & POLLIN) {
			uint64_t exp;
			if (read(tick, &exp, sizeof exp) < 0)
				continue;
			double t = now_s();
			animate(t - last);
			last = t;
			if (t - last_sys >= 2.0) {
				sys_update();
				last_sys = t;
			}
			time_t wall = time(NULL);
			struct tm tm;
			localtime_r(&wall, &tm);
			if (tm.tm_min != last_min) {
				last_min = tm.tm_min;
				mark_dirty(SURF_BAR);
			}
			wl_list_for_each(o, &S.outputs, link) {
				if (o->bar.dirty) render(&o->bar);
				if (o->dock.dirty) render(&o->dock);
			}
		}
	}
	return 0;
}
