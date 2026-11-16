/* plume-term — fenêtre Wayland, clavier, souris, presse-papiers, boucle. */
#include "term.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon-compose.h>

#include "xdg-shell-client-protocol.h"

#define VERSION "0.1"
#define DEFAULT_FONT_PX 14.0

struct app A;

static struct xkb_compose_table *compose_table;
static struct xkb_compose_state *compose_state;
static char *sel_text;          /* dernière sélection (collée au clic molette) */
static struct vpos sel_origin;
static const char *app_id = "plume-term";
static char base_title[128] = "Plume — terminal";

double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

void request_redraw(void)
{
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
			int fd = memfd_create("plume-term", MFD_CLOEXEC);
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

static void update_title(void)
{
	struct tab *t = tab_active();
	char title[256];
	if (t && t->label[0])
		snprintf(title, sizeof title, "%s — %s", t->label, base_title);
	else
		snprintf(title, sizeof title, "%s", base_title);
	static char last[256];
	if (strcmp(last, title)) {
		snprintf(last, sizeof last, "%s", title);
		xdg_toplevel_set_title(A.toplevel, title);
	}
}

static void render(void)
{
	if (!A.configured || A.width <= 0 || A.height <= 0)
		return;
	struct buffer *b = get_buffer(A.width, A.height);
	if (!b)
		return;
	cairo_t *cr = cairo_create(b->cs);
	render_frame(cr, A.width, A.height);
	cairo_destroy(cr);
	cairo_surface_flush(b->cs);
	wl_surface_attach(A.surface, b->wl, 0, 0);
	wl_surface_damage_buffer(A.surface, 0, 0, b->w, b->h);
	A.frame_cb = wl_surface_frame(A.surface);
	wl_callback_add_listener(A.frame_cb, &frame_listener, NULL);
	wl_surface_commit(A.surface);
	b->busy = true;
	A.dirty = false;
	for (int i = 0; i < MAX_TABS; i++)
		A.tabs[i].dirty = false;
	update_title();
}

/* ---------------------------- taille --------------------------------- */

static void apply_size(void)
{
	int rows, cols;
	layout_grid(A.width, A.height, &rows, &cols);
	bool changed = false;
	for (int i = 0; i < MAX_TABS; i++) {
		struct tab *t = &A.tabs[i];
		if (t->used && (t->rows != rows || t->cols != cols)) {
			tab_resize(t, rows, cols);
			changed = true;
		}
	}
	if (changed && now_s() - A.start > 0.5)
		A.resized_t = now_s();
	A.sel.active = false;
	A.dirty = true;
}

static void set_font(double px)
{
	font_setup(px);
	apply_size();
	A.resized_t = now_s();
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
		A.height = 600;
	}
	A.configured = true;
	apply_size();
}
static const struct xdg_surface_listener xs_listener = { .configure = xs_configure };

static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	(void)d; (void)t; (void)st;
	if (w > 0 && h > 0) {
		A.width = w;
		A.height = h;
	}
}
static void tl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; A.running = false; }
static const struct xdg_toplevel_listener tl_listener = { .configure = tl_configure, .close = tl_close };

/* ------------------------------ onglets ------------------------------ */

bool open_tab_cmd(char **cmd, const char *in_dir)
{
	for (int i = 0; i < MAX_TABS; i++) {
		if (A.tabs[i].used)
			continue;
		int rows = 24, cols = 80;
		if (A.configured)
			layout_grid(A.width, A.height, &rows, &cols);
		/* le nouvel onglet démarre dans le dossier de l'onglet actif */
		char dir[4096] = "";
		struct tab *cur = tab_active();
		if (in_dir)
			snprintf(dir, sizeof dir, "%s", in_dir);
		else if (cur) {
			pid_t pg = tcgetpgrp(cur->fd);
			char p[64];
			snprintf(p, sizeof p, "/proc/%d/cwd", (int)(pg > 0 ? pg : cur->pid));
			ssize_t n = readlink(p, dir, sizeof dir - 1);
			dir[n > 0 ? n : 0] = '\0';
		}
		if (!tab_open(i, rows, cols, cmd, dir[0] ? dir : A.workdir))
			return false;
		A.active = i;
		A.sel.active = false;
		A.dirty = true;
		return true;
	}
	return false;
}

static bool new_tab(void)
{
	return open_tab_cmd(tab_count() == 0 ? A.cmd : NULL, NULL);
}

/* lance un programme détaché (il survit à la fermeture du terminal) */
void spawn_argv(char *const argv[])
{
	pid_t p = fork();
	if (p == 0) {
		setsid();
		for (int s = 1; s < NSIG; s++)
			signal(s, SIG_DFL);
		if (fork() == 0) {
			execvp(argv[0], argv);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0)
		while (waitpid(p, NULL, 0) < 0 && errno == EINTR)
			;
}

/* clavier à l'écran -> terminal */
void send_to_terminal(uint32_t c, VTermModifier mod)
{
	struct tab *t = tab_active();
	if (!t)
		return;
	vterm_keyboard_unichar(t->vt, c, mod);
	t->view = 0;
	A.sel.active = false;
	A.dirty = true;
}

void send_key_to_terminal(VTermKey k, VTermModifier mod)
{
	struct tab *t = tab_active();
	if (!t)
		return;
	vterm_keyboard_key(t->vt, k, mod);
	t->view = 0;
	A.sel.active = false;
	A.dirty = true;
}

static void set_hud(bool on)
{
	A.hud = on;
	if (on) {
		hud_init();
		hud_tick();
		xdg_toplevel_set_fullscreen(A.toplevel, NULL);
	} else {
		hud_focus_files(false);
		xdg_toplevel_unset_fullscreen(A.toplevel);
	}
	H.cache_dirty = true;
	if (A.configured)
		apply_size();
}

static void switch_tab(int i)
{
	if (i < 0 || i >= MAX_TABS || !A.tabs[i].used || i == A.active)
		return;
	A.active = i;
	A.sel.active = false;
	A.dirty = true;
}

static void cycle_tab(int dir)
{
	for (int k = 1; k <= MAX_TABS; k++) {
		int i = ((A.active + dir * k) % MAX_TABS + MAX_TABS) % MAX_TABS;
		if (A.tabs[i].used) {
			switch_tab(i);
			return;
		}
	}
}

/* après la fermeture d'un onglet : on passe au voisin, ou on quitte */
static void after_close(void)
{
	if (tab_count() == 0) {
		A.running = false;
		return;
	}
	if (!A.tabs[A.active].used) {
		for (int k = 1; k <= MAX_TABS; k++) {
			int i = ((A.active - k) % MAX_TABS + MAX_TABS) % MAX_TABS;
			if (A.tabs[i].used) {
				A.active = i;
				break;
			}
		}
	}
	A.dirty = true;
}

static void close_tab(int i)
{
	tab_close(&A.tabs[i]);
	after_close();
}

/* -------------------------- presse-papiers --------------------------- */

static const char *TEXT_MIMES[] = { "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT", "STRING" };
static char offer_mime[64];

static void src_target(void *d, struct wl_data_source *s, const char *m) { (void)d; (void)s; (void)m; }
static void src_send(void *d, struct wl_data_source *s, const char *mime, int32_t fd)
{
	(void)d; (void)s; (void)mime;
	const char *p = A.clip ? A.clip : "";
	size_t n = strlen(p);
	while (n > 0) {
		ssize_t w = write(fd, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			break;
		p += w;
		n -= w;
	}
	close(fd);
}
static void src_cancelled(void *d, struct wl_data_source *s)
{
	(void)d;
	if (s == A.source)
		A.source = NULL;
	wl_data_source_destroy(s);
}
static void src_dnd_drop(void *d, struct wl_data_source *s) { (void)d; (void)s; }
static void src_dnd_finished(void *d, struct wl_data_source *s) { (void)d; (void)s; }
static void src_action(void *d, struct wl_data_source *s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
	.target = src_target, .send = src_send, .cancelled = src_cancelled,
	.dnd_drop_performed = src_dnd_drop, .dnd_finished = src_dnd_finished, .action = src_action,
};

void clipboard_copy(const char *text)
{
	if (!text || !*text || !A.ddm || !A.data_device)
		return;
	free(A.clip);
	A.clip = strdup(text);
	struct wl_data_source *s = wl_data_device_manager_create_data_source(A.ddm);
	wl_data_source_add_listener(s, &src_listener, NULL);
	for (size_t i = 0; i < sizeof TEXT_MIMES / sizeof TEXT_MIMES[0]; i++)
		wl_data_source_offer(s, TEXT_MIMES[i]);
	wl_data_device_set_selection(A.data_device, s, A.last_serial);
	A.source = s;
}

static void paste_text(const char *s, size_t n)
{
	struct tab *t = tab_active();
	if (!t || !n)
		return;
	/* comme xterm : les fins de ligne deviennent des Entrée */
	char *buf = malloc(n);
	if (!buf)
		return;
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n')
			continue;
		/* pas de séquence de fin de collage cachée dans le texte */
		if (s[i] == 0x1b)
			continue;
		buf[k++] = s[i] == '\n' ? '\r' : s[i];
	}
	vterm_keyboard_start_paste(t->vt);
	tab_write(t, buf, k);
	vterm_keyboard_end_paste(t->vt);
	free(buf);
	t->view = 0;
	A.dirty = true;
}

void clipboard_paste(void)
{
	if (A.source && A.clip) { /* c'est nous qui possédons le presse-papiers */
		paste_text(A.clip, strlen(A.clip));
		return;
	}
	if (!A.offer || !A.offer_text || A.paste_fd >= 0)
		return;
	int p[2];
	if (pipe2(p, O_CLOEXEC) < 0)
		return;
	wl_data_offer_receive(A.offer, offer_mime, p[1]);
	close(p[1]);
	wl_display_flush(A.display);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	A.paste_fd = p[0];
	A.paste_len = 0;
}

static void paste_read(void)
{
	for (;;) {
		if (A.paste_len + 4096 > A.paste_cap) {
			size_t cap = A.paste_cap ? A.paste_cap * 2 : 16384;
			if (cap > (16u << 20))
				goto done; /* 16 Mo maximum */
			char *nb = realloc(A.paste_buf, cap);
			if (!nb)
				goto done;
			A.paste_buf = nb;
			A.paste_cap = cap;
		}
		ssize_t n = read(A.paste_fd, A.paste_buf + A.paste_len, A.paste_cap - A.paste_len);
		if (n > 0) {
			A.paste_len += n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && errno == EAGAIN)
			return;
		break;
	}
done:
	close(A.paste_fd);
	A.paste_fd = -1;
	paste_text(A.paste_buf, A.paste_len);
	A.paste_len = 0;
}

static void offer_offer(void *d, struct wl_data_offer *o, const char *mime)
{
	(void)d;
	if (o != A.pending_offer)
		return;
	for (size_t i = 0; i < sizeof TEXT_MIMES / sizeof TEXT_MIMES[0]; i++)
		if (!strcmp(mime, TEXT_MIMES[i])) {
			/* on garde le type le plus précis (ordre de TEXT_MIMES) */
			if (!A.pending_text) {
				A.pending_text = true;
				snprintf(offer_mime, sizeof offer_mime, "%s", mime);
			} else {
				for (size_t j = 0; j < i; j++)
					if (!strcmp(offer_mime, TEXT_MIMES[j]))
						return;
				snprintf(offer_mime, sizeof offer_mime, "%s", mime);
			}
		}
}
static void offer_src_actions(void *d, struct wl_data_offer *o, uint32_t a) { (void)d; (void)o; (void)a; }
static void offer_action(void *d, struct wl_data_offer *o, uint32_t a) { (void)d; (void)o; (void)a; }
static const struct wl_data_offer_listener offer_listener = {
	.offer = offer_offer, .source_actions = offer_src_actions, .action = offer_action,
};

static void dd_data_offer(void *d, struct wl_data_device *dd, struct wl_data_offer *o)
{
	(void)d; (void)dd;
	A.pending_offer = o;
	A.pending_text = false;
	offer_mime[0] = '\0';
	wl_data_offer_add_listener(o, &offer_listener, NULL);
}
static struct wl_data_offer *dnd_offer;
static void dd_enter(void *d, struct wl_data_device *dd, uint32_t s, struct wl_surface *sf, wl_fixed_t x,
		     wl_fixed_t y, struct wl_data_offer *o)
{
	(void)d; (void)dd; (void)s; (void)sf; (void)x; (void)y;
	dnd_offer = o; /* glisser-déposer : non géré */
}
static void dd_leave(void *d, struct wl_data_device *dd)
{
	(void)d; (void)dd;
	if (dnd_offer && dnd_offer != A.offer)
		wl_data_offer_destroy(dnd_offer);
	dnd_offer = NULL;
}
static void dd_motion(void *d, struct wl_data_device *dd, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)dd; (void)t; (void)x; (void)y;
}
static void dd_drop(void *d, struct wl_data_device *dd) { dd_leave(d, dd); }
static void dd_selection(void *d, struct wl_data_device *dd, struct wl_data_offer *o)
{
	(void)d; (void)dd;
	if (A.offer && A.offer != o)
		wl_data_offer_destroy(A.offer);
	A.offer = o;
	A.offer_text = o && o == A.pending_offer && A.pending_text;
}
static const struct wl_data_device_listener dd_listener = {
	.data_offer = dd_data_offer, .enter = dd_enter, .leave = dd_leave,
	.motion = dd_motion, .drop = dd_drop, .selection = dd_selection,
};

/* ----------------------------- sélection ----------------------------- */

static bool word_char(uint32_t c)
{
	if (c == 0 || c == ' ' || c == '\t' || c == CONT_CELL)
		return false;
	return !strchr("\"'`()[]{}<>|;,", (int)(c < 128 ? c : 'a'));
}

static void word_bounds(struct tab *t, struct vpos p, struct vpos *s, struct vpos *e)
{
	struct cell c;
	*s = *e = p;
	tab_cell(t, p.row, p.col, &c);
	if (!word_char(c.ch))
		return;
	while (s->col > 0) {
		tab_cell(t, p.row, s->col - 1, &c);
		if (!word_char(c.ch) && c.ch != CONT_CELL)
			break;
		s->col--;
	}
	while (e->col < t->cols - 1) {
		tab_cell(t, p.row, e->col + 1, &c);
		if (!word_char(c.ch) && c.ch != CONT_CELL)
			break;
		e->col++;
	}
}

static void selection_update(struct tab *t, struct vpos cur)
{
	struct selection *s = &A.sel;
	bool fwd = cur.row > sel_origin.row || (cur.row == sel_origin.row && cur.col >= sel_origin.col);
	struct vpos o = sel_origin;
	if (s->mode == 0) {
		s->a = o;
		s->b = cur;
	} else if (s->mode == 1) {
		struct vpos os, oe, cs, ce;
		word_bounds(t, o, &os, &oe);
		word_bounds(t, cur, &cs, &ce);
		s->a = fwd ? os : oe;
		s->b = fwd ? ce : cs;
	} else {
		s->a = (struct vpos){ fwd ? o.row : o.row, fwd ? 0 : t->cols - 1 };
		s->b = (struct vpos){ cur.row, fwd ? t->cols - 1 : 0 };
	}
	s->active = true;
	A.dirty = true;
}

static char *selection_text(void)
{
	struct tab *t = tab_active();
	if (!t || !A.sel.active)
		return NULL;
	return tab_text(t, A.sel.a, A.sel.b);
}

static void copy_selection(void)
{
	char *s = selection_text();
	if (s && *s)
		clipboard_copy(s);
	free(s);
}

/* ----------------------------- clavier ------------------------------- */

static void disarm_repeat(void)
{
	struct itimerspec its = { 0 };
	timerfd_settime(A.repeat_fd, 0, &its, NULL);
	A.repeat_key = 0;
}

static bool mod_on(const char *name)
{
	return A.xkb_state && xkb_state_mod_name_is_active(A.xkb_state, name, XKB_STATE_MODS_EFFECTIVE) > 0;
}

/* modificateur actif et non « consommé » par la disposition (AltGr, Maj…) */
static bool mod_free(uint32_t key, const char *name)
{
	xkb_mod_index_t i = xkb_keymap_mod_get_index(A.keymap, name);
	if (i == XKB_MOD_INVALID)
		return false;
	return xkb_state_mod_index_is_active(A.xkb_state, i, XKB_STATE_MODS_EFFECTIVE) > 0 &&
	       !xkb_state_mod_index_is_consumed(A.xkb_state, key + 8, i);
}

static VTermKey map_key(xkb_keysym_t s)
{
	switch (s) {
	case XKB_KEY_Return: case XKB_KEY_KP_Enter: return VTERM_KEY_ENTER;
	case XKB_KEY_Tab: case XKB_KEY_ISO_Left_Tab: return VTERM_KEY_TAB;
	case XKB_KEY_BackSpace: return VTERM_KEY_BACKSPACE;
	case XKB_KEY_Escape: return VTERM_KEY_ESCAPE;
	case XKB_KEY_Up: case XKB_KEY_KP_Up: return VTERM_KEY_UP;
	case XKB_KEY_Down: case XKB_KEY_KP_Down: return VTERM_KEY_DOWN;
	case XKB_KEY_Left: case XKB_KEY_KP_Left: return VTERM_KEY_LEFT;
	case XKB_KEY_Right: case XKB_KEY_KP_Right: return VTERM_KEY_RIGHT;
	case XKB_KEY_Insert: case XKB_KEY_KP_Insert: return VTERM_KEY_INS;
	case XKB_KEY_Delete: case XKB_KEY_KP_Delete: return VTERM_KEY_DEL;
	case XKB_KEY_Home: case XKB_KEY_KP_Home: return VTERM_KEY_HOME;
	case XKB_KEY_End: case XKB_KEY_KP_End: return VTERM_KEY_END;
	case XKB_KEY_Prior: case XKB_KEY_KP_Prior: return VTERM_KEY_PAGEUP;
	case XKB_KEY_Next: case XKB_KEY_KP_Next: return VTERM_KEY_PAGEDOWN;
	default: break;
	}
	if (s >= XKB_KEY_F1 && s <= XKB_KEY_F35)
		return VTERM_KEY_FUNCTION(s - XKB_KEY_F1 + 1);
	return VTERM_KEY_NONE;
}

/* raccourcis propres à plume-term ; renvoie true si la touche est prise */
static bool shortcut(uint32_t key, xkb_keysym_t sym)
{
	bool ctrl = mod_on(XKB_MOD_NAME_CTRL), shift = mod_on(XKB_MOD_NAME_SHIFT), alt = mod_on(XKB_MOD_NAME_ALT);
	xkb_keysym_t low = xkb_keysym_to_lower(sym);
	struct tab *t = tab_active();
	if (ctrl && shift && !alt) {
		switch (low) {
		case XKB_KEY_t: new_tab(); return true;
		case XKB_KEY_w: close_tab(A.active); return true;
		case XKB_KEY_c: copy_selection(); return true;
		case XKB_KEY_v: clipboard_paste(); return true;
		case XKB_KEY_h: set_hud(!A.hud); return true;
		case XKB_KEY_f: /* mode HUD : clavier vers la recherche de fichiers, ou retour */
			if (A.hud)
				hud_focus_files(!H.focus);
			return A.hud;
		default: break;
		}
	}
	if (ctrl && !alt) {
		switch (sym) {
		case XKB_KEY_plus: case XKB_KEY_equal: case XKB_KEY_KP_Add:
			set_font(A.font_px + 1); return true;
		case XKB_KEY_minus: case XKB_KEY_underscore: case XKB_KEY_KP_Subtract:
			set_font(A.font_px - 1); return true;
		case XKB_KEY_0: case XKB_KEY_agrave: case XKB_KEY_KP_0:
			set_font(DEFAULT_FONT_PX); return true;
		case XKB_KEY_Tab: cycle_tab(1); return true;
		case XKB_KEY_ISO_Left_Tab: cycle_tab(-1); return true;
		case XKB_KEY_Next: if (!shift) { cycle_tab(1); return true; } break;
		case XKB_KEY_Prior: if (!shift) { cycle_tab(-1); return true; } break;
		default: break;
		}
	}
	/* Alt+1…9 : aller à l'onglet (touche physique, marche en AZERTY) */
	if (alt && !ctrl && key >= KEY_1 && key <= KEY_9) {
		switch_tab(key - KEY_1);
		return true;
	}
	if (shift && !ctrl && !alt && (sym == XKB_KEY_Insert || sym == XKB_KEY_KP_Insert)) {
		clipboard_paste();
		return true;
	}
	/* Maj + Page↑/Page↓/Début/Fin : parcourir l'historique */
	if (shift && !ctrl && t && !t->altscreen) {
		int page = t->rows > 2 ? t->rows - 2 : 1;
		switch (sym) {
		case XKB_KEY_Prior: case XKB_KEY_KP_Prior: tab_scroll_view(t, page); return true;
		case XKB_KEY_Next: case XKB_KEY_KP_Next: tab_scroll_view(t, -page); return true;
		case XKB_KEY_Home: case XKB_KEY_KP_Home: tab_scroll_view(t, t->sb_count); return true;
		case XKB_KEY_End: case XKB_KEY_KP_End: tab_scroll_view(t, -t->sb_count); return true;
		default: break;
		}
	}
	return false;
}

static void key_press(uint32_t key)
{
	struct tab *t = tab_active();
	xkb_keysym_t sym = xkb_state_key_get_one_sym(A.xkb_state, key + 8);
	if (shortcut(key, sym))
		return;
	if (!t)
		return;

	/* touches mortes : ^ puis e = ê */
	char composed[16] = "";
	if (compose_state && xkb_compose_state_feed(compose_state, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
		switch (xkb_compose_state_get_status(compose_state)) {
		case XKB_COMPOSE_COMPOSING:
			return;
		case XKB_COMPOSE_CANCELLED:
			xkb_compose_state_reset(compose_state);
			return;
		case XKB_COMPOSE_COMPOSED:
			xkb_compose_state_get_utf8(compose_state, composed, sizeof composed);
			sym = xkb_compose_state_get_one_sym(compose_state);
			xkb_compose_state_reset(compose_state);
			break;
		default:
			break;
		}
	}

	/* mode HUD : l'explorateur a le clavier (recherche, flèches…) */
	if (A.hud && H.focus) {
		char text[16] = "";
		if (composed[0])
			snprintf(text, sizeof text, "%s", composed);
		else
			xkb_keysym_to_utf8(sym, text, sizeof text);
		hud_key(sym, text, mod_on(XKB_MOD_NAME_CTRL));
		return;
	}

	VTermModifier mod = VTERM_MOD_NONE;
	if (mod_free(key, XKB_MOD_NAME_CTRL)) mod |= VTERM_MOD_CTRL;
	if (mod_free(key, XKB_MOD_NAME_ALT)) mod |= VTERM_MOD_ALT;
	bool shift = mod_on(XKB_MOD_NAME_SHIFT);

	VTermKey vk = map_key(sym);
	if (vk != VTERM_KEY_NONE) {
		if (shift) mod |= VTERM_MOD_SHIFT;
		if (sym == XKB_KEY_ISO_Left_Tab) mod |= VTERM_MOD_SHIFT;
		vterm_keyboard_key(t->vt, vk, mod);
	} else if (composed[0]) {
		/* texte composé : on l'envoie tel quel */
		const unsigned char *p = (const unsigned char *)composed;
		while (*p) {
			uint32_t c = *p;
			int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
			if (extra) c &= 0x3F >> extra;
			p++;
			for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++, p++)
				c = (c << 6) | (*p & 0x3F);
			vterm_keyboard_unichar(t->vt, c, mod);
		}
	} else {
		uint32_t c = xkb_keysym_to_utf32(sym);
		if (!c)
			return;
		/* Ctrl+caractère : libvterm calcule le code de contrôle */
		vterm_keyboard_unichar(t->vt, c, mod);
	}
	t->view = 0;
	A.sel.active = false;
	A.dirty = true;
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
	struct xkb_keymap *km = xkb_keymap_new_from_string(A.xkb_ctx, map, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(map, size);
	if (!km)
		return;
	if (A.xkb_state) xkb_state_unref(A.xkb_state);
	if (A.keymap) xkb_keymap_unref(A.keymap);
	A.keymap = km;
	A.xkb_state = xkb_state_new(km);
	hud_keymap_changed();
	A.dirty = true;
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys)
{
	(void)d; (void)k; (void)sf; (void)keys;
	A.last_serial = s;
	A.focused = true;
	struct tab *t = tab_active();
	if (t)
		vterm_state_focus_in(t->state);
	A.dirty = true;
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)k; (void)s; (void)sf;
	disarm_repeat();
	A.focused = false;
	if (compose_state)
		xkb_compose_state_reset(compose_state);
	struct tab *t = tab_active();
	if (t)
		vterm_state_focus_out(t->state);
	A.dirty = true;
}

static void kb_key(void *dd, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	(void)dd; (void)k; (void)time;
	if (!A.xkb_state)
		return;
	A.last_serial = serial;
	if (A.hud) {
		hud_key_light(key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
		A.dirty = true;
	}
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		key_press(key);
		if (A.repeat_rate > 0 && xkb_keymap_key_repeats(A.keymap, key + 8)) {
			A.repeat_key = key;
			struct itimerspec its = { 0 };
			its.it_value.tv_sec = A.repeat_delay / 1000;
			its.it_value.tv_nsec = (A.repeat_delay % 1000) * 1000000L;
			long iv = 1000000000L / A.repeat_rate;
			its.it_interval.tv_sec = iv / 1000000000L;
			its.it_interval.tv_nsec = iv % 1000000000L;
			timerfd_settime(A.repeat_fd, 0, &its, NULL);
		}
	} else if (key == A.repeat_key) {
		disarm_repeat();
	}
}

static void kb_mods(void *dd, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lck,
		    uint32_t group)
{
	(void)dd; (void)k; (void)serial;
	if (!A.xkb_state)
		return;
	xkb_layout_index_t before = xkb_state_serialize_layout(A.xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
	xkb_state_update_mask(A.xkb_state, dep, lat, lck, 0, 0, group);
	if (xkb_state_serialize_layout(A.xkb_state, XKB_STATE_LAYOUT_EFFECTIVE) != before) {
		hud_keymap_changed(); /* AZERTY ⇄ QWERTY : le clavier à l'écran suit */
		A.dirty = true;
	}
}

static void kb_repeat_info(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
	(void)d; (void)k;
	A.repeat_rate = rate;
	A.repeat_delay = delay;
}

static const struct wl_keyboard_listener kb_listener = {
	.keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave, .key = kb_key,
	.modifiers = kb_mods, .repeat_info = kb_repeat_info,
};

/* ------------------------------ souris ------------------------------- */

static bool in(struct rect r, double x, double y)
{
	return r.w > 0 && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

static void set_cursor(const char *name)
{
	if (!A.cursor_theme || !A.pointer || (A.cursor_name && !strcmp(A.cursor_name, name)))
		return;
	struct wl_cursor *c = wl_cursor_theme_get_cursor(A.cursor_theme, name);
	if (!c && !strcmp(name, "text"))
		c = wl_cursor_theme_get_cursor(A.cursor_theme, "xterm");
	if (!c)
		c = wl_cursor_theme_get_cursor(A.cursor_theme, "left_ptr");
	if (!c || !c->image_count)
		return;
	struct wl_cursor_image *img = c->images[0];
	wl_surface_attach(A.cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
	wl_surface_damage(A.cursor_surface, 0, 0, img->width, img->height);
	wl_surface_commit(A.cursor_surface);
	wl_pointer_set_cursor(A.pointer, A.pointer_serial, A.cursor_surface, img->hotspot_x, img->hotspot_y);
	A.cursor_name = name;
}

/* cellule sous le pointeur (bornée à la grille) */
static void pointer_cell(struct tab *t, int *row, int *col)
{
	int c = (int)((A.px - A.r_grid.x) / A.cw), r = (int)((A.py - A.r_grid.y) / A.ch);
	if (A.px < A.r_grid.x) c = 0;
	if (A.py < A.r_grid.y) r = 0;
	if (c >= t->cols) c = t->cols - 1;
	if (r >= t->rows) r = t->rows - 1;
	if (c < 0) c = 0;
	if (r < 0) r = 0;
	*row = r;
	*col = c;
}

static VTermModifier pointer_mods(void)
{
	VTermModifier m = VTERM_MOD_NONE;
	if (mod_on(XKB_MOD_NAME_SHIFT)) m |= VTERM_MOD_SHIFT;
	if (mod_on(XKB_MOD_NAME_CTRL)) m |= VTERM_MOD_CTRL;
	if (mod_on(XKB_MOD_NAME_ALT)) m |= VTERM_MOD_ALT;
	return m;
}

/* le programme veut la souris (htop, micro…) — Maj force la sélection */
static bool app_mouse(struct tab *t)
{
	return t && t->mouse_mode != VTERM_PROP_MOUSE_NONE && !mod_on(XKB_MOD_NAME_SHIFT) && t->view == 0;
}

static void update_hover_cursor(void)
{
	set_cursor(in(A.r_grid, A.px, A.py) ? "text" : "left_ptr");
}

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)sf;
	A.pointer_serial = serial;
	A.cursor_name = NULL;
	A.px = wl_fixed_to_double(x);
	A.py = wl_fixed_to_double(y);
	update_hover_cursor();
	A.dirty = true;
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
	(void)d; (void)p; (void)s; (void)sf;
	A.px = A.py = -1;
	A.btn_left = false;
	A.dirty = true;
}

static void p_motion(void *d, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)time;
	double ox = A.px, oy = A.py;
	A.px = wl_fixed_to_double(x);
	A.py = wl_fixed_to_double(y);
	if (A.hud && hud_hover_changed(ox, oy, A.px, A.py))
		A.dirty = true;
	update_hover_cursor();
	struct tab *t = tab_active();
	if (!t) {
		A.dirty = true;
		return;
	}
	int row, col;
	pointer_cell(t, &row, &col);
	if (app_mouse(t) && in(A.r_grid, A.px, A.py)) {
		vterm_mouse_move(t->vt, row, col, pointer_mods());
	} else if (A.btn_left && A.sel.dragging) {
		/* glisser au-delà du haut/bas : l'historique défile */
		if (A.py < A.r_grid.y) tab_scroll_view(t, 1);
		else if (A.py > A.r_grid.y + A.r_grid.h) tab_scroll_view(t, -1);
		selection_update(t, (struct vpos){ row - t->view, col });
	}
	/* survol de la barre d'onglets */
	if (A.py < A.r_grid.y)
		A.dirty = true;
}

static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)time;
	A.last_serial = serial;
	bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
	double x = A.px, y = A.py;
	struct tab *t = tab_active();
	A.dirty = true;

	/* mode HUD : clics dans les panneaux (fichiers, clavier) */
	bool in_term_col = x >= A.r_grid.x - A.pad * 2 && x <= A.r_grid.x + A.r_grid.w + A.pad * 2 &&
			   y <= A.r_grid.y + A.r_grid.h + A.pad * 2;
	if (A.hud && !in_term_col) {
		if (down)
			hud_click(x, y, button);
		return;
	}
	if (A.hud && down)
		hud_focus_files(false); /* clic dans le terminal : il reprend le clavier */
	/* barre d'onglets */
	if (y < A.r_grid.y - A.pad * 0.3 && down) {
		for (int i = 0; i < MAX_TABS; i++) {
			if (in(A.r_tab_close[i], x, y) && button == BTN_LEFT) {
				close_tab(i);
				return;
			}
			if (in(A.r_tabs[i], x, y)) {
				if (button == BTN_MIDDLE)
					close_tab(i);
				else
					switch_tab(i);
				return;
			}
		}
		if (in(A.r_new, x, y) && button == BTN_LEFT)
			new_tab();
		return;
	}
	if (!t)
		return;
	int row, col;
	pointer_cell(t, &row, &col);

	if (app_mouse(t)) {
		int b = button == BTN_LEFT ? 1 : button == BTN_MIDDLE ? 2 : button == BTN_RIGHT ? 3 : 0;
		if (b)
			vterm_mouse_button(t->vt, b, down, pointer_mods());
		return;
	}
	if (button == BTN_LEFT) {
		A.btn_left = down;
		if (down) {
			struct vpos vp = { row - t->view, col };
			double tn = now_s();
			if (tn - A.last_click_t < 0.4 && vp.row == A.last_click_pos.row && abs(vp.col - A.last_click_pos.col) <= 1)
				A.click_count = A.click_count % 3 + 1;
			else
				A.click_count = 1;
			A.last_click_t = tn;
			A.last_click_pos = vp;
			sel_origin = vp;
			A.sel.mode = A.click_count - 1;
			A.sel.dragging = true;
			if (A.sel.mode == 0)
				A.sel.active = false;
			else
				selection_update(t, vp);
		} else {
			A.sel.dragging = false;
			if (A.sel.active) {
				free(sel_text);
				sel_text = selection_text();
			}
		}
	} else if (button == BTN_MIDDLE && down) {
		if (sel_text && *sel_text)
			paste_text(sel_text, strlen(sel_text));
		else
			clipboard_paste();
	} else if (button == BTN_RIGHT && down) {
		/* clic droit : copier la sélection, sinon coller */
		if (A.sel.active) {
			copy_selection();
			A.sel.active = false;
		} else {
			clipboard_paste();
		}
	}
}

static void p_axis(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t v)
{
	(void)d; (void)p; (void)time;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
		return;
	struct tab *t = tab_active();
	if (!t)
		return;
	A.axis_acc += wl_fixed_to_double(v);
	while (A.axis_acc >= 10 || A.axis_acc <= -10) {
		bool downw = A.axis_acc > 0;
		A.axis_acc += downw ? -10 : 10;
		if (A.hud && hud_scroll(A.px, A.py, downw ? 1 : -1)) {
			A.dirty = true;
			continue;
		}
		if (app_mouse(t)) {
			int row, col;
			pointer_cell(t, &row, &col);
			vterm_mouse_move(t->vt, row, col, pointer_mods());
			vterm_mouse_button(t->vt, downw ? 5 : 4, true, pointer_mods());
		} else if (t->altscreen) {
			/* écran alternatif (less, man…) : la molette envoie des flèches */
			for (int i = 0; i < 3; i++)
				vterm_keyboard_key(t->vt, downw ? VTERM_KEY_DOWN : VTERM_KEY_UP, VTERM_MOD_NONE);
		} else {
			tab_scroll_view(t, downw ? -3 : 3);
		}
	}
}

static const struct wl_pointer_listener ptr_listener = {
	.enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button, .axis = p_axis,
};

/* claviers et souris peuvent disparaître puis revenir (USB débranché…) :
 * l'ancien objet devient inerte, il faut en redemander un */
static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	(void)d;
	bool kb = caps & WL_SEAT_CAPABILITY_KEYBOARD, ptr = caps & WL_SEAT_CAPABILITY_POINTER;
	if (kb && !A.keyboard) {
		A.keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(A.keyboard, &kb_listener, NULL);
	} else if (!kb && A.keyboard) {
		disarm_repeat();
		wl_keyboard_release(A.keyboard);
		A.keyboard = NULL;
	}
	if (ptr && !A.pointer) {
		A.pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(A.pointer, &ptr_listener, NULL);
		A.cursor_name = NULL;
	} else if (!ptr && A.pointer) {
		wl_pointer_release(A.pointer);
		A.pointer = NULL;
		A.btn_left = false;
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
	} else if (!strcmp(iface, wl_data_device_manager_interface.name)) {
		A.ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, minv(ver, 3));
	}
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = { .global = reg_global, .global_remove = reg_remove };

/* ------------------------------ main --------------------------------- */

static void usage(const char *argv0)
{
	printf("Usage : %s [options] [commande [arguments…]]\n"
	       "  Terminal HUD de Plume Linux.\n\n"
	       "  -e COMMANDE…        lance cette commande au lieu du shell\n"
	       "  -D, --working-directory DOSSIER\n"
	       "  -f, --font-size PX  taille de la police (défaut %.0f)\n"
	       "  -a, --app-id ID     identifiant de fenêtre (règles sway)\n"
	       "  -T, --title TITRE\n"
	       "  --hud               plein écran avec panneaux : système, réseau, fichiers, clavier\n"
	       "  -v, --version\n\n"
	       "  Ctrl+Maj+T nouvel onglet · Ctrl+Maj+W fermer · Ctrl+Tab / Alt+1…9 changer\n"
	       "  Ctrl+Maj+C copier · Ctrl+Maj+V coller · Maj+Page↑/↓ historique\n"
	       "  Ctrl+plus / Ctrl+moins / Ctrl+0 taille du texte · Ctrl+Maj+H mode HUD\n"
	       "  Mode HUD : Ctrl+Maj+F (ou clic) = clavier vers l'explorateur, Échap = retour\n",
	       argv0, DEFAULT_FONT_PX);
}

static void init_compose(void)
{
	const char *loc = getenv("LC_ALL");
	if (!loc || !*loc) loc = getenv("LC_CTYPE");
	if (!loc || !*loc) loc = getenv("LANG");
	if (!loc || !*loc) loc = "C";
	compose_table = xkb_compose_table_new_from_locale(A.xkb_ctx, loc, XKB_COMPOSE_COMPILE_NO_FLAGS);
	if (!compose_table)
		compose_table = xkb_compose_table_new_from_locale(A.xkb_ctx, "en_US.UTF-8", XKB_COMPOSE_COMPILE_NO_FLAGS);
	if (compose_table)
		compose_state = xkb_compose_state_new(compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
}

int main(int argc, char **argv)
{
	double font_px = DEFAULT_FONT_PX;
	const char *env_fs = getenv("PLUME_TERM_FONT_SIZE");
	if (env_fs && atof(env_fs) > 0)
		font_px = atof(env_fs);
	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		bool has_next = i + 1 < argc;
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(argv[0]);
			return 0;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--version")) {
			printf("plume-term %s (libvterm %d.%d)\n", VERSION, VTERM_VERSION_MAJOR, VTERM_VERSION_MINOR);
			return 0;
		} else if ((!strcmp(a, "-D") || !strcmp(a, "--working-directory")) && has_next) {
			A.workdir = argv[++i];
		} else if ((!strcmp(a, "-f") || !strcmp(a, "--font-size")) && has_next) {
			font_px = atof(argv[++i]);
		} else if ((!strcmp(a, "-a") || !strcmp(a, "--app-id")) && has_next) {
			app_id = argv[++i];
		} else if ((!strcmp(a, "-T") || !strcmp(a, "--title")) && has_next) {
			snprintf(base_title, sizeof base_title, "%s", argv[++i]);
		} else if (!strcmp(a, "--hud")) {
			A.hud = true;
		} else if (!strcmp(a, "-e") || !strcmp(a, "--")) {
			i++;
			break;
		} else if (a[0] == '-') {
			fprintf(stderr, "plume-term : option inconnue « %s » (voir --help)\n", a);
			return 2;
		} else {
			break;
		}
	}
	if (i < argc)
		A.cmd = &argv[i];

	setlocale(LC_CTYPE, "");
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_DFL);
	A.paste_fd = -1;
	A.start = now_s();
	for (int k = 0; k < MAX_TABS; k++)
		A.tabs[k].fd = -1;

	A.display = wl_display_connect(NULL);
	if (!A.display) {
		fprintf(stderr, "plume-term : pas de connexion Wayland (lancez-le depuis le bureau)\n");
		return 1;
	}
	A.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	init_compose();
	struct wl_registry *reg = wl_display_get_registry(A.display);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(A.display);
	if (!A.compositor || !A.shm || !A.wm_base) {
		fprintf(stderr, "plume-term : compositeur incomplet\n");
		return 1;
	}
	if (A.ddm && A.seat) {
		A.data_device = wl_data_device_manager_get_data_device(A.ddm, A.seat);
		wl_data_device_add_listener(A.data_device, &dd_listener, NULL);
	}

	font_setup(font_px);
	if (!new_tab()) {
		fprintf(stderr, "plume-term : impossible de créer un pseudo-terminal\n");
		return 1;
	}

	A.surface = wl_compositor_create_surface(A.compositor);
	A.xdg_surface = xdg_wm_base_get_xdg_surface(A.wm_base, A.surface);
	xdg_surface_add_listener(A.xdg_surface, &xs_listener, NULL);
	A.toplevel = xdg_surface_get_toplevel(A.xdg_surface);
	xdg_toplevel_add_listener(A.toplevel, &tl_listener, NULL);
	xdg_toplevel_set_app_id(A.toplevel, app_id);
	xdg_toplevel_set_title(A.toplevel, base_title);
	if (A.hud)
		set_hud(true);
	wl_surface_commit(A.surface);

	const char *cs = getenv("XCURSOR_SIZE");
	A.cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), cs && atoi(cs) > 0 ? atoi(cs) : 24, A.shm);
	A.cursor_surface = wl_compositor_create_surface(A.compositor);

	int tick = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	struct itimerspec its = { { 1, 0 }, { 1, 0 } };
	timerfd_settime(tick, 0, &its, NULL);
	A.repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

	int last_min = -1;
	A.running = true;
	while (A.running) {
		if (A.dirty && !A.frame_cb && A.configured)
			render();
		while (wl_display_prepare_read(A.display) != 0)
			wl_display_dispatch_pending(A.display);
		wl_display_flush(A.display);

		struct pollfd fds[4 + MAX_TABS];
		int nf = 0;
		fds[nf++] = (struct pollfd){ .fd = wl_display_get_fd(A.display), .events = POLLIN };
		fds[nf++] = (struct pollfd){ .fd = tick, .events = POLLIN };
		fds[nf++] = (struct pollfd){ .fd = A.repeat_fd, .events = POLLIN };
		fds[nf++] = (struct pollfd){ .fd = A.paste_fd, .events = POLLIN };
		int tab_of[MAX_TABS];
		for (int k = 0; k < MAX_TABS; k++)
			if (A.tabs[k].used) {
				tab_of[nf - 4] = k;
				fds[nf++] = (struct pollfd){ .fd = A.tabs[k].fd, .events = POLLIN };
			}
		/* animations courtes (sonnerie, taille) : on redessine un peu plus souvent */
		struct tab *at = tab_active();
		double tn = now_s();
		static double last_t;
		if (A.hud && last_t > 0)
			hud_decay(tn - last_t);
		last_t = tn;
		bool anim = (at && tn - at->bell_t < 0.4) || tn - A.resized_t < 1.6 || (A.hud && hud_animating());
		if (anim)
			A.dirty = true;
		int timeout = anim || (A.dirty && !A.frame_cb) ? 30 : -1;

		if (poll(fds, nf, timeout) < 0) {
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

		for (int k = 4; k < nf; k++)
			if (fds[k].revents & (POLLIN | POLLHUP | POLLERR)) {
				struct tab *t = &A.tabs[tab_of[k - 4]];
				if (t->used && t->fd == fds[k].fd) {
					tab_read(t);
					if (!t->used)
						after_close();
				}
			}

		uint64_t exp;
		if ((fds[2].revents & POLLIN) && read(A.repeat_fd, &exp, sizeof exp) > 0 && A.repeat_key && A.xkb_state)
			key_press(A.repeat_key);
		if ((fds[3].revents & (POLLIN | POLLHUP)) && A.paste_fd >= 0)
			paste_read();
		if ((fds[1].revents & POLLIN) && read(tick, &exp, sizeof exp) > 0) {
			for (int k = 0; k < MAX_TABS; k++) {
				struct tab *t = &A.tabs[k];
				if (!t->used)
					continue;
				int st;
				if (t->pid > 0 && waitpid(t->pid, &st, WNOHANG) == t->pid) {
					t->pid = 0;
					/* le shell est fini : on lit ce qui reste, puis on ferme */
					tab_read(t);
					if (t->used)
						tab_close(t);
					after_close();
					continue;
				}
				tab_refresh_label(t);
			}
			while (waitpid(-1, NULL, WNOHANG) > 0)
				;
			if (A.hud) {
				hud_tick();
				A.dirty = true; /* horloge à la seconde, mesures */
			}
			time_t now = time(NULL);
			struct tm tm;
			localtime_r(&now, &tm);
			if (tm.tm_min != last_min) {
				last_min = tm.tm_min;
				A.dirty = true;
			}
		}
	}
	for (int k = 0; k < MAX_TABS; k++)
		tab_close(&A.tabs[k]);
	return 0;
}
