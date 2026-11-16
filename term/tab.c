/* plume-term — onglets : pseudo-terminal, libvterm, historique, texte. */
#include "term.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* palette du HUD (identique au thème foot de Plume) */
static const uint32_t PALETTE[16] = {
	0x0b1620, 0xff5470, 0x4fe0a4, 0xe6c86e, 0x4fb8e0, 0xb48cff, 0x6fe3ff, 0xcdeefc,
	0x1d5a78, 0xff7a90, 0x7af0bf, 0xf2dc94, 0x9fe4ff, 0xcbb0ff, 0xa6f0ff, 0xe9f9ff,
};
#define DEF_FG 0xcdeefc
#define DEF_BG 0x02060b

static VTermColor vcol(uint32_t c)
{
	VTermColor v;
	vterm_color_rgb(&v, (c >> 16) & 255, (c >> 8) & 255, c & 255);
	return v;
}

static uint32_t to_rgb(struct tab *t, VTermColor c)
{
	if (VTERM_COLOR_IS_INDEXED(&c))
		vterm_screen_convert_color_to_rgb(t->screen, &c);
	return (uint32_t)c.rgb.red << 16 | (uint32_t)c.rgb.green << 8 | c.rgb.blue;
}

static void from_vcell(struct tab *t, const VTermScreenCell *vc, struct cell *out)
{
	out->ch = vc->chars[0];
	out->width = vc->width > 0 ? vc->width : 1;
	out->attrs = 0;
	if (vc->attrs.bold) out->attrs |= A_BOLD;
	if (vc->attrs.italic) out->attrs |= A_ITALIC;
	if (vc->attrs.underline) out->attrs |= A_UNDER;
	if (vc->attrs.reverse) out->attrs |= A_REVERSE;
	if (vc->attrs.strike) out->attrs |= A_STRIKE;
	if (vc->attrs.conceal) out->attrs |= A_CONCEAL;
	if (VTERM_COLOR_IS_DEFAULT_FG(&vc->fg)) out->attrs |= A_FG_DEF;
	if (VTERM_COLOR_IS_DEFAULT_BG(&vc->bg)) out->attrs |= A_BG_DEF;
	out->fg = to_rgb(t, vc->fg);
	out->bg = to_rgb(t, vc->bg);
}

static void blank(struct cell *c)
{
	memset(c, 0, sizeof *c);
	c->width = 1;
	c->fg = DEF_FG;
	c->bg = DEF_BG;
	c->attrs = A_FG_DEF | A_BG_DEF;
}

/* --------------------------- rappels libvterm ------------------------ */

static void mark_dirty(struct tab *t)
{
	t->dirty = true;
	if (t == tab_active())
		request_redraw();
}

static int cb_damage(VTermRect r, void *u) { (void)r; mark_dirty(u); return 1; }
static int cb_moverect(VTermRect d, VTermRect s, void *u) { (void)d; (void)s; mark_dirty(u); return 1; }

static int cb_movecursor(VTermPos pos, VTermPos old, int visible, void *u)
{
	(void)old; (void)visible;
	struct tab *t = u;
	t->cursor = pos;
	mark_dirty(t);
	return 1;
}

static int cb_settermprop(VTermProp prop, VTermValue *val, void *u)
{
	struct tab *t = u;
	switch (prop) {
	case VTERM_PROP_CURSORVISIBLE: t->cursor_visible = val->boolean; break;
	case VTERM_PROP_CURSORSHAPE: t->cursor_shape = val->number; break;
	case VTERM_PROP_ALTSCREEN:
		t->altscreen = val->boolean;
		t->view = 0;
		break;
	case VTERM_PROP_MOUSE: t->mouse_mode = val->number; break;
	case VTERM_PROP_TITLE: {
		static char acc[sizeof t->title];
		static size_t n;
		if (val->string.initial)
			n = 0;
		size_t m = val->string.len;
		if (n + m >= sizeof acc)
			m = sizeof acc - 1 - n;
		memcpy(acc + n, val->string.str, m);
		n += m;
		acc[n] = '\0';
		if (val->string.final) {
			snprintf(t->title, sizeof t->title, "%s", acc);
			tab_refresh_label(t);
		}
		break;
	}
	default:
		return 0;
	}
	mark_dirty(t);
	return 1;
}

static int cb_bell(void *u)
{
	struct tab *t = u;
	t->bell_t = now_s();
	mark_dirty(t);
	return 1;
}

static void sb_free_line(struct sbline *l)
{
	free(l->cells);
	l->cells = NULL;
	l->cols = 0;
}

static int cb_sb_pushline(int cols, const VTermScreenCell *cells, void *u)
{
	struct tab *t = u;
	/* retire les blancs de fin pour économiser la mémoire */
	int n = cols;
	while (n > 0 && (cells[n - 1].chars[0] == 0 || cells[n - 1].chars[0] == ' ') &&
	       VTERM_COLOR_IS_DEFAULT_BG(&cells[n - 1].bg) && !cells[n - 1].attrs.reverse)
		n--;
	int slot;
	if (t->sb_count < SCROLLBACK) {
		slot = (t->sb_head + t->sb_count) % SCROLLBACK;
		t->sb_count++;
	} else {
		slot = t->sb_head;
		t->sb_head = (t->sb_head + 1) % SCROLLBACK;
		sb_free_line(&t->sb[slot]);
	}
	struct sbline *l = &t->sb[slot];
	l->cols = n;
	l->cells = n ? malloc(sizeof(struct cell) * n) : NULL;
	if (n && !l->cells)
		l->cols = 0;
	for (int i = 0; i < l->cols; i++)
		from_vcell(t, &cells[i], &l->cells[i]);
	const VTermLineInfo *li = vterm_state_get_lineinfo(t->state, 0);
	l->cont = li && li->continuation;
	/* l'utilisateur lit l'historique : la vue ne bouge pas */
	if (t->view > 0 && t->view < t->sb_count)
		t->view++;
	/* la sélection suit le texte qui remonte */
	if (t == tab_active() && A.sel.active) {
		A.sel.a.row--;
		A.sel.b.row--;
	}
	return 1;
}

static int cb_sb_popline(int cols, VTermScreenCell *cells, void *u)
{
	struct tab *t = u;
	if (t->sb_count == 0)
		return 0;
	int slot = (t->sb_head + t->sb_count - 1) % SCROLLBACK;
	struct sbline *l = &t->sb[slot];
	for (int i = 0; i < cols; i++) {
		VTermScreenCell *vc = &cells[i];
		memset(vc, 0, sizeof *vc);
		struct cell c;
		if (i < l->cols)
			c = l->cells[i];
		else
			blank(&c);
		vc->chars[0] = c.ch;
		vc->width = c.width;
		vc->attrs.bold = !!(c.attrs & A_BOLD);
		vc->attrs.italic = !!(c.attrs & A_ITALIC);
		vc->attrs.underline = !!(c.attrs & A_UNDER);
		vc->attrs.reverse = !!(c.attrs & A_REVERSE);
		vc->attrs.strike = !!(c.attrs & A_STRIKE);
		vc->attrs.conceal = !!(c.attrs & A_CONCEAL);
		vc->fg = vcol(c.fg);
		vc->bg = vcol(c.bg);
		if (c.attrs & A_FG_DEF) vc->fg.type |= VTERM_COLOR_DEFAULT_FG;
		if (c.attrs & A_BG_DEF) vc->bg.type |= VTERM_COLOR_DEFAULT_BG;
	}
	sb_free_line(l);
	t->sb_count--;
	if (t == tab_active() && A.sel.active) {
		A.sel.a.row++;
		A.sel.b.row++;
	}
	if (t->view > t->sb_count)
		t->view = t->sb_count;
	return 1;
}

static int cb_sb_clear(void *u)
{
	struct tab *t = u;
	for (int i = 0; i < t->sb_count; i++)
		sb_free_line(&t->sb[(t->sb_head + i) % SCROLLBACK]);
	t->sb_head = t->sb_count = t->view = 0;
	mark_dirty(t);
	return 1;
}

static const VTermScreenCallbacks screen_cbs = {
	.damage = cb_damage,
	.moverect = cb_moverect,
	.movecursor = cb_movecursor,
	.settermprop = cb_settermprop,
	.bell = cb_bell,
	.sb_pushline = cb_sb_pushline,
	.sb_popline = cb_sb_popline,
	.sb_clear = cb_sb_clear,
};

static void on_output(const char *s, size_t len, void *u)
{
	tab_write(u, s, len);
}

/* ------------------------------ onglets ------------------------------ */

int tab_count(void)
{
	int n = 0;
	for (int i = 0; i < MAX_TABS; i++)
		n += A.tabs[i].used;
	return n;
}

struct tab *tab_active(void)
{
	struct tab *t = &A.tabs[A.active];
	return t->used ? t : NULL;
}

static const char *user_shell(void)
{
	const char *s = getenv("SHELL");
	if (s && *s && access(s, X_OK) == 0)
		return s;
	struct passwd *pw = getpwuid(getuid());
	if (pw && pw->pw_shell && *pw->pw_shell && access(pw->pw_shell, X_OK) == 0)
		return pw->pw_shell;
	return "/bin/sh";
}

bool tab_open(int slot, int rows, int cols, char **cmd, const char *dir)
{
	struct tab *t = &A.tabs[slot];
	memset(t, 0, sizeof *t);
	if (rows < 2) rows = 2;
	if (cols < 2) cols = 2;
	struct winsize ws = { .ws_row = rows, .ws_col = cols,
			      .ws_xpixel = (unsigned short)(cols * A.cw), .ws_ypixel = (unsigned short)(rows * A.ch) };
	int fd;
	pid_t pid = forkpty(&fd, NULL, NULL, &ws);
	if (pid < 0)
		return false;
	if (pid == 0) {
		for (int s = 1; s < NSIG; s++)
			signal(s, SIG_DFL);
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		if (dir && *dir && chdir(dir) != 0) {
			const char *home = getenv("HOME");
			if (home) chdir(home);
		}
		setenv("TERM", "xterm-256color", 1);
		setenv("COLORTERM", "truecolor", 1);
		setenv("TERM_PROGRAM", "plume-term", 1);
		unsetenv("COLUMNS");
		unsetenv("LINES");
		if (cmd && cmd[0]) {
			execvp(cmd[0], cmd);
			fprintf(stderr, "plume-term : impossible de lancer « %s » : %s\n", cmd[0], strerror(errno));
			sleep(3);
			_exit(127);
		}
		const char *sh = user_shell();
		const char *base = strrchr(sh, '/');
		execl(sh, base ? base + 1 : sh, (char *)NULL);
		_exit(127);
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

	t->used = true;
	t->fd = fd;
	t->pid = pid;
	t->rows = rows;
	t->cols = cols;
	t->cursor_visible = true;
	t->cursor_shape = VTERM_PROP_CURSORSHAPE_BAR_LEFT;
	t->vt = vterm_new(rows, cols);
	vterm_set_utf8(t->vt, 1);
	vterm_output_set_callback(t->vt, on_output, t);
	t->state = vterm_obtain_state(t->vt);
	t->screen = vterm_obtain_screen(t->vt);
	vterm_screen_set_callbacks(t->screen, &screen_cbs, t);
	vterm_screen_enable_altscreen(t->screen, 1);
	vterm_screen_enable_reflow(t->screen, true);
	vterm_screen_set_damage_merge(t->screen, VTERM_DAMAGE_SCROLL);
	vterm_state_set_bold_highbright(t->state, 1);
	vterm_screen_reset(t->screen, 1);
	for (int i = 0; i < 16; i++) {
		VTermColor c = vcol(PALETTE[i]);
		vterm_state_set_palette_color(t->state, i, &c);
	}
	VTermColor fg = vcol(DEF_FG), bg = vcol(DEF_BG);
	vterm_screen_set_default_colors(t->screen, &fg, &bg);
	/* la forme du curseur par défaut : barre fine, comme le thème foot */
	VTermValue v = { .number = VTERM_PROP_CURSORSHAPE_BAR_LEFT };
	vterm_state_set_termprop(t->state, VTERM_PROP_CURSORSHAPE, &v);
	tab_refresh_label(t);
	t->dirty = true;
	return true;
}

void tab_close(struct tab *t)
{
	if (!t->used)
		return;
	if (t->pid > 0) {
		kill(-t->pid, SIGHUP);
		kill(t->pid, SIGHUP);
		waitpid(t->pid, NULL, WNOHANG);
	}
	if (t->fd >= 0)
		close(t->fd);
	if (t->vt)
		vterm_free(t->vt);
	for (int i = 0; i < t->sb_count; i++)
		sb_free_line(&t->sb[(t->sb_head + i) % SCROLLBACK]);
	memset(t, 0, sizeof *t);
	t->fd = -1;
	if (A.sel.active)
		A.sel.active = false;
}

void tab_resize(struct tab *t, int rows, int cols)
{
	if (!t->used || rows < 2 || cols < 2 || (rows == t->rows && cols == t->cols))
		return;
	t->rows = rows;
	t->cols = cols;
	vterm_set_size(t->vt, rows, cols);
	vterm_screen_flush_damage(t->screen);
	struct winsize ws = { .ws_row = rows, .ws_col = cols,
			      .ws_xpixel = (unsigned short)(cols * A.cw), .ws_ypixel = (unsigned short)(rows * A.ch) };
	ioctl(t->fd, TIOCSWINSZ, &ws);
	if (t->view > t->sb_count)
		t->view = t->sb_count;
	t->dirty = true;
}

void tab_write(struct tab *t, const char *s, size_t len)
{
	while (len > 0 && t->used) {
		ssize_t n = write(t->fd, s, len);
		if (n > 0) {
			s += n;
			len -= n;
		} else if (n < 0 && errno == EINTR) {
			continue;
		} else if (n < 0 && errno == EAGAIN) {
			struct pollfd p = { .fd = t->fd, .events = POLLOUT };
			if (poll(&p, 1, 200) <= 0)
				return; /* le programme ne lit plus : on abandonne */
		} else {
			return;
		}
	}
}

void tab_read(struct tab *t)
{
	static char buf[65536];
	size_t total = 0;
	while (t->used && total < 4u << 20) {
		ssize_t n = read(t->fd, buf, sizeof buf);
		if (n > 0) {
			vterm_input_write(t->vt, buf, n);
			total += n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && errno == EAGAIN)
			break;
		/* EIO ou fin : le programme est terminé */
		int st;
		waitpid(t->pid, &st, WNOHANG);
		t->pid = 0;
		tab_close(t);
		A.dirty = true;
		request_redraw();
		return;
	}
	if (t->used)
		vterm_screen_flush_damage(t->screen);
}

/* titre de l'onglet : programme au premier plan + dossier courant */
void tab_refresh_label(struct tab *t)
{
	if (!t->used)
		return;
	char old[sizeof t->label];
	snprintf(old, sizeof old, "%s", t->label);
	if (t->title[0]) {
		snprintf(t->label, sizeof t->label, "%s", t->title);
	} else {
		pid_t pg = tcgetpgrp(t->fd);
		if (pg <= 0)
			pg = t->pid;
		char path[64], comm[32] = "", cwd[PATH_MAX] = "";
		snprintf(path, sizeof path, "/proc/%d/comm", (int)pg);
		FILE *f = fopen(path, "r");
		if (f) {
			if (fgets(comm, sizeof comm, f))
				comm[strcspn(comm, "\n")] = '\0';
			fclose(f);
		}
		snprintf(path, sizeof path, "/proc/%d/cwd", (int)pg);
		ssize_t n = readlink(path, cwd, sizeof cwd - 1);
		cwd[n > 0 ? n : 0] = '\0';
		const char *home = getenv("HOME");
		char shown[PATH_MAX];
		size_t hl = home ? strlen(home) : 0;
		if (hl && !strncmp(cwd, home, hl) && (cwd[hl] == '/' || cwd[hl] == '\0'))
			snprintf(shown, sizeof shown, "~%s", cwd + hl);
		else
			snprintf(shown, sizeof shown, "%s", cwd);
		/* garde seulement le dernier dossier pour rester court */
		const char *last = strrchr(shown, '/');
		const char *dirname = (last && last[1]) ? last + 1 : shown;
		if (comm[0] && dirname[0])
			snprintf(t->label, sizeof t->label, "%s · %s", comm, dirname);
		else
			snprintf(t->label, sizeof t->label, "%s", comm[0] ? comm : "terminal");
	}
	if (strcmp(old, t->label))
		A.dirty = true;
}

/* ------------------------- lecture des cellules ---------------------- */

bool tab_cell(struct tab *t, int vrow, int col, struct cell *out)
{
	if (vrow < 0) {
		int idx = t->sb_count + vrow;
		if (idx < 0) {
			blank(out);
			return false;
		}
		struct sbline *l = &t->sb[(t->sb_head + idx) % SCROLLBACK];
		if (col < l->cols)
			*out = l->cells[col];
		else
			blank(out);
		return true;
	}
	if (vrow >= t->rows || col >= t->cols) {
		blank(out);
		return false;
	}
	VTermScreenCell vc;
	if (!vterm_screen_get_cell(t->screen, (VTermPos){ vrow, col }, &vc)) {
		blank(out);
		return false;
	}
	from_vcell(t, &vc, out);
	return true;
}

bool tab_line_cont(struct tab *t, int vrow)
{
	if (vrow < 0) {
		int idx = t->sb_count + vrow;
		return idx >= 0 && t->sb[(t->sb_head + idx) % SCROLLBACK].cont;
	}
	if (vrow >= t->rows)
		return false;
	const VTermLineInfo *li = vterm_state_get_lineinfo(t->state, vrow);
	return li && li->continuation;
}

static size_t put_utf8(char *o, uint32_t c)
{
	if (c < 0x80) { o[0] = c; return 1; }
	if (c < 0x800) { o[0] = 0xC0 | (c >> 6); o[1] = 0x80 | (c & 63); return 2; }
	if (c < 0x10000) { o[0] = 0xE0 | (c >> 12); o[1] = 0x80 | ((c >> 6) & 63); o[2] = 0x80 | (c & 63); return 3; }
	o[0] = 0xF0 | (c >> 18); o[1] = 0x80 | ((c >> 12) & 63); o[2] = 0x80 | ((c >> 6) & 63); o[3] = 0x80 | (c & 63);
	return 4;
}

/* texte entre deux positions (incluses), lignes coupées recollées */
char *tab_text(struct tab *t, struct vpos a, struct vpos b)
{
	if (a.row > b.row || (a.row == b.row && a.col > b.col)) {
		struct vpos x = a; a = b; b = x;
	}
	size_t cap = (size_t)(b.row - a.row + 1) * (t->cols + 1) * 4 + 1, n = 0;
	char *out = malloc(cap);
	if (!out)
		return NULL;
	for (int r = a.row; r <= b.row; r++) {
		int c0 = r == a.row ? a.col : 0;
		int c1 = r == b.row ? b.col : t->cols - 1;
		size_t line_start = n, last_nonblank = n;
		for (int c = c0; c <= c1 && c < t->cols; c++) {
			struct cell cl;
			tab_cell(t, r, c, &cl);
			if (cl.ch == CONT_CELL)
				continue;
			uint32_t ch = cl.ch ? cl.ch : ' ';
			n += put_utf8(out + n, ch);
			if (ch != ' ')
				last_nonblank = n;
		}
		bool next_cont = r < b.row && tab_line_cont(t, r + 1);
		if (!next_cont) {
			if (last_nonblank < line_start) last_nonblank = line_start;
			n = last_nonblank;
			if (r < b.row)
				out[n++] = '\n';
		}
	}
	out[n] = '\0';
	return out;
}

void tab_scroll_view(struct tab *t, int delta)
{
	if (t->altscreen)
		return;
	int v = t->view + delta;
	if (v < 0) v = 0;
	if (v > t->sb_count) v = t->sb_count;
	if (v != t->view) {
		t->view = v;
		t->dirty = true;
		request_redraw();
	}
}
